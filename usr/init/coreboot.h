/**
 * \file coreboot.h
 * \brief boot new core
 */

/*
 * Copyright (c) 2020, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef LIBBARRELFISH_COREBOOT_H
#define LIBBARRELFISH_COREBOOT_H

#include <sys/cdefs.h>
#include <errors/errno.h>
#include <barrelfish_kpi/types.h>

__BEGIN_DECLS

/// the maximum cores we support
#define COREBOOT_MAX_CORES 2

typedef enum corestate {
    CORE_STATE_UNKNOWN,
    CORE_STATE_OFF,
    CORE_STATE_RUNNING,
    CORE_STATE_SLEEPING,
} corestate_t;

struct corestatus {
    coreid_t    core;
    hwid_t      mpid;
    corestate_t state;
    const char *cpudriver;
    const char *init;
};


/**
 * @brief boots a new core with the provided mpid
 *
 * @param[in]  mpid         The ARM MPID of the core to be booted
 * @param[in]  boot_driver  Path of the boot driver binary
 * @param[in]  cpu_driver   Path of the CPU driver binary
 * @param[in]  init         Path to the init binary
 * @param[out] core         Returns the coreid of the booted core
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_boot_core(hwid_t mpid, const char *boot_driver, const char *cpu_driver,
                            const char *init, struct capref urpc_frame, coreid_t *core);

/**
 * @brief shutdown the execution of the given core and free its resources
 *
 * @param[in] core  Coreid of the core to be shut down
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_shutdown_core(coreid_t core);

/**
 * @brief shuts down the core and reboots it using the provided arguments
 *
 * @param[in] core         Coreid of the core to be rebooted
 * @param[in] boot_driver  Path of the boot driver binary
 * @param[in] cpu_driver   Path of the CPU driver binary
 * @param[in] init         Path to the init binary
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_reboot_core(coreid_t core, const char *boot_driver, const char *cpu_driver,
                              const char *init);

/**
 * @brief suspends (halts) the execution of the given core
 *
 * @param[in] core  Coreid of the core to be suspended
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_suspend_core(coreid_t core);

/**
 * @brief resumes the execution of the given core
 *
 * @param[in] core  Coreid of the core to be resumed
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_resume_core(coreid_t core);

/**
 * @brief obtains the number of cores present in the system.
 *
 * @param[out] num_cores  returns the number of cores in the system
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: This function should return the number of cores that the system supports
 */
errval_t coreboot_get_num_cores(coreid_t *num_cores);

/**
 * @brief obtains the status of a core in the system.
 *
 * @param[in]  core    the ID of the core to obtain the status from
 * @param[out] status  status struct filled in
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_get_core_status(coreid_t core, struct corestatus *status);

__END_DECLS

#endif
