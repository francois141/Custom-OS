/**
 * \file
 * \brief RPC Bindings for AOS
 */

/*
 * Copyright (c) 2013-2016, ETH Zurich.
 * Copyright (c) 2022 The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef _LIB_BARRELFISH_AOS_MESSAGES_H
#define _LIB_BARRELFISH_AOS_MESSAGES_H

#include <aos/aos.h>
#include <aos/ump_chan.h>
#include <proc_mgmt/proc_mgmt.h>

#include <fs/fat32.h>


#include "aos_rpc_types.h"

/// defines the transport backend of the RPC channel
enum aos_rpc_transport {
    AOS_RPC_LMP,
    AOS_RPC_UMP,
};

/// type of the receive handler function.
/// depending on your RPC implementation, maybe you want to slightly adapt this
typedef void (*aos_rpc_handler_fn)(struct aos_rpc *ac, void *data);
typedef aos_rpc_handler_fn aos_recv_handler_fn;

struct handler_closure {
    aos_recv_handler_fn handler;
    void               *data;
};

#define MKHANDLER(_handler, _data) ((struct handler_closure) { .handler = _handler, .data = _data })
#define NOOP_HANDLER               MKHANDLER(NULL, NULL)

struct rpc_buf {
    void          *data;
    size_t         size;
    struct capref *caps;
    size_t         caps_size;
};

struct aos_rpc_handler_data {
    struct {
        void          *data;
        size_t         datasize;
        struct capref *caps;
        size_t         caps_size;
    } recv;
    struct {
        void          *data;
        size_t         bufsize;
        size_t        *datasize;
        struct capref *caps;
        size_t         caps_bufsize;
        size_t        *caps_size;
    } send;
    // can be aos_rpc or async_channel
    void             *chan;
    struct spawninfo *spawninfo;
    // call this function to send back data once it is ready
    struct event_closure resume_fn;
};

/**
 * @brief represents an RPC binding
 *
 * Note: the RPC binding should work over LMP (M4) or UMP (M6)
 */

struct aos_rpc {
    enum aos_rpc_transport transport;
    union {
        struct {
            struct lmp_chan channel;
        } lmp;
        struct {
            struct ump_chan channel;
        } ump;
    };
    struct waitset *waitset;

    size_t recv_size;
    size_t recv_offset;

    size_t recv_caps_size;
    size_t recv_caps_offset;

    size_t send_size;
    size_t send_offset;

    size_t send_caps_size;
    size_t send_caps_offset;

    struct rpc_buf recv_buf;
    struct rpc_buf send_buf;

    struct handler_closure recv_handler;
    struct handler_closure send_handler;

    bool late_init_done;
};

extern struct aos_rpc      rpc_to_init;
extern struct thread_mutex rpc_mutex;

/**
 * @brief Initialize an aos_rpc struct.
 *
 * @param[in] rpc  The aos_rpc struct to initialize.
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_lmp_connect(struct aos_rpc *rpc, struct capref remote);


errval_t aos_rpc_lmp_listen(struct aos_rpc *rpc_server, struct capref *retcap);
errval_t aos_rpc_lmp_accept(struct aos_rpc *rpc, struct handler_closure handler,
                            struct waitset *waitset);

errval_t aos_rpc_ump_connect(struct aos_rpc *rpc, struct capref frame, bool primary,
                             struct waitset *waitset);

void aos_rpc_destroy_server(struct aos_rpc *rpc_server);

errval_t aos_rpc_send_blocking_varsize(struct aos_rpc *rpc, const void *buf, size_t size,
                                       struct capref *capbuf, size_t capsize);
/**
 * @brief Send a message over an RPC channel.
 */
errval_t aos_rpc_send_blocking(struct aos_rpc *rpc, const void *buf, size_t size, struct capref cap);

errval_t aos_rpc_recv_blocking_varsize(struct aos_rpc *rpc, void **buf, size_t *size,
                                       struct capref **capbuf, size_t *capsize);
/**
 * @brief Receive a message over an RPC channel.
 *
 * @param[in]  rpc       the RPC channel to use
 * @param[in]  buf       buffer to receive the message into
 * @param[in]  bufsize   size of the buffer
 * @param[out] datasize  returns the size of the received message
 * @param[out] retcap    returns the received capability, or NULL_CAP if none
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_recv_blocking(struct aos_rpc *rpc, void *buf, size_t bufsize, size_t *datasize,
                               struct capref *retcap);

errval_t aos_rpc_send(struct aos_rpc *rpc);
errval_t aos_rpc_recv(struct aos_rpc *rpc);
errval_t aos_rpc_send_with_handler(struct aos_rpc *rpc, struct handler_closure handler);
errval_t aos_rpc_recv_with_handler(struct aos_rpc *rpc, struct handler_closure handler);
/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Init Channel
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Send a single number over an RPC channel.
 *
 * @param[in] chan  the RPC channel to use  (init channel)
 * @param[in] val   the number to send
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: init
 */
errval_t aos_rpc_send_number(struct aos_rpc *chan, uintptr_t val);


/**
 * @brief Send a single number over an RPC channel.
 *
 * @param[in] chan  the RPC channel to use (init channel)
 * @param[in] val   the string to send
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: init
 */
errval_t aos_rpc_send_string(struct aos_rpc *chan, const char *string);


/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Memory Channel
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Request a RAM capability with >= bytes of size
 *
 * @param[in]  chan       the RPC channel to use (memory channel)
 * @param[in]  bytes      minimum number of bytes to request
 * @param[in]  alignment  minimum alignment of the requested RAM capability
 * @param[out] retcap     received capability
 * @param[out] ret_bytes  size of the received capability in bytes
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: memory
 */
errval_t aos_rpc_get_ram_cap(struct aos_rpc *chan, size_t bytes, size_t alignment,
                             struct capref *retcap, size_t *ret_bytes);


/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Serial Channel
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief obtains a single character from the serial
 *
 * @param chan  the RPC channel to use (serial channel)
 * @param retc  returns the read character
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_serial_getchar(struct aos_rpc *chan, char *retc);

/// XXX aos_rpc_serial_getchar but it supports reading an entire string at once
/// we are still bound by the reading buffers of 1024 bytes.
errval_t aos_rpc_serial_getstr(struct aos_rpc *rpc, char *buf, size_t buflen, size_t *retlen);

/**
 * @brief sends a single character to the serial
 *
 * @param chan  the RPC channel to use (serial channel)
 * @param c     the character to send
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_serial_putchar(struct aos_rpc *chan, char c);

/// XXX aos_rpc_serial_putchar but it supports sending an entire string at once
/// we are still bound by the reading buffers of 1024 bytes.
errval_t aos_rpc_serial_putstr(struct aos_rpc *chan, const char *str, size_t len);


/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Process Management
 * ------------------------------------------------------------------------------------------------
 */

errval_t aos_rpc_proc_spawn_mapped(struct aos_rpc *chan, int argc, const char *argv[], int capc,
                                   struct capref capv[], coreid_t core, domainid_t *newpid,
                                   struct capref stdin_frame, struct capref stdout_frame);

/**
 * @brief requests a new process to be spawned with the supplied arguments and caps
 *
 * @param[in]  chan    the RPC channel to use (process channel)
 * @param[in]  argc    number of arguments in argv
 * @param[in]  argv    array of strings of the arguments to be passed to the new process
 * @param[in]  capc    the number of capabilities that are being sent
 * @param[in]  cap     capabilities to give to the new process, or NULL_CAP if none
 * @param[in]  core    core on which to spawn the new process on
 * @param[out] newpid  returns the PID of the spawned process
 *
 * @return SYS_ERR_OK on success, or error value on failure
 *
 * Hint: we should be able to send multiple capabilities, but we can only send one.
 *       Think how you could send multiple cappabilities by just sending one.
 */
errval_t aos_rpc_proc_spawn_with_caps(struct aos_rpc *chan, int argc, const char *argv[], int capc,
                                      struct capref capv[], coreid_t core, domainid_t *newpid);


/**
 * @brief requests a new process to be spawned with the supplied commandline
 *
 * @param[in]  chan    the RPC channel to use (process channel)
 * @param[in]  cmdline  command line of the new process, including its args
 * @param[in]  core     core on which to spawn the new process on
 * @param[out] newpid   returns the PID of the spawned process
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_spawn_with_cmdline(struct aos_rpc *chan, const char *cmdline, coreid_t core,
                                         domainid_t *newpid);


/**
 * @brief requests a new process to be spawned with the default arguments
 *
 * @param[in]  chan     the RPC channel to use (process channel)
 * @param[in]  path     name of the binary to be spawned
 * @param[in]  core     core on which to spawn the new process on
 * @param[out] newpid   returns the PID of the spawned process
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_spawn_with_default_args(struct aos_rpc *chan, const char *path, coreid_t core,
                                              domainid_t *newpid);


/**
 * @brief obtains a list of PIDs of all processes in the system
 *
 * @param[in]  chan       the RPC channel to use (process channel)
 * @param[out] pids       array of PIDs of all processes in the system (freed by caller)
 * @param[out] pid_count  the number of PIDs in the list
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_all_pids(struct aos_rpc *chan, domainid_t **pids, size_t *pid_count);

/**
 * @brief obtains the status of a process
 *
 * @param[in]  chan         the RPC channel to use (process channel)
 * @param[in]  pid          PID of the process to get the status of
 * @param[out] core         core on which the process is running
 * @param[out] cmdline      buffer to store the cmdline in
 * @param[out] cmdline_max  size of the cmdline buffer in bytes
 * @param[out] state        returns the state of the process
 * @param[out] exit_code    returns the exit code of the process (if terminated)
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_status(struct aos_rpc *chan, domainid_t pid, coreid_t *core,
                                 char *cmdline, int cmdline_max, uint8_t *state, int *exit_code);


/**
 * @brief obtains the name of a process with a given PID
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] name  the name of the process to search for
 * @param[in] pid   returns PID of the process to pause/suspend
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_name(struct aos_rpc *chan, domainid_t pid, char *name, size_t len);


/**
 * @brief obtains the PID of a process with a given name
 *
 * @param[in]  chan  the RPC channel to use (process channel)
 * @param[in]  name  the name of the process to search for
 * @param[out] pid   returns PID of the process with the given name
 *
 * @return SYS_ERR_OK on success, or error value on failure
 *
 * Note: if there are multiple processes with the same name, the smallest PID should be
 * returned.
 */
errval_t aos_rpc_proc_get_pid(struct aos_rpc *chan, const char *name, domainid_t *pid);


/**
 * @brief pauses or suspends the execution of a running process
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to pause/suspend
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_pause(struct aos_rpc *chan, domainid_t pid);


/**
 * @brief resumes a previously paused process
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to resume
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_resume(struct aos_rpc *chan, domainid_t pid);


/**
 * @brief exists the current process with the supplied exit code
 *
 * @param[in] chan    the RPC channel to use (process channel)
 * @param[in] status  exit status code to send to the process manager.
 *
 * @return SYS_ERR_OK on success, or error value on failure
 *
 * Note: this function does not return, the process manager will halt the process execution.
 */
errval_t aos_rpc_proc_exit(struct aos_rpc *chan, int status);


/**
 * @brief waits for the process with the given PID to exit
 *
 * @param[in]  chan     the RPC channel to use (process channel)
 * @param[in]  pid      PID of the process to wait for
 * @param[out] status   returns the exit status of the process
 *
 * @return SYS_ERR_OK on success, or error value on failure
 *
 * Note: the RPC will only return after the process has exited
 */
errval_t aos_rpc_proc_wait(struct aos_rpc *chan, domainid_t pid, int *status);


/**
 * @brief requests that the process with the given PID is terminated
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to be terminated
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_kill(struct aos_rpc *chan, domainid_t pid);


/**
 * @brief requests that all processes that match the supplied name are terminated
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] name  name of the processes to be terminated
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_kill_all(struct aos_rpc *chan, const char *name);


errval_t aos_rpc_filesystem_open(struct aos_rpc *chan, char *path, struct fat32_handle **fat32_handle_addr);

errval_t aos_rpc_filesystem_read(struct aos_rpc *chan, struct fat32_handle* fat32_handle_addr, void *buf, size_t len, size_t *byte_read);

errval_t aos_rpc_filesystem_write(struct aos_rpc *chan, struct fat32_handle* fat32_handle_addr, void *buf, size_t len, size_t *bytes_written);

errval_t aos_rpc_filesystem_seek(struct aos_rpc *chan, struct fat32_handle* fat32_handle_addr, off_t offset, int whence);

errval_t aos_rpc_filesystem_tell(struct aos_rpc *chan, struct fat32_handle* fat32_handle_addr, size_t *position);

errval_t aos_rpc_filesystem_close(struct aos_rpc *chan, struct fat32_handle *fat32_handle_addr);

errval_t aos_rpc_filesystem_dir_open(struct aos_rpc *chan, const char *path, struct fat32_handle **fat32_handle_addr);

errval_t aos_rpc_filesystem_dir_next(struct aos_rpc *chan, struct fat32_handle *fat32_handle_addr, char **new_name);

errval_t aos_rpc_filesystem_dir_close(struct aos_rpc *chan, struct fat32_handle *fat32_handle_addr);

errval_t aos_rpc_filesystem_mkdir(struct aos_rpc *chan, const char *path);

errval_t aos_rpc_filesystem_rmdir(struct aos_rpc *chan, const char *path);

errval_t aos_rpc_filesystem_mkfile(struct aos_rpc *chan, const char *path, struct fat32_handle **fat32_handle_addr);

errval_t aos_rpc_filesystem_rmfile(struct aos_rpc *chan, const char *path);

errval_t aos_rpc_filesystem_is_directory(struct aos_rpc *chan, const char *path, bool *is_directory);

errval_t aos_rpc_filesystem_stat(struct aos_rpc *chan, struct fat32_handle *fat32_handle_addr, struct fs_fileinfo *file_info);

errval_t aos_rpc_test_suite_run(struct aos_rpc *rpc, struct test_suite_config config);

errval_t aos_rpc_cap_delete_remote(struct aos_rpc *rpc, struct capref root, capaddr_t src,
                                   uint8_t level);
errval_t aos_rpc_cap_revoke_remote(struct aos_rpc *rpc, struct capref root, capaddr_t src,
                                   uint8_t level);
errval_t aos_rpc_cap_retype_remote(struct aos_rpc *rpc, struct capref src_root,
                                   struct capref dest_root, capaddr_t src, gensize_t offset,
                                   enum objtype new_type, gensize_t objsize, size_t count,
                                   capaddr_t to, capaddr_t slot, int to_level);

/*
 * ------------------------------------------------------------------------------------------------
 * AOS RPC: Network
 * ------------------------------------------------------------------------------------------------
 */

/**
 * \brief Ping the target ip
 */
errval_t aos_rpc_network_ping(struct aos_rpc* rpc, uint32_t target_ip, uint32_t* ping_ms);

/**
 * \brief Tell the network handler that the current process starts listening at the given port
 */
errval_t aos_rpc_network_start_listening(struct aos_rpc* rpc, uint16_t port, bool is_tcp);

/**
 * \brief Send a packet in the network
 */
errval_t aos_rpc_network_send(struct aos_rpc* rpc, uint32_t ip, uint16_t port, bool is_tcp, uint16_t src_port, uint16_t data_size, void* data);

/**
 * \brief Set the io method
*/
errval_t aos_rpc_network_set_io(struct aos_rpc* rpc, bool is_network, bool is_tcp, uint32_t ip, uint16_t dest_port, uint16_t src_port);

/**
 * \brief Returns the RPC channel to init.
 */
struct aos_rpc *aos_rpc_get_init_channel(void);

/**
 * \brief Returns the channel to the memory server
 */
struct aos_rpc *aos_rpc_get_memory_channel(void);

/**
 * \brief Returns the channel to the process manager
 */
struct aos_rpc *aos_rpc_get_process_channel(void);

/**
 * \brief Returns the channel to the serial console
 */
struct aos_rpc *aos_rpc_get_serial_channel(void);

/**
 * \brief Returns the channel to the filesystem
 */
struct aos_rpc *aos_rpc_get_filesystem_channel(void);

struct simple_async_channel;
struct simple_response;
typedef void (*simple_async_response_handler)(struct simple_async_channel* chan, void* data, size_t size, struct simple_response* res);

/**
 * @brief Setup a new async channel between a process (not init) and the init process of its core
 *  This function must be called only once per process
 */
errval_t simple_async_proc_setup(simple_async_response_handler response_handler);

struct simple_async_channel* aos_rpc_get_async_channel(void);


size_t aos_rpc_checksum(void *buf, size_t size);

#endif  // _LIB_BARRELFISH_AOS_MESSAGES_H
