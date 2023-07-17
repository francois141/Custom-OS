//
//  virtio_net_device.h
//  Controller
//
//  Created by FireWolf on 10/21/22.
//

#ifndef virtio_net_device_h
#define virtio_net_device_h

#include <stdio.h>
#include <aos/aos.h>
#include <maps/qemu_map.h>
#include <dev/virtio/virtio_mmio_dev.h>
#include <dev/virtio/virtio_net_dev.h>
#include <devif/queue_interface_backend.h>
#include <netutil/etharp.h>
#include "virtio_net_queue.h"

//
// MARK: - Debugging Macros
//

#define pinfo(fmt, ...) \
{                       \
    debug_printf("%s() DInfo: " fmt "\n", __func__, ##__VA_ARGS__); \
};

static inline void pbufrow(const void* buffer, size_t length)
{
    for (size_t index = 0; index < length; index += 1)
    {
        printf("%02X", ((const uint8_t*) buffer)[index]);
    }

    printf("\n");
}

static inline void pbuf(const void* buffer, size_t length, size_t column)
{
    uint8_t* ptr = (uint8_t*) buffer;

    size_t nrows = length / column;

    for (size_t row = 0; row < nrows; row += 1)
    {
        printf("[%04lu] ", row * column);

        pbufrow(ptr, column);

        ptr += column;
    }

    if (nrows * column < length)
    {
        printf("[%04lu] ", nrows * column);

        pbufrow(ptr, length - nrows * column);
    }
}

//
// MARK: - Section Specific Macros
//

// Uncomment this line to turn on debugging output of this module
// #define VNET_DEBUG

#ifdef VNET_DEBUG
#define pinfovn pinfo
#define pbufvn pbuf
#else
#define pinfovn(fmt, ...) {}
#define pbufvn(buffer, length, column) {}
#endif

//
// MARK: - Virtual Network Device
//

/// Represents a virtio network device
struct vnet_device;

/// Represents a user-specified device configuration
struct vnet_device_config
{
    ///
    /// Specify the size of the TX queue
    ///
    /// @note The size must be a power of 2 and must not exceed the maximum size.
    /// @note Specify a size of 0 to use the maximum size.
    ///
    size_t tx_queue_size;

    ///
    /// Specify the size of the RX queue
    ///
    /// @note The size must be a power of 2 and must not exceed the maximum size.
    /// @note Specify a size of 0 to use the maximum size.
    ///
    size_t rx_queue_size;
};

//
// MARK: - Virtual Network Device: Query Device Properties
//

///
/// Get the underlying virtio device handle
///
/// @param self A non-null device handle
/// @return A non-null virtio device handle.
///
virtio_mmio_t* vnet_device_get_virtio_mmio_handle(struct vnet_device* self);

///
/// Get the transmit queue
///
/// @param self A non-null device handle
/// @return A non-null transmit queue exposed by the virtual network device.
///
struct devq* vnet_device_get_tx_queue(struct vnet_device* self);

///
/// Get the receive queue
///
/// @param self A non-null device handle
/// @return A non-null receive queue exposed by the virtual network device.
///
struct devq* vnet_device_get_rx_queue(struct vnet_device* self);

///
/// Get the size of the transmit queue
///
/// @param self A non-null device handle
/// @return The number of entries that the queue can hold.
/// @note The caller may use the returned value to decide how much memory it needs to allocate for each packet queue.
/// @seealso `devq_register()` and `devq_enqueue()`.
///
size_t vnet_device_get_tx_queue_size(struct vnet_device* self);

///
/// Get the size of the receive queue
///
/// @param self A non-null device handle
/// @return The number of entries that the queue can hold.
/// @note The caller may use the returned value to decide how much memory it needs to allocate for each packet queue.
/// @seealso `devq_register()` and `devq_enqueue()`.
///
size_t vnet_device_get_rx_queue_size(struct vnet_device* self);

///
/// Get the MAC address of this virtual network device
///
/// @param self A non-null device handle
/// @param mac A non-null pointer to the MAC address to be filled
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
errval_t vnet_device_get_mac_address(struct vnet_device* self, struct eth_addr* mac);

//
// MARK: - Virtual Network Device: Hardware Initialization
//

///
/// Create a new virtual network device instance
///
/// @return A non-null device handle on success, `NULL` otherwise.
///
struct vnet_device* vnet_device_create(void);

///
/// Initialize the virtio network device instance
///
/// @param self A non-null device handle
/// @param config An optional user-specified device configuration
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This function maps the device frame and initializes the Mackerel binding and all member fields.
///
errval_t vnet_device_init(struct vnet_device* self, struct vnet_device_config* config);

///
/// Probe the virtio network device
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This function conforms to Section 4.2.3.1 MMIO-specific initialization sequence.
///
errval_t vnet_device_probe(struct vnet_device* self);

///
/// Start the virtio network device
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This function conforms to Section 3.1.1 Driver Requirements: Device Initialization.
///
errval_t vnet_device_start(struct vnet_device* self);

///
/// Stop the virtio network device and release all allocated resources
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
errval_t vnet_device_stop(struct vnet_device* self);

///
/// Finalize the virtio network device instance
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
errval_t vnet_device_fini(struct vnet_device* self);

///
/// Destroy the given virtual network device instance
///
/// @param self A non-null device handle returned by `vnet_device_create()`.
///
void vnet_device_destroy(struct vnet_device* self);

#endif /* virtio_net_device_h */
