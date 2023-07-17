#include "session.h"

#include <assert.h>
#include <string.h>

#include "../tty/tty.h"
#include "../pathutil/pathutil.h"

// forward declaration of trie callbacks.
static size_t _shell_trie_encode(char c);
static char   _shell_trie_decode(size_t index);

void session_init(struct shell_session *session, const char *prompt, tab_complete_fn tab_complete)
{
    da_init(&session->history, sizeof(struct history_item));
    trie_init(&session->vars, 256, _shell_trie_encode, _shell_trie_decode);
    trie_init(&session->cmds, 256, _shell_trie_encode, _shell_trie_decode);

    (void)prompt;
    (void)tab_complete;
    session->wd = malloc(sizeof(char) * (strlen(FS_ROOT_DIRECTORY) + 1));
    strcpy(session->wd, FS_ROOT_DIRECTORY);

    session->prompt     = prompt;
    session->prompt_len = strlen(prompt);

    session->colwidth = tty_get_column_width() - 1 - session->prompt_len;
    session->hindex   = 0;

    session->tab_complete_fn               = tab_complete;
    session->tab_complete_results.optc     = 0;
    session->tab_complete_results.optv     = NULL;
    session->tab_complete_results.position = 0;
    session->tab_complete_results.done_fn  = NULL;

    sb_init(&session->line_buf);
}

struct history_item *session_current(struct shell_session *session)
{
    struct history_item *items = session->history.buf;
    return &items[session->hindex];
}

struct gap_buffer *session_edit_line(struct shell_session *session)
{
    struct history_item *hi = session_current(session);
    if (!hi->dirty) {
        // we are trying to modify the history, make a copy.
        hi->dirty = true;
        gb_reinit_from_cstr(&hi->buf, hi->str);
    }
    return &hi->buf;
}

char *session_line_cstr(struct shell_session *session)
{
    struct history_item *hi = session_current(session);
    if (hi->dirty) {
        return gb_to_cstr(session_edit_line(session));
    }
    return hi->str;
}

size_t session_line_len(struct shell_session *session)
{
    struct history_item *hi = session_current(session);
    return hi->dirty ? hi->buf.size : strlen(hi->str);
}

size_t *session_cursor(struct shell_session *session)
{
    return &session_current(session)->cursor;
}

size_t *session_vcursor(struct shell_session *session)
{
    return &session_current(session)->vcursor;
}

__attribute__((used)) void session_append_history(struct shell_session *session, char *str)
{
    size_t              len = strlen(str);
    struct history_item hi
        = { .dirty = false, .str = str, .cursor = len, .vcursor = MIN(len, session->colwidth) };
    gb_init(&hi.buf);
    da_append(&session->history, sizeof(struct history_item), &hi);
}

void session_append_editable(struct shell_session *session)
{
    struct history_item hi = { .dirty = true, .str = NULL, .cursor = 0, .vcursor = 0 };
    gb_init(&hi.buf);
    da_append(&session->history, sizeof(struct history_item), &hi);
}

void session_move_vcursor_backward(struct shell_session *session, size_t n)
{
    // XXX we should do something better here.
    for (size_t i = 0; i < n; ++i) {
        if (*session_vcursor(session) > 0 && *session_cursor(session) < *session_vcursor(session)) {
            --*session_vcursor(session);
        }
    }
}

bool session_history_up(struct shell_session *session)
{
    if (session->hindex > 0) {
        --session->hindex;
        return true;
    }
    return false;
}

bool session_history_down(struct shell_session *session)
{
    size_t num_history = (session->history.size) / sizeof(struct history_item);
    if (session->hindex < num_history - 1) {
        ++session->hindex;
        return true;
    }
    return false;
}

char *session_wd(struct shell_session *session) {
    return session->wd; ///< we always keep the wd in a "good state"
}

bool session_cd(struct shell_session *session, char *path) {
    char *abspath = path;
    if (!pathutil_is_abs_path(path)) {
        bool ok = pathutil_concat_paths(session_wd(session), path, &abspath);
        if (!ok) return false;
    }
    char *sanitized_path;
    bool ok = pathutil_sanitize_path(abspath, &sanitized_path);
    if (!ok) {
        if (!pathutil_is_abs_path(path)) free(abspath);
        return false;
    }
    if (!pathutil_is_abs_path(sanitized_path)) {
        free(abspath);
    }
    if (!pathutil_is_directory(sanitized_path)) {
        free(sanitized_path);
        return false;
    }
    free(session->wd);
    session->wd = sanitized_path;
    return true;
}

static size_t _shell_trie_encode(char c)
{
    return (size_t)c;
}

static char _shell_trie_decode(size_t index)
{
    return (char)index;
}
