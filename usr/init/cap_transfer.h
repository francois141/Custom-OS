#ifndef CAP_TRANSFER_H
#define CAP_TRANSFER_H

#include <aos/aos.h>

struct cap_transfer {
    struct capability cap;
    coreid_t          owner;
    uint8_t           relations;
    bool valid;
};

void     cap_dump_relations(struct capref cap);
errval_t cap_transfer_copy(struct capref cap, struct cap_transfer *transfer);
errval_t cap_transfer_move(struct capref cap, struct cap_transfer *transfer);
errval_t cap_from_transfer(struct cap_transfer *transfer, struct capref cap);
bool     cap_transfer_is_valid(struct cap_transfer *transfer);

#endif  // CAP_TRANSFER_H