#include "builtin.h"

#include <stdio.h>
#include <stdlib.h>

#include "../pathutil/pathutil.h"

#define TTY_COLOR_DIRECTORY "\033[92m"
#define TTY_COLOR_FILE      "\033[34m"

static void print_ls_entry(bool flag_l, bool flag_a, char *name, bool is_directory)
{
    if (!flag_a && name[0] == '.') {
        return;
    }
    static const char *file_type = "\033[0m     FILE  ";
    static const char *dir_type  = "\033[0mDIRECTORY  ";
    const char        *curr_type = is_directory ? dir_type : file_type;
    printf("%s%s%s\n", (flag_l ? curr_type : ""),
           is_directory ? TTY_COLOR_DIRECTORY : TTY_COLOR_FILE, name);
}

static void ls_dir(const char *dirname, bool flag_l, bool flag_a)
{
    if (flag_l) {
        printf("\033[1m     TYPE  NAME\033[0m\n");
    }
    (void)flag_l;
    (void)flag_a;
    errval_t err = SYS_ERR_OK;
    fs_dirhandle_t dh;
    bool abs = false;
    char *dirpath;
    if (pathutil_is_abs_path(dirname)) {
        abs = true;
        dirpath = (char *) dirname;
    } else {
        pathutil_concat_paths(builtin_wd(), dirname, &dirpath);
    }
    err = opendir(dirpath, &dh);
    if (err_is_fail(err) || dh == NULL) {
        DEBUG_ERR(err, "opendir");
        builtin_fail("cannot open directory");
    }
    do {
        char *name;
        err = readdir(dh, &name);
        if (err_no(err) == FS_ERR_INDEX_BOUNDS) {
            break;
        } else {
            builtin_fail_if_err(err);
        }
        print_ls_entry(flag_l, flag_a, name, pathutil_is_rel_directory(dirpath, name));
    } while (err_is_ok(err));
    err = closedir(dh);
    builtin_fail_if_err(err);
    if (!abs) {
        free(dirpath);
    }
}

int main(int argc, char *argv[])
{
    builtin_init("ls", argc, argv);
    errval_t err = filesystem_init();
    builtin_fail_if_err(err);

    int  dirs   = builtin_getargc();
    bool flag_l = builtin_getflag('l');
    bool flag_a = builtin_getflag('a');
    if (dirs <= 1) {
        ls_dir(dirs == 0 ? builtin_wd() : builtin_getarg(0), flag_l, flag_a);
    } else {
        for (int i = 0; i < dirs; ++i) {
            const char *dirname = builtin_getarg(i);
            printf("'%s':\n", dirname);
            ls_dir(dirname, flag_l, flag_a);
            if (i < dirs - 1) {
                printf("\n");
            }
        }
    }
    return EXIT_SUCCESS;
}