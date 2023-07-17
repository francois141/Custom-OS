#include <string.h>
#include <aos/aos.h>
#include <aos/deferred.h>
#include <spawn/multiboot.h>
#include <elf/elf.h>
#include <barrelfish_kpi/arm_core_data.h>
#include <aos/kernel_cap_invocations.h>
#include <aos/cache.h>

#include "coreboot.h"


#define ARMv8_KERNEL_OFFSET 0xffff000000000000

#define CORE_DATA_FRAME_SIZE BASE_PAGE_SIZE
#define STACK_FRAME_SIZE 16*BASE_PAGE_SIZE


extern struct platform_info platform_info;
extern struct bootinfo     *bi;

struct mem_info {
    size_t   size;       // Size in bytes of the memory region
    void    *buf;        // Address where the region is currently mapped
    lpaddr_t phys_base;  // Physical base address
};

/**
 * Load a ELF image into memory.
 *
 * binary:            Valid pointer to ELF image in current address space
 * mem:               Where the ELF will be loaded
 * entry_point:       Virtual address of the entry point
 * reloc_entry_point: Return the loaded, physical address of the entry_point
 */
__attribute__((__used__)) static errval_t load_elf_binary(genvaddr_t             binary,
                                                          const struct mem_info *mem,
                                                          genvaddr_t             entry_point,
                                                          genvaddr_t            *reloc_entry_point)

{
    struct Elf64_Ehdr *ehdr = (struct Elf64_Ehdr *)binary;

    /* Load the CPU driver from its ELF image. */
    bool found_entry_point = 0;
    bool loaded            = 0;

    struct Elf64_Phdr *phdr = (struct Elf64_Phdr *)(binary + ehdr->e_phoff);
    for (size_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) {
            DEBUG_PRINTF("Segment %d load address 0x% " PRIx64 ", file size %" PRIu64
                         ", memory size 0x%" PRIx64 " SKIP\n",
                         i, phdr[i].p_vaddr, phdr[i].p_filesz, phdr[i].p_memsz);
            continue;
        }

        DEBUG_PRINTF("Segment %d load address 0x% " PRIx64 ", file size %" PRIu64 ", memory size "
                     "0x%" PRIx64 " LO"
                     "AD"
                     "\n",
                     i, phdr[i].p_vaddr, phdr[i].p_filesz, phdr[i].p_memsz);


        if (loaded) {
            USER_PANIC("Expected one load able segment!\n");
        }
        loaded = 1;

        void    *dest      = mem->buf;
        lpaddr_t dest_phys = mem->phys_base;

        assert(phdr[i].p_offset + phdr[i].p_memsz <= mem->size);

        /* copy loadable part */
        memcpy(dest, (void *)(binary + phdr[i].p_offset), phdr[i].p_filesz);

        /* zero out BSS section */
        memset(dest + phdr[i].p_filesz, 0, phdr[i].p_memsz - phdr[i].p_filesz);

        if (!found_entry_point) {
            if (entry_point >= phdr[i].p_vaddr && entry_point - phdr[i].p_vaddr < phdr[i].p_memsz) {
                *reloc_entry_point = (dest_phys + (entry_point - phdr[i].p_vaddr));
                found_entry_point  = 1;
            }
        }
    }

    if (!found_entry_point) {
        USER_PANIC("No entry point loaded\n");
    }

    return SYS_ERR_OK;
}

/**
 * Relocate an already loaded ELF image.
 *
 * binary:            Valid pointer to ELF image in current address space
 * mem:               Where the ELF is loaded
 * kernel_:       Virtual address of the entry point
 * reloc_entry_point: Return the loaded, physical address of the entry_point
 */
__attribute__((__used__)) static errval_t relocate_elf(genvaddr_t binary, struct mem_info *mem,
                                                       lvaddr_t load_offset)
{
    DEBUG_PRINTF("Relocating image.\n");

    struct Elf64_Ehdr *ehdr = (struct Elf64_Ehdr *)binary;

    size_t             shnum = ehdr->e_shnum;
    struct Elf64_Phdr *phdr  = (struct Elf64_Phdr *)(binary + ehdr->e_phoff);
    struct Elf64_Shdr *shead = (struct Elf64_Shdr *)(binary + (uintptr_t)ehdr->e_shoff);

    /* Search for relocaton sections. */
    for (size_t i = 0; i < shnum; i++) {
        struct Elf64_Shdr *shdr = &shead[i];
        if (shdr->sh_type == SHT_REL || shdr->sh_type == SHT_RELA) {
            if (shdr->sh_info != 0) {
                DEBUG_PRINTF("I expected global relocations, but got"
                             " section-specific ones.\n");
                return ELF_ERR_HEADER;
            }


            uint64_t segment_elf_base  = phdr[0].p_vaddr;
            uint64_t segment_load_base = mem->phys_base;
            uint64_t segment_delta     = segment_load_base - segment_elf_base;
            uint64_t segment_vdelta    = (uintptr_t)mem->buf - segment_elf_base;

            size_t rsize;
            if (shdr->sh_type == SHT_REL) {
                rsize = sizeof(struct Elf64_Rel);
            } else {
                rsize = sizeof(struct Elf64_Rela);
            }

            assert(rsize == shdr->sh_entsize);
            size_t nrel = shdr->sh_size / rsize;

            void *reldata = (void *)(binary + shdr->sh_offset);

            /* Iterate through the relocations. */
            for (size_t ii = 0; ii < nrel; ii++) {
                void *reladdr = reldata + ii * rsize;

                switch (shdr->sh_type) {
                case SHT_REL:
                    DEBUG_PRINTF("SHT_REL unimplemented.\n");
                    return ELF_ERR_PROGHDR;
                case SHT_RELA: {
                    struct Elf64_Rela *rel = reladdr;

                    uint64_t offset = rel->r_offset;
                    uint64_t sym    = ELF64_R_SYM(rel->r_info);
                    uint64_t type   = ELF64_R_TYPE(rel->r_info);
                    uint64_t addend = rel->r_addend;

                    uint64_t *rel_target = (void *)offset + segment_vdelta;

                    switch (type) {
                    case R_AARCH64_RELATIVE:
                        if (sym != 0) {
                            DEBUG_PRINTF("Relocation references a"
                                         " dynamic symbol, which is"
                                         " unsupported.\n");
                            return ELF_ERR_PROGHDR;
                        }

                        /* Delta(S) + A */
                        *rel_target = addend + segment_delta + load_offset;
                        break;

                    default:
                        DEBUG_PRINTF("Unsupported relocation type %d\n", type);
                        return ELF_ERR_PROGHDR;
                    }
                } break;
                default:
                    DEBUG_PRINTF("Unexpected type\n");
                    break;
                }
            }
        }
    }

    return SYS_ERR_OK;
}


static inline errval_t _create_KCB(struct capref *KCB_block) {
    errval_t err = SYS_ERR_OK;

    const size_t KCB_ALIGNMENT = 4*BASE_PAGE_SIZE;

    struct capref ram_kcb;
    err = ram_alloc_aligned(&ram_kcb, OBJSIZE_KCB,KCB_ALIGNMENT);
    if(err_is_fail(err)) {
        return err; //TODO: Handle error properly
    }

    err = slot_alloc(KCB_block);
    if(err_is_fail(err)) {
        return err; //TODO: Handle error properly
    }

    err = cap_retype(*KCB_block, ram_kcb, 0, ObjType_KernelControlBlock, OBJSIZE_KCB);
    if(err_is_fail(err)) {
        return err; //TODO: Handle error properly
    }

    return SYS_ERR_OK;
}

static inline errval_t _load_module_into_memory(const char* path_module, const char *path_entry_point, lvaddr_t offset, genvaddr_t *phys_entry_point, struct mem_region **module) {
    errval_t err = SYS_ERR_OK;

    // Step 1) Load the module
    *module = multiboot_find_module(bi, path_module);
    if (module == NULL) {
        // handle error
        return SPAWN_ERR_DOMAIN_NOTFOUND;
    }

    struct capref module_frame = {
        .cnode = cnode_module,
        .slot = (*module)->mrmod_slot,
    };

    // Step 2) Map binary into vspace
    void *v_addr_module = NULL;
    err = paging_map_frame_attr_offset(get_current_paging_state(), &v_addr_module, (*module)->mrmod_size, module_frame, 0, VREGION_FLAGS_READ_WRITE);
    if(err_is_fail(err)) {
        return err;
    }

    // Step 3) Create & map a ram object to put the module
    struct capref physical_chunck_boot_module;
    size_t physical_chunck_size = 0;

    err = frame_alloc(&physical_chunck_boot_module, (*module)->mrmod_size, &physical_chunck_size);
    if(err_is_fail(err)) {
        return err;
    }

    assert(physical_chunck_size >= (*module)->mrmod_size);

    struct capability boot_module_frame;
    err = cap_direct_identify(physical_chunck_boot_module, &boot_module_frame);
    if(err_is_fail(err)) {
        return err;
    }

    void *v_addr_physical_chunck;
    err = paging_map_frame_attr_offset(get_current_paging_state(), &v_addr_physical_chunck, get_size(&boot_module_frame), physical_chunck_boot_module, 0, VREGION_FLAGS_READ_WRITE);
    if(err_is_fail(err)) {
        return err;
    }

    struct mem_info boot_info_physical_chunck;
    boot_info_physical_chunck.phys_base = get_address(&boot_module_frame);
    boot_info_physical_chunck.size = get_size(&boot_module_frame);
    boot_info_physical_chunck.buf = v_addr_physical_chunck;

    // Step 4) Get the entry point of the module
    uintptr_t symb_index = 0;
    struct Elf64_Sym *entry_point = elf64_find_symbol_by_name((genvaddr_t)v_addr_module,(*module)->mrmod_size, path_entry_point, 0, STT_FUNC, &symb_index);
    if(entry_point == NULL) {
        return SPAWN_ERR_ELF_MAP;
    }

    // Step 5) Map the module
    err = load_elf_binary((genvaddr_t) v_addr_module, &boot_info_physical_chunck, (genvaddr_t)entry_point->st_value, phys_entry_point);
    if(err_is_fail(err)) {
        return err;
    }

    // Step 6) Relocate the module
    *phys_entry_point += offset;

    err = relocate_elf((genvaddr_t) v_addr_module, &boot_info_physical_chunck ,offset);
    if(err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

static errval_t _allocate_and_map_frame(void **vaddr, struct capref *cap, size_t *size, size_t alloc_size) {
    errval_t err = SYS_ERR_OK;

    err = frame_alloc(cap, alloc_size, size);
    if(err_is_fail(err)) {
        return err;
    }

    assert(*size >= alloc_size);

    err = paging_map_frame_attr_offset(get_current_paging_state(), vaddr, alloc_size, *cap, 0, VREGION_FLAGS_READ_WRITE);
    if(err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

static errval_t _allocate_core_data(void **core_data, struct capref *core_data_frame, size_t *core_data_size) {
   return _allocate_and_map_frame(core_data, core_data_frame, core_data_size, CORE_DATA_FRAME_SIZE);
}

static errval_t _allocate_stack(void **stack_vaddr, struct capref *stack_frame, size_t *stack_size) {
    return _allocate_and_map_frame(stack_vaddr, stack_frame, stack_size, STACK_FRAME_SIZE);
}

static errval_t _load_monitor_process(const char *monitor_path, void **monitor_v_addr, struct armv8_coredata_memreg *monitor_memory_register) {
    errval_t err = SYS_ERR_OK;

    struct mem_region *monitor_module = multiboot_find_module(bi, monitor_path);
    if(monitor_module == NULL) {
        return SPAWN_ERR_FIND_MODULE;
    }

    struct capref monitor_frame = {
            .cnode = cnode_module,
            .slot = monitor_module->mrmod_slot,
    };

    struct capability monitor_frame_cap;
    err = cap_direct_identify(monitor_frame, &monitor_frame_cap);
    if(err_is_fail(err)) {
        return err;
    }

    monitor_memory_register->base = get_address(&monitor_frame_cap);
    monitor_memory_register->length =  get_size(&monitor_frame_cap);

    err = paging_map_frame_attr_offset(get_current_paging_state(), monitor_v_addr, monitor_frame_cap.u.ram.bytes, monitor_frame, 0, VREGION_FLAGS_READ_WRITE);
    if(err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

static inline errval_t _cache_flush(void *core_data, size_t core_data_size) {
    // Clear the instruction cache
    arm64_dcache_wb_range((vm_offset_t) core_data, core_data_size);
    arm64_idcache_wbinv_range((vm_offset_t) core_data, core_data_size);
    return SYS_ERR_OK;
}

static inline errval_t _fill_core_data_structure(struct armv8_core_data *core_data,hwid_t mpid, struct capref stack_frame, genvaddr_t cpu_entry_point, genvaddr_t monitor_vaddr, struct capref KCB_block, struct armv8_coredata_memreg monitor_memory_register, struct mem_region *cpu_module, struct capref urpc_frame) {
    errval_t err = SYS_ERR_OK;
    // Set Boot magic field
    core_data->boot_magic = ARMV8_BOOTMAGIC_PSCI;

    // Give the physical address & the limit of the frame
    struct capability stack_frame_cap;
    err = cap_direct_identify(stack_frame, &stack_frame_cap);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    core_data->cpu_driver_stack = get_address(&stack_frame_cap) + get_size(&stack_frame_cap);
    core_data->cpu_driver_stack_limit = get_address(&stack_frame_cap);

    // Setup virtual address of CPU Driver point
    core_data->cpu_driver_entry = cpu_entry_point;

    // Setup command line
    memset(core_data->cpu_driver_cmdline, 0, sizeof(core_data->cpu_driver_cmdline));
    const char *start_arguments = multiboot_module_opts(cpu_module);
    if(start_arguments != NULL) {
        strlcpy(core_data->cpu_driver_cmdline, start_arguments,128);
    }

    // Fill memory structure
    struct capref cpu_memory_frame;
    size_t size_to_alloc = ARMV8_CORE_DATA_PAGES * BASE_PAGE_SIZE + elf_virtual_size((lvaddr_t) monitor_vaddr);
    size_t allocated_size;

    err = frame_alloc(&cpu_memory_frame, size_to_alloc, &allocated_size);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    struct capability cpu_memory_frame_cap;
    err = cap_direct_identify(cpu_memory_frame, &cpu_memory_frame_cap);
    if(err_is_fail(err)) {
        return err;
    }
    core_data->memory.base = get_address(&cpu_memory_frame_cap);
    core_data->memory.length = get_size(&cpu_memory_frame_cap);

    assert(allocated_size >= size_to_alloc);

    // struct capref urpc_memory_frame;
    // size_to_alloc = PAGE_SIZE;

    // err = frame_alloc(&urpc_memory_frame, size_to_alloc, &allocated_size);
    // if(err_is_fail(err)) {
    //     return err; // TODO: Clean up ds after the error
    // }

    struct capability urpc_memory_frame_cap;
    err = cap_direct_identify(urpc_frame, &urpc_memory_frame_cap);
    if(err_is_fail(err)) {
        return err;
    }
    core_data->urpc_frame.base = get_address(&urpc_memory_frame_cap);
    core_data->urpc_frame.length = get_size(&urpc_memory_frame_cap);

    // Fill monitor binary
    core_data->monitor_binary = monitor_memory_register;

    // KCB Physical address
    struct frame_identity kcb_frame_identity;
    err = invoke_kcb_identify(KCB_block, &kcb_frame_identity);
    if (err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }
    core_data->kcb = kcb_frame_identity.base;

    // Assign logical & physical cores
    core_data->src_core_id = disp_get_core_id();
    core_data->dst_core_id =  mpid;
    core_data->src_arch_id = disp_get_core_id();
    core_data->dst_arch_id = mpid;

    return SYS_ERR_OK;
}


/**
 * @brief boots a new core with the provided mpid
 *
 * @param[in]  mpid         The ARM MPID of the core to be booted
 * @param[in]  boot_driver  Path of the boot driver binary
 * @param[in]  cpu_driver   Path of the CPU driver binary
 * @param[in]  init         Path to the init binary
 * @param[out] core         Returns the coreid of the booted core
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_boot_core(hwid_t mpid, const char *boot_driver, const char *cpu_driver,
                            const char *init, struct capref urpc_frame, coreid_t *core)
{
    errval_t err = SYS_ERR_OK;
    (void) core; // TODO: Fill this variable

    struct capref KCB_block;
    err = _create_KCB(&KCB_block);
    if(err_is_fail(err)) {
        return err; // TODO: Push with custom error (like KCB SETUP FAIL)
    }

    genvaddr_t boot_driver_entry_point;
    genvaddr_t cpu_entry_point;

    struct mem_region *bootloader_module = NULL;
    struct mem_region *cpu_module = NULL;

    const size_t BOOTLOADER_OFFSET = 0;
    err = _load_module_into_memory(boot_driver, "boot_entry_psci", BOOTLOADER_OFFSET, &boot_driver_entry_point, &bootloader_module);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    err = _load_module_into_memory(cpu_driver, "arch_init", ARMv8_KERNEL_OFFSET, &cpu_entry_point, &cpu_module);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    struct armv8_core_data *core_data = NULL;
    struct capref core_data_frame;
    size_t core_data_size = 0;

    err = _allocate_core_data((void**)&core_data, &core_data_frame, &core_data_size);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    void *stack_vaddr = NULL;
    struct capref stack_frame;
    size_t stack_size = 0;

    err = _allocate_stack((void**)&stack_vaddr, &stack_frame, &stack_size);
    if(err_is_fail(err))  {
        return err; // TODO: Clean up ds after the error
    }

    void *monitor_vaddr;
    struct armv8_coredata_memreg monitor_memory_register;

    err = _load_monitor_process(init, &monitor_vaddr,&monitor_memory_register);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    // Fill core data structure
    err = _fill_core_data_structure(core_data,mpid, stack_frame, cpu_entry_point, (genvaddr_t) monitor_vaddr, KCB_block, monitor_memory_register, cpu_module, urpc_frame);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    // Flush the cache
    err = _cache_flush((void*) core_data, core_data_size);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    // Call invoke_monitor_spawn_core
    struct capability core_data_frame_cap;
    err = cap_direct_identify(core_data_frame, &core_data_frame_cap);
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    err = invoke_monitor_spawn_core(mpid, CPU_ARM8,boot_driver_entry_point,get_address(&core_data_frame_cap), 0); // TODO: Is it the correct way to use the last parameter?
    if(err_is_fail(err)) {
        return err; // TODO: Clean up ds after the error
    }

    return SYS_ERR_OK;
}

/**
 * @brief shutdown the execution of the given core and free its resources
 *
 * @param[in] core  Coreid of the core to be shut down
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_shutdown_core(coreid_t core)
{
    (void)core;
    // Hints:
    //  - think of what happens when you call this function with the coreid of another core,
    //    or with the coreid of the core you are running on.
    //  - use the BSP core as the manager.
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief shuts down the core and reboots it using the provided arguments
 *
 * @param[in] core         Coreid of the core to be rebooted
 * @param[in] boot_driver  Path of the boot driver binary
 * @param[in] cpu_driver   Path of the CPU driver binary
 * @param[in] init         Path to the init binary
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_reboot_core(coreid_t core, const char *boot_driver, const char *cpu_driver,
                              const char *init)
{
(void)core;
(void)boot_driver;
(void)cpu_driver;
(void)init;
    // Hints:
    //  - think of what happens when you call this function with the coreid of another core,
    //    or with the coreid of the core you are running on.
    //  - use the BSP core as the manager.
    //  - after you've shutdown the core, you can reuse `coreboot_boot_core` to boot it again.

    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief suspends (halts) the execution of the given core
 *
 * @param[in] core  Coreid of the core to be suspended
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: calling this function with the coreid of the BSP core (0) will cause an error.
 */
errval_t coreboot_suspend_core(coreid_t core)
{
    (void)core;
    // Hints:
    //  - think of what happens when you call this function with the coreid of another core,
    //    or with the coreid of the core you are running on.
    //  - use the BSP core as the manager.

    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief resumes the execution of the given core
 *
 * @param[in] core  Coreid of the core to be resumed
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_resume_core(coreid_t core)
{
    (void)core;
    // Hints:
    //  - check if the coreid is valid and the core is in fact suspended
    //  - wake up the core to resume its execution

    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}



/**
 * @brief obtains the number of cores present in the system.
 *
 * @param[out] num_cores  returns the number of cores in the system
 *
 * @return SYS_ERR_OK on success, errval on failure
 *
 * Note: This function should return the number of cores that the system supports
 */
errval_t coreboot_get_num_cores(coreid_t *num_cores)
{
    // TODO: change me with multicore support!
    *num_cores = 1;
    return SYS_ERR_OK;
}


/**
 * @brief obtains the status of a core in the system.
 *
 * @param[in]  core    the ID of the core to obtain the status from
 * @param[out] status  status struct filled in
 *
 * @return SYS_ERR_OK on success, errval on failure
 */
errval_t coreboot_get_core_status(coreid_t core, struct corestatus *status)
{
    (void)core;
    (void)status;
    // TODO: obtain the status of the core.
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}