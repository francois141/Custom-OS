#include <aos/aos.h>

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/simple_async_channel.h>
#include "../drivers/virtio-net/virtio_net_device.h"
#include "../drivers/enet/enet.h"
#include <netutil/udp.h>

static const size_t packet_size = 2048;
// header at the beginning of the packet that must be reserved for the driver
static const size_t header_size = 12;

struct network_queue {
    // queue and its parameters
    struct devq* queue;
    size_t size;
    // frame containing the shared regions with the queue
    struct capref frame;
    // pointer to the above frame
    uint8_t* buffer;
    // array of size packet_size specifying for each packet if can be sent or not
    // only used by the transfer queue
    bool* packet_ready;
    regionid_t rid;
};

struct network_state {
    void* net_device;
    // mac address of the device
    struct eth_addr mac;
    // channel used to communicate with the init process
    struct simple_async_channel* async;

    // transfer queue
    struct network_queue tx;
    // receive queue
    struct network_queue rx;
};

struct network_state _network_state;

static errval_t vnet_init(void){
    errval_t err;
    debug_printf("virtio-net: Driver started.\n");

    // Guard: Allocate a new device instance
    struct vnet_device* device = vnet_device_create();

    if (device == NULL)
        USER_PANIC("virtio-net: Failed to allocate the device instance.");

    _network_state.net_device = device;
    // Guard: Initialize the device instance
    struct vnet_device_config config = { .rx_queue_size = 0, .tx_queue_size = 0 };

    err = vnet_device_init(device, &config /* NULL or specify a queue size of 0 to use the maximum size */);

    if (err_is_fail(err))
        return err;

    // Guard: Probe the virtio network device
    err = vnet_device_probe(device);
    if (err_is_fail(err))
        return err;

    // Guard: Start the virtio network device
    err = vnet_device_start(device);
    if (err_is_fail(err))
    return err;

    // Guard: Retrieve the MAC address
    err = vnet_device_get_mac_address(device, &_network_state.mac);
    if(err_is_fail(err))
        return err;

    debug_printf("virtio-net: MAC Address = %02X:%02X:%02X:%02X:%02X:%02X.\n",
                 _network_state.mac.addr[0], _network_state.mac.addr[1], _network_state.mac.addr[2], _network_state.mac.addr[3], _network_state.mac.addr[4], _network_state.mac.addr[5]);

    _network_state.rx.queue = vnet_device_get_rx_queue(device);
    _network_state.rx.size = vnet_device_get_rx_queue_size(device);
    _network_state.tx.queue = vnet_device_get_tx_queue(device);
    _network_state.tx.size = vnet_device_get_rx_queue_size(device);

    return SYS_ERR_OK;
}

static errval_t dev_enet_init(void){
    errval_t err;

    debug_printf("Enet driver started \n");
    struct enet_driver_state * st = (struct enet_driver_state*)
                                    calloc(1, sizeof(struct enet_driver_state));
    _network_state.net_device = st;

    err = enet_device_init(st);
    if(err_is_fail(err)){
        return err;
    }

    enet_read_mac(st);
    memcpy(_network_state.mac.addr, &st->mac, 6);
    // revert the mac address because it's in the wrong order..
    for(int i = 0; i < 3; i++){
        uint8_t temp = _network_state.mac.addr[i];
        _network_state.mac.addr[i] =_network_state.mac.addr[5-i];
        _network_state.mac.addr[5-i] = temp;
    }
    debug_printf("enet: MAC Address = %02X:%02X:%02X:%02X:%02X:%02X.\n",
                 _network_state.mac.addr[0], _network_state.mac.addr[1], _network_state.mac.addr[2], _network_state.mac.addr[3], _network_state.mac.addr[4], _network_state.mac.addr[5]);

    err = enet_probe(st);
    if (err_is_fail(err)) {
        // TODO cleanup
        return err;
    }

    err = enet_init(st);
    if (err_is_fail(err)) {
        // TODO cleanup
        return err;
    }

    debug_printf("Enet driver init done \n");
    debug_printf("Creating devqs \n");

    err = enet_rx_queue_create(&st->rxq, st->d);
    if (err_is_fail(err)) {
        debug_printf("Failed creating RX devq \n");
        return err;
    }

    err = enet_tx_queue_create(&st->txq, st->d);
    if (err_is_fail(err)) {
        debug_printf("Failed creating RX devq \n");
        return err;
    }

    _network_state.rx.queue = (struct devq*)st->rxq;
    _network_state.rx.size = st->rxq->size;
    _network_state.tx.queue = (struct devq*)st->txq;
    _network_state.tx.size = st->txq->size;

    return SYS_ERR_OK;
}

static errval_t network_init_queue(struct network_queue* queue, bool is_transfer){
    // queue.queue and queue.size must already be set
    errval_t err;
    size_t queue_buffer_size = queue->size * packet_size;
    err = frame_alloc(&queue->frame, queue_buffer_size, NULL);
    if(err_is_fail(err))
        return err;

    err = paging_map_frame_attr(get_current_paging_state(), (void**)&queue->buffer, queue_buffer_size, queue->frame, VREGION_FLAGS_READ_WRITE_NOCACHE);
    if(err_is_fail(err))
        return err;

    err = devq_register(queue->queue, queue->frame, &queue->rid);
    if(err_is_fail(err))
        return err;

    if(is_transfer){
        queue->packet_ready = malloc(queue->size);
        memset(queue->packet_ready, true, queue->size);
    } else {
        // enqueue all packets
        // don't enqueue the last one because the enet driver does not like it...
        for(size_t i = 0; i < queue->size-1; i++){
            err = devq_enqueue(queue->queue, queue->rid, i * packet_size, packet_size, 0, packet_size, 0);
            if(err_is_fail(err))
                return err;
        }
    }

    return SYS_ERR_OK;
}

static errval_t network_stack_init(const char* platform_name){
    errval_t err;
    if(strcmp(platform_name, "qemu") == 0)
        err = vnet_init();
    else if(strcmp(platform_name, "imx8x") == 0)
        err = dev_enet_init();
    else 
        err = ERR_INVALID_ARGS;

    if(err_is_fail(err))
        return err;

    err = network_init_queue(&_network_state.rx, false);
    if(err_is_fail(err))
        return err;
    err = network_init_queue(&_network_state.tx, true);
    if(err_is_fail(err))
        return err;

    struct aos_network_request_init init_req;
    memcpy(init_req.mac, _network_state.mac.addr, 6);
    init_req.base.type = AOS_RPC_NETWORK_REQUEST_INIT;
    init_req.base.base.type = AOS_RPC_REQUEST_TYPE_NETWORK;
    err = aos_rpc_send_blocking(get_init_rpc(), &init_req, sizeof(init_req), NULL_CAP);
    if(err_is_fail(err))
        return err;
    err = aos_rpc_recv_blocking(get_init_rpc(), NULL, 0, NULL, NULL);
    if(err_is_fail(err))
        return err;

    return SYS_ERR_OK;
}

static void async_request_free(struct simple_request* req, void* data, size_t size){
    (void)data;
    (void)size;
    free(req->send.data);
}

static errval_t receive_packet(void){
    errval_t err;
    struct devq_buf packet;
    err = devq_dequeue(_network_state.rx.queue, &packet.rid,
                                   &packet.offset, &packet.length,
                                   &packet.valid_data, &packet.valid_length,
                                   &packet.flags);
    if(err_is_fail(err))
        return SYS_ERR_OK;

    size_t req_size = sizeof(struct aos_network_packet_request) + packet.valid_length;
    struct aos_network_packet_request *req = malloc(req_size);
    req->base.base.type = AOS_RPC_REQUEST_TYPE_NETWORK;
    req->base.type = AOS_RPC_NETWORK_REQUEST_RECEIVE;
    req->packet_size = packet.valid_length;
    memcpy(req->packet, _network_state.rx.buffer + packet.offset + packet.valid_data, packet.valid_length);
    simple_async_request(_network_state.async, req, req_size, async_request_free, NULL);

    // Zero out the buffer
    memset(_network_state.rx.buffer + packet.offset, 0xCC, packet.length);

    // Reset the packet buffer
    packet.valid_data = 0;
    packet.valid_length = packet.length;
    
    // Finished processing the packet
    // Put it back to the RX queue so that the device can reuse it
    err = devq_enqueue(_network_state.rx.queue, packet.rid,
                         packet.offset, packet.length,
                         packet.valid_data, packet.valid_length,
                         packet.flags);
    

    return err;
}

static errval_t send_packet(size_t size, void* data){
    errval_t err;
    // find a free packet to use
    ssize_t packet_idx = -1;
    for(size_t i = 0; i < _network_state.tx.size; i++){
        if(_network_state.tx.packet_ready[i]){
            packet_idx = i;
            break;
        }
    }

    struct devq_buf packet;
    while(packet_idx == -1){
        // try to retrieve a buffer after it was used
        err = devq_dequeue(_network_state.tx.queue, &packet.rid, &packet.offset, &packet.length, &packet.valid_data, &packet.valid_length, &packet.flags);
        if(err_is_ok(err)){
            assert(packet.rid == _network_state.tx.rid);
            packet_idx = packet.offset / packet_size;
        }
    }

    packet.rid = _network_state.tx.rid;
    packet.offset = packet_idx * packet_size;
    packet.length = packet_size;
    packet.valid_data = header_size;
    packet.valid_length = size;
    packet.flags = 0;

    memcpy(_network_state.tx.buffer + packet.offset + packet.valid_data, data, size);
    err = devq_enqueue(_network_state.tx.queue, packet.rid, packet.offset, packet.length, packet.valid_data, packet.valid_length, packet.flags);
    
    return err;
}

static void async_request_handler(struct simple_async_channel* chan, void* data, size_t size, struct simple_response* res){
    errval_t err = send_packet(size, data);
    if(err_is_fail(err))
        DEBUG_ERR(err, "Failed to send packet");

    res->send.data = NULL;
    res->send.size = 0;
    simple_async_respond(chan, res);
}

int main(int argc, char** argv)
{
    errval_t err;
    (void)argc;
    (void)argv;

    if(argc < 2){
        debug_printf("network usage: network <imx8x/qemu>\n");
        return EXIT_FAILURE;
    }

    err = simple_async_proc_setup(async_request_handler);
    if(err_is_fail(err))
        DEBUG_ERR(err, "Failed to initialize async channel");
    _network_state.async = aos_rpc_get_async_channel();

    err = network_stack_init(argv[1]);
    if(err_is_fail(err))
        DEBUG_ERR(err, "Failed to init network");

    struct waitset* ws = get_default_waitset();
    while(true){
        err = check_for_event(ws);
        if(err_is_ok(err)){
            err = event_dispatch(ws);
            assert(err_is_ok(err));
        }
        receive_packet();
        thread_yield();
    }

    debug_printf("Goodbye.");
    return 0;
}
