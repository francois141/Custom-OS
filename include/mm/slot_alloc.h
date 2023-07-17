/**
 * \file
 * \brief Slot management for memory allocator
 */

/*
 * Copyright (c) 2007, 2008, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef MM_SLOT_ALLOC_H
#define MM_SLOT_ALLOC_H

#include <sys/cdefs.h>
#include <errors/errno.h>
#include <aos/caddr.h>
#include <aos/slot_alloc.h>

__BEGIN_DECLS

// forward declaration
struct mm;

/// Instance data for pre-allocating slot allocator for 2 level cspace
struct slot_prealloc {
    /// generic slot allocator data for this allocator
    struct slot_allocator a;

    /// Metadata for next place from which to allocate slots
    struct {
        /// the next capabilty slot to allocate in range 0..L2_CNODE_SLOTS
        struct capref cap;
        /// the number of free slots in the CNode
        cslot_t free;
    } meta[2];

    /// Which entry in meta array we are currently allocating from
    uint8_t current;

    /// flag whether the slot allocator is currently refilling
    bool is_refilling;

    /// RAM allocator instance to allocate space for new cnodes
    struct mm *mm;
};


/**
 * @brief initializes the preallocating slot allocator
 *
 * @param[in] this              prealloc slot allocator instance to be initialized
 * @param[in] initial_cnode     an empty L2 CNode to bootstrap the allocator
 * @param[in] ram_mm            mm instance to allocate memory for CNodes from
 *
 * @return errval_t SYS_ERR_OK on success, LIB_ERR_SLOT_ALLOC_INIT on failure
 *
 * Note: this function assumes that the supplied initial_cnode is in fact an L2 CNode and
 *       does not have any occupied slots.
 */
errval_t slot_prealloc_init(struct slot_prealloc *this, struct capref initial_cnode,
                            struct mm *ram_mm);


/**
 * @brief allocates a new capability slot using the given slot prealloc instance
 *
 * @param[in]  this  slot prealloc instance to allocate the capability slot from
 * @param[out] ret   returns the allocated capability slot (filled in by the function)
 *
 * @return SYS_ERR_OK on success, MM_ERR_SLOT* on failure
 *
 * Note: this function does not automatically refill the slot allocator.
 */
errval_t slot_prealloc_alloc(struct slot_prealloc *this, struct capref *ret);


/**
 * @brief  frees an allocated capability slot
 *
 * @param[in] this   slot prealloc instance to free the capability slot in
 * @param[in] cap    allocated, but empty capability slot to be freed
 *
 * @return SYS_ERR_OK on success, MM_ERR_SLOT* on failure
 *
 * Note: the freeing of slots is not really handled by the prealloc slot allocator
 */
errval_t slot_prealloc_free(struct slot_prealloc *this, struct capref cap);


/**
 * @brief refills the prealloc slot allocator
 *
 * @param[in] this  prealloc slot allocator instance to be refilled
 *
 * @return SYS_ERR_OK on success, MM_ERR_* or LIB_ERR_* on failure
 *
 * Note: refilling only happens when there is refilling needed, i.e., when the
 *       second CNode is used up.
 *
 * Note: it is safe to call this function while refilling is in progress.
 */
errval_t slot_prealloc_refill(struct slot_prealloc *this);


__END_DECLS

#endif // MM_SLOT_ALLOC_H
