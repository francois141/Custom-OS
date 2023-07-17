#include "shell.h"

#include "tty/tty.h"
#include <stdio.h>
#include <aos/deferred.h>
#include <fs/fat32.h>
#include <fs/dirent.h>


int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    errval_t err = filesystem_init();
    if (err_is_fail(err)) {
        debug_printf("shell: could not initialize filesystem.");
    }

    barrelfish_usleep(250000);  // wait for everything to setup
    return shell_launch_session();
}