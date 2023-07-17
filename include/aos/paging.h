/*
 * Copyright (c) 2012, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */


#ifndef LIBAOS_PAGING_H
#define LIBAOS_PAGING_H 1

#include <errors/errno.h>
#include <aos/capabilities.h>
#include <aos/slab.h>
#include <barrelfish_kpi/paging_arch.h>
#include <aos/paging_types.h>

// default size of the exception stack
#define PAGING_EXCEPT_STACK_SIZE (1 << 16)
#define PAGING_PAGE_ALIGN(x)     (((x) + BASE_PAGE_SIZE - 1) & ~(BASE_PAGE_SIZE - 1))

// forward declarations
struct thread;
struct paging_state;

// Forward decl
static inline errval_t frame_identify(struct capref frame, struct frame_identity *ret);


/**
 * @brief initializes the virtual memory system, and sets up self-paging
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure.
 */
errval_t paging_init(void);


/**
 * @brief initializes self-paging for the given thread
 *
 * @param[in] thread  the thread to setup paging for.
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure.
 */
errval_t paging_init_onthread(struct thread *thread);


/**
 * @brief initializes the paging state struct for the current process
 *
 * @param[in] st           the paging state to be initialized
 * @param[in] start_vaddr  start virtual address to be managed
 * @param[in] root         capability to the root leve page table
 * @param[in] ca           the slot allocator instance to be used
 *
 * @return SYS_ERR_OK on success, or LIB_ERR_* on failure
 */
errval_t paging_init_state(struct paging_state *st, lvaddr_t start_vaddr, struct capref root,
                           struct slot_allocator *ca);


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
                                   struct capref pdir, struct slot_allocator *ca);


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
errval_t paging_free_state_foreign(struct paging_state *st);


/**
 * @brief allocates (reserves) a region of virtual address space with a given alignment
 * and size.
 *
 * @param[in]  st         paging state of the address space to allocate a region in
 * @param[out] buf        returns the aligned base address of the allocated region
 * @param[in]  bytes      size of the region in bytes
 * @param[in]  alignment  requested alignment of the virtual region
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure
 *
 * This function should always return an unallocated, non-overlapping virtual address
 * region. The function can return a region that has previously been unmapped.
 */
errval_t paging_alloc(struct paging_state *st, void **buf, size_t bytes, size_t alignment);


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
 * @return SYS_ERR_OK on sucecss, LIB_ERR_* on failure.
 */
errval_t paging_map_frame_attr_offset(struct paging_state *st, void **buf, size_t bytes,
                                      struct capref frame, size_t offset, int flags);


/**
 * @brief maps a frame at a free virtual address region and returns its address
 *
 * @param[in]  st     paging state of the address space to create the mapping in
 * @param[out] buf    returns the virtual address of the mapped frame
 * @param[in]  bytes  the amount of bytes to be mapped
 * @param[in]  frame  frame capability of backing memory to be mapped
 * @param[in]  flags  mapping flags
 *
 * @return SYS_ERR_OK on sucecss, LIB_ERR_* on failure.
 */
static inline errval_t paging_map_frame_attr(struct paging_state *st, void **buf, size_t bytes,
                                             struct capref frame, int flags)
{
    return paging_map_frame_attr_offset(st, buf, bytes, frame, 0, flags);
}


/**
 * \brief Finds a free virtual address and maps `bytes` of the supplied frame at the address
 *
 * @param[in]  st      the paging state to create the mapping in
 * @param[out] buf     returns the virtual address at which this frame has been mapped.
 * @param[in]  bytes   the number of bytes to map.
 * @param[in]  frame   the frame capability to be mapped
 *
 * @return Either SYS_ERR_OK if no error occured or an error indicating what went wrong
 * otherwise.
 */
static inline errval_t paging_map_frame(struct paging_state *st, void **buf, size_t bytes,
                                        struct capref frame)
{
    return paging_map_frame_attr(st, buf, bytes, frame, VREGION_FLAGS_READ_WRITE);
}


/**
 * \brief Finds a free virtual address and maps the supplied frame in full at the
 * allocated address
 *
 * @param[in]  st      the paging state to create the mapping in
 * @param[out] buf     returns the virtual address at which this frame has been mapped.
 * @param[in]  frame   the frame capability to be mapped
 *
 * @return Either SYS_ERR_OK if no error occured or an error indicating what went wrong
 * otherwise.
 */
static inline errval_t paging_map_frame_complete(struct paging_state *st, void **buf,
                                                 struct capref frame)
{
    errval_t              err;
    struct frame_identity id;
    err = frame_identify(frame, &id);
    if (err_is_fail(err)) {
        return err;
    }

    return paging_map_frame_attr(st, buf, id.bytes, frame, VREGION_FLAGS_READ_WRITE);
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
 * allocated), otherwise the mapping request shoud fail.
 */
errval_t paging_map_fixed_attr_offset(struct paging_state *st, lvaddr_t vaddr, struct capref frame,
                                      size_t bytes, size_t offset, int flags);


/**
 * @brief maps a frame at a user-provided virtual address region
 *
 * @param[in] st     paging state of the address space to create the mapping in
 * @param[in] vaddr  provided virtual address to map the frame at
 * @param[in] frame  frame capability of backing memory to be mapped
 * @param[in] bytes  the amount of bytes to be mapped
 * @param[in] flags  mapping flags
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure
 */
static inline errval_t paging_map_fixed_attr(struct paging_state *st, lvaddr_t vaddr,
                                             struct capref frame, size_t bytes, int flags)
{
    return paging_map_fixed_attr_offset(st, vaddr, frame, bytes, 0, flags);
}


/**
 * @brief mapps the provided frame at the supplied address in the paging state
 *
 * @param[in] st      the paging state to create the mapping in
 * @param[in] vaddr   the virtual address to create the mapping at
 * @param[in] frame   the frame to map in
 * @param[in] bytes   the number of bytes that will be mapped.
 *
 * @return SYS_ERR_OK
 */
static inline errval_t paging_map_fixed(struct paging_state *st, lvaddr_t vaddr,
                                        struct capref frame, size_t bytes)
{
    return paging_map_fixed_attr(st, vaddr, frame, bytes, VREGION_FLAGS_READ_WRITE);
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
errval_t paging_decommit(struct paging_state *st, lvaddr_t vaddr, size_t bytes);

/**
 * @brief unmaps the previously mapped virtual address space region starting at address
 * `region`
 *
 * @param[in] st      paging state of the virtual address space unmap in
 * @param[in] region  base address of the region to be unmapped
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure.
 *
 * The function unmaps the entire region.
 *
 * Note, the supplied virtual address must correspond to the base address of a
 * allocated virtual region. Invocations with virtual addresses not corresponding
 * to base addresses of allocated virtual address space regions, either within
 * or outside, should result in an error.
 */
errval_t paging_unmap(struct paging_state *st, const void *region);

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
errval_t try_map(struct paging_state *st, lvaddr_t vaddr);

errval_t dev_frame_map(struct capref dev_cap, struct capability dev_frame, genpaddr_t base,
                       gensize_t size, void **buf);

#endif  // LIBAOS_PAGING_H
