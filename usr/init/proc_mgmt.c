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
 * This file contains the process manager. It has basically the same interface as the
 * process manager client (see include/proc_mgmt/proc_mgmt.h). And a few additional functions
 */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include <pthread.h>

#include <aos/aos.h>
#include <spawn/spawn.h>
#include <spawn/multiboot.h>
#include <spawn/elfimg.h>
#include <spawn/argv.h>
#include <argparse/argparse.h>

#include "proc_mgmt.h"
#include "rpc_handler.h"

extern struct bootinfo *bi;
extern coreid_t         my_core_id;

/*
 * ------------------------------------------------------------------------------------------------
 * Utility Functions
 * ------------------------------------------------------------------------------------------------
 */

__attribute__((__used__)) static void spawn_info_to_proc_status(struct spawninfo   *si,
                                                                struct proc_status *status)
{
    status->core      = my_core_id;
    status->pid       = si->pid;
    status->exit_code = si->exitcode;
    strncpy(status->cmdline, si->cmdline, sizeof(status->cmdline));
    switch (si->state) {
    case SPAWN_STATE_SPAWNING:
        status->state = PROC_STATE_SPAWNING;
        break;
    case SPAWN_STATE_READY:
        status->state = PROC_STATE_SPAWNING;
        break;
    case SPAWN_STATE_RUNNING:
        status->state = PROC_STATE_RUNNING;
        break;
    case SPAWN_STATE_SUSPENDED:
        status->state = PROC_STATE_PAUSED;
        break;
    case SPAWN_STATE_KILLED:
        status->state     = PROC_STATE_KILLED;
        status->exit_code = -1;
        break;
    case SPAWN_STATE_TERMINATED:
        status->state     = PROC_STATE_EXITED;
        status->exit_code = si->exitcode;
        break;
    default:
        status->state = PROC_STATE_UNKNOWN;
    }
}

static inline bool _is_proc_killed(struct proc_mgmt_element *proc);
static inline bool _is_proc_killed(struct proc_mgmt_element *proc)
{
    return proc->si->state >= SPAWN_STATE_KILLED;
}

static inline bool _is_proc_not_killed(struct proc_mgmt_element *proc);
static inline bool _is_proc_not_killed(struct proc_mgmt_element *proc)
{
    return !_is_proc_killed(proc);
}

// return whether or not the proc name matches the search name (considering if search_name is an
// absolute path or not)
static bool _proc_mgmt_name_match(const char *proc_name, const char *search_name)
{
    // decide if search_name is absolute or not depending on if there is a / in it
    int search_name_idx = 0;
    while (search_name[search_name_idx] != 0 && search_name[search_name_idx] != '/') {
        search_name_idx++;
    }

    if (search_name[search_name_idx] == '/') {
        // absolute path
        return strcmp(proc_name, search_name) == 0;
    }

    // we need to get the filename for proc_name
    const char *proc_filename = proc_name;
    int         proc_name_idx = 0;
    while (proc_name[proc_name_idx] != 0) {
        if (proc_name[proc_name_idx] == '/') {
            // set the new start as after this
            proc_filename = proc_name + proc_name_idx + 1;
        }

        proc_name_idx++;
    }

    return strcmp(proc_filename, search_name) == 0;
}

/*
 * ------------------------------------------------------------------------------------------------
 * Initialization function
 * ------------------------------------------------------------------------------------------------
 */
errval_t proc_mgmt_init(void)
{
    struct proc_mgmt_state *pms = get_proc_mgmt_state();

    thread_mutex_init(&pms->mutex);

    pms->procs = NULL;

    // no process get pid 0
    // all pids from a process are equal modulo PROC_MGMT_MAX_CORES
    pms->next_pid = my_core_id;
    if (pms->next_pid == 0)
        pms->next_pid += PROC_MGMT_MAX_CORES;

    return SYS_ERR_OK;
}

/*
 * ------------------------------------------------------------------------------------------------
 * Spawning a new process
 * ------------------------------------------------------------------------------------------------
 */

static errval_t _load_elf_internal(const char *path, struct elfimg *img, int *argc, char ***argv) {
    if((strnlen(path, 7) >= 7 && strncasecmp("/SDCARD/", path,7) == 0)) {
        return spawn_load_filesystem(path, img, argc, argv);
    } else {
        return spawn_load_elf(bi, path, img, argc, argv);
    }
}

static errval_t _proc_mgmt_spawn_internal(struct elfimg *img, int argc, const char *argv[], int capc,
                                          struct capref capv[], coreid_t core, domainid_t *pid,
                                          struct capref stdin_frame, struct capref stdout_frame)
{
    errval_t                err = SYS_ERR_OK;
    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    assert(core == my_core_id);

    thread_mutex_lock_nested(&pms->mutex);

    domainid_t process_id = pms->next_pid;
    pms->next_pid += PROC_MGMT_MAX_CORES;
    // no need to keep the mutex locked while calling the spawn function
    thread_mutex_unlock(&pms->mutex);

    // zero-set spawninfo, for safety
    struct spawninfo *si = calloc(sizeof(struct spawninfo), 1);
    si->state            = SPAWN_STATE_SPAWNING;

    err = spawn_load_mapped(si, img, argc, argv, capc, capv, process_id, stdin_frame, stdout_frame);
    if (err_is_fail(err))
        return err;

    err = spawn_setup_ipc(si, get_default_waitset(), MKHANDLER(sync_rpc_request_handler, si));
    if (err_is_fail(err)) {
        return err;
    }

    // add the process at the beginning of the list
    struct proc_mgmt_element *proc_el = malloc(sizeof(struct proc_mgmt_element));
    thread_mutex_lock_nested(&pms->mutex);
    proc_el->next          = pms->procs;
    proc_el->si            = si;
    proc_el->waiting_procs = NULL;
    pms->procs             = proc_el;
    pms->nb_processes_running++;

    thread_mutex_unlock(&pms->mutex);

    *pid = process_id;

    return spawn_start(si);
}

errval_t proc_mgmt_spawn_mapped(int argc, const char *argv[], int capc, struct capref capv[],
                                coreid_t core, domainid_t *pid, struct capref stdin_frame,
                                struct capref stdout_frame)
{
    errval_t err = SYS_ERR_OK;

    if (argc < 1 || argv == NULL || pid == NULL)
        return ERR_INVALID_ARGS;

    struct elfimg img;
    const char   *path = argv[0];

    err = _load_elf_internal(path, &img, NULL, NULL);
    if(err_is_fail(err)) {
        return err;
    }

    // Note: With multicore support, you many need to send a message to the other core
    return _proc_mgmt_spawn_internal(&img, argc, argv, capc, capv, core, pid, stdin_frame, stdout_frame);
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
    return proc_mgmt_spawn_mapped(argc, argv, capc, capv, core, pid, NULL_CAP, NULL_CAP);
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
    if (cmdline == NULL || pid == NULL)
        return ERR_INVALID_ARGS;

    int      argc;
    char   **argv = spawn_parse_args(cmdline, &argc);
    errval_t err  = proc_mgmt_spawn_with_caps(argc, (const char **)argv, 0, NULL, core, pid);

    // free argv
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    free(argv);

    return err;
}


/**
 * @brief spawns a new process with the default arguments on the given core
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
    errval_t err = SYS_ERR_OK;

    if (path == NULL || pid == NULL)
        return ERR_INVALID_ARGS;

    struct elfimg img;
    int           argc;
    char        **argv;

    err = _load_elf_internal(path, &img, &argc, &argv);
    if (err_is_fail(err)) {
       return err;
    }

    // Note: With multicore support, you many need to send a message to the other core
    err = _proc_mgmt_spawn_internal(&img, argc, (const char **)argv, 0, NULL, core, pid, NULL_CAP, NULL_CAP);
    if(err_is_fail(err)) {
       return err;
    }

    // free argv
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    free(argv);
    return err;
}


/*
 * ------------------------------------------------------------------------------------------------
 * Listing of Processes
 * ------------------------------------------------------------------------------------------------
 */

// return the spawninfo associated with pid or NULL if no was found
static struct spawninfo *_proc_mgmt_get_si(struct proc_mgmt_state *pms, domainid_t pid)
{
    thread_mutex_lock_nested(&pms->mutex);
    struct proc_mgmt_element *proc = pms->procs;
    while (proc != NULL) {
        if (proc->si->pid == pid) {
            thread_mutex_unlock(&pms->mutex);

            return proc->si;
        }

        proc = proc->next;
    }

    thread_mutex_unlock(&pms->mutex);
    return NULL;
}

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
    if (ps == NULL || num == NULL)
        return ERR_INVALID_ARGS;

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    thread_mutex_lock_nested(&pms->mutex);
    struct proc_status *statuses = malloc(sizeof(struct proc_status) * pms->nb_processes_running);

    struct proc_mgmt_element *proc        = pms->procs;
    size_t                    proc_number = 0;
    while (proc != NULL) {
        if (_is_proc_not_killed(proc)) {
            spawn_info_to_proc_status(proc->si, &statuses[proc_number]);
            proc_number++;
        }

        proc = proc->next;
    }
    assert(proc_number == pms->nb_processes_running);

    *num = pms->nb_processes_running;
    *ps  = statuses;
    thread_mutex_unlock(&pms->mutex);

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
    if (pids == NULL || num == NULL)
        return ERR_INVALID_ARGS;

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    thread_mutex_lock_nested(&pms->mutex);
    domainid_t *process_ids = malloc(sizeof(domainid_t) * pms->nb_processes_running);

    struct proc_mgmt_element *proc        = pms->procs;
    size_t                    proc_number = 0;
    while (proc != NULL) {
        if (_is_proc_not_killed(proc)) {
            process_ids[proc_number] = proc->si->pid;
            proc_number++;
        }

        proc = proc->next;
    }

    assert(proc_number == pms->nb_processes_running);
    *num  = pms->nb_processes_running;
    *pids = process_ids;
    thread_mutex_unlock(&pms->mutex);

    return SYS_ERR_OK;
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
    if (name == NULL || pid == NULL)
        return ERR_INVALID_ARGS;

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    thread_mutex_lock_nested(&pms->mutex);
    struct proc_mgmt_element *proc = pms->procs;
    while (proc != NULL) {
        if (_proc_mgmt_name_match(proc->si->binary_name, name)) {
            *pid = proc->si->pid;

            thread_mutex_unlock(&pms->mutex);
            return SYS_ERR_OK;
        }

        proc = proc->next;
    }

    thread_mutex_unlock(&pms->mutex);
    return SPAWN_ERR_DOMAIN_NOTFOUND;
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
    if (pid == 0 || status == NULL)
        return ERR_INVALID_ARGS;

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    thread_mutex_lock_nested(&pms->mutex);
    struct spawninfo *si = _proc_mgmt_get_si(pms, pid);
    if (si == NULL)
        return SPAWN_ERR_DOMAIN_NOTFOUND;

    spawn_info_to_proc_status(si, status);
    return SYS_ERR_OK;
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
    if (pid == 0 || name == NULL || len == 0)
        return ERR_INVALID_ARGS;

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    struct spawninfo       *si  = _proc_mgmt_get_si(pms, pid);
    if (si == NULL)
        return SPAWN_ERR_DOMAIN_NOTFOUND;

    strncpy(name, si->binary_name, len - 1);
    return SYS_ERR_OK;
}

errval_t proc_mgmt_get_async(domainid_t pid, struct simple_async_channel** async){
    if(pid == 0)
        return ERR_INVALID_ARGS;

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    struct spawninfo       *si  = _proc_mgmt_get_si(pms, pid);
    if (si == NULL)
        return SPAWN_ERR_DOMAIN_NOTFOUND;

    *async = &si->async;
    return SYS_ERR_OK;
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
    //   - find the process with the given PID and suspend it
    if (pid == 0)
        return ERR_INVALID_ARGS;

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    struct spawninfo       *si  = _proc_mgmt_get_si(pms, pid);
    if (si == NULL)
        return SPAWN_ERR_DOMAIN_NOTFOUND;

    return spawn_suspend(si);
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
    //   - find the process with the given PID and resume its execution
    if (pid == 0)
        return ERR_INVALID_ARGS;

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    struct spawninfo       *si  = _proc_mgmt_get_si(pms, pid);
    if (si == NULL)
        return SPAWN_ERR_DOMAIN_NOTFOUND;

    return spawn_resume(si);
}


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
errval_t proc_mgmt_exit(int status)
{
    domainid_t pid = proc_mgmt_get_self_pid();

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    struct spawninfo       *si  = _proc_mgmt_get_si(pms, pid);
    if (si == NULL)
        return SPAWN_ERR_DOMAIN_NOTFOUND;

    return spawn_exit(si, status);
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
    // make compiler happy about unused parameters
    (void)pid;
    (void)status;

    USER_PANIC("should not be called by the process manager\n");
    return SYS_ERR_OK;
}


/**
 * @brief tells the process manager than the process with pid has terminated.
 *
 * @param[in] pid     process identifier of the process to wait for
 * @param[in] status  integer value with the given status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this means the process has exited gracefully
 */
errval_t proc_mgmt_register_wait(domainid_t pid, struct event_closure resume_fn, int *exit_code)
{
    struct proc_mgmt_state   *pms  = get_proc_mgmt_state();
    struct proc_mgmt_element *proc = pms->procs;

    thread_mutex_lock_nested(&pms->mutex);
    // get the proc with this id
    while (proc != NULL) {
        if (proc->si->pid == pid)
            break;

        proc = proc->next;
    }
    if (proc == NULL) {
        thread_mutex_unlock(&pms->mutex);
        resume_fn.handler(resume_fn.arg);
        return SPAWN_ERR_DOMAIN_NOTFOUND;
    }

    if (_is_proc_killed(proc)) {
        // process has already been killed, no need to wait
        *exit_code = proc->si->exitcode;

        thread_mutex_unlock(&pms->mutex);
        resume_fn.handler(resume_fn.arg);
        return SYS_ERR_OK;
    }

    // add the channel to the waiting list
    struct proc_mgmt_exit_waiting_proc *waiting = malloc(sizeof(struct proc_mgmt_exit_waiting_proc));
    waiting->resume_fn  = resume_fn;
    waiting->exit_code  = exit_code;
    waiting->next       = proc->waiting_procs;
    proc->waiting_procs = waiting;
    thread_mutex_unlock(&pms->mutex);

    return SYS_ERR_OK;
}

// Kill the process in *proc
static errval_t _proc_mgmt_kill(struct proc_mgmt_state *pms, struct proc_mgmt_element **proc)
{
    struct spawninfo *si  = (*proc)->si;
    errval_t          err = spawn_kill(si);
    if (err_is_fail(err)) {
        return err;
    }

    struct proc_mgmt_element *old_proc = *proc;

    // notify waiting threads
    struct proc_mgmt_exit_waiting_proc *waiting_proc = old_proc->waiting_procs;
    while (waiting_proc != NULL) {
        // set the exit code
        *waiting_proc->exit_code = old_proc->si->exitcode;
        waiting_proc->resume_fn.handler(waiting_proc->resume_fn.arg);
        if (err_is_fail(err))
            return err;

        struct proc_mgmt_exit_waiting_proc *to_delete = waiting_proc;
        waiting_proc                                  = waiting_proc->next;
        free(to_delete);
    }

    err = spawn_cleanup(si);
    pms->nb_processes_running--;

    // keep the process in the list
    // free(si);
    // free(old_proc);

    return err;
}

/**
 * @brief tells the process manager than the process with pid has terminated.
 *
 * @param[in] pid     process identifier of the process to wait for
 * @param[in] status  integer value with the given status
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this means the process has exited gracefully
 */
errval_t proc_mgmt_terminated(domainid_t pid, int status)
{
    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    thread_mutex_lock_nested(&pms->mutex);
    struct proc_mgmt_element **proc = &pms->procs;
    while (*proc != NULL) {
        if ((*proc)->si->pid == pid) {
            (*proc)->si->exitcode = status;
            errval_t err          = _proc_mgmt_kill(pms, proc);
            (*proc)->si->state    = SPAWN_STATE_TERMINATED;

            thread_mutex_unlock(&pms->mutex);
            return err;
        }

        proc = &(*proc)->next;
    }

    thread_mutex_unlock(&pms->mutex);
    return SPAWN_ERR_DOMAIN_NOTFOUND;
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
    //  - find the process in the process table and kill it
    //   - remove the process from the process table
    //   - clean up the state of the process
    //   - M4: notify its waiting processes

    struct proc_mgmt_state *pms = get_proc_mgmt_state();
    thread_mutex_lock_nested(&pms->mutex);
    struct proc_mgmt_element **proc = &pms->procs;
    while (*proc != NULL) {
        if ((*proc)->si->pid == pid) {
            errval_t err       = _proc_mgmt_kill(pms, proc);
            (*proc)->si->state = SPAWN_STATE_KILLED;

            thread_mutex_unlock(&pms->mutex);
            return err;
        }

        proc = &(*proc)->next;
    }

    thread_mutex_unlock(&pms->mutex);
    return SPAWN_ERR_DOMAIN_NOTFOUND;
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
    //  - find all the processs that match the given name
    //  - remove the process from the process table
    //  - clean up the state of the process
    //  - M4: notify its waiting processes

    struct proc_mgmt_state   *pms  = get_proc_mgmt_state();
    struct proc_mgmt_element *proc = pms->procs;

    const unsigned int MAX_PROCESSES = 4096;
    domainid_t        *pids          = (domainid_t *)calloc(MAX_PROCESSES, sizeof(domainid_t));
    size_t             idx           = 0;

    while (proc != NULL) {
        if (_is_proc_not_killed(proc) && _proc_mgmt_name_match(proc->si->binary_name, name)) {
            pids[idx++] = proc->si->pid;
        }
        proc = proc->next;
    }

    for (size_t i = 0; i < idx; i++) {
        errval_t err = proc_mgmt_kill(pids[i]);
        if (err_is_fail(err)) {
            return err;
        }
    }

    return SYS_ERR_OK;
}
