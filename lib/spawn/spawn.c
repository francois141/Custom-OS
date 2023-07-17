/*
 * Copyright (c) 2016, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <ctype.h>
#include <string.h>

#include <aos/aos.h>
#include <aos/dispatcher_arch.h>
#include <barrelfish_kpi/paging_arm_v8.h>
#include <barrelfish_kpi/domain_params.h>

#include <elf/elf.h>
#include <spawn/spawn.h>
#include <spawn/multiboot.h>
#include <spawn/argv.h>
#include <spawn/elfimg.h>
#include <argparse/argparse.h>

static errval_t _setup_lmp_enpoint(struct spawninfo *si, struct cnoderef taskcn)
{
    errval_t err = SYS_ERR_OK;

    struct capref localep;
    err = aos_rpc_lmp_listen(&si->rpc_server, &localep);

    struct capref child_capref = {
        .cnode = taskcn,
        .slot  = TASKCN_SLOT_INITEP,
    };

    err = cap_copy(child_capref, localep);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_COPY);
    }

    return err;
}

static inline errval_t _setup_cspace(struct capref *cnode1_ref, struct capref *l0_table,
                                     struct cnoderef *rootcn_slot_taskcn, int capc,
                                     struct capref caps[], struct capref stdin_frame,
                                     struct capref stdout_frame)
{
    errval_t err = SYS_ERR_OK;

    err = cnode_create_l1(cnode1_ref, NULL);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    struct cnoderef rootcn_slot_alloc_0;
    struct cnoderef rootcn_slot_alloc_1;
    struct cnoderef rootcn_slot_alloc_2;
    struct cnoderef rootcn_slot_pagecn;
    struct cnoderef rootcn_slot_capv;

    err = cnode_create_foreign_l2(*cnode1_ref, ROOTCN_SLOT_TASKCN, rootcn_slot_taskcn);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    err = cnode_create_foreign_l2(*cnode1_ref, ROOTCN_SLOT_SLOT_ALLOC0, &rootcn_slot_alloc_0);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    err = cnode_create_foreign_l2(*cnode1_ref, ROOTCN_SLOT_SLOT_ALLOC1, &rootcn_slot_alloc_1);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    err = cnode_create_foreign_l2(*cnode1_ref, ROOTCN_SLOT_SLOT_ALLOC2, &rootcn_slot_alloc_2);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    err = cnode_create_foreign_l2(*cnode1_ref, ROOTCN_SLOT_PAGECN, &rootcn_slot_pagecn);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    err = cnode_create_foreign_l2(*cnode1_ref, ROOTCN_SLOT_CAPV, &rootcn_slot_capv);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CNODE_CREATE);
    }

    // Setup l0_table capref
    l0_table->cnode = rootcn_slot_pagecn;
    l0_table->slot  = 0;

    // Map slot to root node
    struct capref root_cs_space = {
        .cnode = *rootcn_slot_taskcn,
        .slot  = TASKCN_SLOT_ROOTCN,
    };
    err = cap_copy(root_cs_space, *cnode1_ref);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_COPY);
    }

    // this is needed by the grading library (which calls invoke_kernel_get_core_id instead of disp_get_core_id...)
    // this shouldn't be copied usually
    struct capref root_cs_kernel = {
        .cnode = *rootcn_slot_taskcn,
        .slot  = TASKCN_SLOT_KERNELCAP,
    };
    err = cap_copy(root_cs_kernel, cap_kernel);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_COPY);
    }

    // Create the base memory capability for the process
    struct capref physical_chunk;
    size_t        frame_size = 1024 * 1024;

    err = ram_alloc(&physical_chunk, frame_size);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_FRAME_ALLOC);
    }

    struct capref earlymem_capref = {
        .cnode = *rootcn_slot_taskcn,
        .slot  = TASKCN_SLOT_EARLYMEM,
    };

    err = cap_copy(earlymem_capref, physical_chunk);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_COPY);
    }

    if (!capref_is_null(stdin_frame)) {
        struct capref stdin_capref = {
                .cnode = *rootcn_slot_taskcn,
                .slot  = TASKCN_SLOT_STDIN_FRAME,
        };

        err = cap_copy(stdin_capref, stdin_frame);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_CAP_COPY);
        }
    }

    if (!capref_is_null(stdout_frame)) {
        struct capref stdout_capref = {
                .cnode = *rootcn_slot_taskcn,
                .slot  = TASKCN_SLOT_STDOUT_FRAME,
        };

        err = cap_copy(stdout_capref, stdout_frame);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_CAP_COPY);
        }
    }

    // Pass caps to user
    for (int i = 0; i < capc; i++) {
        if (capref_is_null(caps[i])) {
            continue;
        }
        struct capref input_cap_user_space_ref = {
            .cnode = rootcn_slot_capv,
            .slot  = i,
        };
        err = cap_copy(input_cap_user_space_ref, caps[i]);
        // XXX support sending NULL_CAP to offset the sent capabilities
        if (err_no(err) != SYS_ERR_SOURCE_CAP_LOOKUP && err_is_fail(err)) {
            return err_push(err, LIB_ERR_CAP_COPY);
        }
    }

    // Pass the device cap to the child (for the drivers)
    // only do this on core 0 right now
    if(disp_get_core_id() == 0){
        struct capref device_cap_child = {
            .cnode = *rootcn_slot_taskcn,
            .slot = TASKCN_SLOT_DEV,
        };

        err = cap_copy(device_cap_child, cap_devices);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_CAP_COPY);
        }
    }

    return SYS_ERR_OK;
}

static inline errval_t _setup_vspace(struct paging_state *child_paging_state, size_t start_vaddr,
                                     struct capref root, struct slot_allocator *ca)
{
    errval_t err = SYS_ERR_OK;

    struct capref l0_table_created;
    slot_alloc(&l0_table_created);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    err = vnode_create(l0_table_created, ObjType_VNode_AARCH64_l0);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VNODE_CREATE);
    }

    err = cap_copy(root, l0_table_created);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_COPY);
    }

    err = paging_init_state_foreign(child_paging_state, start_vaddr, l0_table_created, ca);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_PMAP_INIT);
    }

    return SYS_ERR_OK;
}

static inline errval_t _elf_alloc_function(void *child_page_state_arg, genvaddr_t base, size_t size,
                                           uint32_t flags, void **ret)
{
    errval_t err = SYS_ERR_OK;

    struct paging_state *parent_paging_state = get_current_paging_state();
    struct paging_state *child_paging_state  = (struct paging_state *)child_page_state_arg;

    const genvaddr_t start_addr = ROUND_PAGE_DOWN(base);
    const genvaddr_t end_addr   = ROUND_PAGE_UP(base + size);
    const size_t     total_size = end_addr - start_addr;

    void *returned_address;

    struct capref physical_memory_chunck;
    size_t        nb_returned_bytes;
    err = frame_alloc(&physical_memory_chunck, total_size, &nb_returned_bytes);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_FRAME_ALLOC);
    }

    assert(nb_returned_bytes >= total_size);

    // Map the frame to the parent such that we can write to it
    err = paging_map_frame_attr_offset(parent_paging_state, &returned_address, total_size,
                                       physical_memory_chunck, /*offset to the frame*/ 0,
                                       VREGION_FLAGS_READ | VREGION_FLAGS_WRITE);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VSPACE_LAYOUT_INIT);
    }

    *ret = returned_address + base - start_addr;

    // Map the frame to the child such that we can then access it in the childs process
    err = paging_map_fixed_attr_offset(child_paging_state, start_addr, physical_memory_chunck,
                                       total_size, /*offset to the frame*/ 0, flags);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VSPACE_LAYOUT_INIT);
    }

    return SYS_ERR_OK;
}

static inline errval_t _parse_elf_image(struct elfimg *img, struct paging_state *child_st,
                                        lvaddr_t *entry_point_elf_img,
                                        lvaddr_t *global_offset_table_address)
{
    errval_t err = SYS_ERR_OK;

    genvaddr_t elf_base_address = (genvaddr_t)img->buf;
    genvaddr_t elf_size         = img->size;

    err = elf_load(EM_AARCH64, _elf_alloc_function, (void *)child_st, elf_base_address, elf_size,
                   entry_point_elf_img);
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_ELF_MAP);  // TODO: Maybe find a better error
    }


    struct Elf64_Shdr *parsed_values = elf64_find_section_header_name(elf_base_address, elf_size,
                                                                      ".got");
    if (parsed_values == NULL) {
        return SPAWN_ERR_ELF_MAP;  // TODO: Maybe find a better error
    }

    *global_offset_table_address = parsed_values->sh_addr;

    return SYS_ERR_OK;
}


static inline errval_t _setup_arguments(struct cnoderef *root_slot_taskcn, int argc,
                                        const char *argv[], struct paging_state *child_paging_state,
                                        void **child_vaddr_to_physical_chunk)
{
    errval_t err = SYS_ERR_OK;

    // Step 1) Allocate physical frame
    struct capref physical_chunk;
    size_t        frame_size = BASE_PAGE_SIZE;  // TODO: What should we put here as size?
    size_t        allocated_size;

    err = frame_alloc(&physical_chunk, frame_size, &allocated_size);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_FRAME_ALLOC);
    }

    // Step 2) Map in frame in cspace
    struct capref pos_in_child_cpace = {
        .cnode = *root_slot_taskcn,
        .slot  = TASKCN_SLOT_ARGSPAGE,
    };

    err = cap_copy(pos_in_child_cpace, physical_chunk);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_CAP_COPY);
    }

    // Step 3) Setup the paging structure to write from the parent
    void *parent_vaddr_to_physical_chunck;
    err = paging_map_frame_attr_offset(get_current_paging_state(), &parent_vaddr_to_physical_chunck,
                                       frame_size, physical_chunk, 0, VREGION_FLAGS_READ_WRITE);
    if (err_is_fail(err))
        return err;

    // Step 4) Clear everything
    memset(parent_vaddr_to_physical_chunck, 0, frame_size);

    // Step 5) Write into the frame - everything except the arguments
    struct spawn_domain_params *params
        = (struct spawn_domain_params *)parent_vaddr_to_physical_chunck;

    params->argc           = argc;
    params->envp[0]        = NULL;
    params->vspace_buf     = NULL;
    params->vspace_buf_len = 0;
    params->tls_init_base  = NULL;
    params->tls_init_len   = 0;
    params->tls_total_len  = 0;
    params->pagesize       = 0;

    // Step 6) Write the argument in memory (use the child virtual address)
    err = paging_map_frame_attr_offset(child_paging_state, child_vaddr_to_physical_chunk,
                                       frame_size, physical_chunk, 0, VREGION_FLAGS_READ_WRITE);
    if (err_is_fail(err))
        return err;

    void *start_argv_address = parent_vaddr_to_physical_chunck + sizeof(struct spawn_domain_params);

    for (int arg_idx = 0; arg_idx < argc; arg_idx++) {
        // Copy the content
        size_t size = strlen(argv[arg_idx]) + 1;
        memcpy(start_argv_address, argv[arg_idx], size);
        // Setup the address
        params->argv[arg_idx] = start_argv_address - parent_vaddr_to_physical_chunck
                                + *child_vaddr_to_physical_chunk;
        // Increment the pointer
        start_argv_address += size;
    }

    // set last to null
    params->argv[argc] = NULL;

    return SYS_ERR_OK;
}

/**
 * @brief Sets the initial values of some registers in the dispatcher
 *
 * @param[in] handle    dispatcher handle to the child's dispatcher
 * @param[in] entry     entry point of the new process
 * @param[in] got_base  the base address of the global offset table
 *
 */
__attribute__((__used__)) static void armv8_set_registers(dispatcher_handle_t handle,
                                                          lvaddr_t entry, lvaddr_t got_base)
{
    assert(got_base != 0);
    assert(entry != 0);

    // set the got_base in the shared struct
    struct dispatcher_shared_aarch64 *disp_arm = get_dispatcher_shared_aarch64(handle);
    disp_arm->got_base                         = got_base;

    // set the got_base in the registers for the enabled case
    arch_registers_state_t *enabled_area         = dispatcher_get_enabled_save_area(handle);
    enabled_area->regs[REG_OFFSET(PIC_REGISTER)] = got_base;

    // set the got_base in the registers for the disabled case
    arch_registers_state_t *disabled_area         = dispatcher_get_disabled_save_area(handle);
    disabled_area->regs[REG_OFFSET(PIC_REGISTER)] = got_base;
    disabled_area->named.pc                       = entry;
}


static inline errval_t _setup_dispatcher(struct paging_state *child_paging_state,
                                         struct cnoderef     *rootcn_slot_taskcn,
                                         lvaddr_t             entry_point_elf_img,
                                         lvaddr_t global_offset_table_address, struct capref *dcb,
                                         struct capref *child_frame, void *child_vaddr_to_arguments,
                                         domainid_t pid)
{
    errval_t err = SYS_ERR_OK;
    // create frame of the required size for the dispatcher
    struct capref dispatcher_frame;
    size_t        nb_returned_bytes;
    err = frame_alloc(&dispatcher_frame, DISPATCHER_FRAME_SIZE, &nb_returned_bytes);
    if (err_is_fail(err)) {
        // XXX handle the error correctly
        return err_push(err, SPAWN_ERR_CREATE_DISPATCHER_FRAME);
    }
    assert(nb_returned_bytes >= DISPATCHER_FRAME_SIZE);


    // allocate the slot & create the dispatcher.
    // struct capref dcb;
    slot_alloc(dcb);
    err = dispatcher_create(*dcb);
    if (err_is_fail(err)) {
        // XXX handle the error correctly
        return err_push(err, SPAWN_ERR_CREATE_DISPATCHER);
    }

    struct capref dispatcher_slot = {
        .cnode = *rootcn_slot_taskcn,
        .slot  = TASKCN_SLOT_DISPATCHER,
    };
    err = cap_copy(dispatcher_slot, *dcb);
    if (err_is_fail(err)) {
        // XXX handle the error
        return err;
    }

    // capability for this frame should also be stored in the child’s CSpace in the appropriate slot
    child_frame->cnode = *rootcn_slot_taskcn;
    child_frame->slot  = TASKCN_SLOT_DISPFRAME;
    err                = cap_copy(*child_frame, dispatcher_frame);
    if (err_is_fail(err)) {
        // XXX handle error
        return err;
    }

    // map dispatcher to current paging state and the child
    void *dispatcher_page;
    err = paging_map_frame_attr_offset(get_current_paging_state(), &dispatcher_page,
                                       nb_returned_bytes, dispatcher_frame,
                                       /*offset to the frame*/ 0, VREGION_FLAGS_READ_WRITE);
    if (err_is_fail(err)) {
        // XXX handle the error correctly.
        return err;
    }

    void *dispatcher_page_child;
    err = paging_map_frame_attr_offset(child_paging_state, &dispatcher_page_child,
                                       nb_returned_bytes, dispatcher_frame,
                                       /*offset to the frame*/ 0, VREGION_FLAGS_READ_WRITE);
    if (err_is_fail(err)) {
        // XXX handle the error correctly.
        return err;
    }

    dispatcher_handle_t               handle        = (dispatcher_handle_t)dispatcher_page;
    struct dispatcher_shared_generic *disp          = get_dispatcher_shared_generic(handle);
    struct dispatcher_generic        *disp_gen      = get_dispatcher_generic(handle);
    arch_registers_state_t           *enabled_area  = dispatcher_get_enabled_save_area(handle);
    arch_registers_state_t           *disabled_area = dispatcher_get_disabled_save_area(handle);

    disp_gen->core_id   = disp_get_current_core_id();
    disp_gen->domain_id = pid;

    // virtual address of the dispatcher frame in child’s VSpace
    disp->udisp = (lvaddr_t)dispatcher_page_child;
    // Start in disabled mode
    disp->disabled = 1;
    // name for debugging.
    snprintf(disp->name, sizeof(disp->name) - 1, "proc%d", pid);

    // Set program counter (where it should start to execute)
    registers_set_entry(disabled_area, entry_point_elf_img);

    // Set program parameters
    registers_set_param(enabled_area, (lvaddr_t)child_vaddr_to_arguments);

    // Initialize offset registers
    // got_addr is the address of the .got in the child's VSpace
    armv8_set_registers(handle, entry_point_elf_img, global_offset_table_address);

    // we won’t use error handling frames
    disp_gen->eh_frame          = 0;
    disp_gen->eh_frame_size     = 0;
    disp_gen->eh_frame_hdr      = 0;
    disp_gen->eh_frame_hdr_size = 0;

    // TASKCN_SLOT_SELFEP: Endpoint to itself.
    struct capref ep_slot = { .cnode = *rootcn_slot_taskcn, .slot = TASKCN_SLOT_SELFEP };
    err                   = cap_retype(ep_slot, *dcb, 0, ObjType_EndPointLMP, 0);
    if (err_is_fail(err)) {
        // XXX handle the error
        return err;
    }

    return SYS_ERR_OK;
}


char **spawn_parse_args(const char *opts, int *argc_dest)
{
    errval_t err  = SYS_ERR_OK;
    int      argc = 0;
    char   **argv = NULL;
    err           = cmdline_to_argv(opts, &argc, &argv);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "cmdline_to_argv faied.");
    }
    *argc_dest = argc;
    return argv;
}

errval_t spawn_load_elf(struct bootinfo *bi, const char *name, struct elfimg *img, int *argc,
                        char ***argv)
{
    errval_t err = SYS_ERR_OK;
    // - Get the module from the multiboot image
    struct mem_region *module = multiboot_find_module(bi, name);

    if (module == NULL) {
        // handle error
        return SPAWN_ERR_DOMAIN_NOTFOUND;
    }

    const char *opts = multiboot_module_opts(module);

    if (opts == NULL) {
        // TODO maybe use a more descriptive error
        return SPAWN_ERR_DOMAIN_NOTFOUND;
    }

    // - create the elfimg struct from the module
    elfimg_init_from_module(img, module);
    err = elfimg_map(img);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "elfimg_map failed");
        return err;
    }

    static const char elf_header[] = { 0x7f, 'E', 'L', 'F' };
    if (img->size < sizeof(elf_header) || memcmp(img->buf, elf_header, sizeof(elf_header)) != 0) {
        debug_printf("spawn: %s is not an ELF image\n", name);
        return SPAWN_ERR_DOMAIN_NOTFOUND;
    } else {
#if DEBUG_SPAWN
        debug_printf("spawn: %s is an ELF image\n", name);
#endif
    }

    if (argc == NULL || argv == NULL)
        return SYS_ERR_OK;

    // split opts on spaces, and count the number of arguments
    *argv = spawn_parse_args(opts, argc);

    for (int i = 0; i < *argc; ++i) {
        debug_printf("argv[%d] = %s\n", i, (*argv)[i]);
    }

    return SYS_ERR_OK;
}


/**
 * @brief constructs a new process by loading the image from the bootinfo struct
 *
 * @param[in] si    spawninfo structure to fill in
 * @param[in] bi    pointer to the bootinfo struct
 * @param[in] name  name of the binary in the bootinfo struct
 * @param[in] pid   the process id (PID) for the new process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note, this function prepares a new process for running, but it does not make it
 * runnable. See spawn_start().
 */
errval_t spawn_load_with_bootinfo(struct spawninfo *si, struct bootinfo *bi, const char *name,
                                  domainid_t pid)
{
    struct elfimg img;
    int           argc;
    char        **argv;
    errval_t      err = spawn_load_elf(bi, name, &img, &argc, &argv);
    if (err_is_fail(err))
        return err;

    // - Call spawn_load_with_args
    err = spawn_load_with_args(si, &img, argc, (const char **)argv, pid);

    // free arguments after spawn complete (TODO should spawn take ownership?)
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    free(argv);

    return err;
}


errval_t spawn_load_filesystem(const char *path, struct elfimg *img, int *argc,
                        char ***argv)
{
    errval_t err = SYS_ERR_OK;

    struct fat32_filesystem *fs = get_mounted_filesystem();

    char *file_path = NULL;
    char *split = strchr(path, ' ');
    if(split == NULL) {
        file_path = (char*)path;
    } else {
        file_path = calloc(split - path + 1, 1);
        memcpy(file_path, path, split - path);
    }

    assert(file_path != NULL);

    struct fat32_handle *handle;
    err = fat32_open(fs, file_path, &handle);
    if(err_is_fail(err)) {
        return err;
    }

    size_t elf_size = handle->entry.file_size;
    void *buffer = malloc(ROUND_UP(elf_size,4096));
    if(buffer == NULL) {
        return LIB_ERR_MALLOC_FAIL;
    }

    size_t nb_bytes_read;
    err = fat32_read(fs, handle, buffer, elf_size, &nb_bytes_read);
    if(err_is_fail(err)) {
        return err;
    }

    assert(nb_bytes_read == elf_size);

    struct elfimg image = {
        .mem = NULL_CAP,
        .buf = buffer,
        .size = elf_size,
    };

    *img = image;

    static const char elf_header[] = { 0x7f, 'E', 'L', 'F' };
    if (img->size < sizeof(elf_header) || memcmp(img->buf, elf_header, sizeof(elf_header)) != 0) {
        debug_printf("spawn: %s is not an ELF image\n", path);
        return SPAWN_ERR_DOMAIN_NOTFOUND;
    } else {
#if DEBUG_SPAWN
        debug_printf("spawn: %s is an ELF image\n", name);
#endif
    }

    if (argc == NULL || argv == NULL)
        return SYS_ERR_OK;

    // split opts on spaces, and count the number of arguments
    *argv = spawn_parse_args(path, argc);

    for (int i = 0; i < *argc; ++i) {
        debug_printf("argv[%d] = %s\n", i, (*argv)[i]);
    }

    return SYS_ERR_OK;
}

errval_t spawn_load_mapped(struct spawninfo *si, struct elfimg *img, int argc,
                           const char *argv[], int capc, struct capref caps[], domainid_t pid,
                           struct capref stdin_frame, struct capref stdout_frame)
{
    errval_t err = SYS_ERR_OK;

    si->pid         = pid;
    si->state       = SPAWN_STATE_SPAWNING;
    si->exitcode    = 0;
    si->binary_name = malloc(strlen(argv[0]) + 1);
    strcpy(si->binary_name, argv[0]);

    // construct the command line
    err = argv_to_cmdline(argc, argv, &si->cmdline);
    if (err_is_fail(err)) {
        return err;
    }

    struct capref root_cnode_lvl1_child;
    struct capref root_l0_table_child;

#if DEBUG_SPAWN
    debug_printf("Setup cspace\n");
#endif

    struct cnoderef rootcn_slot_taskcn;
    err = _setup_cspace(&root_cnode_lvl1_child, &root_l0_table_child, &rootcn_slot_taskcn, capc,
                        caps, stdin_frame, stdout_frame);
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_SETUP_CSPACE);
    }
    si->cspace = root_cnode_lvl1_child;

    struct paging_state *child_paging_state = (struct paging_state *)malloc(
            sizeof(struct paging_state));
    if (child_paging_state == NULL) {
        return LIB_ERR_MALLOC_FAIL;  // TODO : Handle error correctly
    }

#if DEBUG_SPAWN
    debug_printf("Setup vspace\n");
#endif
    err = _setup_vspace(child_paging_state, BASE_PAGE_SIZE, root_l0_table_child,
                        get_default_slot_allocator());
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_VSPACE_INIT);  // TODO : Handle error correctly
    }
    si->vspace = root_l0_table_child;

    lvaddr_t entry_point_elf_img         = 0;
    lvaddr_t global_offset_table_address = 0;

#if DEBUG_SPAWN
    debug_printf("Parse elf\n");
#endif
    err = _parse_elf_image(img, child_paging_state, &entry_point_elf_img,
                           &global_offset_table_address);
    if (err_is_fail(err)) {
        return err_push(err,
                        SPAWN_ERR_ELF_MAP);  // TODO : Find maybe a better error & handle correctly
    }

    struct capref dispatcher;
    struct capref dispframe;

#if DEBUG_SPAWN
    debug_printf("Setup arguments\n");
#endif
    void **child_vaddr_to_arguments = (void **)malloc(sizeof(void **));

    err = _setup_arguments(&rootcn_slot_taskcn, argc, argv, child_paging_state,
                           child_vaddr_to_arguments);  // TODO : Fill this function
    if (err_is_fail(err)) {
        return err;
    }

    err = _setup_lmp_enpoint(si, rootcn_slot_taskcn);
    if (err_is_fail(err)) {
        return err;
    }

#if DEBUG_SPAWN
    debug_printf("Setup dispatcher\n");
    printf("0x%x 0x%x\n", entry_point_elf_img, global_offset_table_address);
#endif
    err = _setup_dispatcher(child_paging_state, &rootcn_slot_taskcn, entry_point_elf_img,
                            global_offset_table_address, &dispatcher, &dispframe,
                            *child_vaddr_to_arguments, pid);
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_SETUP_DISPATCHER);
    }
    si->dispatcher = dispatcher;

    si->state = SPAWN_STATE_READY;

    err = invoke_dispatcher(dispatcher, cap_dispatcher, root_cnode_lvl1_child, root_l0_table_child,
                            dispframe, /*run*/ false);
    if (err_is_fail(err)) {
        return err_push(err, SPAWN_ERR_SETUP_DISPATCHER);
    }

    return SYS_ERR_OK;
}

/**
 * @brief constructs a new process from the provided image pointer
 *
 * @param[in] si    spawninfo structure to fill in
 * @param[in] img   pointer to the elf image in memory
 * @param[in] argc  number of arguments in argv
 * @param[in] argv  command line arguments
 * @param[in] capc  number of capabilities in the caps array
 * @param[in] caps  array of capabilities to pass to the child
 * @param[in] pid   the process id (PID) for the new process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note, this function prepares a new process for running, but it does not make it
 * runnable. See spawn_start().
 */
errval_t spawn_load_with_caps(struct spawninfo *si, struct elfimg *img, int argc,
                              const char *argv[], int capc, struct capref caps[], domainid_t pid)
{
    return spawn_load_mapped(si, img, argc, argv, capc, caps, pid, NULL_CAP, NULL_CAP);
}


/**
 * @brief starts the execution of the new process by making it runnable
 *
 * @param[in] si   spawninfo structure of the constructed process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_start(struct spawninfo *si)
{
    if (si == NULL)
        return ERR_INVALID_ARGS;

    errval_t err;

    //  - check whether the process is in the right state (ready to be started)
    //  - invoke the dispatcher to make the process runnable
    //  - set the state to running
    if (si->state != SPAWN_STATE_READY)
        return PROC_MGMT_ERR_INVALID_SPAWND;

    // We can call resume even for the first time
    err = invoke_dispatcher_resume(si->dispatcher);
    if (err_is_fail(err))
        return err;

    si->state = SPAWN_STATE_RUNNING;
    return SYS_ERR_OK;
}

/**
 * @brief resumes the execution of a previously stopped process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_resume(struct spawninfo *si)
{
    //  - check whether the process is in the right state
    //  - resume the execution of the process
    //  - set the state to running
    if (si == NULL)
        return ERR_INVALID_ARGS;

    errval_t err;

    if (si->state != SPAWN_STATE_SUSPENDED)
        return PROC_MGMT_ERR_INVALID_SPAWND;

    err = invoke_dispatcher_resume(si->dispatcher);
    if (err_is_fail(err))
        return err;

    si->state = SPAWN_STATE_RUNNING;
    return SYS_ERR_OK;
}

/**
 * @brief stops/suspends the execution of a running process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_suspend(struct spawninfo *si)
{
    //  - check whether the process is in the right state
    //  - stop the execution of the process
    //  - set the state to suspended
    if (si == NULL)
        return ERR_INVALID_ARGS;

    errval_t err;

    if (si->state != SPAWN_STATE_RUNNING)
        return PROC_MGMT_ERR_INVALID_SPAWND;

    err = invoke_dispatcher_stop(si->dispatcher);
    if (err_is_fail(err))
        return err;

    si->state = SPAWN_STATE_SUSPENDED;
    return SYS_ERR_OK;
}

/**
 * @brief stops the execution of a running process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_kill(struct spawninfo *si)
{
    //  - check whether the process is in the right state
    //  - stop the execution of the process
    //  - set the state to killed

    if (si == NULL)
        return ERR_INVALID_ARGS;

    errval_t err;

    if (si->state != SPAWN_STATE_SUSPENDED && si->state != SPAWN_STATE_RUNNING)
        return PROC_MGMT_ERR_INVALID_SPAWND;

    if (si->state == SPAWN_STATE_RUNNING) {
        err = invoke_dispatcher_stop(si->dispatcher);
        if (err_is_fail(err))
            return err;
    }

    si->state = SPAWN_STATE_KILLED;
    return SYS_ERR_OK;
}

/**
 * @brief marks the process as having exited
 *
 * @param[in] si        spawninfo structure of the process
 * @param[in] exitcode  exit code of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * The process manager should call this function when it receives the exit
 * notification from the child process. The function makes sure that the
 * process is no longer running and can be cleaned up later on.
 */
errval_t spawn_exit(struct spawninfo *si, int exitcode)
{
    //  - check whether the process is in the right state
    //  - stop the execution of the process, update the exit code
    //  - set the state to terminated

    if (si == NULL)
        return ERR_INVALID_ARGS;

    errval_t err;

    // exit can only be called from the thread itself
    if (si->state != SPAWN_STATE_RUNNING)
        return PROC_MGMT_ERR_INVALID_SPAWND;

    err = invoke_dispatcher_stop(si->dispatcher);
    if (err_is_fail(err))
        return err;

    si->state    = SPAWN_STATE_TERMINATED;
    si->exitcode = exitcode;

    return SYS_ERR_OK;
}

/**
 * @brief cleans up the resources of a process
 *
 * @param[in] si   spawninfo structure of the process
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: The process has to be stopped before calling this function.
 */
errval_t spawn_cleanup(struct spawninfo *si)
{
    // make compiler happy about unused parameters
    errval_t err;

    free(si->binary_name);
    free(si->cmdline);

    err = cap_destroy(si->dispatcher);
    if (err_is_fail(err))
        return err;

    err = cap_destroy(si->vspace);
    if (err_is_fail(err))
        return err;

    err = cap_destroy(si->cspace);
    if (err_is_fail(err))
        return err;

    aos_rpc_destroy_server(&si->rpc_server);

    // Resources need to be cleaned up at some point. How would you go about this?
    // This is certainly not an easy task. You need to track down all the resources
    // that the process was using and collect them. Recall, in Barrelfish all the
    // resources are represented by capabilities -- so you could, in theory, simply
    // walk the CSpace of the process. Then, some of the resources you may have kept
    // in the process manager's CSpace and created mappings in the VSpace.
    return SYS_ERR_OK;
}

/**
 * @brief initializes the IPC channel for the process
 *
 * @param[in] si       spawninfo structure of the process
 * @param[in] ws       waitset to be used
 * @param[in] handler  message handler for the IPC channel
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 *
 * Note: this functionality is required for the IPC milestone.
 *
 * Hint: the IPC subsystem should be initialized before the process is being run for
 * the first time.
 */
errval_t spawn_setup_ipc(struct spawninfo *si, struct waitset *ws, struct handler_closure handler)
{
    return aos_rpc_lmp_accept(&si->rpc_server, handler, ws);
}


/**
 * @brief sets the receive handler function for the message channel
 *
 * @param[in] si       spawninfo structure of the process
 * @param[in] handler  handler function to be set
 *
 * @return SYS_ERR_OK on success, SPAWN_ERR_* on failure
 */
errval_t spawn_set_recv_handler(struct spawninfo *si, aos_recv_handler_fn handler)
{
    // make compiler happy about unused parameters
    (void)si;
    (void)handler;

    // TODO:
    //  - set the custom receive handler for the message channel
    USER_PANIC("Not implemented");
    return LIB_ERR_NOT_IMPLEMENTED;
}
