#include "iox.h"

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/ump_chan.h>
#include <aos/waitset.h>
#include <aos/deferred.h>

struct io_comm_state {
    bool           is_mapped;  ///< whether the i/o has been mapped
    struct waitset ws;
    struct aos_rpc rpc;
};

#define IOX_READ_BUF_SIZE 4096
struct ump_io_state {
    struct io_comm_state stdin, stdout;

    size_t read_buf_begin, read_buf_end;
    char   read_buf[IOX_READ_BUF_SIZE];
};

static struct ump_io_state iox_io;

static struct cnoderef _get_capv_ref(void)
{
    struct capref capv = {
        .cnode = cnode_root,
        .slot  = ROOTCN_SLOT_TASKCN,
    };
    return build_cnoderef(capv, CNODE_TYPE_OTHER);
}

static errval_t _get_io_frame(struct cnoderef capv, size_t slot, struct capref *io_frame,
                              bool *is_mapped)
{
    errval_t      err   = SYS_ERR_OK;
    struct capref frame = {
        .cnode = capv,
        .slot  = slot,
    };
    struct capability cap;
    err = cap_direct_identify(frame, &cap);
    if (err_no(err) == SYS_ERR_CAP_NOT_FOUND) {
        *io_frame  = NULL_CAP;
        *is_mapped = false;
        return SYS_ERR_OK;  ///< not an error, we just don't have a mapped i/o frame
    }
    if (err_is_fail(err)) {
        *io_frame  = NULL_CAP;
        *is_mapped = false;
        return err;
    }
    if (cap.type != ObjType_Frame) {
        *io_frame  = NULL_CAP;
        *is_mapped = false;
        return SYS_ERR_CAP_NOT_FOUND;
    }
    *io_frame  = frame;
    *is_mapped = true;
    return SYS_ERR_OK;
}

static errval_t _get_io_frames(struct capref *stdin_frame, bool *stdin_mapped,
                               struct capref *stdout_frame, bool *stdout_mapped)
{
    errval_t        err  = SYS_ERR_OK;
    struct cnoderef capv = _get_capv_ref();
    err                  = _get_io_frame(capv, TASKCN_SLOT_STDIN_FRAME, stdin_frame, stdin_mapped);
    if (err_is_fail(err)) {
        return err;
    }
    err = _get_io_frame(capv, TASKCN_SLOT_STDOUT_FRAME, stdout_frame, stdout_mapped);
    if (err_is_fail(err)) {
        return err;
    }
    return SYS_ERR_OK;
}

static errval_t _io_comm_state_init(struct io_comm_state *io, bool output, struct capref frame)
{
    errval_t err = SYS_ERR_OK;
    waitset_init(&io->ws);
    err = aos_rpc_ump_connect(&io->rpc, frame, /*primary*/ output, &io->ws);
    if (err_is_fail(err)) {
        return err;
    }
    return SYS_ERR_OK;
}

__attribute__((used)) static errval_t _ump_io_state_init(struct ump_io_state *io)
{
    errval_t      err = SYS_ERR_OK;
    struct capref stdin_frame, stdout_frame;
    err = _get_io_frames(&stdin_frame, &io->stdin.is_mapped, &stdout_frame, &io->stdout.is_mapped);
    if (err_is_fail(err)) {
        return err;
    }
    if (io->stdin.is_mapped) {
        err = _io_comm_state_init(&io->stdin, false, stdin_frame);
        if (err_is_fail(err)) {
            return err;
        }
    }
    if (io->stdout.is_mapped) {
        err = _io_comm_state_init(&io->stdout, true, stdout_frame);
        if (err_is_fail(err)) {
            return err;
        }
    }
    return SYS_ERR_OK;
}

errval_t iox_init(void)
{
    errval_t err = _ump_io_state_init(&iox_io);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "_ump_io_state_init");
        return err;
    }
    iox_io.read_buf_begin = 0;
    iox_io.read_buf_end   = 0;
    return SYS_ERR_OK;
}

void iox_destroy(void) {
    if (iox_io.stdin.is_mapped) {
        waitset_destroy(&iox_io.stdin.ws);
    }
    if (iox_io.stdout.is_mapped) {
        // XXX This is our "hacky way" to send EOF for now
        //     Ideally, we would have an out-of-band signal for this.
        iox_write("", 1); ///< write a null byte.
        waitset_destroy(&iox_io.stdout.ws);
    }
}

/// called to fill the internal read buffer.
static errval_t _iox_read_internal(void)
{
    assert(iox_io.read_buf_begin == iox_io.read_buf_end);
    errval_t      err = SYS_ERR_OK;
    struct capref retcap;
    iox_io.read_buf_begin = 0;
    err = aos_rpc_recv_blocking(&iox_io.stdin.rpc, iox_io.read_buf, IOX_READ_BUF_SIZE,
                                &iox_io.read_buf_end, &retcap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_recv_blocking");
        return err;
    }
    if (iox_io.read_buf_end == 0 || !capref_is_null(retcap)) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return SYS_ERR_OK;
}

size_t iox_read(char *buf, size_t len)
{
    errval_t err = SYS_ERR_OK;
    if (len == 0) {
        return 0;
    }
    if (!iox_io.stdin.is_mapped) {
        size_t retlen;
        err = aos_rpc_serial_getstr(aos_rpc_get_serial_channel(), buf, len, &retlen);
        if (err_is_fail(err)) {
            return 0;
        }
        return retlen;
    }
    if (iox_io.read_buf_begin == iox_io.read_buf_end) {
        // we need to refill
        err = _iox_read_internal();
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "_iox_read_internal");
            return 0;
        }
    }
    size_t take = iox_io.read_buf_end - iox_io.read_buf_begin;
    if (take > len) {
        take = len;
    }
    // debug_printf("memcpying %zu bytes\n", take);
    memcpy(buf, iox_io.read_buf + iox_io.read_buf_begin, take);
    iox_io.read_buf_begin += take;
    len -= take;

    // XXX consider directly returning on e.g., '\n', etc.
#if 0
    if (len > 0) {
        return take + iox_read(buf + take, len);
    }
#endif
    return take;
}

size_t iox_write(const char *buf, size_t len)
{
    errval_t err = SYS_ERR_OK;
    if (len == 0) {
        return 0;
    }
    if (!iox_io.stdout.is_mapped) {
        /// XXX aos_rpc_serial_putstr has a limit of 1024 bytes, respect that here.
        if (sizeof(struct aos_terminal_str_rpc_request) + len > 1024) {
            len = 1024 - sizeof(struct aos_terminal_str_rpc_request);
        }
        err = aos_rpc_serial_putstr(aos_rpc_get_serial_channel(), buf, len);
        if (err_is_fail(err)) {
            return 0;
        }
        return len;
    }
    err = aos_rpc_send_blocking(&iox_io.stdout.rpc, buf, len, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_send_blocking");
        return 0;
    }
    return len;
}