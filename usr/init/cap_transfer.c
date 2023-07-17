#include "cap_transfer.h"

#include <aos/domain.h>
#include "barrelfish_kpi/distcaps.h"
#include "distops/invocations.h"

void cap_dump_relations(struct capref cap)
{
    errval_t err = SYS_ERR_OK;
    uint8_t  relations;
    monitor_cap_has_relations(cap, ~0, &relations);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "monitor_cap_has_relations failed");
    }
    debug_printf("Local relations : Desc = %d, Ancs = %d, Copy = %d\n", relations & RRELS_DESC_BIT,
                 relations & RRELS_ANCS_BIT, relations & RRELS_COPY_BIT);
    monitor_remote_relations(cap, 0, 0, &relations);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "monitor_remote_relations failed");
    }
    debug_printf("Remote relations: Desc = %d, Ancs = %d, Copy = %d\n", relations & RRELS_DESC_BIT,
                 relations & RRELS_ANCS_BIT, relations & RRELS_COPY_BIT);
}

errval_t cap_transfer_move(struct capref cap, struct cap_transfer *transfer)
{
    if (capref_is_null(cap)) {
        transfer->valid = false;
        return SYS_ERR_OK;
    } else {
        transfer->valid = true;
    } 
    errval_t err = SYS_ERR_OK;

    err = monitor_cap_identify(cap, &transfer->cap);
    if (err_is_fail(err)) {
        return err;
    }

    // mask is ~0 because we want to get all relations
    uint8_t local_rels;
    err = monitor_cap_has_relations(cap, ~0, &local_rels);
    if (err_is_fail(err)) {
        return err;
    }

    // local relations of this cap become remote relations of cap on other core
    transfer->relations = local_rels;
    // if there are no local copies of this cap, the other core becomes the owner
    if (local_rels & RRELS_COPY_BIT) {
        transfer->owner = disp_get_core_id();
    } else if (distcap_is_moveable(transfer->cap.type)) {
        transfer->owner = 1 - disp_get_core_id();
    } else {
        // we're trying to send the last copy of a non-moveable cap. This is not allowed.
        // Panic for now, should probably return an error later.
        debug_print_capability(&transfer->cap);
        USER_PANIC("Trying to send last copy of non-moveable cap");
    }

    // set the copy bit for remote relations, and obtain previous entries
    uint8_t remote_rels;
    err = monitor_remote_relations(cap, RRELS_COPY_BIT, RRELS_COPY_BIT, &remote_rels);
    if (err_is_fail(err)) {
        return err;
    }

    // nullify the copy because we're moving
    err = monitor_nullify_cap(cap);
    if (err_is_fail(err)) {
        return err;
    }

    return err;
}

errval_t cap_transfer_copy(struct capref cap, struct cap_transfer *transfer)
{
    if (capref_is_null(cap)) {
        transfer->valid = false;
        return SYS_ERR_OK;
    } else {
        transfer->valid = true;
    } 
    errval_t err = SYS_ERR_OK;

    err = monitor_cap_identify(cap, &transfer->cap);
    if (err_is_fail(err)) {
        return err;
    }

    // set the copy bit for remote relations, and obtain previous entries
    uint8_t remote_rels;
    err = monitor_remote_relations(cap, RRELS_COPY_BIT, RRELS_COPY_BIT, &remote_rels);
    if (err_is_fail(err)) {
        return err;
    }

    // mask is ~0 because we want to get all relations
    uint8_t local_rels;
    err = monitor_cap_has_relations(cap, ~0, &local_rels);
    if (err_is_fail(err)) {
        return err;
    }

    // TODO: Is it ok not to send the remote relations? Probably yes (at least in a two-core system),
    // because the sending core's remote relations will be the receiving core's local relations.
    transfer->relations = RRELS_COPY_BIT | local_rels;
    transfer->owner     = disp_get_core_id();

    return err;
}

errval_t cap_from_transfer(struct cap_transfer *transfer, struct capref cap)
{
    if (!transfer->valid) {
        return SYS_ERR_CAP_NOT_FOUND;
    }
    errval_t err = SYS_ERR_OK;

    err = monitor_cap_create(cap, &transfer->cap, transfer->owner);
    if (err_is_fail(err)) {
        return err;
    }
    uint8_t _rels;
    err = monitor_remote_relations(cap, transfer->relations, ~0, &_rels);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "monitor_remote_relations failed");
    }

    return err;
}

bool cap_transfer_is_valid(struct cap_transfer *transfer) {
    return transfer->valid;
}
