#ifndef LIBBARRELFISH_BUILTIN_H
#define LIBBARRELFISH_BUILTIN_H

#include <aos/debug.h>

#include <fs/fs.h>
#include <fs/dirent.h>

// #define BUILTIN_DEBUG_ERR_ON_FAIL

#define NO_FS_FALLBACK ///< sets up a fall back file system to test the filesystem specific commands

#ifdef NO_FS_FALLBACK
errval_t setup_fallback_fs(void);
#endif

void builtin_init(const char *name, int argc, char *argv[]);

bool builtin_getflag(char flag);
int builtin_getargc(void);
const char *builtin_getarg(int index);

void builtin_fail(const char *msg);
void builtin_fail_err(errval_t err);
void builtin_fail_if_err(errval_t err);

const char *builtin_wd(void);

#endif //LIBBARRELFISH_BUILTIN_H
