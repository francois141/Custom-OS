#include "builtin.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static struct {
    const char  *name;
    int          argc;
    const char **argv;
    const char  *wd;
} builtin_info;

#ifdef NO_FS_FALLBACK
static errval_t _setup_file(char *filename, const char *data)
{
    int   res = 0;
    FILE *f   = fopen(filename, "w");
    if (f == NULL) {
        return FS_ERR_OPEN;
    }
    size_t written = fwrite(data, 1, strlen(data), f);
    if (written != strlen(data)) {
        return FS_ERR_READ;
    }
    res = fclose(f);
    if (res) {
        return FS_ERR_CLOSE;
    }
    return SYS_ERR_OK;
}

#define MKDIR(path)                                                                                \
    do {                                                                                           \
        err = mkdir(path);                                                                         \
        if (err_is_fail(err)) {                                                                    \
            return err;                                                                            \
        }                                                                                          \
    } while (0)

#define WRITE_FILE(path, data)                                                                     \
    do {                                                                                           \
        err = _setup_file(path, data);                                                             \
        if (err_is_fail(err)) {                                                                    \
            return err;                                                                            \
        }                                                                                          \
    } while (0)


static errval_t _setup_fs(void)
{
    errval_t err = SYS_ERR_OK;
    MKDIR("Zeus_D");
    WRITE_FILE("Zeus_D/A.txt", "I love deadlines. I like the whooshing sound they ...");
    WRITE_FILE("Zeus_D/B.txt", "I love deadlines. I like the whooshing sound they ...");
    WRITE_FILE("Zeus_D/C.txt", "I love deadlines. I like the whooshing sound they ...");
    WRITE_FILE("Zeus_D/D.txt", "I love deadlines. I like the whooshing sound they ...");

    MKDIR("Hera_D");
    MKDIR("Poseidon_D");
    MKDIR("Demeter_D");
    MKDIR("Athena_D");
    MKDIR("Apollo_D");
    MKDIR("Artemis_D");

    WRITE_FILE("Ares.txt", "I love deadlines. I like the whooshing sound they ...");
    WRITE_FILE("Aphrodite.txt", "I love deadlines. I like the whooshing sound they ...");
    WRITE_FILE("Hermes.txt", "I love deadlines. I like the whooshing sound they ...");
    WRITE_FILE("Dionysus.txt", "I love deadlines. I like the whooshing sound they ...");
    WRITE_FILE("Hades.txt", "I love deadlines. I like the whooshing sound they ...");

    return SYS_ERR_OK;
}

errval_t setup_fallback_fs(void)
{
    errval_t err = SYS_ERR_OK;
    err          = filesystem_init();
    if (err_is_fail(err)) {
        return err;
    }
    return _setup_fs();
}
#endif

void builtin_init(const char *name, int argc, char *argv[])
{
    builtin_info.name = name;
    builtin_info.argc = argc;
    builtin_info.argv = (const char **)argv;
    //builtin_fail_if_err(setup_fallback_fs());
    builtin_info.wd = NULL;
    for (int i = 0; i < argc; ++i) {
        if (strcmp(builtin_info.argv[i], "--wd") == 0) {
            if (i + 1 < builtin_info.argc) {
                builtin_info.wd = builtin_info.argv[i + 1];
                break;
            } else {
                builtin_fail("missing working_directory parameter.");
            }
        }
    }
    if (builtin_info.wd == NULL) {
        builtin_info.wd = "/";
    }
#if 0
    debug_printf("pwd: '%s'\n", builtin_info.wd);
#endif
}

bool builtin_getflag(char flag)
{
    for (int i = 0; i < builtin_info.argc; ++i) {
        if (strcmp(builtin_info.argv[i], "--wd") == 0) {
            ++i;
            continue;
        }
        if (builtin_info.argv[i][0] != '-')
            continue;
        size_t len = strlen(builtin_info.argv[i]);
        if (len <= 1)
            continue;
        for (size_t j = 1; j < len; ++j) {
            if (builtin_info.argv[i][j] == flag)
                return true;
        }
    }
    return false;
}

int builtin_getargc(void)
{
    int argc = 0;
    for (int i = 1; i < builtin_info.argc; ++i) {
        if (strcmp(builtin_info.argv[i], "--wd") == 0) {
            ++i;
            continue;
        }
        if (builtin_info.argv[i][0] != '-' || strlen(builtin_info.argv[i]) <= 1) {
            ++argc;
        }
    }
    return argc;
}

const char *builtin_getarg(int index)
{
    for (int i = 1; i < builtin_info.argc; ++i) {
        if (strcmp(builtin_info.argv[i], "--wd") == 0) {
            ++i;
            continue;
        }
        if (builtin_info.argv[i][0] != '-' || strlen(builtin_info.argv[i]) <= 1) {
            if (index-- == 0) {
                return builtin_info.argv[i];
            }
        }
    }
    return NULL;
}

void builtin_fail(const char *msg)
{
    printf("\033[31;1m%s: %s\033[0m\n", builtin_info.name, msg);
    exit(EXIT_FAILURE);
}

void builtin_fail_err(errval_t err)
{
#if BUILTIN_DEBUG_ERR_ON_FAIL
    DEBUG_ERR(err, "builtin_fail_err");
#endif
    builtin_fail(err_getstring(err));
}

void builtin_fail_if_err(errval_t err)
{
    if (err_is_fail(err)) {
        builtin_fail_err(err);
    }
}

const char *builtin_wd(void) {
    return builtin_info.wd;
}