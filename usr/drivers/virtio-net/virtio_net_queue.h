//
//  virtio_net_queue.h
//  Controller
//
//  Created by FireWolf on 10/21/22.
//

#ifndef virtio_net_queue_h
#define virtio_net_queue_h

#include <aos/aos.h>
#include <devif/backends/net/virtio_devif.h>

//
// MARK: - Virtual Network Queue
//

/// Forward Declaration
struct vnet_device;

///
/// Represents a queue exposed by the virtio network device
///
/// @note Abstract queues can be considered as the interface of the virtio network device.
/// @note This class implements Barrelfish's queue interface `devif/queue_interface_backend.h`.
///
struct vnet_queue;

//
// MARK: - Virtual Network Queue: Query Queue Properties
//

///
/// Get the size of this virtual network queue
///
/// @param self A non-null queue instance
/// @return The number of entries that this queue can hold.
///
size_t vnet_queue_get_size(struct vnet_queue* self);

//
// MARK: - Virtual Network Queue: Create Network Queues
//

///
/// Create a virtual network queue used for transmitting network packets with the given size
///
/// @param instance A non-null pointer to a newly created instance on return
/// @param device A non-null virtio network device instance as the provider
/// @param index The index of the backend virtual queue
/// @param size The number of entries in the queue
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @warning The caller is responsible for releasing the returned pointer.
/// @note The caller must ensure that
///       1) The given index is valid;
///       2) The virtual queue at the given index is inactive;
///       3) The given queue size is a power of 2 and does not exceed the maximum allowed size.
/// @note This function conforms to Section 4.2.3.2 Virtqueue Configuration.
///
errval_t vnet_queue_create_tx_queue_with_size(struct vnet_queue** instance, struct vnet_device* device, size_t index, size_t size);

///
/// Create a virtual network queue used for receiving network packets with the given size
///
/// @param instance A non-null pointer to a newly created instance on return
/// @param device A non-null virtio network device instance as the provider
/// @param index The index of the backend virtual queue
/// @param size The number of entries in the queue
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @warning The caller is responsible for releasing the returned pointer.
/// @note The caller must ensure that
///       1) The given index is valid;
///       2) The virtual queue at the given index is inactive;
///       3) The given queue size is a power of 2 and does not exceed the maximum allowed size.
/// @note This function conforms to Section 4.2.3.2 Virtqueue Configuration.
///
errval_t vnet_queue_create_rx_queue_with_size(struct vnet_queue** instance, struct vnet_device* device, size_t index, size_t size);

///
/// Destroy the given virtual network queue
///
/// @param self A non-null queue instance returned by `vnet_queue_create_with_size()`
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
errval_t vnet_queue_destroy(struct vnet_queue* self);

#endif /* virtio_net_queue_h */
