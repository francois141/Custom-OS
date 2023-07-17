#include "builtin.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    builtin_init("echo", argc, argv);
    int args = builtin_getargc();
    if (args != 1) {
        builtin_fail("unexpected number of arguments.");
    }
    printf("%s\n", builtin_getarg(0));
    return EXIT_SUCCESS;
}