#include "aos/simple_async_channel.h"

#include <aos/aos.h>
#include <aos/aos_rpc.h>

static void simple_async_prepare_send(struct simple_async_channel* async) {
    // if no messages are queued, do nothing
    if (async->requests.head == NULL && async->responses.head == NULL) {
        // debug_printf("async_prepare_send called with no messages queued\n");
        return;
    }
    if (async->current_sending == SIMPLE_ASYNC_REQUEST) {
        if (async->requests.head == NULL) {
            async->current_sending = SIMPLE_ASYNC_RESPONSE;
            return simple_async_prepare_send(async);
        }

        struct simple_request* req = async->requests.head;
        // including header
        size_t msg_size = sizeof(struct simple_async_message) + req->send.size;

        struct simple_async_message* msg = malloc(msg_size);
        msg->identifier = req;
        msg->type = SIMPLE_ASYNC_REQUEST;
        msg->size = req->send.size;
        memcpy(msg->data, req->send.data, req->send.size);

        async->rpc->send_buf.data = msg;
        async->rpc->send_buf.size = msg_size;
        async->rpc->send_size = msg_size;

    } else if (async->current_sending == SIMPLE_ASYNC_RESPONSE) {
        if (async->responses.head == NULL) {
            async->current_sending = SIMPLE_ASYNC_REQUEST;
            return simple_async_prepare_send(async);
        } 

        struct simple_response* res = async->responses.head;
        // including header
        size_t msg_size = sizeof(struct simple_async_message) + res->send.size;

        struct simple_async_message* msg = malloc(msg_size);
        msg->identifier = res->identifier;
        msg->type = SIMPLE_ASYNC_RESPONSE;
        msg->size = res->send.size;
        memcpy(msg->data, res->send.data, res->send.size);

        async->rpc->send_buf.data = msg;
        async->rpc->send_buf.size = msg_size;
        async->rpc->send_size = msg_size;
    }
    // debug_printf("Sending async message of size %zu\n", async->rpc->send_buf.size);
    aos_rpc_send(async->rpc);
}

static void simple_async_handle_send(struct aos_rpc* rpc, void* arg) {
    // debug_printf("calling send handler\n");
    struct simple_async_channel* async = arg;
    free(rpc->send_buf.data);
    if (async->current_sending == SIMPLE_ASYNC_REQUEST) {
        // debug_printf("async_handle_send called with sending ASYNC_REQUEST\n");
        struct simple_request* req = async->requests.head;
        async->requests.head = req->next;
        if (async->requests.head == NULL) {
            async->requests.tail = NULL;
        }
        // don't free req, because it will be accessed once the corresponding response arrives
        // - free(req);
        async->current_sending = SIMPLE_ASYNC_RESPONSE;
    } else if (async->current_sending == SIMPLE_ASYNC_RESPONSE) {
        struct simple_response* res = async->responses.head;
        async->responses.head = res->next;
        if (async->responses.head == NULL) {
            async->responses.tail = NULL;
        }
        res->finalizer(res);
        free(res);
        async->current_sending = SIMPLE_ASYNC_REQUEST;
    }
    simple_async_prepare_send(async);
}

static void free_finalizer(struct simple_response* res) {
    free(res->send.data);
}

static void simple_async_handle_recv(struct aos_rpc* rpc, void* arg) {
    struct simple_async_channel* async = arg;
    struct simple_async_message* msg = rpc->recv_buf.data;

    // may not match message size due to truncation
    size_t datasize = rpc->recv_size - sizeof(struct simple_async_message);
    if (msg->type == SIMPLE_ASYNC_RESPONSE) {
        // find request with matching identifier
        struct simple_request* req = msg->identifier;
        // call callback
        req->callback(req, &msg->data, datasize);
        // free request
        free(req);
    } else if (msg->type == SIMPLE_ASYNC_REQUEST) {
        // allocate response
        struct simple_response* res = malloc(sizeof(struct simple_response));
        res->identifier = msg->identifier;
        res->finalizer = free_finalizer;
        res->next = NULL;
        // call correct handler and give it ref to response
        async->response_handler(async, &msg->data, datasize, res);
    }
    aos_rpc_recv(rpc);
}

void simple_async_init(struct simple_async_channel* async, struct aos_rpc* rpc, simple_async_response_handler response_handler) {
    async->response_handler = response_handler;
    async->current_sending = SIMPLE_ASYNC_REQUEST;
    async->rpc = rpc;
    async->requests.head = NULL;
    async->requests.tail = NULL;
    async->responses.head = NULL;
    async->responses.tail = NULL;

    rpc->send_handler = MKHANDLER(simple_async_handle_send, async);
    rpc->recv_handler = MKHANDLER(simple_async_handle_recv, async);

    aos_rpc_recv(rpc);
}

void simple_async_request(struct simple_async_channel* async, void* data, size_t size, simple_async_callback callback, void* meta) {
    // allocate request
    struct simple_request* req = malloc(sizeof(struct simple_request));
    req->send.data = data;
    req->send.size = size;
    req->callback = callback;
    req->meta = meta;
    req->next = NULL;
    // enqueue req in async->requests and start send if queue was previously empty
    // only enqueue if both requests and responses are empty !!
    bool was_empty = async->requests.head == NULL && async->responses.head == NULL;

    if (async->requests.head == NULL) {
        async->requests.head = req;
        async->requests.tail = req;
    } else {
        async->requests.tail->next = req;
        async->requests.tail = req;
    }

    // debug_printf("async_request: was_empty=%d\n", was_empty);

    if (was_empty) {
        simple_async_prepare_send(async);
    }
}

void simple_async_respond(struct simple_async_channel* async, struct simple_response* res) {
    // enqueue res in async->responses and start send if queue was previously empty
    bool was_empty = async->requests.head == NULL && async->responses.head == NULL;

    if (async->responses.head == NULL) {
        async->responses.head = res;
        async->responses.tail = res;
    } else {
        async->responses.tail->next = res;
        async->responses.tail = res;
    }

    if (was_empty) {
        simple_async_prepare_send(async);
    }
}

