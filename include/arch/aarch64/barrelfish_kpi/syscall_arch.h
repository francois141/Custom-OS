/**
 * \file
 * \brief User-side system call implementation
 */

/*
 * Copyright (c) 2022, The University of British Columbia
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef ARCH_AARCH64_BARRELFISH_KPI_SYSCALL_ARCH_H_
#define ARCH_AARCH64_BARRELFISH_KPI_SYSCALL_ARCH_H_ 1

#include <barrelfish_kpi/syscalls.h>  // for struct sysret.
#include <aos/caddr.h>
#include <aos/static_assert.h>

/// the number of bits available for the syscall number
#define SYSCALL_NUM_BITS 4

/// the bumber of bits available to encode the argument count
#define SYSCALL_ARGC_BITS 4

/// defines the number of bits available for the invocation level
#define SYSCALL_LEVEL_BITS 4

/// the number of bits available for the number of message words in the lmp message
#define SYSCALL_MSG_WORDS_BITS (8 - SYSCALL_LEVEL_BITS)

/// the system call information, total 8 bits
struct syscall {
    uint8_t syscall : SYSCALL_NUM_BITS;     ///< system call number
    uint8_t argc    : SYSCALL_ARGC_BITS;    ///< the number of arguments
};
STATIC_ASSERT_SIZEOF(struct syscall, sizeof(uint8_t));

union syscall_info {
    /// generic system call information
    struct syscall sc;

    /// invocation information
    struct {
        /// generic part
        struct syscall sc;
        /// the command to execute on the invoked capability
        uint8_t cmd;
        /// the flags of the invocation
        uint8_t flags;
        /// the number of words in the lmp message
        uint8_t msg_words : SYSCALL_MSG_WORDS_BITS;
        /// the level of the capability to invoke
        uint8_t invoke_level : SYSCALL_LEVEL_BITS;
        /// the cptr of the capabilty to invoke
        capaddr_t invoke_cptr;
    } invoke;
    uint64_t raw;
};

// the maximum syscall count should not exceed the number of available bits
STATIC_ASSERT(SYSCALL_COUNT < (1 << SYSCALL_NUM_BITS), "not enough system call bits");

// the size of the struct shall be the same as a machine word
STATIC_ASSERT_SIZEOF(union syscall_info, sizeof(uintptr_t));

#endif
