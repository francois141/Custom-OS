#include "builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <aos/network.h>
#include <aos/domain.h>

uint16_t port;

static void server(uint32_t src_ip, uint16_t src_port, uint16_t data_size, void* data, void* meta){
    (void)data_size;
    (void)meta;
    printf("Got UDP message from ip %d.%d.%d.%d:%hu to port %hu:\n", src_ip & 255, (src_ip >> 8) & 255, (src_ip >> 16) & 255, (src_ip >> 24) & 255, src_port, port);
    // assume it's ASCII text
    printf("%s", data);
}

int main(int argc, char *argv[])
{
    builtin_init("listen", argc, argv);
    int args = builtin_getargc();
    if (args != 2) {
        builtin_fail("unexpected number of arguments.");
    }
    if(strcmp(builtin_getarg(0), "udp") != 0)
        builtin_fail("udp is the only supported protocol");

    port = atoi(builtin_getarg(1));
    errval_t err = network_init();
    if(err_is_fail(err))
        builtin_fail("Failed to init network");

    network_listen(port, SERVER_PROTOCOL_UDP, server, NULL);
    while (true) {
        err = event_dispatch(get_default_waitset());
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}