#include "async_channel.h"

#include <aos/aos.h>
#include <aos/aos_rpc.h>

#include "aos/domain.h"

#include "cap_transfer.h"

#include "async_channel.h"

static void async_prepare_send(struct async_channel *async)
{
    // if no messages are queued, do nothing
    if (async->requests.head == NULL && async->responses.head == NULL) {
        return;
    }
    if (async->current_sending == ASYNC_REQUEST && async->requests.head == NULL) {
        async->current_sending = ASYNC_RESPONSE;
    }
    if (async->current_sending == ASYNC_RESPONSE && async->responses.head == NULL) {
        async->current_sending = ASYNC_REQUEST;
    }
    struct send_data *send  = NULL;
    struct request   *ident = NULL;
    if (async->current_sending == ASYNC_REQUEST) {
        send  = &async->requests.head->send;
        ident = async->requests.head;
    } else if (async->current_sending == ASYNC_RESPONSE) {
        send  = &async->responses.head->send;
        ident = async->responses.head->identifier;
    }

    size_t msg_size = sizeof(struct async_message) + sizeof(struct cap_transfer) * send->capc
                      + send->size;
    struct async_message *msg = malloc(msg_size);
    msg->identifier           = ident;
    msg->type                 = async->current_sending;
    msg->size                 = send->size;
    msg->capc                 = send->capc;

    for (size_t i = 0; i < send->capc; i++) {
        cap_transfer_move(send->capv[i], (struct cap_transfer *)msg->data + i);
    }
    void *data = msg->data + sizeof(struct cap_transfer) * msg->capc;
    memcpy(data, send->data, send->size);


    async->rpc->send_buf.data = msg;
    async->rpc->send_buf.size = msg_size;
    async->rpc->send_size     = msg_size;

    aos_rpc_send(async->rpc);
}

static void async_handle_send(struct aos_rpc *rpc, void *arg)
{
    // debug_printf("calling send handler\n");
    struct async_channel *async = arg;
    free(rpc->send_buf.data);
    if (async->current_sending == ASYNC_REQUEST) {
        // debug_printf("async_handle_send called with sending ASYNC_REQUEST\n");
        struct request *req  = async->requests.head;
        async->requests.head = req->next;
        if (async->requests.head == NULL) {
            async->requests.tail = NULL;
        }
        // don't free req, because it will be accessed once the corresponding response arrives
        // - free(req);
        async->current_sending = ASYNC_RESPONSE;
    } else if (async->current_sending == ASYNC_RESPONSE) {
        struct response *res  = async->responses.head;
        async->responses.head = res->next;
        if (async->responses.head == NULL) {
            async->responses.tail = NULL;
        }
        res->finalizer(res);
        free(res);
        async->current_sending = ASYNC_REQUEST;
    }
    async_prepare_send(async);
}

static void free_finalizer(struct response *res)
{
    free(res->send.data);
    free(res->send.capv);
}

static void async_handle_recv(struct aos_rpc *rpc, void *arg)
{
    errval_t              err   = SYS_ERR_OK;
    struct async_channel *async = arg;
    struct async_message *msg   = rpc->recv_buf.data;

    struct capref *capv = NULL;
    if (msg->capc > 0) {
        struct cap_transfer *transfers = (struct cap_transfer *)msg->data;
        capv                          = malloc(sizeof(struct capref) * msg->capc);
        for (size_t i = 0; i < msg->capc; i++) {
            struct cap_transfer* transfer = &transfers[i];
            if (!cap_transfer_is_valid(transfer)) {
                capv[i] = NULL_CAP;
                continue;
            }
            err = slot_alloc(&capv[i]);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "slot_alloc failed");
            }
            err = cap_from_transfer(transfer, capv[i]);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "cap_from_transfer failed");
            }
        }
    }
    void *data = msg->data + sizeof(struct cap_transfer) * msg->capc;

    if (msg->type == ASYNC_RESPONSE) {
        // find request with matching identifier
        struct request *req = msg->identifier;
        // call callback
        req->callback(req, data, msg->size, capv, msg->capc);
        // free request
        free(req);
    } else if (msg->type == ASYNC_REQUEST) {
        // allocate response
        struct response *res = malloc(sizeof(struct response));
        res->identifier      = msg->identifier;
        res->finalizer       = free_finalizer;
        res->next            = NULL;
        // call correct handler and give it ref to response
        async->response_handler(async, data, msg->size, capv, msg->capc, res);
    } else {
        USER_PANIC("Invalid async message type");
    }
    free(capv);
    aos_rpc_recv(rpc);
}

void async_init(struct async_channel *async, struct aos_rpc *rpc,
                async_response_handler response_handler)
{
    memset(async, 0, sizeof(struct async_channel));
    async->response_handler = response_handler;
    async->current_sending  = ASYNC_REQUEST;
    async->rpc              = rpc;

    rpc->send_handler = MKHANDLER(async_handle_send, async);
    rpc->recv_handler = MKHANDLER(async_handle_recv, async);

    aos_rpc_recv(rpc);
}

void async_request(struct async_channel *async, void *data, size_t size, struct capref *capv,
                   size_t capc, async_callback callback, void *meta)
{
    // allocate request
    struct request *req = malloc(sizeof(struct request));
    req->send.data      = data;
    req->send.size      = size;
    req->send.capc      = capc;
    req->send.capv      = capv;
    req->callback       = callback;
    req->meta           = meta;
    req->next           = NULL;
    // enqueue req in async->requests and start send if queue was previously empty
    // only enqueue if both requests and responses are empty !!
    bool was_empty = async->requests.head == NULL && async->responses.head == NULL;

    if (async->requests.head == NULL) {
        async->requests.head = req;
        async->requests.tail = req;
    } else {
        async->requests.tail->next = req;
        async->requests.tail       = req;
    }

    // debug_printf("async_request: was_empty=%d\n", was_empty);

    if (was_empty) {
        async_prepare_send(async);
    }
}

void async_respond(struct async_channel *async, struct response *res)
{
    // enqueue res in async->responses and start send if queue was previously empty
    bool was_empty = async->requests.head == NULL && async->responses.head == NULL;

    if (async->responses.head == NULL) {
        async->responses.head = res;
        async->responses.tail = res;
    } else {
        async->responses.tail->next = res;
        async->responses.tail       = res;
    }

    if (was_empty) {
        async_prepare_send(async);
    }
}
