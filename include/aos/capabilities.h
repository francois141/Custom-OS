/**
 * @file
 * @brief Base capability/cnode handling functions.
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2012, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef INCLUDEBARRELFISH_CAPABILITIES_H
#define INCLUDEBARRELFISH_CAPABILITIES_H

#include <stdint.h>
#include <sys/cdefs.h>

#include <barrelfish_kpi/types.h>
#include <barrelfish_kpi/capabilities.h>
#include <barrelfish_kpi/dispatcher_shared.h>
#include <barrelfish_kpi/distcaps.h>

#include <aos/slot_alloc.h>
#include <aos/cap_predicates.h>
#include <aos/invocations.h>
__BEGIN_DECLS

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Generic Capability Operations
//
////////////////////////////////////////////////////////////////////////////////////////////////////


/**
 * @brief Copy the soruce capability into the destination slot.
 *
 * @param[out] dest    Location of destination slot, which must be empty
 * @param[in]  src     Location of source capability
 *
 * @returns error value indicating success or failure of the operation
 *  - @retval SYS_ERR_OK   capability has been successfully deleted
 *  - @retval SLOT_IN_USE  if the destination slot was not empty
 *  - @retcal
 *
 * @note The source and destination of the capability can be in different CSpaces.
 */
errval_t cap_copy(struct capref dest, struct capref src);


/**
 * @brief Mint (Copy changing type-specific parameters) a capability
 *
 * @param[out] dest    Location of destination slot, which must be empty
 * @param[in]  src     The source capability to be minted
 * @param[in]  param1  Type-specific parameter 1
 * @param[in]  param2  Type-specific parameter 2
 *
 * @returns error value indicating success or failure of the operation
 *   - @retval SYS_ERR_OK capability has been successfully deleted
 *
 * @note Consult the Barrelfish Kernel API Specification for the meaning of the
 * type-specific parameters. Minting doesn't change the type of the capability.
 */
errval_t cap_mint(struct capref dest, struct capref src, uint64_t param1, uint64_t param2);


/**
 * @brief Retype (part of) a capability into a new capabilities of the given type and size
 *
 * @param[out] dest      capref to an empty slot in a CSpace to hold the derived capablity
 * @param[in]  src       capref to the capability to be retype from
 * @param[in]  offset    offset into the source capability
 * @param[in]  new_type  object type of the derived capabilities
 * @param[in]  objsize   size of the derived capability (ignored for fixed-size object)
 * @param[in]  count     number of objects to be derived
 *
 * @returns error value indicating success or failure of the operation
 *   - SYS_ERR_OK on success
 *
 * @note When retyping IRQSrc capabilities, offset and objsize represent the start
 * and end of the to be created interrupt range. Count must be 1 for IRQSrc.
 */
errval_t cap_retype_many(struct capref dest, struct capref src, gensize_t offset,
                         enum objtype new_type, gensize_t objsize, size_t count);


/**
 * @brief Retype (part of) a capability into a new capability of the given type and size
 *
 * @param[out] dest      capref to an empty slot in a CSpace to hold the derived capablity
 * @param[in]  src       capref to the capability to be retype from
 * @param[in]  offset    offset into the source capability
 * @param[in]  new_type  object type of the derived capaility
 * @param[in]  objsize   size of the derived capability (ignored for fixed-size object)
 *
 * @returns error value indicating success or failure of the operation
 *   - SYS_ERR_OK on success
 *
 * @note When retyping IRQSrc capabilities, offset and objsize represent the start
 * and end of the to be created interrupt range. Count must be 1 for IRQSrc.
 */
static inline errval_t cap_retype(struct capref dest, struct capref src, gensize_t offset,
                                  enum objtype new_type, gensize_t objsize)
{
    return cap_retype_many(dest, src, offset, new_type, objsize, 1);
}


/**
 * @brief creates a new capability with the given size and type (limited )
 *
 * @param[out] dest   capref to an empty slot in a CSpace to hold the created capablity
 * @param[in]  type   object type of the derived capaility
 * @param[in]  bytes  size of the created capability (ignored for fixed-size object)
 *
 * @returns error value indicating success or failure of the operation
 *   - @retval SYS_ERR_OK on success
 *   - @retval SYS_ERR_TYPE_NOT_CREATABLE if ihe type doesn't permit creation
 *
 * Only certain types of capabilities can be created this way. If invoked on a capability type,
 * that is not creatable at runtime the error SYS_ERR_TYPE_NOT_CREATABLE is returned. Most
 * capabilities have to be retyped from other capabilities with cap_retype().
 */
errval_t cap_create(struct capref dest, enum objtype type, size_t bytes);


/**
 * @brief Delete the given capability
 *
 * @param[in] cap  capref to the capability to be deleted
 *
 * @returns error value indicating success or failure of the operation
 *   - @retval SYS_ERR_OK capability has been successfully deleted
 *   - @retval SYS_ERR_CAP_NOT_FOUND capability does not exist
 *
 * Deletes (but does not revoke) the given capability, allowing the CNode slot to be reused.
 *
 * @note in some cases deleting the capability is not trivial and will require support
 * from the monitor to complete. In this case the kernel returns SYS_ERR_RETRY_THROUGH_MONITOR
 * and the function will try to delete the capability through an RPC call to the monitor.
 */
errval_t cap_delete(struct capref cap);


/**
 * @brief Destroys the given capability (delete + free slot)
 *
 * @param[in] cap  capref to the capability to be destroyed
 *
 * @returns error value indicating success or failure of the operation
 *   - @retval SYS_ERR_OK capability has been successfully deleted
 *   - @retval SYS_ERR_CAP_NOT_FOUND capability does not exist
 *
 * Deletes (but does not revoke) the given capability, and frees the slot by returning it to the
 * default slot allocator allowing for reuse with calls to slot_alloc().
 *
 * @note in some cases deleting the capability is not trivial and will require support
 * from the monitor to complete. In this case the kernel returns SYS_ERR_RETRY_THROUGH_MONITOR
 * and the function will try to delete the capability through an RPC call to the monitor.
 */
errval_t cap_destroy(struct capref cap);


/**
 * @brief  Revoke (delete all other copies and descendants) the given capability
 *
 * @param[in] cap  capref to the capability to be revoked
 *
 * @returns error value indicating success or failure of the operation
 *   - @retval SYS_ERR_OK capability has been successfully deleted
 *   - @retval SYS_ERR_CAP_NOT_FOUND capability does not exist
 *
 * Deletes all copies and descendants of the given capability, but not the
 * capability itself. If this succeeds, the capability is guaranteed to be
 * the only copy in the system.
 *
 * @note revoking the capability is a potentially expensive operation and thus may require
 * involvement of the monitor. The kernel indicates this by returning the error code
 * SYS_ERR_RETRY_THROUGH_MONITOR and the function will trigger an RPC call to the monitor
 */
errval_t cap_revoke(struct capref cap);


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// CNode Creation
//
////////////////////////////////////////////////////////////////////////////////////////////////////


/**
 * @brief Creates a new L1 CNode
 *
 * @param[out] ret_dest  returns the capref to the newly create CNode
 * @param[out] cnoderef  returns the cnoderef to the newly created CNode if not NULL
 *
 * @returns error value indicating the success or failure of the CNode creation
 *   - @retval SYS_ERR_OK  if the CNode has been successfully created
 *   - @retval LIB_ERR_SLOT_ALLOC   if the slot to hold the CNode capability could not be allocated
 *
 * @note The newly created L1 CNode has the same initial size of the L2 CNodes (L2_CNODE_SLOTS).
 * The function will allocate a new slot in the caller's CSpace and allocates memory for the new
 * L1 CNode.
 */
errval_t cnode_create_l1(struct capref *ret_dest, struct cnoderef *cnoderef);


/**
 * @brief Creates a new L2 CNode in the caller's CSpace
 *
 * @param[out] ret_dest  returns the capref to the newly create CNode
 * @param[out] cnoderef  returns the cnoderef to the newly created CNode if not NULL
 *
 * @returns error value indicating the success or failure of the CNode creation
 *   - @retval SYS_ERR_OK  if the CNode has been successfully created
 *   - @retval LIB_ERR_SLOT_ALLOC   if the slot to hold the CNode capability could not be allocated
 *
 * @note The newly created L2 CNode has L2_CNODE_SLOTS slots. The function will allocate a new
 * slot in the caller's root CNode and allocates memory for the new L2 CNode. This may trigger
 * resizing of the L1 CNode.
 */
errval_t cnode_create_l2(struct capref *ret_dest, struct cnoderef *cnoderef);


/**
 * @brief Creates a new L2 CNode in the supplied CSpace
 *
 * @param[in]  dest_l1    Capability to the L1CNode representing the foreign CSpace
 * @param[in]  dest_slot  Slot in the destination L1 CNode to create the new L2 CNode in
 * @param[out] cnoderef   Returns the cnoderef to the newly created CNode
 *
 * @returns  error value indicating the success or failure of the CNode creation
 *   - @retval SYS_ERR_OK  if the CNode has been successfully created
 *
 * @note The newly created L2 CNode has L2_CNODE_SLOTS slots. The function will allocate a new
 * slot in the caller's root CNode and allocates memory for the new L2 CNode. This may trigger
 * resizing of the L1 CNode.
 */
errval_t cnode_create_foreign_l2(struct capref dest_l1, cslot_t dest_slot,
                                 struct cnoderef *cnoderef);

/**
 * \brief Create a CNode from newly-allocated RAM in the given slot
 *
 * \param dest location in which to place CNode cap
 * \param cnoderef cnoderef struct, filled-in if non-NULL with relevant info
 * \param cntype, type of new cnode
 * \param slots Minimum number of slots in created CNode
 * \param retslots If non-NULL, filled in with the  number of slots in created CNode
 *
 * This function requires that dest refer to an existing but empty slot. It
 * allocates memory (using #ram_alloc), and retypes that memory to a new CNode.
 * The intermediate ram cap is destroyed.
 */


/**
 * @brief Creates a new CNode from newly allocated RAM in the given slot
 *
 * @param[in]  dest      An empty slot in the CSpace.
 * @param[out] cnoderef  returns the cnoderef to the newly created CNode if not NULL
 * @param[in]  cntype    the type of the CNode to be created
 * @param[in]  slots     the number of slots of the CNode to be created
 *
 * @return error value indicating the success or failure of the CNode creation
 *   - @retval SYS_ERR_OK  if the CNode has been successfully created
 *   - @retval LIB_ERR_CNODE_TYPE   if the supplied capability type wasn't a CNode
 *   - @retval LIB_ERR_CNODE_SLOTS  if the number of slots was wrong (L2Cnode)
 */
errval_t cnode_create_raw(struct capref dest, struct cnoderef *cnoderef, enum objtype cntype,
                          cslot_t slots);


/**
 * @brief Creates a new CNode using the supplied RAM capability and slot
 *
 * @param[in]  dest      An empty slot in the CSpace to create the new CNode in
 * @param[in]  src       RAM capability that is retyped into an CNode
 * @param[in]  cntype    the type of the CNode to be create (ObjType_L1CNode or ObjType_L2CNode)
 * @param[out] cnoderef  returns the cnoderef to the newly created CNode if not NULL
 * @param[in]  slots     the number of slots to create the new CNode
 *
 * @return error value indicating the success or failure of the CNode creation
 *  - @retval SYS_ERR_OK  if the CNode has been successfully created
 *  - @retval LIB_ERR_CNODE_TYPE  if the supplied capability type is not a CNode
 *  - @retval LIB_ERR_CAP_RETYPE  if the retype operation has failed
 *
 * @note This function requires that dest refer to an existing but empty slot. It retypes the given
 * memory to a new CNode.
 */
errval_t cnode_create_from_mem(struct capref dest, struct capref src, enum objtype cntype,
                               struct cnoderef *cnoderef, size_t slots);


/**
 * @brief replaces the own L1Cnode with the supplied one
 *
 * @param[in]  cn   L1Cnode capability to replace the own L1Cnode with
 * @param[out] ret  An empty slot in the CSpace to return the old L1Cnode in
 *
 * @return error value indicating the success or failure of the CNode creation
 *  - @retval SYS_ERR_OK  if the CNode has been successfully replaced
 *  - @retval SYS_ERR_SLOT_IN_USE if any of the destination slots were in use
 *
 * @note This function copies the capabilities of the current L1Cnode into the new one.
 * If the supplied L1Cnode is smaller than the current one, it will only copy the capabilities
 * that fit into the new L1Cnode.
 */
errval_t cnode_replace_own_l1(struct capref cn, struct capref ret);


errval_t cnode_build_cnoderef(struct cnoderef *cnoder, struct capref capr);
errval_t cnode_build_cnoderef_for_l1(struct cnoderef *cnoder, struct capref capr);


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Creation of Specific Capability Types
//
////////////////////////////////////////////////////////////////////////////////////////////////////

errval_t frame_create(struct capref dest, size_t bytes, size_t *retbytes);
errval_t frame_alloc(struct capref *dest, size_t bytes, size_t *retbytes);

errval_t vnode_create(struct capref dest, enum objtype type);
errval_t vnode_alloc(struct capref *dest, enum objtype type);

errval_t idcap_create(struct capref dest);
errval_t idcap_alloc(struct capref *dest);






errval_t dispatcher_create(struct capref dest);



struct lmp_endpoint;

errval_t endpoint_create(size_t buflen, struct capref *retcap, struct lmp_endpoint **retep);
errval_t ump_endpoint_create(struct capref dest, size_t bytes);


/**
 * \brief Perform mapping operation in kernel by minting a cap to a VNode
 *
 * \param dest destination VNode cap
 * \param src  source Frame cap
 * \param slot slot in destination VNode
 * \param attr Architecture-specific page (table) attributes
 * \param off Offset from source frame to map (must be page-aligned)
 */
static inline errval_t vnode_map(struct capref dest, struct capref src, capaddr_t slot,
                                 uint64_t attr, uint64_t off, uint64_t pte_count,
                                 struct capref mapping)
{
    assert(get_croot_addr(dest) == CPTR_ROOTCN);

    capaddr_t       sroot  = get_croot_addr(src);
    capaddr_t       saddr  = get_cap_addr(src);
    enum cnode_type slevel = get_cap_level(src);

    enum cnode_type mcn_level = get_cnode_level(mapping);
    capaddr_t       mcn_addr  = get_cnode_addr(mapping);
    capaddr_t       mcn_root  = get_croot_addr(mapping);

    return invoke_vnode_map(dest, slot, sroot, saddr, slevel, attr, off, pte_count, mcn_root,
                            mcn_addr, mcn_level, mapping.slot);
}

static inline errval_t vnode_unmap(struct capref pgtl, struct capref mapping)
{
    capaddr_t       mapping_addr = get_cap_addr(mapping);
    enum cnode_type level        = get_cap_level(mapping);

    return invoke_vnode_unmap(pgtl, mapping_addr, level);
}

static inline errval_t vnode_modify_flags(struct capref pgtl, size_t entry, size_t num_pages,
                                          uint64_t attr)
{
    return invoke_vnode_modify_flags(pgtl, entry, num_pages, attr);
}

static inline errval_t vnode_copy_remap(struct capref dest, struct capref src, capaddr_t slot,
                                        uint64_t attr, uint64_t off, uint64_t pte_count,
                                        struct capref mapping)
{
    enum cnode_type slevel = get_cap_level(src);
    capaddr_t       saddr  = get_cap_addr(src);

    enum cnode_type mcn_level = get_cnode_level(mapping);
    capaddr_t       mcn_addr  = get_cnode_addr(mapping);

    return invoke_vnode_copy_remap(dest, slot, saddr, slevel, attr, off, pte_count, mcn_addr,
                                   mapping.slot, mcn_level);
}


static inline errval_t cap_get_state(struct capref cap, distcap_state_t *state)
{
    capaddr_t       caddr = get_cap_addr(cap);
    enum cnode_type level = get_cap_level(cap);

    return invoke_cnode_get_state(cap_root, caddr, level, state);
}

/**
 * \brief identify any capability.
 * \param cap capref of the cap to identify
 * \param ret pointer to struct capability to fill out with the capability
 */
static inline errval_t cap_direct_identify(struct capref cap, struct capability *ret)
{
    return invoke_cap_identify(cap, ret);
}

/**
 * \brief Identify a mappable capability.
 *
 * \param cap the capability to identify
 * \param ret A pointer to a `struct frame_identify` to fill in
 * \returns An error if identified cap is not mappable.
 */
static inline errval_t cap_identify_mappable(struct capref cap, struct frame_identity *ret)
{
    errval_t          err;
    struct capability thecap;
    assert(ret);

    /* zero ret */
    ret->base  = 0;
    ret->bytes = 0;
    ret->pasid = 0;

    err = cap_direct_identify(cap, &thecap);
    if (err_is_fail(err)) {
        return err;
    }

    if (!type_is_mappable(thecap.type)) {
        return LIB_ERR_CAP_NOT_MAPPABLE;
    }

    ret->base  = get_address(&thecap);
    ret->bytes = get_size(&thecap);
    ret->pasid = get_pasid(&thecap);

    return SYS_ERR_OK;
}

/**
 * \brief Identify a frame. This wraps the invocation so we can handle the
 *        case where the Frame cap is not invokable.
 * \param cap the capability to identify
 * \param ret A pointer to a `struct frame_identify` to fill in
 */
static inline errval_t frame_identify(struct capref frame, struct frame_identity *ret)
{
    return cap_identify_mappable(frame, ret);
}


__END_DECLS

#endif  // INCLUDEBARRELFISH_CAPABILITIES_H
