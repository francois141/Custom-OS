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
//                   !! WARNING !!   DO NOT EDIT THIS FILE   !! WARNING !!                       //
//                                                                                               //
//      This file is part of the grading library and will be overwritten before grading.         //
//              To ensure tests are run correctly, do not edit this file                         //
//                                                                                               //
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __GRADING_OPTIONS_H
#define __GRADING_OPTIONS_H

#include <aos/solution.h>


struct grading_options {

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 1
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m1_subtest_run;

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 2
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m2_subtest_run;

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 3
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m3_subtest_run;

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 4
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m4_subtest_run;
    bool     rpc_stub_enable;

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 5
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m5_subtest_run;

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 6
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m6_subtest_run;

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 7 (Filesystem)
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m7_fs_subtest_run;

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 7 (Nameservice)
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m7_ns_subtest_run;

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 7 (Shell)
     * --------------------------------------------------------------------------------------------
     */
    // no tests here, will run by typing the shell

    /*
     * --------------------------------------------------------------------------------------------
     * Test Milestone 7 (Capabilities)
     * --------------------------------------------------------------------------------------------
     */

    uint32_t m7_caps_subtest_run;
};

/* Checks to see if the given argument is a grading option (starts with "g:")
 * and, if so, sets the appropriate option values and returns true.  Otherwise
 * returns false. */
bool grading_handle_arg(struct grading_options *options, const char *arg);

#endif /* __GRADING_OPTIONS_H */
