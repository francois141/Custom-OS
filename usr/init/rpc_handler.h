#pragma once

#include <aos/aos_rpc.h>
#include <async_channel.h>
#include <fs/fat32.h>


void set_cross_core_channel(struct async_channel *async);
struct async_channel *get_cross_core_channel(void);
void sync_rpc_request_handler(struct aos_rpc *rpc, void *arg);
void async_rpc_request_handler(struct async_channel *chan, void *data, size_t size,
                               struct capref *capv, size_t capc, struct response *res);