/**
 * \file
 * \brief RPC Bindings for AOS
 */

/*
 * Copyright (c) 2013-2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached license file.
 * if you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. attn: systems group.
 */

#include "aos/debug.h"
#include "errors/errno.h"
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/simple_async_channel.h>
#include <argparse/argparse.h>

#define RPC_LMP_MSG_MORE      (1ull << 63)
#define RPC_LMP_MSG_HASCAP    (1ull << 62)
#define RPC_LMP_MSG_SIZE_MASK (~(RPC_LMP_MSG_MORE | RPC_LMP_MSG_HASCAP))
#define RPC_LMP_MSG_MAX_SIZE  ((LMP_MSG_LENGTH - 1) * sizeof(uintptr_t))

struct aos_rpc      rpc_to_init;
struct thread_mutex rpc_mutex;
struct simple_async_channel proc_async;
bool is_async_initialized = false;

static void ensure_recv_capacity(struct aos_rpc *rpc, size_t size)
{
    if (rpc->recv_buf.size < size) {
        // always allocate spare capacity
        size               = size * 2;
        rpc->recv_buf.data = realloc(rpc->recv_buf.data, size);
        rpc->recv_buf.size = size;
    }
}

static void ensure_recv_cap_capacity(struct aos_rpc *rpc, size_t count)
{
    if (rpc->recv_buf.caps_size < count) {
        count                   = count * 2;
        rpc->recv_buf.caps      = realloc(rpc->recv_buf.caps, count * sizeof(struct capref));
        rpc->recv_buf.caps_size = count;
    }
}

static errval_t transport_try_send(struct aos_rpc *rpc, bool *more)
{
    errval_t err = SYS_ERR_OK;
    if (rpc->transport == AOS_RPC_LMP) {
        struct lmp_chan *lc = &rpc->lmp.channel;
        uintptr_t        words[7];

        // debug_printf("offset %zu, size %zu\n", rpc->send_caps_offset, rpc->send_caps_size);
        bool hascap = rpc->send_caps_offset < rpc->send_caps_size;
        struct capref sendcap    = hascap ? rpc->send_buf.caps[rpc->send_caps_offset] : NULL_CAP;
        size_t        send_size  = MIN(rpc->send_size - rpc->send_offset, RPC_LMP_MSG_MAX_SIZE);
        size_t        new_offset = rpc->send_offset + send_size;
        size_t        new_caps_offset = MIN(rpc->send_caps_offset + 1, rpc->send_caps_size);

        *more       = new_offset < rpc->send_size || new_caps_offset < rpc->send_caps_size;
        size_t meta = send_size | (*more ? RPC_LMP_MSG_MORE : 0) | (hascap ? RPC_LMP_MSG_HASCAP : 0);
        memcpy(words, rpc->send_buf.data + rpc->send_offset, send_size);

        // debug_print_cap_at_capref(lc->remote_cap);
        err = lmp_ep_send(lc->remote_cap, LMP_FLAG_SYNC, sendcap, LMP_MSG_LENGTH, meta, words[0],
                          words[1], words[2], words[3], words[4], words[5], words[6]);

        if (!err_is_fail(err)) {
            rpc->send_offset      = new_offset;
            rpc->send_caps_offset = new_caps_offset;
        } 

    } else if (rpc->transport == AOS_RPC_UMP) {
        struct ump_chan *uc         = &rpc->ump.channel;
        struct ump_msg   msg        = {};
        size_t           send_size  = MIN(rpc->send_size - rpc->send_offset, UMP_MSG_MAX_SIZE);
        size_t           new_offset = rpc->send_offset + send_size;

        memcpy(msg.data, rpc->send_buf.data + rpc->send_offset, send_size);
        msg.size = send_size;
        msg.more = new_offset < rpc->send_size;

        // TODO what do we do if there is a cap? We should probably emit an error

        err = ump_chan_send(uc, &msg);

        if (!err_is_fail(err)) {
            rpc->send_offset = new_offset;
        }
        *more = msg.more;
    } else {
        assert(!"Invalid transport");
    }

    return err;
}

static errval_t transport_try_recv(struct aos_rpc *rpc, bool *more)
{
    errval_t err = SYS_ERR_OK;
    if (rpc->transport == AOS_RPC_LMP) {
        struct lmp_chan *lc = &rpc->lmp.channel;

        struct lmp_recv_msg recv_data = LMP_RECV_MSG_INIT;
        struct capref       cap;

        err = lmp_chan_recv(lc, &recv_data, &cap);

        if (err_is_fail(err)) {
            return err;
        }

        uintptr_t *words = recv_data.words;
        size_t     meta  = words[0];
        *more            = (meta & RPC_LMP_MSG_MORE) != 0;
        size_t size      = meta & RPC_LMP_MSG_SIZE_MASK;
        size_t hascap = (meta & RPC_LMP_MSG_HASCAP) != 0;

        ensure_recv_capacity(rpc, rpc->recv_offset + size);
        memcpy(rpc->recv_buf.data + rpc->recv_offset, &words[1], size);
        rpc->recv_offset += size;

        if (hascap) {
            ensure_recv_cap_capacity(rpc, rpc->recv_caps_offset + 1);
            rpc->recv_buf.caps[rpc->recv_caps_offset] = cap;
            rpc->recv_caps_offset += 1;
            if (!capref_is_null(cap)) {
                err = lmp_chan_alloc_recv_slot(lc);
                if (err_is_fail(err)) {
                    USER_PANIC_ERR(err, "lmp_chan_alloc_recv_slot");
                }
            }
        }

    } else if (rpc->transport == AOS_RPC_UMP) {
        struct ump_chan *uc = &rpc->ump.channel;
        struct ump_msg   msg;
        err = ump_chan_recv(uc, &msg);

        if (err_is_fail(err)) {
            // set more to true to indicate that we should try again
            *more = true;
            return err;
        }

        *more = msg.more;
        ensure_recv_capacity(rpc, rpc->recv_offset + msg.size);
        memcpy(rpc->recv_buf.data + rpc->recv_offset, msg.data, msg.size);
        rpc->recv_offset += msg.size;
    } else {
        assert(!"Invalid transport");
    }

    return err;
}

static errval_t transport_register_send(struct aos_rpc *rpc, struct event_closure closure)
{
    errval_t err = SYS_ERR_OK;
    if (rpc->transport == AOS_RPC_LMP) {
        err = lmp_chan_register_send(&rpc->lmp.channel, rpc->waitset, closure);
    } else if (rpc->transport == AOS_RPC_UMP) {
        err = ump_chan_register_send(&rpc->ump.channel, rpc->waitset, closure);
    } else {
        assert(!"Invalid transport");
    }
    return err;
}

static errval_t transport_register_recv(struct aos_rpc *rpc, struct event_closure closure)
{
    errval_t err = SYS_ERR_OK;
    if (rpc->transport == AOS_RPC_LMP) {
        err = lmp_chan_register_recv(&rpc->lmp.channel, rpc->waitset, closure);
    } else if (rpc->transport == AOS_RPC_UMP) {
        err = ump_chan_register_recv(&rpc->ump.channel, rpc->waitset, closure);
    } else {
        assert(!"Invalid transport");
    }
    return err;
}

static void transport_send_handler(void *arg)
{
    errval_t        err = SYS_ERR_OK;
    struct aos_rpc *rpc = arg;

    bool more = false;
    err       = transport_try_send(rpc, &more);

    if (err_is_fail(err) && !(lmp_err_is_transient(err) || ump_err_is_transient(err))) {
        USER_PANIC_ERR(err, "transport_try_send");
    }
    // todo check for non-transient error and abort

    if (more || lmp_err_is_transient(err) || ump_err_is_transient(err)) {
        transport_register_send(rpc, MKCLOSURE(transport_send_handler, arg));
    } else {
        // reset send state
        rpc->send_offset      = 0;
        rpc->send_caps_offset = 0;
        if (rpc->send_handler.handler) {
            rpc->send_handler.handler(rpc, rpc->send_handler.data);
        }
    }
}

static void transport_recv_handler(void *arg)
{
    errval_t        err = SYS_ERR_OK;
    struct aos_rpc *rpc = arg;

    bool more = false;
    err       = transport_try_recv(rpc, &more);

    // todo check for non-transient error and abort
    if (err_is_fail(err) && !(lmp_err_is_transient(err) || ump_err_is_transient(err))) {
        USER_PANIC_ERR(err, "transport_try_recv");
    }
    if (more || lmp_err_is_transient(err) || ump_err_is_transient(err)) {
        transport_register_recv(rpc, MKCLOSURE(transport_recv_handler, arg));
    } else {
        // reset receive state
        rpc->recv_size        = rpc->recv_offset;
        rpc->recv_caps_size   = rpc->recv_caps_offset;
        rpc->recv_offset      = 0;
        rpc->recv_caps_offset = 0;
        if (rpc->recv_handler.handler) {
            rpc->recv_handler.handler(rpc, rpc->recv_handler.data);
        }
    }
}

static errval_t _lmp_init_late_client(struct aos_rpc *rpc);

errval_t aos_rpc_send(struct aos_rpc *rpc)
{
    return transport_register_send(rpc, MKCLOSURE(transport_send_handler, rpc));
}

errval_t aos_rpc_send_with_handler(struct aos_rpc *rpc, struct handler_closure handler)
{
    rpc->send_handler = handler;
    return aos_rpc_send(rpc);
}

errval_t aos_rpc_recv(struct aos_rpc *rpc)
{
    return transport_register_recv(rpc, MKCLOSURE(transport_recv_handler, rpc));
}

errval_t aos_rpc_recv_with_handler(struct aos_rpc *rpc, struct handler_closure handler)
{
    rpc->recv_handler = handler;
    return aos_rpc_recv(rpc);
}

static void _blocking_closure(struct aos_rpc *rpc, void *data)
{
    (void)rpc;
    *(bool *)data = false;
}

errval_t aos_rpc_send_blocking_varsize(struct aos_rpc *rpc, const void *buf, size_t size,
                                       struct capref *capbuf, size_t capsize)
{
    errval_t err = SYS_ERR_OK;

    err = _lmp_init_late_client(rpc);
    if (err_is_fail(err)) {
        return err;
    }

    rpc->send_buf.data = (void *)buf;
    rpc->send_size     = size;
    rpc->send_buf.size = size;

    rpc->send_buf.caps  = capbuf;
    rpc->send_caps_size = capsize;

    bool waiting = true;
    aos_rpc_send_with_handler(rpc, MKHANDLER(_blocking_closure, &waiting));
    while (waiting) {
        err = event_dispatch(rpc->waitset);
        if (err_is_fail(err)) {
            return err;
        }
    }

    return err;
}

errval_t aos_rpc_send_blocking(struct aos_rpc *rpc, const void *buf, size_t size, struct capref cap)
{
    if (capref_is_null(cap)) {
        return aos_rpc_send_blocking_varsize(rpc, buf, size, NULL, 0);
    } else {
        return aos_rpc_send_blocking_varsize(rpc, buf, size, &cap, 1);
    }
}

errval_t aos_rpc_recv_blocking_varsize(struct aos_rpc *rpc, void **buf, size_t *size,
                                       struct capref **capbuf, size_t *capsize)
{
    errval_t err = SYS_ERR_OK;

    err = _lmp_init_late_client(rpc);
    if (err_is_fail(err)) {
        return err;
    }

    bool waiting = true;
    aos_rpc_recv_with_handler(rpc, MKHANDLER(_blocking_closure, &waiting));

    while (waiting) {
        err = event_dispatch(rpc->waitset);
        if (err_is_fail(err)) {
            return err;
        }
    }

    if (buf != NULL)
        *buf = rpc->recv_buf.data;
    if (size != NULL)
        *size = rpc->recv_size;
    if (capbuf != NULL)
        *capbuf = rpc->recv_buf.caps;
    if (capsize != NULL)
        *capsize = rpc->recv_caps_size;

    return err;
}

errval_t aos_rpc_recv_blocking(struct aos_rpc *rpc, void *buf, size_t bufsize, size_t *datasize,
                               struct capref *retcap)
{
    void          *data = NULL;
    size_t         size = 0;
    struct capref *caps;
    size_t         numcaps = 0;
    errval_t       err     = aos_rpc_recv_blocking_varsize(rpc, &data, &size, &caps, &numcaps);
    if (err_is_fail(err)) {
        return err;
    }
    memcpy(buf, data, MIN(size, bufsize));
    if (datasize != NULL)
        *datasize = size;
    if (retcap != NULL) {
        if (numcaps > 1) {
            DEBUG_WARN("aos_rpc_recv_blocking: more than one cap received, using first one\n");
        }
        if (numcaps > 0) {
            *retcap = caps[0];
        } else {
            *retcap = NULL_CAP;
        }
    }
    return err;
}


errval_t aos_rpc_lmp_connect(struct aos_rpc *rpc, struct capref remote)
{
    errval_t err = SYS_ERR_OK;

    memset(rpc, 0, sizeof(*rpc));

    rpc->transport      = AOS_RPC_LMP;
    rpc->late_init_done = false;

    err = lmp_chan_accept(&rpc->lmp.channel, 1024, remote);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_RPC_INIT);
    }

    rpc->recv_offset = 0;
    rpc->send_size   = 0;
    rpc->send_offset = 0;

    rpc->recv_buf.caps = malloc(sizeof(struct capref));
    rpc->recv_buf.caps_size = 1;
    rpc->recv_buf.data = malloc(8192);
    rpc->recv_buf.size = 8192;

    err = lmp_chan_alloc_recv_slot(&rpc->lmp.channel);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp_chan_alloc_recv_slot");
    }

    return err;
}

errval_t aos_rpc_ump_connect(struct aos_rpc *rpc, struct capref frame, bool primary,
                             struct waitset *waitset)
{
    errval_t err = SYS_ERR_OK;

    memset(rpc, 0, sizeof(*rpc));
    rpc->transport = AOS_RPC_UMP;
    err            = ump_chan_init(&rpc->ump.channel, frame, primary);
    if (err_is_fail(err)) {
        return err;
    }
    rpc->waitset = waitset;

    rpc->send_handler = NOOP_HANDLER;
    rpc->recv_handler = NOOP_HANDLER;

    return err;
}


static errval_t _lmp_init_late_client(struct aos_rpc *rpc)
{
    if (rpc->late_init_done) {
        return SYS_ERR_OK;
    }
    rpc->late_init_done = true;

    // init_late only required for lmp transport
    if (rpc->transport != AOS_RPC_LMP) {
        return SYS_ERR_OK;
    }

    errval_t err = SYS_ERR_OK;

    // malloc waitset memory
    rpc->waitset = malloc(sizeof(struct waitset));
    waitset_init(rpc->waitset);

    // send local endpoint capability to init
    err = aos_rpc_send_blocking(rpc, NULL, 0, rpc->lmp.channel.local_cap);

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed to send local endpoint capability to init");
        return err_push(err, LIB_ERR_RPC_INIT_LATE);
    }

    size_t        buf;
    size_t        recvsize;
    struct capref cap;
    err = aos_rpc_recv_blocking(rpc, &buf, sizeof(buf), &recvsize, &cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed to receive ack from init");
        return err_push(err, LIB_ERR_RPC_INIT_LATE);
    }

    if (recvsize != 8 || buf != 42) {
        debug_printf("aos_rpc_init_late: received invalid ack from init, got %zu, expected 42\n",
                     buf);
        debug_printf("size: %zu\n", recvsize);
        return LIB_ERR_RPC_INIT_LATE;
    }

#if DEBUG_AOS_RPC
    debug_printf("aos_rpc_init: done\n");
#endif

    return err;
}

static void _lmp_setup_handler(void *arg)
{
    errval_t         err = SYS_ERR_OK;
    struct aos_rpc  *rpc = arg;
    struct lmp_chan *lc  = &rpc->lmp.channel;

    struct lmp_recv_msg recv_data = LMP_RECV_MSG_INIT;
    struct capref       cap;

    err = lmp_chan_recv(lc, &recv_data, &cap);

    if (err_is_fail(err) && lmp_err_is_transient(err)) {
        // reregister
        lmp_chan_register_recv(lc, rpc->waitset, MKCLOSURE(_lmp_setup_handler, arg));
        return;
    }

    if (capref_is_null(cap)) {
        debug_printf("ERROR: no endpoint capability received\n");
        return;
    }

    lc->remote_cap = cap;

    err = lmp_chan_alloc_recv_slot(lc);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp_chan_alloc_recv_slot");
    }

#if DEBUG_AOS_RPC
    debug_printf("remote endpoint capability received and initialized\n");
#endif

    // send ack for channel setup to client, and then start listening for requests
    size_t ack     = 42;
    rpc->send_size = sizeof(ack);
    // TODO check if sendbuf is large enough
    memcpy(rpc->send_buf.data, &ack, sizeof(ack));

    aos_rpc_send(rpc);
}

static void _server_handler_func(struct aos_rpc *rpc, void *data)
{
    (void)data;
    aos_rpc_recv(rpc);
}

errval_t aos_rpc_lmp_listen(struct aos_rpc *rpc, struct capref *retcap)
{
    errval_t err = SYS_ERR_OK;
    memset(rpc, 0, sizeof(*rpc));

    rpc->transport = AOS_RPC_LMP;

    err = lmp_chan_accept(&rpc->lmp.channel, 1023, NULL_CAP);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed to create endpoint");
        return err;
    }

    rpc->send_buf.data      = malloc(1024);
    rpc->send_buf.size      = 1024;
    rpc->send_buf.caps      = malloc(sizeof(struct capref));
    rpc->send_buf.caps_size = 1;

    *retcap = rpc->lmp.channel.local_cap;

    return err;
}

errval_t aos_rpc_lmp_accept(struct aos_rpc *rpc, struct handler_closure handler,
                            struct waitset *waitset)
{
    errval_t err      = SYS_ERR_OK;
    rpc->waitset      = waitset;
    rpc->recv_handler = handler;
    rpc->send_handler = MKHANDLER(_server_handler_func, NULL);

    // allocate receive slot for endpoint capability in bootstrapping
    err = lmp_chan_alloc_recv_slot(&rpc->lmp.channel);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "lmp_chan_alloc_recv_slot");
        return err;
    }

    // bootstrap the lmp channel
    err = lmp_chan_register_recv(&rpc->lmp.channel, waitset, MKCLOSURE(_lmp_setup_handler, rpc));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failed to register receive handler");
        return err;
    }

    return err;
}

void aos_rpc_destroy_server(struct aos_rpc *rpc)
{
    lmp_chan_destroy(&rpc->lmp.channel);
    free(rpc->recv_buf.data);
    free(rpc->send_buf.data);
    memset(rpc, 0, sizeof(*rpc));
}

/*
 * ===============================================================================================
 * Generic RPCs
 * ===============================================================================================
 */


static errval_t _rpc_prepare_and_send(struct aos_rpc *rpc, struct aos_generic_rpc_request *req,
                                      size_t size, struct capref cap)
{
    errval_t err = SYS_ERR_OK;
    // send the request over the specified channel
    err = aos_rpc_send_blocking(rpc, req, size, cap);
    if (err_is_fail(err)) {
        return err;
    }
    return SYS_ERR_OK;
}

static errval_t _rpc_recv_and_validate(struct aos_rpc *rpc, struct aos_generic_rpc_response *res,
                                       size_t size, struct capref *cap)
{
    errval_t      err = SYS_ERR_OK;
    size_t        datasize;
    bool          no_cap = cap == NULL;
    struct capref dummy_cap;
    if (no_cap) {
        cap = &dummy_cap;
    }
    err = aos_rpc_recv_blocking(rpc, res, size, &datasize, cap);
    if (err_is_fail(err)) {
        return err;
    }
    if (no_cap && !capref_is_null(*cap)) {
        // throwing away a cap is not allowed!
        debug_printf("attempting to through away received cap (rpc.)\n");
        debug_print_cap_at_capref(*cap);
        return SYS_ERR_GUARD_MISMATCH;
    }
    if (datasize != size) {
        debug_printf("mismatching rpc size (expected: %zu, received: %zu)\n", size, datasize);
        return SYS_ERR_GUARD_MISMATCH;
    }
    if (err_is_fail(res->err)) {
        return res->err;
    }
    return err;
}

/**
 * @brief Send a single number over an RPC channel.
 *
 * @param[in] chan  the RPC channel to use
 * @param[in] val   the number to send
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: init
 */
errval_t aos_rpc_send_number(struct aos_rpc *rpc, uintptr_t num)
{
    errval_t                              err = SYS_ERR_OK;
    struct aos_generic_number_rpc_request req;
    req.val       = num;
    req.base.type = AOS_RPC_REQUEST_TYPE_GENERIC_NUMBER;
    err           = _rpc_prepare_and_send(rpc, (struct aos_generic_rpc_request *)&req,
                                          sizeof(struct aos_generic_number_rpc_request), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_generic_number_rpc_response res;
    err = _rpc_recv_and_validate(rpc, (struct aos_generic_rpc_response *)&res,
                                 sizeof(struct aos_generic_number_rpc_response), NULL);
    if (err_is_fail(err)) {
        return err;
    }
    if (res.base.type != AOS_RPC_RESPONSE_TYPE_GENERIC_NUMBER) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return SYS_ERR_OK;
}


/**
 * @brief Send a single number over an RPC channel.
 *
 * @param[in] chan  the RPC channel to use
 * @param[in] val   the string to send
 *
 * @returns SYS_ERR_OK on success, or error value on failure
 *
 * Channel: init
 */
errval_t aos_rpc_send_string(struct aos_rpc *rpc, const char *string)
{
    errval_t err  = SYS_ERR_OK;
    size_t   size = strlen(string) + 1;
    size_t   len  = sizeof(struct aos_generic_string_rpc_request) + sizeof(char) * size;


    void                                  *buf = malloc(len);
    struct aos_generic_string_rpc_request *req = buf;
    req->size                                  = size;
    req->base.type                             = AOS_RPC_REQUEST_TYPE_GENERIC_STRING;
    /// XXX pack the string at the end of the request
    memcpy(buf + sizeof(struct aos_generic_string_rpc_request), string, size);
    err = _rpc_prepare_and_send(rpc, (struct aos_generic_rpc_request *)req, len, NULL_CAP);
    if (err_is_fail(err)) {
        goto cleanup;
    }

    struct aos_generic_string_rpc_response res;
    struct capref                          cap;
    size_t                                 datasize;
    err = aos_rpc_recv_blocking(rpc, &res, sizeof(struct aos_generic_string_rpc_response),
                                &datasize, &cap);
    if (err_is_fail(err)) {
        goto cleanup;
    }
    if (res.base.type != AOS_RPC_RESPONSE_TYPE_GENERIC_STRING) {
        err = SYS_ERR_GUARD_MISMATCH;
        goto cleanup;
    }
cleanup:
    free(buf);
    return err;
}


/*
 * ===============================================================================================
 * RAM Alloc RPCs
 * ===============================================================================================
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
errval_t aos_rpc_get_ram_cap(struct aos_rpc *rpc, size_t bytes, size_t alignment,
                             struct capref *ret_cap, size_t *ret_bytes)
{
    errval_t err = SYS_ERR_OK;
    // printf("Current space: %d\n", get_default_slot_allocator()->space);

    // setup the memserver request
    struct aos_memserver_rpc_request req;
    req.base.type = AOS_RPC_REQUEST_TYPE_MEMSERVER;
    req.size      = bytes;
    req.alignment = alignment;

    // send the request over the specified channel
    err = aos_rpc_send_blocking(rpc, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_memserver_rpc_response res;
    size_t                            datasize;
    err = aos_rpc_recv_blocking(rpc, &res, sizeof(res), &datasize, ret_cap);
    if (err_is_fail(err)) {
        return err;
    }
    if (err_is_fail(res.base.err)) {
        return res.base.err;
    }

    *ret_bytes = res.retbytes;
    return SYS_ERR_OK;
}


/*
 * ===============================================================================================
 * Serial RPCs
 * ===============================================================================================
 */


/**
 * @brief obtains a single character from the serial
 *
 * @param chan  the RPC channel to use (serial channel)
 * @param retc  returns the read character
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_serial_getchar(struct aos_rpc *rpc, char *retc)
{
    // debug_printf("aos_rpc_serial_getchar\n");
    errval_t                        err = SYS_ERR_OK;
    struct aos_terminal_rpc_request req;

    req.base.type = AOS_RPC_REQUEST_TYPE_TERMINAL;
    req.ttype     = AOS_TERMINAL_RPC_REQUEST_TYPE_GETCHAR;

    err = _rpc_prepare_and_send(rpc, (struct aos_generic_rpc_request *)&req,
                                sizeof(struct aos_terminal_rpc_request), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_terminal_rpc_response res;
    err = _rpc_recv_and_validate(rpc, (struct aos_generic_rpc_response *)&res,
                                 sizeof(struct aos_terminal_rpc_response), NULL);
    if (err_is_fail(err)) {
        return err;
    }
    if (res.ttype != AOS_TERMINAL_RPC_RESPONSE_TYPE_GETCHAR) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    *retc = res.u.getchar.c;
    return SYS_ERR_OK;
}


errval_t aos_rpc_serial_getstr(struct aos_rpc *rpc, char *buf, size_t buflen, size_t *retlen)
{
    (void)buf;
    (void)retlen;
    errval_t err = SYS_ERR_OK;
    assert(buflen <= 1024);

    struct aos_terminal_str_rpc_request *req = malloc(sizeof(struct aos_terminal_str_rpc_request));
    req->ttype                               = AOS_TERMINAL_STR_RPC_REQUEST_TYPE_GETSTR;
    req->size                                = buflen;
    req->base.type                           = AOS_RPC_REQUEST_TYPE_TERMINAL_STR;
    err = _rpc_prepare_and_send(rpc, (struct aos_generic_rpc_request *)req,
                                sizeof(struct aos_terminal_str_rpc_request), NULL_CAP);
    if (err_is_fail(err)) {
        *retlen = 0;
        return err;
    }

    size_t recv_size = sizeof(struct aos_terminal_str_rpc_response) + buflen;
    struct aos_terminal_str_rpc_response *res = malloc(recv_size);
    struct capref                         cap;
    size_t                                datasize;
    err = aos_rpc_recv_blocking(rpc, res, sizeof(struct aos_terminal_str_rpc_response) + buflen,
                                &datasize, &cap);
    if (err_is_fail(err)) {
        *retlen = 0;
        return err;
    }
    if (res->base.type != AOS_RPC_RESPONSE_TYPE_TERMINAL_STR
        || res->ttype != AOS_TERMINAL_STR_RPC_RESPONSE_TYPE_GETSTR) {
        printf("invalid response type!\n");
        *retlen = 0;
        return SYS_ERR_GUARD_MISMATCH;
    }

    assert(res->size <= buflen);
    *retlen = res->size;

    memcpy(buf, res->buf, res->size);

    free(res);
    return SYS_ERR_OK;
}

/**
 * @brief sends a single character to the serial
 *
 * @param chan  the RPC channel to use (serial channel)
 * @param c     the character to send
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_serial_putchar(struct aos_rpc *rpc, char c)
{
    errval_t                        err = SYS_ERR_OK;
    struct aos_terminal_rpc_request req;

    req.base.type   = AOS_RPC_REQUEST_TYPE_TERMINAL;
    req.ttype       = AOS_TERMINAL_RPC_REQUEST_TYPE_PUTCHAR;
    req.u.putchar.c = c;

    err = _rpc_prepare_and_send(rpc, (struct aos_generic_rpc_request *)&req,
                                sizeof(struct aos_terminal_rpc_request), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_terminal_rpc_response res;
    err = _rpc_recv_and_validate(rpc, (struct aos_generic_rpc_response *)&res,
                                 sizeof(struct aos_terminal_rpc_response), NULL);
    if (err_is_fail(err)) {
        return err;
    }
    if (res.ttype != AOS_TERMINAL_RPC_RESPONSE_TYPE_PUTCHAR) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return SYS_ERR_OK;
}

errval_t aos_rpc_serial_putstr(struct aos_rpc *rpc, const char *str, size_t len)
{
    errval_t err  = SYS_ERR_OK;
    size_t   size = sizeof(struct aos_terminal_str_rpc_request) + sizeof(char) * len;
    assert(size <= 1024);

    void                                *buf = malloc(size);
    struct aos_terminal_str_rpc_request *req = buf;
    req->size                                = len;
    req->ttype                               = AOS_TERMINAL_STR_RPC_REQUEST_TYPE_PUTSTR;
    req->base.type                           = AOS_RPC_REQUEST_TYPE_TERMINAL_STR;
    /// XXX pack the string at the end of the request
    memcpy(buf + sizeof(struct aos_terminal_str_rpc_request), str, len);
    err = _rpc_prepare_and_send(rpc, (struct aos_generic_rpc_request *)req, size, NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_terminal_str_rpc_response res;
    struct capref                        cap;
    size_t                               datasize;
    err = aos_rpc_recv_blocking(rpc, &res, sizeof(struct aos_terminal_str_rpc_response), &datasize,
                                &cap);
    if (err_is_fail(err)) {
        return err;
    }
    if (res.base.type != AOS_RPC_RESPONSE_TYPE_TERMINAL_STR) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return SYS_ERR_OK;
}


/*
 * ===============================================================================================
 * Processes RPCs
 * ===============================================================================================
 */

static const struct aos_generic_rpc_request _rpc_proc_mgmt_request
    = { .type = AOS_RPC_REQUEST_TYPE_PROC_MGMT };

static errval_t _aos_rpc_proc_spawn_cmdline_with_caps(struct aos_rpc *chan, const char *cmdline,
                                                      int capc, struct capref capv[], coreid_t core,
                                                      domainid_t *newpid, bool is_default,
                                                      struct capref stdin_frame, struct capref stdout_frame)
{
    size_t req_size = sizeof(struct aos_proc_mgmt_rpc_spawn_request) + strlen(cmdline) + 1;
    struct aos_proc_mgmt_rpc_spawn_request *req = malloc(req_size);
    req->base.base                              = _rpc_proc_mgmt_request;
    req->base.proc_type = is_default ? AOS_RPC_PROC_MGMT_REQUEST_SPAWN_DEFAULT
                                     : AOS_RPC_PROC_MGMT_REQUEST_SPAWN_CMDLINE;
    req->capc           = capc;
    req->base.core      = core;
    strcpy(req->cmdline, cmdline);

    struct capref *extcapv = malloc(sizeof(struct capref) * (capc + 2));
    for (int i = 0; i < capc; ++i) {
        extcapv[i] = capv[i];
    }
    extcapv[capc + 0] = stdin_frame;
    extcapv[capc + 1] = stdout_frame;
    errval_t err = aos_rpc_send_blocking_varsize(chan, req, req_size, extcapv, capc + 2);
    free(extcapv);
    free(req);
    if (err_is_fail(err))
        return err;

    struct aos_proc_mgmt_rpc_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }
    if (newpid)
        *newpid = res.pid;

    return res.base.err;
}

errval_t aos_rpc_proc_spawn_mapped(struct aos_rpc *chan, int argc, const char *argv[], int capc,
                                   struct capref capv[], coreid_t core, domainid_t *newpid,
                                   struct capref stdin_frame, struct capref stdout_frame)
{
    errval_t err     = SYS_ERR_OK;
    char    *cmdline = NULL;
    err              = argv_to_cmdline(argc, argv, &cmdline);
    if (err_is_fail(err)) {
        return err;
    }
    err = _aos_rpc_proc_spawn_cmdline_with_caps(chan, cmdline, capc, capv, core, newpid, false, stdin_frame, stdout_frame);
    free(cmdline);
    return err;
}

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
                                      struct capref capv[], coreid_t core, domainid_t *newpid)
{
    return aos_rpc_proc_spawn_mapped(chan, argc, argv, capc, capv, core, newpid, NULL_CAP, NULL_CAP);
}


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
                                         domainid_t *newpid)
{
    return _aos_rpc_proc_spawn_cmdline_with_caps(chan, cmdline, 0, NULL, core, newpid, false, NULL_CAP, NULL_CAP);
}


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
                                              domainid_t *newpid)
{
    return _aos_rpc_proc_spawn_cmdline_with_caps(chan, path, 0, NULL, core, newpid, true, NULL_CAP, NULL_CAP);
}

/**
 * @brief obtains a list of PIDs of all processes in the system
 *
 * @param[in]  chan       the RPC channel to use (process channel)
 * @param[out] pids       array of PIDs of all processes in the system (freed by caller)
 * @param[out] pid_count  the number of PIDs in the list
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_all_pids(struct aos_rpc *chan, domainid_t **pids, size_t *pid_count)
{
    // make compiler happy about unused parameters
    struct aos_proc_mgmt_rpc_request req = { .base      = _rpc_proc_mgmt_request,
                                             .proc_type = AOS_RPC_PROC_MGMT_REQUEST_ALL_PIDS,
                                             // get the pid from all cores
                                             .core = (coreid_t)-1 };

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    // TODO: this may not be enough
    struct aos_proc_mgmt_rpc_all_pid_response *res = malloc(1024);
    err = aos_rpc_recv_blocking(chan, res, 1024, NULL, NULL);
    if (err_is_fail(err))
        return err;

    *pid_count = res->num;
    *pids      = malloc(res->num * sizeof(domainid_t));
    memcpy(*pids, res->pids, res->num * sizeof(domainid_t));

    err = res->base.err;
    free(res);

    return err;
}

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
                                 char *cmdline, int cmdline_max, uint8_t *state, int *exit_code)
{
    struct aos_proc_mgmt_rpc_basic_request req;
    req.base.base      = _rpc_proc_mgmt_request;
    req.base.core      = pid % PROC_MGMT_MAX_CORES;
    req.base.proc_type = AOS_RPC_PROC_MGMT_REQUEST_STATUS;
    req.pid            = pid;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_proc_mgmt_rpc_status_response *res = malloc(
        sizeof(struct aos_proc_mgmt_rpc_status_response));
    err = aos_rpc_recv_blocking(chan, res, sizeof(*res), NULL, NULL);
    if (err_is_fail(err)) {
        free(res);
        return err;
    }

    if (cmdline && cmdline_max >= 1)
        strncpy(cmdline, res->status.cmdline, cmdline_max - 1);

    if (core)
        *core = res->status.core;

    if (state)
        *state = res->status.state;

    if (exit_code)
        *exit_code = res->status.exit_code;

    err = res->base.err;

    free(res);
    return err;
}


/**
 * @brief obtains the name of a process with a given PID
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] name  the name of the process to search for
 * @param[in] pid   returns PID of the process to pause/suspend
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_get_name(struct aos_rpc *chan, domainid_t pid, char *name, size_t len)
{
    struct aos_proc_mgmt_rpc_basic_request req;
    req.base.base      = _rpc_proc_mgmt_request;
    req.base.core      = pid % PROC_MGMT_MAX_CORES;
    req.base.proc_type = AOS_RPC_PROC_MGMT_REQUEST_NAME;
    req.pid            = pid;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_proc_mgmt_rpc_response *res = malloc(1024);
    err                                    = aos_rpc_recv_blocking(chan, res, 1024, NULL, NULL);
    if (err_is_fail(err)) {
        free(res);
        return err;
    }

    strncpy(name, res->name, len - 1);

    err = res->base.err;

    free(res);
    return err;
}


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
errval_t aos_rpc_proc_get_pid(struct aos_rpc *chan, const char *name, domainid_t *pid)
{
    size_t req_size = sizeof(struct aos_proc_mgmt_rpc_basic_request) + strlen(name) + 1;
    struct aos_proc_mgmt_rpc_basic_request *req = malloc(req_size);
    req->base.base                              = _rpc_proc_mgmt_request;
    req->base.proc_type                         = AOS_RPC_PROC_MGMT_REQUEST_PID;
    // the process can be in any core
    req->base.core = (coreid_t)-1;
    strcpy(req->name, name);

    errval_t err = aos_rpc_send_blocking(chan, req, req_size, NULL_CAP);
    if (err_is_fail(err))
        return err;

    free(req);
    struct aos_proc_mgmt_rpc_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        free(req);
        return err;
    }

    *pid = res.pid;

    return res.base.err;
}


/**
 * @brief pauses or suspends the execution of a running process
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to pause/suspend
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_pause(struct aos_rpc *chan, domainid_t pid)
{
    struct aos_proc_mgmt_rpc_basic_request req;
    req.base.base      = _rpc_proc_mgmt_request;
    req.base.proc_type = AOS_RPC_PROC_MGMT_REQUEST_PAUSE;
    req.base.core      = pid % PROC_MGMT_MAX_CORES;
    req.pid            = pid;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_proc_mgmt_rpc_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.err;
}


/**
 * @brief resumes a previously paused process
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to resume
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_resume(struct aos_rpc *chan, domainid_t pid)
{
    struct aos_proc_mgmt_rpc_basic_request req;
    req.base.base      = _rpc_proc_mgmt_request;
    req.base.proc_type = AOS_RPC_PROC_MGMT_REQUEST_RESUME;
    req.base.core      = pid % PROC_MGMT_MAX_CORES;
    req.pid            = pid;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_proc_mgmt_rpc_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.err;
}


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
errval_t aos_rpc_proc_exit(struct aos_rpc *chan, int status)
{
    struct aos_proc_mgmt_rpc_exit_request req;
    req.base.base      = _rpc_proc_mgmt_request;
    req.base.proc_type = AOS_RPC_PROC_MGMT_REQUEST_EXIT;
    req.base.core      = disp_get_core_id();
    req.pid            = proc_mgmt_get_self_pid();
    req.exit_code      = status;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_proc_mgmt_rpc_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    // we should never be able to reach this part
    return res.base.err;
}


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
errval_t aos_rpc_proc_wait(struct aos_rpc *chan, domainid_t pid, int *status)
{
    struct aos_proc_mgmt_rpc_basic_request req;
    req.base.base      = _rpc_proc_mgmt_request;
    req.base.proc_type = AOS_RPC_PROC_MGMT_REQUEST_WAIT;
    req.base.core      = pid % PROC_MGMT_MAX_CORES;
    req.pid            = pid;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_proc_mgmt_rpc_wait_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    if (status)
        *status = res.exit_code;

    return res.base.err;
}

/**
 * @brief requests that the process with the given PID is terminated
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] pid   PID of the process to be terminated
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_kill(struct aos_rpc *chan, domainid_t pid)
{
    struct aos_proc_mgmt_rpc_basic_request req;
    req.base.base      = _rpc_proc_mgmt_request;
    req.base.proc_type = AOS_RPC_PROC_MGMT_REQUEST_KILL;
    req.base.core      = pid % PROC_MGMT_MAX_CORES;
    req.pid            = pid;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_proc_mgmt_rpc_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.err;
}


/**
 * @brief requests that all processes that match the supplied name are terminated
 *
 * @param[in] chan  the RPC channel to use (process channel)
 * @param[in] name  name of the processes to be terminated
 *
 * @return SYS_ERR_OK on success, or error value on failure
 */
errval_t aos_rpc_proc_kill_all(struct aos_rpc *chan, const char *name)
{
    size_t req_size = sizeof(struct aos_proc_mgmt_rpc_basic_request) + strlen(name) + 1;
    struct aos_proc_mgmt_rpc_basic_request *req = malloc(req_size);
    req->base.base                              = _rpc_proc_mgmt_request;
    req->base.proc_type                         = AOS_RPC_PROC_MGMT_REQUEST_KILLALL;
    // a matching process can be in any core
    req->base.core = (coreid_t)-1;
    strcpy(req->name, name);

    errval_t err = aos_rpc_send_blocking(chan, req, req_size, NULL_CAP);

    free(req);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_proc_mgmt_rpc_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.err;
}

errval_t aos_rpc_filesystem_open(struct aos_rpc *chan, char *path, struct fat32_handle **fat32_handle_addr){
    struct aos_filesystem_rpc_open_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_OPEN;
    memcpy(req.path, path, strlen(path) + 1);

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_open_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    *fat32_handle_addr = res.fat32_handle_addr;

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_read(struct aos_rpc *chan, struct fat32_handle* fat32_handle_addr, void *buf, size_t len, size_t *bytes_read) {
    *bytes_read = 0;
    void *current_buffer = buf;

    while(*bytes_read < len) {
        const size_t size_structure = sizeof(struct aos_filesystem_rpc_read_request);
        struct aos_filesystem_rpc_read_request req;
        req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
        req.base.request_type = AOS_RPC_FILESYSTEM_READ;
        req.fat32_handle_addr = fat32_handle_addr;
        req.len = MIN(512,len - *bytes_read);

        errval_t err = aos_rpc_send_blocking(chan, &req, size_structure, NULL_CAP);
        if (err_is_fail(err)) {
            return err;
        }

        struct aos_filesystem_rpc_read_response res;
        err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
        if (err_is_fail(err)) {
            return err;
        }
        if(res.len == 0) {
            break;
        }

        *bytes_read += res.len;
        memcpy(current_buffer, res.buffer, res.len);
        current_buffer += res.len;

        if(err_is_fail(res.base.base.err)) {
            return res.base.base.err;
        }
    }

    return SYS_ERR_OK;
}


errval_t aos_rpc_filesystem_write(struct aos_rpc *chan, struct fat32_handle* fat32_handle_addr, void *buf, size_t len, size_t *bytes_written) {
    *bytes_written = 0;
    void *current_buffer = buf;

    while(*bytes_written < len) {
        const size_t size_structure = sizeof(struct aos_filesystem_rpc_write_request);
        struct aos_filesystem_rpc_write_request req;
        req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
        req.base.request_type = AOS_RPC_FILESYSTEM_WRITE;
        req.fat32_handle_addr = fat32_handle_addr;
        req.len = MIN(512,len - *bytes_written);
        memcpy(req.buffer, current_buffer, req.len);

        errval_t err = aos_rpc_send_blocking(chan, &req, size_structure, NULL_CAP);
        if (err_is_fail(err)) {
            return err;
        }

        struct aos_filesystem_rpc_write_response res;
        err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
        if (err_is_fail(err)) {
            return err;
        }

        *bytes_written += req.len;
        current_buffer += req.len;

        if(err_is_fail(res.base.base.err)) {
            return res.base.base.err;
        }
    }

    return SYS_ERR_OK;
}

errval_t aos_rpc_filesystem_seek(struct aos_rpc *chan, struct fat32_handle* fat32_handle_addr, off_t offset, int whence) {
    struct aos_filesystem_rpc_seek_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_SEEK;
    req.fat32_handle_addr = fat32_handle_addr;
    req.offset = offset;
    req.whence = whence;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(struct aos_filesystem_rpc_seek_request), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_seek_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_tell(struct aos_rpc *chan, struct fat32_handle* fat32_handle_addr, size_t *position) {
    struct aos_filesystem_rpc_tell_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_TELL;
    req.fat32_handle_addr = fat32_handle_addr;


    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(struct aos_filesystem_rpc_tell_request), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_tell_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    *position = res.position;

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_close(struct aos_rpc *chan, struct fat32_handle *fat32_handle_addr) {
    const size_t size_structure = sizeof(struct aos_filesystem_rpc_close_request);
    struct aos_filesystem_rpc_close_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_CLOSE;
    req.fat32_handle_addr = fat32_handle_addr;

    errval_t err = aos_rpc_send_blocking(chan, &req, size_structure, NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_filesystem_rpc_close_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_dir_open(struct aos_rpc *chan, const char *path, struct fat32_handle **fat32_handle_addr){
    struct aos_filesystem_rpc_dir_open_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_DIR_OPEN;
    memcpy(req.path, path, strlen(path) + 1);

    assert(path[0] == FS_PATH_SEP);

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_dir_open_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    *fat32_handle_addr = res.fat32_handle_addr;

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_dir_next(struct aos_rpc *chan, struct fat32_handle *fat32_handle_addr, char **new_name){
    struct aos_filesystem_rpc_dir_next_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_DIR_NEXT;
    req.fat32_handle_addr = fat32_handle_addr;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_dir_next_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    size_t size = strlen(res.name) + 1;

    *new_name = malloc(sizeof(size));
    memcpy(*new_name, res.name, size);

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_dir_close(struct aos_rpc *chan, struct fat32_handle *fat32_handle_addr) {
    struct aos_filesystem_rpc_close_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_DIR_CLOSE;
    req.fat32_handle_addr = fat32_handle_addr;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err))
        return err;

    struct aos_filesystem_rpc_dir_close_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_mkdir(struct aos_rpc *chan, const char *path) {

    struct aos_filesystem_rpc_mkdir_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_MKDIR;
    memcpy(req.path, path, strlen(path) + 1);

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_mkdir_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_rmdir(struct aos_rpc *chan, const char *path) {
    struct aos_filesystem_rpc_rmdir_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_RMDIR;
    memcpy(req.path, path, strlen(path) + 1);

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_rmdir_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_mkfile(struct aos_rpc *chan, const char *path, struct fat32_handle **fat32_handle_addr) {
    struct aos_filesystem_rpc_mkfile_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_MKFILE;
    memcpy(req.path, path, strlen(path) + 1);

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_mkfile_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    *fat32_handle_addr = res.fat32_handle_addr;

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_rmfile(struct aos_rpc *chan, const char *path) {
    struct aos_filesystem_rpc_rmfile_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_RMFILE;
    memcpy(req.path, path, strlen(path) + 1);

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_rmfile_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_is_directory(struct aos_rpc *chan, const char *path, bool *is_directory) {
    struct aos_filesystem_rpc_rmfile_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_IS_DIRECTORY;
    memcpy(req.path, path, strlen(path) + 1);

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_is_directory_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    *is_directory = res.is_directory;

    return res.base.base.err;
}

errval_t aos_rpc_filesystem_stat(struct aos_rpc *chan, struct fat32_handle *fat32_handle_addr, struct fs_fileinfo *file_info) {
    struct aos_filesystem_rpc_stat_request req;
    req.base.base.type = AOS_RPC_REQUEST_TYPE_FILESYSTEM;
    req.base.request_type = AOS_RPC_FILESYSTEM_STAT;
    req.fat32_handle_addr = fat32_handle_addr;

    errval_t err = aos_rpc_send_blocking(chan, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_filesystem_rpc_stat_response res;
    err = aos_rpc_recv_blocking(chan, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    *file_info = res.file_info;

    return res.base.base.err;
}


errval_t aos_rpc_test_suite_run(struct aos_rpc *rpc, struct test_suite_config config)
{
    errval_t                          err = SYS_ERR_OK;
    struct aos_test_suite_rpc_request req;
    req.config    = config;
    req.base.type = AOS_RPC_REQUEST_TYPE_TEST_SUITE;
    err           = _rpc_prepare_and_send(rpc, (struct aos_generic_rpc_request *)&req,
                                          sizeof(struct aos_test_suite_rpc_request), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }

    struct aos_test_suite_rpc_response res;
    err = _rpc_recv_and_validate(rpc, (struct aos_generic_rpc_response *)&res,
                                 sizeof(struct aos_test_suite_rpc_response), NULL);
    if (err_is_fail(err)) {
        return err;
    }
    if (res.base.type != AOS_RPC_RESPONSE_TYPE_TEST_SUITE) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return res.base.err;
}

errval_t aos_rpc_cap_retype_remote(struct aos_rpc *rpc, struct capref src_root,
                                   struct capref dest_root, capaddr_t src, gensize_t offset,
                                   enum objtype new_type, gensize_t objsize, size_t count,
                                   capaddr_t to, capaddr_t slot, int to_level)
{
    struct aos_distcap_retype_request req = {
        .base = {
            .base = {
                .type = AOS_RPC_REQUEST_TYPE_DISTCAP,
            },
            .type = AOS_RPC_DISTCAP_RETYPE,
        },
        .src = src,
        .offset = offset,
        .new_type = new_type,
        .objsize = objsize,
        .count = count,
        .to = to,
        .slot = slot,
        .to_level = to_level,
    };
    struct capref caps[] = { src_root, dest_root };
    errval_t      err    = aos_rpc_send_blocking_varsize(rpc, &req, sizeof(req), caps, 2);
    if (err_is_fail(err)) {
        return err;
    }
    struct aos_generic_rpc_response res;
    err = _rpc_recv_and_validate(rpc, &res, sizeof(res), NULL);
    if (err_is_fail(err)) {
        return err;
    }
    if (res.type != AOS_RPC_RESPONSE_TYPE_DISTCAP) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return res.err;
}

errval_t aos_rpc_cap_delete_remote(struct aos_rpc *rpc, struct capref root, capaddr_t src,
                                   uint8_t level)
{
    struct aos_distcap_delete_request req = {
        .base = {
            .base = {
                .type = AOS_RPC_REQUEST_TYPE_DISTCAP,
            },
            .type = AOS_RPC_DISTCAP_DELETE,
        },
        .src = src,
        .level = level,
    };
    errval_t err = aos_rpc_send_blocking_varsize(rpc, &req, sizeof(req), &root, 1);
    if (err_is_fail(err)) {
        return err;
    }
    struct aos_generic_rpc_response res;
    err = _rpc_recv_and_validate(rpc, &res, sizeof(res), NULL);
    if (err_is_fail(err)) {
        return err;
    }
    if (res.type != AOS_RPC_RESPONSE_TYPE_DISTCAP) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return res.err;
}

errval_t aos_rpc_cap_revoke_remote(struct aos_rpc *rpc, struct capref root, capaddr_t src,
                                   uint8_t level)
{
    struct aos_distcap_revoke_request req = {
        .base = {
            .base = {
                .type = AOS_RPC_REQUEST_TYPE_DISTCAP,
            },
            .type = AOS_RPC_DISTCAP_REVOKE,
        },
        .src = src,
        .level = level,
    };

    errval_t err = aos_rpc_send_blocking_varsize(rpc, &req, sizeof(req), &root, 1);
    if (err_is_fail(err)) {
        return err;
    }
    struct aos_generic_rpc_response res;
    err = _rpc_recv_and_validate(rpc, &res, sizeof(res), NULL);
    if (err_is_fail(err)) {
        return err;
    }
    if (res.type != AOS_RPC_RESPONSE_TYPE_DISTCAP) {
        return SYS_ERR_GUARD_MISMATCH;
    }
    return res.err;
}

errval_t aos_rpc_network_ping(struct aos_rpc* rpc, uint32_t target_ip, uint32_t* ping_ms){
    struct aos_network_ping_request req = {
        .base = {
            .base = {
                .type = AOS_RPC_REQUEST_TYPE_NETWORK,
            },
            .type = AOS_RPC_NETWORK_REQUEST_PING,
        },
        .ip = target_ip
    };

    errval_t err = aos_rpc_send_blocking(rpc, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }
    struct aos_network_ping_response res;
    err = aos_rpc_recv_blocking(rpc, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    if(ping_ms != NULL)
        *ping_ms = res.ping_ms;

    return res.base.base.err;
}

errval_t aos_rpc_network_start_listening(struct aos_rpc* rpc, uint16_t port, bool is_tcp){
    struct aos_network_listen_request req = {
        .base = {
            .base = {
                .type = AOS_RPC_REQUEST_TYPE_NETWORK,
            },
            .type = AOS_RPC_NETWORK_LISTEN,
        },
        .port = port,
        .pid = disp_get_domain_id(),
        .is_tcp = is_tcp
    };

    errval_t err = aos_rpc_send_blocking(rpc, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }
    struct aos_generic_rpc_response res;
    err = aos_rpc_recv_blocking(rpc, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.err;
}

errval_t aos_rpc_network_send(struct aos_rpc* rpc, uint32_t ip, uint16_t port, bool is_tcp, uint16_t src_port, uint16_t data_size, void* data){
    size_t req_size = sizeof(struct aos_network_send_request) + data_size;
    struct aos_network_send_request* req = malloc(req_size);
    
    *req = (struct aos_network_send_request){
        .base = {
            .base = {
                .type = AOS_RPC_REQUEST_TYPE_NETWORK,
            },
            .type = AOS_RPC_NETWORK_REQUEST_SEND,
        },
        // this request is for the init process
        .pid = 0,
        .is_tcp = is_tcp,
        .target_ip = ip,
        .target_port = port,
        .host_port = src_port,
        .data_size = data_size
    };
    memcpy(req->data, data, data_size);

    errval_t err = aos_rpc_send_blocking(rpc, req, req_size, NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }
    struct aos_generic_rpc_response res;
    err = aos_rpc_recv_blocking(rpc, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.err;
}

errval_t aos_rpc_network_set_io(struct aos_rpc* rpc, bool is_network, bool is_tcp, uint32_t ip, uint16_t dest_port, uint16_t src_port){
    struct aos_network_setio_request req = {
        .base = {
            .base = {
                .type = AOS_RPC_REQUEST_TYPE_NETWORK,
            },
            .type = AOS_RPC_NETWORK_SET_IO,
        },
        .is_network = is_network,
        .is_tcp = is_tcp,
        .ip = ip,
        .dst_port = dest_port,
        .src_port = src_port
    };

    errval_t err = aos_rpc_send_blocking(rpc, &req, sizeof(req), NULL_CAP);
    if (err_is_fail(err)) {
        return err;
    }
    struct aos_generic_rpc_response res;
    err = aos_rpc_recv_blocking(rpc, &res, sizeof(res), NULL, NULL);
    if (err_is_fail(err)) {
        return err;
    }

    return res.err;
}

/**
 * \brief Returns the RPC channel to init.
 */
struct aos_rpc *aos_rpc_get_init_channel(void)
{
    return get_init_rpc();
}

/**
 * \brief Returns the channel to the memory server
 */
struct aos_rpc *aos_rpc_get_memory_channel(void)
{
    // XXX for now we just use the init_rpc channel for everything.
    return aos_rpc_get_init_channel();
}

/**
 * \brief Returns the channel to the process manager
 */
struct aos_rpc *aos_rpc_get_process_channel(void)
{
    // XXX for now we just use the init_rpc channel for everything.
    return aos_rpc_get_init_channel();
}

/**
 * \brief Returns the channel to the serial console
 */
struct aos_rpc *aos_rpc_get_serial_channel(void)
{
    // XXX for now we just use the init_rpc channel for everything.
    return aos_rpc_get_init_channel();
}

struct aos_rpc *aos_rpc_get_filesystem_channel(void) {
    return aos_rpc_get_init_channel();
}


static void _init_rpc_handler_done(struct aos_rpc* rpc, void* setup_done){
    (void)rpc;
    *(bool*)setup_done = true;
}

errval_t simple_async_proc_setup(simple_async_response_handler response_handler){
    // we are not in an init process
    assert(disp_get_domain_id() >= 4);

    if(is_async_initialized)
        return SYS_ERR_OK;

    errval_t err;

    struct capref lmp_cap;
    struct aos_rpc* rpc = malloc(sizeof(struct aos_rpc));
    err = aos_rpc_lmp_listen(rpc, &lmp_cap);
    if(err_is_fail(err)){
        DEBUG_ERR(err, "Could not create lmp capability");
    }
    struct aos_generic_rpc_request setup_req;
    setup_req.type = AOS_RPC_REQUEST_TYPE_SETUP_CHANNEL;
    err = aos_rpc_send_blocking(get_init_rpc(), &setup_req, sizeof(setup_req), lmp_cap);
    if(err_is_fail(err)){
        DEBUG_ERR(err, "Could not send lmp capability");
    }
    bool is_setup = false;
    err = aos_rpc_lmp_accept(rpc, MKHANDLER(_init_rpc_handler_done, &is_setup), get_default_waitset());
    if(err_is_fail(err)){
        DEBUG_ERR(err, "Could not accept new lmp");
    }

    while(!is_setup){
        err = event_dispatch(get_default_waitset());
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }
    err = aos_rpc_recv_blocking(get_init_rpc(), NULL, 0, NULL, NULL);
    if(err_is_fail(err))
        DEBUG_ERR(err, "Could not receive from rpc");

    simple_async_init(&proc_async, rpc, response_handler);
    is_async_initialized = true;

    return SYS_ERR_OK;
}

struct simple_async_channel* aos_rpc_get_async_channel(void){
    if(!is_async_initialized)
        USER_PANIC("The async channel is not initialized");

    return &proc_async;
}
