#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <spawn/spawn.h>
#include <grading/grading.h>

#include "aos/aos_rpc_types.h"
#include "aos/debug.h"
#include "async_channel.h"
#include "cap_transfer.h"
#include "distops/invocations.h"
#include "rpc_handler.h"
#include "tests.h"
#include "proc_mgmt.h"
#include "distcap_handler.h"
#include "network_handler.h"

#include "../shell/serial/serial.h"

// XXX memory limit: currently 1GiB
#define RRC_MEMORY_LIMIT (1024 * 1024 * 1024)

#define HANDLE_RPC_REQUEST_SZ(type, size)                                                           \
    do {                                                                                            \
        if (data.recv.datasize != size) {                                                           \
            _send_err_rpc_response(res, SYS_ERR_INVALID_SIZE);                                      \
            return true;                                                                            \
        }                                                                                           \
        *data.send.datasize   = sizeof(struct aos_##type##_rpc_response);                           \
        bool send_immediately = true;                                                               \
        res->err              = SYS_ERR_OK;                                                         \
        res->err = _handle_##type##_rpc_request(&data, (struct aos_##type##_rpc_request *)req,      \
                                                (struct aos_##type##_rpc_response *)res,            \
                                                data.send.caps, data.spawninfo, &send_immediately); \
        *data.send.caps_size = capref_is_null(data.send.caps[0]) ? 0 : 1;                           \
        return send_immediately;                                                                    \
    } while (0)

#define HANDLE_RPC_REQUEST(type)                                                                   \
    HANDLE_RPC_REQUEST_SZ(type, (sizeof(struct aos_##type##_rpc_request)))

static void _send_err_rpc_response(struct aos_generic_rpc_response *res, errval_t err)
{
    res->type = AOS_RPC_RESPONSE_TYPE_NONE;
    res->err  = err;
}

static struct async_channel *cross_core_channel;

void set_cross_core_channel(struct async_channel *async)
{
    cross_core_channel = async;
}

struct async_channel *get_cross_core_channel(void) { return cross_core_channel; }

// called when getting a response after transmitting a rpc request from one core to the other
static void _handle_rpc_transmit_response(struct request *req, void *data, size_t size,
                                          struct capref *capv, size_t capc)
{
    struct aos_rpc_handler_data *handler = req->meta;
    assert(size <= handler->send.bufsize);
    assert(capc <= handler->send.caps_bufsize);

    memcpy(handler->send.data, data, size);
    memcpy(handler->send.caps, capv, capc * sizeof(struct capref));
    *handler->send.datasize = size;
    *handler->send.caps_size = capc;
    handler->resume_fn.handler(handler->resume_fn.arg);
    free(handler);
}

static void _rpc_transmit_with_handler(struct aos_rpc_handler_data *data, async_callback callback)
{
    // copy it as the handler data is on the stack
    struct aos_rpc_handler_data *handler = malloc(sizeof(struct aos_rpc_handler_data));
    memcpy(handler, data, sizeof(struct aos_rpc_handler_data));
    async_request(get_cross_core_channel(), data->recv.data, data->recv.datasize, data->recv.caps,
                  data->recv.caps_size, callback, handler);
}

// call to transmit the request to the other core
// don't forget to return false right after
static void _rpc_transmit(struct aos_rpc_handler_data *data)
{
    return _rpc_transmit_with_handler(data, _handle_rpc_transmit_response);
}

static errval_t _handle_memserver_rpc_request(struct aos_rpc_handler_data       *data,
                                              struct aos_memserver_rpc_request  *req,
                                              struct aos_memserver_rpc_response *res,
                                              struct capref *cap, struct spawninfo *spawninfo,
                                              bool *send_immediately)
{
    if (disp_get_core_id() != 0) {
        // forward to core 0
        _rpc_transmit(data);
        *send_immediately = false;
        return SYS_ERR_OK;
    }
    (void)data;
    (void)send_immediately;
    grading_rpc_handler_ram_cap(req->size, req->alignment);
    errval_t err   = SYS_ERR_OK;
    res->base.type = AOS_RPC_RESPONSE_TYPE_MEMSERVER;
    if (req->alignment != BASE_PAGE_SIZE) {
        // TODO(jlscheerer) support different alignments.
        return MM_ERR_BAD_ALIGNMENT;
    }

    // check to see if we are still allowed to allocate
    if (spawninfo && spawninfo->mem > RRC_MEMORY_LIMIT - req->size) {
        debug_printf("denying memory request due to limits\n");
        return MM_ERR_OUT_OF_MEMORY;
    }

    debug_printf("size: %zu | alignment: %zu\n", req->size, req->alignment);
    err = ram_alloc(cap, req->size);
    if (err_is_fail(err)) {
        return err;
    }
    res->retbytes = req->size;
    debug_printf("retbytes: %zu\n", res->retbytes);

    if (spawninfo) {
        spawninfo->mem += req->size;
    }
    return SYS_ERR_OK;
}

static errval_t _handle_terminal_rpc_request(struct aos_rpc_handler_data      *data,
                                             struct aos_terminal_rpc_request  *req,
                                             struct aos_terminal_rpc_response *res,
                                             struct capref *cap, struct spawninfo *spawninfo,
                                             bool *send_immediately)
{
    /// XXX we could potentially also rate limit based on spawninfo (ignoring for now)
    (void)spawninfo;
    errval_t err = SYS_ERR_OK;
    if (cap)
        *cap = NULL_CAP;
    res->base.type = AOS_RPC_RESPONSE_TYPE_TERMINAL;
    if (req->ttype == AOS_TERMINAL_RPC_REQUEST_TYPE_PUTCHAR) {
        res->ttype = AOS_TERMINAL_RPC_RESPONSE_TYPE_PUTCHAR;
        if(network_is_using_network_io() && disp_get_core_id() == 0){
            grading_rpc_handler_serial_putchar(req->u.putchar.c);
            err = network_io_putchar(req->u.putchar.c);
            if (err_is_fail(err)) {
                return err;
            }
        } else if (is_usr_serial_enabled() && disp_get_core_id() == 0) {
            grading_rpc_handler_serial_putchar(req->u.putchar.c);
            err = serial_putchar(req->u.putchar.c);
            if (err_is_fail(err)) {
                return err;
            }
        } else if (is_usr_serial_enabled()) {
            _rpc_transmit(data);
            *send_immediately = false;
            return SYS_ERR_OK;
        } else {
            grading_rpc_handler_serial_putchar(req->u.putchar.c);
            // XXX fallback to the syscall
            sys_print(&req->u.putchar.c, 1);
        }
    } else if (req->ttype == AOS_TERMINAL_RPC_REQUEST_TYPE_GETCHAR) {
        res->ttype = AOS_TERMINAL_RPC_RESPONSE_TYPE_GETCHAR;
        if(network_is_using_network_io() && disp_get_core_id() == 0){
            grading_rpc_handler_serial_getchar();
            res->base.err  = SYS_ERR_OK;
            res->base.type = AOS_RPC_RESPONSE_TYPE_TERMINAL;
            res->ttype     = AOS_TERMINAL_RPC_RESPONSE_TYPE_GETCHAR;
            err = network_io_getchar_register_wait(/*num_characters*/ 1, data->resume_fn, NULL,
                                               &res->u.getchar.c);
            if (err_is_fail(err)) {
                return err;
            }
            // don't send immediately wait for completion
            *send_immediately = false;
        } else if (is_usr_serial_enabled() && disp_get_core_id() == 0) {
            grading_rpc_handler_serial_getchar();
            res->base.err  = SYS_ERR_OK;
            res->base.type = AOS_RPC_RESPONSE_TYPE_TERMINAL;
            res->ttype     = AOS_TERMINAL_RPC_RESPONSE_TYPE_GETCHAR;
            err = serial_getchar_register_wait(/*num_characters*/ 1, data->resume_fn, NULL,
                                               &res->u.getchar.c);
            if (err_is_fail(err)) {
                return err;
            }
            // don't send immediately wait for completion
            *send_immediately = false;
        } else if (is_usr_serial_enabled()) {
            res->base.type = AOS_RPC_RESPONSE_TYPE_TERMINAL;
            res->ttype     = AOS_TERMINAL_RPC_RESPONSE_TYPE_GETCHAR;
            _rpc_transmit(data);
            *send_immediately = false;
        } else {
            grading_rpc_handler_serial_getchar();
            // XXX fallback to the syscall
            sys_getchar(&res->u.getchar.c);
        }
    } else {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return SYS_ERR_OK;
}

static errval_t _handle_terminal_str_rpc_request(struct aos_rpc_handler_data          *data,
                                                 struct aos_terminal_str_rpc_request  *req,
                                                 struct aos_terminal_str_rpc_response *res,
                                                 struct capref *cap, struct spawninfo *spawninfo,
                                                 bool *send_immediately)
{
    (void)data;
    (void)cap;
    (void)spawninfo;
    errval_t err = SYS_ERR_OK;
    if (req->ttype == AOS_TERMINAL_STR_RPC_REQUEST_TYPE_PUTSTR) {
        char *buf = ((char *)req) + sizeof(struct aos_terminal_str_rpc_request);
        // XXX should we call the grading function this way?
        res->base.type = AOS_RPC_RESPONSE_TYPE_TERMINAL_STR;
        if(network_is_using_network_io() && disp_get_core_id() == 0){
            for (size_t i = 0; i < req->size; ++i) {
                grading_rpc_handler_serial_putchar(buf[i]);
            }
            size_t retbytes = 0;
            err             = network_io_putstring(buf, req->size, &retbytes);
            if (err_is_fail(err)) {
                return err;
            }
            res->size = retbytes;
        } else if (is_usr_serial_enabled() && disp_get_core_id() == 0) {
            for (size_t i = 0; i < req->size; ++i) {
                grading_rpc_handler_serial_putchar(buf[i]);
            }
            size_t retbytes = 0;
            err             = serial_putstr(buf, req->size, &retbytes);
            if (err_is_fail(err)) {
                return err;
            }
            res->size = retbytes;
        } else if (is_usr_serial_enabled()) {
            _rpc_transmit(data);
            *send_immediately = false;
            return SYS_ERR_OK;
        } else {
            for (size_t i = 0; i < req->size; ++i) {
                grading_rpc_handler_serial_putchar(buf[i]);
            }
            // XXX fallback to the syscall
            sys_print(buf, req->size);
        }
        return SYS_ERR_OK;
    } else if (req->ttype == AOS_TERMINAL_STR_RPC_REQUEST_TYPE_GETSTR) {
        if(network_is_using_network_io() && disp_get_core_id() == 0){
            /// XXX note quite obvious how many times we should be calling the grading library here...
            grading_rpc_handler_serial_getchar();
            res->base.err  = SYS_ERR_OK;
            res->base.type = AOS_RPC_RESPONSE_TYPE_TERMINAL_STR;
            res->ttype     = AOS_TERMINAL_STR_RPC_RESPONSE_TYPE_GETSTR;
            err = network_io_getchar_register_wait(/*num_characters*/ req->size, data->resume_fn,
                                               &res->size, (char *)&res->buf);
            *data->send.datasize = sizeof(struct aos_terminal_str_rpc_response) + res->size;
            if (err_is_fail(err)) {
                return err;
            }
            // don't send immediately wait for completion
            *send_immediately = false;
        } else if (is_usr_serial_enabled() && disp_get_core_id() == 0) {
            /// XXX note quite obvious how many times we should be calling the grading library here...
            grading_rpc_handler_serial_getchar();
            res->base.err  = SYS_ERR_OK;
            res->base.type = AOS_RPC_RESPONSE_TYPE_TERMINAL_STR;
            res->ttype     = AOS_TERMINAL_STR_RPC_RESPONSE_TYPE_GETSTR;
            err = serial_getchar_register_wait(/*num_characters*/ req->size, data->resume_fn,
                                               &res->size, (char *)&res->buf);
            *data->send.datasize = sizeof(struct aos_terminal_str_rpc_response) + res->size;
            if (err_is_fail(err)) {
                return err;
            }
            // don't send immediately wait for completion
            *send_immediately = false;
        } else if (is_usr_serial_enabled()) {
            _rpc_transmit(data);
            *send_immediately = false;
            return SYS_ERR_OK;
        } else {
            // XXX fallback to the syscall
            // NOTE: not quite obvious how to call the grading library in this case, so we omit calling it...
            assert(false);
        }
        return SYS_ERR_OK;
    }
    return SYS_ERR_ILLEGAL_INVOCATION;
}

static errval_t _handle_generic_number_rpc_request(struct aos_rpc_handler_data            *data,
                                                   struct aos_generic_number_rpc_request  *req,
                                                   struct aos_generic_number_rpc_response *res,
                                                   struct capref *cap, struct spawninfo *spawninfo,
                                                   bool *send_immediately)
{
    (void)data;
    (void)send_immediately;
    (void)cap;
    (void)spawninfo;
    grading_rpc_handle_number(req->val);
    debug_printf("%zu was sent via generic_number_rpc_request\n", req->val);
    res->base.type = AOS_RPC_RESPONSE_TYPE_GENERIC_NUMBER;
    return SYS_ERR_OK;
}

static errval_t _handle_generic_string_rpc_request(struct aos_rpc_handler_data            *data,
                                                   struct aos_generic_string_rpc_request  *req,
                                                   struct aos_generic_string_rpc_response *res,
                                                   struct capref *cap, struct spawninfo *spawninfo,
                                                   bool *send_immediately)
{
    (void)data;
    (void)send_immediately;
    (void)cap;
    (void)spawninfo;
    char *buf = ((char *)req) + sizeof(struct aos_generic_string_rpc_request);
    grading_rpc_handler_string(buf);
    debug_printf("\"%s\" was sent via generic_string_rpc_request\n", buf);
    res->base.type = AOS_RPC_RESPONSE_TYPE_GENERIC_STRING;
    return SYS_ERR_OK;
}

static void _handle_rpc_all_pids_response(struct request *req, void *data, size_t size,
                                          struct capref *capv, size_t capc)
{
    (void)capv, (void)capc;
    struct aos_rpc_handler_data *handler = req->meta;
    assert(size <= handler->send.bufsize);

    struct aos_proc_mgmt_rpc_all_pid_response *all_res   = handler->send.data;
    struct aos_proc_mgmt_rpc_all_pid_response *core_res  = data;
    size_t                                     copy_size = core_res->num * sizeof(domainid_t);
    size_t available_size = handler->send.bufsize - *handler->send.datasize;
    if (copy_size > available_size) {
        // we don't have enough memory to send back the whole response
        all_res->base.err = LIB_ERR_RPC_BUF_OVERFLOW;
        copy_size         = 0;
    } else {
        // add the pids at the end
        memcpy(all_res->pids + all_res->num, core_res->pids, copy_size);
    }
    *handler->send.datasize += copy_size;
    all_res->num += core_res->num;

    handler->resume_fn.handler(handler->resume_fn.arg);
    free(handler);
}

static bool _handle_proc_mgmt_rpc_request(struct aos_rpc_handler_data *data)
{
    // errval_t err;

    struct aos_proc_mgmt_rpc_basic_request *req = data->recv.data;
    struct aos_proc_mgmt_rpc_response      *res = data->send.data;

    if (req->base.core != (coreid_t)-1 && req->base.core != disp_get_core_id()) {
        _rpc_transmit(data);
        return false;
    }

    // set default return size and capref
    *data->send.datasize = sizeof(struct aos_proc_mgmt_rpc_response);
    switch (req->base.proc_type) {
    case AOS_RPC_PROC_MGMT_REQUEST_SPAWN_CMDLINE: {
        const struct aos_proc_mgmt_rpc_spawn_request *proc_req
            = (const struct aos_proc_mgmt_rpc_spawn_request *)req;
        grading_rpc_handler_process_spawn((char*)proc_req->cmdline, proc_req->base.core);
        int    argc;
        char **argv = spawn_parse_args(proc_req->cmdline, &argc);

        assert((int)data->recv.caps_size == proc_req->capc + 2);
        struct capref stdin_frame = data->recv.caps[proc_req->capc + 0];
        struct capref stdout_frame = data->recv.caps[proc_req->capc + 1];
        res->base.err = proc_mgmt_spawn_mapped(argc, (const char **)argv, proc_req->capc,
                                               data->recv.caps, proc_req->base.core, &res->pid,
                                               stdin_frame, stdout_frame);
        // free argv
        for (int i = 0; i < argc; i++)
            free(argv[i]);
        free(argv);
        break;
    }
    case AOS_RPC_PROC_MGMT_REQUEST_SPAWN_DEFAULT: {
        const struct aos_proc_mgmt_rpc_spawn_request *proc_req
            = (const struct aos_proc_mgmt_rpc_spawn_request *)req;
        grading_rpc_handler_process_spawn((char*)proc_req->cmdline, proc_req->base.core);
        res->base.err = proc_mgmt_spawn_program(proc_req->cmdline, proc_req->base.core, &res->pid);
        break;
    }
    case AOS_RPC_PROC_MGMT_REQUEST_ALL_PIDS: {
        struct aos_proc_mgmt_rpc_all_pid_response *all_res
            = (struct aos_proc_mgmt_rpc_all_pid_response *)res;
        domainid_t *pids;
        res->base.err = proc_mgmt_get_proc_list(&pids, &all_res->num);
        if (err_is_ok(res->base.err)) {
            // check that we do not override the internal buffer
            size_t copy_size = all_res->num * sizeof(domainid_t);

            assert(data->send.bufsize > sizeof(struct aos_proc_mgmt_rpc_all_pid_response));
            size_t available_size = data->send.bufsize
                                    - sizeof(struct aos_proc_mgmt_rpc_all_pid_response);
            if (copy_size > available_size) {
                // we don't have enough memory to send back the whole response
                res->base.err = LIB_ERR_RPC_BUF_OVERFLOW;
                copy_size     = 0;
            } else {
                memcpy(all_res->pids, pids, copy_size);
            }
            *data->send.datasize = sizeof(struct aos_proc_mgmt_rpc_all_pid_response) + copy_size;
            free(pids);

            if (req->base.core == (coreid_t)-1) {
                // request the pids of the other core
                // note: this will only work if we have 2 cores with id 0 and 1
                req->base.core = 1 - disp_get_core_id();
                // transmit the request to the other core
                _rpc_transmit_with_handler(data, _handle_rpc_all_pids_response);
                return false;
            }
        }
        break;
    }
    case AOS_RPC_PROC_MGMT_REQUEST_STATUS: {
        struct aos_proc_mgmt_rpc_status_response *res_status
            = (struct aos_proc_mgmt_rpc_status_response *)res;
        res_status->base.err = proc_mgmt_get_status(req->pid, &res_status->status);
        *data->send.datasize = sizeof(struct aos_proc_mgmt_rpc_status_response);
        break;
    }
    case AOS_RPC_PROC_MGMT_REQUEST_NAME: {
        assert(data->send.bufsize > sizeof(struct aos_proc_mgmt_rpc_response));
        size_t len_available = data->send.bufsize - sizeof(struct aos_proc_mgmt_rpc_response);
        res->base.err        = proc_mgmt_get_name(req->pid, res->name, len_available);
        if (err_is_ok(res->base.err))
            *data->send.datasize += strlen(res->name) + 1;
        break;
    }
    case AOS_RPC_PROC_MGMT_REQUEST_PID:
        res->base.err = proc_mgmt_get_pid_by_name(req->name, &res->pid);
        if (err_is_fail(res->base.err) && req->base.core == (coreid_t)-1) {
            // try to look for the pid on the other core
            // note: this will only work if we have 2 cores with id 0 and 1
            req->base.core = 1 - disp_get_core_id();
            _rpc_transmit(data);
            return false;
        }
        break;

    case AOS_RPC_PROC_MGMT_REQUEST_PAUSE:
        grading_rpc_handler_process_pause(req->pid);
        res->base.err = proc_mgmt_suspend(req->pid);
        break;

    case AOS_RPC_PROC_MGMT_REQUEST_RESUME:
        grading_rpc_handler_process_resume(req->pid);
        res->base.err = proc_mgmt_resume(req->pid);
        break;

    case AOS_RPC_PROC_MGMT_REQUEST_EXIT: {
        struct aos_proc_mgmt_rpc_exit_request *exit_req
            = (struct aos_proc_mgmt_rpc_exit_request *)req;
        grading_rpc_handler_process_exit(exit_req->pid, exit_req->exit_code);
        res->base.err = proc_mgmt_terminated(exit_req->pid, exit_req->exit_code);
        return false;
        break;
    }

    case AOS_RPC_PROC_MGMT_REQUEST_WAIT: {
        struct aos_proc_mgmt_rpc_wait_response *wait_res
            = (struct aos_proc_mgmt_rpc_wait_response *)res;
        grading_rpc_handler_process_wait(req->pid);
        res->base.err = proc_mgmt_register_wait(req->pid, data->resume_fn, &wait_res->exit_code);
        // don't send the message back immediatly, we need to wait
        return false;
    }

    case AOS_RPC_PROC_MGMT_REQUEST_KILL:
        grading_rpc_handler_process_kill(req->pid);
        res->base.err = proc_mgmt_kill(req->pid);
        break;

    case AOS_RPC_PROC_MGMT_REQUEST_KILLALL:
        grading_rpc_handler_process_killall(req->name);
        res->base.err = proc_mgmt_killall(req->name);
        if (err_is_ok(res->base.err) && req->base.core == (coreid_t)-1) {
            // try to look for the name on the other core
            // note: this will only work if we have 2 cores with id 0 and 1
            req->base.core = 1 - disp_get_core_id();
            _rpc_transmit(data);
            return false;
        }
        break;
    }

    return true;
}

static errval_t _handle_test_suite_rpc_request(struct aos_rpc_handler_data        *data,
                                               struct aos_test_suite_rpc_request  *req,
                                               struct aos_test_suite_rpc_response *res,
                                               struct capref *cap, struct spawninfo *spawninfo,
                                               bool *send_immediately)
{
    (void)data;
    (void)send_immediately;
    (void)cap;
    (void)spawninfo;
    res->base.type = AOS_RPC_RESPONSE_TYPE_TEST_SUITE;
    return test_suite_run(req->config);
}

static bool _handle_filesystem_rpc_request(struct aos_rpc_handler_data *data) {
    const struct aos_filesystem_request *req = data->recv.data;
    struct aos_filesystem_response *res = data->send.data;

    if (disp_get_core_id() != 0) {
        // forward to core 0
        _rpc_transmit(data);
        return false;
    }

    if (req->request_type == AOS_RPC_FILESYSTEM_OPEN) {
        // Request
        const struct aos_filesystem_rpc_open_request *open_request = data->recv.data;
        // Response
        struct aos_filesystem_rpc_open_response *response = (struct aos_filesystem_rpc_open_response *) res;
        // Operation
        res->base.err = fat32_open(get_mounted_filesystem(), open_request->path, &response->fat32_handle_addr);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_open_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_READ) {
        // Request
        const struct aos_filesystem_rpc_read_request *read_request = data->recv.data;
        // Response
        struct aos_filesystem_rpc_read_response *response = (struct aos_filesystem_rpc_read_response *) res;
        response->fat32_handle_addr = read_request->fat32_handle_addr;
        // Operation
        res->base.err = fat32_read(get_mounted_filesystem(), read_request->fat32_handle_addr, &response->buffer,
                                   read_request->len, &response->len);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_read_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_WRITE) {
        // Request
        const struct aos_filesystem_rpc_write_request *write_request = data->recv.data;
        // Response
        struct aos_filesystem_rpc_write_response *response = (struct aos_filesystem_rpc_write_response *) res;
        response->fat32_handle_addr = write_request->fat32_handle_addr;
        // Operation
        res->base.err = fat32_write(get_mounted_filesystem(), write_request->fat32_handle_addr,write_request->buffer,
                                    write_request->len, &response->bytes_written);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_write_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_SEEK) {
        // Request
        const struct aos_filesystem_rpc_seek_request *read_request = data->recv.data;
        // Response
        struct aos_filesystem_rpc_seek_response *response = (struct aos_filesystem_rpc_seek_response *) res;
        response->fat32_handle_addr = read_request->fat32_handle_addr;
        // Operation
        res->base.err = fat32_seek(get_mounted_filesystem(), read_request->fat32_handle_addr, read_request->whence,
                                   read_request->offset);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_seek_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_TELL) {
        // Request
        const struct aos_filesystem_rpc_tell_request *read_request = data->recv.data;
        // Response
        struct aos_filesystem_rpc_tell_response *response = (struct aos_filesystem_rpc_tell_response *) res;
        // Operation
        res->base.err = fat32_tell(get_mounted_filesystem(), read_request->fat32_handle_addr, &response->position);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_tell_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_CLOSE) {
        // Request
        const struct aos_filesystem_rpc_close_request *close_request = data->recv.data;
        // Operation
        res->base.err = fat32_close(get_mounted_filesystem(), close_request->fat32_handle_addr);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_close_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_DIR_OPEN) {
        // Request
        const struct aos_filesystem_rpc_dir_open_request *dir_open_request = data->recv.data;
        // Response
        struct aos_filesystem_rpc_dir_open_response *response = (struct aos_filesystem_rpc_dir_open_response *) res;
        // Operation
        res->base.err = fat32_open_directory(get_mounted_filesystem(), dir_open_request->path, &response->fat32_handle_addr);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_dir_open_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_DIR_NEXT) {
        // Request
        const struct aos_filesystem_rpc_dir_next_request *read_request = data->recv.data;
        // Response
        struct aos_filesystem_rpc_dir_next_response *response = (struct aos_filesystem_rpc_dir_next_response *) res;
        // Operation
        char *path = NULL;
        res->base.err = fat32_read_next_directory(get_mounted_filesystem(), read_request->fat32_handle_addr, &path);
        // Copy into response - No risk of overflow size < 11
        if(path != NULL) {
            memcpy(response->name, path, strlen(path)+1);
        }
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_dir_next_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_DIR_CLOSE) {
        // Request
        const struct aos_filesystem_rpc_tell_request *dir_close_request = data->recv.data;
        // Operation
        res->base.err = fat32_close_directory(get_mounted_filesystem(), dir_close_request->fat32_handle_addr);
        // Size of the responseaos_rpc
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_dir_close_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_MKDIR) {
        // Request
        const struct aos_filesystem_rpc_mkdir_request *mkdir_request = data->recv.data;
        // Operation
        struct fat32_handle *handle;
        res->base.err = fat32_mkdir(get_mounted_filesystem(), mkdir_request->path, &handle);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_mkdir_response);

    } else if (req->request_type == AOS_RPC_FILESYSTEM_RMDIR)  {
        // Request
        const struct aos_filesystem_rpc_rmdir_request *rmdir_request = data->recv.data;
        // Operation
        res->base.err = fat32_remove_directory(get_mounted_filesystem(), rmdir_request->path);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_rmdir_response);

    } else if(req->request_type == AOS_RPC_FILESYSTEM_MKFILE) {
        // Request
        const struct aos_filesystem_rpc_mkfile_request *mkfile_request = data->recv.data;
        // Operation
        // Response
        struct aos_filesystem_rpc_mkfile_response *response = (struct aos_filesystem_rpc_mkfile_response *) res;
        res->base.err = fat32_create(get_mounted_filesystem(), mkfile_request->path, &response->fat32_handle_addr);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_mkfile_response);

    } else if(req->request_type == AOS_RPC_FILESYSTEM_RMFILE) {
        // Request
        const struct aos_filesystem_rpc_rmfile_request *rmfile_request = data->recv.data;
        // Operation
        res->base.err = fat32_remove(get_mounted_filesystem(), rmfile_request->path);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_rmfile_response);

    } else if(req->request_type == AOS_RPC_FILESYSTEM_IS_DIRECTORY) {
        // Request
        const struct aos_filesystem_rpc_is_directory_request *is_directory_request = data->recv.data;
        // Operation
        struct aos_filesystem_rpc_is_directory_response *response = (struct aos_filesystem_rpc_is_directory_response *) res;
        res->base.err = fat32_is_directory(get_mounted_filesystem(), is_directory_request->path, &response->is_directory);
        // Size of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_is_directory_response);
    } else if(req->request_type == AOS_RPC_FILESYSTEM_STAT) {
        // Request
        const struct aos_filesystem_rpc_stat_request *stat_request = data->recv.data;
        // Operation
        struct aos_filesystem_rpc_stat_response *response = (struct aos_filesystem_rpc_stat_response *) res;
        res->base.err = fat32_stat(get_mounted_filesystem(), stat_request->fat32_handle_addr, &response->file_info);
        // Sie of the response
        *data->send.datasize = sizeof(struct aos_filesystem_rpc_stat_response);
    } else {
        //assert(false);
    }

    return true;
}

// called when getting a response after transmitting a rpc request from one core to the other
static void _handle_simple_async_transmit_response(struct simple_request *req, void *data, size_t size)
{
    struct aos_rpc_handler_data *handler = req->meta;

    memcpy(handler->send.data, data, size);
    *handler->send.datasize = size;
    handler->resume_fn.handler(handler->resume_fn.arg);
    free(handler);
}

static bool _rpc_simple_async_transmit(struct aos_rpc_handler_data *data, domainid_t pid)
{
    struct simple_async_channel* async;
    errval_t err = proc_mgmt_get_async(pid, &async);
    if(err_is_fail(err))
        return false;

    struct aos_rpc_handler_data *handler = malloc(sizeof(struct aos_rpc_handler_data));
    memcpy(handler, data, sizeof(struct aos_rpc_handler_data));
    simple_async_request(async, data->recv.data, data->recv.datasize, _handle_simple_async_transmit_response, handler);

    return true;
}

static bool _handle_network_rpc_request(struct aos_rpc_handler_data* data){
    struct aos_network_basic_request *req = data->recv.data;
    struct aos_generic_rpc_response *res = data->send.data;

    *data->send.datasize = sizeof(struct aos_generic_rpc_response);
    switch (req->type)
    {
    case AOS_RPC_NETWORK_REQUEST_INIT: {
        struct aos_network_request_init* req_init = (struct aos_network_request_init*)req;
        assert(data->spawninfo != NULL);
        res->err = network_rpc_init(&data->spawninfo->async, req_init->mac);
        break;
    }
    case AOS_RPC_NETWORK_LISTEN: {
        struct aos_network_listen_request* list_req = (struct aos_network_listen_request*)req;
        res->err = network_register_listen(list_req->port, list_req->is_tcp, list_req->pid);
        break;
    }
    case AOS_RPC_NETWORK_REQUEST_RECEIVE: {
        struct aos_network_packet_request* req_packet = (struct aos_network_packet_request*)req;
        res->err = network_receive_packet(req_packet->packet_size, req_packet->packet);
        break;
    }
    case AOS_RPC_NETWORK_REQUEST_PING: {
        struct aos_network_ping_request *req_ping = (struct aos_network_ping_request*)req;
        struct aos_network_ping_response *res_ping = (struct aos_network_ping_response*)res;
        *data->send.datasize = sizeof(*res_ping);
        res->err = network_ping(req_ping->ip, &res_ping->base.base.err, &res_ping->ping_ms, data->resume_fn);
        return false;
    }

    case AOS_RPC_NETWORK_REQUEST_SEND: {
        struct aos_network_send_request *req_send = (struct aos_network_send_request*)req;
        if((req_send->pid & 3) != disp_get_core_id()){
            _rpc_transmit(data);
            return false;
        }

        if(req_send->pid == 0){
            // request to the network handler
            res->err = network_send_packet(req_send->target_ip, req_send->target_port, req_send->host_port, req_send->is_tcp, req_send->data_size, req_send->data, &res->err, data->resume_fn);
            return false;
        } else {
            // request to one of the processes
            return !_rpc_simple_async_transmit(data, req_send->pid);
        }
        break;
    }

    case AOS_RPC_NETWORK_SET_IO: {
        struct aos_network_setio_request *req_io = (struct aos_network_setio_request*)req;
        network_set_using_network_io(req_io->is_network, req_io->ip, req_io->is_tcp, req_io->dst_port, req_io->src_port);
        res->err = SYS_ERR_OK;
        break;
    }
    }

    return true;
}

static void _simple_async_rpc_request_handler(struct simple_async_channel *chan, void *data, size_t size,
                               struct simple_response *res);

static errval_t _handle_setup_channel_request(struct aos_rpc_handler_data *data) {
    errval_t err;
    struct capref remote_cap = *data->recv.caps;

    struct aos_rpc* rpc = malloc(sizeof(struct aos_rpc));
    err = aos_rpc_lmp_connect(rpc, remote_cap);
    if(err_is_fail(err))
        return err;

    *data->send.datasize = 16;

    // note: we use send_blocking to force it to use lmp_late_init
    err = aos_rpc_send_blocking(rpc, NULL, 0, NULL_CAP);
    // use the default waitset, or we'll never get the events
    waitset_destroy(rpc->waitset);
    free(rpc->waitset);
    rpc->waitset = get_default_waitset();

    if(err_is_fail(err))
        return err;

    data->resume_fn.handler(data->resume_fn.arg);

    assert(data->spawninfo != NULL);
    simple_async_init(&data->spawninfo->async, rpc, _simple_async_rpc_request_handler);

    return SYS_ERR_OK;
}

static bool _handle_generic_rpc_request(struct aos_rpc_handler_data data) {
    struct aos_generic_rpc_request *req = (struct aos_generic_rpc_request *) data.recv.data;
    struct aos_generic_rpc_response *res = (struct aos_generic_rpc_response *) data.send.data;
    if(data.send.caps_size)
        *data.send.caps_size = 0;

    switch(req->type){
        case AOS_RPC_REQUEST_TYPE_GENERIC_NUMBER:
            HANDLE_RPC_REQUEST(generic_number);

        case AOS_RPC_REQUEST_TYPE_GENERIC_STRING:
            HANDLE_RPC_REQUEST_SZ(generic_string, data.recv.datasize);

        case AOS_RPC_REQUEST_TYPE_SETUP_CHANNEL: {
            errval_t err = _handle_setup_channel_request(&data);
            if(err_is_fail(err))
                USER_PANIC_ERR(err, "Could not setup channel");
            return false;
        }

        case AOS_RPC_REQUEST_TYPE_MEMSERVER:
            HANDLE_RPC_REQUEST(memserver);

        case AOS_RPC_REQUEST_TYPE_PROC_MGMT:
            return _handle_proc_mgmt_rpc_request(&data);

        case AOS_RPC_REQUEST_TYPE_TERMINAL:
            HANDLE_RPC_REQUEST(terminal);

        case AOS_RPC_REQUEST_TYPE_TERMINAL_STR:
            HANDLE_RPC_REQUEST_SZ(terminal_str, data.recv.datasize);

        case AOS_RPC_REQUEST_TYPE_TEST_SUITE:
            HANDLE_RPC_REQUEST(test_suite);

        case AOS_RPC_REQUEST_TYPE_NETWORK:
            return _handle_network_rpc_request(&data);

        case AOS_RPC_REQUEST_TYPE_FILESYSTEM:
            return _handle_filesystem_rpc_request(&data);

        case AOS_RPC_REQUEST_TYPE_DISTCAP:
            return handle_distcap_rpc_request(&data);

        default:
            debug_printf("invalid rpc request type: %d\n", req->type);
            *data.send.datasize = 0;
    }

    return true;
}

static void _sync_rpc_request_resume(void *arg)
{
    struct aos_rpc *chan = arg;
    aos_rpc_send(chan);
}

void sync_rpc_request_handler(struct aos_rpc *rpc, void *arg)
{
    rpc->send_caps_size = 0;
    struct aos_rpc_handler_data data = {
            .recv = {
                    .data = rpc->recv_buf.data,
                    .datasize = rpc->recv_size,
                    .caps = rpc->recv_buf.caps,
                    .caps_size = rpc->recv_caps_size,
            },
            .send = {
                    .data = rpc->send_buf.data,
                    .bufsize = rpc->send_buf.size,
                    .datasize = &rpc->send_size,
                    .caps = rpc->send_buf.caps,
                    .caps_bufsize = rpc->send_buf.caps_size,
                    .caps_size = &rpc->send_caps_size,
            },
            .chan = rpc,
            .spawninfo = arg,
            .resume_fn = MKCLOSURE(_sync_rpc_request_resume, rpc)
    };
    bool send_reply = _handle_generic_rpc_request(data);
    if (send_reply) {
        aos_rpc_send(rpc);
    }
}

struct _async_rpc_resume_arg {
    struct async_channel *chan;
    struct response      *res;
};

static void _async_rpc_request_finalize(struct response *res)
{
    free(res->send.data);
    free(res->send.capv);
}

static void _async_rpc_request_resume(void *arg)
{
    struct _async_rpc_resume_arg *resume_arg = arg;
    resume_arg->res->finalizer               = _async_rpc_request_finalize;
    async_respond(resume_arg->chan, resume_arg->res);
    free(resume_arg);
}

void async_rpc_request_handler(struct async_channel *chan, void *data, size_t size,
                               struct capref *capv, size_t capc, struct response *res)
{

    // debug_printf("async rpc handler, capc = %zu\n", capc);
    // for (size_t i = 0; i < capc; i++) {
    //     debug_print_cap_at_capref(capv[i]);
    //     cap_dump_relations(capv[i]);
    // }

    res->send.data = malloc(4096);
    res->send.capv = malloc(sizeof(struct capref) * 16);

    res->send.capv[0] = NULL_CAP;
    res->send.capc = 0;

    struct _async_rpc_resume_arg *resume_arg = malloc(sizeof(struct _async_rpc_resume_arg));
    resume_arg->chan                         = chan;
    resume_arg->res                          = res;
    struct aos_rpc_handler_data handler = {
            .recv = {
                    .data = data,
                    .datasize = size,
                    .caps = capv,
                    .caps_size = capc,
            },
            .send = {
                    .data = res->send.data,
                    .bufsize = 4096,
                    .datasize = &res->send.size,
                    .caps = res->send.capv,
                    .caps_bufsize = 16,
                    .caps_size = &res->send.capc,
            },
            .chan = chan,
            .spawninfo = NULL,
            .resume_fn = MKCLOSURE(_async_rpc_request_resume, resume_arg)
    };

    bool send_reply = _handle_generic_rpc_request(handler);
    if (send_reply) {
        _async_rpc_request_resume(resume_arg);
    }
}

struct _simple_async_rpc_resume_arg {
    struct simple_async_channel *chan;
    struct simple_response      *res;
};

static void _simple_async_rpc_request_finalize(struct simple_response *res)
{
    free(res->send.data);
}

static void _simple_async_rpc_request_resume(void *arg)
{
    struct _simple_async_rpc_resume_arg *resume_arg = arg;
    resume_arg->res->finalizer               = _simple_async_rpc_request_finalize;
    simple_async_respond(resume_arg->chan, resume_arg->res);
    free(resume_arg);
}

static void _simple_async_rpc_request_handler(struct simple_async_channel *chan, void *data, size_t size,
                               struct simple_response *res) {
    res->send.data = malloc(4096);

    struct _simple_async_rpc_resume_arg *resume_arg = malloc(sizeof(struct _simple_async_rpc_resume_arg));
    resume_arg->chan = chan;
    resume_arg->res = res;
    struct aos_rpc_handler_data handler = {
            .recv = {
                    .data = data,
                    .datasize = size,
                    .caps_size = 0,
                    .caps = NULL
            },
            .send = {
                    .data = res->send.data,
                    .bufsize = 4096,
                    .datasize = &res->send.size,
                    .caps = NULL,
                    .caps_size = NULL,
                    .caps_bufsize = 0
            },
            .chan = chan,
            .spawninfo = NULL,
            .resume_fn = MKCLOSURE(_simple_async_rpc_request_resume, resume_arg)
    };

    bool send_reply = _handle_generic_rpc_request(handler);
    if (send_reply) {
        _simple_async_rpc_request_resume(resume_arg);
    }
}
