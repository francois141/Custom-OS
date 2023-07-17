/**
 * \file
 * \brief virtio-net driver module
 */

/*
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <aos/aos.h>
#include "virtio_net_device.h"
#include <netutil/udp.h>

__attribute__((unused))
static void sample_send_packets(struct vnet_device* device)
{
    // Sample Code:
    // Send a packet
    struct devq* tx_queue = vnet_device_get_tx_queue(device);

    size_t tx_queue_size = vnet_device_get_tx_queue_size(device);

    size_t tx_packet_size = 2048;

    size_t tx_buffer_size = tx_queue_size * tx_packet_size;

    // Step 1: Allocate physical memory for the packet buffer
    struct capref tx_frame;

    errval_t error = frame_alloc(&tx_frame, tx_buffer_size, NULL);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "Failed to allocate the frame for the receive queue.");
    }

    // Step 2: Map the packet buffer frame so that the network stack can process the packet
    uint8_t* tx_buffer;

    error = paging_map_frame_complete(get_current_paging_state(), (void**) &tx_buffer, tx_frame);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "Failed to map the frame for the receive queue.");
    }

    // Step 3: Register this frame with the TX queue
    regionid_t tx_rid;

    error = devq_register(tx_queue, tx_frame, &tx_rid);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "Failed to register the memory with the RX queue.");
    }

    // Send a packet (75 bytes: nslookup www.example.com; IP + UDP + DNS)
    char packet_bytes[] = {
        0x14, 0xdd, 0xa9, 0x6f, 0x18, 0x80,                 // Link: Destination MAC Address
        0xfc, 0xaa, 0x14, 0x21, 0x49, 0xa3,                 // Link: Source MAC Address
        0x08, 0x00,                                         // Link: IPv4
        0x45,                                               // Network: IPv4, Header Length 20 bytes
        0x00,                                               // Network: Type of Service
        0x00, 0x3d,                                         // Network: Total Length
        0x8f, 0x01,                                         // Network: Identification
        0x00, 0x00,                                         // Network: Flags and Fragment Offset
        0x40,                                               // Network: TTL
        0x11,                                               // Network: Protocol UDP
        0x60, 0x35,                                         // Network: Header Checksum
        0xc0, 0xa8, 0x05, 0x28,                             // Network: Source Address
        0xc0, 0xa8, 0x05, 0x01,                             // Network: Destination Address
        0xe8, 0xb8,                                         // Transport: Source Port
        0x00, 0x35,                                         // Transport: Destination Port
        0x00, 0x29,                                         // Transport: Length
        0x8b, 0xb4,                                         // Transport: Checksum
        0xe9, 0xee, 0x01, 0x00, 0x00, 0x01,                 // Application: DNS Query
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x77,
        0x77, 0x77, 0x07, 0x65, 0x78, 0x61, 0x6d, 0x70,
        0x6c, 0x65, 0x03, 0x63, 0x6f, 0x6d, 0x00, 0x00,
        0x01, 0x00, 0x01
    };

    debug_printf("Send 4 packets...\n");

    for (size_t index = 0; index < 4; index += 1)
    {
        memcpy(tx_buffer + index * tx_packet_size + 12, packet_bytes, sizeof(packet_bytes));

        struct devq_buf packet_buffer;
        packet_buffer.rid = tx_rid;
        packet_buffer.offset = index * tx_packet_size;
        packet_buffer.length = tx_packet_size;
        packet_buffer.valid_data = 12;
        packet_buffer.valid_length = sizeof(packet_bytes);
        packet_buffer.flags = 0;

        debug_printf("Sending the packet %zu...\n", index);

        error = devq_enqueue(tx_queue, packet_buffer.rid, packet_buffer.offset, packet_buffer.length, packet_buffer.valid_data, packet_buffer.valid_length, packet_buffer.flags);

        if (err_is_fail(error))
        {
            USER_PANIC_ERR(error, "Failed to send the packet %zu.", index);
        }

        while (true)
        {
            struct devq_buf recycled;

            if (err_is_ok(devq_dequeue(tx_queue, &recycled.rid, &recycled.offset, &recycled.length, &recycled.valid_data, &recycled.valid_length, &recycled.flags)))
            {
                debug_printf("Recycled the buffer for packet %zu. RID = %zu; Offset = %zu; Length = %zu.\n", index, recycled.rid, recycled.offset, recycled.length);

                break;
            }
        }
    }
}

__attribute__((unused))
static void sample_receive_packets(struct vnet_device* device)
{
    // Sample Code:
    // >> Populate the RX queue
    // Section 5.1.6.3: Setting Up Receive Buffers
    // The maximum packet is 1514 bytes and a 12-byte Virtio Net header is prepended to the packet
    // Thus the minimum buffer size for each packet is 1514 + 12 = 1526 bytes, rounded it up to 2048 bytes
    struct devq* rx_queue = vnet_device_get_rx_queue(device);

    size_t rx_queue_size = vnet_device_get_rx_queue_size(device);

    size_t rx_packet_size = 2048;

    size_t rx_buffer_size = rx_queue_size * rx_packet_size;

    // Step 1: Allocate physical memory for the packet buffer
    struct capref rx_frame;

    errval_t error = frame_alloc(&rx_frame, rx_buffer_size, NULL);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "Failed to allocate the frame for the receive queue.");
    }

    // Step 2: Map the packet buffer frame so that the network stack can process the packet
    uint8_t* rx_buffer;

    error = paging_map_frame_complete(get_current_paging_state(), (void**) &rx_buffer, rx_frame);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "Failed to map the frame for the receive queue.");
    }

    memset(rx_buffer, 0xCC, rx_buffer_size);

    // Step 3: Register this frame with the RX queue
    regionid_t rx_rid;

    error = devq_register(rx_queue, rx_frame, &rx_rid);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "Failed to register the memory with the RX queue.");
    }

    // Step 4: Split the frame into 2 KB chunks and enqueue each of them to the RX queue
    for (size_t index = 0; index < rx_queue_size; index += 1)
    {
        error = devq_enqueue(rx_queue, rx_rid, index * rx_packet_size, rx_packet_size, 0, rx_packet_size, 0);

        if (err_is_fail(error))
        {
            USER_PANIC_ERR(error, "[%02d] Failed to enqueue the packet buffer to the RX queue.", index);
        }
    }

    // Receive a packet
    struct devq_buf packet_buffer;

    debug_printf("====================================================================================================================\n");

    while (true)
    {
        // Interrupt is not available: Polling for a packet
        if (err_is_ok(devq_dequeue(rx_queue, &packet_buffer.rid,
                                   &packet_buffer.offset, &packet_buffer.length,
                                   &packet_buffer.valid_data, &packet_buffer.valid_length,
                                   &packet_buffer.flags)))
        {
            debug_printf("Received a network packet of %zu bytes:\n", packet_buffer.valid_length);
            debug_printf("\t          RID = %zu\n", packet_buffer.rid);
            debug_printf("\tBuffer Offset = %zu\n", packet_buffer.offset);
            debug_printf("\tBuffer Length = %zu\n", packet_buffer.length);
            debug_printf("\t  Data Offset = %zu\n", packet_buffer.valid_data);
            debug_printf("\t  Data Length = %zu\n", packet_buffer.valid_length);
            debug_printf("\t        Flags = %zu\n", packet_buffer.flags);

            // Process the packet: Network layer, Transport layer, Application layer
            // Here we just print the packet content
            pbuf(rx_buffer + packet_buffer.offset + packet_buffer.valid_data, packet_buffer.valid_length, 16);

            // Zero out the buffer
            memset(rx_buffer + packet_buffer.offset, 0xCC, packet_buffer.length);

            // Reset the packet buffer
            packet_buffer.valid_data = 0;
            packet_buffer.valid_length = packet_buffer.length;

            // Finished processing the packet
            // Put it back to the RX queue so that the device can reuse it
            error = devq_enqueue(rx_queue, packet_buffer.rid,
                                 packet_buffer.offset, packet_buffer.length,
                                 packet_buffer.valid_data, packet_buffer.valid_length,
                                 packet_buffer.flags);

            assert(err_is_ok(error));

            debug_printf("====================================================================================================================\n");
        }
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    errval_t error;

    debug_printf("virtio-net: Driver started.\n");

    // Guard: Allocate a new device instance
    struct vnet_device* device = vnet_device_create();

    if (device == NULL)
    {
        USER_PANIC("virtio-net: Failed to allocate the device instance.");
    }

    // TODO: M7: Network Project
    // TODO: Implement the missing part in `vnet_device_init()` in `virtio_net_device.c`

    // Guard: Initialize the device instance
    struct vnet_device_config config = { .rx_queue_size = 0, .tx_queue_size = 0 };

    error = vnet_device_init(device, &config /* NULL or specify a queue size of 0 to use the maximum size */);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "virtio-net: Failed to initialize the virtio network device.");
    }

    // Guard: Probe the virtio network device
    error = vnet_device_probe(device);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "virtio-net: Failed to probe the virtio network device.");
    }

    // Guard: Start the virtio network device
    error = vnet_device_start(device);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "virtio-net: Failed to start the virtio network device.");
    }

    // Guard: Retrieve the MAC address
    struct eth_addr mac;

    error = vnet_device_get_mac_address(device, &mac);

    if (err_is_fail(error))
    {
        USER_PANIC_ERR(error, "virtio-net: Failed to retrieve the MAC address.");
    }

    debug_printf("virtio-net: MAC Address = %02X:%02X:%02X:%02X:%02X:%02X.\n",
                 mac.addr[0], mac.addr[1], mac.addr[2], mac.addr[3], mac.addr[4], mac.addr[5]);

    // TODO: M7: Network Project
    // TODO: Notify the network stack that this virtio network device is now available
    // TODO: The network stack should allocate a frame of size QUEUE_SIZE * PACKET_BUFFER_SIZE
    // TODO: Refer to `sample_send_packets()` and `sample_receive_packets()`.

    debug_printf("Goodbye.");
}