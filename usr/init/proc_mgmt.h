/*
 * Copyright (c) 2022 The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef INIT_PROC_MGMT_H_
#define INIT_PROC_MGMT_H_ 1

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <proc_mgmt/proc_mgmt.h>

/**
 * @brief terminates the process in respose to an exit message
 *
 * @param[in] pid     the PID of the process to be terminated
 * @param[in] status  exit code
 *
 * @return SYS_ERR_OK on sucess, error value on failure
 */
errval_t proc_mgmt_terminated(domainid_t pid, int status);

/**
 * @brief registers a channel to be triggered when the process exits
 *
 * @param[in] pid   the PID of the process to register a trigger for
 * @param[in] resume_fn     event to be called once the process exits
 * @param[out] exit_code     to be set as the process exit code
 *
 * @return SYS_ERR_OK on sucess, SPANW_ERR_* on failure
 */
errval_t proc_mgmt_register_wait(domainid_t pid, struct event_closure resume_fn, int *exit_code);

struct simple_async_channel;
/**
 * @brief get the simple async channel associated with the given pid
 *
 * @param[in] pid   the PID of the process to register a trigger for
 * @param[out] async   the async channel associated with the pid
 *
 * @return SYS_ERR_OK on sucess, SPANW_ERR_* on failure
 */
errval_t proc_mgmt_get_async(domainid_t pid, struct simple_async_channel** async);

#endif /* INIT_PROC_MGMT_H_ */