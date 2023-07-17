#ifndef COREBOOT_UTILS_H
#define COREBOOT_UTILS_H

#include <aos/aos.h>

struct setup_msg_0 {
    struct {
        genpaddr_t base;
        gensize_t length;
    } ram;
    size_t bootinfo_size;
    genpaddr_t mmstring_base;
};

errval_t copy_bootinfo_capabilities(struct bootinfo* bi);

#endif // COREBOOT_UTILS_H
