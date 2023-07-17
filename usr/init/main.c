/**
 * \file
 * \brief init process for child spawning
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <stdlib.h>

#include <aos/aos.h>
#include <aos/morecore.h>
#include <aos/paging.h>
#include <aos/waitset.h>
#include <aos/aos_rpc.h>
#include <async_channel.h>
#include <mm/mm.h>
#include <spawn/spawn.h>
#include <spawn/multiboot.h>
#include <proc_mgmt.h>
#include <grading/grading.h>
#include <grading/io.h>
#include <argparse/argparse.h>
#include <aos/deferred.h>

#include "barrelfish_kpi/capbits.h"
#include "barrelfish_kpi/distcaps.h"
#include "distops/caplock.h"
#include "errors/errno.h"
#include "mem_alloc.h"
#include "coreboot.h"
#include "tests.h"
#include "rpc_handler.h"
#include "coreboot_utils.h"
#include "distops/invocations.h"
#include "cap_transfer.h"
#include "distops/deletestep.h"
#include "distcap_handler.h"
#include "network_handler.h"

#include "../shell/serial/serial.h"

#include <fs/fat32.h>

struct bootinfo *bi;

coreid_t             my_core_id;
struct platform_info platform_info;

// start the process doing the grading tests
static void launch_grading(void){
    struct mem_region *module = multiboot_find_module(bi, "init");
    if (!module) {
        debug_printf("multiboot_find_module() failed\n");
        return;
    }

    const char *bi_cmdline = multiboot_module_opts(module);
    if (!bi_cmdline) {
        debug_printf("multiboot_module_opts() failed\n");
        return;
    }

    // we only want the args
    const char* arg_pos = bi_cmdline;
    // go after the module name
    while(*arg_pos != 0 && *arg_pos != ' ')
        arg_pos++;

    // replace it with module_proc
    char* cmdline = malloc(strlen("grading_proc") + strlen(arg_pos) + 1);
    strcpy(cmdline, "grading_proc");
    strcat(cmdline, arg_pos);

    debug_printf("cmdline is %s\n", cmdline);

    domainid_t pid;
    errval_t err = proc_mgmt_spawn_with_cmdline(cmdline, disp_get_core_id(), &pid);
    if(err_is_fail(err))
        USER_PANIC_ERR(err, "Failed to start grading");
}

static int bsp_main(int argc, char *argv[]) {
    errval_t err;

    // initialize the grading/testing subsystem
    // DO NOT REMOVE THE FOLLOWING LINE!
    grading_setup_bsp_init(argc, argv);

    // First argument contains the bootinfo location, if it's not set
    bi = (struct bootinfo *) strtol(argv[1], NULL, 10);
    assert(bi);

    // initialize our RAM allocator
    err = initialize_ram_alloc(bi);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "initialize_ram_alloc");
    }

    err = paging_init();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "paging_init");
    }

    grading_test_early();

    // used to switch inter-core rpc channel to asynchronous mode
    struct async_channel async;
    err = proc_mgmt_init();
    // TODO: give to proc_mgmt later
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "proc_mgmt_init");
    }

    // set up self endpoint (required for LMP)
    err = cap_retype(cap_selfep, cap_dispatcher, 0, ObjType_EndPointLMP, 0);
    if (err_is_fail(err)) {
        return err;
    }

    err = serial_server_init(&async, platform_info.platform);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "serial_server_init");
    }

    ////////////////////////
    /// Boot second core ///
    ////////////////////////

    struct capref  remote_core_urpc_frame;
    struct aos_rpc remote_core_rpc;

    err = frame_alloc(&remote_core_urpc_frame, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "unable to allocate frame for remote core URPC");
    }
    err = aos_rpc_ump_connect(&remote_core_rpc, remote_core_urpc_frame, true, get_default_waitset());
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "unable to connect to remote core URPC");
    }


    // allocate 512 mb of ram for the remote core (may want to change this later)
    struct capref remote_core_ram_cap;
    err = ram_alloc(&remote_core_ram_cap, 512 * 1024 * 1024);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "unable to allocate ram for remote core");
    }

    switch (platform_info.platform) {
    case PI_PLATFORM_IMX8X: {
        // SPAWN THE SECOND CORE on the IMX8X baord
        hwid_t mpid = 1;
        err         = coreboot_boot_core(mpid, "boot_armv8_generic", "cpu_imx8x", "init",
                                         remote_core_urpc_frame, NULL);
        break;
    }
    case PI_PLATFORM_QEMU: {
        // SPAWN THE SECOND CORE on QEMU
        hwid_t mpid = 1;
        err         = coreboot_boot_core(mpid, "boot_armv8_generic", "cpu_a57_qemu", "init",
                                         remote_core_urpc_frame, NULL);
        break;
    }
    default:
        debug_printf("Unsupported platform\n");
        return LIB_ERR_NOT_IMPLEMENTED;
    }
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Booting second core failed. Continuing.\n");
    }

    struct capability ram_capa;
    cap_direct_identify(remote_core_ram_cap, &ram_capa);
    struct capability mmstring_capa;
    cap_direct_identify(cap_mmstrings, &mmstring_capa);
    void *multiboot_strings;
    err = paging_map_frame_attr(get_current_paging_state(), (void **)&multiboot_strings,
                                BASE_PAGE_SIZE, cap_mmstrings, VREGION_FLAGS_READ);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "paging_map_frame_attr");
    }

    struct setup_msg_0 setup_msg = {
        .ram = {
            .base = get_address(&ram_capa),
            .length = get_size(&ram_capa),
        },
        .bootinfo_size = sizeof(*bi) + bi->regions_length * sizeof(struct mem_region),
        .mmstring_base = get_address(&mmstring_capa)
    };

    struct cap_transfer* module_caps = malloc(sizeof(struct cap_transfer) * bi->regions_length);

    size_t module_cap_count = L2_CNODE_SLOTS;
    for (size_t i = 0; i < L2_CNODE_SLOTS; ++i) {
        struct capref module_cap = {
            .cnode = cnode_module,
            .slot = i
        };
        struct capability thecap;
        monitor_cap_identify(module_cap, &thecap);
        // debug_printf("cap %zu: type %d\n", i, thecap.type);
        // cap_dump_relations(module_cap);
        if (thecap.type == ObjType_Null) {
            module_cap_count = i;
            break;
        }
    }
    debug_printf("Sending %zu caps\n", module_cap_count);
    for (size_t i = 0; i < module_cap_count; ++i) {
        struct capref module_cap = {
            .cnode = cnode_module,
            .slot = i
        };
        err = cap_transfer_copy(module_cap, module_caps + i);
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "cap_transfer_copy on module cap, slot %zu", i);
        }
    }


    aos_rpc_send_blocking(&remote_core_rpc, &setup_msg, sizeof(setup_msg), NULL_CAP);
    aos_rpc_send_blocking(&remote_core_rpc, bi,
                          sizeof(*bi) + bi->regions_length * sizeof(struct mem_region), NULL_CAP);
    aos_rpc_send_blocking(&remote_core_rpc, multiboot_strings, BASE_PAGE_SIZE, NULL_CAP);
    aos_rpc_send_blocking(&remote_core_rpc, module_caps, sizeof(struct cap_transfer) * module_cap_count, NULL_CAP);

    ////////////////////////////////
    /// Booot second core finish ///
    ////////////////////////////////

    // async channel will call echo_handler on all messages it receives
    async_init(&async, &remote_core_rpc, async_rpc_request_handler);
    set_cross_core_channel(&async);

    // send a test async message
    // const char data[] = "Hello World from bsp core!";
    // debug_printf("sending request to app core\n");
    //async_request(&async, (void*)data, sizeof(data), print_callback, NULL);

    barrelfish_usleep(250000);
    if(platform_info.platform == PI_PLATFORM_IMX8X) {

#define SD_CARD_BOARD
#ifdef SD_CARD_BOARD
        err = mount_filesystem();
        if (err_is_fail(err)) {
            USER_PANIC_ERR(err, "Failed to mount the filesystem driver\n");
        }
#endif

#ifdef FILESYSTEM_ELF
        domainid_t fs_test_pid;

        assert(err_is_ok(proc_mgmt_spawn_program("/SDCARD/HELLOFAT arg1 arg2 arg3", 0, &fs_test_pid)));

        struct waitset *default_ws = get_default_waitset();
        delete_steps_init(default_ws);
        while (true) {
            err = event_dispatch(default_ws);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "in event_dispatch");
                abort();
            }
        }
#endif

#ifdef FILESYSTEM_TEST
        domainid_t fs_test_pid;
        err = proc_mgmt_spawn_program("filereader", /*core*/ 0, &fs_test_pid);

        struct waitset *default_ws = get_default_waitset();
        delete_steps_init(default_ws);
        while (true) {
            err = event_dispatch(default_ws);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "in event_dispatch");
                abort();
            }
        }
#endif


#ifdef FILESYSTEM_BENCHMARK
        benchmark_read(get_mounted_filesystem()->b_driver, 500);
        benchmark_write(get_mounted_filesystem()->b_driver, 500);
#endif
    }

    struct waitset *default_ws = get_default_waitset();
    delete_steps_init(default_ws);
    caplock_init(default_ws);
    err = distcap_init();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "distcap_init");
    }

    domainid_t shell_pid;
    // err = proc_mgmt_spawn_program("tester", 0, &shell_pid);
    // if (err_is_fail(err)) {
    //     DEBUG_ERR(err, "spawning tester failed. Continuing.\n");
    // }
    err = proc_mgmt_spawn_program("shell", /*core*/ 0, &shell_pid);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "spawning shell failed. Continuing.\n");
    }

    launch_grading();

    err = network_handler_init(platform_info.platform);
    if(err_is_fail(err)){
        DEBUG_ERR(err, "Network handler init failed. Continuing.\n");
    }

    debug_printf("Message handler loop\n");
    // Hang around
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}

static int app_main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    errval_t err;

    debug_print_cap_at_capref(cap_urpc);

    err = paging_init();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "paging_init");
    }

    struct aos_rpc bsp_core_rpc;
    err = aos_rpc_ump_connect(&bsp_core_rpc, cap_urpc, false, get_default_waitset());
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "unable to connect to BSP core URPC");
    }

    ////////////////////
    // Bootinfo Setup //
    ////////////////////

    struct setup_msg_0 msg;
    err = aos_rpc_recv_blocking(&bsp_core_rpc, &msg, sizeof(msg), NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "in aos_rpc_recv_blocking");
        abort();
    }
    bi  = (struct bootinfo *)malloc(msg.bootinfo_size);
    err = aos_rpc_recv_blocking(&bsp_core_rpc, bi, msg.bootinfo_size, NULL, NULL);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "in aos_rpc_recv_blocking");
        abort();
    }
    debug_printf("received setup message, bootinfo size = %zu\n", msg.bootinfo_size);

    err = copy_bootinfo_capabilities(bi);
    if(err_is_fail(err))
        USER_PANIC_ERR(err, "unable to copy bootinfo capabilities");


    ///////////////////////////
    // End of Bootinfo Setup //
    //////////////////////////


    err = initialize_ram_alloc_range(bi, msg.ram.base, msg.ram.length);
    if (err_is_fail(err))
        USER_PANIC_ERR(err, "unable to initialize ram allocator");

    // we can only do this once the paging is initialized
    err = frame_create(cap_mmstrings, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err))
        USER_PANIC_ERR(err, "unable frame_create");

    void *mmstring_buf;
    err = paging_map_frame(get_current_paging_state(), &mmstring_buf, BASE_PAGE_SIZE, cap_mmstrings);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "paging_map_frame");
    }

    err = aos_rpc_recv_blocking(&bsp_core_rpc, mmstring_buf, BASE_PAGE_SIZE, NULL, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "aos_rpc_recv_blocking");
    }

    void* buf;
    size_t size;
    err = aos_rpc_recv_blocking_varsize(&bsp_core_rpc, &buf, &size, NULL, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "aos_rpc_recv_blocking_varsize");
    }
    struct cap_transfer* mod_caps = buf;
    size_t num_caps_transferred = size / sizeof(struct cap_transfer);
    for (size_t i = 0; i < num_caps_transferred; ++i) {
        struct capref module_cap = {
            .cnode = cnode_module,
            .slot = i,
        };
        cap_from_transfer(&mod_caps[i], module_cap);
        // cap_dump_relations(module_cap);
    }

    struct async_channel async;
    async_init(&async, &bsp_core_rpc, async_rpc_request_handler);
    set_cross_core_channel(&async);

    grading_test_early();

    err = proc_mgmt_init();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "proc_mgmt_init");
    }

    ///////////////////////////
    /// Finish Booting Core ///
    ///////////////////////////

    // set up self endpoint (required for LMP)
    err = cap_retype(cap_selfep, cap_dispatcher, 0, ObjType_EndPointLMP, 0);
    if (err_is_fail(err)) {
        return err;
    }

    struct waitset *default_ws = get_default_waitset();
    delete_steps_init(default_ws);
    caplock_init(default_ws);
    err = distcap_init();
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "distcap_init");
    }

    err = serial_server_init(&async, platform_info.platform);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "serial_server_init");
    }

    launch_grading();

    debug_printf("Message handler loop\n");
    while (true) {
        err = event_dispatch(default_ws);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "in event_dispatch");
            abort();
        }
    }

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    errval_t err;

    /* obtain the core information from the kernel*/
    err = invoke_kernel_get_core_id(cap_kernel, &my_core_id);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to obtain the core id from the kernel\n");
    }

    /* Set the core id in the disp_priv struct */
    disp_set_core_id(my_core_id);

    /* obtain the platform information */
    err = invoke_kernel_get_platform_info(cap_kernel, &platform_info);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "failed to obtain the platform info from the kernel\n");
    }

    char *platform;
    switch (platform_info.platform) {
    case PI_PLATFORM_QEMU:
        platform = "QEMU";
        break;
    case PI_PLATFORM_IMX8X:
        platform = "IMX8X";
        break;
    default:
        platform = "UNKNOWN";
    }

    // this print statement shoudl remain here
    grading_printf("init domain starting on core %" PRIuCOREID " (%s)", my_core_id, platform);
    fflush(stdout);

    if (my_core_id == 0)
        return bsp_main(argc, argv);
    else
        return app_main(argc, argv);
}
