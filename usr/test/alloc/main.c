/*
 * Copyright (c) 2016 ETH Zurich.
 * Copyright (c) 2022 The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstr. 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */


#include <stdio.h>

#include <aos/aos.h>
#include <grading/grading.h>
#include <grading/io.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    errval_t err;

    coreid_t core = disp_get_core_id();

    /// !!! Keep those prints here to make the tests go through
    grading_printf("alloc running on core %d\n", core);

    struct capref frame;

    err = frame_alloc(&frame, BASE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("M1-1", "frame_alloc on core %d failed %s\n", core, err_getstring(err));
    }

    grading_printf("allocated %zu bytes on core %d\n", BASE_PAGE_SIZE, core);

    err = frame_alloc(&frame, LARGE_PAGE_SIZE, NULL);
    if (err_is_fail(err)) {
        grading_test_fail("M1-1", "frame_alloc failed %s\n", err_getstring(err));
    }

    grading_printf("allocated %zu bytes on core %d\n", LARGE_PAGE_SIZE, core);

    while(1) {
        thread_yield();
    }

    return EXIT_SUCCESS;
}
