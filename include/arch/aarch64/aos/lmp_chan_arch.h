/**
 * \file
 * \brief
 */

/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef ARCH_AARCH64_BARRELFISH_LMP_CHAN_H
#define ARCH_AARCH64_BARRELFISH_LMP_CHAN_H

#include <aos/syscall_arch.h>
#include <aos/caddr.h>
#include <barrelfish_kpi/lmp.h>
#include <barrelfish_kpi/syscalls.h>

/// the the maximum message length should fit in the available message bits
STATIC_ASSERT(LMP_MSG_LENGTH <= (1 << SYSCALL_MSG_WORDS_BITS), "message length too large");

/**
 * \brief Send a message on the given LMP channel, if possible
 *
 * Non-blocking, may fail if there is no space in the receiver's endpoint.
 *
 * \param ep Remote endpoint cap
 * \param flags LMP send flags
 * \param send_cap (Optional) capability to send with the message
 * \param length_words Length of the message in words; payload beyond this
 *                      size will not be delivered
 * \param arg1..N Message payload
 */
static inline errval_t lmp_ep_send(struct capref ep, lmp_send_flags_t flags, struct capref send_cap,
                                   uint8_t length_words, uintptr_t arg1, uintptr_t arg2,
                                   uintptr_t arg3, uintptr_t arg4, uintptr_t arg5, uintptr_t arg6,
                                   uintptr_t arg7, uintptr_t arg8)
{
    union syscall_info si = { 0 };

    if (length_words > LMP_MSG_LENGTH) {
        return SYS_ERR_ILLEGAL_INVOCATION;
    }

    // TODO: if length_words < LRPC_MSG_LENGTH then do LRPC.

    // set the invocation
    si.sc.syscall          = SYSCALL_INVOKE;
    si.sc.argc             = 10;
    si.invoke.invoke_cptr  = get_cap_addr(ep);
    si.invoke.invoke_level = get_cap_level(ep);
    si.invoke.flags        = flags;
    si.invoke.msg_words    = length_words;

    // construct the send cap
    uint8_t   send_level    = get_cap_level(send_cap);
    capaddr_t send_cptr     = get_cap_addr(send_cap);
    uint64_t  send_cap_info = ((uint64_t)send_level) << 32 | send_cptr;

    return syscall(si.raw, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, send_cap_info, 0, 0).error;
}

#define lmp_ep_send8(ep, flags, send_cap, a, b, c, d, e, f, g, h)                                  \
    lmp_ep_send((ep), (flags), (send_cap), 8, (a), (b), (c), (d), (e), (f), (g), (h))
#define lmp_ep_send7(ep, flags, send_cap, a, b, c, d, e, f, g)                                     \
    lmp_ep_send((ep), (flags), (send_cap), 7, (a), (b), (c), (d), (e), (f), (g), 0)
#define lmp_ep_send6(ep, flags, send_cap, a, b, c, d, e, f)                                        \
    lmp_ep_send((ep), (flags), (send_cap), 6, (a), (b), (c), (d), (e), (f), 0, 0)
#define lmp_ep_send5(ep, flags, send_cap, a, b, c, d, e)                                           \
    lmp_ep_send((ep), (flags), (send_cap), 5, (a), (b), (c), (d), (e), 0, 0, 0)
#define lmp_ep_send4(ep, flags, send_cap, a, b, c, d)                                              \
    lmp_ep_send((ep), (flags), (send_cap), 4, (a), (b), (c), (d), 0, 0, 0, 0)
#define lmp_ep_send3(ep, flags, send_cap, a, b, c)                                                 \
    lmp_ep_send((ep), (flags), (send_cap), 3, (a), (b), (c), 0, 0, 0, 0, 0)
#define lmp_ep_send2(ep, flags, send_cap, a, b)                                                    \
    lmp_ep_send((ep), (flags), (send_cap), 2, (a), (b), 0, 0, 0, 0, 0, 0)
#define lmp_ep_send1(ep, flags, send_cap, a)                                                       \
    lmp_ep_send((ep), (flags), (send_cap), 1, (a), 0, 0, 0, 0, 0, 0, 0)
#define lmp_ep_send0(ep, flags, send_cap)                                                          \
    lmp_ep_send((ep), (flags), (send_cap), 0, 0, 0, 0, 0, 0, 0, 0, 0)

#define lmp_chan_send8(lc, flags, send_cap, a, b, c, d, e, f, g, h)                                \
    lmp_ep_send8((lc)->remote_cap, (flags), (send_cap), (a), (b), (c), (d), (e), (f), (g), (h))
#define lmp_chan_send7(lc, flags, send_cap, a, b, c, d, e, f, g)                                   \
    lmp_ep_send7((lc)->remote_cap, (flags), (send_cap), (a), (b), (c), (d), (e), (f), (g))
#define lmp_chan_send6(lc, flags, send_cap, a, b, c, d, e, f)                                      \
    lmp_ep_send6((lc)->remote_cap, (flags), (send_cap), (a), (b), (c), (d), (e), (f))
#define lmp_chan_send5(lc, flags, send_cap, a, b, c, d, e)                                         \
    lmp_ep_send5((lc)->remote_cap, (flags), (send_cap), (a), (b), (c), (d), (e))
#define lmp_chan_send4(lc, flags, send_cap, a, b, c, d)                                            \
    lmp_ep_send4((lc)->remote_cap, (flags), (send_cap), (a), (b), (c), (d))
#define lmp_chan_send3(lc, flags, send_cap, a, b, c)                                               \
    lmp_ep_send3((lc)->remote_cap, (flags), (send_cap), (a), (b), (c))
#define lmp_chan_send2(lc, flags, send_cap, a, b)                                                  \
    lmp_ep_send2((lc)->remote_cap, (flags), (send_cap), (a), (b))
#define lmp_chan_send1(lc, flags, send_cap, a)                                                     \
    lmp_ep_send1((lc)->remote_cap, (flags), (send_cap), (a))
#define lmp_chan_send0(lc, flags, send_cap)                                                        \
    lmp_ep_send0((lc)->remote_cap, (flags), (send_cap))

#endif
