#include <aos/network.h>

#include <aos/aos_rpc.h>
#include <aos/simple_async_channel.h>

struct listener_list{
    struct listener_list* next;
    enum server_protocol protocol;
    uint16_t port;
    network_listener listener;
    void* meta;
};

struct network_state {
    // list of functions (tcp/udp) listening to ports on this process
    struct listener_list* listeners;
};

static struct network_state ns;

static void async_request_handler(struct simple_async_channel* async, void* data, size_t size, struct simple_response* res){
    (void)size;
    (void)res;
    struct aos_network_send_request* req = data;
    res->send.size = 0;
    res->send.data = NULL;
    if(req->base.base.type != AOS_RPC_REQUEST_TYPE_NETWORK || req->base.type != AOS_RPC_NETWORK_REQUEST_SEND){
        debug_printf("Got network request with unkown type\n");
        simple_async_respond(async, res);
        return;
    }
    enum server_protocol protocol = SERVER_PROTOCOL_UDP;

    struct listener_list* item = ns.listeners;
    while(item != NULL){
        if(item->port == req->host_port && item->protocol == protocol)
            break;

        item = item->next;
    }

    if(item == NULL){
        debug_printf("Got a request to a port we are not listening to\n");
        simple_async_respond(async, res);
        return;
    }
    item->listener(req->target_ip, req->target_port, req->data_size, req->data, item->meta);
    simple_async_respond(async, res);
}

errval_t network_init(void){
    errval_t err;

    memset(&ns, 0, sizeof(struct network_state));

    err = simple_async_proc_setup(async_request_handler);
    if(err_is_fail(err))
        return err;

    return SYS_ERR_OK;
}

errval_t ping(uint32_t target_ip, uint32_t* ping_ms){
    return aos_rpc_network_ping(get_init_rpc(), target_ip, ping_ms);
}

errval_t network_listen(uint16_t port, enum server_protocol protocol, network_listener listener, void* meta){
    assert(protocol == SERVER_PROTOCOL_UDP);
    // first find if we are already listening to this port
    struct listener_list* item = ns.listeners;
    while(item != NULL){
        if(item->port == port && item->protocol == protocol)
            return NETWORK_ERR_PORT_ALREADY_USED;
        item = item->next;
    }

    errval_t err = aos_rpc_network_start_listening(get_init_rpc(), port, false);
    if(err_is_fail(err))
        return err;

    item = malloc(sizeof(struct listener_list));
    item->port = port;
    item->protocol = protocol;
    item->meta = meta;
    item->listener = listener;
    item->next = ns.listeners;
    ns.listeners = item;

    return SYS_ERR_OK;
}

errval_t network_send(uint32_t ip, uint16_t port, enum server_protocol protocol, uint16_t src_port, uint16_t data_size, void* data){
    assert(protocol == SERVER_PROTOCOL_UDP);
    if(data_size > 2000){
        return MM_ERR_OUT_OF_BOUNDS;
    }

    return aos_rpc_network_send(get_init_rpc(), ip, port, false, src_port, data_size, data);
}

errval_t network_set_io(bool is_network, bool is_tcp, uint32_t ip, uint16_t dest_port, uint16_t src_port){
    return aos_rpc_network_set_io(get_init_rpc(), is_network, is_tcp, ip, dest_port, src_port);
}
