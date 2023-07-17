#ifndef LIBBARRELFISH_CMDBUILTINS_H
#define LIBBARRELFISH_CMDBUILTINS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <aos/aos.h>

errval_t iox_init(void);
size_t iox_write(const char *buf, size_t len);
size_t iox_read(char *buf, size_t len);

void iox_destroy(void);

#endif  // LIBBARRELFISH_CMDBUILTINS_H