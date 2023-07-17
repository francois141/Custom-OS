/**
 * \file
 * \brief Debugging functions
 */

/*
 * Copyright (c) 2008-2011, ETH Zurich.
 * Copyright (c) 2015, 2016 Hewlett Packard Enterprise Development LP.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <aos/aos.h>
#include <aos/caddr.h>
#include <aos/debug.h>
#include <aos/sys_debug.h>
#include <aos/dispatch.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <barrelfish_kpi/dispatcher_shared.h>
#include <stdio.h>


#define DISP_MEMORY_SIZE 1024  // size of memory dump in bytes


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Print Functions
//
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Debug print function that uses the kernel sys_print
 *
 * @param[in] fmt   Format string (printf style)
 */
void debug_printf(const char *fmt, ...)
{
    struct thread *me = thread_self();
    va_list        argptr;
    char           id[32] = "-";
    char           str[1024];
    size_t         len;

    if (me) {
        snprintf(id, sizeof(id), "%" PRIuPTR, thread_get_id(me));
    }
    len = snprintf(str, sizeof(str), "\033[34m%.*s.\033[31m%u.%s\033[0m: ", DISP_NAME_LEN,
                   disp_name(), disp_get_current_core_id(), id);
    if (len < sizeof(str)) {
        va_start(argptr, fmt);
        vsnprintf(str + len, sizeof(str) - len, fmt, argptr);
        va_end(argptr);
    }
    sys_print(str, sizeof(str));
}

/**
 * @brief Prints out the message with the location and error information
 *
 * @param[in] file  Source file of the caller
 * @param[in] func  Function name of the caller
 * @param[in] line  Line number of the caller
 * @param[in] err   error number
 * @param[in] msg   Message format to be printed (printf style)
 *
 * @note: do not use this function directly. Rather use the DEBUG_ERR macro
 */
void debug_err(const char *file, const char *func, int line, errval_t err, const char *msg, ...)
{
    va_list ap;
    char    str[256];
    size_t  len;

    char *leader = err_is_ok(err) ? "\x1B[1;32mSUCCESS" : "\x1B[1;91mERROR";

    len = snprintf(str, sizeof(str), "%s: %.*s.%u in %s() %s:%d\n%s: ", leader, DISP_NAME_LEN,
                   disp_name(), disp_get_current_core_id(), func, file, line, leader);
    if (msg != NULL) {
        va_start(ap, msg);
        len += vsnprintf(str + len, sizeof(str) - len, msg, ap);
        va_end(ap);
    }

    len += snprintf(str + len, sizeof(str) - len, "\x1B[0m\n");
    sys_print(str, len);

    if (err != 0) {
        leader = "Error calltrace:\n";
        sys_print(leader, strlen(leader));
        err_print_calltrace(err);
        sys_print("\n", 1);
    }
}


/**
 * @brief Prints out the message with the location
 *
 * @param[in] file  Source file of the caller
 * @param[in] func  Function name of the caller
 * @param[in] line  Line number of the caller
 * @param[in] msg   Message format to be printed (printf style)
 *
 * @note: do not use this function directly. Rather use the DEBUG_WARN macro
 */
void debug_warn(const char *file, const char *func, int line, const char *msg, ...)
{
    va_list argptr;
    char    str[256];
    size_t  len;

    len = snprintf(str, sizeof(str), "\x1B[1;33mWARNING: %.*s.%u in %s() at %s:%d :\n",
                   DISP_NAME_LEN, disp_name(), disp_get_current_core_id(), func, file, line);
    if (len < sizeof(str)) {
        va_start(argptr, msg);
        len += vsnprintf(str + len, sizeof(str) - len, msg, argptr);
        va_end(argptr);
    }
    len += snprintf(str + len, sizeof(str) - len, "\x1B[0m\n");
    sys_print(str, len);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Panic Function
//
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Prints a message and aborts the program
 *
 * @param[in] file  Source file of the caller
 * @param[in] func  Function name of the caller
 * @param[in] line  Line number of the caller
 * @param[in] msg   Message to print (printf style)
 *
 * Something irrecoverably bad happened. Print a panic message, then abort.
 *
 * Use the PANIC macros instead of calling this function directly.
 */
void user_panic_fn(const char *file, const char *func, int line, const char *msg, ...)
{
    va_list ap;
    char    msg_str[128];
    va_start(ap, msg);
    vsnprintf(msg_str, sizeof(msg_str), msg, ap);
    va_end(ap);

    char str[256];
    snprintf(str, sizeof(str), "\x1B[1;91m%.*s.%u in %s() %s:%d\n%s\x1B[0m\n", DISP_NAME_LEN,
             disp_name(), disp_get_current_core_id(), func, file, line, msg_str);
    sys_print(str, sizeof(str));

    abort();
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Printing CSpace
//
////////////////////////////////////////////////////////////////////////////////////////////////////


/**
 * @brief Walk and debug print a L2 CNode
 *
 * @param[in] l2cnode  The L2 CNode to walk
 */
static void walk_cspace_l2(struct capref l2cnode)
{
    errval_t          err;
    struct capability cap;
    struct cnoderef   cnode = build_cnoderef(l2cnode, 1);

    debug_printf("  Printing L2 CNode at L1 slot=%d\n", l2cnode.slot);

    for (unsigned i = 0; i < L2_CNODE_SLOTS; i++) {
        struct capref pos = { .cnode = cnode, .slot = i };

        // Get cap data
        err = invoke_cap_identify(pos, &cap);
        if (err_no(err) == SYS_ERR_IDENTIFY_LOOKUP || err_no(err) == SYS_ERR_CAP_NOT_FOUND
            || err_no(err) == SYS_ERR_LMP_CAPTRANSFER_SRC_LOOKUP) {
            continue;
        } else if (err_is_fail(err)) {
            DEBUG_ERR(err, "debug_cap_identify failed");
            return;
        }

        char   buf[256];
        size_t prpos = 0;

        prpos += snprintf(buf, sizeof(buf), "slot %" PRIuCADDR " caddr 0x%" PRIxCADDR " is a ",
                          pos.slot, get_cap_addr(pos));
        assert(prpos < sizeof(buf));
        prpos += debug_snprint_capability(&buf[prpos], sizeof(buf) - prpos, &cap);
        assert(prpos < sizeof(buf));
        debug_printf("    %s\n", buf);
    }
}

/**
 * @brief Dumps the CSpace of the given L1CNode root capability
 *
 * @param[in] root  The L1Cnode root capability of the CSpace to dump
 */
void debug_dump_cspace(struct capref root)
{
    struct capability root_cap;
    struct capability l2_cap;

    /* find out size of root cnode */
    errval_t err = invoke_cap_identify(root, &root_cap);
    assert(err_is_ok(err));
    assert(root_cap.type == ObjType_L1CNode);

    size_t c1size = 0;
    err           = invoke_cnode_get_size(root, &c1size);
    assert(err_is_ok(err));

    int l1slots = c1size / OBJSIZE_CTE;
    debug_printf("Printing L1 CNode (slots=%u)\n", l1slots);
    for (int slot = 0; slot < l1slots; slot++) {
        struct cnoderef cnode = build_cnoderef(root, 0);
        struct capref   pos   = { .cnode = cnode, .slot = slot };
        err                   = invoke_cap_identify(pos, &l2_cap);

        // If cap type was Null, kernel returns error
        if (err_no(err) == SYS_ERR_IDENTIFY_LOOKUP || err_no(err) == SYS_ERR_CAP_NOT_FOUND
            || err_no(err) == SYS_ERR_LMP_CAPTRANSFER_SRC_LOOKUP) {
            continue;
        } else if (err_is_fail(err)) {
            DEBUG_ERR(err, "debug_cap_identify failed");
            return;
        }
        walk_cspace_l2(pos);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Printing Capability Refernces
//
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Prints the given capref into the supplied buffer
 *
 * @param[in] buf  Buffer to print the capref in
 * @param[in] len  Length of the supplied buffer in bytes
 * @param[in] cap  Capref to be printed
 *
 * @return Number of bytes written to the buffer
 */
int debug_snprint_capref(char *buf, size_t len, struct capref cap)
{
    return snprintf(buf, len,
                    "CRootAddr:0x%08" PRIxCADDR ", "
                    "CNodeAddr:0x%08" PRIxCADDR ", CNodeLevel:%d, Slot:%" PRIuCADDR ", CapLevel:%d",
                    get_croot_addr(cap), get_cnode_addr(cap), get_cnode_level(cap), cap.slot,
                    get_cap_level(cap));
}


/**
 * @brief Prints the given capref to stdout
 *
 * @param[in] cap  Capref to be printed
 */
void debug_print_capref(struct capref cap)
{
    char buf[128];
    debug_snprint_capref(buf, sizeof(buf), cap);
    debug_printf("capref: %s\n", buf);
}


/**
 * @brief Prints the given cnoderef into the supplied buffer
 *
 * @param[in] buf    Buffer to print the cnoderef in
 * @param[in] len    Length of the supplied buffer in bytes
 * @param[in] cnode  Cnoderef to be printed
 *
 * @return Number of bytes written to the buffer
 */
int debug_snprint_cnoderef(char *buf, size_t len, struct cnoderef cnode)
{
    return snprintf(buf, len,
                    "CRootAddr:0x%08" PRIxCADDR ", CNodeAddr:0x%08" PRIxCADDR ", CNodeLevel:%d",
                    cnode.croot, cnode.cnode, cnode.level);
}


/**
 * @brief Prints the given cnoderef to stdout
 *
 * @param[in] cnode  Cnoderef to be printed
 *
 * @return Number of bytes written to the buffer
 */
void debug_print_cnoderef(struct cnoderef cnode)
{
    char buf[128];
    debug_snprint_cnoderef(buf, sizeof(buf), cnode);
    debug_printf("cnoderef: %s\n", buf);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Printing Capabilities
//
////////////////////////////////////////////////////////////////////////////////////////////////////


/**
 * @brief prints the given capability int the supplied buffer
 *
 * @param[in] buf  Buffer to print the capability in
 * @param[in] len  Length of the supplied buffer in bytes
 * @param[in] cap  Capability to be printed
 *
 * @return Number of bytes written to the buffer
 */
int debug_snprint_capability(char *buf, size_t len, struct capability *cap)
{
    char *mappingtype;

    switch (cap->type) {
    case ObjType_PhysAddr:
        return snprintf(buf, len,
                        "physical address range cap (0x%" PRIxGENPADDR ":%" PRIuGENSIZE ")",
                        cap->u.physaddr.base, cap->u.physaddr.bytes);

    case ObjType_RAM:
        return snprintf(buf, len, "RAM cap (0x%" PRIxGENPADDR ":%" PRIuGENSIZE ")", cap->u.ram.base,
                        cap->u.ram.bytes);

    case ObjType_L1CNode: {
        int ret = snprintf(buf, len,
                           "L1 CNode cap "
                           "(allocated bytes %#" PRIxGENSIZE ", rights mask %#" PRIxCAPRIGHTS ")",
                           cap->u.l1cnode.allocated_bytes, cap->u.l1cnode.rightsmask);
        return ret;
    }

    case ObjType_L2CNode: {
        return snprintf(buf, len,
                        "L2 CNode cap "
                        "(cnode=%" PRIxLPADDR ")"
                        "(rights mask %#" PRIxCAPRIGHTS ")",
                        cap->u.l2cnode.cnode, cap->u.l2cnode.rightsmask);
    }

    case ObjType_EndPointUMP:
        return snprintf(
            buf, len, "EndPoint UMP cap (0x%" PRIxGENPADDR ":%" PRIuGENSIZE "), if=%" PRIu32,
            cap->u.endpointump.base, cap->u.endpointump.bytes, cap->u.endpointump.iftype);

    case ObjType_Dispatcher:
        return snprintf(buf, len, "Dispatcher cap %p", cap->u.dispatcher.dcb);

    case ObjType_Frame:
        return snprintf(buf, len, "Frame cap (0x%" PRIxGENPADDR ":%" PRIuGENSIZE ")",
                        cap->u.frame.base, cap->u.frame.bytes);

    case ObjType_DevFrame:
        return snprintf(buf, len, "Device Frame cap (0x%" PRIxGENPADDR ":%" PRIuGENSIZE ")",
                        cap->u.frame.base, cap->u.devframe.bytes);

    case ObjType_VNode_ARM_l1:
        return snprintf(buf, len, "ARM L1 table at 0x%" PRIxGENPADDR, cap->u.vnode_arm_l1.base);

    case ObjType_VNode_ARM_l2:
        return snprintf(buf, len, "ARM L2 table at 0x%" PRIxGENPADDR, cap->u.vnode_arm_l2.base);

    case ObjType_VNode_AARCH64_l0:
        return snprintf(buf, len, "AARCH64 L0 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_aarch64_l0.base);

    case ObjType_VNode_AARCH64_l1:
        return snprintf(buf, len, "AARCH64 L1 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_aarch64_l1.base);

    case ObjType_VNode_AARCH64_l2:
        return snprintf(buf, len, "AARCH64 L2 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_aarch64_l2.base);

    case ObjType_VNode_AARCH64_l3:
        return snprintf(buf, len, "AARCH64 L3 table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_aarch64_l3.base);

    case ObjType_VNode_x86_32_ptable:
        return snprintf(buf, len, "x86_32 Page table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_32_ptable.base);

    case ObjType_VNode_x86_32_pdir:
        return snprintf(buf, len, "x86_32 Page directory at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_32_pdir.base);

    case ObjType_VNode_x86_32_pdpt:
        return snprintf(buf, len, "x86_32 PDPT at 0x%" PRIxGENPADDR, cap->u.vnode_x86_32_pdpt.base);

    case ObjType_VNode_x86_64_ptable:
        return snprintf(buf, len, "x86_64 Page table at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_64_ptable.base);

    case ObjType_VNode_x86_64_pdir:
        return snprintf(buf, len, "x86_64 Page directory at 0x%" PRIxGENPADDR,
                        cap->u.vnode_x86_64_pdir.base);

    case ObjType_VNode_x86_64_pdpt:
        return snprintf(buf, len, "x86_64 PDPT at 0x%" PRIxGENPADDR, cap->u.vnode_x86_64_pdpt.base);

    case ObjType_VNode_x86_64_pml4:
        return snprintf(buf, len, "x86_64 PML4 at 0x%" PRIxGENPADDR, cap->u.vnode_x86_64_pml4.base);

    case ObjType_Frame_Mapping:
        mappingtype = "Frame";
        goto ObjType_Mapping;
    case ObjType_DevFrame_Mapping:
        mappingtype = "DevFrame";
        goto ObjType_Mapping;

    case ObjType_VNode_x86_64_pml4_Mapping:
        mappingtype = "x86_64 PML4";
        goto ObjType_Mapping;
    case ObjType_VNode_x86_64_pdpt_Mapping:
        mappingtype = "x86_64 PDPT";
        goto ObjType_Mapping;
    case ObjType_VNode_x86_64_pdir_Mapping:
        mappingtype = "x86_64 PDIR";
        goto ObjType_Mapping;
    case ObjType_VNode_x86_64_ptable_Mapping:
        mappingtype = "x86_64 PTABLE";
        goto ObjType_Mapping;

    case ObjType_VNode_x86_32_pdpt_Mapping:
        mappingtype = "x86_32 PDPT";
        goto ObjType_Mapping;
    case ObjType_VNode_x86_32_pdir_Mapping:
        mappingtype = "x86_32 PDIR";
        goto ObjType_Mapping;
    case ObjType_VNode_x86_32_ptable_Mapping:
        mappingtype = "x86_32 PTABLE";
        goto ObjType_Mapping;

    case ObjType_VNode_ARM_l1_Mapping:
        mappingtype = "ARM l1";
        goto ObjType_Mapping;
    case ObjType_VNode_ARM_l2_Mapping:
        mappingtype = "ARM l2";
        goto ObjType_Mapping;

    case ObjType_VNode_AARCH64_l0_Mapping:
        mappingtype = "AARCH64 l0";
        goto ObjType_Mapping;
    case ObjType_VNode_AARCH64_l1_Mapping:
        mappingtype = "AARCH64 l1";
        goto ObjType_Mapping;
    case ObjType_VNode_AARCH64_l2_Mapping:
        mappingtype = "AARCH64 l2";
        goto ObjType_Mapping;
    case ObjType_VNode_AARCH64_l3_Mapping:
        mappingtype = "AARCH64 l3";
        goto ObjType_Mapping;

    case ObjType_EndPointLMP:
        return snprintf(buf, len, "EndPoint cap (disp %p offset 0x%" PRIxLVADDR ")",
                        cap->u.endpointlmp.listener, cap->u.endpointlmp.epoffset);


    ObjType_Mapping:
        return snprintf(buf, len,
                        "%s Mapping (%s cap @%p, "
                        "ptable cap @0x%p, entry=%hu, pte_count=%hu)",
                        mappingtype, mappingtype, cap->u.frame_mapping.cap,
                        cap->u.frame_mapping.ptable, cap->u.frame_mapping.entry,
                        cap->u.frame_mapping.pte_count);

    case ObjType_IRQTable:
        return snprintf(buf, len, "IRQTable cap");

    case ObjType_IRQSrc:
        return snprintf(buf, len, "IRQSrc cap (vec: %" PRIu64 "-%" PRIu64 ")",
                        cap->u.irqsrc.vec_start, cap->u.irqsrc.vec_end);

    case ObjType_IRQDest:
        return snprintf(buf, len, "IRQDest cap (vec: %" PRIu64 ", cpu: %" PRIu64 ")",
                        cap->u.irqdest.vector, cap->u.irqdest.cpu);

    case ObjType_IO:
        return snprintf(buf, len, "IO cap (0x%hx-0x%hx)", cap->u.io.start, cap->u.io.end);

    case ObjType_Kernel:
        return snprintf(buf, len, "Kernel cap");

    case ObjType_KernelControlBlock:
        return snprintf(buf, len, "Kernel control block cap");

    case ObjType_ID:
        return snprintf(buf, len,
                        "ID capability (coreid 0x%" PRIxCOREID " core_local_id 0x%" PRIx32 ")",
                        cap->u.id.coreid, cap->u.id.core_local_id);

    case ObjType_ProcessManager:
        return snprintf(buf, len, "Process manager capability");

    case ObjType_Domain:
        return snprintf(buf, len,
                        "Domain capability (coreid 0x%" PRIxCOREID " core_local_id 0x%" PRIx32 ")",
                        cap->u.domain.coreid, cap->u.domain.core_local_id);

    case ObjType_PerfMon:
        return snprintf(buf, len, "PerfMon cap");

    case ObjType_Null:
        return snprintf(buf, len, "Null cap (empty slot)");

    default:
        return snprintf(buf, len, "UNKNOWN TYPE! (%d)", cap->type);
    }
}

/**
 * @brief prints the given capability to stdout
 *
 * @param[in] cap  Capability to be printed
 */
void debug_print_capability(struct capability *cap)
{
    char buf[256];
    debug_snprint_capability(buf, sizeof(buf), cap);
    debug_printf("Capability: %s\n", buf);
}


/**
 * @brief prints the capability at the given capref into the supplied buffer
 *
 * @param[in] buf  Buffer to print the capability in
 * @param[in] len  Length of the supplied buffer in bytes
 * @param[in] cap  Capref of the capability to be printed
 *
 * @return Number of bytes written to the buffer
 */
int debug_snprint_cap_at_capref(char *buf, size_t len, struct capref cap)
{
    struct capability capability;
    errval_t          err;

    if (capref_is_null(cap)) {
        return snprintf(buf, len, "(null cap)");
    }

    err = invoke_cap_identify(cap, &capability);
    if (err_is_fail(err)) {
        return snprintf(buf, len, "(ERROR identifying cap!)");
    } else {
        return debug_snprint_capability(buf, len, &capability);
    }
}


/**
 * @brief prints the capability at the given capref to stdout
 *
 * @param[in] cap  Capref of the capability to be printed
 */
void debug_print_cap_at_capref(struct capref cap)
{
    char buf[256];
    debug_snprint_cap_at_capref(buf, sizeof(buf), cap);
    debug_printf("Capability: %s\n", buf);
}


/*
 * Have this invocation here to make debug_cap_identify work for domains that
 * have no monitor connection but hold Kernel cap (e.g. init)
 */
static inline errval_t invoke_kernel_identify_cap(capaddr_t cap, int level, struct capability *out)
{
    return cap_invoke4(cap_kernel, KernelCmd_Identify_cap, cap, level, (uintptr_t)out).error;
}

errval_t debug_cap_identify(struct capref cap, struct capability *ret)
{
    if (get_cap_addr(cap) == 0) {
        return SYS_ERR_CAP_NOT_FOUND;
    }

    uint8_t   level = get_cap_level(cap);
    capaddr_t caddr = get_cap_addr(cap);
    errval_t  err   = invoke_kernel_identify_cap(caddr, level, ret);
    if (err_is_ok(err)) {
        // we have kernel cap, return result;
        return SYS_ERR_OK;
    }

    // TODO: For a generic implementation, query the monitor. See libbarrelfish.
    return err;
}

/**
 * \brief Enable fine-grained tracing of cap operations on address range
 * [start_addr, start_addr+size)
 * \arg types enable tracing for given set of ORed ObjType_s
 * \arg start_addr start of region to trace for
 * \arg size size of region to trace for
 */
errval_t debug_cap_trace_ctrl(uintptr_t types, genpaddr_t start_addr, gensize_t size)
{
    if (types) {
        printf("enabling pmem tracing: 0x%" PRIxGENPADDR "--0x%" PRIxGENPADDR " for types "
               "0x%" PRIxPTR "\n",
               start_addr, start_addr + size, types);
    }
    return sys_debug_cap_trace_ctrl(types, start_addr, size);
}

/**
 * \brief Dump own hw page tables
 */
errval_t debug_dump_hw_ptables(void *vaddr)
{
    return invoke_dispatcher_dump_ptables(cap_dispatcher, (lvaddr_t)vaddr);
}


void debug_dump_mem(lvaddr_t start_addr, lvaddr_t end_addr, lvaddr_t point)
{
    debug_printf("Dumping memory in range 0x%" PRIxLVADDR " to 0x%" PRIxLVADDR ":\n", start_addr,
                 end_addr);

    for (uintptr_t *p = (void *)start_addr; (uintptr_t)p < end_addr; p++) {
        uint8_t *bytes = (void *)p;
        char     buf[32];
        size_t   bufpos = 0;
        for (unsigned i = 0; i < sizeof(uintptr_t); i++) {
            bufpos += snprintf(&buf[bufpos], sizeof(buf) - bufpos, "%02x ", bytes[i]);
            assert(bufpos < sizeof(buf));
        }
        debug_printf("%p: %.*s %*" PRIxPTR "%s\n", p, (int)sizeof(buf), buf,
                     (int)sizeof(uintptr_t) * 2, *p,
                     p == (uintptr_t *)point ? " <== We are here" : "");
    }
}

void debug_dump_mem_around_addr(lvaddr_t addr)
{
    /* lvaddr_t page_aligned_addr = ROUND_DOWN(addr, BASE_PAGE_SIZE); */
    lvaddr_t start_addr = ROUND_DOWN(addr - DISP_MEMORY_SIZE / 2, sizeof(uintptr_t));
    lvaddr_t end_addr   = ROUND_UP(addr + 2 * DISP_MEMORY_SIZE, sizeof(uintptr_t));

    /* if (start_addr < page_aligned_addr) { */
    /*     start_addr = page_aligned_addr; */
    /* } */
    /* if (end_addr > page_aligned_addr + BASE_PAGE_SIZE) { */
    /*     end_addr = page_aligned_addr + BASE_PAGE_SIZE; */
    /* } */

    debug_dump_mem(start_addr, end_addr, addr);
}
