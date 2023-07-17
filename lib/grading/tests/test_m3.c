/*
 * Copyright (c) 2019, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

///////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                               //
//                          !! WARNING !!   !! WARNING !!   !! WARNING !!                        //
//                                                                                               //
//      This file is part of the grading library and will be overwritten before grading.         //
//                         You may edit this file for your own tests.                            //
//                                                                                               //
///////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>

#include <aos/aos.h>
#include <aos/solution.h>
#include <aos/capabilities.h>
#include <aos/deferred.h>
#include <aos/ram_alloc.h>
#include <aos/aos_rpc.h>
#include <grading/grading.h>
#include <proc_mgmt/proc_mgmt.h>
#include <spawn/argv.h>


#include <grading/io.h>
#include <grading/state.h>
#include <grading/options.h>
#include <grading/tests.h>
#include "../include/grading/options_internal.h"

#define BINARY_NAME "hello"
#define CMDLINE     "hello arg1 arg2 arg3"


static void spawn_one_without_args(void)
{
    errval_t err;

    // the core we want to spawn on, our own.
    coreid_t core = disp_get_core_id();

    grading_printf("spawn_one_without_args(%s)\n", BINARY_NAME);

    domainid_t pid;
    err = proc_mgmt_spawn_with_cmdline(BINARY_NAME, core, &pid);
    if (err_is_fail(err)) {
        grading_test_fail("P1-1", "failed to load: %s\n", err_getstring(err));
        return;
    }
    // Heads up! When you have messaging support, then you may need to handle a
    // few messages here for the process to start up
    grading_printf("waiting 2 seconds to give the other domain chance to run...\n");
    barrelfish_usleep(2000000);
}

static void spawn_one_with_default_args(void)
{
    errval_t err;

    // the core we want to spawn on, our own.
    coreid_t core = disp_get_core_id();

    grading_printf("spawn_one_with_default_args(%s)\n", BINARY_NAME);

    domainid_t pid;
    err = proc_mgmt_spawn_program(BINARY_NAME, core, &pid);
    if (err_is_fail(err)) {
        grading_test_fail("P1-2", "failed to load: %s\n", err_getstring(err));
        return;
    }
    // Heads up! When you have messaging support, then you may need to handle a
    // few messages here for the process to start up

    grading_printf("waiting 2 seconds to give the other domain chance to run...\n");
    barrelfish_usleep(2000000);
}

static void spawn_one_with_args(void)
{
    errval_t err;

    // the core we want to spawn on, our own.
    coreid_t core = disp_get_core_id();

    grading_printf("spawn_one_with_args(%s)\n", CMDLINE);

    domainid_t pid;
    err = proc_mgmt_spawn_with_cmdline(CMDLINE, core, &pid);
    if (err_is_fail(err)) {
        grading_test_fail("P1-3", "failed to load: %s\n", err_getstring(err));
        return;
    }
    // Heads up! When you have messaging support, then you may need to handle a
    // few messages here for the process to start up
    grading_printf("waiting 2 seconds to give the other domain chance to run...\n");
    barrelfish_usleep(2000000);
}

static void spawn_list(void)
{
    errval_t    err;
    size_t      num;
    domainid_t *pids;

    grading_printf("spawn_list()\n");

    err = proc_mgmt_get_proc_list(&pids, &num);
    if (err_is_fail(err)) {
        grading_test_fail("P1-4", "failed to get the list list: %s\n", err_getstring(err));
        return;
    }

    if (num < 3) {
        grading_test_fail("P1-4", "expected at least 2 processes, got %zu\n", num);
        return;
    }

    if (pids == NULL) {
        grading_test_fail("P1-4", "pids array was NULL\n");
        return;
    }

    for (size_t i = 0; i < num; i++) {
        struct proc_status st;
        err = proc_mgmt_get_status(pids[i], &st);
        if (err_is_fail(err)) {
            grading_test_fail("P1-3", "failed to get status for pid %" PRIuDOMAINID ": %s\n",
                              pids[i], err_getstring(err));
            return;
        }

        grading_printf("%4d  %-16s %2d\n", st.pid, st.cmdline, st.state);
    }

    free(pids);

    grading_test_pass("P1-4", "passed spawn_list\n");
}

errval_t grading_run_tests_processes(void)
{
    if (grading_options.m3_subtest_run == 0) {
        return SYS_ERR_OK;
    }

    // run them on core 0 only, core 1 tests come in M5 / M6
    if (disp_get_core_id() != 0) {
        return SYS_ERR_OK;
    }


    grading_printf("#################################################\n");
    grading_printf("# TESTS: Milestone 3 (Process Management)        \n");
    grading_printf("#################################################\n");

    spawn_one_without_args();
    spawn_one_with_default_args();
    spawn_one_with_args();
    spawn_list();

    grading_printf("#################################################\n");
    grading_printf("# DONE:  Milestone 3 (Process Management)        \n");
    grading_printf("#################################################\n");

    grading_stop();

    return SYS_ERR_OK;
}

bool grading_opts_handle_m3_tests(struct grading_options *opts, const char *arg)
{
    (void)arg;

    // enable the m3 tests
    opts->m3_subtest_run = 0x1;

    // TODO(optional): parsing options to selectively enable tests or configure them at runtime.

    return true;
}