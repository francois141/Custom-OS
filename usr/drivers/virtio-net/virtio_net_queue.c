//
//  virtio_net_queue.c
//  Controller
//
//  Created by FireWolf on 10/21/22.
//

#include "virtio_net_queue.h"
#include "virtio_net_device.h"
#include <devif/queue_interface_backend.h>
#include <virtio_queue.h>
#include <arch/aarch64/aos/cache.h>

//
// MARK: - Utilities
//

/// Check whether the given integer is a power of 2
static bool IsPowerOf2(uint64_t x)
{
    return (x != 0) && ((x & (x - 1)) == 0);
}

//
// MARK: - Virtual Network Queue: Member Types
//

/// Type of a function that initializes the virtual function table for a virtual network queue
typedef void (*vnet_queue_vft_initializer)(struct vnet_queue*);

/// A 12-byte header prepended to each network packet to be sent and having been received
struct virtio_net_hdr
{
#define VIRTIO_NET_HDR_F_NEEDS_CSUM 1
#define VIRTIO_NET_HDR_F_DATA_VALID 2
#define VIRTIO_NET_HDR_F_RSC_INFO 4
    uint8_t flags;
#define VIRTIO_NET_HDR_GSO_NONE 0
#define VIRTIO_NET_HDR_GSO_TCPV4 1
#define VIRTIO_NET_HDR_GSO_UDP 3
#define VIRTIO_NET_HDR_GSO_TCPV6 4
#define VIRTIO_NET_HDR_GSO_ECN 0x80
    uint8_t gso_type;
    uint16_t hdr_len;       // Little Endian
    uint16_t gso_size;      // Little Endian
    uint16_t csum_start;    // Little Endian
    uint16_t csum_offset;   // Little Endian
    uint16_t num_buffers;   // Little Endian
};

///
/// Initialize the given header for sending a fully checksummed packet
///
/// @param self A non-null virtio net header.
///
static void virtio_net_hdr_init_for_fully_checksummed_packet(struct virtio_net_hdr* self)
{
    self->flags = 0;

    self->gso_type = VIRTIO_NET_HDR_GSO_NONE;

    self->hdr_len = 0;

    self->gso_size = 0;

    self->csum_start = 0;

    self->csum_offset = 0;

    self->num_buffers = 0;
}

/// Represents a memory region that can be used by the virtual network queue
struct vnet_queue_mem_region
{
    /// A capability to the backend frame
    struct capref frame;

    /// The identifier of this memory region
    regionid_t rid;

    /// The starting physical address of the backend frame
    genpaddr_t paddr;

    /// The size of the backend frame
    size_t size;

    /// The virtual address at which the frame is mapped
    lvaddr_t vaddr;

    /// The next memory region that can be used by the virtual network queue
    struct vnet_queue_mem_region* next;
};

///
/// Create a memory region with the given frame and identifier
///
/// @param frame A capability to the backend frame
/// @param rid The identifier of the memory region
/// @return A non-null memory region on success, `NULL` otherwise.
///
static struct vnet_queue_mem_region* vnet_queue_mem_region_create(struct capref frame, regionid_t rid)
{
    // Guard: Get the physical address of the given frame
    struct frame_identity identity;

    if (err_is_fail(frame_identify(frame, &identity)))
    {
        return NULL;
    }

    // Guard: Map the given frame
    lvaddr_t vaddr = 0;

    if (err_is_fail(paging_map_frame_attr(get_current_paging_state(), (void**) &vaddr, identity.bytes, frame, VREGION_FLAGS_READ_WRITE_NOCACHE)))
    {
        return NULL;
    }

    // Guard: Allocate a new instance
    struct vnet_queue_mem_region* instance = malloc(sizeof(struct vnet_queue_mem_region));

    if (instance == NULL)
    {
        return NULL;
    }

    // Initialize the instance
    instance->frame = frame;

    instance->rid = rid;

    instance->paddr = identity.base;

    instance->size = identity.bytes;

    instance->vaddr = vaddr;

    instance->next = NULL;

    return instance;
}

///
/// Destroy the given memory region
///
/// @param self A non-null memory region
///
__attribute__((unused))
static void vnet_queue_mem_region_destroy(struct vnet_queue_mem_region* self)
{
    paging_unmap(get_current_paging_state(), (const void*) self->vaddr);

    cap_destroy(self->frame);

    bzero(self, sizeof(struct vnet_queue_mem_region));
}

///
/// Dump the content of the given memory region
///
/// @param self A non-null memory region
/// @param column The number of columns
///
__attribute__((unused))
static void vnet_queue_mem_region_dump(const struct vnet_queue_mem_region* self, size_t column)
{
    (void)self;
    (void)column;
    pinfovn("Memory Region Dump:");
    pinfovn("\t  RID = %zu", self->rid);
    pinfovn("\tPADDR = %p", self->paddr);
    pinfovn("\tVADDR = %p", self->vaddr);
    pinfovn("\t Size = %zu", self->size);
    pinfovn("\t Next = %p", self->next);
    pinfovn("\tContent:");
    pbufvn((const void*) self->vaddr, self->size, column);
}

//
// MARK: - Virtual Network Queue: Member Fields
//

///
/// Represents a queue exposed by the virtio network device
///
/// @note Abstract queues can be considered as the interface of the virtio network device.
/// @note This class implements Barrelfish's queue interface `devif/queue_interface_backend.h`.
///
struct vnet_queue
{
    /// The base class
    struct devq super;

    /// The virtio network device (provider)
    struct vnet_device* device;

    /// The backend virtual queue
    struct virtq queue;

    /// The index of the backend virtual queue (i.e. Virtio Queue Selector)
    size_t index;

    /// The size of the backend virtual queue (i.e. Virtio Queue Size)
    size_t size;

    /// A list of memory regions that can be used by this virtual network queue
    struct vnet_queue_mem_region* regions;

    ///
    /// An array of memory region infos, each of which is associated with an entry in the descriptor table
    ///
    /// @note The length of this array is identical to the size of the queue.
    ///
    struct devq_buf* descriptor_infos;

    ///
    /// The index of the last seen used descriptor reported by the device
    ///
    /// @note i.e. The descriptor at this index can be reused by the driver to send another packet
    ///
    size_t last_seen;
};

//
// MARK: - Virtual Network Queue: Query Queue Properties
//

///
/// Get the size of this virtual network queue
///
/// @param self A non-null queue instance
/// @return The number of entries that this queue can hold.
///
size_t vnet_queue_get_size(struct vnet_queue* self)
{
    return self->size;
}

//
// MARK: - Virtual Network Queue: Device Access
//

///
/// Get the virtio device handle
///
/// @param self A non-null queue instance
/// @return A non-null virtio device handle.
///
static virtio_mmio_t* vnet_queue_get_virtio_mmio_handle(struct vnet_queue* self)
{
   return vnet_device_get_virtio_mmio_handle(self->device);
}

//
// MARK: - Virtual Network Queue: Network Queue IMP
//

///
/// Get the memory region by its identifier
///
/// @param self A non-null queue instance
/// @param rid The identifier of a memory region
/// @return A non-null memory region on success, `NULL` if no region has the given identifier.
///
static const struct vnet_queue_mem_region* vnet_queue_get_region_by_id(struct vnet_queue* self, regionid_t rid)
{
    for (struct vnet_queue_mem_region* current = self->regions; current != NULL; current = current->next)
    {
        if (current->rid == rid)
        {
            return current;
        }
    }

    return NULL;
}

///
/// Add a chunk of physical memory that can be used by the hardware to manipulate the network queue
///
/// @param instance A non-null queue instance
/// @param cap A capability to a frame
/// @param rid The region identifier
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
static errval_t vnet_queue_register(struct devq* instance, struct capref frame, regionid_t rid)
{
    // Guard: Create a new memory region
    struct vnet_queue_mem_region* region = vnet_queue_mem_region_create(frame, rid);

    if (region == NULL)
    {
        return DEVQ_ERR_REGISTER_REGION;
    }

    // Add the new region
    struct vnet_queue* self = (struct vnet_queue*) instance;

    region->next = self->regions;

    self->regions = region;

    return SYS_ERR_OK;
}

///
/// Remove a chunk of physical memory that can no longer be used by the hardware for the network queue
///
/// @param instance A non-null queue instance
/// @param rid The identifier of a previously registered physical memory region
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
static errval_t vnet_queue_deregister(struct devq* instance, regionid_t rid)
{
    (void)instance;
    (void)rid;
    return LIB_ERR_NOT_IMPLEMENTED;
}

///
/// Get the amount of memory that must be reserved at the beginning of each network packet buffer
///
/// @param instance A non-null queue instance
/// @return The number of bytes that must be reserved.
///
static size_t vnet_queue_get_num_reserved_bytes(struct devq* instance)
{
    (void)instance;

    STATIC_ASSERT(sizeof(struct virtio_net_hdr) == 12, "The header size must be 12 bytes.");

    return sizeof(struct virtio_net_hdr);
}

//
// MARK: - Virtual Network Queue: Network Queue IMP (TX Queue)
//

///
/// Enqueue a buffer that represents a network packet to be sent into the transmit queue
///
/// @param instance A non-null queue instance
/// @param packet_buf A buffer descriptor that describes the packet to be sent
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This will be the new interface of `devq_enqueue()`.
///
static errval_t vnet_queue_tx_enqueue_v2(struct devq* instance, struct devq_buf packet_buf)
{
    pinfovn("TX: Invoked with packet buffer:");
    pinfovn("\t          RID = %zu", packet_buf.rid);
    pinfovn("\tBuffer Offset = %zu", packet_buf.offset);
    pinfovn("\tBuffer Length = %zu", packet_buf.length);
    pinfovn("\t  Data Offset = %zu", packet_buf.valid_data);
    pinfovn("\t  Data Length = %zu", packet_buf.valid_length);
    pinfovn("\t        Flags = %zu", packet_buf.flags);

    struct vnet_queue* self = (struct vnet_queue*) instance;

    // Guard: Retrieve the memory region that has the given identifier
    const struct vnet_queue_mem_region* region = vnet_queue_get_region_by_id(self, packet_buf.rid);

    if (region == NULL)
    {
        debug_printf("Failed to find the memory region that has the given identifier %u.\n", packet_buf.rid);

        return DEVQ_ERR_INVALID_REGION_ID;
    }

    pinfovn("TX: Found the memory region that has the given identifier:");
    pinfovn("\t  RID = %zu", region->rid);
    pinfovn("\tPADDR = %p", region->paddr);
    pinfovn("\tVADDR = %p", region->vaddr);
    pinfovn("\t Size = %zu", region->size);

    // Guard: Ensure that there are at least 12 bytes at the beginning of the buffer
    if (packet_buf.valid_data < sizeof(struct virtio_net_hdr))
    {
        debug_printf("The caller should reserve at least %zu bytes at the beginning of the buffer.\n", sizeof(struct virtio_net_hdr));

        return DEVQ_ERR_INVALID_BUFFER_ARGS;
    }

    // Prepend the virtio network header to the beginning of the buffer
    // Offset into the memory region where the virtio net header followed by the packet content starts
    size_t offset = packet_buf.offset + packet_buf.valid_data - sizeof(struct virtio_net_hdr);

    // Total length of the virtio net header plus the packet content
    size_t length = sizeof(struct virtio_net_hdr) + packet_buf.valid_length;

    // Section 5.1.6.2 Packet Transmission
    // The network stack will send a fully checksummed packet
    // Initialize the virtio net header
    virtio_net_hdr_init_for_fully_checksummed_packet((struct virtio_net_hdr*) (region->vaddr + offset));

    // Ensure that the device can see the virtio net header + the packet content
    cpu_dcache_wbinv_range(region->vaddr + offset, length);

    // Section 2.6.5 The Virtqueue Descriptor Table
    // Note that the given packet span only one descriptor entry,
    // so we can use the `index` in the available ring as the index of the next free entry in the descriptor table
    size_t index = self->queue.avail->index % self->size;

    pinfovn("TX: Index of the next available descriptor = %zu.", index);

    struct virtq_desc* descriptor = &self->queue.desc[index];

    virtq_descriptor_init(descriptor, region->paddr + offset, length, 0, 0);

    pinfovn("TX: Descriptor Entry: Address = %p; Length = %zu; Flags = %zu; Next = %zu.",
          descriptor->paddr, descriptor->length, descriptor->flags, descriptor->next);

    pinfovn("TX: Descriptor Content:");

    pbufvn((const void*) region->vaddr + offset, length, 8);

    // Ensure that the device can see the updated descriptor entry
    dmb();

    // Record the memory region information for this descriptor entry
    self->descriptor_infos[index] = packet_buf;

    // Section 2.6.6 The Virtqueue Available Ring
    // Update the available ring
    self->queue.avail->flags = VIRTQ_AVAIL_F_NO_INTERRUPT;

    self->queue.avail->ring[index] = index;

    self->queue.avail->index += 1;

    // Section 4.2.3.3 Available Buffer Notifications
    // Notify the device that there is an available descriptor
    // Note that the register value is the queue index instead of the descriptor index
    virtio_mmio_QueueNotify_wr(vnet_queue_get_virtio_mmio_handle(self), self->index);

    return SYS_ERR_OK;
}

///
/// Dequeue a buffer that can be reused to store a future network packet from the transmit queue
///
/// @param instance A non-null queue instance
/// @param packet_buf A buffer descriptor that describes a buffer which can be reused to store a future packet on return
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This will be the new interface of `devq_dequeue()`.
///
static errval_t vnet_queue_tx_dequeue_v2(struct devq* instance, struct devq_buf* packet_buf)
{
    struct vnet_queue* self = (struct vnet_queue*) instance;

    // Guard: Check whether there is a buffer that have been used by the device and thus can be recycled
    //   i.e. Check whether the Used Ring is empty
    size_t current_used_index = self->queue.used->index;

    if (self->last_seen == current_used_index)
    {
        return DEVQ_ERR_QUEUE_EMPTY;
    }

    pinfovn("TX: Last Seen = %zu; Current Used Index = %zu.", self->last_seen, current_used_index);

    // There is at least a buffer that can be recycled
    struct virtq_used_elem* element = &self->queue.used->ring[self->last_seen % self->size];

    pinfovn("TX: Used Element: Head = %zu; Length = %zu.", element->id, element->len);

    // Note that `vnet_queue_tx_enqueue()` uses a single descriptor entry each time
    // Populate the information of the buffer that can be recycled
    *packet_buf = self->descriptor_infos[element->id];

    // Acknowledge that we have recycled this buffer
    self->last_seen += 1;

    return SYS_ERR_OK;
}

///
/// Enqueue a buffer that represents a network packet to be sent into the transmit queue
///
/// @param instance A non-null queue instance
/// @param rid The identifier of a previously registered physical memory region
/// @param offset The offset into the memory region where the buffer starts
/// @param length The length of the buffer in bytes
/// @param valid_offset The offset into the buffer (described by <rid, offset, length>) where the valid data in the buffer starts
/// @param valid_length The length of the valid data in the buffer in bytes
/// @param flags The flags associated with the given buffer described by the 4-tuple above
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
static errval_t vnet_queue_tx_enqueue(struct devq* instance, regionid_t rid, genoffset_t offset, genoffset_t length, genoffset_t valid_offset, genoffset_t valid_length, uint64_t flags)
{
    struct devq_buf packet_buf = {
        .rid = rid,
        .offset = offset,
        .length = length,
        .valid_data = valid_offset,
        .valid_length = valid_length,
        .flags = flags,
    };

    return vnet_queue_tx_enqueue_v2(instance, packet_buf);
}

///
/// Dequeue a buffer that can be reused to store a future network packet from the transmit queue
///
/// @param instance A non-null queue instance
/// @param rid The identifier of a previously registered physical memory region on return
/// @param offset The offset into the memory region where the buffer starts on return
/// @param length The length of the buffer in bytes on return
/// @param valid_offset The offset into the buffer (described by <rid, offset, length>) where the valid data in the buffer starts on return
/// @param valid_length The length of the valid data in the buffer in bytes on return
/// @param flags The flags associated with the given buffer described by the 4-tuple above on return
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
static errval_t vnet_queue_tx_dequeue(struct devq* instance, regionid_t* rid, genoffset_t* offset, genoffset_t* length, genoffset_t* valid_offset, genoffset_t* valid_length, uint64_t* flags)
{
    struct devq_buf packet_buf;

    errval_t error = vnet_queue_tx_dequeue_v2(instance, &packet_buf);

    if (err_is_ok(error))
    {
        *rid = packet_buf.rid;

        *offset = packet_buf.offset;

        *length = packet_buf.length;

        *valid_offset = packet_buf.valid_data;

        *valid_length = packet_buf.valid_length;

        *flags = packet_buf.flags;
    }

    return error;
}

//
// MARK: - Virtual Network Queue: Network Queue IMP (RX Queue)
//

///
/// Enqueue a buffer that can be used to store the incoming network packet into the receive queue
///
/// @param instance A non-null queue instance
/// @param packet_buf A buffer descriptor that describes a buffer which can be used to store the incoming packet
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This will be the new interface of `devq_enqueue()`.
///
static errval_t vnet_queue_rx_enqueue_v2(struct devq* instance, struct devq_buf packet_buf)
{
    pinfovn("RX: Invoked with packet buffer:");
    pinfovn("\t          RID = %zu", packet_buf.rid);
    pinfovn("\tBuffer Offset = %zu", packet_buf.offset);
    pinfovn("\tBuffer Length = %zu", packet_buf.length);
    pinfovn("\t  Data Offset = %zu", packet_buf.valid_data);
    pinfovn("\t  Data Length = %zu", packet_buf.valid_length);
    pinfovn("\t        Flags = %zu", packet_buf.flags);

    struct vnet_queue* self = (struct vnet_queue*) instance;

    // Guard: Retrieve the memory region that has the given identifier
    const struct vnet_queue_mem_region* region = vnet_queue_get_region_by_id(self, packet_buf.rid);

    if (region == NULL)
    {
        debug_printf("Failed to find the memory region that has the given identifier %u.\n", packet_buf.rid);

        return DEVQ_ERR_INVALID_REGION_ID;
    }

    pinfovn("RX: Found the memory region that has the given identifier:");
    pinfovn("\t  RID = %zu", region->rid);
    pinfovn("\tPADDR = %p", region->paddr);
    pinfovn("\tVADDR = %p", region->vaddr);
    pinfovn("\t Size = %zu", region->size);

    // Section 2.6.5 The Virtqueue Descriptor Table
    // Note that the given packet span only one descriptor entry,
    // so we can use the `index` in the available ring as the index of the next free entry in the descriptor table
    size_t index = self->queue.avail->index % self->size;

    pinfovn("RX: Index of the next available descriptor = %zu.", index);

    struct virtq_desc* descriptor = &self->queue.desc[index];

    virtq_descriptor_init(descriptor, region->paddr + packet_buf.offset, packet_buf.length, VIRTQ_DESC_F_WRITE, 0);

    pinfovn("RX: Descriptor Entry: Address = %p; Length = %zu; Flags = %zu; Next = %zu.",
          descriptor->paddr, descriptor->length, descriptor->flags, descriptor->next);

    // Ensure that the device can see the updated descriptor entry
    dmb();

    // Record the memory region information for this descriptor entry
    self->descriptor_infos[index] = packet_buf;

    // Section 2.6.6 The Virtqueue Available Ring
    // Update the available ring
    self->queue.avail->flags = VIRTQ_AVAIL_F_NO_INTERRUPT;

    self->queue.avail->ring[index] = index;

    self->queue.avail->index += 1;

    // Section 4.2.3.3 Available Buffer Notifications
    // Notify the device that there is an available descriptor
    // Note that the register value is the queue index instead of the descriptor index
    virtio_mmio_QueueNotify_wr(vnet_queue_get_virtio_mmio_handle(self), self->index);

    return SYS_ERR_OK;
}

///
/// Dequeue a buffer that represents a received network packet from the receive queue
///
/// @param instance A non-null queue instance
/// @param packet_buf A buffer descriptor that describes a buffer that represents the received packet on return
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note This will be the new interface of `devq_dequeue()`.
///
static errval_t vnet_queue_rx_dequeue_v2(struct devq* instance, struct devq_buf* packet_buf)
{
    struct vnet_queue* self = (struct vnet_queue*) instance;

    // Guard: Check whether there is a buffer that have been used by the device
    //   i.e. Check whether there is a received packet that can be processed by the network stack
    //   i.e. Check whether the Used Ring is empty
    // Note that the hardware may increment the used index (because of newly arrived packets) while the driver is processing this packet
    size_t current_used_index = self->queue.used->index;

    if (self->last_seen == current_used_index)
    {
        return DEVQ_ERR_QUEUE_EMPTY;
    }

    pinfovn("RX: Last Seen Index = %zu; Current Used Index = %zu.", self->last_seen, current_used_index);

    // There is at least a buffer that can be processed
    struct virtq_used_elem* element = &self->queue.used->ring[self->last_seen % self->size];

    pinfovn("RX: Used Element: Head = %zu; Length = %zu.", element->id, element->len);

    // Note that `VIRTIO_NET_F_MRG_RXBUF` was not negotiated and thus the entire packet is contained within this buffer
    // Populate the information of the buffer that can be recycled
    *packet_buf = self->descriptor_infos[element->id];

    packet_buf->valid_data += sizeof(struct virtio_net_hdr);

    packet_buf->valid_length = element->len - sizeof(struct virtio_net_hdr);

    // Acknowledge that we have processed this buffer
    self->last_seen += 1;

#ifdef VNET_DEBUG
    {
        struct virtq_desc* descriptor = &self->queue.desc[element->id];
        pinfovn("RX: Descriptor Entry: Address = %p; Length = %zu; Flags = %zu; Next = %zu.",
              descriptor->paddr, descriptor->length, descriptor->flags, descriptor->next);

        pinfovn("RX: Found the packet buffer:");
        pinfovn("\t          RID = %zu", packet_buf->rid);
        pinfovn("\tBuffer Offset = %zu", packet_buf->offset);
        pinfovn("\tBuffer Length = %zu", packet_buf->length);
        pinfovn("\t  Data Offset = %zu", packet_buf->valid_data);
        pinfovn("\t  Data Length = %zu", packet_buf->valid_length);
        pinfovn("\t        Flags = %zu", packet_buf->flags);

        const struct vnet_queue_mem_region* region = vnet_queue_get_region_by_id(self, packet_buf->rid);
        pinfovn("RX: Found the memory region that has the given identifier %zu:", packet_buf->rid);
        // vnet_queue_mem_region_dump(region, 32);

        struct virtio_net_hdr* header = (struct virtio_net_hdr*) (region->vaddr + packet_buf->offset + packet_buf->valid_data - sizeof(struct virtio_net_hdr));
        pinfovn("RX: Net Header:");
        pinfovn("\tFlags = %u.", header->flags);
        pinfovn("\tGSO Type = %u.", header->gso_type);
        pinfovn("\tHDR Length = %u.", header->hdr_len);
        pinfovn("\tGSO Size = %u.", header->gso_size);
        pinfovn("\tCSUM Start = %u.", header->csum_start);
        pinfovn("\tCSUM Offset = %u.", header->csum_offset);
        pinfovn("\tNum Buffers = %u.", header->num_buffers);
    }
#endif

    return SYS_ERR_OK;
}

///
/// Enqueue a buffer that can be used to store the incoming network packet into the receive queue
///
/// @param instance A non-null queue instance
/// @param rid The identifier of a previously registered physical memory region
/// @param offset The offset into the memory region where the buffer starts
/// @param length The length of the buffer in bytes
/// @param valid_offset The offset into the buffer (described by <rid, offset, length>) where the valid data in the buffer starts
/// @param valid_length The length of the valid data in the buffer in bytes
/// @param flags The flags associated with the given buffer described by the 4-tuple above
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
static errval_t vnet_queue_rx_enqueue(struct devq* instance, regionid_t rid, genoffset_t offset, genoffset_t length, genoffset_t valid_offset, genoffset_t valid_length, uint64_t flags)
{
    struct devq_buf packet_buf = {
        .rid = rid,
        .offset = offset,
        .length = length,
        .valid_data = valid_offset,
        .valid_length = valid_length,
        .flags = flags,
    };

    return vnet_queue_rx_enqueue_v2(instance, packet_buf);
}

///
/// Dequeue a buffer that represents a received network packet from the receive queue
///
/// @param instance A non-null queue instance
/// @param rid The identifier of a previously registered physical memory region on return
/// @param offset The offset into the memory region where the buffer starts on return
/// @param length The length of the buffer in bytes on return
/// @param valid_offset The offset into the buffer (described by <rid, offset, length>) where the valid data in the buffer starts on return
/// @param valid_length The length of the valid data in the buffer in bytes on return
/// @param flags The flags associated with the given buffer described by the 4-tuple above on return
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
static errval_t vnet_queue_rx_dequeue(struct devq* instance, regionid_t* rid, genoffset_t* offset, genoffset_t* length, genoffset_t* valid_offset, genoffset_t* valid_length, uint64_t* flags)
{
    struct devq_buf packet_buf;

    errval_t error = vnet_queue_rx_dequeue_v2(instance, &packet_buf);

    if (err_is_ok(error))
    {
        *rid = packet_buf.rid;

        *offset = packet_buf.offset;

        *length = packet_buf.length;

        *valid_offset = packet_buf.valid_data;

        *valid_length = packet_buf.valid_length;

        *flags = packet_buf.flags;
    }

    return error;
}

//
// MARK: - Virtual Network Queue: Create Network Queues (Private)
//

///
/// Allocate a frame of the given size and map it at a free virtual address
///
/// @param frame A non-null pointer to the newly allocated frame on return
/// @param paddr A non-null pointer to the starting physical address of the newly allocated frame on return
/// @param vaddr A non-null pointer to the virtual address at which the newly allocated frame is mapped on return
/// @param bytes Specify the amount of physical memory in bytes to allocate
/// @param flags Specify the flags to map the newly allocated frame
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
static errval_t vnet_queue_alloc_and_map_frame(struct capref* frame, genpaddr_t* paddr, lvaddr_t* vaddr, size_t bytes, int flags)
{
    // Guard: Allocate a frame
    errval_t err = frame_alloc(frame, bytes, &bytes);

    if (err_is_fail(err))
    {
        return err;
    }

    // Guard: Map the frame
    err = paging_map_frame_attr(get_current_paging_state(), (void**) vaddr, bytes, *frame, flags);

    if (err_is_fail(err))
    {
        return err;
    }

    // Zero out the frame
    memset((void*) *vaddr, 0, bytes);

    // Guard: Set the starting physical address
    struct frame_identity identity;

    err = frame_identify(*frame, &identity);

    if (err_is_fail(err))
    {
        return err;
    }

    *paddr = identity.base;

    return SYS_ERR_OK;
}

///
/// Initialize a virtual network queue with the given size
///
/// @param self A non-null queue instance
/// @param device A non-null virtio network device instance as the provider
/// @param index The index of the backend virtual queue
/// @param size The number of entries in the queue
/// @return `SYS_ERR_OK` on success, other values otherwise.
/// @note The caller must ensure that
///       1) The given index is valid;
///       2) The virtual queue at the given index is inactive;
///       3) The given queue size is a power of 2 and does not exceed the maximum allowed size.
/// @note This function conforms to Section 4.2.3.2 Virtqueue Configuration.
///
static errval_t vnet_queue_init_with_size(struct vnet_queue* self, struct vnet_device* device, size_t index, size_t size)
{
    // Grab the MMIO handle
    virtio_mmio_t* mmio = vnet_device_get_virtio_mmio_handle(device);
    
    // Select the queue
    virtio_mmio_QueueSel_wr(mmio, index);

    // Guard: The queue of interest must be inactive
    if (virtio_mmio_QueueReady_rd(mmio) != 0)
    {
        USER_PANIC("The virtual queue at the given index %zu is active.", index);
    }

    // Guard: The given queue size must not exceed the maximum size
    size_t maximum_size = virtio_mmio_QueueNumMax_rd(mmio);

    if (size > maximum_size)
    {
        USER_PANIC("The given size %zu exceeds the maximum supported size %zu.", size, maximum_size);
    }

    // Guard: The given queue size must be a power of 2 as stated in Section 2.6 Split Virtqueues
    if (!IsPowerOf2(size))
    {
        USER_PANIC("The given size %zu must be a power of 2.", size);
    }

    // Guard: Allocate the region information for each descriptor entry
    struct devq_buf* infos = calloc(size, sizeof(struct devq_buf));

    if (infos == NULL)
    {
        return LIB_ERR_MALLOC_FAIL;
    }
    
    debug_printf("[%02d] Virtual Queue:\n", index);
    debug_printf("\t  Queue Max Size = %zu.\n", maximum_size);
    debug_printf("\t      Queue Size = %zu.\n", size);
    virtio_mmio_QueueNum_wr(mmio, size);

    // The starting physical address of each virtual queue component
    genpaddr_t paddr = 0;

    // Guard: Allocate physical memory for the virtual queue descriptor table
    errval_t err = vnet_queue_alloc_and_map_frame(&self->queue.desc_frame,
                                                  &paddr, (lvaddr_t *) &self->queue.desc,
                                                  virtq_split_virtq_descriptor_table_size(size),
                                                  VREGION_FLAGS_READ_WRITE_NOCACHE);

    if (err_is_fail(err))
    {
        free(infos);

        DEBUG_ERR(err, "Failed to allocate memory for the descriptor table.");

        return err;
    }

    debug_printf("\tDescriptor Table = %p.\n", paddr);
    virtio_mmio_QueueDescLow_wr(mmio, paddr & 0xFFFFFFFF);
    virtio_mmio_QueueDescHigh_wr(mmio, paddr >> 32);

    // Guard: Allocate physical memory for the available ring
    err = vnet_queue_alloc_and_map_frame(&self->queue.avail_frame,
                                         &paddr, (lvaddr_t *) &self->queue.avail,
                                         virtq_split_virtq_available_ring_size(size),
                                         VREGION_FLAGS_READ_WRITE_NOCACHE);

    if (err_is_fail(err))
    {
        free(infos);

        // TODO: Unmap and release the frame
        DEBUG_ERR(err, "Failed to allocate memory for the available ring.");

        return err;
    }

    debug_printf("\t  Available Ring = %p.\n", paddr);
    virtio_mmio_queue_avail_lo_wr(mmio, paddr & 0xFFFFFFFF);
    virtio_mmio_queue_avail_hi_wr(mmio, paddr >> 32);

    // Guard: Allocate physical memory for the used ring
    err = vnet_queue_alloc_and_map_frame(&self->queue.used_frame,
                                         &paddr, (lvaddr_t *) &self->queue.used,
                                         virtq_split_virtq_used_ring_size(size),
                                         VREGION_FLAGS_READ_WRITE_NOCACHE);

    if (err_is_fail(err))
    {
        free(infos);

        // TODO: Unmap and release the frame
        DEBUG_ERR(err, "Failed to allocate memory for the used ring.");

        return err;
    }

    debug_printf("\t       Used Ring = %p.\n", paddr);
    virtio_mmio_queue_used_lo_wr(mmio, paddr & 0xFFFFFFFF);
    virtio_mmio_queue_used_hi_wr(mmio, paddr >> 32);

    // Mark the queue ready
    virtio_mmio_QueueReady_wr(mmio, 0x1);

    // All done: Initialize the instance
    self->device = device;

    self->index = index;

    self->size = size;

    self->regions = NULL;

    self->descriptor_infos = infos;

    self->last_seen = 0;

    return SYS_ERR_OK;
}

//
// MARK: - Virtual Network Queue: Create Network Queues (Private)
//

///
/// Initialize the virtual function table for a virtual network queue used for transmitting network packets
///
/// @param self A non-null queue instance
///
static void vnet_queue_init_vft_for_tx(struct vnet_queue* self)
{
    self->super.f.reg = vnet_queue_register;

    self->super.f.dereg = vnet_queue_deregister;

    self->super.f.enq = vnet_queue_tx_enqueue;

    self->super.f.deq = vnet_queue_tx_dequeue;

    self->super.f.get_reserved_size = vnet_queue_get_num_reserved_bytes;
}

///
/// Initialize the virtual function table for a virtual network queue used for receiving network packets
///
/// @param self A non-null queue instance
///
static void vnet_queue_init_vft_for_rx(struct vnet_queue* self)
{
    self->super.f.reg = vnet_queue_register;

    self->super.f.dereg = vnet_queue_deregister;

    self->super.f.enq = vnet_queue_rx_enqueue;

    self->super.f.deq = vnet_queue_rx_dequeue;

    self->super.f.get_reserved_size = vnet_queue_get_num_reserved_bytes;
}

///
/// Create a virtual network queue with the given size
///
/// @param instance A non-null pointer to a newly created instance on return
/// @param vft_initializer A non-null pointer to the function that initializes the virtual function table
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
static errval_t vnet_queue_create_with_size(struct vnet_queue** instance, vnet_queue_vft_initializer vft_initializer, struct vnet_device* device, size_t index, size_t size)
{
    // Guard: Allocate a new instance
    struct vnet_queue* self = calloc(1, sizeof(struct vnet_queue));

    if (self == NULL)
    {
        return LIB_ERR_MALLOC_FAIL;
    }

    // Initialize the base class
    errval_t error = devq_init(&self->super, false);

    if (err_is_fail(error))
    {
        DEBUG_ERR(error, "Failed to initialize the base class.");

        return error;
    }

    // Register virtual functions
    (*vft_initializer)(self);

    // Initialize the concrete class
    error = vnet_queue_init_with_size(self, device, index, size);

    if (err_is_fail(error))
    {
        DEBUG_ERR(error, "Failed to initialize the concrete class.");

        return error;
    }

    // All done: Set the returned instance
    *instance = self;

    return SYS_ERR_OK;
}

//
// MARK: - Virtual Network Queue: Create Network Queues (Public)
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
errval_t vnet_queue_create_tx_queue_with_size(struct vnet_queue** instance, struct vnet_device* device, size_t index, size_t size)
{
    return vnet_queue_create_with_size(instance, vnet_queue_init_vft_for_tx, device, index, size);
}

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
errval_t vnet_queue_create_rx_queue_with_size(struct vnet_queue** instance, struct vnet_device* device, size_t index, size_t size)
{
    return vnet_queue_create_with_size(instance, vnet_queue_init_vft_for_rx, device, index, size);
}

///
/// Destroy the given virtual network queue
///
/// @param self A non-null queue instance returned by `vnet_queue_create_with_size()`
/// @return `SYS_ERR_OK` on success, other values otherwise.
///
errval_t vnet_queue_destroy(struct vnet_queue* self)
{
    (void)self;
    return LIB_ERR_NOT_IMPLEMENTED;
}