//
//  virtio_net_device.c
//  Controller
//
//  Created by FireWolf on 10/21/22.
//

#include "virtio_net_device.h"

//
// MARK: - Virtual Network Device: Constants
//

enum
{
    /// The index of the first virtual queue supported by this driver
    VIRTQ_INDEX_INIT = 0,

    /// Section 5.1.2: The index of the virtual queue for receiving network packets
    VIRTQ_RX_INDEX = 0,

    /// Section 5.1.2: The index of the virtual queue for sending network packets
    VIRTQ_TX_INDEX = 1,

    /// The total number of virtual queues supported by this driver
    VIRTQ_COUNT = 2,
};

//
// MARK: - Virtual Network Device: Member Fields
//

/// Represents a virtio network device
struct vnet_device
{
    /// The underlying virtio device handle
    virtio_mmio_t device;

    /// The virtual address at which the device registers are mapped
    lvaddr_t vaddr;

    /// All supported virtual network queues, `nullptr` if not initialized
    struct vnet_queue* queues[VIRTQ_COUNT];

    /// User-specified device configuration
    struct vnet_device_config config;
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
virtio_mmio_t* vnet_device_get_virtio_mmio_handle(struct vnet_device* self)
{
    return &self->device;
}

///
/// Get the transmit queue
///
/// @param self A non-null device handle
/// @return A non-null transmit queue exposed by the virtual network device.
///
struct devq* vnet_device_get_tx_queue(struct vnet_device* self)
{
    return (struct devq*) self->queues[VIRTQ_TX_INDEX];
}

///
/// Get the receive queue
///
/// @param self A non-null device handle
/// @return A non-null receive queue exposed by the virtual network device.
///
struct devq* vnet_device_get_rx_queue(struct vnet_device* self)
{
    return (struct devq*) self->queues[VIRTQ_RX_INDEX];
}

///
/// Get the size of the queue at the given index
///
/// @param self A non-null device handle
/// @param index The index of the queue
/// @return The number of entries that the queue at the given index can hold.
/// @warning The caller must ensure that the given index is valid.
///
static size_t vnet_device_get_queue_size(struct vnet_device* self, size_t index)
{
    return vnet_queue_get_size(self->queues[index]);
}

///
/// Get the size of the transmit queue
///
/// @param self A non-null device handle
/// @return The number of entries that the queue can hold.
/// @note The caller may use the returned value to decide how much memory it needs to allocate for each packet queue.
/// @seealso `devq_register()` and `devq_enqueue()`.
///
size_t vnet_device_get_tx_queue_size(struct vnet_device* self)
{
    return vnet_device_get_queue_size(self, VIRTQ_TX_INDEX);
}

///
/// Get the size of the receive queue
///
/// @param self A non-null device handle
/// @return The number of entries that the queue can hold.
/// @note The caller may use the returned value to decide how much memory it needs to allocate for each packet queue.
/// @seealso `devq_register()` and `devq_enqueue()`.
///
size_t vnet_device_get_rx_queue_size(struct vnet_device* self)
{
    return vnet_device_get_queue_size(self, VIRTQ_RX_INDEX);
}

///
/// Get the MAC address of this virtual network device
///
/// @param self A non-null device handle
/// @param mac A non-null pointer to the MAC address to be filled
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
errval_t vnet_device_get_mac_address(struct vnet_device* self, struct eth_addr* mac)
{
    for (int index = 0; index < 6; index += 1)
    {
        mac->addr[5 - index] = mackerel_read_addr_8(self->device.base, 0x100 + index);
    }

    return SYS_ERR_OK;
}

///
/// Get the user specified size of the receive queue
///
/// @param self A non-null device handle
/// @return The desired size of the receive queue.
///
static size_t vnet_device_get_user_specified_rx_queue_size(struct vnet_device* self)
{
    return self->config.rx_queue_size;
}

///
/// Get the user specified size of the transmit queue
///
/// @param self A non-null device handle
/// @return The desired size of the transmit queue.
///
static size_t vnet_device_get_user_specified_tx_queue_size(struct vnet_device* self)
{
    return self->config.tx_queue_size;
}

//
// MARK: - Virtual Network Device: Hardware Initialization (Private)
//

///
/// Set up the virtio network device
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This function conforms to Section 5.1.5 Network Device: Device Initialization.
///
static errval_t vnet_device_setup(struct vnet_device* self)
{
    // Type of the function that creates a virtual network queue
    typedef errval_t (*vnet_queue_creator)(struct vnet_queue**, struct vnet_device*, size_t, size_t);

    // Type of the function that retrieves the user-specified size of a virtual network queue
    typedef size_t (*vnet_queue_get_user_size)(struct vnet_device*);

    // A table of creator functions, each of which is used to create a specific concrete virtual network queue
    static const vnet_queue_creator vnet_queue_creators[] =
    {
        [VIRTQ_RX_INDEX] = vnet_queue_create_rx_queue_with_size,
        [VIRTQ_TX_INDEX] = vnet_queue_create_tx_queue_with_size,
    };

    // A table of query functions, each of which is used to retrieve the user-specified size of a specific concrete virtual network queue
    static const vnet_queue_get_user_size vnet_queue_get_user_specified_sizes[] =
    {
        [VIRTQ_RX_INDEX] = vnet_device_get_user_specified_rx_queue_size,
        [VIRTQ_TX_INDEX] = vnet_device_get_user_specified_tx_queue_size,
    };

    errval_t error;

    // Guard: Set up each virtual data queue supported by this driver
    for (size_t index = VIRTQ_INDEX_INIT; index < VIRTQ_COUNT; index += 1)
    {
        // Get the type of the current data queue: 0: RX, 1: TX
        // index = 2n - 2: RX
        // index = 2n - 1: TX
        // index = 2n: Control (Unsupported by this driver)
        size_t type = index % 2;

        // Guard: Check whether the user has specified a queue size
        size_t queue_size = (*vnet_queue_get_user_specified_sizes[type])(self);

        if (queue_size == 0)
        {
            // Retrieve the maximum queue size from the device and use it as the queue size
            virtio_mmio_QueueSel_wr(&self->device, index);

            queue_size = virtio_mmio_QueueNumMax_rd(&self->device);
        }

        // Guard: Create the queue with the maximum supported size
        error = (*vnet_queue_creators[type])(&self->queues[index], self, index, queue_size);

        if (err_is_fail(error))
        {
            DEBUG_ERR(error, "Failed to set up the virtual queue at index %zu.", index);

            return error;
        }
    }

    return SYS_ERR_OK;
}

//
// MARK: - Virtual Network Device: Hardware Initialization (Public)
//

///
/// Create a new virtual network device instance
///
/// @return A non-null device handle on success, `NULL` otherwise.
///
struct vnet_device* vnet_device_create(void)
{
    return calloc(1, sizeof(struct vnet_device));
}

///
/// Initialize the virtio network device instance
///
/// @param self A non-null device handle
/// @param config An optional user-specified device configuration
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This function maps the device frame and initializes the Mackerel binding and all member fields.
///
errval_t vnet_device_init(struct vnet_device* self, struct vnet_device_config* config)
{
    // TODO: M7: Network Project
    // TODO: Get the capability to the device frame (i.e. the device register region)
    // TODO: Map the capability so that all device registers are accessible.
    // TODO: Initialize the member field `vaddr` to the address of the MMIO region
    // TODO: You can remove this assertion once you have finished the above tasks.
    errval_t err;

    struct capref dev_cap = { .cnode = cnode_task, .slot = TASKCN_SLOT_DEV };
    struct capability dev_frame;
    err = cap_direct_identify(dev_cap, &dev_frame);
    if (err_is_fail(err) || dev_frame.type != ObjType_DevFrame) {
        DEBUG_ERR(err, "cap_direct_identify");
        return err;
    }
    err = dev_frame_map(dev_cap, dev_frame, QEMU_VIRTIO_NIC_BASE, QEMU_VIRTIO_NIC_SIZE, (void**)&self->vaddr);
    if (err_is_fail(err)) {
        return err;
    }
        
    // Initialize the Mackerel binding
    virtio_mmio_initialize(&self->device, (void *)(self->vaddr + QEMU_VIRTIO_NIC_OFFSET));

    // Set the user-specified configuration
    if (config != NULL)
    {
        self->config = *config;
    }

    return SYS_ERR_OK;
}

///
/// Probe the virtio network device
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This function conforms to Section 4.2.3.1 MMIO-specific initialization sequence.
///
errval_t vnet_device_probe(struct vnet_device* self)
{
    if (virtio_mmio_MagicValue_rd(&self->device) != virtio_mmio_magic_value)
    {
        USER_PANIC("Unexpected magic value. The given device is not a virtio device.");
    }

    if (virtio_mmio_Version_rd(&self->device) != virtio_mmio_virtio_version_virtio10)
    {
        USER_PANIC("Mismatched virtio version. Expected 1.0 but the given device is legacy (%d).",
                   virtio_mmio_Version_rd(&self->device));
    }

    if (virtio_mmio_DeviceID_rd(&self->device) != virtio_mmio_virtio_network_card)
    {
        USER_PANIC("Mismatched device id. Expected a network card but the given device id is %d.", 
                   virtio_mmio_DeviceID_rd(&self->device));
    }

    return SYS_ERR_OK;
}

///
/// Start the virtio network device
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This function conforms to Section 3.1.1 Driver Requirements: Device Initialization.
///
errval_t vnet_device_start(struct vnet_device* self)
{
    // 1. Reset the device.
    virtio_mmio_Reset_wr(&self->device, 0x0);
    
    // 2. Set the ACKNOWLEDGE status bit: the guest OS has notice the device.
    virtio_mmio_Status_acknowledge_wrf(&self->device, 1);

    // 3. Set the DRIVER status bit: the guest OS knows how to drive the device.
    virtio_mmio_Status_driver_wrf(&self->device, 1);

    // 4. Read device feature bits, and write the subset of feature bits understood by the OS and
    // driver to the device. During this step the driver MAY read (but MUST NOT write) the
    // device-specific configuration fields to check that it can support the device before accepting it.
    // 4.1. Negotiate the first 32 features
    {
        // Read the device features
        virtio_mmio_DeviceFeaturesSel_rawwr(&self->device, 0);

        uint32_t features = virtio_mmio_DeviceFeatures_rd(&self->device);

        debug_printf("Device Features [00-31] = 0x%08x.\n", features);

        virtio_net_FeatureBits_t feature_bits = (virtio_net_FeatureBits_t)&features;

        static char buf[4096];
        virtio_net_FeatureBits_prtval(buf, sizeof(buf), feature_bits);
        printf(buf);

        uint32_t activated_features = 0;
        feature_bits = (virtio_net_FeatureBits_t)&activated_features;
        virtio_net_FeatureBits_VIRTIO_NET_F_MAC_insert(feature_bits, 1);
        virtio_net_FeatureBits_VIRTIO_NET_F_STATUS_insert(feature_bits, 1);

        // Set the driver features
        debug_printf("Driver Features [00-31] = 0x%08x.\n", activated_features);

        virtio_mmio_DriverFeaturesSel_rawwr(&self->device, 0);

        virtio_mmio_DriverFeatures_wr(&self->device, activated_features);
    }
    // 4.2. Negotiate the second 32 features
    {
        // Read the device features
        virtio_mmio_DeviceFeaturesSel_rawwr(&self->device, 1);

        uint32_t features = virtio_mmio_DeviceFeatures_rd(&self->device);

        debug_printf("Device Features [32-63] = 0x%08x.\n", features);

        // Set the driver features
        debug_printf("Driver Features [32-63] = 0x%08x.\n", features);

        virtio_mmio_DriverFeaturesSel_rawwr(&self->device, 1);

        virtio_mmio_DriverFeatures_wr(&self->device, features);
    }

    // 5. Set the FEATURES_OK status bit. The driver MUST NOT accept new feature bits after this step.
    virtio_mmio_Status_features_ok_wrf(&self->device, 1);

    // 6. Re-read device status to ensure the FEATURES_OK bit is still set: otherwise, the device
    // does not support our subset of features and the device is unusable.
    if (virtio_mmio_Status_features_ok_rdf(&self->device) != 1)
    {
        USER_PANIC("Failed to negotiate the features.");
    };

    // 7. Perform device-specific setup, including discovery of virtqueues for the device, optional
    // per-bus setup, reading and possibly writing the device’s virtio configuration space, and
    // population of virtqueues.
    errval_t error = vnet_device_setup(self);

    if (err_is_fail(error))
    {
        debug_printf("Failed to configure the virtio network device.\n");

        return error;
    }

    // 8. Set the DRIVER_OK status bit. At this point the device is “live”
    virtio_mmio_Status_driver_ok_wrf(&self->device, 1);

    debug_printf("The virtio network device has been initialized and configured.\n");

    return SYS_ERR_OK;
}

///
/// Stop the virtio network device and release all allocated resources
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
errval_t vnet_device_stop(struct vnet_device* self)
{
    // Reset the device
    virtio_mmio_Reset_wr(&self->device, 0x0);

    // Destroy all network queues
    for (size_t index = VIRTQ_INDEX_INIT; index < VIRTQ_COUNT; index += 1)
    {
        vnet_queue_destroy(self->queues[index]);

        self->queues[index] = NULL;
    }

    return SYS_ERR_OK;
}

///
/// Finalize the virtio network device instance
///
/// @param self A non-null device handle
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
errval_t vnet_device_fini(struct vnet_device* self)
{
    // Unmap the MMIO region
    errval_t error = paging_unmap(get_current_paging_state(), (const void*) self->vaddr);

    self->vaddr = 0;

    return error;
}

///
/// Destroy the given virtual network device instance
///
/// @param self A non-null device handle returned by `vnet_device_create()`.
///
void vnet_device_destroy(struct vnet_device* self)
{
    free(self);
}