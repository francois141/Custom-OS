
#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/domain.h>

#include "aos/event_queue.h"
#include "barrelfish_kpi/capbits.h"
#include "barrelfish_kpi/distcaps.h"
#include "distcap_handler.h"
#include "aos/aos_rpc_types.h"
#include "async_channel.h"
#include "barrelfish_kpi/distcaps.h"
#include "distops/debug.h"
#include "distops/caplock.h"
#include "distops/deletestep.h"
#include "distops/invocations.h"
#include "rpc_handler.h"
#include "tests.h"
#include "proc_mgmt.h"
#include "distops/domcap.h"
#include "cap_transfer.h"
#include "rpc_handler.h"
#include "mem_alloc.h"

typedef struct capability capability_t;

struct delete_sync {
    struct aos_distcap_base_request base;
    capability_t                    cap;
    uint8_t                         owner;
    enum {
        DELETE_SYNC_MOVE_OWNER,
        DELETE_SYNC_DELETE_FOREIGNS,
        DELETE_SYNC_LAST_NONOWNER,
    } op;
};

struct revoke_sync {
    struct aos_distcap_base_request base;
    capability_t                    cap;
    uint8_t                         owner;
};

struct retype_sync {
    struct aos_distcap_base_request base;
    capability_t                    cap;
    uint8_t                         owner;
    gensize_t                       offset;
    gensize_t                       objsize;
    size_t                          count;
};

struct remote_revoke_suspend {
    struct aos_rpc_handler_data rpc_data;
    struct delete_queue_node    qn;
};

struct remote_retype_suspend {
    struct aos_rpc_handler_data rpc_data;
    struct retype_sync          sync;
    struct domcapref            src_cap;
    struct domcapref            dest_cap;
};

struct remote_delete_suspend {
    struct aos_rpc_handler_data rpc_data;
    struct delete_sync          sync;
    struct domcapref            cap;
    struct delete_queue_node    qn;
};

struct retype_suspend {
    struct aos_rpc_handler_data rpc_data;
    struct retype_sync          sync;
    struct domcapref            src_cap;
    struct domcapref            dest_cap;
    struct event_queue_node     qn;
};

struct delete_suspend {
    struct aos_rpc_handler_data rpc_data;
    struct delete_sync          sync;
    struct domcapref            cap;
    struct delete_queue_node    qn;
};

struct revoke_suspend {
    struct aos_rpc_handler_data rpc_data;
    struct revoke_sync          sync;
    struct domcapref            cap;
    struct delete_queue_node    qn;
};

static struct capref tempcap = {};

errval_t distcap_init(void)
{
    return slot_alloc(&tempcap);
}

static void delete_last(struct domcapref domcap)
{
    errval_t err = SYS_ERR_OK;

    err = monitor_delete_last(domcap.croot, domcap.cptr, domcap.level, tempcap);
    if (err_no(err) == SYS_ERR_RAM_CAP_CREATED) {
        err = aos_ram_free(tempcap);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "aos_ram_free");
        }
    } else if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "monitor_delete_last");
    }
}

static void queue_delete_handler(void *arg)
{
    struct delete_suspend *data = arg;
    data->rpc_data.resume_fn.handler(data->rpc_data.resume_fn.arg);
    free(arg);
}

static void queue_revoke_handler(void *arg)
{
    struct revoke_suspend *data = arg;
    data->rpc_data.resume_fn.handler(data->rpc_data.resume_fn.arg);
    free(arg);
}

static void remote_queue_revoke_handler(void *arg)
{
    struct remote_revoke_suspend *data = arg;
    data->rpc_data.resume_fn.handler(data->rpc_data.resume_fn.arg);
    free(arg);
}

static void coresync_delete_handler(struct request *req, void *data, size_t size,
                                    struct capref *capv, size_t capc)
{
    (void)capv;
    assert(capc == 0);
    assert(size == sizeof(struct aos_generic_rpc_response));

    errval_t                         err      = SYS_ERR_OK;
    struct delete_suspend           *suspend  = req->meta;
    struct aos_generic_rpc_response *response = data;

    if (response->err != SYS_ERR_OK) {
        USER_PANIC_ERR(response->err, "delete failed on remote core");
    }

    struct domcapref cap = suspend->cap;
    // we locked the cap before sending the remote request. unlock it now
    caplock_unlock(cap);
    if (suspend->sync.op == DELETE_SYNC_LAST_NONOWNER) {
        // remote relations on the other core were updated. nullify cap and return.
        err = monitor_nullify_domcap(cap.croot, cap.cptr, cap.level);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_nullify_domcap");
        }
        suspend->rpc_data.resume_fn.handler(suspend->rpc_data.resume_fn.arg);
        free(suspend);
    } else if (suspend->sync.op == DELETE_SYNC_DELETE_FOREIGNS) {
        // all foreign copies were deleted. now delete local copies and send response
        delete_last(cap);
        delete_queue_wait(&suspend->qn, MKCLOSURE(queue_delete_handler, suspend));
    } else if (suspend->sync.op == DELETE_SYNC_MOVE_OWNER) {
        // ownership was moved to remote core. nullify cap on this core and send response
        err = monitor_nullify_domcap(cap.croot, cap.cptr, cap.level);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_nullify_domcap");
        }
        suspend->rpc_data.resume_fn.handler(suspend->rpc_data.resume_fn.arg);
        free(suspend);
    } else {
        USER_PANIC("invalid delete sync op");
    }
}

static void coresync_retype_handler(struct request *req, void *data, size_t size,
                                    struct capref *capv, size_t capc)
{
    (void)capv;
    assert(capc == 0);
    assert(size == sizeof(struct aos_generic_rpc_response));

    errval_t                         err      = SYS_ERR_OK;
    struct retype_suspend           *suspend  = req->meta;
    struct aos_generic_rpc_response *response = data;

    caplock_unlock(suspend->src_cap);

    if (response->err != SYS_ERR_OK) {
        struct aos_generic_rpc_response *client_response = (struct aos_generic_rpc_response *)suspend->rpc_data.send.data;
        client_response->err = response->err;
        suspend->rpc_data.resume_fn.handler(suspend->rpc_data.resume_fn.arg);
        free(suspend);
        return;
    }

    struct aos_distcap_retype_request *retype_req = suspend->rpc_data.recv.data;
    err = monitor_domcap_retype_remote_cap(suspend->dest_cap, suspend->src_cap, retype_req->offset,
                                           retype_req->new_type, retype_req->objsize,
                                           retype_req->count, retype_req->slot);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "monitor_domcap_retype_remote_cap");
    }

    suspend->rpc_data.resume_fn.handler(suspend->rpc_data.resume_fn.arg);
    free(suspend);
}

static void coresync_revoke_handler(struct request *req, void *data, size_t size,
                                    struct capref *capv, size_t capc)
{
    (void)capv;
    assert(capc == 0);
    assert(size == sizeof(struct aos_generic_rpc_response));

    errval_t                         err      = SYS_ERR_OK;
    struct revoke_suspend           *suspend  = req->meta;
    struct aos_generic_rpc_response *response = data;

    if (response->err != SYS_ERR_OK) {
        USER_PANIC_ERR(response->err, "revoke failed on remote core");
    }

    struct domcapref domcap = suspend->cap;
    caplock_unlock(domcap);
    if (suspend->sync.owner == disp_get_core_id()) {
        err = monitor_revoke_mark_target(domcap.croot, domcap.cptr, domcap.level);
    } else {
        err = monitor_revoke_mark_relations(&suspend->sync.cap);
    }
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "monitor_revoke_mark_target");
    }
    delete_queue_wait(&suspend->qn, MKCLOSURE(queue_revoke_handler, suspend));
}

static void delete_step_1(void* arg) {
    struct delete_suspend *suspend = arg;
    errval_t err = monitor_domcap_lock_cap(suspend->cap);
    if (err_no(err) == SYS_ERR_CAP_LOCKED) {
        caplock_wait(suspend->cap, &suspend->qn.qn, MKCLOSURE(delete_step_1, suspend));
    } else if(err_is_ok(err)) {
        async_request(get_cross_core_channel(), &suspend->sync, sizeof(struct delete_sync),
                    NULL, 0, coresync_delete_handler, suspend);
    } else {
        USER_PANIC_ERR(err, "monitor_domcap_lock_cap");
    }
}

static void retype_step_1(void* arg) {
    struct retype_suspend *suspend = arg;
    errval_t err = monitor_domcap_lock_cap(suspend->src_cap);
    if (err_no(err) == SYS_ERR_CAP_LOCKED) {
        caplock_wait(suspend->src_cap, &suspend->qn, MKCLOSURE(retype_step_1, suspend));
    } else if(err_is_ok(err)) {
        err = monitor_domcap_identify(suspend->src_cap, &suspend->sync.cap);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_domcap_identify");
        }
        err = monitor_get_domcap_owner(suspend->src_cap, &suspend->sync.owner);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_get_domcap_owner");
        }

        err = monitor_is_retypeable(&suspend->sync.cap, suspend->sync.offset, suspend->sync.objsize, suspend->sync.count);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_is_retypeable");
        }
        async_request(get_cross_core_channel(), &suspend->sync, sizeof(struct retype_sync), NULL, 0,
                        coresync_retype_handler, suspend);
    } else {
        USER_PANIC_ERR(err, "monitor_domcap_lock_cap");
    }
}

static void revoke_step_1(void* arg) {
    struct revoke_suspend *suspend = arg;
    errval_t err = monitor_domcap_lock_cap(suspend->cap);
    if (err_no(err) == SYS_ERR_CAP_LOCKED) {
        caplock_wait(suspend->cap, &suspend->qn.qn, MKCLOSURE(revoke_step_1, suspend));
    } else if(err_is_ok(err)) {
        async_request(get_cross_core_channel(), &suspend->sync, sizeof(struct retype_sync),
                        NULL, 0, coresync_revoke_handler, suspend);
    } else {
        USER_PANIC_ERR(err, "monitor_domcap_lock_cap");
    }
}

bool handle_distcap_rpc_request(struct aos_rpc_handler_data *rpc_data)
{
    errval_t err = SYS_ERR_OK;

    struct aos_distcap_base_request *basereq = rpc_data->recv.data;
    struct aos_generic_rpc_response *res     = rpc_data->send.data;
    res->type                                = AOS_RPC_RESPONSE_TYPE_DISTCAP;
    res->err                                 = SYS_ERR_OK;
    *rpc_data->send.datasize                 = sizeof(struct aos_generic_rpc_response);

    if (basereq->type == AOS_RPC_DISTCAP_RETYPE) {
        struct aos_distcap_retype_request *req = (struct aos_distcap_retype_request *)basereq;
        assert(rpc_data->recv.caps_size == 2);
        DEBUG_CAPOPS("Retype request");

        struct domcapref src_cap = { .croot = rpc_data->recv.caps[0], .cptr = req->src, .level = 2 };

        struct domcapref dest_cap
            = { .croot = rpc_data->recv.caps[1], .cptr = req->to, .level = req->to_level };

        struct retype_suspend *suspend = malloc(sizeof(struct retype_suspend));

        suspend->rpc_data = *rpc_data;
        suspend->src_cap  = src_cap;
        suspend->dest_cap = dest_cap;

        suspend->sync.base.base.type = AOS_RPC_REQUEST_TYPE_DISTCAP;
        suspend->sync.base.type      = AOS_RPC_DISTCAP_RETYPE_SYNC;
        suspend->sync.count          = req->count;
        suspend->sync.offset         = req->offset;
        suspend->sync.objsize        = req->objsize;

        retype_step_1(suspend);

        return false;
    } else if (basereq->type == AOS_RPC_DISTCAP_DELETE) {
        struct aos_distcap_delete_request *req = (struct aos_distcap_delete_request *)basereq;
        assert(rpc_data->recv.caps_size == 1);
        struct domcapref domcap
            = { .croot = rpc_data->recv.caps[0], .cptr = req->src, .level = req->level };
        struct capability thecap;

        struct delete_suspend *suspend = malloc(sizeof(struct delete_suspend));
        suspend->rpc_data              = *rpc_data;
        suspend->cap                   = domcap;

        DEBUG_CAPOPS("Delete request for cap:\n");
        monitor_domcap_identify(domcap, &thecap);
        debug_print_capability(&thecap);

        uint8_t rels;
        err = monitor_domcap_remote_relations(domcap.croot, domcap.cptr, domcap.level, 0, 0, &rels);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_domcap_remote_relations");
        }

        // check if any remote copies exist (which requires synchronization with the other core)
        if (rels & RRELS_COPY_BIT) {
            DEBUG_CAPOPS("delete: remote copies exist\n");
            uint8_t owner;
            err = monitor_get_domcap_owner(domcap, &owner);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_get_domcap_owner");
            }
            suspend->sync.base.base.type = AOS_RPC_REQUEST_TYPE_DISTCAP;
            suspend->sync.base.type      = AOS_RPC_DISTCAP_DELETE_SYNC;
            suspend->sync.cap            = thecap;
            suspend->sync.owner          = owner;
            if (owner == disp_get_core_id()) {
                // we are deleting the last copy of the cap on the owning core
                if (distcap_is_moveable(thecap.type)) {
                    // move ownership to other core, remove remote copy relation on other core
                    DEBUG_CAPOPS("delete: move ownership to other core\n");
                    suspend->sync.op = DELETE_SYNC_MOVE_OWNER;
                } else {
                    // delete all copies on the other core
                    DEBUG_CAPOPS("delete: delete all copies on the other core\n");
                    suspend->sync.op = DELETE_SYNC_DELETE_FOREIGNS;
                }
            } else {
                // we are deleting the last copy of the cap on the non-owning core
                // we only need to signal the other core to update its remote relations,
                // and nullify the cap on this core
                DEBUG_CAPOPS("delete: signal other core to update remote relations\n");
                suspend->sync.op = DELETE_SYNC_LAST_NONOWNER;
            }
            delete_step_1(suspend);
        } else {
            // no remote copies exist, we can delete the cap immediately
            delete_last(domcap);
            delete_queue_wait(&suspend->qn, MKCLOSURE(queue_delete_handler, suspend));
        }
        return false;
    } else if (basereq->type == AOS_RPC_DISTCAP_REVOKE) {
        struct aos_distcap_revoke_request *req = (struct aos_distcap_revoke_request *)basereq;
        assert(rpc_data->recv.caps_size == 1);
        struct domcapref domcap
            = { .croot = rpc_data->recv.caps[0], .cptr = req->src, .level = req->level };
        struct capability thecap;

        struct revoke_suspend *suspend = malloc(sizeof(struct revoke_suspend));
        suspend->rpc_data              = *rpc_data;
        suspend->cap                   = domcap;

        monitor_domcap_identify(domcap, &thecap);
        DEBUG_CAPOPS("Revoke request for cap:\n");
        DEBUG_PRINTCAP(&thecap);

        uint8_t rels;
        err = monitor_domcap_remote_relations(domcap.croot, domcap.cptr, domcap.level, 0, 0, &rels);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_domcap_remote_relations");
        }

        // check if any remote copies or descendants exist (which requires synchronization with the other core)
        if (rels & (RRELS_COPY_BIT | RRELS_DESC_BIT)) {
            DEBUG_CAPOPS("revoke: remote copies exist\n");

            uint8_t owner;
            err = monitor_get_domcap_owner(domcap, &owner);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_get_domcap_owner");
            }

            suspend->sync.base.base.type = AOS_RPC_REQUEST_TYPE_DISTCAP;
            suspend->sync.base.type      = AOS_RPC_DISTCAP_REVOKE_SYNC;
            suspend->sync.cap            = thecap;
            suspend->sync.owner          = owner;
            revoke_step_1(suspend);
            return false;
        } else {
            // no remote copies exist, we can mark the cap for revocation immediately
            err = monitor_revoke_mark_target(domcap.croot, domcap.cptr, domcap.level);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_revoke_mark_target");
            }
            delete_queue_wait(&suspend->qn, MKCLOSURE(queue_revoke_handler, suspend));
            return false;
        }
    } else if (basereq->type == AOS_RPC_DISTCAP_RETYPE_SYNC) {
        struct retype_sync *sync = (struct retype_sync *)basereq;
        DEBUG_CAPOPS("retype sync request\n");

        res->err = monitor_is_retypeable(&sync->cap, sync->offset, sync->objsize, sync->count);
        // if retype will succeed, update remote relations to set descendant bit
        if (err_is_ok(res->err)) {
            err = monitor_cap_create(tempcap, &sync->cap, sync->owner);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_cap_create");
            }
            err = monitor_remote_relations(tempcap, RRELS_DESC_BIT, RRELS_DESC_BIT, NULL);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_remote_relations");
            }
            err = monitor_nullify_cap(tempcap);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_nullify_cap");
            }
        }

        return true;
    } else if (basereq->type == AOS_RPC_DISTCAP_REVOKE_SYNC) {
        struct revoke_sync *sync = (struct revoke_sync *)basereq;
        DEBUG_CAPOPS("revoke sync request, owner = %d, core = %d\n", sync->owner,
                     disp_get_core_id());

        struct remote_revoke_suspend *suspend = malloc(sizeof(struct remote_revoke_suspend));
        suspend->rpc_data                     = *rpc_data;

        if (sync->owner != disp_get_core_id()) {
            err = monitor_revoke_mark_relations(&sync->cap);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_revoke_mark_relations");
            }
        } else {
            err = monitor_cap_create(tempcap, &sync->cap, sync->owner);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_cap_create");
            }
            err = monitor_revoke_mark_target(cap_root, get_cap_addr(tempcap),
                                             get_cap_level(tempcap));
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_revoke_mark_target");
            }
            err = monitor_nullify_cap(tempcap);

            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_nullify_cap");
            }
        }
        delete_queue_wait(&suspend->qn, MKCLOSURE(remote_queue_revoke_handler, suspend));
        res->err = SYS_ERR_OK;
        return false;
    } else if (basereq->type == AOS_RPC_DISTCAP_DELETE_SYNC) {
        struct delete_sync *sync = (struct delete_sync *)basereq;
        DEBUG_CAPOPS("delete sync request, owner = %d, core = %d\n", sync->owner,
                     disp_get_core_id());
        err = monitor_cap_create(tempcap, &sync->cap, sync->owner);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_cap_create");
        }
        // err = monitor_lock_cap(cap_root, get_cap_addr(tempcap), get_cap_level(tempcap));
        // if (err_is_fail(err)) {
        //     // TODO decide if we should wait or abort
        //     USER_PANIC_ERR(err, "monitor_lock_cap");
        // }
        uint8_t owner;
        err = monitor_get_cap_owner(cap_root, get_cap_addr(tempcap), get_cap_level(tempcap), &owner);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "monitor_get_cap_owner");
        }
        if (sync->op == DELETE_SYNC_MOVE_OWNER) {
            // check that the cap is owned by the other core
            assert(owner == 1 - disp_get_core_id());
            // set the owner of the cap to this core
            err = monitor_set_cap_owner(cap_root, get_cap_addr(tempcap), get_cap_level(tempcap),
                                        disp_get_core_id());
        } else if (sync->op == DELETE_SYNC_DELETE_FOREIGNS) {
            // check that the cap is owned by the other core
            assert(owner == 1 - disp_get_core_id());
            // delete all copies of the cap on this core
            err = monitor_delete_foreigns(tempcap);
        } else if (sync->op == DELETE_SYNC_LAST_NONOWNER) {
            // check that the cap is owned by this core
            assert(owner == disp_get_core_id());
            // unset RRELS_COPY_BIT
            err = monitor_remote_relations(tempcap, 0, RRELS_COPY_BIT, NULL);
        } else {
            USER_PANIC("Unknown delete sync operation");
        }
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "delete sync operation");
        }
        if (sync->op != DELETE_SYNC_DELETE_FOREIGNS) {
            err = monitor_nullify_cap(tempcap);
            if (err_is_fail(err)) {
                USER_PANIC_ERR(err, "monitor_nullify_cap");
            }
        }
        res->err = SYS_ERR_OK;
        return true;
    } else {
        DEBUG_WARN("Unknown distcap request type: %d\n", basereq->type);
        res->err = SYS_ERR_GUARD_MISMATCH;
        return true;
    }
}
