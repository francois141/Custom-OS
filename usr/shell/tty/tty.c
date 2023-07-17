#include "tty.h"

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

#include <aos/aos_rpc.h>
#include <aos/debug.h>

void tty_erase(enum tty_erase_line_type type)
{
    printf("\x1b[%dK", type);
}

void tty_cursor_show(void)
{
    printf("\x1b[?25h");
    tty_flush();
}

void tty_cursor_hide(void)
{
    printf("\x1b[?25l");
    tty_flush();
}

void tty_cursor_forward(int n)
{
    if (!n)
        return;
    printf("\x1b[%dC", n);
}

void tty_cursor_backward(int n)
{
    if (!n)
        return;
    printf("\x1b[%dD", n);
}

void tty_clear_line(void)
{
    tty_erase(TTY_ERASE_LINE);
    tty_cursor_backward(1e6);
}

void tty_flush(void)
{
    fflush(stdout);
}

void tty_get_cursor_position(int *row, int *col)
{
    printf("\x1b[6n");
    tty_flush();
    // XXX equivalent of scanf("\x1b[%d;%dR", row, col);, but we want to do it in "raw-mode"
#if 1
    char retbuf[128];
    int pos = 0;
    do {
        retbuf[pos++] = tty_read();
    } while (retbuf[pos - 1] != 'R');
    sscanf(retbuf, "\x1b[%d;%dR", row, col);
#else
    scanf("\x1b[%d;%dR", row, col);
#endif
}

int tty_get_column_width(void)
{
    // XXX this is a "hacky" way of determining the column width:
    //     - move the cursor forward by a HUGE_AMOUNT
    //     - get the current cursor position
    //     - clear the line again.
    tty_cursor_hide();
    const int HUGE_AMOUNT = 1e6;
    tty_cursor_forward(HUGE_AMOUNT);
    int rows, cols;
    tty_get_cursor_position(&rows, &cols);

    tty_erase(TTY_ERASE_LINE);
    // move the cursor back to the beginning of the line.
    // tty_write(1, "\r");
    printf("\033[%dD", cols - 1);
    tty_cursor_show();
    return cols;
}

char tty_read(void)
{
    // XXX to prevent buffering, we read char-by-char "raw-mode"
    char c;
    errval_t err = aos_rpc_serial_getchar(aos_rpc_get_serial_channel(), &c);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_serial_getchar failed.");
        assert(false);
    }
    return c;
}

void tty_write(size_t len, const char *buf)
{
    printf("%.*s", len, buf);
}

char tty_read_skip_multi_byte(void)
{
    char c;
    // check for multi-character UTF-8 sequence
    // XXX read the character(s) to not mess up char count; but ignore the input
    // NOTE we can have up to three additional bytes, all with MSB set.
    while ((c = tty_read()) & 128)
        ;
    return c;
}

void tty_clear_screen(void)
{
    printf("\x1b[H\x1b[2J");
    tty_flush();
}