#ifndef USER_INIT_ASYNC_CHANNEL_H
#define USER_INIT_ASYNC_CHANNEL_H

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/capabilities.h>

struct async_channel;
struct async_message;
struct request;
struct response;

enum async_msg_type { ASYNC_REQUEST, ASYNC_RESPONSE };

struct async_message {
    struct request     *identifier;
    enum async_msg_type type;
    size_t              size;
    size_t              capc;
    char                data[];
};

typedef void (*async_response_handler)(struct async_channel *chan, void *data, size_t size,
                                       struct capref *capv, size_t capc, struct response *res);
typedef void (*async_callback)(struct request *req, void *data, size_t size, struct capref *capv,
                               size_t capc);

struct send_data {
    void          *data;
    size_t         size;
    size_t         capc;
    struct capref *capv;
};

struct request {
    struct request *next;

    async_callback   callback;
    struct send_data send;

    void *meta;
};

struct response {
    struct request  *identifier;
    struct response *next;
    void (*finalizer)(struct response *res);
    struct send_data send;
};

struct async_channel {
    struct aos_rpc     *rpc;
    enum async_msg_type current_sending;
    struct {
        struct request *head;
        struct request *tail;
    } requests;
    struct {
        struct response *head;
        struct response *tail;
    } responses;
    async_response_handler response_handler;
};

void async_init(struct async_channel *async, struct aos_rpc *rpc,
                async_response_handler response_handler);
void async_request(struct async_channel *async, void *data, size_t size, struct capref *capv,
                   size_t capc, async_callback callback, void *meta);
void async_respond(struct async_channel *async, struct response *res);

#endif
