#ifndef LIBBARRELFISH_SIMPLE_ASYNC_CHANNEL_H
#define LIBBARRELFISH_SIMPLE_ASYNC_CHANNEL_H

#include <aos/aos.h>
#include <aos/aos_rpc.h>

struct simple_async_channel;
struct simple_async_message;
struct simple_request;
struct simple_response;

enum simple_async_msg_type {
    SIMPLE_ASYNC_REQUEST,
    SIMPLE_ASYNC_RESPONSE
};

struct simple_async_message {
    struct simple_request* identifier;
    enum simple_async_msg_type type;
    size_t size;
    uintptr_t data[];
};

typedef void (*simple_async_response_handler)(struct simple_async_channel* chan, void* data, size_t size, struct simple_response* res);
typedef void (*simple_async_callback)(struct simple_request* req, void* data, size_t size);

struct simple_request {
    struct simple_request* next;

    simple_async_callback callback;
    struct {
        void* data;
        size_t size;
    } send;

    void* meta;
};

struct simple_response {
    struct simple_request* identifier;
    struct simple_response* next;
    void (*finalizer)(struct simple_response* res);
    struct {
        void* data;
        size_t size;
    } send;
};



struct simple_async_channel {
    struct aos_rpc* rpc;
    enum simple_async_msg_type current_sending;
    struct {
        struct simple_request* head;
        struct simple_request* tail;
    } requests;
    struct {
        struct simple_response* head;
        struct simple_response* tail;
    } responses;
    simple_async_response_handler response_handler;
};

// void sync_call(struct async_channel* async, void* data, size_t size, void** res, size_t ressize) {
// }


void simple_async_init(struct simple_async_channel* async, struct aos_rpc* rpc, simple_async_response_handler response_handler);
void simple_async_request(struct simple_async_channel* async, void* data, size_t size, simple_async_callback callback, void* meta);
void simple_async_respond(struct simple_async_channel* async, struct simple_response* res);

#endif
