#ifndef LIBBARRELFISH_TTY_H
#define LIBBARRELFISH_TTY_H

#include <aos/aos.h>
#include <stdbool.h>
#include "../../init/async_channel.h"

/// XXX outputs any pending characters after reading.
#define SERIAL_ECHO_ON_TYPE 1

errval_t serial_server_init(struct async_channel *async, enum pi_platform platform);

// XXX as this does not work on qemu, we enable it selectively
bool is_usr_serial_enabled(void);

errval_t serial_putchar(char c);

// retlen contains the actual number of bytes that were written.
errval_t serial_putstr(const char *buf, size_t len, size_t *retlen);

errval_t serial_getchar_register_wait(size_t len, struct event_closure resume_fn, size_t *retlen,
                                      char *buf);

#endif  // LIBBARRELFISH_TTY_H