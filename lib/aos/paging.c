/**
 * \file
 * \brief AOS paging helpers.
 */

/*
 * Copyright (c) 2012, 2013, 2016, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <aos/aos.h>
#include <aos/paging.h>
#include <aos/except.h>
#include <aos/slab.h>
#include "threads_priv.h"

#include <stdio.h>
#include <string.h>
#include <aos/threads.h>

static struct paging_state current;


#define PT_DEBUG_CHILD_COUNT

#define PT_CHK_ALIGN(alignment)                                                                    \
    (((alignment) & ((alignment)-1)) == 0 && (alignment) >= BASE_PAGE_SIZE)
#define TYPE_IS_PTE(type)                                                                          \
    (type == ObjType_VNode_AARCH64_l0 || type == ObjType_VNode_AARCH64_l1                          \
     || type == ObjType_VNode_AARCH64_l2 || type == ObjType_VNode_AARCH64_l3)
#define TYPE_IS_NON_CHILD_PTE(type)                                                                \
    (type == ObjType_VNode_AARCH64_l0 || type == ObjType_VNode_AARCH64_l1                          \
     || type == ObjType_VNode_AARCH64_l2)

#define PT_SET_LAZY(pt, index) (pt)->lazy[(index) / 32] |= 1 << ((index) % 32);
#define PT_CLR_LAZY(pt, index) (pt)->lazy[(index) / 32] &= ~(1 << ((index) % 32));
#define PT_IS_LAZY(pt, index)  (((pt)->lazy[(index) / 32] & (1 << ((index) % 32))) != 0)

static inline errval_t _pt_ensure_slab_space(struct paging_state *st)
{
    errval_t err = SYS_ERR_OK;

    // TODO: Do not duplicate the code for these two checks
    if (st->_refill_slab_pt) {
        if (slab_freecount(&st->page_table_allocator) == 0) {
            // this should never happen! (we are now stuck...)
            return LIB_ERR_SLAB_ALLOC_FAIL;
        }
        return SYS_ERR_OK;
    }

    if (!st->_refill_slab_pt && slab_freecount(&st->page_table_allocator) <= 8) {
        st->_refill_slab_pt = true;

        struct capref cap;
        err = st->slot_alloc->alloc(st->slot_alloc, &cap);
        if (err_is_fail(err)) {
            st->_refill_slab_pt = false;
            return err;  // NOTE there is not much we can do at this point...
        }
        err = slab_refill_no_pagefault(&st->page_table_allocator, cap,
                                       sizeof(struct slab_head) + 12 * sizeof(struct page_table));
        if (err_is_fail(err)) {
            st->_refill_slab_pt = false;
            assert(err_is_ok(st->slot_alloc->free(st->slot_alloc, cap)));
            return err;  // NOTE there is not much we can do at this point...
        }
        st->_refill_slab_pt = false;
    }

    if (st->_refill_slab_rb) {
        if (slab_freecount(&st->rb_node_allocator) == 0) {
            // this should never happen! (we are now stuck...)
            return LIB_ERR_SLAB_ALLOC_FAIL;
        }
        return SYS_ERR_OK;
    }

    if (!st->_refill_slab_rb && slab_freecount(&st->rb_node_allocator) <= 8) {
        st->_refill_slab_rb = true;

        struct capref cap;
        err = st->slot_alloc->alloc(st->slot_alloc, &cap);
        if (err_is_fail(err)) {
            st->_refill_slab_rb = false;
            return err;  // NOTE there is not much we can do at this point...
        }
        err = slab_refill_no_pagefault(&st->rb_node_allocator, cap,
                                       sizeof(struct slab_head) + 12 * sizeof(struct rb_node));
        if (err_is_fail(err)) {
            st->_refill_slab_rb = false;
            assert(err_is_ok(st->slot_alloc->free(st->slot_alloc, cap)));
            return err;  // NOTE there is not much we can do at this point...
        }
        st->_refill_slab_rb = false;
    }
    return SYS_ERR_OK;
}

/**
 * @brief allocates a new page table for the given paging state with the given type
 *
 * @param[in]  st    paging state to allocate the page table for (required for slot allcator)
 * @param[in]  type  the type of the page table to create
 * @param[out] ret   returns the capref to the newly allocated page table
 *
 * @returns error value indicating success or failure
 *   - @retval SYS_ERR_OK if the allocation was successful
 *   - @retval LIB_ERR_SLOT_ALLOC if there couldn't be a slot allocated for the new page table
 *   - @retval LIB_ERR_VNODE_CREATE if the page table couldn't be created
 */
static errval_t pt_alloc(struct paging_state *st, enum objtype type, struct capref *ret)
{
    thread_mutex_lock_nested(&mm_mutex);
    assert(st != NULL);
    errval_t err = SYS_ERR_OK;

    assert(TYPE_IS_PTE(type));

    // try to get a slot from the slot allocator to hold the new page table
    err = st->slot_alloc->alloc(st->slot_alloc, ret);
    if (err_is_fail(err)) {
        thread_mutex_unlock(&mm_mutex);
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    // create the vnode in the supplied slot
    err = vnode_create(*ret, type);
    if (err_is_fail(err)) {
        thread_mutex_unlock(&mm_mutex);
        return err_push(err, LIB_ERR_VNODE_CREATE);
    }

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}

__attribute__((unused)) static errval_t pt_alloc_l1(struct paging_state *st, struct capref *ret)
{
    return pt_alloc(st, ObjType_VNode_AARCH64_l1, ret);
}

__attribute__((unused)) static errval_t pt_alloc_l2(struct paging_state *st, struct capref *ret)
{
    return pt_alloc(st, ObjType_VNode_AARCH64_l2, ret);
}

__attribute__((unused)) static errval_t pt_alloc_l3(struct paging_state *st, struct capref *ret)
{
    return pt_alloc(st, ObjType_VNode_AARCH64_l3, ret);
}

static inline uint16_t _pt_get_type_index(enum objtype type, lvaddr_t addr)
{
    assert(TYPE_IS_PTE(type));

    switch (type) {
    case ObjType_VNode_AARCH64_l0:
        return VMSAv8_64_L0_INDEX(addr);
    case ObjType_VNode_AARCH64_l1:
        return VMSAv8_64_L1_INDEX(addr);
    case ObjType_VNode_AARCH64_l2:
        return VMSAv8_64_L2_INDEX(addr);
    case ObjType_VNode_AARCH64_l3:
        return VMSAv8_64_L3_INDEX(addr);
    default:
        return 0;
    }
}

static inline enum objtype _pt_get_entry_type(enum objtype type)
{
    assert(TYPE_IS_NON_CHILD_PTE(type));

    switch (type) {
    case ObjType_VNode_AARCH64_l0:
        return ObjType_VNode_AARCH64_l1;
    case ObjType_VNode_AARCH64_l1:
        return ObjType_VNode_AARCH64_l2;
    case ObjType_VNode_AARCH64_l2:
        return ObjType_VNode_AARCH64_l3;
    default:
        return 0;
    }
}

static inline void _pt_table_init(struct page_table *pt, enum objtype type, uint16_t index)
{
    assert(pt != NULL);
    assert(TYPE_IS_PTE(type));

    pt->page_table   = NULL_CAP;
    pt->mapping      = NULL_CAP;
    pt->index        = index;
    pt->num_children = 0;
    pt->type         = type;

    for (size_t entry = 0; entry < VMSAv8_64_PTABLE_NUM_ENTRIES; ++entry) {
        PT_CLR_LAZY(pt, entry);
        if (pt->type != ObjType_VNode_AARCH64_l3) {
            pt->entries[entry].pt = NULL;
        } else {
            pt->entries[entry].frame_cap = NULL_CAP;
        }
    }
}

static inline errval_t _pt_table_create(struct paging_state *st, struct page_table **pt,
                                        enum objtype type, uint16_t index)
{
    assert(pt != NULL);
    *pt = slab_alloc(&st->page_table_allocator);
    if (*pt == NULL) {
        // we could not allocate a new page_table via the slab allocator...
        return LIB_ERR_SLAB_ALLOC_FAIL;
    }
    _pt_table_init(*pt, type, index);
    return SYS_ERR_OK;
}

// TODO: In the future, we can use a method that can return in O(1) instead of nb of page tables
static inline u_int16_t _pt_table_num_children(struct paging_state *st, struct page_table *pt)
{
    assert(st != NULL && pt != NULL);
#ifdef PT_DEBUG_CHILD_COUNT
    u_int16_t num_children = 0;
    for (size_t entry = 0; entry < VMSAv8_64_PTABLE_NUM_ENTRIES; ++entry) {
        if (pt->type == ObjType_VNode_AARCH64_l3 && !capref_is_null(pt->entries[entry].frame_cap)) {
            ++num_children;
        } else if (pt->entries[entry].pt != NULL) {
            ++num_children;
        }
    }
    assert(pt->num_children == num_children);
#endif
    return pt->num_children;
}

// TODO we should change this to automatically destroy on higher levels if we need to.
static inline errval_t _pt_table_destroy(struct paging_state *st, struct page_table **pt)
{
    assert(pt != NULL);
    enum objtype type = (*pt)->type;
    assert(type == ObjType_VNode_AARCH64_l1 || type == ObjType_VNode_AARCH64_l2
           || type == ObjType_VNode_AARCH64_l3);
    assert(_pt_table_num_children(st, *pt) == 0);
    errval_t err = SYS_ERR_OK;

    err = cap_destroy((*pt)->mapping);
    if (err_is_fail(err)) {
        // TODO handle the error correctly.
        return err;
    }

    err = cap_destroy((*pt)->page_table);
    if (err_is_fail(err)) {
        // TODO handle the error correctly.
        return err;
    }
    slab_free(&st->page_table_allocator, *pt);
    *pt = NULL;
    return SYS_ERR_OK;
}

static inline errval_t _pt_table_locate_entry(struct paging_state *st, struct page_table *pt,
                                              lvaddr_t addr, int flags, struct page_table **entry,
                                              bool create_if_missing)
{
    assert(st != NULL && pt != NULL);

    errval_t     err  = SYS_ERR_OK;
    enum objtype type = pt->type;
    assert(TYPE_IS_NON_CHILD_PTE(type));

    uint16_t index = _pt_get_type_index(type, addr);

    *entry = pt->entries[index].pt;
    if (*entry != NULL || !create_if_missing) {
        return SYS_ERR_OK;
    }

    // create the meta data structure
    err = _pt_table_create(st, entry, _pt_get_entry_type(type), index);
    if (err_is_fail(err)) {
        return err;
    }
    err = pt_alloc(st, _pt_get_entry_type(type), &(*entry)->page_table);
    if (err_is_fail(err)) {
        // XXX not handling this error is M1 specific; we should undo the creation of the page table
        //     (i.e., _pt_table_create) in general (but here it does not matter)
        return err;
    }

    // create & set up the mapping
    err = st->slot_alloc->alloc(st->slot_alloc, &(*entry)->mapping);
    if (err_is_fail(err)) {
        // XXX not handling this error is M1 specific; we should undo the creation of the page table
        //     (i.e., _pt_table_create) in general (but here it does not matter)
        // "undo" pt_alloc
        st->slot_alloc->free(st->slot_alloc, (*entry)->page_table);
        // "undo" _pt_table_create
        slab_free(&st->page_table_allocator, *entry);
        return err;
    }

    // XXX think more about the flags; check they are compatible, etc. (maybe just need to set L3).
    err = vnode_map(pt->page_table, (*entry)->page_table, index, flags, 0, 1, (*entry)->mapping);
    if (err_is_fail(err)) {
        // hack to avoid returning an error when the page table was mapped while we were creating it
        // TODO we should figure out a way to do this correctly by pre-allocating the page tables
        // and slots
        return SYS_ERR_OK;
        // XXX not handling this error is M1 specific; we should undo the creation of the page table
        //     (i.e., _pt_table_create) in general (but here it does not matter)
        // "undo" st->slot_alloc->alloc
        st->slot_alloc->free(st->slot_alloc, (*entry)->mapping);
        // "undo" pt_alloc
        st->slot_alloc->free(st->slot_alloc, (*entry)->page_table);
        // "undo" _pt_table_create
        slab_free(&st->page_table_allocator, *entry);
        return err;
    }

    ++pt->num_children;
    pt->entries[index].pt = *entry;

    return SYS_ERR_OK;
}

static inline errval_t _pt_table_get_entry(struct paging_state *st, struct page_table *pt,
                                           lvaddr_t addr, int flags, struct page_table **entry)
{
    return _pt_table_locate_entry(st, pt, addr, flags, entry, true);
}

static inline errval_t _pt_table_map_frame(struct paging_state *st, struct page_table *ptl3,
                                           lvaddr_t addr, size_t bytes, struct capref frame,
                                           size_t offset, int flags, bool lazy)
{
    errval_t err = SYS_ERR_OK;
    assert(st != NULL && ptl3 != NULL);

    uint16_t index = VMSAv8_64_L3_INDEX(addr);
    // check that we are not overwriting an existing frame_cap!
    if (!capref_is_null(ptl3->entries[index].frame_cap)) {
        return SYS_ERR_OK;
    }
    ++ptl3->num_children;

    err = st->slot_alloc->alloc(st->slot_alloc, &ptl3->entries[index].frame_cap);
    if (err_is_fail(err)) {
        return err;
    }

    if (lazy) {
        // TODO check that the frame is off size exactly BASE_PAGE_SIZE
        //      we delete frames of lazily allocated pages.
        PT_SET_LAZY(ptl3, index);
    }

    // TODO is it correct to assume the offset should be used for the L3 mapping?
    err = vnode_map(ptl3->page_table, frame, index, flags, offset,
                    DIVIDE_ROUND_UP(bytes, BASE_PAGE_SIZE), ptl3->entries[index].frame_cap);
    if (err_is_fail(err)) {
        return err;
    }
    return SYS_ERR_OK;
}

/**
 * @brief returns the size of the allocated segment identified by start_vaddr
 *
 * @param[in]  st               A pointer to the paging state to allocate from
 * @param[in]  start_vaddr      Starting virtual address of the region to lookup
 * @param[out] bytes            The size of the requested region identified by start_vaddr
 *
 * @return Either SYS_ERR_OK if no error occurred or an error indicating what went wrong otherwise.
 */
static inline errval_t _vaddr_lookup(struct paging_state *st, lvaddr_t start_vaddr, size_t *bytes)
{
    // look for the next range, being allocated (size = 0) or free
    struct rb_node *node = rb_tree_find_greater(&st->virtual_memory, start_vaddr + 1);
    if (node == NULL)
        // Note: this should never happen as we make sure there is always a free range at the end
        return ERR_INVALID_ARGS;

    *bytes = node->start - start_vaddr;
    return SYS_ERR_OK;
}

/**
 * @brief returns the size and starting address of the region containing a specific virtual address
 *
 * @param[in]  st               A pointer to the paging state to allocate from
 * @param[out] vaddr            Virtual address contained within the region to lookup
 * @param[out] start_vaddr      Starting virtual address of the identified region
 * @param[out] bytes            Size of the identified region
 *
 * @return Either SYS_ERR_OK if no error occurred or an error indicating what went wrong otherwise.
 */
static inline errval_t _vaddr_lookup_region(struct paging_state *st, lvaddr_t vaddr,
                                            lvaddr_t *start_vaddr, size_t *bytes)
{
    // look for the first node indicating the beginning of the allocated region
    struct rb_node *node = rb_tree_find_lower(&st->virtual_memory, vaddr);
    if (node == NULL)
        // the memory range is not handled by st
        return MM_ERR_NOT_FOUND;

    if (node->size != 0)
        // the memory region containing vaddr is free
        // TODO: return a better error
        return MM_ERR_NOT_FOUND;

    // this is faster than a call to rb_tree_find_greater
    struct rb_node *succ = rb_tree_successor(node);
    if (succ == NULL)
        // Note: this should never happen as we make sure there is always a free range at the end
        return ERR_INVALID_ARGS;

    *start_vaddr = node->start;
    *bytes       = succ->start - node->start;

    return SYS_ERR_OK;
}


/**
 * @brief Inner function used to allocate a virtual memory region after all checks were performed
 *
 * @param[in]  st                A pointer to the paging state to allocate from
 * @param[in]  node              Free memory region containing [vaddr, vaddr + bytes]
 * @param[in]  vaddr             The address to allocate the virtual memory
 * @param[in]  bytes             The size of the region to allocate
 * @param[out]  allocated_bytes  Actual size of the allocated region (zero on error).
 *
 * @return Either SYS_ERR_OK if no error occurred or an error indicating what went wrong otherwise.
 */
static inline errval_t _vaddr_alloc_inner(struct paging_state *st, struct rb_node *node,
                                          lvaddr_t vaddr, size_t bytes)
{
    lvaddr_t original_start = node->start;
    size_t   original_size  = node->size;

    // set the node start at the start of the range and size to 0 to signify it is allocated
    node->start = vaddr;
    rb_tree_update_size(node, 0);

    // add a free range at the beginning and the end if necessary
    if (vaddr > original_start) {
        struct rb_node *left = slab_alloc(&st->rb_node_allocator);
        if (left == NULL)
            return MM_ERR_OUT_OF_MEMORY;

        left->start = original_start;
        left->size  = vaddr - original_start;
        rb_tree_insert(&st->virtual_memory, left);
    }

    if (original_start + original_size > vaddr + bytes) {
        struct rb_node *right = slab_alloc(&st->rb_node_allocator);
        if (right == NULL)
            return MM_ERR_OUT_OF_MEMORY;

        right->start = vaddr + bytes;
        right->size  = original_start + original_size - (vaddr + bytes);
        rb_tree_insert(&st->virtual_memory, right);
    }

    return SYS_ERR_OK;
}

/**
 * @brief Allocate virtual address from the allocator (at any address).
 *
 * @param[in]  st                A pointer to the paging state to allocate from
 * @param[out] buf               Returns the free virtual address that was found.
 * @param[in]  bytes             The requested (minimum) size of the region to allocate
 * @param[in]  alignment         The address needs to be a multiple of 'alignment'.
 * @param[out]  allocated_bytes  Actual size of the allocated region (zero on error).
 *
 * @return Either SYS_ERR_OK if no error occurred or an error indicating what went wrong otherwise.
 */
static inline errval_t _vaddr_alloc(struct paging_state *st, void **buf, size_t bytes,
                                    size_t alignment, size_t *allocated_bytes)
{
    // look for enough memory to make sure we can always align the start correctly
    size_t          requested_size = bytes + alignment - 1;
    struct rb_node *node           = rb_tree_find_minsize(&st->virtual_memory, requested_size);
    if (node == NULL)
        return LIB_ERR_OUT_OF_VIRTUAL_ADDR;

    lvaddr_t alloc_addr = ROUND_UP(node->start, alignment);
    *buf                = (void *)alloc_addr;
    *allocated_bytes    = bytes;

    return _vaddr_alloc_inner(st, node, alloc_addr, bytes);
}

/**
 * @brief Allocate a fixed virtual address from the allocator
 *
 * @param[in]  st                A pointer to the paging state to allocate from
 * @param[out] buf               Returns the free virtual address that was found.
 * @param[in]  bytes             The requested (minimum) size of the region to allocate
 * @param[out]  allocated_bytes  Actual size of the allocated region (zero on error).
 *
 * @return Either SYS_ERR_OK if no error occurred or an error indicating what went wrong otherwise.
 */
static inline errval_t _vaddr_alloc_fixed(struct paging_state *st, lvaddr_t vaddr, size_t bytes,
                                          size_t *allocated_bytes)
{
    struct rb_node *node = rb_tree_find(&st->virtual_memory, vaddr);
    if (node == NULL || node->start + node->size < vaddr + bytes)
        return LIB_ERR_OUT_OF_VIRTUAL_ADDR;

    *allocated_bytes = bytes;
    return _vaddr_alloc_inner(st, node, vaddr, bytes);
}

/**
 * @brief free an allocated virtual address region.
 *
 * @param[in]  st               A pointer to the paging state to allocate from
 * @param[in]  vaddr            The start address of the region to free
 *
 * @return Either SYS_ERR_OK if no error occurred or an error indicating what went wrong otherwise.
 */
static inline errval_t _vaddr_free(struct paging_state *st, lvaddr_t vaddr)
{
    // get the node at the end of the allocated region
    // look for the first node indicating the beginning of the allocated region
    struct rb_node *node = rb_tree_find_lower(&st->virtual_memory, vaddr);
    assert(node->size == 0);
    assert(node->start == vaddr);
    if (node == NULL || node->size != 0 || node->start != vaddr)
        return ERR_INVALID_ARGS;

    struct rb_node *succ = rb_tree_successor(node);
    if (succ == NULL)
        // Note: this should never happen as we make sure there is always a free range at the end
        return ERR_INVALID_ARGS;

    node->size = succ->start - node->start;

    // merge the free region with the ones before and after if possible
    struct rb_node *pred = rb_tree_predecessor(node);
    if (pred != NULL && pred->size > 0 && pred->start + pred->size == vaddr) {
        node->start = pred->start;
        node->size += pred->size;
        rb_tree_delete(&st->virtual_memory, pred);

        slab_free(&st->rb_node_allocator, pred);
    }

    if (succ->size > 0 && succ->start == node->start + node->size) {
        node->size += succ->size;
        rb_tree_delete(&st->virtual_memory, succ);

        slab_free(&st->rb_node_allocator, succ);
    }

    // we must call this function to keep the metadata in the whole tree up to date
    rb_tree_update_size(node, node->size);

    return SYS_ERR_OK;
}

static size_t _paging_exception_stack[PAGING_EXCEPT_STACK_SIZE];

static void _paging_handle_exception(enum exception_type type, int subtype, void *addr,
                                     arch_registers_state_t *regs)
{
    (void)type;
    (void)subtype;
    (void)regs;
    lvaddr_t page_addr = (lvaddr_t)addr & ~(BASE_PAGE_SIZE - 1);

    if (page_addr < BASE_PAGE_SIZE) {
        debug_printf("attempting to dereference address 0x%x, calling ip = 0x%x\n", page_addr, (lvaddr_t)registers_get_ip(regs));
        // XXX maybe we can return a better error code here?
        USER_PANIC_ERR(SYS_ERR_VNODE_NOT_INSTALLED, "(likely) null-pointer dereference.");
    }

    errval_t             err;
    struct paging_state *st = get_current_paging_state();

    err = _pt_ensure_slab_space(st);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to ensure paging state space");
    }

    err = try_map(st, page_addr);
    // debug_printf("handling page fault at address %p, sp = 0x%x\n", addr,
    // (lvaddr_t)registers_get_sp(regs));
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to map address, calling ip = 0x%x",
                       (lvaddr_t)registers_get_ip(regs));
    }
}

/**
 * @brief initializes the paging state struct for the current process
 *
 * @param[in] st           the paging state to be initialized
 * @param[in] start_vaddr  start virtual address to be managed
 * @param[in] root         capability to the root level page table
 * @param[in] ca           the slot allocator instance to be used
 *
 * @return SYS_ERR_OK on success, or LIB_ERR_* on failure
 */
errval_t paging_init_state(struct paging_state *st, lvaddr_t start_vaddr, struct capref root,
                           struct slot_allocator *ca)
{
    // XXX paging_init_state is NOT thread safe.
    errval_t err = SYS_ERR_OK;

    // (M1) basic state struct initialization
    st->slot_alloc = ca;

    st->_refill_slab_pt = false;
    st->_refill_slab_rb = false;

    // XXX we probably want to use a different refill function as this just allocates BASE_PAGE_SIZE
    //     M1 specific: we do not care about refill (we have space for enough page_tables by def.)
    slab_init(&st->page_table_allocator, sizeof(struct page_table), NULL);
    slab_grow(&st->page_table_allocator, (void *)&st->_page_table_buf, sizeof(st->_page_table_buf));

    slab_init(&st->rb_node_allocator, sizeof(struct rb_node), NULL);
    slab_grow(&st->rb_node_allocator, (void *)&st->_rb_node_buf, sizeof(st->_rb_node_buf));

    rb_tree_init(&st->virtual_memory);

    struct rb_node *range = slab_alloc(&st->rb_node_allocator);
    range->start          = start_vaddr;
    // manage the whole virtual memory address starting from vaddr
    // The + 1 makes sure that there will always be a free memory range as the last node of the tree
    // TODO: is this right, nothing in this memory range is mapped by something else?
    range->size = (1UL << 48) - range->start + 1;
    rb_tree_insert(&st->virtual_memory, range);

    // XXX maybe there is something useful we could do with the parent_index for L0?
    _pt_table_init(&st->l0, ObjType_VNode_AARCH64_l0, /*parent_index=*/0);
    st->l0.page_table = root;

    void *stack_base = _paging_exception_stack;
    void *stack_end  = (char *)_paging_exception_stack + sizeof(_paging_exception_stack);

    err = thread_set_exception_handler(_paging_handle_exception, NULL, stack_base, stack_end, NULL,
                                       NULL);

    if (!mm_mutex_init) {
        thread_mutex_init(&mm_mutex);
        mm_mutex_init = true;
    }

    return err;
}


/**
 * @brief initializes the paging state struct for a foreign process when spawning a new one
 *
 * @param[in] st           the paging state to be initialized
 * @param[in] start_vaddr  start virtual address to be managed
 * @param[in] root         capability to the root leve page table
 * @param[in] ca           the slot allocator instance to be used
 *
 * @return SYS_ERR_OK on success, or LIB_ERR_* on failure
 */
errval_t paging_init_state_foreign(struct paging_state *st, lvaddr_t start_vaddr,
                                   struct capref root, struct slot_allocator *ca)
{
    errval_t err = SYS_ERR_OK;
    thread_mutex_lock_nested(&mm_mutex);
    err = paging_init_state(st, start_vaddr, root, ca);
    thread_mutex_unlock(&mm_mutex);
    return err;
}

/**
 * @brief This function initializes the paging for this domain
 *
 * Note: The function is called once before main.
 */
errval_t paging_init(void)
{
    // XXX paging_init is NOT thread safe.

    // (M1) call paging_init_state for &current
    paging_init_state(&current, VADDR_OFFSET, cap_vroot, get_default_slot_allocator());
    set_current_paging_state(&current);

    // TODO (M2): initialize self-paging handler
    // TIP: use thread_set_exception_handler() to setup a page fault handler
    // TIP: Think about the fact that later on, you'll have to make sure that
    // you can handle page faults in any thread of a domain.
    // TIP: it might be a good idea to call paging_init_state() from here to
    // avoid code duplication.

    return SYS_ERR_OK;
}


/**
 * @brief frees up the resources allocate in the foreign paging state
 *
 * @param[in] st   the foreign paging state to be freed
 *
 * @return SYS_ERR_OK on success, or LIB_ERR_* on failure
 *
 * Note: this function will free up the resources of *the current* paging state
 * that were used to construct the foreign paging state. There should be no effect
 * on the paging state of the foreign process.
 */
errval_t paging_free_state_foreign(struct paging_state *st)
{
    (void)st;
    // TODO: implement me
    return SYS_ERR_OK;
}


/**
 * @brief Initializes the paging functionality for the calling thread
 *
 * @param[in] t   the tread to initialize the paging state for.
 *
 * This function prepares the thread to handing its own page faults
 */
errval_t paging_init_onthread(struct thread *t)
{
    // make compiler happy about unused parameters
    errval_t err = SYS_ERR_OK;

    err = thread_target_set_exception_handler(
        t, _paging_handle_exception, NULL, (char *)t->default_except_stack,
        (char *)t->default_except_stack + t->default_except_stack_size, NULL, NULL);

    return err;
}

/**
 * @brief Find a free region of virtual address space that is large enough to accommodate a
 *        buffer of size 'bytes'.
 *
 * @param[in]  st          A pointer to the paging state to allocate from
 * @param[out] buf         Returns the free virtual address that was found.
 * @param[in]  bytes       The requested (minimum) size of the region to allocate
 * @param[in]  alignment   The address needs to be a multiple of 'alignment'.
 *
 * @return Either SYS_ERR_OK if no error occurred or an error indicating what went wrong otherwise.
 */
errval_t paging_alloc(struct paging_state *st, void **buf, size_t bytes, size_t alignment)
{
    thread_mutex_lock_nested(&mm_mutex);

    errval_t err = SYS_ERR_OK;
    if (!PT_CHK_ALIGN(alignment)) {
        // XXX should we return a SYS_ERR here instead?
        thread_mutex_unlock(&mm_mutex);
        return err_push(err, MM_ERR_BAD_ALIGNMENT);
    }

    err = _pt_ensure_slab_space(st);
    if (err_is_fail(err)) {
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    size_t allocated_bytes;
    err = _vaddr_alloc(st, buf, bytes, alignment, &allocated_bytes);
    if (err_is_fail(err)) {
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}


static inline errval_t _paging_map_single_page(struct paging_state *st, lvaddr_t vaddr,
                                               struct capref frame, size_t bytes, size_t offset,
                                               int flags, bool lazy)
{
    errval_t err = SYS_ERR_OK;

    err = _pt_ensure_slab_space(st);
    if (err_is_fail(err)) {
        return err;
    }

    // XXX think about how we want to handle the flags. Do we just set them on L3?
    // XXX we could refactor this code to be recursive. (i.e., directly resolve L3)
    struct page_table *ptl1;
    err = _pt_table_get_entry(st, &st->l0, vaddr, flags, &ptl1);
    if (err_is_fail(err)) {
        return err;
    }

    struct page_table *ptl2;
    err = _pt_table_get_entry(st, ptl1, vaddr, flags, &ptl2);
    if (err_is_fail(err)) {
        return err;
    }

    struct page_table *ptl3;
    err = _pt_table_get_entry(st, ptl2, vaddr, flags, &ptl3);
    if (err_is_fail(err)) {
        return err;
    }

    // XXX this is most likely using offset wrong?... (it refers to an offset within the frame)
    // printf("offset: %d\n", offset);
    err = _pt_table_map_frame(st, ptl3, vaddr, bytes, frame, offset, flags, lazy);
    if (err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}


// XXX requires that we have already allocated bytes starting from vaddr
static inline errval_t _paging_map_vaddr(struct paging_state *st, lvaddr_t vaddr,
                                         struct capref frame, size_t bytes, size_t offset,
                                         int flags, bool lazy)
{
    assert(st != NULL);

    errval_t err = SYS_ERR_OK;

    struct frame_identity frame_ident;
    err = frame_identify(frame, &frame_ident);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_IDENTIFY);
    }
    gensize_t frame_bytes = frame_ident.bytes;

    if (frame_bytes < bytes) {
        // TODO handle smaller sizes correctly. (e.g., ensure we do not map out of the req. space)
        return LIB_ERR_RAM_ALLOC_WRONG_SIZE;
    }

    for (size_t curr_addr_to_map = vaddr; curr_addr_to_map < vaddr + bytes;
         curr_addr_to_map += BASE_PAGE_SIZE) {
        err = _paging_map_single_page(st, curr_addr_to_map, frame, BASE_PAGE_SIZE, offset, flags,
                                      lazy);
        if (err_is_fail(err)) {
            return err;
        }

        offset += BASE_PAGE_SIZE;
    }

    return SYS_ERR_OK;
}

/**
 * @brief maps a frame at a free virtual address region and returns its address
 *
 * @param[in]  st      paging state of the address space to create the mapping in
 * @param[out] buf     returns the virtual address of the mapped frame
 * @param[in]  bytes   the amount of bytes to be mapped
 * @param[in]  frame   frame capability of backing memory to be mapped
 * @param[in]  offset  offset into the frame capability to be mapped
 * @param[in]  flags   mapping flags
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure.
 */
errval_t paging_map_frame_attr_offset(struct paging_state *st, void **buf, size_t bytes,
                                      struct capref frame, size_t offset, int flags)
{
    thread_mutex_lock_nested(&mm_mutex);
    assert(st != NULL && buf != NULL);

    errval_t err = SYS_ERR_OK;
    err          = paging_alloc(st, buf, bytes, BASE_PAGE_SIZE);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "paging_alloc failed");
        thread_mutex_unlock(&mm_mutex);
        return err;
    }
    lvaddr_t vaddr = (lvaddr_t)*buf;

    err = _paging_map_vaddr(st, vaddr, frame, bytes, offset, flags, /*lazy=*/false);
    if (err_is_fail(err)) {
        // TODO most likely we should free buf here and set it to NULL?
        //       and free the allocation again...
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}

/**
 * @brief maps a frame at a user-provided virtual address region
 *
 * @param[in] st      paging state of the address space to create the mapping in
 * @param[in] vaddr   provided virtual address to map the frame at
 * @param[in] frame   frame capability of backing memory to be mapped
 * @param[in] bytes   the amount of bytes to be mapped
 * @param[in] offset  offset into the frame capability to be mapped
 * @param[in] flags   mapping flags
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure
 *
 * The region at which the frame is requested to be mapped must be free (i.e., hasn't been
 * allocated), otherwise the mapping request should fail.
 */
errval_t paging_map_fixed_attr_offset(struct paging_state *st, lvaddr_t vaddr, struct capref frame,
                                      size_t bytes, size_t offset, int flags)
{
    thread_mutex_lock_nested(&mm_mutex);
    // TODO Add more relevant checks for the input parameters.
    errval_t err = SYS_ERR_OK;

    err = _pt_ensure_slab_space(st);
    if (err_is_fail(err)) {
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    size_t allocated_bytes = 0;
    err                    = _vaddr_alloc_fixed(st, vaddr, bytes, &allocated_bytes);
    if (err_is_fail(err)) {
        // TODO handle the error:
        thread_mutex_unlock(&mm_mutex);
        return err;
    }
    assert(allocated_bytes > 0);

    err = _paging_map_vaddr(st, vaddr, frame, allocated_bytes, offset, flags, /*lazy=*/false);
    if (err_is_fail(err)) {
        // TODO handle the error: free the allocated vaddr.
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}

static inline errval_t _pt_table_lookup(struct paging_state *st, lvaddr_t vaddr,
                                        struct page_table **ptl3)
{
    assert(st != NULL);
    // XXX the code has a lot of common structure between the different levels (IMPROVE!)
    *ptl3 = NULL;

    errval_t           err = SYS_ERR_OK;
    struct page_table *ptl1;
    err = _pt_table_locate_entry(st, &st->l0, vaddr, VREGION_FLAGS_READ, &ptl1,
                                 /*create_page_if_missing=*/false);
    if (err_is_fail(err)) {
        return err;
    }
    if (ptl1 == NULL) {
        return SYS_ERR_OK;
    }

    struct page_table *ptl2;
    err = _pt_table_locate_entry(st, ptl1, vaddr, VREGION_FLAGS_READ, &ptl2,
                                 /*create_page_if_missing=*/false);
    if (err_is_fail(err)) {
        return err;
    }
    if (ptl2 == NULL) {
        return SYS_ERR_OK;
    }

    err = _pt_table_locate_entry(st, ptl2, vaddr, VREGION_FLAGS_READ, ptl3,
                                 /*create_page_if_missing=*/false);
    if (err_is_fail(err)) {
        return err;
    }
    if (ptl3 == NULL) {
        return SYS_ERR_OK;
    }

    return SYS_ERR_OK;
}

static inline errval_t _unmap_single_frame(struct paging_state *st, lvaddr_t vaddr)
{
    assert(st != NULL);
    // XXX the code has a lot of common structure between the different levels (IMPROVE!)
    errval_t           err = SYS_ERR_OK;
    struct page_table *ptl1;
    err = _pt_table_locate_entry(st, &st->l0, vaddr, VREGION_FLAGS_READ, &ptl1,
                                 /*create_page_if_missing=*/false);
    if (err_is_fail(err)) {
        return err;
    }

    struct page_table *ptl2;
    err = _pt_table_locate_entry(st, ptl1, vaddr, VREGION_FLAGS_READ, &ptl2,
                                 /*create_page_if_missing=*/false);
    if (err_is_fail(err)) {
        return err;
    }

    struct page_table *ptl3;
    err = _pt_table_locate_entry(st, ptl2, vaddr, VREGION_FLAGS_READ, &ptl3,
                                 /*create_page_if_missing=*/false);
    if (err_is_fail(err)) {
        return err;
    }

    uint16_t l0_index = VMSAv8_64_L0_INDEX(vaddr);
    uint16_t l1_index = VMSAv8_64_L1_INDEX(vaddr);
    uint16_t l2_index = VMSAv8_64_L2_INDEX(vaddr);
    uint16_t l3_index = VMSAv8_64_L3_INDEX(vaddr);

    // We never mapped the page (can happen with lazy alloc)
    // There is nothing to do
    if (ptl3 == NULL) {
        return SYS_ERR_OK;
    }

    if (PT_IS_LAZY(ptl3, l3_index)) {
        if (capref_is_null(ptl3->entries[l3_index].frame_cap)) {
            return SYS_ERR_OK;
        }
        err = cap_destroy(ptl3->entries[l3_index].frame_cap);
        if (err_is_fail(err)) {
            return err;
        }
    } else {
        assert(!capref_is_null(ptl3->entries[l3_index].frame_cap));
    }

    --ptl3->num_children;
    ptl3->entries[l3_index].frame_cap = NULL_CAP;

    uint16_t l3_num_children = _pt_table_num_children(st, ptl3);
    if (l3_num_children > 0) {
        return SYS_ERR_OK;
    }
    err = _pt_table_destroy(st, &ptl2->entries[l2_index].pt);
    --ptl2->num_children;

    uint16_t l2_num_children = _pt_table_num_children(st, ptl2);
    if (l2_num_children > 0) {
        return SYS_ERR_OK;
    }
    err = _pt_table_destroy(st, &ptl1->entries[l1_index].pt);
    --ptl1->num_children;

    uint16_t l1_num_children = _pt_table_num_children(st, ptl1);
    if (l1_num_children > 0) {
        return SYS_ERR_OK;
    }
    err = _pt_table_destroy(st, &st->l0.entries[l0_index].pt);
    --st->l0.num_children;

    return SYS_ERR_OK;
}

/**
 * @brief decommits any memory allocated to the given memory region
 *
 * @param[in] st      paging state of the virtual address space unmap in
 * @param[in] vaddr   start of the region
 * @param[in] bytes   length of the region
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure.
 *
 * The function decommit the physical memory commited to a given region
 *
 * Note, the given region is still reserved and memory will be lazily allocated if
 * it is accessed again
 */
errval_t paging_decommit(struct paging_state *st, lvaddr_t vaddr, size_t bytes)
{
    assert(st != NULL);
    if ((vaddr & (BASE_PAGE_SIZE - 1)) == 0 || (bytes & (BASE_PAGE_SIZE - 1)) == 0 || bytes == 0)
        return ERR_INVALID_ARGS;

    thread_mutex_lock_nested(&mm_mutex);

    errval_t err;

    // TODO: check that this memory is indeed reserved
    for (size_t offset = 0; offset < bytes; offset += BASE_PAGE_SIZE) {
        err = _unmap_single_frame(st, vaddr + offset);
        if (err_is_fail(err)) {
            // TODO restore the mapping if we fail, otherwise we will partially unmap the frame.
            thread_mutex_unlock(&mm_mutex);
            return err;
        }
    }

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}

/**
 * @brief Unmaps the region starting at the supplied pointer.
 *
 * @param[in] st      the paging state to create the mapping in
 * @param[in] region  starting address of the region to unmap
 *
 * @return SYS_ERR_OK on success, or error code indicating the kind of failure
 *
 * The supplied `region` must be the start of a previously mapped frame.
 */
errval_t paging_unmap(struct paging_state *st, const void *region)
{
    thread_mutex_lock_nested(&mm_mutex);
    assert(st != NULL);

    errval_t err = SYS_ERR_OK;

    err = _pt_ensure_slab_space(st);
    if (err_is_fail(err))
        return err;

    // TODO handle NULL region correctly
    lvaddr_t vaddr = (lvaddr_t)region;

    size_t bytes;
    err = _vaddr_lookup(st, vaddr, &bytes);
    if (err_is_fail(err)) {
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    // We cannot allocate 0 sized chunks.
    assert(bytes > 0);
    // we should only have allocated aligned.
    assert(vaddr == ROUND_UP(vaddr, BASE_PAGE_SIZE));

    // unmap each region: for now we unmap a single frame at a time
    // OPTIMIZE once this works we can free all at the same level at once.
    for (size_t offset = 0; offset < bytes; offset += BASE_PAGE_SIZE) {
        err = _unmap_single_frame(st, vaddr + offset);
        if (err_is_fail(err)) {
            // TODO restore the mapping if we fail, otherwise we will partially unmap the frame.
            thread_mutex_unlock(&mm_mutex);
            return err;
        }
    }

    err = _vaddr_free(st, vaddr);
    if (err_is_fail(err)) {
        // TODO handle the error correctly.
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}

/**
 * @brief attempts to maps a previously allocated but not yet mapped virtual address space region
 *
 * @param[in] st      paging state of the virtual address space unmap in
 * @param[in] region  page-aligned address virtual address contained within an allocated region
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure.
 *
 * The function does not map the entire region (mapping is done lazily).
 *
 * Note, the supplied virtual address must be contained within an already allocated region.
 * This function does not allocate memory at the provided vaddr (see paging_map_fixed)
 */
errval_t try_map(struct paging_state *st, lvaddr_t vaddr)
{
    thread_mutex_lock_nested(&mm_mutex);

    assert(st != NULL);
    assert(vaddr == ROUND_UP(vaddr, BASE_PAGE_SIZE));

    errval_t err = SYS_ERR_OK;

    lvaddr_t start_vaddr = 0;
    size_t   bytes       = 0;
    err                  = _vaddr_lookup_region(st, vaddr, &start_vaddr, &bytes);
    if (err_is_fail(err)) {
        printf("vaddr: 0x%lx, start_vaddr: 0x%lx, bytes: %u\n", vaddr, start_vaddr, bytes);

        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    // we now know the page was actually allocated.
    struct page_table *ptl3;
    err = _pt_table_lookup(st, vaddr, &ptl3);
    if (err_is_fail(err)) {
        return err;
    }
    uint16_t index = VMSAv8_64_L3_INDEX(vaddr);
    if (ptl3 != NULL && !capref_is_null(ptl3->entries[index].frame_cap)) {
        // page was already mapped. nothing to do here...
        thread_mutex_unlock(&mm_mutex);
        return SYS_ERR_OK;
    }

    // allocate a frame to map to.
    struct capref frame;
    // XXX for now we just lazily map a single new page.
    const size_t alloc_size = BASE_PAGE_SIZE;
    err                     = frame_alloc(&frame, alloc_size, NULL);
    if (err_is_fail(err)) {
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    err = _paging_map_vaddr(st, vaddr, frame, alloc_size, /*offset=*/0, VREGION_FLAGS_READ_WRITE,
                            /*lazy=*/true);
    if (err_is_fail(err)) {
        // TODO handle the error correctly.
        thread_mutex_unlock(&mm_mutex);
        return err;
    }

    thread_mutex_unlock(&mm_mutex);
    return SYS_ERR_OK;
}


errval_t dev_frame_map(struct capref dev_cap, struct capability dev_frame, genpaddr_t base,
                       gensize_t size, void **buf)
{
    errval_t      err = SYS_ERR_OK;
    struct capref df;
    err = slot_alloc(&df);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "slot_alloc");
        return err;
    }
    err = cap_retype(df, dev_cap, base - get_address(&dev_frame), ObjType_DevFrame,
                     ROUND_UP(size, BASE_PAGE_SIZE));
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "cap_retype");
        return err;
    }

    // XXX we are retyping, to possibly pass this to a separate domain in that case we probably
    //     want to pass df_uart via ARGCN; and initialize within the new domain.
    err = paging_map_frame_attr_offset(get_current_paging_state(), buf, size, df, 0,
                                       VREGION_FLAGS_READ_WRITE_NOCACHE);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "paging_map_frame_attr_offset");
        return err;
    }

    return SYS_ERR_OK;
}
