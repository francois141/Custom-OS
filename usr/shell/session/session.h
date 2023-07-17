#ifndef LIBBARRELFISH_SESSION_H
#define LIBBARRELFISH_SESSION_H

#include <stdbool.h>
#include <stdlib.h>

#include "../containers/dynamic_array.h"
#include "../containers/gap_buffer.h"
#include "../containers/trie.h"

#include "../session/session.h"
#include "../cmdparse/cmdparse.h"

// forward declarations
struct shell_session;
struct shell_tab_complete_results;

typedef void (*tab_complete_done)(struct shell_session *, struct shell_tab_complete_results *);
typedef void (*tab_complete_fn)(struct shell_session *, struct parsed_autocomplete *,
                                struct shell_tab_complete_results *);

struct shell_tab_complete_results {
    size_t            optc;      ///< number of options to "scroll" through
    char            **optv;      ///< options to "scroll" trough
    size_t            position;  ///< current position in the results
    tab_complete_done done_fn;   ///< if done_fn != NULL called on completion, used for clean-up
};

struct shell_session {
    size_t colwidth;  ///< shell will render at most (colwidth + 1) characters per line.
    struct dynamic_array history;     ///< dynamic array containing `history_item`s.
    size_t               hindex;      ///< position in `history` that refers to the current line.
    const char          *prompt;      ///< prompt to show before every line.
    size_t               prompt_len;  ///< length of the prompt.

    // XXX for now this simplifies the auto complete logic (and guarantees sorted order).
    struct trie cmds;     ///< commands defined in the current shell context
    struct trie vars;     ///< variables defined in the current shell context

    char *wd;  ///< current working directory of the shell

    tab_complete_fn                   tab_complete_fn;
    enum parse_mode                   tab_complete_mode;
    struct shell_tab_complete_results tab_complete_results;

    string_builder_t line_buf;  ///< "buffer" for constructing the shell output.
};

struct history_item {
    bool              dirty;  ///< use buf iff. dirty otherwise use the str.
    char             *str;    ///< "committed" history is stored as a char *
    struct gap_buffer buf;    ///< edited history is stored in a gap_buffer

    size_t cursor;            ///< cursor into the text buffer
    size_t vcursor;           ///< cursor position on the screen
};


void session_init(struct shell_session *session, const char *prompt, tab_complete_fn tab_complete);
struct history_item *session_current(struct shell_session *session);

struct gap_buffer *session_edit_line(struct shell_session *session);
char              *session_line_cstr(struct shell_session *session);
size_t             session_line_len(struct shell_session *session);

size_t *session_cursor(struct shell_session *session);
size_t *session_vcursor(struct shell_session *session);
void    session_move_vcursor_backward(struct shell_session *session, size_t n);

void session_append_history(struct shell_session *session, char *str);
void session_append_editable(struct shell_session *session);
bool session_history_up(struct shell_session *session);
bool session_history_down(struct shell_session *session);

char *session_wd(struct shell_session *session);
bool session_cd(struct shell_session *session, char *path);

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#endif  // LIBBARRELFISH_SESSION_H
