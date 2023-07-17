#pragma once

#include <aos/aos_rpc.h>

errval_t distcap_init(void);
bool handle_distcap_rpc_request(struct aos_rpc_handler_data *rpc_data);
