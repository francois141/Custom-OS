/**
 * \file
 * \brief Slot management for the memory allocator.
 */

/*
 * Copyright (c) 2007, 2008, 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <aos/aos.h>
#include <mm/mm.h>
#include <mm/slot_alloc.h>


/**
 * @brief helper function to refill the root slot allocator
 *
 * @param[in]  st    void pointer to the mm instance
 * @param[in]  size  the size to allocate
 * @param[out] cap   returns the capref of the allocated RAM.
 *
 * @return SYS_ERR_OK on success, MM_ERR_* on failure
 */
static errval_t rootcn_alloc(void *st, size_t reqsize, struct capref *cap)
{
    return mm_alloc((struct mm *)st, reqsize, cap);
}


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
errval_t slot_prealloc_refill(struct slot_prealloc *this)
{
    errval_t err;

    /* If we are already refilling, just return */
    if (this->is_refilling) {
        return SYS_ERR_OK;
    }

    /* refill the next CNode if we've used one up */
    uint8_t refill = !this->current;
    assert(refill == 0 || refill == 1);


    /* if the next CNode is still full, we do not have to do anything */
    if (this->meta[refill].free == L2_CNODE_SLOTS) {
        return SYS_ERR_OK;
    }

    this->is_refilling = true;

    /*
     * explicitly allocate RAM for the next CNode, as we need to control the
     * allocation of a slot in the L1 CNode.
     */
    struct capref ram_cap = NULL_CAP;
    err = mm_alloc(this->mm, OBJSIZE_L2CNODE, &ram_cap);
    if (err_is_fail(err)) {
        err = err_push(err, MM_ERR_SLOT_MM_ALLOC);
        goto out;
    }

    /*
     * we'll do a retype to an L2 CNode, so we need to allocate a slot in the root
     * CNode. This in turn may fail, and we need to refill the root slot allocator,
     * that in turn expands the L1 CNode.
     */
    struct capref cnode_cap;
    err = slot_alloc_root(&cnode_cap);
    if (err_no(err) == LIB_ERR_SLOT_ALLOC_NO_SPACE) {
        /* we got an out of memory error, try refilling the root slot allocator */
        err = root_slot_allocator_refill(rootcn_alloc, this->mm);
        if (err_is_fail(err)) {
            err = err_push(err, LIB_ERR_ROOTSA_RESIZE);
            goto out;
        }
        /* refilling succeeded, so we can try again with allocating a slot in the L1 CNode */
        err = slot_alloc_root(&cnode_cap);
    }

    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_SLOT_ALLOC);
        goto out;
    }

    /*
     * we got the memory for the next CNode, and we got a slot in the root CNode,
     * so we can now savely create the new L2 CNode for this slot allocator
     */
    err = cnode_create_from_mem(cnode_cap, ram_cap, ObjType_L2CNode,
                                &this->meta[refill].cap.cnode, L2_CNODE_SLOTS);
    if (err_is_fail(err)) {
        // free the allocated root CNode slot again
        slot_free(cnode_cap);
        err = err_push(err, LIB_ERR_CNODE_CREATE);
        goto out;
    }

    // Set the metadata
    this->meta[refill].cap.slot = 0;
    this->meta[refill].free = L2_CNODE_SLOTS;
    this->a.space += L2_CNODE_SLOTS;

out:
    /* free the allocated RAM cap if there was an error and we have allocated a ramcap */
    if (err_is_fail(err) && !capref_is_null(ram_cap)) {
        err = mm_free(this->mm, ram_cap);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "mm_free failed");
        }
    }
    this->is_refilling = false;
    return err;
}

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
errval_t slot_prealloc_alloc(struct slot_prealloc *this, struct capref *ret)
{
    /* the amount of global space should always match the of the two CNodes */
    assert(this->a.space >= this->meta[0].free + this->meta[1].free);

    /* we're out of slots, return error */
    if (this->a.space == 0) {
        return MM_ERR_SLOT_NOSLOTS;
    }

    /* check if we need to flip the current allocating CNode */
    if (this->meta[this->current].free == 0) {
        // debug_printf("slot_prealloc: switching cnodes %d->%d\n", this->current,
        // !this->current);
        this->current = !this->current;
    }

    assert(this->current == 0 || this->current == 1);
    assert(this->meta[this->current].free > 0);

    /* set the return capability */
    *ret = this->meta[this->current].cap;

    /* update the slot in the capability */
    this->meta[this->current].cap.slot++;

    /* update free space tracking */
    this->meta[this->current].free--;
    this->a.space--;

    return SYS_ERR_OK;
}


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
errval_t slot_prealloc_free(struct slot_prealloc *this, struct capref cap)
{
    /* increment the slot here, as the slot has been incremented in slot_prealloc_alloc */
    cap.slot += 1;

    /* we can only free the last allocated slot again */
    if (capcmp(this->meta[this->current].cap, cap)) {
        this->meta[this->current].free++;
        this->meta[this->current].cap.slot--;
        this->a.space++;
    } else {
        // debug_printf("WARNING: leaking capablity slot (caller: %p)\n",
        //              __builtin_return_address(0));
    }
    return SYS_ERR_OK;
}


/**
 * @brief a wrapper around slot_prealloc_free to handle the generic calls
 *
 * @param[in]  ca    pointer to the generic part of the slot_prealloc instance
 * @param[out] cap   capability to be freed
 *
 * @return SYS_ERR_OK on success, LIB_ERR_SLOT_ALLOC* on failure
 */
static errval_t slot_prealloc_alloc_generic(struct slot_allocator *ca, struct capref *cap)
{
    struct slot_prealloc *this = (struct slot_prealloc *)ca;
    return slot_prealloc_alloc(this, cap);
}


/**
 * @brief a wrapper around slot_prealloc_free to handle the generic calls
 *
 * @param[in] ca    pointer to the generic part of the slot_prealloc instance
 * @param[in] cap   capability to be freed
 *
 * @return SYS_ERR_OK on success, LIB_ERR_SLOT_ALLOC* on failure
 */
static errval_t slot_prealloc_free_generic(struct slot_allocator *ca, struct capref cap)
{
    struct slot_prealloc *this = (struct slot_prealloc *)ca;
    return slot_prealloc_free(this, cap);
}


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
                            struct mm *ram_mm)
{
    if (capref_is_null(initial_cnode) || ram_mm == NULL) {
        return LIB_ERR_SLOT_ALLOC_INIT;
    }

    // initialize the specific parts of the slot allocator
    this->current = 0;
    this->meta[0].cap = initial_cnode;
    this->meta[0].free = L2_CNODE_SLOTS;
    this->meta[1].free = 0;
    this->is_refilling = false;
    this->mm = ram_mm;

    // initialize the generic part of the slot allocator
    this->a.alloc = slot_prealloc_alloc_generic;
    this->a.free = slot_prealloc_free_generic;
    this->a.nslots = L2_CNODE_SLOTS;
    this->a.space = L2_CNODE_SLOTS;
    thread_mutex_init(&this->a.mutex);

    return SYS_ERR_OK;
}