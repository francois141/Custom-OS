/**
 * \file
 * \brief Hello world application
 */

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
    /// !!! Keep those prints here to make the tests go through
    grading_printf("Hello, world! from userspace (%u)\n", argc);

    for (int i = 0; i < argc; i++) {
        grading_printf("argv[%d] = %s\n", i, argv[i]);
    }

    while(1) {
        thread_yield();
    }

    return EXIT_SUCCESS;
}
