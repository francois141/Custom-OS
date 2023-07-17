/*
 * Copyright (c) 2020 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef VIRTIO_NET_DEVIF_H
#define VIRTIO_NET_DEVIF_H

#include <errors/errno.h>

struct vnetd;
struct vnet_queue;

errval_t vnet_rx_queue_create(struct vnet_queue ** q, struct vnetd* dev);
errval_t vnet_tx_queue_create(struct vnet_queue ** q, struct vnetd* dev);

#endif
