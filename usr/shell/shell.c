/**
 * \file
 * \brief main command-line interface (shell)
 */

#define DBG_SHELL    0
#define DBG_READLINE 0

#include "shell.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <fs/fs.h>
#include <fs/dirent.h>

#include "session/session.h"
#include "tty/readline.h"
#include "tty/tty.h"
#include "cmdparse/cmdbuiltins.h"
#include "pathutil/pathutil.h"

static void _tab_complete_cleanup(struct shell_session              *session,
                                  struct shell_tab_complete_results *results)
{
    (void)session;
    // XXX results are keys of the trie, thus we do not free them.
    if (results->optc > 0 && results->optv != NULL) {
        free(results->optv);
    }
}

static void _tab_complete_cleanup_rec(struct shell_session              *session,
                                      struct shell_tab_complete_results *results)
{
    (void)session;
    if (results->optc > 0 && results->optv != NULL) {
        for (size_t i = 0; i < results->optc; ++i) {
            free(results->optv[i]);
        }
        _tab_complete_cleanup(session, results);
    }
}

static void tab_complete_cmd(struct shell_session *session, struct parsed_autocomplete *pa,
                             struct shell_tab_complete_results *results)
{
    results->optc = trie_collect(&session->cmds, pa->buf, &results->optv);
}

static void tab_complete_arg(struct shell_session *session, struct parsed_autocomplete *pa,
                             struct shell_tab_complete_results *results)
{
    if (SHELL_TAB_COMPLETE_FILENAMES) {
        struct dynamic_array da;
        da_init(&da, sizeof(char *) * 10);
        int num_matches = 0;

        // XXX currently we only tab complete files in the current directory
        fs_dirhandle_t dh;
        errval_t       err = opendir(session_wd(session), &dh);
        if (dh == NULL) {
            goto empty_tab_complete;
        }
        do {
            char *name;
            err = readdir(dh, &name);
            if (err_no(err) == FS_ERR_INDEX_BOUNDS) {
                break;
            } else {
                /// XXX in this case we are (potentially) leaking memory
                if (err_is_fail(err))
                    goto empty_tab_complete;
            }
            if (strncmp(name, pa->buf, strlen(pa->buf)) == 0) {
                ++num_matches;
                char *cpy = malloc(strlen(name) + 1);
                strcpy(cpy, name);
                da_append(&da, sizeof(char *), &cpy);
            }
        } while (err_is_ok(err));
        err = closedir(dh);

        results->optc    = num_matches;
        results->optv    = da_release(&da);
        results->done_fn = _tab_complete_cleanup_rec;
    } else {
    empty_tab_complete:
        (void)session;
        (void)pa;
        results->optc    = 0;
        results->optv    = NULL;
        results->done_fn = _tab_complete_cleanup;
    }
}

static void tab_complete_var(struct shell_session *session, struct parsed_autocomplete *pa,
                             struct shell_tab_complete_results *results)
{
    char **opts   = NULL;
    results->optc = trie_collect(&session->vars, pa->buf, &opts);
    if (results->optc == 1 && strcmp(opts[0], pa->buf) == 0) {
        // single result matching a variable: replace by value
        results->optv    = malloc(sizeof(char *));
        results->optv[0] = trie_lookup(&session->vars, pa->buf);
        results->done_fn = _tab_complete_cleanup;
    } else {
        // we need to transform VAR into $VAR...
        results->optv = malloc(sizeof(char *) * results->optc);
        for (size_t i = 0; i < results->optc; ++i) {
            size_t len          = strlen(opts[i]);
            results->optv[i]    = malloc(sizeof(char) * (len + 2));
            results->optv[i][0] = '$';
            memcpy(results->optv[i] + 1, opts[i], len);
            results->optv[i][len + 1] = '\0';
        }
        results->done_fn = _tab_complete_cleanup_rec;
    }
    free(opts);
}

static void tab_complete(struct shell_session *session, struct parsed_autocomplete *pa,
                         struct shell_tab_complete_results *results)
{
    if (pa->mode == PARSE_MODE_COMMAND) {
        return tab_complete_cmd(session, pa, results);
    } else if (pa->mode == PARSE_MODE_ARGUMENT) {
        return tab_complete_arg(session, pa, results);
    } else if (pa->mode == PARSE_MODE_VARIABLE) {
        return tab_complete_var(session, pa, results);
    }
}

int shell_launch_session(void)
{
    errval_t             err = SYS_ERR_OK;
    struct shell_session session;
    session_init(&session, "~$ ", tab_complete);
    cmd_register_builtins(&session);

    do {
        char                *line = shell_read_line(&session);
        struct parsed_define def;
        err = cmd_parse_define(&session.vars, line, &def);
        if (err_is_fail(err) && err_no(err) != CMDPARSE_ERR_NOT_DEFINE) {
            if (err_no(err) == CMDPARSE_ERR_UNKNOWN_VARIABLE) {
                printf("%sshell: unknown variable: `$%s`\n%s", TTY_COLOR_BOLD_RED, def.err_str,
                       TTY_COLOR_RESET);
            } else {
                printf("%sillegal variable definition.\n%sNOTE: declaring variables as part of a "
                       "command is not supported.%s\n",
                       TTY_COLOR_BOLD_RED, TTY_COLOR_RED_BG, TTY_COLOR_RESET);
            }
            continue;
        }
        if (def.key != NULL) {
            printf("define: %s := '%s'\n", def.key, def.value);
            trie_insert(&session.vars, def.key, def.value);
            continue;
        }

        struct parsed_command_pipeline pl;
        err = cmd_parse_line(&session.vars, line, &pl);
        if (err_is_fail(err)) {
            if (err_no(err) == CMDPARSE_ERR_UNKNOWN_VARIABLE) {
                printf("%sshell: unknown variable: `$%s`\n%s", TTY_COLOR_BOLD_RED, pl.err_str,
                       TTY_COLOR_RESET);
            } else if (err_no(err) == CMDPARSE_ERR_MISSING_QUOTE) {
                printf("%sshell: unterminated quoted string.\n%s", TTY_COLOR_BOLD_RED,
                       TTY_COLOR_RESET);
            } else if (err_no(err) == CMDPARSE_ERR_MISSING_ESCAPE) {
                printf("%sshell: unterminated escape sequence.\n%s", TTY_COLOR_BOLD_RED,
                       TTY_COLOR_RESET);
            } else if (err_no(err) == CMDPARSE_ERR_VAR_AS_CMD) {
                printf("%sshell: attempting to use a variable as part of a command.\n%s",
                       TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
            }
            err = command_pipeline_deinit(&pl);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "command_pipeline_deinit failed.");
            }
            continue;
        }
        if (pl.size == 1 && strcmp(pl.cmds[0].command, "") == 0) {
            // empty command, remove from history and continue
            da_pop(&session.history, sizeof(struct history_item));
            continue;
        }
#if DBG_SHELL
        printf("[Parsed %zu Command(s)]\n", pl.size);
        for (size_t i = 0; i < pl.size; ++i) {
            char op = (i == 0) ? '-' : pl.ops[i - 1];
            printf("  %c Command(%d) = %s\n", op, i + 1, pl.cmds[i].command);
            for (size_t j = 0; j < pl.cmds[i].argc; ++j) {
                printf("     <argv[%d]='%s'>\n", j, pl.cmds[i].argv[j]);
            }
        }
#endif
        cmd_dispatch_commands(&session, &pl);
        err = command_pipeline_deinit(&pl);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "command_pipeline_deinit failed.");
        }
    } while (true);

    return EXIT_SUCCESS;
}