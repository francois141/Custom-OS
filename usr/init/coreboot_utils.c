#include "coreboot_utils.h"

#include <aos/kernel_cap_invocations.h>

errval_t copy_bootinfo_capabilities(struct bootinfo* bi) {
    errval_t err;

    // Capability location used to store mem addresses
    struct capref mem_cap = {
        .cnode = cnode_memory,
        .slot  = 0,
    };
    // Capability location used to store physical addresses
    struct capref phys_cap = {
        .cnode = {
            .cnode = CPTR_PHYADDRCN_BASE,
            .level = CNODE_TYPE_OTHER,
            .croot = CPTR_ROOTCN
        },
        .slot = 0,
    };
    // Capability location used to store frames
    struct capref frame_cap = {
        .cnode = {
            .cnode = ROOTCN_SLOT_ADDR(ROOTCN_SLOT_SEGCN),
            .level = CNODE_TYPE_OTHER,
            .croot = CPTR_ROOTCN
        },
        .slot = 0,
    };
    // Capability location used to module devFrames
    struct capref module_cap = {
        .cnode = cnode_module,
        .slot = 0
    };

    for(size_t i = 0; i < bi->regions_length; i++){
        errval_t (*forge_function)(struct capref, genpaddr_t, gensize_t, coreid_t) = NULL;
        struct capref* cap = NULL;
        gensize_t map_size = bi->regions[i].mr_bytes;

        switch (bi->regions[i].mr_type)
        {
        case RegionType_Empty:
            forge_function = ram_forge;
            cap = &mem_cap;
            break;

        case RegionType_PhyAddr:
        case RegionType_PlatformData:
            forge_function = physaddr_forge;
            cap = &phys_cap;
            break;

        case RegionType_RootTask:
            forge_function = frame_forge;
            cap = &frame_cap;
            break;
            
        case RegionType_Module:
            // we send these using the new cap transfer mechanism, so break here
            break;
            forge_function = frame_forge;
            module_cap.slot = bi->regions[i].mrmod_slot;
            cap = &module_cap;
            map_size = bi->regions[i].mrmod_size;
            break;

        default:
            break;
        }

        if(forge_function != NULL && cap != NULL){
            map_size = ROUND_PAGE_UP(map_size);
            err = forge_function(*cap, bi->regions[i].mr_base, map_size, disp_get_core_id());
            if(err_is_fail(err))
                return err;
            cap->slot++;
        }
    }

    return SYS_ERR_OK;
}
