/*
 * Copyright (c) 2020, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include "mem_alloc.h"
#include <mm/mm.h>
#include <aos/paging.h>
#include <grading/grading.h>

/// MM allocator instance data
static struct mm aos_mm;

/// slot allocator instance used by MM
static struct slot_prealloc init_slot_alloc;

/**
 * @brief wrapper around the slot allocator refill function
 *
 * @param ca  slot allocator instance to be refilled
 *
 * @return SYS_ERR_OK on success, MM_ERR_* or LIB_ERR_* on failure
 */
static errval_t mm_slot_alloc_refill(struct slot_allocator *ca)
{
    struct slot_prealloc *this = (struct slot_prealloc *)ca;
    return slot_prealloc_refill(this);
}


/**
 * @brief initializes the ram allocator (MM)
 *
 * @return SYS_ERR_OK on success, MM_ERR_* or LIB_ERR_* on failure
 */
static inline errval_t initialize_ram_allocator(void)
{
    errval_t err;

    /*
     * Initialize the slot allocator for the memory manager.
     * Note: we're using the slot_prealloc here because we need to be careful
     * when we need to refill the slot allocator to avoid circular calls between
     * mm_alloc() and slot_alloc().
     *
     * For this we use a specific L2 CNode, we construc the capref for it
     */
    struct capref cnode_cap = {
        .cnode = {
            .croot = CPTR_ROOTCN,
            .cnode = ROOTCN_SLOT_ADDR(ROOTCN_SLOT_SLOT_ALLOC0),
            .level = CNODE_TYPE_OTHER,
        },
        .slot = 0,
    };

    // initialize the slot allocator and pass in a reference to the MM
    err = slot_prealloc_init(&init_slot_alloc, cnode_cap, &aos_mm);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC_INIT);
    }

    // Initialize the MM instance with the slot allocator.
    err = mm_init(&aos_mm, ObjType_RAM, &init_slot_alloc.a, mm_slot_alloc_refill, NULL, 0);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Can't initalize the memory manager.");
    }

    return SYS_ERR_OK;
}

/**
 * @brief initializes the local memory allocator, adding memory from the bootinfo
 *
 * @param[in] bi  bootinfo structure containing the memory regions
 *
 * @returns SYS_ERR_OK on success, LIB_ERR_* on failure
 */
errval_t initialize_ram_alloc(struct bootinfo *bi)
{
    errval_t err;

    /* initialize the RAM allocator instance */
    err = initialize_ram_allocator();
    if (err_is_fail(err)) {
        return err;
    }

    /* construct the capref to the L2 CNode that contains capabilities to the RAM regions */
    struct capref mem_cap = {
        .cnode = cnode_memory,
        .slot  = 0,
    };

    /* walk the bootinfo structure and add all RAM caps to allocator */
    for (size_t i = 0; i < bi->regions_length; i++) {
        /* only add RAM regions that are marked as "available" */
        if (bi->regions[i].mr_type == RegionType_Empty) {
            struct capability c;
            err = cap_direct_identify(mem_cap, &c);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "failed to get the frame info\n");
                mem_cap.slot++;
                continue;
            }

            // some santity checks
            assert(c.type == ObjType_RAM);
            assert(c.u.ram.base == bi->regions[i].mr_base);
            assert(c.u.ram.bytes == bi->regions[i].mr_bytes);



            err = mm_add(&aos_mm, mem_cap);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "Warning: adding RAM region %d (%p/%zu) FAILED", i,
                          bi->regions[i].mr_base, bi->regions[i].mr_bytes);
            }

            mem_cap.slot++;
        }
    }

    debug_printf("Added %" PRIu64 " MB of physical memory.\n",
                 mm_mem_available(&aos_mm) / 1024 / 1024);

    // Finally, we can initialize the generic RAM allocator to use our local allocator
    ram_alloc_set(aos_ram_alloc_aligned);

    // Calling the grading tests.
    // Note: do not remove the call to the grading tests. If you decide to move the
    //       memserver to a separate process, please ensure linking with the grading
    //       library and calling the following function.
    grading_test_mm(&aos_mm);

    return SYS_ERR_OK;
}

errval_t initialize_ram_alloc_cap(struct capref cap) {
    errval_t err = SYS_ERR_OK;
    /* initialize the RAM allocator instance */
    err = initialize_ram_allocator();
    if (err_is_fail(err)) {
        return err;
    }

    err = mm_add(&aos_mm, cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Warning: adding RAM region FAILED");
        return err;
    }

    // Finally, we can initialize the generic RAM allocator to use our local allocator
    ram_alloc_set(aos_ram_alloc_aligned);
    return err;
}

errval_t initialize_ram_alloc_range(struct bootinfo *bi, genpaddr_t ram_base, gensize_t ram_size){
    errval_t err;

    /* initialize the RAM allocator instance */
    err = initialize_ram_allocator();
    if (err_is_fail(err)) {
        return err;
    }

    /* construct the capref to the L2 CNode that contains capabilities to the RAM regions */
    struct capref mem_cap = {
        .cnode = cnode_memory,
        .slot  = 0,
    };

    /* walk the bootinfo structure and add all RAM caps to allocator */
    for (size_t i = 0; i < bi->regions_length; i++) {
        /* only add RAM regions that are marked as "available" */
        if (bi->regions[i].mr_type == RegionType_Empty) {
            struct capability c;
            err = cap_direct_identify(mem_cap, &c);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "failed to get the frame info\n");
                mem_cap.slot++;
                continue;
            }

            // some santity checks
            assert(c.type == ObjType_RAM);
            assert(c.u.ram.base == bi->regions[i].mr_base);
            assert(c.u.ram.bytes == bi->regions[i].mr_bytes);
            if(ram_base < c.u.ram.base || ram_base >= c.u.ram.base + c.u.ram.bytes){
                // not for us
                mem_cap.slot++;
                continue;
            }
            assert(ram_base + ram_size <= c.u.ram.base + c.u.ram.bytes);

            // only pick the right location
            struct capref ram_range;
            err = slot_alloc(&ram_range);
            if(err_is_fail(err))
                return err;

            err = cap_retype(ram_range, mem_cap, ram_base - c.u.ram.base, ObjType_RAM, ram_size);
            if(err_is_fail(err))
                return err;

            err = mm_add(&aos_mm, ram_range);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "Warning: adding RAM region %d (%p/%zu) FAILED", i,
                          bi->regions[i].mr_base, bi->regions[i].mr_bytes);
            }

            mem_cap.slot++;
        }
    }

    debug_printf("Added %" PRIu64 " MB of physical memory.\n",
                 mm_mem_available(&aos_mm) / 1024 / 1024);

    // Finally, we can initialize the generic RAM allocator to use our local allocator
    ram_alloc_set(aos_ram_alloc_aligned);

    return SYS_ERR_OK;
}


/**
 * @brief allocates physical memory with the given size and alignment requirements
 *
 * @param[out] cap        returns the allocated capabilty
 * @param[in]  size       size of the allocation request in bytes
 * @param[in]  alignment  alignment constraint of the allocation request
 *
 * @return SYS_ERR_OK on success, MM_ERR_* on failure
 */
errval_t aos_ram_alloc_aligned(struct capref *cap, size_t size, size_t alignment)
{
    return mm_alloc_aligned(&aos_mm, size, alignment, cap);
}


/**
 * @brief frees previously allocated physical memoyr
 *
 * @param cap  capabilty to the memory that is to be freed
 *
 * @return SYS_ERR_OK on success, MM_ERR_* on failure
 */
errval_t aos_ram_free(struct capref cap)
{
    return mm_free(&aos_mm, cap);
}


