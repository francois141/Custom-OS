/**
 * \file
 * \brief Debugging functions
 */

/*
 * Copyright (c) 2008, 2010, 2011, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef BARRELFISH_DEBUG_H
#define BARRELFISH_DEBUG_H

#include <sys/cdefs.h>

#include <errors/errno.h>
#include <aos/caddr.h>
#include <stddef.h>
#include <stdlib.h>
#include <barrelfish_kpi/registers_arch.h>

__BEGIN_DECLS

// forward declaration
struct capability;

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
void debug_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

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
void debug_err(const char *file, const char *func, int line, errval_t err, const char *msg, ...);

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
void debug_warn(const char *file, const char *func, int line, const char *msg, ...);


#ifdef NDEBUG
#define DEBUG_PRINTF(fmt...)   ((void)0)
#define DEBUG_ERR(err, msg...) ((void)0)
#define HERE                   ((void)0)
#else
#define DEBUG_PRINTF(fmt...)   debug_printf(fmt);
#define DEBUG_ERR(err, msg...) debug_err(__FILE__, __FUNCTION__, __LINE__, err, msg)
#define DEBUG_WARN(msg...)     debug_warn(__FILE__, __FUNCTION__, __LINE__, msg)
#include <aos/dispatch.h>
#define HERE                                                                                       \
    debug_printf("Disp %.*s.%u: %s, %s, %u\n", DISP_NAME_LEN, disp_name(), disp_get_core_id(),     \
                 __FILE__, __func__, __LINE__)
#endif

/// Prints the unimplemented message
#define UNIMPLEMENTED() DEBUG_WARN("Function `%s` has not yet been implemented.\n", __FUNCTION__)


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// PANIC
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
    __attribute__((noreturn));


/**
 * @brief Prints out a message and then aborts the domain
 */
#define USER_PANIC_ERR(err, msg...)                                                                \
    do {                                                                                           \
        debug_err(__FILE__, __func__, __LINE__, err, msg);                                         \
        abort();                                                                                   \
    } while (0)

/**
 * @brief Prints out a message and then aborts the domain
 */
#define USER_PANIC(msg...) user_panic_fn(__FILE__, __func__, __LINE__, msg);


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Printing CSpace
//
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Dumps the CSpace of the given L1CNode root capability
 *
 * @param[in] root  The L1Cnode root capability of the CSpace to dump
 */
void debug_dump_cspace(struct capref root);

/**
 * @brief Dumps the CSpace of the current domain.
 */
static inline void debug_dump_my_cspace(void)
{
    debug_dump_cspace(cap_root);
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
int debug_snprint_capref(char *buf, size_t len, struct capref cap);

/**
 * @brief Prints the given capref to stdout
 *
 * @param[in] cap  Capref to be printed
 */
void debug_print_capref(struct capref cap);

/**
 * @brief Prints the given cnoderef into the supplied buffer
 *
 * @param[in] buf    Buffer to print the cnoderef in
 * @param[in] len    Length of the supplied buffer in bytes
 * @param[in] cnode  Cnoderef to be printed
 *
 * @return Number of bytes written to the buffer
 */
int debug_snprint_cnoderef(char *buf, size_t len, struct cnoderef cnode);

/**
 * @brief Prints the given cnoderef to stdout
 *
 * @param[in] cnode  Cnoderef to be printed
 */
void debug_print_cnoderef(struct cnoderef cnode);


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
int debug_snprint_capability(char *buf, size_t len, struct capability *cap);

/**
 * @brief prints the given capability to stdout
 *
 * @param[in] cap  Capability to be printed
 */
void debug_print_capability(struct capability *cap);

/**
 * @brief prints the capability at the given capref into the supplied buffer
 *
 * @param[in] buf  Buffer to print the capability in
 * @param[in] len  Length of the supplied buffer in bytes
 * @param[in] cap  Capref of the capability to be printed
 *
 * @return Number of bytes written to the buffer
 */
int debug_snprint_cap_at_capref(char *buf, size_t len, struct capref cap);

/**
 * @brief prints the capability at the given capref to stdout
 *
 * @param[in] cap  Capref of the capability to be printed
 */
void debug_print_cap_at_capref(struct capref cap);


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Printing VSPACE
//
////////////////////////////////////////////////////////////////////////////////////////////////////


errval_t debug_dump_hw_ptables(void *);


errval_t debug_cap_identify(struct capref cap, struct capability *ret);
errval_t debug_cap_trace_ctrl(uintptr_t types, genpaddr_t start_addr, gensize_t size);


void debug_print_save_area(arch_registers_state_t *state);
void debug_print_fpu_state(arch_registers_state_t *state);
void debug_dump(arch_registers_state_t *state);
void debug_call_chain(arch_registers_state_t *state);
void debug_return_addresses(void);
void debug_dump_mem_around_addr(lvaddr_t addr);
void debug_dump_mem(lvaddr_t base, lvaddr_t limit, lvaddr_t point);


__END_DECLS

#endif  // BARRELFISH_DEBUG_H
