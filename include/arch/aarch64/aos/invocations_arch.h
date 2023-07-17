/**
 * \file
 * \brief Low-level capability invocations
 */

/*
 * Copyright (c) 2007-2010, 2012, 2013, 2015, ETH Zurich.
 * Copyright (c) 2015, Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef INCLUDEBARRELFISH_INVOCATIONS_ARCH_H
#define INCLUDEBARRELFISH_INVOCATIONS_ARCH_H

#include <aos/syscall_arch.h>  // for sys_invoke and cap_invoke
#include <barrelfish_kpi/dispatcher_shared.h>
#include <barrelfish_kpi/distcaps.h>  // for distcap_state_t
#include <barrelfish_kpi/lmp.h>       // invoking lmp endpoint requires flags
#include <barrelfish_kpi/syscalls.h>
#include <barrelfish_kpi/platform.h>  // for struct platform info
#include <barrelfish_kpi/syscall_arch.h>
#include <aos/caddr.h>
#include <barrelfish_kpi/paging_arch.h>

/**
 * @brief capability invocation
 *
 * @param[in] to    capability to invoke
 * @param[in] argc  the number of arguments for the invocation
 * @param[in] cmd   command to invoke on the capability
 * @param[in] arg1  first argument for the invocation
 * @param[in] arg2  second argument for the invocation
 * @param[in] arg3  third argument for the invocation
 * @param[in] arg4  fourth argument for the invocation
 * @param[in] arg5  fifth argument for the invocation
 * @param[in] arg6  sixth argument for the invocation
 * @param[in] arg7  seventh argument for the invocation
 * @param[in] arg8  eighth argument for the invocation
 * @param[in] arg9  ninth argument for the invocation
 *
 * @return returns SYS_ERR_OK on success, or SYS_ERR_* on failure
 */
static inline struct sysret cap_invoke(struct capref to, uintptr_t argc, uintptr_t cmd,
                                       uintptr_t arg1, uintptr_t arg2, uintptr_t arg3,
                                       uintptr_t arg4, uintptr_t arg5, uintptr_t arg6,
                                       uintptr_t arg7, uintptr_t arg8, uintptr_t arg9)
{
    union syscall_info si = { 0 };

    if (argc > 9 || cmd > 0xff) {
        return SYSRET(SYS_ERR_ILLEGAL_INVOCATION);
    }

    // set the invocation
    si.sc.syscall          = SYSCALL_INVOKE;
    si.sc.argc             = argc + 1;
    si.invoke.invoke_cptr  = get_cap_addr(to);
    si.invoke.invoke_level = get_cap_level(to);
    si.invoke.cmd          = cmd;
    si.invoke.flags        = LMP_FLAG_IDENTIFY;

    return syscall(si.raw, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, 0, 0);
}

#define cap_invoke10(to, _a, _b, _c, _d, _e, _f, _g, _h, _i, _j)                                   \
    cap_invoke(to, 9, _a, _b, _c, _d, _e, _f, _g, _h, _i, _j)

#define cap_invoke9(to, _a, _b, _c, _d, _e, _f, _g, _h, _i)                                        \
    cap_invoke(to, 8, _a, _b, _c, _d, _e, _f, _g, _h, _i, 0)

#define cap_invoke8(to, _a, _b, _c, _d, _e, _f, _g, _h)                                            \
    cap_invoke(to, 7, _a, _b, _c, _d, _e, _f, _g, _h, 0, 0)

#define cap_invoke7(to, _a, _b, _c, _d, _e, _f, _g)                                                \
    cap_invoke(to, 6, _a, _b, _c, _d, _e, _f, _g, 0, 0, 0)

#define cap_invoke6(to, _a, _b, _c, _d, _e, _f)                                                    \
    cap_invoke(to, 5, _a, _b, _c, _d, _e, _f, 0, 0, 0, 0)

#define cap_invoke5(to, _a, _b, _c, _d, _e)                                                        \
    cap_invoke(to, 4, _a, _b, _c, _d, _e, 0, 0, 0, 0, 0)

#define cap_invoke4(to, _a, _b, _c, _d)                                                            \
    cap_invoke(to, 3, _a, _b, _c, _d, 0, 0, 0, 0, 0, 0)

#define cap_invoke3(to, _a, _b, _c)                                                                \
    cap_invoke(to, 2, _a, _b, _c, 0, 0, 0, 0, 0, 0, 0)

#define cap_invoke2(to, _a, _b)                                                                    \
    cap_invoke(to, 1, _a, _b, 0, 0, 0, 0, 0, 0, 0, 0)

#define cap_invoke1(to, _a)                                                                        \
    cap_invoke(to, 0, _a, 0, 0, 0, 0, 0, 0, 0, 0, 0)

/**
 * \brief Retype (part of) a capability.
 *
 * Retypes (part of) CPtr 'cap' into 'objsize'd caps of type 'newtype' and places them
 * into slots starting at slot 'slot' in the CNode, addressed by 'to', with
 * 'bits' address bits of 'to' valid.
 *
 * See also cap_retype(), which wraps this.
 *
 * \param root          Capability of the source cspace root CNode to invoke
 * \param src_cspace    Source cspace cap address in our cspace.
 * \param cap           Address of cap to retype in source cspace.
 * \param offset        Offset into cap to retype
 * \param newtype       Kernel object type to retype to.
 * \param objsize       Size of created objects, for variable-sized types
 * \param count         Number of objects to create
 * \param to_cspace     Destination cspace cap address in our cspace
 * \param to            Address of CNode cap in destination cspcae to place
 *                      retyped caps into.
 * \param to_level      Level/depth of CNode cap in destination cspace
 * \param slot          Slot in CNode cap to start placement.
 *
 * \return Error code
 */
STATIC_ASSERT(ObjType_Num < 0xFFFF, "retype invocation argument packing does not truncate enum "
                                    "objtype");
static inline errval_t invoke_cnode_retype(struct capref root, capaddr_t src_cspace, capaddr_t cap,
                                           gensize_t offset, enum objtype newtype,
                                           gensize_t objsize, size_t count, capaddr_t to_cspace,
                                           capaddr_t to, enum cnode_type to_level, capaddr_t slot)
{
    assert(cap != CPTR_NULL);

    assert(newtype < ObjType_Num);
    assert(count <= 0xFFFFFFFF);
    assert(to_level <= 0xF);

    return cap_invoke10(root, CNodeCmd_Retype, src_cspace, cap, offset,
                        ((uint32_t)to_level << 16) | newtype, objsize, count, to_cspace, to, slot)
        .error;
}


static inline errval_t invoke_vnode_map(struct capref ptable, capaddr_t slot, capaddr_t src_root,
                                        capaddr_t src, enum cnode_type srclevel, size_t flags,
                                        size_t offset, size_t pte_count, capaddr_t mcnroot,
                                        capaddr_t mcnaddr, enum cnode_type mcnlevel,
                                        cslot_t mapping_slot)
{
    assert(slot <= 0xffff);
    assert(srclevel <= 0xf);
    assert(mcnlevel <= 0xf);
    assert(offset <= 0xffffffff);
    assert(flags <= 0xffffffff);
    assert(pte_count <= 0xffff);
    assert(mapping_slot <= L2_CNODE_SLOTS);

    uintptr_t small_values = srclevel | (mcnlevel << 4) | (mapping_slot << 8) | (slot << 16);

    return cap_invoke9(ptable, VNodeCmd_Map, src_root, src, flags, offset, pte_count, mcnroot,
                       mcnaddr, small_values)
        .error;
}

static inline errval_t invoke_idcap_identify(struct capref idcap, idcap_id_t *id)
{
    assert(id != NULL);
    assert(get_croot_addr(idcap) == CPTR_ROOTCN);

    struct sysret sysret = cap_invoke1(idcap, IDCmd_Identify);

    if (err_is_ok(sysret.error)) {
        *id = sysret.value;
    }

    return sysret.error;
}

static inline errval_t invoke_get_global_paddr(struct capref kernel_cap, genpaddr_t *global)
{
    struct sysret sr = cap_invoke1(kernel_cap, KernelCmd_GetGlobalPhys);
    if (err_is_ok(sr.error)) {
        *global = sr.value;
    }

    return sr.error;
}

/*
 * MVA extensions
 */

static inline errval_t invoke_kernel_get_platform_info(struct capref         kernel_cap,
                                                       struct platform_info *pi)
{
    struct sysret sr = cap_invoke2(kernel_cap, KernelCmd_Get_platform, (lvaddr_t)pi);
    if (err_is_fail(sr.error)) {
        pi->arch     = PI_ARCH_UNKNOWN;
        pi->platform = PI_PLATFORM_UNKNOWN;
    }

    return sr.error;
}

#endif
