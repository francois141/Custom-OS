/**
 * \file
 * \brief create child process library
 */

/*
 * Copyright (c) 2016, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef _LIB_SPAWN_H_
#define _LIB_SPAWN_H_ 1

#include <barrelfish_kpi/types.h>
#include <aos/aos_rpc.h>
#include <fs/fat32.h>
#include <aos/simple_async_channel.h>



// forward declarations
struct elfimg;
struct bootinfo;
struct waitset;


/**
 * @brief represents the state of the process
 */
typedef enum spawnstate {
    SPAWN_STATE_UNKNOWN = 0,  ///< unknown state
    SPAWN_STATE_SPAWNING,     ///< process is being constructed
    SPAWN_STATE_READY,        ///< process is ready to run for the first time (hasn't run yet)
    SPAWN_STATE_RUNNING,      ///< process is running
    SPAWN_STATE_SUSPENDED,    ///< process is stopped, but has been running before
    SPAWN_STATE_KILLED,       ///< process has been killed
    SPAWN_STATE_TERMINATED,   ///< process has terminated (exited normally)
    SPAWN_STATE_CLEANUP,      ///< process is being cleaned up
} spawn_state_t;


/**
 * @brief structure to keep track track of the spawned process
 *
 * Hint: this structure is intended to keep track of the resources that were allocated
 * during the spawning of the process, and keep track of the state of the process throughout
 * its lifetime.
 *
 * Hint: the fields in this structure are suggestions of what you might want to keep track of,
 * but this structure is far from complete.
 */
struct spawninfo {
    /// name of the binary this process runs
    char *binary_name;

    /// the full commandline of this process, including its arguments
    char *cmdline;

    /// PID of this process
    domainid_t pid;

    /// execution state of this process
    spawn_state_t state;

    /// exit code of this process, or zero if it hasn't exited yet
    int exitcode;

    // RPC server for the child process
    struct aos_rpc rpc_server;
    // secondary async channel that can be set up
    struct simple_async_channel async;

    // L1 Cnode used for the child process
    struct capref cspace;
    // L0 page table used for the child process
    struct capref vspace;

    // Dispatcher associated with the child process
    struct capref dispatcher;

    /// Amount of bytes in memory that has been granted
    size_t mem;
};

/**
 * @brief Parse a command line into argc and argv
 */
char **spawn_parse_args(const char *opts, int *argc_dest);

/**
 * @brief load an elf using the path from the bootinfo struct
 *          For a later milestone: look in the system for the file
 *
 * @param[in] bi    pointer to the bootinfo struct
 * @param[in] name  name of the binary in the bootinfo struct
 * @param[out] img   pointer to the elf image which will be loaded
 * @param[out] argc  default parameter number, can be NULL (to be freed by caller)
 * @param[out] argv  default parameters, can be NULL (to be freed by caller)
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note, this function prepares a new process for running, but it does not make it
 * runnable. See spawn_start().
 */
errval_t spawn_load_elf(struct bootinfo *bi, const char *name, struct elfimg *img, int *argc,
                        char ***argv);

/**
 * @brief constructs a new process by loading the image from the bootinfo struct
 *
 * @param[in] si    spawninfo structure to fill in
 * @param[in] bi    pointer to the bootinfo struct
 * @param[in] name  name of the binary in the bootinfo struct
 * @param[in] pid   the process id (PID) for the new process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note, this function prepares a new process for running, but it does not make it
 * runnable. See spawn_start().
 */
errval_t spawn_load_with_bootinfo(struct spawninfo *si, struct bootinfo *bi, const char *name,
                                  domainid_t pid);

errval_t spawn_load_filesystem(const char *path, struct elfimg *img, int *argc,
                               char ***argv);

errval_t spawn_load_mapped(struct spawninfo *si, struct elfimg *img, int argc,
                           const char *argv[], int capc, struct capref caps[], domainid_t pid,
                           struct capref stdin_frame, struct capref stdout_frame);

/**
 * @brief constructs a new process from the provided image pointer
 *
 * @param[in] si    spawninfo structure to fill in
 * @param[in] img   pointer to the elf image in memory
 * @param[in] argc  number of arguments in argv
 * @param[in] argv  command line arguments
 * @param[in] capc  number of capabilities in the caps array
 * @param[in] caps  array of capabilities to pass to the child
 * @param[in] pid   the process id (PID) for the new process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note, this function prepares a new process for running, but it does not make it
 * runnable. See spawn_start().
 */
errval_t spawn_load_with_caps(struct spawninfo *si, struct elfimg *img, int argc,
                              const char *argv[], int capc, struct capref caps[], domainid_t pid);

/**
 * @brief constructs a new process by loading the image from the provided module
 *
 * @param[in] si    spawninfo structure to fill in
 * @param[in] img   pointer to the elf image in memory
 * @param[in] argc  number of arguments in argv
 * @param[in] argv  command line arguments
 * @param[in] pid   the process id (PID) for the new process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note, this function prepares a new process for running, but it does not make it
 * runnable. See spawn_start().
 */
static inline errval_t spawn_load_with_args(struct spawninfo *si, struct elfimg *img, int argc,
                                            const char *argv[], domainid_t pid)
{
    return spawn_load_with_caps(si, img, argc, argv, 0, NULL, pid);
}

/**
 * @brief starts the execution of the new process by making it runnable
 *
 * @param[in] si   spawninfo structure of the constructed process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_start(struct spawninfo *si);

/**
 * @brief resumes the execution of a previously stopped process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_resume(struct spawninfo *si);

/**
 * @brief stops (suspends) the execution of a running process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_suspend(struct spawninfo *si);

/**
 * @brief kills the execution of a running process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_kill(struct spawninfo *si);

/**
 * @brief marks the process as having exited
 *
 * @param[in] si        spawninfo structure of the process
 * @param[in] exitcode  exit code of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * The process manager should call this function when it receives the exit
 * notification from the child process. The function makes sure that the
 * process is no longer running and can be cleaned up later on.
 */
errval_t spawn_exit(struct spawninfo *si, int exitcode);

/**
 * @brief cleans up the resources of a process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: The process has to be stopped before calling this function.
 */
errval_t spawn_cleanup(struct spawninfo *si);

/**
 * @brief initializes the IPC channel for the process
 *
 * @param[in] si       spawninfo structure of the process
 * @param[in] ws       waitset to be used
 * @param[in] handler  message handler for the IPC channel
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this functionality is required for the IPC milestone.
 *
 * Hint: the IPC subsystem should be initialized before the process is being run for
 * the first time.
 */
errval_t spawn_setup_ipc(struct spawninfo *si, struct waitset *ws, struct handler_closure handler);

/**
 * @brief sets the receive handler function for the message channel
 *
 * @param[in] si       spawninfo structure of the process
 * @param[in] handler  handler function to be set
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_set_recv_handler(struct spawninfo *si, aos_recv_handler_fn handler);


#endif /* _LIB_SPAWN_H_ */
