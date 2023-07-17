/**
 * \file
 * \brief Capability system user code
 */

/*
 * Copyright (c) 2007-2010, 2012, 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include "aos/aos_rpc.h"
#include <stdint.h>
#include <stdbool.h>
#include <aos/aos.h>
#include <aos/cspace.h>
#include <aos/caddr.h>
#include <aos/kernel_cap_invocations.h>
#include <aos/lmp_endpoints.h>
#include <aos/aos_rpc.h>
#include <stdio.h>

/// Initializer for the current
#define ROOT_CNODE_INIT                                                                            \
    {                                                                                              \
        .croot = CPTR_ROOTCN, .cnode = 0, .level = CNODE_TYPE_ROOT,                                \
    }

#define TASK_CNODE_INIT                                                                            \
    {                                                                                              \
        .croot = CPTR_ROOTCN, .cnode = CPTR_TASKCN_BASE, .level = CNODE_TYPE_OTHER,                \
    }

#define PAGE_CNODE_INIT                                                                            \
    {                                                                                              \
        .croot = CPTR_ROOTCN, .cnode = CPTR_PAGECN_BASE, .level = CNODE_TYPE_OTHER,                \
    }

#define MODULE_CNODE_INIT                                                                          \
    {                                                                                              \
        .croot = CPTR_ROOTCN, .cnode = CPTR_MODULECN_BASE, .level = CNODE_TYPE_OTHER,              \
    }


/*
 * ------------------------------------------------------------------------------------------------
 * Well-known CNodes
 * ------------------------------------------------------------------------------------------------
 */

/// the own root CNode (L1CNode)
struct cnoderef cnode_root = ROOT_CNODE_INIT;

/// the own task CNode
struct cnoderef cnode_task = TASK_CNODE_INIT;

/// Super CNode
struct cnoderef cnode_memory = {
    .cnode = CPTR_SUPERCN_BASE,
    .level = CNODE_TYPE_OTHER,
    .croot = CPTR_ROOTCN,
};

/// Page CNode
struct cnoderef cnode_page = PAGE_CNODE_INIT;

/// Module CNode
struct cnoderef cnode_module = MODULE_CNODE_INIT;

/*
 * ------------------------------------------------------------------------------------------------
 * Well-known Capabilities
 * ------------------------------------------------------------------------------------------------
 */

/// Capability to Root CNode
struct capref cap_root = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_ROOTCN };

/// Capability for IRQ table
struct capref cap_irq = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_IRQ };

/// Capability for endpoint to self
struct capref cap_selfep = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_SELFEP };

/// Capability for dispatcher
struct capref cap_dispatcher = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_DISPATCHER };

/// Capability for dispatcher
struct capref cap_dispframe = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_DISPFRAME };

/// Capability for ArgSpace
struct capref cap_argcn = { .cnode = ROOT_CNODE_INIT, .slot = ROOTCN_SLOT_ARGCN };

/// Capability for monitor endpoint
struct capref cap_monitorep = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_MONITOREP };

/// Capability for bootinfo (only in monitor)
struct capref cap_bootinfo = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_BOOTINFO };

/// Capability to the multi boot strings
struct capref cap_mmstrings = { .cnode = MODULE_CNODE_INIT, .slot = 0 };

/// Capability for kernel (only in monitor)
struct capref cap_kernel = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_KERNELCAP };

/// Capability for IPI sending (only in monitor)
struct capref cap_ipi = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_IPI };

/// Capability for endpoint to init (only in monitor/mem_serv)
struct capref cap_initep = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_INITEP };

/// Capability to the URPC frame
struct capref cap_urpc = { .cnode = TASK_CNODE_INIT, .slot = TASKCN_SLOT_MON_URPC };

/// Root PML4 VNode
struct capref cap_vroot = {
    .cnode = PAGE_CNODE_INIT,
    .slot  = PAGECN_SLOT_VROOT,
};

/// Cap for the devices
struct capref cap_devices = {
    .cnode = TASK_CNODE_INIT,
    .slot = TASKCN_SLOT_DEV
};

/*
 * ------------------------------------------------------------------------------------------------
 * Remote Capability Operations
 * ------------------------------------------------------------------------------------------------
 */


/**
 * \brief Retype a capability into one or more new capabilities, going through
 * the monitor to ensure consistancy with other cores.  Only necessary for
 * caps that have been sent remotely.
 */
static errval_t cap_retype_remote(struct capref src_root, struct capref dest_root, capaddr_t src,
                                  gensize_t offset, enum objtype new_type, gensize_t objsize,
                                  size_t count, capaddr_t to, capaddr_t slot, int to_level)
{
    return aos_rpc_cap_retype_remote(get_init_rpc(), src_root, dest_root, src, offset, new_type, objsize, count, to,
                                     slot, to_level);
}


/**
 * \brief Delete the given capability, going through  the monitor to ensure
 * consistancy with other cores.  Only necessary for caps that have been sent
 * remotely.
 *
 * \param cap Capability to be deleted
 *
 * Deletes (but does not revoke) the given capability, allowing the CNode slot
 * to be reused.
 */
static errval_t cap_delete_remote(struct capref root, capaddr_t src, uint8_t level)
{
    return aos_rpc_cap_delete_remote(get_init_rpc(), root, src, level);
}

/**
 * \brief Revoke (delete all copies and descendants of) the given capability,
 * going through the monitor to ensure consistancy with other cores.  Only
 * necessary for caps that have been sent remotely.
 *
 * \param cap Capability to be revoked
 *
 * Deletes all copies and descendants of the given capability, but not the
 * capability itself. If this succeeds, the capability is guaranteed to be
 * the only copy in the system.
 */
static errval_t cap_revoke_remote(struct capref root, capaddr_t src, uint8_t level)
{
    return aos_rpc_cap_revoke_remote(get_init_rpc(), root, src, level);
}


/*
 * ------------------------------------------------------------------------------------------------
 * Generic Capability Operations
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Copy the soruce capability into the destination slot.
 *
 * @param[out] dest    Location of destination slot, which must be empty
 * @param[in]  src     Location of source capability
 *
 * @returns error value indicating success or failure of the operation
 *   - @retval SYS_ERR_OK capability has been successfully deleted
 *   - @retval SLOT_IN_USE if the destination slot was not empty
 *
 * @note The source and destination of the capability can be in different CSpaces.
 */
errval_t cap_copy(struct capref dest, struct capref src)
{
    capaddr_t       dcs_addr  = get_croot_addr(dest);
    capaddr_t       dcn_addr  = get_cnode_addr(dest);
    enum cnode_type dcn_level = get_cnode_level(dest);

    capaddr_t       scp_root  = get_croot_addr(src);
    capaddr_t       scp_addr  = get_cap_addr(src);
    enum cnode_type scp_level = get_cap_level(src);

    return invoke_cnode_copy(cap_root, dcs_addr, dcn_addr, dest.slot, scp_root, scp_addr, dcn_level,
                             scp_level);
}


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
errval_t cap_mint(struct capref dest, struct capref src, uint64_t param1, uint64_t param2)
{
    capaddr_t       dcs_addr  = get_croot_addr(dest);
    capaddr_t       dcn_addr  = get_cnode_addr(dest);
    enum cnode_type dcn_level = get_cnode_level(dest);

    capaddr_t       scp_root  = get_croot_addr(src);
    capaddr_t       scp_addr  = get_cap_addr(src);
    enum cnode_type scp_level = get_cap_level(src);

    return invoke_cnode_mint(cap_root, dcs_addr, dcn_addr, dest.slot, scp_root, scp_addr, dcn_level,
                             scp_level, param1, param2);
}

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
                         enum objtype new_type, gensize_t objsize, size_t count)
{
    errval_t err;

    capaddr_t       dcs_addr  = get_croot_addr(dest);
    capaddr_t       dcn_addr  = get_cnode_addr(dest);
    enum cnode_type dcn_level = get_cnode_level(dest);

    capaddr_t scp_root = get_croot_addr(src);
    capaddr_t scp_addr = get_cap_addr(src);

    err = invoke_cnode_retype(cap_root, scp_root, scp_addr, offset, new_type, objsize, count,
                              dcs_addr, dcn_addr, dcn_level, dest.slot);
    if (err_no(err) == SYS_ERR_RETRY_THROUGH_MONITOR) {
        struct capref src_root  = get_croot_capref(src);
        struct capref dest_root = get_croot_capref(dest);
        return cap_retype_remote(src_root, dest_root, scp_addr, offset, new_type, objsize, count,
                                 dcn_addr, dest.slot, dcn_level);
    } else {
        return err;
    }
}


/**
 * @brief creates a new capability with the given size and type (limited )
 *
 * @param[out] dest   capref to an empty slot in a CSpace to hold the created capablity
 * @param[in]  type   object type of the derived capaility
 * @param[in]  bytes  size of the created capability (ignored for fixed-size object)
 *
 * @returns
 *   - @retval SYS_ERR_OK on success
 *   - @retval SYS_ERR_TYPE_NOT_CREATABLE if ihe type doesn't permit creation
 *
 * Only certain types of capabilities can be created this way. If invoked on a capability type,
 * that is not creatable at runtime the error SYS_ERR_TYPE_NOT_CREATABLE is returned. Most
 * capabilities have to be retyped from other capabilities with cap_retype().
 */
errval_t cap_create(struct capref dest, enum objtype type, size_t size)
{
    // Address of the cap to the destination CNode
    capaddr_t       dest_cnode_cptr  = get_cnode_addr(dest);
    enum cnode_type dest_cnode_level = get_cnode_level(dest);

    return invoke_cnode_create(cap_root, type, size, dest_cnode_cptr, dest_cnode_level, dest.slot);
}


/**
 * @brief Delete the given capability
 *
 * @param cap  capref to the capability to be deleted
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
errval_t cap_delete(struct capref cap)
{
    errval_t err;

    struct capref   croot = get_croot_capref(cap);
    capaddr_t       caddr = get_cap_addr(cap);
    enum cnode_type level = get_cap_level(cap);

    err = invoke_cnode_delete(croot, caddr, level);
    if (err_no(err) == SYS_ERR_RETRY_THROUGH_MONITOR) {
        return cap_delete_remote(croot, caddr, level);
    } else {
        return err;
    }
}


/**
 * @brief Destroys the given capability (delete + free slot)
 *
 * @param cap  capref to the capability to be destroyed
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
errval_t cap_destroy(struct capref cap)
{
    errval_t err;
    err = cap_delete(cap);
    if (err_is_fail(err)) {
        return err;
    }

    err = slot_free(cap);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_WHILE_FREEING_SLOT);
    }

    return SYS_ERR_OK;
}


/**
 * @brief  Revoke (delete all other copies and descendants) the given capability
 *
 * @param cap  capref to the capability to be revoked
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
errval_t cap_revoke(struct capref cap)
{
    errval_t err;

    struct capref   croot = get_croot_capref(cap);
    capaddr_t       caddr = get_cap_addr(cap);
    enum cnode_type level = get_cap_level(cap);

    err = invoke_cnode_revoke(croot, caddr, level);
    if (err_no(err) == SYS_ERR_RETRY_THROUGH_MONITOR) {
        return cap_revoke_remote(croot, caddr, level);
    } else {
        return err;
    }
}


/*
 * ------------------------------------------------------------------------------------------------
 * CNode Creation
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Creates a new L1 CNode
 *
 * @param[out] ret_dest  returns the capref to the newly create CNode
 * @param[out] cnoderef  returns the cnoderef to the newly created CNode if not NULL
 *
 * @return errval_t
 *
 * @note The newly created L1 CNode has the same initial size of the L2 CNodes (L2_CNODE_SLOTS).
 * The function will allocate a new slot in the caller's CSpace and allocates memory for the new
 * L1 CNode.
 */
errval_t cnode_create_l1(struct capref *ret_dest, struct cnoderef *cnoderef)
{
    errval_t err;

    // Allocate a slot in root cn for destination
    assert(ret_dest != NULL);
    err = slot_alloc(ret_dest);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    return cnode_create_raw(*ret_dest, cnoderef, ObjType_L1CNode, L2_CNODE_SLOTS);
}


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
errval_t cnode_create_l2(struct capref *ret_dest, struct cnoderef *cnoderef)
{
    errval_t err;

    // Allocate a slot in root cn for destination
    assert(ret_dest != NULL);
    err = slot_alloc_root(ret_dest);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    return cnode_create_raw(*ret_dest, cnoderef, ObjType_L2CNode, L2_CNODE_SLOTS);
}


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
                               struct cnoderef *cnoderef, size_t slots)
{
    errval_t err;

    // must be of a CNode type
    if (cntype != ObjType_L1CNode && cntype != ObjType_L2CNode) {
        return LIB_ERR_CNODE_TYPE;
    }

    // Retype it to the destination
    err = cap_retype(dest, src, 0, cntype, slots * OBJSIZE_CTE);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_RETYPE);
    }

    // Construct the cnoderef to return
    if (cnoderef != NULL) {
        enum cnode_type ref_cntype = (cntype == ObjType_L1CNode) ? CNODE_TYPE_ROOT
                                                                 : CNODE_TYPE_OTHER;

        *cnoderef = build_cnoderef(dest, ref_cntype);
    }

    return SYS_ERR_OK;
}


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
errval_t cnode_create_foreign_l2(struct capref dest_l1, cslot_t dest_slot, struct cnoderef *cnoderef)
{
    if (capref_is_null(dest_l1)) {
        return LIB_ERR_CROOT_NULL;
    }

    struct capref dest;
    dest.cnode = build_cnoderef(dest_l1, CNODE_TYPE_ROOT);
    dest.slot  = dest_slot;

    // Create proper cnoderef for foreign L2
    if (cnoderef) {
        cnoderef->croot = get_cap_addr(dest_l1);
        cnoderef->cnode = ROOTCN_SLOT_ADDR(dest_slot);
        cnoderef->level = CNODE_TYPE_OTHER;
    }

    return cnode_create_raw(dest, NULL, ObjType_L2CNode, L2_CNODE_SLOTS);
}


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
                          cslot_t slots)
{
    errval_t err, err2;

    // the cnode type must match
    if (cntype != ObjType_L1CNode && cntype != ObjType_L2CNode) {
        return LIB_ERR_CNODE_TYPE;
    }

    // the number of slots must be at least L2_CNODE_SLOTS,
    // and if it is an L2Cnode exactly L2_CNODE_SLOTS
    if (slots < L2_CNODE_SLOTS || (cntype == ObjType_L2CNode && slots != L2_CNODE_SLOTS)) {
        return LIB_ERR_CNODE_SLOTS;
    }

    // Allocate some memory
    struct capref ram;
    err = ram_alloc(&ram, slots * OBJSIZE_CTE);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_RAM_ALLOC);
    }

    err = cnode_create_from_mem(dest, ram, cntype, cnoderef, slots);
    if (err_is_fail(err)) {
        err = err_push(err, LIB_ERR_CNODE_CREATE_FROM_MEM);
    }

    // we don't need the RAM cap anymore, free it
    err2 = cap_destroy(ram);
    if (err_is_fail(err2)) {
        // XXX: here the cap_destroy has failed, but we have successfully retyped
        //      above. So this should not fail, so we barf here.
        DEBUG_ERR(err, "BUG: cap destroy failed while creating CNode.");
        return err_push(err, LIB_ERR_CAP_DESTROY);
    }

    return err;
}

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
errval_t cnode_replace_own_l1(struct capref new, struct capref ret)
{
    assert(get_croot_addr(new) == CPTR_ROOTCN);
    assert(get_cap_level(new) == CNODE_TYPE_COUNT);
    capaddr_t new_cptr = get_cap_addr(new);

    assert(get_croot_addr(ret) == CPTR_ROOTCN);
    assert(get_cap_level(ret) == CNODE_TYPE_COUNT);
    capaddr_t retcn_ptr = get_cnode_addr(ret);

    return invoke_cnode_resize(cap_root, new_cptr, retcn_ptr, ret.slot);
}

/*
 * ------------------------------------------------------------------------------------------------
 * Creation of Specific Capability Types
 * ------------------------------------------------------------------------------------------------
 */


/**
 * @brief Create a Frame cap referring to newly-allocated RAM in a given slot
 *
 * @param[out] dest     Empty slot in the CSpace to place the created capablity in
 * @param[in]  bytes    Minimum size of frame to create
 * @param[out] retbytes If non-NULL, filled in with size of created frame
 *
 * @return error value indicating the success or failure of the frame creation
 *   - @retval SYS_ERR_OK           if the frame has been successfully created
 *   - @retval LIB_ERR_RAM_ALLOC    the allocation of the RAM capability has failed
 *   - @retval LIB_ERR_CAP_RETYPE   retype operation has failed
 *   - @retval LIB_ERR_CAP_DESTROY  destroying of the RAM capability has failed
 *
 * @note This function requires that dest refer to an existing but empty slot.
 * The function uses `ram_alloc` to allocate
 *
 * This function will returns a special error code if ram_alloc fails
 * due to the constrains on the memory server (size of cap or region
 * of memory). This is to facilitate retrying with different constraints.
 */
errval_t frame_create(struct capref dest, size_t bytes, size_t *retbytes)
{
    assert(bytes > 0);
    errval_t err;

    if (capref_is_null(dest)) {
        return LIB_ERR_CAP_WAS_NULL;
    }

    bytes = ROUND_UP(bytes, BASE_PAGE_SIZE);

    struct capref ram;
    err = ram_alloc(&ram, bytes);
    if (err_is_fail(err)) {
        if (err_no(err) == MM_ERR_NOT_FOUND || err_no(err) == LIB_ERR_RAM_ALLOC_WRONG_SIZE) {
            return err_push(err, LIB_ERR_RAM_ALLOC_MS_CONSTRAINTS);
        }
        return err_push(err, LIB_ERR_RAM_ALLOC);
    }

    err = cap_retype(dest, ram, 0, ObjType_Frame, bytes);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_RETYPE);
    }

    err = cap_destroy(ram);
    if (err_is_fail(err)) {
        // XXX: here the cap_destroy has failed, but we have successfully retyped
        //      above. So this should not fail, so we barf here.
        DEBUG_ERR(err, "BUG: cap destroy failed while creating a frame.");
        return err_push(err, LIB_ERR_CAP_DESTROY);
    }

    if (retbytes != NULL) {
        *retbytes = bytes;
    }

    return SYS_ERR_OK;
}

/**
 * \brief Create a Frame cap referring to newly-allocated RAM in an allocated slot
 *
 * \param dest  Pointer to capref struct, filled-in with location of new cap
 * \param bytes Minimum size of frame to create
 * \param retbytes If non-NULL, filled in with size of created frame
 */
errval_t frame_alloc(struct capref *dest, size_t bytes, size_t *retbytes)
{
    errval_t err = slot_alloc(dest);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    return frame_create(*dest, bytes, retbytes);
}


/**
 * \brief Create a VNode in newly-allocated memory
 *
 * \param dest location to place new VNode cap
 * \param type VNode type to create
 *
 * This function requires that dest refer to an existing but empty slot.
 * The intermidiate ram cap is destroyed.
 */
errval_t vnode_create(struct capref dest, enum objtype type)
{
    errval_t err;

    struct capref ram;

    err = ram_alloc_aligned(&ram, vnode_objsize(type), vnode_objsize(type));
    if (err_no(err) == LIB_ERR_RAM_ALLOC_WRONG_SIZE && type != ObjType_VNode_ARM_l1) {
        // can only get 4kB pages, cannot create ARM_l1, and waste 3kB for
        // ARM_l2
        err = ram_alloc(&ram, BASE_PAGE_SIZE);
    }
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_RAM_ALLOC);
    }

    assert(type_is_vnode(type));
    err = cap_retype(dest, ram, 0, type, vnode_objsize(type));
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_RETYPE);
    }

    err = cap_destroy(ram);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_DESTROY);
    }

    return SYS_ERR_OK;
}


/**
 * \brief Create a dispatcher capability and store it in the slot pointed to
 * by 'dest'
 *
 * This function requires that dest refers to an existing but empty slot. It
 * allocates a new RAM cap, retypes it to one of type ObjType_Dispatcher and
 * stores it at the slot pointed to by the capref struct 'dest'. The intermediate
 * ram cap is then destroyed.
 *
 * \param dest location to place new dispatcher cap
 *
 * \return Either SYS_ERR_OK if no error occured or an error
 * indicating what went wrong otherwise.
 */
errval_t dispatcher_create(struct capref dest)
{
    errval_t err;

    struct capref ram;
    assert(1 << log2ceil(OBJSIZE_DISPATCHER) == OBJSIZE_DISPATCHER);
    err = ram_alloc(&ram, OBJSIZE_DISPATCHER);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_RAM_ALLOC);
    }

    err = cap_retype(dest, ram, 0, ObjType_Dispatcher, 0);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_RETYPE);
    }

    err = cap_destroy(ram);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_DESTROY);
    }
    return SYS_ERR_OK;
}

/**
 * \brief Create endpoint to caller on current dispatcher.
 *
 * \param buflen  Length of incoming LMP buffer, in words
 * \param retcap  Pointer to capref struct, filled-in with location of cap
 * \param retep   Double pointer to LMP endpoint, filled-in with allocated EP
 */
errval_t endpoint_create(size_t buflen, struct capref *retcap, struct lmp_endpoint **retep)
{
    errval_t err = slot_alloc(retcap);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    return lmp_endpoint_create_in_slot(buflen, *retcap, retep);
}


/**
 * \brief Create an ID cap in a newly allocated slot.
 *
 * \param dest  Pointer to capref struct, filld-in with location of new cap.
 *
 * The caller is responsible for revoking the cap after using it.
 */
errval_t idcap_alloc(struct capref *dest)
{
    errval_t err = slot_alloc(dest);

    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    return idcap_create(*dest);
}

/**
 * \brief Create an ID cap in the specified slot.
 *
 * \param dest  Capref, where ID cap should be created.
 *
 * The caller is responsible for revoking the cap after using it.
 */
errval_t idcap_create(struct capref dest)
{
    return cap_create(dest, ObjType_ID, 0);
}

/**
 * \brief Builds a #cnoderef struct from a #capref struct using cap
 *        identification.
 *
 * \param cnoder Pointer to a cnoderef struct, fill-in by function.
 * \param capr   Capref to a CNode capability.
 */
errval_t cnode_build_cnoderef(struct cnoderef *cnoder, struct capref capr)
{
    struct capability cap;
    errval_t          err = debug_cap_identify(capr, &cap);
    if (err_is_fail(err)) {
        return err;
    }

    if (cap.type != ObjType_L1CNode && cap.type != ObjType_L2CNode) {
        return LIB_ERR_NOT_CNODE;
    }

    if (!cnodecmp(capr.cnode, cnode_root)) {
        USER_PANIC("cnode_build_cnoderef NYI for non rootcn caprefs");
    }

    cnoder->croot = get_croot_addr(capr);
    cnoder->cnode = capr.slot << L2_CNODE_BITS;
    cnoder->level = CNODE_TYPE_OTHER;

    return SYS_ERR_OK;
}
