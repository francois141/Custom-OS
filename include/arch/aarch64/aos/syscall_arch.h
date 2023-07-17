/**
 * \file
 * \brief User-side system call implementation
 */

/*
 * Copyright (c) 2007-2016, ETH Zurich.
 * Copyright (c) 2015, Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef ARCH_AARCH64_BARRELFISH_SYSCALL_H
#define ARCH_AARCH64_BARRELFISH_SYSCALL_H

#include <barrelfish_kpi/syscalls.h>      // for struct sysret.
#include <barrelfish_kpi/syscall_arch.h>  // for the syscall argument

/**
 * @brief the actual system call function
 *
 * the arguments are left in the registers x0-x11 (caller save registers)
 * the return value is stored in x0 and x1 when returning from the syscall
 *
 * The first argument encodes the system call information (union syscall_info)
 */
struct sysret syscall(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4,
                      uint64_t arg5, uint64_t arg6, uint64_t arg7, uint64_t arg8, uint64_t arg9,
                      uint64_t arg10, uint64_t arg11);


static inline struct sysret syscall7(uint8_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                     uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    union syscall_info info = { .sc.syscall = num, .sc.argc = 7 };
    return syscall(info.raw, arg1, arg2, arg3, arg4, arg5, arg6, 0, 0, 0, 0, 0);
}

static inline struct sysret syscall6(uint8_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                     uint64_t arg4, uint64_t arg5)
{
    union syscall_info info = { .sc.syscall = num, .sc.argc = 6 };
    return syscall(info.raw, arg1, arg2, arg3, arg4, arg5, 0, 0, 0, 0, 0, 0);
}

static inline struct sysret syscall5(uint8_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                     uint64_t arg4)
{
    union syscall_info info = { .sc.syscall = num, .sc.argc = 5 };
    return syscall(info.raw, arg1, arg2, arg3, arg4, 0, 0, 0, 0, 0, 0, 0);
}

static inline struct sysret syscall4(uint8_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    union syscall_info info = { .sc.syscall = num, .sc.argc = 4 };
    return syscall(info.raw, arg1, arg2, arg3, 0, 0, 0, 0, 0, 0, 0, 0);
}

static inline struct sysret syscall3(uint8_t num, uint64_t arg1, uint64_t arg2)
{
    union syscall_info info = { .sc.syscall = num, .sc.argc = 3 };
    return syscall(info.raw, arg1, arg2, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static inline struct sysret syscall2(uint8_t num, uint64_t arg1)
{
    union syscall_info info = { .sc.syscall = num, .sc.argc = 2 };
    return syscall(info.raw, arg1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

static inline struct sysret syscall1(uint8_t num)
{
    union syscall_info info = { .sc.syscall = num, .sc.argc = 1 };
    return syscall(info.raw, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}


#endif
