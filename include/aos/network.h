/**
 * @file
 * @brief Interface for networking
 *
 * Network interface. Can be used to communicate with other devices on the same network
 */

#ifndef LIB_NETWORK_H_
#define LIB_NETWORK_H_

#include <errors/errno.h>
#include <stdint.h>

typedef void (*network_listener)(uint32_t src_ip, uint16_t src_port, uint16_t data_size, void* data, void* meta);
enum server_protocol {
    SERVER_PROTOCOL_UDP
};

/**
 * @brief Allows the current process to do network commands
 *
 * @return SYS_ERR_OK on sucess, error value on failure
 */
errval_t network_init(void);

/**
 * @brief Ping the target ip
 * @param[in]  target_ip  the ip to ping
 * @param[out] ping_ms   returned time (in milliseconds) taken to get a response
 *
 * @return SYS_ERR_OK on sucess, error value on failure
 */
errval_t ping(uint32_t target_ip, uint32_t* ping_ms);

/**
 * @brief Start listening on a specific port
 * @param[in]  target_ip  the ip to ping
 * @param[out] ping_ms   returned time (in milliseconds) taken to get a response
 *
 * @return SYS_ERR_OK on sucess, error value on failure
 */
errval_t network_listen(uint16_t port, enum server_protocol protocol, network_listener listener, void* meta);

/**
 * @brief Send a tcp/udp request
 *
 * @return SYS_ERR_OK on sucess, error value on failure
 */
errval_t network_send(uint32_t ip, uint16_t port, enum server_protocol protocol, uint16_t src_port, uint16_t data_size, void* data);

/**
 * @brief Set the network io
 *
 * @return SYS_ERR_OK on sucess, error value on failure
 */
errval_t network_set_io(bool is_network, bool is_tcp, uint32_t ip, uint16_t dest_port, uint16_t src_port);

#endif
