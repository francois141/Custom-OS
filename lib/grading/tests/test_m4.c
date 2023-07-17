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


#include <aos/aos.h>
#include <aos/solution.h>
#include <proc_mgmt/proc_mgmt.h>
#include <grading/grading.h>

#include <grading/io.h>
#include <grading/state.h>
#include <grading/options.h>
#include <grading/tests.h>
#include "../include/grading/options_internal.h"

#define BINARY_NAME "rpcclient"

errval_t grading_run_tests_rpc(void)
{
    errval_t err;

    if (grading_options.m4_subtest_run == 0) {
        return SYS_ERR_OK;
    }

    // run them on core 0 only, core 1 tests come in M5 / M6
    coreid_t core = disp_get_core_id();
    if (core != 0) {
        return SYS_ERR_OK;
    }

    grading_printf("#################################################\n");
    grading_printf("# TESTS: Milestone 4 (RPC)                       \n");
    grading_printf("#################################################\n");

    domainid_t pid;
    err = proc_mgmt_spawn_with_cmdline(BINARY_NAME, core, &pid);
    if (err_is_fail(err)) {
        grading_test_fail("R1-1", "failed to spawn %s: %s\n", BINARY_NAME, err_getstring(err));
        return err_push(err, PROC_MGMT_ERR_SPAWND_REQUEST);
    }

    grading_printf("#################################################\n");
    grading_printf("# DONE:  Milestone 4 (RPC)                       \n");
    grading_printf("#################################################\n");

    return SYS_ERR_OK;
}


bool grading_opts_handle_m4_tests(struct grading_options *opts, const char *arg)
{
    (void)arg;

    // enable the m4 tests
    opts->m4_subtest_run = 0x1;
    opts->rpc_stub_enable = true;

    // TODO(optional): parsing options to selectively enable tests or configure them at runtime.

    return true;
}