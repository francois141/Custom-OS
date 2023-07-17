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
 * @brief Interface for managing processes
 *
 * Process management interface. The interface should work for all processes other than
 * the process manager through the corresponding RPCs (see lib/proc_mgmt) and the client library.
 * Moreover, the same interface should also work for the process manager itself
 * (see usr/init/proc_mgmt.c)
 */


#ifndef LIB_PROC_MGMT_H_
#define LIB_PROC_MGMT_H_ 1

#include <sys/cdefs.h>
#include <errors/errno.h>
#include <barrelfish_kpi/types.h>

#include <aos/threads.h>

// maximum number of cores supported by proc_mgmt
#define PROC_MGMT_MAX_CORES 4

__BEGIN_DECLS

struct async_channel;

/*
 * ------------------------------------------------------------------------------------------------
 * Process Manager state
 * ------------------------------------------------------------------------------------------------
 */

/*
struct proc_mgmt_rpc_spawn_proc {
    int argc;
    char argv[0];
};

struct proc_mgmt_rpc_spawn_proc_resp {
    errval_t err;
    domainid_t pid;
};
*/

// Linked list containing the processes waiting for a process to exit
struct proc_mgmt_exit_waiting_proc {
    struct event_closure                resume_fn;
    int                                *exit_code;
    struct proc_mgmt_exit_waiting_proc *next;
};

// Linked list containing all processes information
struct proc_mgmt_element {
    struct spawninfo         *si;
    struct proc_mgmt_exit_waiting_proc *waiting_procs;
    struct proc_mgmt_element *next;
};


struct proc_mgmt_state {
    // recursive mutex used for thread safety
    struct thread_mutex mutex;

    // Number of processes handled by this state
    size_t nb_processes_running;
    // list of all the processes handled
    struct proc_mgmt_element *procs;

    // next pid to be attributed
    domainid_t next_pid;
};


/**
 * @brief Initialize the process manager, should only be called once per core
 *
 * @return SYS_ERR_OK on sucess, error value on failure
 */
errval_t proc_mgmt_init(void);

/*
 * ------------------------------------------------------------------------------------------------
 * Spawning a new process
 * ------------------------------------------------------------------------------------------------
 */

errval_t proc_mgmt_spawn_mapped(int argc, const char *argv[], int capc, struct capref capv[],
                                coreid_t core, domainid_t *pid, struct capref stdin_frame,
                                struct capref stdout_frame);

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
                                   coreid_t core, domainid_t *pid);


/**
 * @brief spawns a new process with the given argv values on the given core.
 *
 * @param[in]  argc  the number of arguments expected in the argv array
 * @param[in]  argv  array of null-terminated strings containing the arguments
 * @param[in]  core  id of the core to spawn the program on
 * @param[out] pid   returned program id (PID) of the spawned process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: concatenating all values of argv into a single string should yield the
 * command line of the process to be spawned.
 */
static inline errval_t proc_mgmt_spawn_program_argv(int argc, const char *argv[], coreid_t core,
                                                    domainid_t *pid)
{
    return proc_mgmt_spawn_with_caps(argc, argv, 0, NULL, core, pid);
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
errval_t proc_mgmt_spawn_with_cmdline(const char *cmdline, coreid_t core, domainid_t *pid);


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
errval_t proc_mgmt_spawn_program(const char *path, coreid_t core, domainid_t *pid);


/*
 * ------------------------------------------------------------------------------------------------
 * Listing of Processes
 * ------------------------------------------------------------------------------------------------
 */

typedef enum proc_state {
    PROC_STATE_UNKNOWN,   ///< the process state is unknown.
    PROC_STATE_SPAWNING,  ///< the process is spawning.
    PROC_STATE_RUNNING,   ///< the process is running normally.
    PROC_STATE_PAUSED,    ///< the process has been paused
    PROC_STATE_EXITED,    ///< the process has exited.
    PROC_STATE_KILLED,    ///< the process has been killed.
} proc_state_t;


static inline const char *proc_state_str(proc_state_t state) {
    static const char *states[] = {
        "UNKNOWN",
        "SPAWNING",
        "RUNNING",
        "PAUSED",
        "EXITED",
        "KILLED",
    };
    return states[state];
}

///< represents the status of a process
struct proc_status {
    coreid_t     core;          ///< the core the process is running on.
    domainid_t   pid;           ///< the prcess id
    char         cmdline[128];  ///< the command line of the process
    proc_state_t state;         ///< the state of the process
    int          exit_code;     ///< the exit code of the process
};

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
errval_t proc_mgmt_ps(struct proc_status **ps, size_t *num);


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
errval_t proc_mgmt_get_proc_list(domainid_t **pids, size_t *num);


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
errval_t proc_mgmt_get_pid_by_name(const char *name, domainid_t *pid);


/**
 * @brief obtains the PID for the calling process
 *
 * @return PID of the calling process
 */
static inline domainid_t proc_mgmt_get_self_pid(void)
{
    return disp_get_domain_id();
}


/**
 * @brief obtains the status of a process with the given PID
 *
 * @param[in]  pid     the PID of the process to obtain the status for
 * @param[out] status  status struct filled in with process information
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_get_status(domainid_t pid, struct proc_status *status);


/**
 * @brief obtains the name of a process with the given PID
 *
 * @param[in] did   the PID of the process
 * @param[in] name  buffer to store the name in
 * @param[in] len   length of the name buffer
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_get_name(domainid_t pid, char *name, size_t len);

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
errval_t proc_mgmt_suspend(domainid_t pid);


/**
 * @brief resumes the execution of a process
 *
 * @param[in] pid  the PID of the process to resume
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_resume(domainid_t pid);


/*
 * ------------------------------------------------------------------------------------------------
 * Termination of a Process
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief tells the process manager that the calling process terminated with the given status
 *
 * @param[in] status  integer value with the given status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this function should not be called by the process directly, but from the exit code
 *       when main returns. Moreover, the function should make sure that the process is
 *       no longer scheduled. The status is the return value of main(), or the error value
 *       e.g., page fault or alike.
 */
errval_t proc_mgmt_exit(int status);


/**
 * @brief waits for a process to have terminated
 *
 * @param[in]  pid     process identifier of the process to wait for
 * @param[out] status  returns the status of the process as set by `proc_mgmt_exit()`
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_wait(domainid_t pid, int *status);


/**
 * @brief terminates the process with the given process id
 *
 * @param[in] pid  process identifier of the process to be killed
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t proc_mgmt_kill(domainid_t pid);


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
errval_t proc_mgmt_killall(const char *name);


__END_DECLS

#endif  // LIB_PROC_MGMT_H_