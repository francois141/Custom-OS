/*
 * Copyright (c) 2022 The University of British Columbia
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

/**
 * @file
 * @brief Client interface for managing processes
 *
 * Note, this library is intended to be used by processes other than the process which
 * runs the process management server. All functions are basically wrappers around the
 * corresponding RPC calls.
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <proc_mgmt/proc_mgmt.h>
#include "../../usr/iox/iox.h"

/*
 * ------------------------------------------------------------------------------------------------
 * Spawning a new process
 * ------------------------------------------------------------------------------------------------
 */

errval_t proc_mgmt_spawn_mapped(int argc, const char *argv[], int capc, struct capref capv[],
                                   coreid_t core, domainid_t *pid, struct capref stdin_frame,
                                   struct capref stdout_frame)
{
    return aos_rpc_proc_spawn_mapped(aos_rpc_get_process_channel(), argc, argv, capc, capv, core,
                                     pid, stdin_frame, stdout_frame);
}

/**
 * @brief spawns a new process with the given arguments and capabilities on the given core.
 *
 * @param[in]  argc  the number of arguments expected in the argv array
 * @param[in]  argv  array of null-terminated strings containing the arguments
 * @param[in]  capc  the number of capabilities to pass to the new process
 * @param[in]  capv  array of capabilitiies to pass to the child
 * @param[in]  core  id of the core to spawn the program on
 * @param[out] pid   returned program id (PID) of the spawned process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: concatenating all values of argv into a single string should yield the
 * command line of the process to be spawned.
 */
errval_t proc_mgmt_spawn_with_caps(int argc, const char *argv[], int capc, struct capref capv[],
                                   coreid_t core, domainid_t *pid)
{
    return aos_rpc_proc_spawn_with_caps(aos_rpc_get_process_channel(), argc, argv, capc, capv, core,
                                        pid);
}


/**
 * @brief spawns a new process with the given commandline arguments on the given core
 *
 * @param[in]  cmdline  commandline of the programm to be spawned
 * @param[in]  core     id of the core to spawn the program on
 * @param[out] pid      returned program id (PID) of the spawned process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this function should replace the default commandline arguments the program.
 */
errval_t proc_mgmt_spawn_with_cmdline(const char *cmdline, coreid_t core, domainid_t *pid)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_spawn_with_cmdline(chan, cmdline, core, pid);
}


/**
 * @brief spawns a nww process with the default arguments on the given core
 *
 * @param[in]  path  string containing the path to the binary to be spawned
 * @param[in]  core  id of the core to spawn the program on
 * @param[out] pid   returned program id (PID) of the spawned process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this function should spawn the program with the default arguments as
 *       listed in the menu.lst file.
 */
errval_t proc_mgmt_spawn_program(const char *path, coreid_t core, domainid_t *pid)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_spawn_with_default_args(chan, path, core, pid);
}


/*
 * ------------------------------------------------------------------------------------------------
 * Listing of Processes
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief obtains the statuses of running processes from the process manager
 *
 * @param[out] ps    array of process status in the system (must be freed by the caller)
 * @param[out] num   the number of processes in teh list
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: the caller is responsible for freeing the array of process statuses.
 *       note: you may use the combination of the functions below to implement this one.
 */
errval_t proc_mgmt_ps(struct proc_status **ps, size_t *num)
{
    errval_t    err;
    domainid_t *pids;
    size_t      num_proc;

    err = proc_mgmt_get_proc_list(&pids, &num_proc);
    if (err_is_fail(err))
        return err;

    struct proc_status *statuses = malloc(sizeof(struct proc_status) * num_proc);
    for (size_t i = 0; i < num_proc; i++) {
        err = proc_mgmt_get_status(pids[i], &statuses[i]);
        if (err_is_fail(err)) {
            free(pids);
            free(statuses);
            return err;
        }
    }

    free(pids);
    *num = num_proc;
    *ps  = statuses;
    return SYS_ERR_OK;
}


/**
 * @brief obtains the list of running processes from the process manager
 *
 * @param[out] pids  array of process ids in the system (must be freed by the caller)
 * @param[out] num   the number of processes in teh list
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: the caller is responsible for freeing the array of process ids.
 */
errval_t proc_mgmt_get_proc_list(domainid_t **pids, size_t *num)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_get_all_pids(chan, pids, num);
}


/**
 * @brief obtains the PID for a process name
 *
 * @param[in]  name  name of the process to obtain the PID for
 * @param[out] pid   returned program id (PID) of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: Names that are an absoute path should match precisely on the full path.
 *       Names that just include the binary name may match all processes with the
 *       same name. If there are multiple matches
 */
errval_t proc_mgmt_get_pid_by_name(const char *name, domainid_t *pid)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_get_pid(chan, name, pid);
}


/**
 * @brief obtains the status of a process with the given PID
 *
 * @param[in] pid
 * @param[out] status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_get_status(domainid_t pid, struct proc_status *status)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    status->pid          = pid;

    return aos_rpc_proc_get_status(chan, pid, &status->core, status->cmdline,
                                   sizeof(status->cmdline), (uint8_t *)&status->state,
                                   &status->exit_code);
}


/**
 * @brief obtains the name of a process with the given PID
 *
 * @param[in] did   the PID of the process
 * @param[in] name  buffer to store the name in
 * @param[in] len   length of the name buffer
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_get_name(domainid_t pid, char *name, size_t len)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_get_name(chan, pid, name, len);
}


/*
 * ------------------------------------------------------------------------------------------------
 * Pausing and Resuming of Processes
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief pauses the execution of a process
 *
 * @param[in] pid  the PID of the process to pause
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_suspend(domainid_t pid)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_pause(chan, pid);
}


/**
 * @brief resumes the execution of a process
 *
 * @param[in] pid  the PID of the process to resume
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_resume(domainid_t pid)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_resume(chan, pid);
}


/*
 * ------------------------------------------------------------------------------------------------
 * Termination of a Process
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief tells the process manager that the calling process terminated with the given
 * status
 *
 * @param[in] status  integer value with the given status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this function will exit the calling process and thus should not return. It tells
 * the process manager its exit status value. The status is the return value of main(), or
 * the error value e.g., page fault or alike.
 */
errval_t proc_mgmt_exit(int status)
{
    iox_destroy();
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    errval_t        err  = aos_rpc_proc_exit(chan, status);
    if (err_is_fail(err))
        return err;

    // make sure we don't return
    while (true)
        thread_yield();

    return SYS_ERR_OK;
}


/**
 * @brief waits for a process to have terminated
 *
 * @param[in]  pid     process identifier of the process to wait for
 * @param[out] status  returns the status of the process as set by `proc_mgmt_exit()`
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_wait(domainid_t pid, int *status)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_wait(chan, pid, status);
}


/**
 * @brief terminates the process with the given process id
 *
 * @param[in] pid  process identifier of the process to be killed
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_kill(domainid_t pid)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_kill(chan, pid);
}


/**
 * @brief terminates all processes that match the given name
 *
 * @param[in] name   null-terminated string of the processes to be terminated
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * The all processes that have the given name should be terminated. If the name is
 * an absolute path, then there must be an exact match. If the name only contains the
 * binary name, then any processes with the same binary name should be terminated.
 *
 * Good students may implement regular expression matching for the name.
 */
errval_t proc_mgmt_killall(const char *name)
{
    struct aos_rpc *chan = aos_rpc_get_process_channel();
    return aos_rpc_proc_kill_all(chan, name);
}
