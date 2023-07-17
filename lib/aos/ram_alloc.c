/**
 * \file
 * \brief RAM allocator code (client-side)
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <aos/aos.h>
#include <aos/aos_rpc.h>
#include <aos/core_state.h>

/* remote (indirect through a channel) version of ram_alloc, for most domains */
static errval_t ram_alloc_remote(struct capref *ret, size_t size, size_t alignment)
{
    size_t retbytes;
    return aos_rpc_get_ram_cap(aos_rpc_get_memory_channel(), size, alignment, ret, &retbytes);
}


void ram_set_affinity(uint64_t minbase, uint64_t maxlimit)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();

    ram_alloc_state->default_minbase  = minbase;
    ram_alloc_state->default_maxlimit = maxlimit;
}

void ram_get_affinity(uint64_t *minbase, uint64_t *maxlimit)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();

    *minbase  = ram_alloc_state->default_minbase;
    *maxlimit = ram_alloc_state->default_maxlimit;
}

#define OBJSPERPAGE_CTE (1 << (BASE_PAGE_BITS - OBJBITS_CTE))

static errval_t ram_alloc_fixed(struct capref *ret, size_t size, size_t alignment)
{
    errval_t err;

    struct ram_alloc_state *state = get_ram_alloc_state();

    // we only serve multiple of base page size as allocations
    size = ROUND_UP(size, BASE_PAGE_SIZE);

    // check if we have space here, otherwise request more memory remotely
    if (state->early_alloc_offset + size > state->early_alloc_size) {
        // we're out of memory, try to allocate remotely
        return ram_alloc_remote(ret, size, alignment);
    }

    // we can only allocate an alignment of base page size
    if (alignment != BASE_PAGE_SIZE) {
        return LIB_ERR_RAM_ALLOC_MS_CONSTRAINTS;
    }

    // we're about todo a retype, this requires a slot, the slot allocator should have enough...
    err = slot_alloc(ret);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    // our mem shoudl be in the slot EARLYMEM
    struct capref mem_cap = { .cnode = cnode_task, .slot = TASKCN_SLOT_EARLYMEM };

    // performa retype of the new regino
    err = cap_retype(*ret, mem_cap, state->early_alloc_offset, ObjType_RAM, size);
    if (err_is_fail(err)) {
        slot_free(*ret);
        return err_push(err, LIB_ERR_CAP_RETYPE);
    }

    // adjust the offset with the new size
    state->early_alloc_offset += size;

    return SYS_ERR_OK;
}

#include <stdio.h>
#include <string.h>

/**
 * \brief Allocates aligned memory in the form of a RAM capability
 *
 * \param ret  Pointer to capref struct, filled-in with allocated cap location
 * \param size Amount of RAM to allocate, in bytes
 * \param alignment Alignment of RAM to allocate
 *              slot used for the cap in #ret, if any
 */
errval_t ram_alloc_aligned(struct capref *ret, size_t size, size_t alignment)
{
    errval_t err;

    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    if (ram_alloc_state->ram_alloc_func != NULL) {
        err = ram_alloc_state->ram_alloc_func(ret, size, alignment);
    } else {
        err = ram_alloc_fixed(ret, size, alignment);
    }

#if 0
    if(err_is_fail(err)) {
      DEBUG_ERR(err, "failed to allocate 2^%" PRIu32 " Bytes of RAM",
                size_bits);
      printf("callstack: %p %p %p %p\n",
	     __builtin_return_address(0),
	     __builtin_return_address(1),
	     __builtin_return_address(2),
	     __builtin_return_address(3));
    }
#endif
    return err;
}

/**
 * \brief Allocates memory in the form of a RAM capability
 *
 * \param ret Pointer to capref struct, filled-in with allocated cap location
 * \param size Amount of RAM to allocate, in bytes.
 *              slot used for the cap in #ret, if any
 */
errval_t ram_alloc(struct capref *ret, size_t size)
{
    return ram_alloc_aligned(ret, size, BASE_PAGE_SIZE);
}

errval_t ram_available(genpaddr_t *available, genpaddr_t *total)
{
    (void)available;
    (void)total;
    // TODO: Implement protocol to check amount of ram available with memserv
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * \brief Initialize the dispatcher specific state of ram_alloc
 */
errval_t ram_alloc_init(void)
{
    errval_t err;

    /* Initialize the ram_alloc_state */
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();

    thread_mutex_init(&ram_alloc_state->ram_alloc_lock);

    ram_alloc_state->mem_connect_done = false;
    ram_alloc_state->mem_connect_err  = 0;
    ram_alloc_state->ram_alloc_func   = NULL;
    ram_alloc_state->default_minbase  = 0;
    ram_alloc_state->default_maxlimit = 0;

    // now try to identify the supplied memory capability to get its size
    struct capref mem_cap = { .cnode = cnode_task, .slot = TASKCN_SLOT_EARLYMEM };

    struct capability cap;
    err = cap_direct_identify(mem_cap, &cap);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_IDENTIFY);
    }

    if (cap.type != ObjType_RAM || cap.u.ram.bytes < 1024 * 1024) {
        printf("Early memory cap is not a RAM cap or too small\n");
        return LIB_ERR_RAM_ALLOC;
    }

    ram_alloc_state->early_alloc_size   = cap.u.ram.bytes;
    ram_alloc_state->early_alloc_offset = 0;

    return SYS_ERR_OK;
}

/**
 * \brief Set ram_alloc to the default ram_alloc_remote or to a given function
 *
 * If local_allocator is NULL, it will be initialized to the default
 * remote allocator.
 */
void ram_alloc_set(ram_alloc_func_t local_allocator)
{
    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();

    ram_alloc_state->ram_alloc_func = local_allocator;
}
