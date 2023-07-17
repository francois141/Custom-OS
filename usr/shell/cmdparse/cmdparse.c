#include "cmdparse.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <aos/debug.h>

#include "../containers/dynamic_array.h"
#include "../containers/trie.h"
#include "../containers/gap_buffer.h"

struct parse_state {
    enum parse_mode mode;
    bool            escaped;   ///< handles backslash
    bool            enquoted;  ///<  handles quotes

    size_t cursor;

    struct dynamic_array cmds;
    string_builder_t     operators;

    struct dynamic_array curr_args;

    size_t           curr_cmd_beg;
    string_builder_t curr_cmd;  ///< command we are currently parsing

    size_t           curr_arg_beg;
    string_builder_t curr_arg;  ///< argument we are currently parsing

    size_t           curr_var_beg;
    string_builder_t curr_var;  ///< variable we are currently parsing

    struct trie *vars;          ///< variables in the current context

    char *err_str;              ///< contains string specific to the current error or NULL
};

static inline bool

_is_white_space(char c)
{
    return c == ' ';
}

static inline bool

_is_escape_char(char c)
{
    return c == '\\';
}

static inline bool

_is_quote_char(char c)
{
    return c == '"';
}

static inline bool

_is_var_char(char c)
{
    return c == '$';
}

static inline bool

_is_operator_char(char c)
{
    return c == '|' || c == '>' || c == '<' || c == ';';
}

static inline bool

_peak_operator(struct parse_state *state, char *line)
{
    char c = line[state->cursor];
    if (c != '&' && c != '|') {
        return false;
    }
    return c == line[state->cursor + 1];
}

static void _pst_append_char(struct parse_state *state, char c)
{
    if (state->mode == PARSE_MODE_COMMAND) {
        sb_append_char(&state->curr_cmd, c);
    } else if (state->mode == PARSE_MODE_ARGUMENT) {
        sb_append_char(&state->curr_arg, c);
    } else if (state->mode == PARSE_MODE_VARIABLE) {
        sb_append_char(&state->curr_var, c);
    }
}

static errval_t _pst_push_argument(struct parse_state *state, bool force, bool var_only)
{
    if (state->mode == PARSE_MODE_VARIABLE) {
        if (force || state->curr_var.size > 0) {
            char *variable = sb_release_to_cstr(&state->curr_var);
            char *value    = trie_lookup(state->vars, variable);
            if (value == NULL) {
                state->err_str = variable;
                return CMDPARSE_ERR_UNKNOWN_VARIABLE;
            }
            free(variable);
            sb_append_str(&state->curr_arg, value);
        }
        state->mode = PARSE_MODE_ARGUMENT;
    }
    if (!var_only && (force || state->curr_arg.size > 0)) {
        char *argument = sb_release_to_cstr(&state->curr_arg);
        da_append(&state->curr_args, sizeof(char *), &argument);
    }
    return SYS_ERR_OK;
}

static errval_t _pst_push_command(struct parse_state *state)
{
    errval_t err = SYS_ERR_OK;
    err          = _pst_push_argument(state, /*force=*/false, false);
    if (err_is_fail(err)) {
        return err;
    }
    if (state->enquoted) {
        return CMDPARSE_ERR_MISSING_QUOTE;
    }
    if (state->escaped) {
        return CMDPARSE_ERR_MISSING_ESCAPE;
    }
    int                   argc = state->curr_args.size / sizeof(char *);
    struct parsed_command cmd  = { .command = sb_release_to_cstr(&state->curr_cmd),
                                   .argc    = argc,
                                   .argv    = da_release(&state->curr_args) };
    da_append(&state->cmds, sizeof(struct parsed_command), &cmd);
    state->mode = PARSE_MODE_COMMAND;
    return SYS_ERR_OK;
}

static errval_t _pst_parse_char(struct parse_state *state, char *line)
{
    errval_t err = SYS_ERR_OK;
    int      i   = state->cursor;
    char     c   = line[i];
    if (state->escaped) {
        if (c == 'n') {
            _pst_append_char(state, '\n');
        } else if (c == 't') {
            _pst_append_char(state, '\t');
        } else {
            /// XXX we may want to emit a warning (if we don't know the escape code.)
            _pst_append_char(state, c);
        }
        state->escaped = false;
    } else if (_is_white_space(c) && !state->enquoted) {
        if (state->mode == PARSE_MODE_COMMAND) {
            if (state->curr_cmd.size > 0) {  // ignore leading whitespace
                state->curr_arg_beg = i + 1;
                state->mode         = PARSE_MODE_ARGUMENT;
            } else {
                state->curr_cmd_beg = i + 1;
            }
        } else if (state->mode == PARSE_MODE_ARGUMENT || state->mode == PARSE_MODE_VARIABLE) {
            err = _pst_push_argument(state, /*force=*/false, false);
            if (err_is_fail(err)) {
                return err;  // e.g., unknown variable encountered
            }
            state->curr_arg_beg = i + 1;
        }
    } else if (_is_escape_char(c)) {
        state->escaped = true;
    } else if (_is_quote_char(c)) {
        if (state->mode == PARSE_MODE_VARIABLE) {
            // if (!state->enquoted) {
            /// push the value of the current argument
            err = _pst_push_argument(state, false, true);
            if (err_is_fail(err)) {
                return err;
            }
            // }
            state->mode = PARSE_MODE_ARGUMENT;
        }
        state->enquoted = !state->enquoted;
    } else if (_is_var_char(c)) {
        if (state->mode == PARSE_MODE_ARGUMENT) {
            state->curr_var_beg = i;
            state->mode         = PARSE_MODE_VARIABLE;
        } else {
            return CMDPARSE_ERR_VAR_AS_CMD;
        }
    } else if (_peak_operator(state, line) || _is_operator_char(c)) {
        err = _pst_push_command(state);
        if (err_is_fail(err)) {
            return err;
        }
        if (_peak_operator(state, line)) {
            /// XXX we expect every operator to take a single symbol, thus we use designated ones.
            sb_append_char(&state->operators, c == '&' ? CMD_OPERATOR_LAND : CMD_OPERATOR_LOR);
            ++state->cursor;
            state->curr_cmd_beg = i + 2;
        } else {
            sb_append_char(&state->operators, c);
            state->curr_cmd_beg = i + 1;
        }
    } else {
        // XXX do we want to perform additional checks on the character?
        _pst_append_char(state, c);
    }
    ++state->cursor;
    return SYS_ERR_OK;
}

static errval_t _pst_parse_n(struct parse_state *state, char *line, size_t n)
{
    errval_t err = SYS_ERR_OK;
    while (state->cursor < n) {
        err = _pst_parse_char(state, line);
        if (err_is_fail(err)) {
            return err;
        }
    }
    return err;
}

static void _pst_skip(struct parse_state *state, char *line)
{
    size_t len = strlen(line);
    while (state->cursor < len) {
        char c = line[state->cursor];
        if (state->escaped) {
            // _pst_append_char(state, c);
            state->escaped = false;
        } else if (_is_white_space(c) && !state->enquoted) {
            if (state->mode == PARSE_MODE_COMMAND) {
                if (state->curr_cmd.size > 0) {  // ignore leading whitespace
                    state->mode = PARSE_MODE_ARGUMENT;
                    break;                       // begin the first argument
                }
            } else if (state->mode == PARSE_MODE_ARGUMENT || state->mode == PARSE_MODE_VARIABLE) {
                state->mode = PARSE_MODE_ARGUMENT;
                break;  // begin the next argument
            }
        } else if (_is_escape_char(c)) {
            state->escaped = true;
        } else if (_is_quote_char(c)) {
            if (state->mode == PARSE_MODE_VARIABLE) {
                if (!state->enquoted) {
                    state->mode = PARSE_MODE_ARGUMENT;
                }
            }
            state->enquoted = !state->enquoted;
        } else if (_is_var_char(c)) {
            state->mode = PARSE_MODE_VARIABLE;
            // for now we keep the variable as part of the argument for completion.
            // break;
        } else if (_peak_operator(state, line)) {
            ++state->cursor;
            state->mode = PARSE_MODE_COMMAND;
        } else if (_is_operator_char(c)) {
            state->mode = PARSE_MODE_COMMAND;
        } else {
            // we don't need to do anything here...
        }
        ++state->cursor;
    }
}

static void _pst_init_state(struct parse_state *state, struct trie *vars)
{
    state->mode   = PARSE_MODE_COMMAND;
    state->cursor = 0;

    state->escaped  = false;
    state->enquoted = false;
    state->vars     = vars;

    da_init(&state->cmds, sizeof(struct parsed_command));
    sb_init(&state->operators);

    da_init(&state->curr_args, sizeof(char *));

    state->curr_cmd_beg = 0;
    sb_init(&state->curr_cmd);

    state->curr_arg_beg = 0;
    sb_init(&state->curr_arg);

    state->curr_var_beg = 0;
    sb_init(&state->curr_var);

    state->err_str = NULL;
}

static void _pst_free_state(struct parse_state *state)
{
    size_t                 cmdc = state->cmds.size / sizeof(struct parsed_command);
    struct parsed_command *cmds = state->cmds.buf;
    for (size_t i = 0; i < cmdc; ++i) {
        free(cmds[i].command);
        for (size_t j = 0; j < cmds[i].argc; ++j) {
            free(cmds[i].argv[j]);
        }
        free(cmds[i].argv);
    }
    size_t cargc = state->curr_args.size / sizeof(char *);
    char **cargs = state->curr_args.buf;
    for (size_t i = 0; i < cargc; ++i) {
        free(cargs[i]);
    }
    da_free(&state->operators);
    da_free(&state->curr_cmd);
    da_free(&state->curr_arg);
    da_free(&state->curr_var);
}

errval_t cmd_parse_line(struct trie *vars, char *line, struct parsed_command_pipeline *pl)
{
    errval_t     err = SYS_ERR_OK;
    const size_t len = strlen(line);

    struct parse_state state;
    _pst_init_state(&state, vars);
    err = _pst_parse_n(&state, line, len);
    if (err_is_fail(err)) {
        pl->err_str = state.err_str;
        return err;
    }
    err = _pst_push_command(&state);
    if (err_is_fail(err)) {
        pl->err_str = state.err_str;
        return err;
    }

    size_t cmdc = state.cmds.size / sizeof(struct parsed_command);
    pl->err_str = NULL;
    pl->size    = cmdc;
    pl->cmds    = (struct parsed_command *)da_release(&state.cmds);
    pl->ops     = sb_release_to_cstr(&state.operators);
    return SYS_ERR_OK;
}

struct parsed_autocomplete cmd_autocomplete(struct trie *vars, char *line, size_t cursor)
{
    size_t len = strlen(line);
    assert(cursor <= len);

    struct parse_state state;
    _pst_init_state(&state, vars);

    // parse everything up to the cursor
    _pst_parse_n(&state, line, cursor);

    enum parse_mode mode = state.mode;
    char *compl = NULL, *context = NULL;
    size_t begin = 0, end = 0, position = 0;
    if (mode == PARSE_MODE_COMMAND) {
        char *command = sb_release_to_cstr(&state.curr_cmd);
        compl         = command;
        begin         = state.curr_cmd_beg;
    } else if (mode == PARSE_MODE_ARGUMENT) {
        char *command  = sb_release_to_cstr(&state.curr_cmd);
        position       = state.curr_args.size / sizeof(char *);
        char *argument = sb_release_to_cstr(&state.curr_arg);
        compl          = argument;
        context        = command;
        begin          = state.curr_arg_beg;
    } else if (mode == PARSE_MODE_VARIABLE) {
        char *command  = sb_release_to_cstr(&state.curr_cmd);
        char *variable = sb_release_to_cstr(&state.curr_var);
        compl          = variable;
        context        = command;
        begin          = state.curr_var_beg;
    }
    _pst_skip(&state, line);
    end = state.cursor;

    _pst_free_state(&state);
    struct parsed_autocomplete pa = {
        .mode = mode, .ctx = context, .buf = compl, .begin = begin, .end = end, .position = position
    };
    return pa;
}

static bool _cmd_is_valid_varname_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

errval_t cmd_parse_define(struct trie *vars, char *command, struct parsed_define *pd)
{
    // check if we have the form ([a-zA-Z_])+=
    size_t len    = strlen(command);
    size_t kindex = 0;
    bool   valid  = true;
    for (; kindex < len; ++kindex) {
        char c = command[kindex];
        if (c == '=') {
            break;
        } else if (!_cmd_is_valid_varname_char(c)) {
            valid = false;
        }
    }
    if (kindex == len) {
        // XXX we did not find a define.
        // struct parsed_define pd = { .valid = true, .key = NULL, .value = NULL };
        pd->valid = true;
        pd->key   = NULL;
        pd->value = NULL;
        return CMDPARSE_ERR_NOT_DEFINE;
    }

    // XXX we found a potential key, but it is invalid
    if (!valid || kindex == 0) {
        // struct parsed_define pd = { .valid = false };
        pd->valid = false;
        return CMDPARSE_ERR_ILLEGAL_VARNAME;
    }

    struct parse_state state;
    _pst_init_state(&state, vars);
    state.mode   = PARSE_MODE_ARGUMENT;
    state.cursor = kindex + 1;
    while (state.cursor < len) {
        _pst_parse_char(&state, command);
        if (state.mode == PARSE_MODE_COMMAND || state.curr_args.size > 0) {
            // XXX we found an invalid value
            _pst_free_state(&state);
            // struct parsed_define pd = { .valid = false };
            pd->valid = false;
            return CMDPARSE_ERR_ILLEGAL_VARVALUE;
        }
    }
    if (state.escaped || state.enquoted) {
        // XXX we found an invalid value
        _pst_free_state(&state);
        // struct parsed_define pd = { .valid = false };
        pd->valid = false;
        return CMDPARSE_ERR_ILLEGAL_VARVALUE;
    }
    errval_t err = _pst_push_argument(&state, true, false);
    if (err_is_fail(err)) {
        // struct parsed_define pd = { .valid = false };
        pd->valid   = false;
        pd->err_str = state.err_str;
        return err;  ///< unknown variable
    }
    char  *arg    = ((char **)state.curr_args.buf)[0];
    size_t arglen = strlen(arg);
    char  *value  = malloc(sizeof(char) * (arglen + 1));
    memcpy(value, arg, arglen);
    value[arglen] = '\0';
    _pst_free_state(&state);

    // XXX extract the key
    char *key = malloc(sizeof(char) * (kindex + 1));
    memcpy(key, command, kindex);
    key[kindex] = '\0';

    // struct parsed_define pd = { .valid = true, .key = key, .value = value };
    pd->valid = true;
    pd->key   = key;
    pd->value = value;
    return SYS_ERR_OK;
}

static void _pst_push_color(struct dynamic_array *colors, size_t begin, const char *color)
{
    cmdline_color_t cmdcolor = { .begin = begin, .color = color };
    da_append(colors, sizeof(cmdline_color_t), &cmdcolor);
}

static void _pst_color_char(struct parse_state *state, char *line, struct dynamic_array *colors,
                            bool *file_op, bool *cmd_empty)
{
    // static const char *color_reset = "\033[0m";
    static const char *color_cmd      = "\033[32m";
    static const char *color_file     = "\033[96m";
    static const char *color_argument = "\033[0m";
    static const char *color_var      = "\033[35m";
    static const char *color_operator = "\033[34m";
    static const char *color_escape   = "\033[36m";
    static const char *color_quote    = "\033[33m";

    int i = state->cursor;
    if (i == 0) {
        _pst_push_color(colors, i, color_cmd);
    }
    char c = line[i];
    if (state->escaped) {
        // _pst_append_char(state, c);
        state->escaped = false;
    } else if (_is_white_space(c) && !state->enquoted) {
        if (state->mode == PARSE_MODE_COMMAND) {
            if (!(*cmd_empty)) {  // ignore leading whitespace
                state->curr_arg_beg = i + 1;
                state->mode         = PARSE_MODE_ARGUMENT;
                _pst_push_color(colors, i + 1, color_argument);
            }
        } else if (state->mode == PARSE_MODE_ARGUMENT || state->mode == PARSE_MODE_VARIABLE) {
            _pst_push_color(colors, i + 1, color_argument);
            state->mode = PARSE_MODE_ARGUMENT;
        }
    } else if (_is_escape_char(c)) {
        // XXX we need to get the last active color
        assert(colors->size > 0);
        size_t           len  = colors->size / sizeof(cmdline_color_t);
        cmdline_color_t *buf  = colors->buf;
        const char      *prev = buf[len - 1].color;

        _pst_push_color(colors, i, color_escape);
        state->escaped = true;
        _pst_push_color(colors, i + 2, prev);
    } else if (_is_quote_char(c)) {
        if (state->mode == PARSE_MODE_VARIABLE && state->enquoted) {
            _pst_push_color(colors, i, color_quote);
        }
        state->enquoted = !state->enquoted;
        if (state->enquoted) {
            _pst_push_color(colors, i, color_quote);
        } else {
            _pst_push_color(colors, i + 1, color_argument);
        }
    } else if (_is_var_char(c)) {
        if (state->mode == PARSE_MODE_ARGUMENT) {
            state->curr_var_beg = i;
            state->mode         = PARSE_MODE_VARIABLE;
        }
        _pst_push_color(colors, i, color_var);
    } else if (_peak_operator(state, line) || _is_operator_char(c)) {
        _pst_push_color(colors, i, color_operator);
        state->mode = PARSE_MODE_COMMAND;
        *file_op    = false;
        if (_peak_operator(state, line)) {
            state->curr_cmd_beg = i + 2;
            _pst_push_color(colors, i + 2, color_cmd);
            ++state->cursor;
        } else {
            *file_op            = (c == '>' || c == '<');
            state->curr_cmd_beg = i + 1;
            if (!*file_op) {
                _pst_push_color(colors, i + 1, color_cmd);
            } else {
                _pst_push_color(colors, i + 1, color_file);
            }
        }
        *cmd_empty = true;
    } else {
        // XXX do we want to perform additional checks on the character?
        if (state->mode == PARSE_MODE_COMMAND) {
            *cmd_empty = false;
        }
    }
    ++state->cursor;
}

static void _pst_color_n(struct parse_state *state, char *line, size_t n,
                         struct dynamic_array *colors)
{
    bool cmd_empty = true;
    bool file_op   = false;
    while (state->cursor < n) {
        _pst_color_char(state, line, colors, &file_op, &cmd_empty);
    }
}

size_t cmdline_color(struct trie *vars, char *line, cmdline_color_t **colors)
{
    errval_t             err = SYS_ERR_OK;
    struct dynamic_array da;
    struct parsed_define def;
    err = cmd_parse_define(vars, line, &def);
    if (err_no(err) != CMDPARSE_ERR_NOT_DEFINE) {
        // we have a define, color appropriately
        size_t i = 0;
        for (; line[i] != '\0' && line[i] != '='; ++i)
            ;
        bool enquoted = line[i] != '\0' && line[i + 1] == '"';
        da_init(&da, sizeof(cmdline_color_t) * (2 + enquoted ? 1 : 0));
        _pst_push_color(&da, 0, /*var_color*/ "\033[35m");
        _pst_push_color(&da, i, "\033[0m");
        if (enquoted) {
            _pst_push_color(&da, i + 1, "\033[33m");
        }
        *colors = da.buf;
        return 2 + (enquoted ? 1 : 0);
    }

    da_init(&da, sizeof(cmdline_color_t) * 8);

    struct parse_state state;
    _pst_init_state(&state, vars);
    _pst_color_n(&state, line, strlen(line), &da);

    _pst_free_state(&state);

    size_t len = da.size / sizeof(cmdline_color_t);
    *colors    = da.buf;
    return len;
}


char *cmdline_apply_colors(const char *line, size_t begin, size_t len, size_t count,
                           cmdline_color_t *colors)
{
    // XXX we require that colors take at most 5 characters!
    char  *buf = malloc(len + (count + 1) * sizeof(char) * 5);
    size_t bi = 0, li = begin, j = 0;
    for (; li < begin + len;) {
        while (j < count && colors[j].begin <= li) {
            assert(strlen(colors[j].color) <= 5);
            memcpy(buf + bi, colors[j].color, strlen(colors[j].color));
            bi += strlen(colors[j].color);
            ++j;
        }
        buf[bi++] = line[li++];
    }
    // end with a reset color
    static const char *color_reset = "\033[0m";
    memcpy(buf + bi, color_reset, strlen(color_reset));
    bi += strlen(color_reset);
    buf[bi] = '\0';
    return buf;
}

errval_t command_pipeline_deinit(struct parsed_command_pipeline *pl)
{
    if (pl->cmds != NULL) {
        for (size_t i = 0; i < pl->size; ++i) {
            if (pl->cmds[i].command != NULL)
                free(pl->cmds[i].command);
            for (size_t j = 0; j < pl->cmds[i].argc; ++j) {
                free(pl->cmds[i].argv[j]);
            }
        }
        free(pl->cmds);
    }
    if (pl->ops != NULL)
        free(pl->ops);
    if (pl->err_str != NULL)
        free(pl->err_str);
    return SYS_ERR_OK;
}
