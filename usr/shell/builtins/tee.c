#include "builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "../pathutil/pathutil.h"

#define CAT_C(callback)                                                                            \
    int c;                                                                                         \
    do {                                                                                           \
        c = getchar();                                                                             \
        callback;                                                                                  \
    } while (c != '\0')

static int tee_stdout(void)
{
    CAT_C(printf("%c", c));
    return EXIT_SUCCESS;
}

static int tee_file(const char *filename, bool silent)
{
    bool abs = false;
    char *filepath = NULL;
    FILE *file = NULL;
    if (filename != NULL) {
        if (pathutil_is_abs_path(filename)) {
            filepath = (char *) filename;
            abs = true;
        } else {
            pathutil_concat_paths(builtin_wd(), filename, &filepath);
        }
        file = fopen(filepath, "w");
        if (file == NULL) {
            builtin_fail("cannot open file.");
            return FS_ERR_OPEN;
        }
    }
    int c;
    do {
        c = getchar();
        if (!silent)  {
            printf("%c", c);
        }
        if (file != NULL) {
            fwrite(&c, 1, /*length*/ 1, file);
        }
    } while (c != '\0');
    if (file != NULL) {
        int res = fclose(file);
        if (res) {
            builtin_fail("cannot close file.");
            return FS_ERR_CLOSE;
        }
    }
    if (!abs) {
        free(filepath);
    }
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    builtin_init("tee", argc, argv);
    errval_t err = filesystem_init();
    builtin_fail_if_err(err);

    bool silent = builtin_getflag('s');
    switch (builtin_getargc()) {
    case 0:
        return tee_stdout();
    case 1:
        return tee_file(builtin_getarg(0), silent);
    default:
        builtin_fail("specifying multiple files unsupported.");
    }
    return EXIT_SUCCESS;
}