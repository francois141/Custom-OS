#include "network_handler.h"

#include <aos/aos.h>
#include <collections/list.h>
#include <aos/systime.h>
#include <aos/deferred.h>
#include <collections/hash_table.h>
#include <spawn/spawn.h>
#include <netutil/etharp.h>
#include <netutil/ip.h>
#include <netutil/udp.h>
#include <netutil/icmp.h>
#include <netutil/htons.h>
#include <netutil/checksum.h>

#include "rpc_handler.h"
#include "proc_mgmt.h"

// 10.0.2.1
static const uint32_t  self_ip   = 0x0102000A;
static struct eth_addr empty_mac = { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };

struct request_with_timeout {
    struct request_with_timeout *next;
    struct request_with_timeout *prev;
    enum { REQ_PING, REQ_UDP } type;
    uint32_t              ip;
    uint32_t              port;
    struct eth_addr       mac;
    struct event_closure  resume_fn;
    errval_t             *err;
    struct deferred_event event;
    systime_t             timestamp;
    void                 *meta1;
    uint32_t              meta2;
    uint16_t              data_size;
    void*                 data;
};

struct network_io_waiting_getchar {
    struct network_io_waiting_getchar* next;
    struct event_closure resume_fn;
    size_t len;
    size_t* ret_len;
    void* buf;
};

struct network_state {
    // mac address of the device
    struct eth_addr              mac;
    domainid_t                   network_pid;
    struct simple_async_channel *async;

    // hash table containing the ip to mac addresses we received
    collections_hash_table *ip_to_mac;
    // gives for each port if there is a pid listening to it
    collections_hash_table *port_to_pid;

    // lists of current requests
    struct request_with_timeout arp_list;
    struct request_with_timeout ping_list;

    // used for the id field of the ip header, incremented each time
    uint16_t next_ip_id;
    // used for the seqno filed in ICMP requests
    uint16_t next_seqno_id;

    bool using_network_io;
    bool io_tcp;
    uint32_t io_ip;
    uint16_t io_target_port;
    uint16_t io_host_port;
    char* io_recv_buf;
    size_t io_recv_size;
    size_t io_recv_pos;

    char* io_send_buf;
    size_t io_send_pos;
    size_t io_send_size;

    struct network_io_waiting_getchar *io_getchar_waiting;
};

struct network_state ns;

static void async_request_free(struct simple_request *req, void *data, size_t size)
{
    (void)data;
    (void)size;
    free(req->send.data);
}

static char  _ip_buf[16];
static char *_format_ip(uint32_t ip)
{
    sprintf(_ip_buf, "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF,
            (ip >> 24) & 0xFF);
    return _ip_buf;
}

static char  _mac_buf[18];
static char *_format_mac(struct eth_addr addr)
{
    sprintf(_mac_buf, "%02X:%02X:%02X:%02X:%02X:%02X", addr.addr[0], addr.addr[1], addr.addr[2],
            addr.addr[3], addr.addr[4], addr.addr[5]);
    return _mac_buf;
}

static void _do_nothing(void* arg){
    (void)arg;
}

errval_t network_handler_init(enum pi_platform platform)
{
    errval_t err;

    memset(&ns, 0, sizeof(ns));
    ns.next_ip_id    = 1;
    ns.next_seqno_id = 1;

    // initialize the doubly linked lists
    ns.arp_list.next  = &ns.arp_list;
    ns.arp_list.prev  = &ns.arp_list;
    ns.ping_list.next = &ns.ping_list;
    ns.ping_list.prev = &ns.ping_list;
    // this ip must never be used
    ns.arp_list.ip  = 0;
    ns.ping_list.ip = 0;

    // make a buffer of 512 bytes to send strings
    ns.io_send_buf = malloc(512);
    ns.io_send_size = 512;

    collections_hash_create(&ns.ip_to_mac, free);
    collections_hash_create(&ns.port_to_pid, _do_nothing);

    char cmdline[20];
    strcpy(cmdline, "network ");
    switch (platform)
    {
    case PI_PLATFORM_QEMU:
        strcat(cmdline, "qemu");
        break;

    case PI_PLATFORM_IMX8X:
        strcat(cmdline, "imx8x");
        break;
    
    default:
        return ERR_INVALID_ARGS;
    }

    err = proc_mgmt_spawn_with_cmdline(cmdline, 0, &ns.network_pid);
    if (err_is_fail(err))
        DEBUG_ERR(err, "spawning network failed. Continuing.\n");

    return SYS_ERR_OK;
}

static void _request_with_timeout_insert(struct request_with_timeout *req,
                                         struct request_with_timeout *list,
                                         struct event_closure closure, uint32_t timeout_ms)
{
    // insert in the doubly linked list
    req->prev        = list;
    req->next        = list->next;
    list->next->prev = req;
    list->next       = req;
    // initialize the timemout
    deferred_event_register(&req->event, get_default_waitset(), timeout_ms * 1000, closure);
}

// add (or replace) the pair [ip, mac] in the cache
static void _insert_mac_ip_cache(uint32_t ip, const struct eth_addr mac)
{
    if (collections_hash_find(ns.ip_to_mac, ip) == NULL)
        debug_printf("Inserting MAC %s for IP %s in cache\n", _format_mac(mac), _format_ip(ip));

    struct eth_addr *mac_addr = malloc(sizeof(struct eth_addr));
    *mac_addr                 = mac;
    collections_hash_insert(ns.ip_to_mac, ip, mac_addr);
}

errval_t network_rpc_init(struct simple_async_channel *async, uint8_t mac[6])
{
    ns.async = async;
    memcpy(ns.mac.addr, mac, 6);

    _insert_mac_ip_cache(self_ip, ns.mac);

    return SYS_ERR_OK;
}

static void _make_ETH_header(void *packet, const struct eth_addr dest_mac, uint16_t protocol)
{
    struct eth_hdr *eth_header = (struct eth_hdr *)packet;
    eth_header->dst            = dest_mac;
    eth_header->src            = ns.mac;
    eth_header->type           = htons(protocol);
}

static void _make_ARP_header(void *packet, const struct eth_addr dest_mac, const uint32_t dest_ip,
                             uint16_t opcode)
{
    struct arp_hdr *arp_header = (struct arp_hdr *)packet;
    *arp_header                = (struct arp_hdr) { .hwtype   = htons(ARP_HW_TYPE_ETH),
                                                    .proto    = htons(ETH_TYPE_IP),
                                                    .hwlen    = sizeof(struct eth_addr),
                                                    .protolen = sizeof(uint32_t),
                                                    .opcode   = htons(opcode),
                                                    .eth_src  = ns.mac,
                                                    .ip_src   = self_ip,
                                                    .eth_dst  = dest_mac,
                                                    .ip_dst   = dest_ip };
}

static void _make_IP_header(void *packet, uint32_t dst_ip, uint16_t packet_size, uint8_t proto)
{
    struct ip_hdr *ip_header = (struct ip_hdr *)packet;
    *ip_header               = (struct ip_hdr) { .h_len   = 5,
                                                 .version = 4,
                                                 .tos     = 0,
                                                 .len     = htons(packet_size),
                                                 .id      = htons(ns.next_ip_id++),
                                                 .flags   = 0,
                                                 .offset  = 0,
                                                 .ttl     = 128,  // this value is used by ping programs
                                                 .proto   = proto,
                                                 .chksum  = 0,
                                                 .src     = self_ip,
                                                 .dest    = dst_ip };
    ip_header->chksum        = inet_checksum(ip_header, sizeof(struct ip_hdr));
}

static void _make_ICMP_header(void *packet, uint16_t packet_size, char *payload, uint8_t type,
                              uint16_t id, uint16_t seqno)
{
    struct icmp_echo_hdr *icmp_header = (struct icmp_echo_hdr *)packet;
    *icmp_header                      = (struct icmp_echo_hdr) {
                             .type = type, .code = 0, .chksum = 0, .id = htons(id), .seqno = htons(seqno)
    };
    memcpy(icmp_header->payload, payload, packet_size - sizeof(*icmp_header));
    icmp_header->chksum = inet_checksum(icmp_header, packet_size);
}

static void _make_UDP_header(void *packet, uint16_t packet_size, char *payload, uint16_t src_port, uint16_t dst_port){
    struct udp_hdr *udp_header = (struct udp_hdr*)packet;
    *udp_header = (struct udp_hdr){
        .src = htons(src_port),
        .dest = htons(dst_port),
        .len = htons(packet_size),
        // checksum is optional
        .chksum = 0
    };

    memcpy(udp_header->data, payload, packet_size - sizeof(struct udp_hdr));
}

static void _network_arp_timeout(void *arg)
{
    struct request_with_timeout *req = arg;
    // remove it from the list
    req->next->prev = req->prev;
    req->prev->next = req->next;

    if(req->err)
        *req->err = NETWORK_ERR_IP_RESOLVE_TIMEOUT;
    req->resume_fn.handler(req->resume_fn.arg);
    free(req);
}

static void _network_request_timeout(void *arg)
{
    struct request_with_timeout *req = arg;
    // remove it from the list
    req->next->prev = req->prev;
    req->prev->next = req->next;

    if(req->err)
        *req->err = NETWORK_ERR_REQUEST_TIMEOUT;
    req->resume_fn.handler(req->resume_fn.arg);
    free(req);
}

static void _send_arp_request(struct request_with_timeout *req)
{
    const size_t packet_rep_size = sizeof(struct eth_hdr) + sizeof(struct arp_hdr);
    uint8_t     *packet_rep_data = malloc(packet_rep_size);
    // use empty_mac, meaning this packet is for everyone
    _make_ETH_header(packet_rep_data, empty_mac, ETH_TYPE_ARP);
    _make_ARP_header(packet_rep_data + sizeof(struct eth_hdr), empty_mac, req->ip, ARP_OP_REQ);
    simple_async_request(ns.async, packet_rep_data, packet_rep_size, async_request_free, NULL);

    _request_with_timeout_insert(req, &ns.arp_list, MKCLOSURE(_network_arp_timeout, req),
                                 NETWORK_IP_RESOLVE_TIMEOUT_MS);
}

static void _send_ping_request(struct request_with_timeout *req)
{
    // Make the reply packet (ETH + IP + ICMP echo)
    // send packet with 32 bytes
    char           payload[32];
    const uint16_t payload_size = sizeof(payload);
    req->meta2                  = ns.next_seqno_id++;
    for (int i = 0; i < payload_size; i++) {
        // generate some payload base on seqno
        payload[i] = 'a' + ((req->meta2 + i) % ('z' - 'a' + 1));
    }
    const uint16_t packet_size = payload_size + sizeof(struct icmp_echo_hdr);

    const uint16_t ip_packet_res_size    = sizeof(struct ip_hdr) + packet_size;
    const uint16_t total_packet_res_size = sizeof(struct eth_hdr) + ip_packet_res_size;
    uint8_t       *res_packet            = malloc(total_packet_res_size);
    _make_ETH_header(res_packet, req->mac, ETH_TYPE_IP);
    _make_IP_header(res_packet + sizeof(struct eth_hdr), req->ip, ip_packet_res_size, IP_PROTO_ICMP);
    _make_ICMP_header(res_packet + sizeof(struct eth_hdr) + sizeof(struct ip_hdr), packet_size,
                      payload, ICMP_ECHO, NETWORK_PING_DEVICE_ID, req->meta2);

    // set the timestamp only now
    req->timestamp = systime_now();

    simple_async_request(ns.async, res_packet, total_packet_res_size, async_request_free, NULL);

    _request_with_timeout_insert(req, &ns.ping_list, MKCLOSURE(_network_request_timeout, req),
                                 NETWORK_PING_TIMEOUT_MS);
}

static void _send_udp_request(uint32_t ip, struct eth_addr mac, uint16_t port, uint16_t src_port, uint16_t data_size, void* data){
    const uint16_t packet_size = data_size + sizeof(struct udp_hdr);
    const uint16_t ip_packet_res_size    = sizeof(struct ip_hdr) + packet_size;
    const uint16_t total_packet_res_size = sizeof(struct eth_hdr) + ip_packet_res_size;
    uint8_t       *res_packet            = malloc(total_packet_res_size);

    _make_ETH_header(res_packet, mac, ETH_TYPE_IP);
    _make_IP_header(res_packet + sizeof(struct eth_hdr), ip, ip_packet_res_size, IP_PROTO_UDP);
    _make_UDP_header(res_packet + sizeof(struct eth_hdr) + sizeof(struct ip_hdr), packet_size, data, src_port, port);
    simple_async_request(ns.async, res_packet, total_packet_res_size, async_request_free, NULL);
}

static errval_t _handle_ARP_packet(size_t packet_size, uint8_t *packet)
{
    if (packet_size < sizeof(struct eth_hdr) + sizeof(struct arp_hdr)) {
        debug_printf("ARP packet is not big enough\n");
        return SYS_ERR_OK;
    }
    struct eth_hdr *eth_header = (struct eth_hdr *)packet;
    struct arp_hdr *arp_header = (struct arp_hdr *)(packet + sizeof(struct eth_hdr));

    // check that this is an IPV4 ARP header
    if (ntohs(arp_header->proto) != ETH_TYPE_IP)
        return SYS_ERR_OK;

    // it is is a declaration ARP packet, ignore it
    if(arp_header->ip_src == 0)
        return SYS_ERR_OK;

    // first, add the ip address of the sender in the cache
    _insert_mac_ip_cache(arp_header->ip_src, arp_header->eth_src);

    switch (ntohs(arp_header->opcode)) {
    case ARP_OP_REQ: {
        if (arp_header->ip_dst == self_ip) {
            // respond to the request
            const size_t packet_rep_size = sizeof(struct eth_hdr) + sizeof(struct arp_hdr);
            uint8_t     *packet_rep_data = malloc(packet_rep_size);
            _make_ETH_header(packet_rep_data, arp_header->eth_src, ETH_TYPE_ARP);
            _make_ARP_header(packet_rep_data + sizeof(struct eth_hdr), arp_header->eth_src,
                             arp_header->ip_src, ARP_OP_REP);
            simple_async_request(ns.async, packet_rep_data, packet_rep_size, async_request_free,
                                 NULL);
        }
        break;
    }

    case ARP_OP_REP: {
        if (memcmp(&eth_header->dst, &ns.mac, 6) == 0) {
            // look through all the arp requests, if one match remove it and keep processing
            struct request_with_timeout *req = ns.arp_list.next;
            while (req->ip != 0) {
                if (req->ip != arp_header->ip_src) {
                    req = req->next;
                    continue;
                }
                req->mac = arp_header->eth_src;

                // found one
                struct request_with_timeout *curr_req = req;
                req                                   = req->next;
                // remove it from the list
                req->prev            = curr_req->prev;
                curr_req->prev->next = req;

                // cancel the event
                deferred_event_cancel(&curr_req->event);

                // got to the next step
                if(curr_req->type == REQ_UDP){
                    _send_udp_request(curr_req->ip, curr_req->mac, (uint16_t)curr_req->meta2, curr_req->meta2 >> 16, curr_req->data_size, curr_req->data);
                    curr_req->resume_fn.handler(curr_req->resume_fn.arg);
                    free(curr_req);
                } else if(curr_req->type == REQ_PING){
                    _send_ping_request(curr_req);
                }
            }
        }
        break;
    }
    default: {
        debug_printf("Unknown ARP opcode %d\n", ntohs(arp_header->opcode));
    }
    }
    return SYS_ERR_OK;
}

static errval_t _handle_ICMP_packet(size_t packet_size, uint8_t *packet, struct eth_addr src_mac,
                                    uint32_t src_ip)
{
    struct icmp_echo_hdr *icmp_header = (struct icmp_echo_hdr *)packet;
    if (packet_size < sizeof(struct icmp_echo_hdr))
        return SYS_ERR_OK;

    if (icmp_header->code != 0) {
        ICMP_DEBUG("Unknown ICMP code %d\n", icmp_header->code);
        return SYS_ERR_OK;
    }

    if (inet_checksum(packet, packet_size) != 0) {
        IP_DEBUG("Packet checksum %x is not null\n", inet_checksum(packet, packet_size));
    }

    if (icmp_header->type == ICMP_ECHO) {
        ICMP_DEBUG("Got ICMP echo request from %s\n", _format_ip(src_ip));

        // Make the reply packet (ETH + IP + ICMP echo reply)
        const uint16_t ip_packet_res_size    = sizeof(struct ip_hdr) + packet_size;
        const uint16_t total_packet_res_size = sizeof(struct eth_hdr) + ip_packet_res_size;
        uint8_t       *res_packet            = malloc(total_packet_res_size);
        _make_ETH_header(res_packet, src_mac, ETH_TYPE_IP);
        _make_IP_header(res_packet + sizeof(struct eth_hdr), src_ip, ip_packet_res_size,
                        IP_PROTO_ICMP);
        _make_ICMP_header(res_packet + sizeof(struct eth_hdr) + sizeof(struct ip_hdr), packet_size,
                          icmp_header->payload, ICMP_ER, ntohs(icmp_header->id),
                          ntohs(icmp_header->seqno));
        simple_async_request(ns.async, res_packet, total_packet_res_size, async_request_free, NULL);
    } else if (icmp_header->type == ICMP_ER) {
        ICMP_DEBUG("Got echo response from %s\n", _format_ip(src_ip));

        struct request_with_timeout *req = ns.ping_list.next;
        while (req->ip != 0) {
            if (req->ip != src_ip || htons(icmp_header->seqno) != req->meta2) {
                req = req->next;
                continue;
            }

            // found the ping request
            // remove it from the list
            struct request_with_timeout *curr_req = req;
            req                                   = req->next;
            req->prev                             = curr_req->prev;
            req->prev->next                       = req;
            deferred_event_cancel(&curr_req->event);

            // check the content
            const int payload_size = packet_size - sizeof(struct icmp_echo_hdr);
            bool      is_valid     = (payload_size == 32);
            for (int i = 0; is_valid && i < payload_size; i++) {
                const char expected = 'a' + ((curr_req->meta2 + i) % ('z' - 'a' + 1));
                if (icmp_header->payload[i] != expected){
                    is_valid = false;
                }
            }
            if(curr_req->err)
                *curr_req->err               = is_valid ? SYS_ERR_OK : NETWORK_ERR_INVALID_PACKET;
            systime_t time_diff     = systime_now() - curr_req->timestamp;
            *(uint32_t *)curr_req->meta1 = systime_to_us(time_diff) / 1000;
            curr_req->resume_fn.handler(curr_req->resume_fn.arg);
            free(curr_req);
        }
    } else {
        ICMP_DEBUG("Unknown ICMP type %d\n", icmp_header->type);
        return SYS_ERR_OK;
    }

    return SYS_ERR_OK;
}

static void _handle_async_free(struct request *req, void *data, size_t size,
                                          struct capref *capv, size_t capc)
{
    (void)data;
    (void)size;
    (void)capv;
    (void)capc;
    free(req->meta);
}

static void _handle_simple_async_free(struct simple_request *req, void *data, size_t size)
{
    (void)data;
    (void)size;
    free(req->meta);
}

static void _network_io_refill_putchar(uint16_t bufsize, void* buf);

static errval_t _handle_UDP_packet(size_t packet_size, uint8_t *packet, struct eth_addr src_mac,
                                    uint32_t src_ip){
    (void)src_mac;
    struct udp_hdr *udp_header = (struct udp_hdr*)packet;

    if (packet_size < sizeof(struct udp_hdr))
        return SYS_ERR_OK;

    if(src_ip == ns.io_ip && ntohs(udp_header->src) == ns.io_target_port && ntohs(udp_header->dest) == ns.io_host_port){
        _network_io_refill_putchar(packet_size - sizeof(struct udp_hdr), udp_header->data);
        return SYS_ERR_OK;
    }

    uint64_t key = ntohs(udp_header->dest) * 2ULL;
    void* hash_result = collections_hash_find(ns.port_to_pid, key);
    if (hash_result == NULL)
        return SYS_ERR_OK;

    domainid_t pid = (domainid_t)(uint64_t)hash_result;
    size_t payload_size = packet_size - sizeof(struct udp_hdr);
    size_t req_size = sizeof(struct aos_network_send_request) + payload_size + 1;
    struct aos_network_send_request* req = malloc(req_size);
    *req = (struct aos_network_send_request){
        .base = {
            .base = {.type = AOS_RPC_REQUEST_TYPE_NETWORK },
            .type = AOS_RPC_NETWORK_REQUEST_SEND
        },
        .pid = pid,
        .is_tcp = false,
        .target_ip = src_ip,
        .target_port = ntohs(udp_header->src),
        .host_port = ntohs(udp_header->dest),
        .data_size = payload_size,
    };
    memcpy(req->data, udp_header->data, payload_size);
    // zero-end the payload
    req->data[payload_size] = 0;
    if((pid & 3) != disp_get_core_id()){
        // cross core
        async_request(get_cross_core_channel(), req, req_size, NULL, 0, _handle_async_free, req);
    } else {
        // same core
        struct simple_async_channel* async;
        errval_t err = proc_mgmt_get_async(pid, &async);
        if(err_is_fail(err))
            return SYS_ERR_OK;

        simple_async_request(async, req, req_size, _handle_simple_async_free, req);
    }
    return SYS_ERR_OK;
}

static errval_t _handle_IP_packet(size_t packet_size, uint8_t *packet, struct eth_addr src_mac)
{
    struct ip_hdr *ip_header = (struct ip_hdr *)packet;
    if (packet_size < sizeof(struct ip_hdr))
        return SYS_ERR_OK;

    if (ip_header->version != 4) {
        IP_DEBUG("Received packet with header %d\n", (int)ip_header->version);
        return SYS_ERR_OK;
    }

    if (ip_header->h_len != 5) {
        IP_DEBUG("Unsupported options field in header\n");
        return SYS_ERR_OK;
    }

    if (ip_header->dest != self_ip) {
        IP_DEBUG("Received IP packet with right MAC but wrong IP %s\n", _format_ip(ip_header->dest));
        return SYS_ERR_OK;
    }

    if (htons(ip_header->len) > packet_size) {
        IP_DEBUG("Packet size is %d, header says it is %d\n", (int)packet_size,
                 (int)htons(ip_header->len));
        return SYS_ERR_OK;
    }
    packet_size = htons(ip_header->len);

    if ((ip_header->flags & IP_MF) || ip_header->offset != 0) {
        IP_DEBUG("Packet is fragmented, dropping\n");
        return SYS_ERR_OK;
    }

    if (inet_checksum(packet, sizeof(struct ip_hdr)) != 0) {
        IP_DEBUG("Packet checksum %x is not null\n", inet_checksum(packet, sizeof(struct ip_hdr)));
        return SYS_ERR_OK;
    }

    _insert_mac_ip_cache(ip_header->src, src_mac);

    switch (ip_header->proto) {
    case IP_PROTO_ICMP:
        return _handle_ICMP_packet(packet_size - sizeof(struct ip_hdr),
                                   packet + sizeof(struct ip_hdr), src_mac, ip_header->src);

    case IP_PROTO_UDP:
        return _handle_UDP_packet(packet_size - sizeof(struct ip_hdr),
                                   packet + sizeof(struct ip_hdr), src_mac, ip_header->src);

    default:
        IP_DEBUG("Unknown IP protocol %d\n", (int)ip_header->proto);
        break;
    }
    return SYS_ERR_OK;
}

errval_t network_receive_packet(size_t packet_size, uint8_t *packet)
{
    if (packet_size < sizeof(struct eth_hdr))
        return SYS_ERR_OK;

    struct eth_hdr *eth_header = (struct eth_hdr *)packet;
    switch (ntohs(eth_header->type)) {
    case ETH_TYPE_ARP:
        return _handle_ARP_packet(packet_size, packet);

    case ETH_TYPE_IP:
        if (memcmp(&eth_header->dst, &ns.mac, 6) == 0) {
            return _handle_IP_packet(packet_size - sizeof(struct eth_hdr),
                                     packet + sizeof(struct eth_hdr), eth_header->src);
        }
        break;

    default:
        if (memcmp(&eth_header->dst, &ns.mac, 6) == 0)
            debug_printf("Got %X req\n", ntohs(eth_header->type));
    }
    return SYS_ERR_OK;
}

errval_t network_ping(uint32_t target_ip, errval_t *ret_err, uint32_t *ping_ms,
                      struct event_closure resume_fn)
{
    struct request_with_timeout *req = malloc(sizeof(struct request_with_timeout));
    *req                             = (struct request_with_timeout) {
                                    .type      = REQ_PING,
                                    .ip        = target_ip,
                                    .resume_fn = resume_fn,
                                    .err       = ret_err,
                                    .meta1     = ping_ms,
    };
    deferred_event_init(&req->event);

    struct eth_addr *target_mac = collections_hash_find(ns.ip_to_mac, target_ip);
    if (target_mac == NULL) {
        // we need to send an ARP request to get the MAC
        _send_arp_request(req);
    } else {
        req->mac = *target_mac;
        _send_ping_request(req);
    }

    return SYS_ERR_OK;
}

errval_t network_send_packet(uint32_t target_ip, uint16_t target_port, uint16_t src_port, bool is_tcp, uint16_t data_size, void* data, errval_t* ret_err, struct event_closure resume_fn){
    assert(!is_tcp);
    struct eth_addr *target_mac = collections_hash_find(ns.ip_to_mac, target_ip);
    if(ret_err)
        *ret_err = SYS_ERR_OK;
    if(target_mac != NULL){
        _send_udp_request(target_ip, *target_mac, target_port, src_port, data_size, data);
        resume_fn.handler(resume_fn.arg);
        return SYS_ERR_OK;
    }

    // put target port in the low bytes, src port in the hight part
    uint32_t meta2 = target_port | (uint32_t)(src_port << 16);
    struct request_with_timeout *req = malloc(sizeof(struct request_with_timeout));
    *req                             = (struct request_with_timeout) {
                                    .type      = REQ_UDP,
                                    .ip        = target_ip,
                                    .resume_fn = resume_fn,
                                    .err       = ret_err,
                                    .meta2     = meta2,
                                    .data_size = data_size,
                                    .data = data,
    };
    deferred_event_init(&req->event);
    _send_arp_request(req);

    return SYS_ERR_OK;
}

errval_t network_register_listen(uint16_t port, bool is_tcp, domainid_t pid){
    uint64_t key = port * 2ULL + is_tcp;
    if (collections_hash_find(ns.port_to_pid, key) != NULL)
        return NETWORK_ERR_PORT_ALREADY_USED;

    collections_hash_insert(ns.port_to_pid, key, (void*)(uint64_t)pid);

    return SYS_ERR_OK;
}

bool network_is_using_network_io(void){
    return ns.using_network_io;
}

void network_set_using_network_io(bool set, uint32_t ip, bool is_tcp, uint16_t target_port, uint16_t host_port){
    ns.using_network_io = set;
    ns.io_ip = ip;
    ns.io_tcp = is_tcp;
    ns.io_host_port = host_port;
    ns.io_target_port = target_port;
}

static void _empty_func(void* arg){
    (void)arg;
}

errval_t network_io_putchar(char c){
    ns.io_send_buf[ns.io_send_pos++] = c;
    if(c == '\n' || c == '\r' || ns.io_send_pos == ns.io_send_size){
        // send the command
        errval_t err = network_send_packet(ns.io_ip, ns.io_target_port, ns.io_host_port, ns.io_tcp, ns.io_send_pos, ns.io_send_buf, NULL, MKCLOSURE(_empty_func, NULL));
        ns.io_send_pos = 0;
        return err;
    }

    return SYS_ERR_OK;
}

errval_t network_io_putstring(char* str, size_t len, size_t* retlen){
    if(ns.io_send_pos > 0){
        // send the command
        errval_t err = network_send_packet(ns.io_ip, ns.io_target_port, ns.io_host_port, ns.io_tcp, ns.io_send_pos, ns.io_send_buf, NULL, MKCLOSURE(_empty_func, NULL));
        ns.io_send_pos = 0;
        if(err_is_fail(err))
            return err;
    }

    *retlen = MIN(len, 2000);

    errval_t err = network_send_packet(ns.io_ip, ns.io_target_port, ns.io_host_port, ns.io_tcp, *retlen, str, NULL, MKCLOSURE(_empty_func, NULL));
    return err;
}

errval_t network_io_getchar_register_wait(size_t len, struct event_closure resume_fn, size_t *retlen,
                                      char *buf){
    if(ns.io_recv_pos < ns.io_recv_size){
        size_t retsize = MIN(len, ns.io_recv_size - ns.io_recv_pos);
        memcpy(buf, ns.io_recv_buf + ns.io_recv_pos, retsize);
        if(retlen)
            *retlen = retsize;
        ns.io_recv_pos += retsize;

        if(ns.io_recv_pos == ns.io_recv_size){
            free(ns.io_recv_buf);
            ns.io_recv_buf = NULL;
            ns.io_recv_pos = 0;
            ns.io_recv_size = 0;
        }
        
        resume_fn.handler(resume_fn.arg);
        return SYS_ERR_OK;
    }

    struct network_io_waiting_getchar* item = malloc(sizeof(struct network_io_waiting_getchar));
    *item = (struct network_io_waiting_getchar){
        .next = NULL,
        .resume_fn = resume_fn,
        .len = len,
        .ret_len = retlen,
        .buf = buf
    };
    // add it at the end
    if(ns.io_getchar_waiting == NULL){
        ns.io_getchar_waiting = item;
    } else {
        struct network_io_waiting_getchar* last_item = ns.io_getchar_waiting;
        while(last_item->next != NULL)
            last_item = last_item->next;
        last_item->next = item;
    }

    return SYS_ERR_OK;
}

static void _network_io_refill_putchar(uint16_t bufsize, void* buf){
    if(ns.io_recv_buf != NULL)
        // drop the packet
        return;

    ns.io_recv_buf = malloc(bufsize);
    memcpy(ns.io_recv_buf, buf, bufsize);
    ns.io_recv_buf[bufsize-1] = 13;
    ns.io_recv_pos = 0;
    ns.io_recv_size = bufsize;

    while(ns.io_recv_buf != NULL && ns.io_getchar_waiting != NULL){
        errval_t err = network_io_getchar_register_wait(ns.io_getchar_waiting->len, ns.io_getchar_waiting->resume_fn, ns.io_getchar_waiting->ret_len, ns.io_getchar_waiting->buf);
        if(err_is_fail(err))
            return;
        struct network_io_waiting_getchar* old = ns.io_getchar_waiting;
        ns.io_getchar_waiting = old->next;
        free(old);
    }
}
