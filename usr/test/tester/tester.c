/**
 * \file
 * \brief init process for child spawning
 */

/*
 * Copyright (c) 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include "aos/capabilities.h"
#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/ump_chan.h>
#include <proc_mgmt/proc_mgmt.h>

static struct capref get_capv(int i) {
    struct cnoderef rootcn_slot_capv = { .croot = CPTR_ROOTCN,
                                         .cnode = ROOTCN_SLOT_ADDR(ROOTCN_SLOT_CAPV),
                                         .level = CNODE_TYPE_OTHER };
    struct capref   input_frame_cap  = {
           .cnode = rootcn_slot_capv,
           .slot  = i,
    };
    return input_frame_cap;
}

static errval_t fill_cnode_get_ref(struct capref* cnode_slot, struct capref* frame_slot) {
    errval_t err = SYS_ERR_OK;

    struct cnoderef cnode_ref;
    err = cnode_create_l2(cnode_slot, &cnode_ref);
    if (err_is_fail(err))
        return err;


    struct capref frame_cap;
    for (int i = 0; i < 10; i++) {
        struct capref cap_pos = { .cnode = cnode_ref, .slot = i };
        if (i == 0 && frame_slot != NULL) {
            *frame_slot = cap_pos;
        }
        frame_alloc(&frame_cap, BASE_PAGE_SIZE, NULL);
        err = cap_copy(cap_pos, frame_cap);
        if (err_is_fail(err)) {
            return err;
        }
        err = cap_delete(frame_cap);
        if (err_is_fail(err)) {
            return err;
        }
    }

    return err;
}

static errval_t fill_cnode(struct capref *cnode_slot)
{
    return fill_cnode_get_ref(cnode_slot, NULL);
}

static errval_t create_nested_cnode(struct capref *cnode_slot, struct capref* inner_frame_slot) {
    errval_t err = SYS_ERR_OK;

    struct cnoderef cnode_ref;
    err = cnode_create_l2(cnode_slot, &cnode_ref);
    if (err_is_fail(err)) {
        return err;
    }

    struct capref temp_slot;
    err = fill_cnode_get_ref(&temp_slot, inner_frame_slot);
    if (err_is_fail(err)) {
        return err;
    }
    
    struct capref outer_dest = { .cnode = cnode_ref, .slot = 0 };
    err = cap_copy(outer_dest, temp_slot);
    if (err_is_fail(err)) {
        return err;
    }

    return cap_delete(temp_slot);
}

static errval_t create_ram_with_frame(struct capref* ram_cap, struct capref* frame_cap) {
    errval_t err = SYS_ERR_OK;

    err = ram_alloc(ram_cap, 2 * BASE_PAGE_SIZE);
    if (err_is_fail(err)) {
        return err;
    }

    err = slot_alloc(frame_cap);
    if (err_is_fail(err)) {
        return err;
    }
    return cap_retype(*frame_cap, *ram_cap, 0, ObjType_Frame, BASE_PAGE_SIZE);
}

static errval_t create_ram_with_desc_ram(struct capref* ram_parent, struct capref* ram_child) {
    errval_t err = SYS_ERR_OK;

    err = ram_alloc(ram_parent, 2 * BASE_PAGE_SIZE);
    if (err_is_fail(err)) {
        return err;
    }

    err = slot_alloc(ram_child);
    if (err_is_fail(err)) {
        return err;
    }
    return cap_retype(*ram_child, *ram_parent, 0, ObjType_RAM, BASE_PAGE_SIZE);
}

static errval_t test_nested_delete(void) {
    struct capref cnode_cap;
    struct capref inner_frame_cap;
    errval_t err = create_nested_cnode(&cnode_cap, &inner_frame_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "create_nested_cnode");
    }
    err = cap_delete(cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_delete");
    }

    return SYS_ERR_OK;
}

static errval_t test_nested_delete2(void) {
    struct capref cnode_cap;
    struct cnoderef cnode_ref;
    errval_t err = cnode_create_l2(&cnode_cap, &cnode_ref);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cnode_create_l2");
    }

    struct capref frame_cap;
    err = frame_alloc(&frame_cap, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "frame_alloc");
    }

    struct capref dest = {.cnode = cnode_ref, .slot = 0};
    err = cap_copy(dest, frame_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_copy");
    }
    err = cap_delete(frame_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_delete");
    }

    err = cap_delete(cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_delete");
    }

    struct capability thecap;
    err = cap_direct_identify(dest, &thecap);

    if (err_no(err) != SYS_ERR_CNODE_NOT_FOUND) {
        USER_PANIC_ERR(err, "cap_direct_identify: cnode not deleted");
    }

    return SYS_ERR_OK;
}

static errval_t test_distcap_delete(void)
{
    errval_t      err = SYS_ERR_OK;
    struct capref cnode_cap;
    err = fill_cnode(&cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "fill_cnode");
    }

    struct capref cnode_copy;
    err = slot_alloc(&cnode_copy);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "slot_alloc");
    }
    err = cap_copy(cnode_copy, cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_copy");
    }

    err = cap_delete(cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_delete");
    }

    err = cap_delete(cnode_copy);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_delete");
    }

    err = fill_cnode(&cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "fill_cnode");
    }

    err = cap_copy(cnode_copy, cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_copy");
    }

    err = cap_revoke(cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_revoke");
    }

    struct capability thecap;
    err = cap_direct_identify(cnode_copy, &thecap);
    if (err_no(err) != SYS_ERR_CAP_NOT_FOUND) {
        USER_PANIC("cap_direct_identify: copy not deleted");
    }

    return SYS_ERR_OK;
}

static errval_t remote_test_parent(void)
{
    errval_t    err = SYS_ERR_OK;
    domainid_t  id;
    const char *child_argv[] = { "tester", "child", NULL };

    // FRAME 1

    struct capref frame_cap;
    err = frame_alloc(&frame_cap, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "frame_alloc");
    }

    // CNODE

    struct capref cnode_cap;
    err = fill_cnode(&cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "fill_cnode");
    }

    // FRAME 2
    struct capref frame_cap2;
    err = frame_alloc(&frame_cap2, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "frame_alloc");
    }

    // RAM CAP WITH DESCENDANT 1

    struct capref ram_cap;
    struct capref desc_frame_cap;
    err = create_ram_with_frame(&ram_cap, &desc_frame_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "create_cnode_with_frame");
    }

    // RAM CAP WITH DESCENDANT 2

    struct capref ram_cap_parent;
    struct capref ram_cap_child;
    err = create_ram_with_desc_ram(&ram_cap_parent, &ram_cap_child);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "create_cnode_with_frame");
    }


    struct capref capv[] = { frame_cap, cnode_cap, frame_cap2, ram_cap, ram_cap_parent };

    debug_printf("spawn child\n");
    err = proc_mgmt_spawn_with_caps(2, child_argv, sizeof(capv) / sizeof(capv[0]), capv, 1 - disp_get_core_id(), &id);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "proc_mgmt_spawn_program");
    }

    err = cap_delete(frame_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_delete");
    }

    err = cap_delete(cnode_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_delete");
    }

    int status;
    err = proc_mgmt_wait(id, &status);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "proc_mgmt_wait");
    } else if (status != EXIT_SUCCESS) {
        USER_PANIC("child exited with error");
    }

    // only check after child has exited, as the child is the one performing the revocation
    struct capability thecap;
    err = cap_direct_identify(desc_frame_cap, &thecap);
    if (err_no(err) != SYS_ERR_CAP_NOT_FOUND) {
        USER_PANIC("cap_direct_identify: copy not deleted");
    }

    return err;
}

static void remote_test_child(void) {
    errval_t err = SYS_ERR_OK;

    debug_printf("== Child Remote Begin ==\n");
    debug_print_cap_at_capref(get_capv(0));
    debug_print_cap_at_capref(get_capv(1));
    debug_print_cap_at_capref(get_capv(2));
    debug_print_cap_at_capref(get_capv(3));
    debug_print_cap_at_capref(get_capv(4));

    // check the frame which should have been transferred to us
    struct capability thecap;
    err = cap_direct_identify(get_capv(0), &thecap);
    if (err_is_fail(err) || thecap.type != ObjType_Frame) {
        USER_PANIC("frame ownership not transfered");
    }

    // check that the cnode copy with which we were spawned was deleted
    err = cap_direct_identify(get_capv(1), &thecap);
    if (err_no(err) != SYS_ERR_CAP_NOT_FOUND) {
        USER_PANIC_ERR(err, "CNode copy on other core not deleted");
    }

    // revoke a shared Frame
    err = cap_revoke(get_capv(2));
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_delete");
    }

    // revoke a RAM capability with descendants on other core
    err = cap_revoke(get_capv(3));
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_revoke");
    }

    // attempt an invalid retype operation

    struct capref ram_child2_slot;
    err = slot_alloc(&ram_child2_slot);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "slot_alloc");
    }

    debug_printf("attempting invalid retype\n");
    err = cap_retype(ram_child2_slot, get_capv(4), 0, ObjType_RAM, BASE_PAGE_SIZE);
    if (err_is_ok(err)) {
        USER_PANIC("cap_retype should have failed");
    }

    // now, attempt a valid retype
    err = cap_retype(ram_child2_slot, get_capv(4), BASE_PAGE_SIZE, ObjType_RAM, BASE_PAGE_SIZE);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "cap_retype should succeed");
    }

    struct capability ram_cap_data;
    err = cap_direct_identify(ram_child2_slot, &ram_cap_data);
    if (err_is_fail(err) || ram_cap_data.type != ObjType_RAM) {
        USER_PANIC_ERR(err, "cap_direct_identify");
    }

    debug_printf("== Child Remote END ==\n");
}

static void test_remote_ram_alloc(void) {
    errval_t err = SYS_ERR_OK;
    struct capref frame_cap;
    err = frame_alloc(&frame_cap, 1 << 20, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "ram_alloc");
    }

    debug_print_cap_at_capref(frame_cap);

    void* buf;
    err = paging_map_frame(get_current_paging_state(), &buf, 1 << 20, frame_cap);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "paging_map_frame");
    }

    *(char*)buf = 'a';
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    errval_t err = SYS_ERR_OK;

    debug_printf("Hello from tester!\n");

    test_remote_ram_alloc();
    test_distcap_delete();
    test_nested_delete();
    test_nested_delete2();

    if (argc > 1) {
        remote_test_child();
    } else {
        remote_test_parent();
    }

    return err_is_fail(err) ? EXIT_FAILURE : EXIT_SUCCESS;
}
