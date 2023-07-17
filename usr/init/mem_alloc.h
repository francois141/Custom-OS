/**
 * \file
 * \brief ram allocator functions
 */

/*
 * Copyright (c) 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef _INIT_MEM_ALLOC_H_
#define _INIT_MEM_ALLOC_H_

#include <stdio.h>
#include <aos/aos.h>


/**
 * @brief initializes the local memory allocator, adding memory from the bootinfo
 *
 * @param[in] bi  bootinfo structure containing the memory regions
 *
 * @returns SYS_ERR_OK on success, LIB_ERR_* on failure
 */
errval_t initialize_ram_alloc(struct bootinfo *bi);

/**
 * @brief initializes the local memory allocator, adding a specific region of memory from bootinfo to the memory allocator
 *
 * @param[in] bi  bootinfo structure containing the memory regions
 * @param[in] ram_base base ram location to map
 * @param[in] ram_size size of the ram location to map
 *
 * @returns SYS_ERR_OK on success, LIB_ERR_* on failure
 */
errval_t initialize_ram_alloc_range(struct bootinfo *bi, genpaddr_t ram_base, gensize_t ram_size);

errval_t initialize_ram_alloc_cap(struct capref cap);

/**
 * @brief allocates physical memory with the given size and alignment requirements
 *
 * @param[out] cap        returns the allocated capabilty
 * @param[in]  size       size of the allocation request in bytes
 * @param[in]  alignment  alignment constraint of the allocation request
 *
 * @return SYS_ERR_OK on success, MM_ERR_* on failure
 */
errval_t aos_ram_alloc_aligned(struct capref *cap, size_t size, size_t alignment);


/**
 * @brief allocates physical memory with the given size requirement
 *
 * @param[out] cap        returns the allocated capabilty
 * @param[in]  size       size of the allocation request in bytes
 *
 * @return SYS_ERR_OK on success, MM_ERR_* on failure
 */
static inline errval_t aos_ram_alloc(struct capref *cap, size_t size)
{
    return aos_ram_alloc_aligned(cap, size, BASE_PAGE_SIZE);
}


/**
 * @brief frees previously allocated physical memoyr
 *
 * @param cap  capabilty to the memory that is to be freed
 *
 * @return SYS_ERR_OK on success, MM_ERR_* on failure
 */
errval_t aos_ram_free(struct capref cap);



#endif /* _INIT_MEM_ALLOC_H_ */
