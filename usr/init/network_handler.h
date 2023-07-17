#ifndef _INIT_NETWORK_HANDLER_H_
#define _INIT_NETWORK_HANDLER_H_

#define NETWORK_IP_RESOLVE_TIMEOUT_MS 5000
#define NETWORK_PING_TIMEOUT_MS 2000

// we always use the same device id, but the sequence number is different for pings
#define NETWORK_PING_DEVICE_ID 0xBA1E

#include <aos/aos.h>

struct simple_async_channel;

// to be called from main
errval_t network_handler_init(enum pi_platform platform);
// to be called by the network using rpc
errval_t network_rpc_init(struct simple_async_channel* async, uint8_t mac[6]);

errval_t network_receive_packet(size_t packet_size, uint8_t* packet);
errval_t network_ping(uint32_t target_ip, errval_t* ret_err, uint32_t* ping_ms, struct event_closure resume_fn);
errval_t network_send_packet(uint32_t target_ip, uint16_t target_port, uint16_t src_port, bool is_tcp, uint16_t data_size, void* data, errval_t* ret_err, struct event_closure resume_fn);
errval_t network_register_listen(uint16_t port, bool is_tcp, domainid_t pid);

// Commands for network io
bool network_is_using_network_io(void);
void network_set_using_network_io(bool set, uint32_t ip, bool is_tcp, uint16_t target_port, uint16_t host_port);

errval_t network_io_putchar(char c);
errval_t network_io_putstring(char* str, size_t len, size_t* retlen);
errval_t network_io_getchar_register_wait(size_t len, struct event_closure resume_fn, size_t *retlen,
                                      char *buf);

#endif