/**
 * \file
 * \brief A library for managing physical memory (i.e., caps)
 */

/*
 * Copyright (c) 2008, 2011, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <string.h>
#include <aos/debug.h>
#include <aos/solution.h>
#include <mm/mm.h>

#include <aos/threads.h>
#include <aos/paging_types.h>

#define STATIC_SLAB_BUF_SIZE (4 * PAGE_SIZE)

static char static_slab_buf[STATIC_SLAB_BUF_SIZE];
static int slab_buf_used = 0;

#define ALIGN_TO(x, align) ((((x) - 1) | (align - 1)) + 1)

bool mm_mutex_init = false;
struct thread_mutex mm_mutex;

static errval_t tracked_slab_alloc(struct mm* mm, void** buf) {
    thread_mutex_lock_nested(&mm_mutex);

    void* res = slab_alloc(&mm->slab);
    if (!res) {
        thread_mutex_unlock(&mm_mutex);
        return MM_ERR_SLAB_ALLOC_FAIL;
    }
    *buf = res;
    mm->slab_free_slots -= 1;

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}

static errval_t slab_try_refill(struct mm* mm) {
    thread_mutex_lock_nested(&mm_mutex);

    if (mm->refilling_slab) {
        thread_mutex_unlock(&mm_mutex);
        return SYS_ERR_OK;
    }
    mm->refilling_slab = true;
    errval_t err = SYS_ERR_OK;
    if (mm->slab_free_slots <= 20) {
        err = slab_refill_pages(&mm->slab, 4 * PAGE_SIZE);
        mm->slab_free_slots = slab_freecount(&mm->slab);
        if (err_is_fail(err)) {
            err = err_push(err, MM_ERR_SLAB_ALLOC_FAIL);
            DEBUG_ERR(err, "slab refill");
        }
    }
    mm->refilling_slab = false;

    thread_mutex_unlock(&mm_mutex);
    return err;
}

void mm_log(struct mm* mm) {
    thread_mutex_lock_nested(&mm_mutex);

    debug_printf("=== LOG BEGIN ===\n");
    for (struct region_info* reg = mm->region_head; reg != NULL; reg = reg->next) {
        debug_printf("region: %lx, %lx, %lx\n", 
            reg->reg_addr, 
            reg->reg_addr + reg->reg_size, 
            reg->reg_size
        );
        for (struct block_info* block = reg->free_head; block != NULL; block = block->next) {
            debug_printf("  block: %lx, %lx, %lx\n", 
                block->block_addr, 
                block->block_addr + block->block_size, 
                block->block_size
            );
        }
    }
    debug_printf("===  LOG END  ===\n");

    thread_mutex_unlock(&mm_mutex);
}


/**
 * @brief initializes the memory manager instance
 *
 * @param[in] mm        memory manager instance to initialize
 * @param[in] objtype   type of the capabilities stored in the memory manager
 * @param[in] ca        capability slot allocator to be used
 * @param[in] refill    slot allocator refill function to be used
 * @param[in] slab_buf  initial buffer space for slab allocators
 * @param[in] slab_sz   size of the initial slab buffer
 *
 * @return error value indicating success or failure
 *  - @retval SYS_ERR_OK if the memory manager was successfully initialized
 */
errval_t mm_init(struct mm *mm, enum objtype objtype, struct slot_allocator *ca,
                 slot_alloc_refill_fn_t refill, void *slab_buf, size_t slab_sz)
{
    // XXX mm_init is NOT thread safe.
    // make compiler happy about unused parameters
    debug_printf("initializing mm\n");

    // initialize the mutex & set it to be recursive
    if (!mm_mutex_init) {
        thread_mutex_init(&mm_mutex);
        mm_mutex_init = true;
    }

    mm->ca = ca;
    mm->refill = refill;
    mm->objtype = objtype;

    slab_init(&mm->slab, SLAB_BLOCK_SIZE, NULL);
    if (slab_buf != NULL) {
        slab_grow(&mm->slab, slab_buf, slab_sz);
    } else if (!slab_buf_used) {
        slab_grow(&mm->slab, static_slab_buf, STATIC_SLAB_BUF_SIZE);
    } else {
        USER_PANIC_ERR(MM_ERR_OUT_OF_MEMORY, "no memory for slab alloc");
    }

    mm->slab_free_slots = slab_freecount(&mm->slab);

    mm->region_head = NULL;

    mm->mem_available = 0;
    mm->mem_total = 0;

    mm->refilling_slot = false;
    mm->refilling_slab = false;

    return SYS_ERR_OK;
}


/**
 * @brief destroys an mm instance
 *
 * @param[in] mm  memory manager instance to be freed
 *
 * @return error value indicating success or failure
 *  - @retval SYS_ERR_OK if the memory manager was successfully destroyed
 *
 * @note: does not free the mm object itself
 *
 * @note: This function is here for completeness. Think about how you would implement it.
 *        It's implementation is not required.
 */
errval_t mm_destroy(struct mm *mm)
{
    (void) mm;

    thread_mutex_lock_nested(&mm_mutex);

    USER_PANIC_ERR(LIB_ERR_NOT_IMPLEMENTED, "hit mm destroy");
    UNIMPLEMENTED();

    thread_mutex_unlock(&mm_mutex);
    return LIB_ERR_NOT_IMPLEMENTED;
}


/**
 * @brief adds new memory resources to the memory manager represented by the capability
 *
 * @param[in] mm   memory manager instance to add resources to
 * @param[in] cap  memory resources to be added to the memory manager instance
 *
 * @return error value indicating the success of the operation
 *  - @retval SYS_ERR_OK              on success
 *  - @retval MM_ERR_CAP_INVALID      if the supplied capability is invalid (size, alignment)
 *  - @retval MM_ERR_CAP_TYPE         if the supplied capability is not of the expected type
 *  - @retval MM_ERR_ALREADY_PRESENT  if the supplied memory is already managed by this allocator
 *  - @retval MM_ERR_SLAB_ALLOC_FAIL  if the memory for the new node's meta data could not be allocate
 *
 * @note: the memory manager instance must be initialized before calling this function.
 *
 * @note: the function transfers ownership of the capability to the memory manager
 *
 * @note: to return allocated memory to the allocator, see mm_free()
 */
errval_t mm_add(struct mm *mm, struct capref cap)
{
    thread_mutex_lock_nested(&mm_mutex);
    errval_t err;

    struct capability thecap;
    err = cap_direct_identify(cap, &thecap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "identify capability");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    void* buf = NULL;
    err = tracked_slab_alloc(mm, &buf);
    struct region_info* reginfo = (struct region_info*)buf;

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "slab alloc");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }
    mm->slab_free_slots -= 1;

    buf = NULL;
    err = tracked_slab_alloc(mm, &buf);
    struct block_info* blockinfo = (struct block_info*)buf;

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "slab alloc");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }
    mm->slab_free_slots -= 1;

    reginfo->next = mm->region_head;
    mm->region_head = reginfo;

    reginfo->cap = cap;
    reginfo->reg_addr = thecap.u.ram.base;
    reginfo->reg_size = thecap.u.ram.bytes;
    reginfo->free_head = blockinfo;

    blockinfo->block_addr = reginfo->reg_addr;
    blockinfo->block_size = reginfo->reg_size;
    blockinfo->next = NULL;

    mm->mem_total += reginfo->reg_size;
    mm->mem_available += reginfo->reg_size;

    err = slab_try_refill(mm);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "slab refill");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}


/**
 * @brief allocates memory with the requested size and alignment
 *
 * @param[in]  mm         memory manager instance to allocate from
 * @param[in]  size       minimum requested size of the memory region to allocate
 * @param[in]  alignment  minimum alignment requirement for the allocation
 * @param[out] retcap     returns the capability to the allocated memory
 *
 * @return error value indicating the success of the operation
 *  - @retval SYS_ERR_OK                on success
 *  - @retval MM_ERR_BAD_ALIGNMENT      if the requested alignment is not a power of two
 *  - @retval MM_ERR_OUT_OF_MEMORY      if there is not enough memory to satisfy the request
 *  - @retval MM_ERR_ALLOC_CONSTRAINTS  if there is memory, but the constraints are too tight
 *  - @retval MM_ERR_SLOT_ALLOC_FAIL    failed to allocate slot for new capability
 *  - @retval MM_ERR_SLAB_ALLOC_FAIL    failed to allocate memory for meta data
 *
 * @note The function allocates memory and returns a capability to it back to the caller.
 * The size of the returned capability is a multiple of BASE_PAGE_SIZE. Alignment requests
 * must be a power of two starting from BASE_PAGE_SIZE.
 *
 * @note The returned ownership of the capability is transferred to the caller.
 */
errval_t mm_alloc_aligned(struct mm *mm, size_t size, size_t alignment, struct capref *retcap)
{
    return mm_alloc_from_range_aligned(mm, 0, -1, size, alignment, retcap);
}


/**
 * @brief allocates memory of a given size within a given base-limit range (EXTRA CHALLENGE)
 *
 * @param[in]  mm         memory manager instance to allocate from
 * @param[in]  base       minimum requested address of the memory region to allocate
 * @param[in]  limit      maximum requested address of the memory region to allocate
 * @param[in]  size       minimum requested size of the memory region to allocate
 * @param[in]  alignment  minimum alignment requirement for the allocation
 * @param[out] retcap     returns the capability to the allocated memory
 *
 * @return error value indicating the success of the operation
 *  - @retval SYS_ERR_OK                on success
 *  - @retval MM_ERR_BAD_ALIGNMENT      if the requested alignment is not a power of two
 *  - @retval MM_ERR_OUT_OF_MEMORY      if there is not enough memory to satisfy the request
 *  - @retval MM_ERR_ALLOC_CONSTRAINTS  if there is memory, but the constraints are too tight
 *  - @retval MM_ERR_OUT_OF_BOUNDS      if the supplied range is not within the allocator's range
 *  - @retval MM_ERR_SLOT_ALLOC_FAIL    failed to allocate slot for new capability
 *  - @retval MM_ERR_SLAB_ALLOC_FAIL    failed to allocate memory for meta data
 *
 * The returned capability should be within [base, limit] i.e., base <= cap.base,
 * and cap.base + cap.size <= limit.
 *
 * The requested alignment should be a power two of at least BASE_PAGE_SIZE.
 */
errval_t mm_alloc_from_range_aligned(struct mm *mm, size_t base, size_t limit, size_t size,
                                     size_t alignment, struct capref *retcap)
{
    thread_mutex_lock_nested(&mm_mutex);

    errval_t err = SYS_ERR_OK;

    if (size == 0) {
        debug_printf("zero-sized allocation, skipping\n");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }
    
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        err = MM_ERR_BAD_ALIGNMENT;
        DEBUG_ERR(err, "bad aligment for mm_alloc_aligned (not a power of two)");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    if (mm->mem_available < size) {
        err = MM_ERR_OUT_OF_MEMORY;
        DEBUG_ERR(err, "not enough memory");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    // 1. find suitable block
    struct region_info* region = NULL;
    struct block_info* prev = NULL;
    struct block_info* curr = NULL;
    for (region = mm->region_head; region != NULL; region = region->next) {
        prev = NULL;
        for (curr = region->free_head; curr != NULL; curr = curr->next) {
            uintptr_t block_end = curr->block_addr + curr->block_size;
            uintptr_t aligned_addr = ALIGN_TO(curr->block_addr, alignment);
            if (aligned_addr <= block_end && base <= aligned_addr && aligned_addr <= limit) {
                size_t real_size = block_end - aligned_addr;
                if (real_size >= size) {
                    goto done;
                }
            }
            prev = curr;
        }
    }
done:

    if (region == NULL) {
        err = MM_ERR_ALLOC_CONSTRAINTS;
        DEBUG_ERR(err, "mm_alloc_aligned could not find block");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    // 2.1 allocate cap return slot

    err = mm->ca->alloc(mm->ca, retcap);

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "slot alloc could not get slot");
        err = err_push(err, MM_ERR_SLOT_ALLOC_FAIL);

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    // 2.2 split off new capability using cap_retype
    // TODO think about smarter ways to split the block to minimize fragmentation
    uintptr_t aligned_addr = ALIGN_TO(curr->block_addr, alignment);

    err = cap_retype(*retcap, region->cap, aligned_addr - region->reg_addr, mm->objtype, size);

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "retype operation failed");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }
    
    // 3. create new block in alignment hole

    // non-aligned split, create new block
    // if split _was_ aligned, there is no need to create new block
    if (aligned_addr != curr->block_addr) {
        struct block_info* block;
        err = tracked_slab_alloc(mm, (void**)&block);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "alignment hole slab alloc");

            thread_mutex_unlock(&mm_mutex);
            return err;
        }
        block->block_addr = curr->block_addr;
        block->block_size = aligned_addr - curr->block_addr;

        block->next = curr;
        if (prev != NULL) {
            prev->next = block;
        } else {
            region->free_head = block;
        }
        // newly created block is now previous of current
        prev = block;
    }

    // 4. resize block to exclude data just allocated
    size_t remaining_size = (curr->block_addr + curr->block_size) - (aligned_addr + size);
    if (remaining_size > 0) {
        curr->block_addr = aligned_addr + size;
        curr->block_size = remaining_size;
    } else { // remaining_size == 0
        // used up available space exactly
        if (prev != NULL) {
            prev->next = curr->next;
        } else {
            region->free_head = curr->next;
        }
        slab_free(&mm->slab, curr);
    }

    // 5. update bookkeeping
    mm->mem_available -= size;

    // attempt to refill slot allocator
    if (!mm->refilling_slot && mm->ca->space <= 20) {
        mm->refilling_slot = true;;
        mm->refill(mm->ca);
        mm->refilling_slot = false;
    }

    slab_try_refill(mm);

    // 6. return split-off capability

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}

/**
 * @brief frees a previously allocated memory by returning it to the memory manager
 *
 * @param[in] mm   the memory manager instance to return the freed memory to
 * @param[in] cap  capability of the memory to be freed
 *
 * @return error value indicating the success of the operation
 *   - @retval SYS_ERR_OK            The memory was successfully freed and added to the allocator
 *   - @retval MM_ERR_NOT_FOUND      The memory was not allocated by this allocator
 *   - @retval MM_ERR_DOUBLE_FREE    The (parts of) memory region has already been freed
 *   - @retval MM_ERR_CAP_TYPE       The capability is not of the correct type
 *   - @retval MM_ERR_CAP_INVALID    The supplied cabability was invalid or does not exist.
 *
 * @pre  The function assumes that the capability passed in is no where else used.
 *       It is the only copy and there are no descendants of it. Calling functions need
 *       to ensure this. Later allocations can safely hand out the freed capability again.
 *
 * @note The memory to be freed must have been added to the `mm` instance and it must have been
 *       allocated before, otherwise an error is to be returned.
 *
 * @note The ownership of the capability slot is transferred to the memory manager and may
 *       be recycled for future allocations.
 */
errval_t mm_free(struct mm *mm, struct capref cap)
{
    thread_mutex_lock_nested(&mm_mutex);

    errval_t err;

    struct capability thecap;
    err = cap_direct_identify(cap, &thecap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "identify capability");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    uintptr_t block_addr = thecap.u.ram.base;
    size_t block_size = thecap.u.ram.bytes;

    struct region_info* region = mm->region_head; 
    for (; region != NULL; region = region->next) {
        if (region->reg_addr <= block_addr && block_addr < region->reg_addr + region->reg_size) {
            break;
        }
    }
    if (region == NULL) {
        err = MM_ERR_NOT_FOUND;
        DEBUG_ERR(err, "could not region corresponding to block");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    err = cap_delete(cap);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "failure when deleting cap in free");

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    err = mm->ca->free(mm->ca, cap);

    struct block_info* pred = NULL;
    struct block_info* succ = NULL;
    for (succ = region->free_head; succ != NULL; succ = succ->next) {
        // TODO check this
        if (block_addr + block_size <= succ->block_addr) {
            if (pred == NULL) {
                break;
            }
            if (pred->block_addr + pred->block_size <= block_addr) {
                break;
            }
        }
        pred = succ;
    }

    // first, try to merge with predecessor
    if (pred != NULL && pred->block_addr + pred->block_size == block_addr) {
        pred->block_size += block_size;
        // on success, try merging predecessor with successor
        // printf("%lx, %lx, %lx, %lx\n", pred->block_addr, pred->block_size, succ->block)
        if (succ != NULL && pred->block_addr + pred->block_size == succ->block_addr) {
            pred->next = succ->next;
            pred->block_size += succ->block_size;
            slab_free(&mm->slab, succ);
        }

        mm->mem_available += block_size;

        thread_mutex_unlock(&mm_mutex);
        return SYS_ERR_OK;
    }

    // on failure to merge with predecessor, try to merge only with successor
    if (succ != NULL && block_addr + block_size == succ->block_addr) {
        succ->block_addr = block_addr;
        succ->block_size += block_size;

        mm->mem_available += block_size;

        thread_mutex_unlock(&mm_mutex);
        return SYS_ERR_OK;
    }

    // if cannot merge at all, allocate new block and insert into free list
    void* buf = NULL;
    err = tracked_slab_alloc(mm, &buf);
    struct block_info* block = (struct block_info*)buf;

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "slab alloc");
        thread_mutex_unlock(&mm_mutex);
        return err;
    }
    block->block_addr = block_addr;
    block->block_size = block_size;
    block->next = succ;

    if (pred == NULL) {
        region->free_head = block;
    } else {
        pred->next = block;
    }

    mm->mem_available += block_size;


    slab_try_refill(mm);

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}


/**
 * @brief returns the amount of available (free) memory of the memory manager
 *
 * @param[in] mm   memory manager instance to query
 *
 * @return the amount of memory available in bytes in the memory manager
 */
size_t mm_mem_available(struct mm *mm)
{
    thread_mutex_lock_nested(&mm_mutex);
    size_t available = mm->mem_available;
    thread_mutex_unlock(&mm_mutex);
    return available;
}


/**
 * @brief returns the total amount of memory this mm instances manages.
 *
 * @param[in] mm   memory manager instance to query
 *
 * @return the total amount of memory in bytes of the memory manager
 */
size_t mm_mem_total(struct mm *mm)
{
    thread_mutex_lock_nested(&mm_mutex);
    size_t result = mm->mem_total;
    thread_mutex_unlock(&mm_mutex);
    return result;
}


/**
 * @brief obtains the range of free memory of the memory allocator instance
 *
 * @param[in]  mm     memory manager instance to query
 * @param[out] base   returns the minimum address of free memroy
 * @param[out] limit  returns the maximum address of free memory
 *
 * Note: This is part of the extra challenge. You can ignore potential (allocation)
 *       holes in the free memory regions, and just return the smallest address of
 *       a region than is free, and likewise the highest address
 */
void mm_mem_get_free_range(struct mm *mm, lpaddr_t *base, lpaddr_t *limit)
{
    thread_mutex_lock_nested(&mm_mutex);

    lpaddr_t minaddr = -1;
    lpaddr_t maxaddr = 0;
    for (struct region_info* region = mm->region_head; region != NULL; region = region->next) {
        if (region->reg_addr < minaddr) {
            minaddr = region->reg_addr;
        }
        if (region->reg_addr + region->reg_size - 1 > maxaddr) {
            maxaddr = region->reg_addr + region->reg_size - 1;
        }
    }

    *base = minaddr;
    *limit = maxaddr;

    thread_mutex_unlock(&mm_mutex);
}
