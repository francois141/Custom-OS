#include "readline.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <aos/deferred.h>

#include "tty.h"

static void shell_update_line(struct shell_session *session)
{
    sb_clear(&session->line_buf);
    sb_append_str(&session->line_buf, "\x1b[?25l");  // hide the cursor.
    sb_append_str(&session->line_buf, "\r");         // move to the beginning of the line
    sb_append_str(&session->line_buf, TTY_COLOR_BLUE);
    sb_append_buf(&session->line_buf, session->prompt_len, session->prompt);
    sb_append_str(&session->line_buf, TTY_COLOR_RESET);

    size_t cursor  = *session_cursor(session);
    size_t vcursor = *session_vcursor(session);

    char  *buf = session_line_cstr(session);
    size_t len = MIN(session_line_len(session) - (cursor - vcursor), session->colwidth + 1);

#if SHELL_CMDLINE_COLORS
    cmdline_color_t *colors;
    size_t           ccount = cmdline_color(&session->vars, buf, &colors);
    char            *cbuf   = cmdline_apply_colors(buf, cursor - vcursor, len, ccount, colors);
    sb_append_buf(&session->line_buf, strlen(cbuf), cbuf);
    free(cbuf);
#else
    sb_append_buf(&session->line_buf, len, buf + (cursor - vcursor));
#endif

    // make sure to not clear unnecessarily: just clear everything we did not override.
    sb_append_str(&session->line_buf, "\x1b[K");  // clear everything after the cursor

    static char RESET_BUF[32];
    snprintf(RESET_BUF, 32, "\x1b[%zuG",
             session->prompt_len + vcursor + 1);  // move the cursor to current position
    sb_append_str(&session->line_buf, RESET_BUF);

    sb_append_str(&session->line_buf, "\x1b[?25h");  // show the cursor.
    char *newline = sb_to_cstr(&session->line_buf);
    tty_write(strlen(newline), newline);
    tty_flush();
}

static void _shell_deinit_autocomplete(struct shell_session *session)
{
    if (session->tab_complete_results.done_fn != NULL) {
        session->tab_complete_results.done_fn(session, &session->tab_complete_results);
    }
    session->tab_complete_results.optc     = 0;
    session->tab_complete_results.optv     = NULL;
    session->tab_complete_results.position = 0;
    session->tab_complete_results.done_fn  = NULL;
}

static bool shell_tab_complete(struct shell_session *session)
{
    char *line = session_line_cstr(session);
    struct parsed_autocomplete pa = cmd_autocomplete(&session->vars, line, *session_cursor(session));
    assert((pa.ctx != NULL) ^ (pa.mode == PARSE_MODE_COMMAND));

#if DBG_READLINE
    {
        _shell_clear_line();
        printf("state: matching '%s' [context: '%s'] on '", pa.buf, pa.ctx);
        size_t i = 0;
        for (i = 0; i < len; ++i) {
            if (i == pa.begin)
                printf("[");
            if (i == pa.end)
                printf("]");
            printf("%c", line[i]);
        }
        if (i == pa.end)
            printf("]");
        printf("'\n");
    }
#endif
    if (session->tab_complete_mode != pa.mode) {
        _shell_deinit_autocomplete(session);
        session->tab_complete_mode = pa.mode;
    }

    // do we need to compute the possible completions?
    if (session->tab_complete_results.optv == NULL) {
        session->tab_complete_fn(session, &pa, &session->tab_complete_results);
        if (session->tab_complete_results.optv == NULL || session->tab_complete_results.optc == 0) {
            // NOTE returning NULL means "do nothing"
            return false;
        }
        session->tab_complete_results.position = 0;
        // erase the current string
        gb_erase_at(session_edit_line(session), pa.begin, pa.end - pa.begin);
        size_t backward = *session_cursor(session) - pa.begin;
        *session_cursor(session) -= backward;
        session_move_vcursor_backward(session, backward);
    } else {
        char  *prev     = session->tab_complete_results.optv[session->tab_complete_results.position
                                                        % session->tab_complete_results.optc];
        size_t backward = *session_cursor(session) - pa.begin;
        gb_erase_at(session_edit_line(session), pa.begin, strlen(prev));
        *session_cursor(session) -= backward;
        session_move_vcursor_backward(session, backward);
        ++session->tab_complete_results.position;
    }

    char  *tab_complete = session->tab_complete_results.optv[session->tab_complete_results.position
                                                            % session->tab_complete_results.optc];
    size_t tab_len      = strlen(tab_complete);
    gb_insert_at(session_edit_line(session), *session_cursor(session), tab_len, tab_complete);

    // we submit if we only have a single option
    bool submit = session->tab_complete_results.optc == 1
                  && session->tab_complete_results.position == 0;
    if (submit) {
        // XXX we also insert a space at the end.
        gb_insert_char_at(session_edit_line(session), *session_cursor(session) + tab_len, ' ');
        _shell_deinit_autocomplete(session);
        session->tab_complete_mode = PARSE_MODE_NONE;
    }

    *session_cursor(session) += tab_len + (submit ? 1 : 0);
    *session_vcursor(session) = MIN(*session_vcursor(session) + tab_len + (submit ? 1 : 0),
                                    session->colwidth);

    if (*session_cursor(session) < session_line_len(session)
        || *session_vcursor(session) >= session->colwidth) {
        return true;
    }

    // XXX check for overlap, skip refresh then; for now we just erase everything
    return true;
}

static void _cursor_move_right(struct shell_session *session)
{
    if (*session_cursor(session) < session_line_len(session)) {
        ++*session_cursor(session);
        *session_vcursor(session) = MIN(*session_vcursor(session) + 1, session->colwidth);
        if (*session_vcursor(session) == session->colwidth) {
            shell_update_line(session);
        } else {
            tty_cursor_forward(1);
            tty_flush();
        }
    }
}

static void _cursor_move_left(struct shell_session *session)
{
    if (*session_cursor(session) > 0) {
        --*session_cursor(session);
        if (*session_vcursor(session) > 0 && *session_cursor(session) < *session_vcursor(session)) {
            --*session_vcursor(session);
        }
        if (*session_vcursor(session) == 0 || *session_vcursor(session) == session->colwidth) {
            shell_update_line(session);
        } else {
            tty_cursor_backward(1);
            tty_flush();
        }
    }
}

char *shell_read_line(struct shell_session *session)
{
    session_append_editable(session);
    size_t num_history         = (session->history.size / sizeof(struct history_item));
    session->hindex            = num_history - 1;
    session->tab_complete_mode = PARSE_MODE_NONE;
    _shell_deinit_autocomplete(session);
    shell_update_line(session);

    do {
        char c                  = tty_read_skip_multi_byte();
        bool reset_tab_complete = true;
        if (c == KEY_ENTER) {
            _shell_deinit_autocomplete(session);
            // XXX this is completely optional:
            //      - once we hit enter preserve the beginning of the line
            // *session_cursor(session) = *session_vcursor(session) = session->colwidth;
            shell_update_line(session);
            tty_write(1, "\n");
            tty_flush();
            break;
        } else if (c == KEY_CTRL_C) {
            free(session_edit_line(session)->buf);
            gb_init(session_edit_line(session));
            tty_write(1, "\n");
            tty_flush();
            break;
        } else if (c == KEY_CTRL_A) {
            // XXX moves the cursor to the beginning of the line
            *session_cursor(session)  = 0;
            *session_vcursor(session) = 0;
            shell_update_line(session);
        } else if (c == KEY_CTRL_E) {
            // XXX moves the cursor to the end of the line
            *session_cursor(session)  = session_line_len(session);
            *session_vcursor(session) = MIN(*session_cursor(session), session->colwidth);
            shell_update_line(session);
        } else if (c == KEY_CTRL_B) {
            // XXX move the cursor backward by one character
            _cursor_move_left(session);
        } else if (c == KEY_CTRL_F) {
            // XXX move the cursor forward by one character
            _cursor_move_right(session);
        } else if (c == KEY_CTRL_L) {
            tty_clear_screen();
            shell_update_line(session);
        } else if (c == KEY_CTRL_P) {
            if (session_history_up(session)) {
                shell_update_line(session);
            }
        } else if (c == KEY_CTRL_N) {
            if (session_history_down(session)) {
                shell_update_line(session);
            }
        } else if (c == KEY_CTRL_W) {
            // XXX delete last word
            bool update = false;
            while (*session_cursor(session) > 0
                   && (!update
                       || gb_to_cstr(session_edit_line(session))[*session_cursor(session) - 1]
                              != ' ')) {
                gb_erase_char_at(session_edit_line(session), --*session_cursor(session));
                session_move_vcursor_backward(session, 1);
                update = true;
            }
            if (update) {
                shell_update_line(session);
            }
        } else if (c == KEY_BACKSPACE || /* alternative backspace */ c == 8) {
            if (*session_cursor(session) > 0) {
                gb_erase_char_at(session_edit_line(session), --*session_cursor(session));
                session_move_vcursor_backward(session, 1);
                // NOTE we need to erase the entire line to "move everything back".
                shell_update_line(session);
            }
        } else if (c == KEY_TAB) {
            reset_tab_complete = false;
            if (shell_tab_complete(session)) {
                shell_update_line(session);
            }
        } else if (c == '\x1b') {  // ESCAPE CODE
            char escape_type = tty_read();
            char esc = tty_read();
            if (escape_type == '[') {
                if (esc == 'A') {  // UP ARROW
                    if (session_history_up(session)) {
                        shell_update_line(session);
                    }
                } else if (esc == 'B') {  // DOWN ARROW
                    if (session_history_down(session)) {
                        shell_update_line(session);
                    }
                } else if (esc == 'C') {  // RIGHT ARROW
                    _cursor_move_right(session);
                } else if (esc == 'D') {  // LEFT ARROW
                    _cursor_move_left(session);
                } else if (esc == '3') { // DELETE KEY
                    /// XXX somehow we receive additional characters on "delete"
                    tty_read(); // TODO(jlscheerer) why do we need this read?
                    if (*session_cursor(session) < session_line_len(session)) {
                        gb_erase_char_at(session_edit_line(session), *session_cursor(session));
                        shell_update_line(session);
                    }
                } else {
                    // printf("Unknown escape sequence: %d\n", esc);
                }
            }
        } else {
            gb_insert_char_at(session_edit_line(session), (*session_cursor(session))++, c);
            *session_vcursor(session) = MIN(*session_vcursor(session) + 1, session->colwidth);
#if SHELL_CMDLINE_COLORS
            /// XXX every character update potentially causes a "color" refresh
            // shell_update_line(session);
            bool c_requires_refresh = c == '"' || c == ' ' || c == '\\' || c == '&' || c == '|'
                                      || c == '<' || c == '>' || c == '=';
            if (*session_cursor(session) < session_line_len(session)
                || *session_vcursor(session) >= session->colwidth || c_requires_refresh) {
                shell_update_line(session);
            } else {
                tty_write(1, &c);
                tty_flush();
            }
#else
            /// XXX only trigger a refresh when absolutely necessary.
            if (*session_cursor(session) < session_line_len(session)
                || *session_vcursor(session) >= session->colwidth) {
                shell_update_line(session);
            } else {
                tty_write(1, &c);
                tty_flush();
            }
#endif
        }
        if (reset_tab_complete) {
            session->tab_complete_mode = PARSE_MODE_NONE;
            _shell_deinit_autocomplete(session);
        }
    } while (true);
    // NOTE now we can clean-up the history again
    size_t hindex = session->hindex;
    char  *buf    = session_line_cstr(session);
    size_t len    = strlen(buf);

    session->hindex               = num_history - 1;
    session_current(session)->str = malloc(sizeof(char) * (len + 1));
    memcpy(session_current(session)->str, buf, len);
    session_current(session)->str[len] = '\0';

    if (hindex != num_history - 1) {
        if (session_current(session)->buf.capacity < len) {
            // we assume that we can reuse the gap buffer, therefore it needs to be big enough
            gb_resize(&session_current(session)->buf, len);
        }
    }

    for (size_t i = 0; i < num_history; ++i) {
        session->hindex                  = i;
        session_current(session)->dirty  = false;
        len                              = session_line_len(session);
        session_current(session)->cursor = len;
        // XXX maybe set this at the beginning
        session_current(session)->vcursor = MIN(len, session->colwidth);
    }

    // NOTE for convenience we return the current command.
    return session_current(session)->str;
}
