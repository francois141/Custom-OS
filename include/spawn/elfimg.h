/*
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef ELFIMG_H_
#define ELFIMG_H_ 1

#include <aos/aos.h>

/**
 * @brief represents an elf image in memory
 */
struct elfimg {
    struct capref mem;   ///< frame capability backing the elf image
    void         *buf;   ///< pointer to the virtual address of the image in memory
    size_t        size;  ///< the size of the image in bytes
};

/**
 * @brief initializes the elfimg structure with the supplied capability and size
 *
 * @param[out] img   elfimg structure to be initialized
 * @param[in]  mem   capability containing the elf image
 * @param[in]  size  size of the elfimage in bytes
 */
static inline void elfimg_init_with_cap(struct elfimg *img, struct capref mem, size_t size)
{
    img->mem  = mem;
    img->buf  = NULL;
    img->size = size;
}


/**
 * @brief initializes the elfimg structure with the supplied buffer and size
 *
 * @param[out] img   elfimg structure to be initialized
 * @param[in]  buf   pointer to elf image in memory
 * @param[in]  size  size of the elfimage in bytes
 *
 * @return errval_t
 */
static inline void elfimg_init_with_mem(struct elfimg *img, void *buf, size_t size)
{
    img->mem  = NULL_CAP;
    img->buf  = buf;
    img->size = size;
}

/**
 * @brief initializes the elfimg structure from the multiboot module
 *
 * @param[out] img     elfimg structure to be initialized
 * @param[in]  module
 *
 * @return SYS_ERR_OK on success, error value on failure
 */
void elfimg_init_from_module(struct elfimg *img, struct mem_region *module);


/**
 * @brief destroys the elfimg structure, returning its backing memory capability if any
 *
 * @param[in]  img  elfimg structure to be destroyed
 * @param[out] mem  returns the capability of the elfimg structure
 *
 * @return SYS_ERR_OK on success, error value on failure
 */
errval_t elfimg_destroy(struct elfimg *img, struct capref *mem);


/**
 * @brief obtains the size of the elf image in bytes
 *
 * @param[in] img  elfimg structrure to be queried
 *
 * @return the size of the elf image in bytes
 */
static inline size_t elfimg_size(struct elfimg *img)
{
    return img->size;
}


/**
 * @brief obtains the virtual address of the elf image in memory
 *
 * @param[in] img  elfimg structrure to be queried
 *
 * @return pointer to the image in memory, or NULL if it hasn't been mapped
 */
static inline lvaddr_t elfimg_base(struct elfimg *img)
{
    return (lvaddr_t)img->buf;
}


/**
 * @brief maps the elfimg structure into the current address space
 *
 * @param[in] img  elfimg structure to be mapped
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure
 *
 * Note: only map the memory if it is not already mapped and has a capability
 */
errval_t elfimg_map(struct elfimg *img);


/**
 * @brief unmaps a previously mapped elfimg structure
 *
 * @param[in] img  elfimg structure to be unmapped
 *
 * @return SYS_ERR_OK on success, LIB_ERR_* on failure
 *
 * Note: only unmap the memory if it is backed by a supplied capability
 */
errval_t elfimg_unmap(struct elfimg *img);

#endif /* ELFIMG_H_ */