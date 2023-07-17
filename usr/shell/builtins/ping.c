#include "builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <string.h>

#include <aos/network.h>
#include "../tty/tty.h"

int main(int argc, char *argv[]) {
    builtin_init("ping", argc, argv);
    if (builtin_getargc() != 1 && builtin_getargc() != 2) {
        printf("Wrong number of inputs for ping");
        return EXIT_FAILURE;
    }

    uint8_t     ip[4];
    const char *ip_str = (builtin_getargc() == 1) ? builtin_getarg(0) : builtin_getarg(1);
    if (sscanf(ip_str, "%hhu.%hhu.%hhu.%hhu", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) {
        printf("%sWrong IPv4 format%s\n", TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    uint32_t target_ip = *(uint32_t *)ip;
    printf("Pinging target %s...\n", ip_str);

    uint32_t count = 1;
    if (builtin_getargc() == 2)
        count = atoi(builtin_getarg(0));

    for (uint32_t i = 0; i < count; i++) {
        uint32_t ping_ms;
        errval_t err = ping(target_ip, &ping_ms);
        if (err_is_fail(err)) {
            if (err_no(err) == NETWORK_ERR_IP_RESOLVE_TIMEOUT) {
                printf("%sCould not resolve ip %s%s\n", TTY_COLOR_BOLD_RED, ip_str,
                       TTY_COLOR_RESET);
            } else if (err_no(err) == NETWORK_ERR_REQUEST_TIMEOUT) {
                printf("%sPing request timeout%s\n", TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
            } else {
                printf("%sAn error occured: %s%s\n", TTY_COLOR_BOLD_RED, err_getstring(err),
                       TTY_COLOR_RESET);
            }
            return EXIT_FAILURE;
        }
        printf("Got response from %s in %u ms\n", ip_str, ping_ms);
    }
}