#include "builtin.h"

#include <stdio.h>
#include <stdlib.h>

#include "../pathutil/pathutil.h"

static errval_t cat_file(const char *filename)
{
    bool abs = false;
    char *filepath;
    if (pathutil_is_abs_path(filename)) {
        filepath = (char *) filename;
        abs = true;
    } else {
        pathutil_concat_paths(builtin_wd(), filename, &filepath);
    }
    FILE *f   = fopen(filepath, "r");
    if (f == NULL) {
        builtin_fail("cannot open file.");
    }
    int   res = fseek(f, 0, SEEK_END);
    if (res) {
        return FS_ERR_INVALID_FH;
    }
    size_t filesize = ftell(f);
    rewind(f);
    // printf("File size is %zu\n", filesize);
    char *buf = calloc(filesize + 2, sizeof(char));
    if (buf == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }
    size_t read = fread(buf, 1, filesize, f);
    printf("%s\n", buf);
    if (read != filesize) {
        return FS_ERR_READ;
    }
    if (!filename) {
        free(filepath);
    }
    return SYS_ERR_OK;
}

int main(int argc, char *argv[])
{
    builtin_init("cat", argc, argv);
    int files = builtin_getargc();
    errval_t err = filesystem_init();
    builtin_fail_if_err(err);

    // XXX we may want to support reading from stdin?
    if (files == 0) {
        builtin_fail("no file(s) specified.");
    }
    for (int i = 0; i < files; ++i) {
        builtin_fail_if_err(cat_file(builtin_getarg(i)));
    }
    return EXIT_SUCCESS;
}