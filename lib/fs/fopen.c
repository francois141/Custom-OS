/**
 * \file fopen.c
 * \brief 
 */


/*
 * Copyright (c) 2016 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <aos/aos.h>
#include <aos/aos_rpc.h>

#include <fs/fs.h>
#include <fs/dirent.h>
#include <fs/ramfs.h>
#include "fs_internal.h"
#include <fs/fat32.h>



static void *mount;

/*
 * FD table
 */

#define STDIN_FILENO    0       /* standard input file descriptor */
#define STDOUT_FILENO   1       /* standard output file descriptor */
#define STDERR_FILENO   2       /* standard error file descriptor */

static struct fdtab_entry fdtab[MAX_FD] = {
        [STDIN_FILENO] = {
                .type = FDTAB_TYPE_STDIN,
                .handle = NULL,
        },
        [STDOUT_FILENO] = {
                .type = FDTAB_TYPE_STDOUT,
                .handle = NULL,
        },
        [STDERR_FILENO] = {
                .type = FDTAB_TYPE_STDERR,
                .handle = NULL,
        },
};

static int fdtab_alloc(struct fdtab_entry *h)
{
    for (int fd = MIN_FD; fd < MAX_FD; fd++) {
        if (fdtab[fd].type == FDTAB_TYPE_AVAILABLE) {
            fdtab[fd].inherited = 0; // Just precautionary
            memcpy(&fdtab[fd], h, sizeof(struct fdtab_entry));

            return fd;
        }
    }

    // table full
    errno = EMFILE;
    return -1;
}

static struct fdtab_entry *fdtab_get(int fd)
{
    static struct fdtab_entry invalid = {
            .type = FDTAB_TYPE_AVAILABLE,
            .handle = NULL,
            .inherited = 0,
    };

    if (fd < MIN_FD || fd >= MAX_FD) {
        return &invalid;
    } else {
        return &fdtab[fd];
    }
}

static void fdtab_free(int fd)
{
    assert(fd >= MIN_FD && fd < MAX_FD);
    assert(fdtab[fd].type != FDTAB_TYPE_AVAILABLE);
    fdtab[fd].type = FDTAB_TYPE_AVAILABLE;
    fdtab[fd].handle = NULL;
    fdtab[fd].fd = 0;
    fdtab[fd].inherited = 0;
}

//XXX: flags are ignored...
static int fs_libc_open(char *path, int flags)
{
    struct fat32_handle *fat_handle;
    errval_t err;

    // TODO: Add flags support
    (void)flags;

    // If O_CREAT was given, we use ramfsfs_create()
    if(flags & O_CREAT) {
        // If O_EXCL was also given, we check whether we can open() first
        if(flags & O_EXCL) {
            err = aos_rpc_filesystem_mkfile(aos_rpc_get_filesystem_channel(), path, &fat_handle);
            if(err_is_ok(err)) {
                aos_rpc_filesystem_close(aos_rpc_get_filesystem_channel(), fat_handle);
                errno = EEXIST;
                return -1;
            }
            assert(err_no(err) == FS_ERR_NOTFOUND);
        }

        err = aos_rpc_filesystem_mkfile(aos_rpc_get_filesystem_channel(), path, &fat_handle);
        if (err_is_fail(err) && err == FS_ERR_EXISTS) {
            err = aos_rpc_filesystem_open(aos_rpc_get_filesystem_channel(), path, &fat_handle);
        }
    } else {
        // Regular open()
        err = aos_rpc_filesystem_open(aos_rpc_get_filesystem_channel(), path, &fat_handle);
    }

    err = aos_rpc_filesystem_open(aos_rpc_get_filesystem_channel(), path, &fat_handle);
    if (err_is_fail(err)) {
        switch(err_no(err)) {
            case FS_ERR_NOTFOUND:
                errno = ENOENT;
                break;

            default:
                break;
        }

        return -1;
    }

    struct fdtab_entry e = {
            .type = FDTAB_TYPE_FILE,
            .handle = (void *)fat_handle,
            .epoll_fd = -1,
    };

    int fd = fdtab_alloc(&e);
    if (fd < 0) {
        aos_rpc_filesystem_close(aos_rpc_get_filesystem_channel(), fat_handle);
        return -1;
    } else {
        return fd;
    }
}

static int fs_libc_read(int fd, void *buf, size_t len)
{
    errval_t err;

    struct fdtab_entry *e = fdtab_get(fd);
    size_t retlen = 0;
    switch(e->type) {
        case FDTAB_TYPE_FILE:
        {
            struct fat32_handle *handle = e->handle;
            assert(e->handle);
            err = aos_rpc_filesystem_read(aos_rpc_get_filesystem_channel(), handle, buf, len, &retlen);
            if (err_is_fail(err)) {
                return -1;
            }
        }
            break;
        default :
            return -1;
    }

    return retlen;
}

static int fs_libc_write(int fd, void *buf, size_t len)
{
    struct fdtab_entry *e = fdtab_get(fd);
    if (e->type == FDTAB_TYPE_AVAILABLE) {
        return -1;
    }

    size_t retlen = 0;

    switch(e->type) {
        case FDTAB_TYPE_FILE:
        {
            struct fat32_handle *handle = e->handle;
            errval_t err = aos_rpc_filesystem_write(aos_rpc_get_filesystem_channel(), handle, buf, len, &retlen);
            if (err_is_fail(err)) {
                return -1;
            }
        }
            break;
        default :
            return -1;
    }

    return retlen;
}

static int fs_libc_close(int fd)
{
    errval_t err;
    struct fdtab_entry *e = fdtab_get(fd);
    if (e->type == FDTAB_TYPE_AVAILABLE) {
        return -1;
    }

    struct fat32_handle *fat_handle = e->handle;

    switch(e->type) {
        case FDTAB_TYPE_FILE:
            // TODO: Replace with my code
            err = aos_rpc_filesystem_close(aos_rpc_get_filesystem_channel(), fat_handle);
            if (err_is_fail(err)) {
                return -1;
            }
            break;
        default:
            return -1;
    }

    fdtab_free(fd);
    return 0;
}

static off_t fs_libc_lseek(int fd, off_t offset, int whence)
{
    struct fdtab_entry *e = fdtab_get(fd);
    struct fat32_handle *fat_handle = e->handle;
    switch(e->type) {
        case FDTAB_TYPE_FILE:
        {
            enum fs_seekpos fs_whence;
            errval_t err;
            size_t retpos;

            switch(whence) {
                case SEEK_SET:
                    fs_whence = FS_SEEK_SET;
                    break;

                case SEEK_CUR:
                    fs_whence = FS_SEEK_CUR;
                    break;

                case SEEK_END:
                    fs_whence = FS_SEEK_END;
                    break;

                default:
                    return -1;
            }

            err = aos_rpc_filesystem_seek(aos_rpc_get_filesystem_channel(), fat_handle, offset,
                                          whence);
            if(err_is_fail(err)) {
                DEBUG_ERR(err, "vfs_seek");
                return -1;
            }

            err = aos_rpc_filesystem_tell(aos_rpc_get_filesystem_channel(), fat_handle, &retpos);
            if(err_is_fail(err)) {
                return -1;
            }
            return retpos;
        }
            break;

        default:
            return -1;
    }
}

static errval_t fs_mkdir(const char *path){ return aos_rpc_filesystem_mkdir(aos_rpc_get_filesystem_channel(), path);}
static errval_t fs_rmdir(const char *path){ return aos_rpc_filesystem_rmdir(aos_rpc_get_filesystem_channel(), path); }
static errval_t fs_rm(const char *path){ return aos_rpc_filesystem_rmfile(aos_rpc_get_filesystem_channel(), path); }
static errval_t fs_opendir(const char *path, fs_dirhandle_t *h){ return aos_rpc_filesystem_dir_open(aos_rpc_get_filesystem_channel(), path, (struct fat32_handle**)h); }
static errval_t fs_readdir(fs_dirhandle_t h, char **name) { return aos_rpc_filesystem_dir_next(aos_rpc_get_filesystem_channel(), h, name); }
static errval_t fs_closedir(fs_dirhandle_t h) { return aos_rpc_filesystem_dir_close(aos_rpc_get_filesystem_channel(),(struct fat32_handle*)h); }
static errval_t fs_fstat(fs_dirhandle_t h, struct fs_fileinfo *b) { return aos_rpc_filesystem_stat(aos_rpc_get_filesystem_channel(), (struct fat32_handle*)h,b); }

// Function used by the shell directly
errval_t fs_is_directory(const char *path, bool *is_directory);
errval_t fs_is_directory(const char *path, bool *is_directory) { return aos_rpc_filesystem_is_directory(aos_rpc_get_filesystem_channel(), path, is_directory); }

typedef int   fsopen_fn_t(char *, int);
typedef int   fsread_fn_t(int, void *buf, size_t);
typedef int   fswrite_fn_t(int, void *, size_t);
typedef int   fsclose_fn_t(int);
typedef off_t fslseek_fn_t(int, off_t, int);
void
newlib_register_fsops__(fsopen_fn_t *open_fn,
                        fsread_fn_t *read_fn,
                        fswrite_fn_t *write_fn,
                        fsclose_fn_t *close_fn,
                        fslseek_fn_t *lseek_fn);

void fs_libc_init(void *fs_state)
{
    newlib_register_fsops__(fs_libc_open, fs_libc_read, fs_libc_write,
                            fs_libc_close, fs_libc_lseek);

    /* register directory operations */
    fs_register_dirops(fs_mkdir, fs_rmdir, fs_rm, fs_opendir,
                       fs_readdir, fs_closedir, fs_fstat);

    mount = fs_state;
}
