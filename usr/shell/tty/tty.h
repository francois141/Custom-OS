#ifndef LIBBARRELFISH_TTY_H
#define LIBBARRELFISH_TTY_H

#include <stddef.h>

/// enables coloring of the command line, potentially impacts performance.
#define SHELL_CMDLINE_COLORS 1

#define TTY_COLOR_BOLD_RED    "\033[31;1m"
#define TTY_COLOR_BOLD_YELLOW "\033[33;1m"
#define TTY_COLOR_BOLD_BLUE   "\033[34;1m"
#define TTY_COLOR_RED_BG      "\033[7m"
#define TTY_COLOR_BLUE        "\033[34m"
#define TTY_COLOR_RESET       "\033[0m"

enum tty_keys {
    KEY_CTRL_A    = 1,
    KEY_CTRL_B    = 2,
    KEY_CTRL_C    = 3,
    KEY_CTRL_E    = 5,
    KEY_CTRL_F    = 6,
    KEY_TAB       = 9,
    KEY_CTRL_L    = 12,
    KEY_ENTER     = 13,
    KEY_CTRL_N    = 14,
    KEY_CTRL_P    = 16,
    KEY_CTRL_W    = 23,
    KEY_ESC       = 27,
    KEY_BACKSPACE = 127,
};

// escape codes from https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
enum tty_erase_line_type {
    TTY_ERASE_AFTER_CURSOR  = 0,  // erase from cursor to the end,
    TTY_ERASE_BEFORE_CURSOR = 1,  // erase from beginning to cursor
    TTY_ERASE_LINE          = 2,  // erase the entire line
};


void tty_erase(enum tty_erase_line_type type);

void tty_clear_line(void);

void tty_flush(void);

void tty_get_cursor_position(int *row, int *col);

int tty_get_column_width(void);

void tty_cursor_show(void);

void tty_cursor_hide(void);

void tty_cursor_forward(int n);

void tty_cursor_backward(int n);

char tty_read(void);

char tty_read_skip_multi_byte(void);

void tty_write(size_t len, const char *buf);

void tty_clear_screen(void);

#endif  // LIBBARRELFISH_TTY_H
