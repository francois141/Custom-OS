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


/* This module contains *all* per-process state for the grading library. */

#include <stdio.h>

#include <aos/aos.h>
#include <grading/grading.h>
#include <spawn/multiboot.h>

#include <grading/io.h>
#include <grading/state.h>
#include <grading/options.h>
#include <grading/tests.h>

/*** Library State ***/
/* XXX - No global variables outside this section.  All state (e.g.
 * malloc()-ed structs) must be accessible from a root in here.  All
 * non-static variables will also be prefixed with 'grading_'. */

coreid_t                  grading_coreid;
struct grading_options    grading_options = {0};
const char               *grading_proc_name;
int                       grading_argc;
char                    **grading_argv;
struct bootinfo          *grading_bootinfo;
enum grading_argument_src grading_argument_src;

/*** End Library State ***/

static char *strdup_static(const char *in)
{
    static int  buf_used = 0;
    static char buf[1024];
    if (strlen(in) + 1 >= sizeof(buf) || buf_used) {
        return NULL;
    }
    memcpy(buf, in, strlen(in) + 1);
    buf_used = 1;
    return buf;
}

static int make_argv_from_cmdline(const char *cmdline, int argc_max, char **argv, char **buffer)
{
    /* Make a copy of the command line, before we chop it up. */
    char *cmdbuf = strdup_static(cmdline);
    if (!cmdbuf) {
        grading_printf("strdup() failed\n");
        return -1;
    }

    /* Skip leading whitespace, and initialise the tokeniser. */
    char *saveptr = NULL;
    char *tok     = strtok_r(cmdbuf, " \t", &saveptr);

    /* Split on whitespace. */
    int argc = 0;
    while (tok && argc < argc_max) {
        argv[argc] = tok;
        argc++;

        tok = strtok_r(NULL, " \t", &saveptr);
    }

    *buffer = cmdbuf;
    return argc;
}

/* Constructs an argv[] list of pointers to the command-line arguments
 * retrieved from bootinfo.  The argv[] array passed in must be preallocated,
 * and of at least argc_max entries.  Returns argc, or -1 on error.
 * This function must not cause any allocations.
 *
 * XXX - doesn't handle quoted strings. */
static int make_argv(struct bootinfo *bi, const char *init, int argc_max, char **argv, char **buffer)
{
    struct mem_region *module = multiboot_find_module(bi, init);
    if (!module) {
        grading_printf("multiboot_find_module() failed\n");
        return -1;
    }

    const char *cmdline = multiboot_module_opts(module);
    if (!cmdline) {
        grading_printf("multiboot_module_opts() failed\n");
        return -1;
    }

    return make_argv_from_cmdline(cmdline, argc_max, argv, buffer);
}

static void parse_bootinfo(struct bootinfo *bi)
{
    int   mb_argc;
    char *mb_argv[256];
    char *buffer;

    if (!bi)
        grading_panic("Bootinfo pointer is null.\n");

    /* Find init's multiboot command line arguments. */
    mb_argc = make_argv(bi, grading_proc_name, 256, mb_argv, &buffer);
    if (mb_argc < 0) {
        grading_panic("Couldn't find init's multiboot command line.\n");
        return;
    }

    /* Echo the multiboot arguments. */
    if (mb_argc < 1) {
        grading_panic("mb_argc < 1");
        return;
    }
    grading_printf("mb_argv = [\"%s\"", mb_argv[0]);
    for (int i = 1; i < mb_argc; i++)
        grading_printf_nb(",\"%s\"", mb_argv[i]);
    grading_printf_nb("]\n");

    /* Parse the arguments. */
    for (int i = 1; i < mb_argc; i++) {
        grading_handle_arg(&grading_options, mb_argv[i]);
    }

    grading_argument_src = GRADING_ARG_SRC_DONE;
}

static void parse_cmdline(const char *cmdline)
{
    int   mb_argc;
    char *mb_argv[256];
    char *buffer;

    /* Find init's multiboot command line arguments. */
    mb_argc = make_argv_from_cmdline(cmdline, 256, mb_argv, &buffer);
    if (mb_argc < 0) {
        grading_panic("Couldn't parse command line.\n");
        return;
    }

    /* Echo the multiboot arguments. */
    if (mb_argc < 1) {
        grading_panic("mb_argc < 1");
        return;
    }
    grading_printf("cmdline_argv = [\"%s\"", mb_argv[0]);
    for (int i = 1; i < mb_argc; i++)
        grading_printf_nb(",\"%s\"", mb_argv[i]);
    grading_printf_nb("]\n");

    /* Parse the arguments. */
    for (int i = 1; i < mb_argc; i++) {
        grading_handle_arg(&grading_options, mb_argv[i]);
    }

    grading_argument_src = GRADING_ARG_SRC_DONE;
}

void grading_parse_arguments(void)
{
    errval_t err;

    /* Find the core ID. */
    err = invoke_kernel_get_core_id(cap_kernel, &grading_coreid);
    if (err_is_fail(err))
        grading_panic("Couldn't get core ID.\n");
    grading_printf("Grading setup on core %u\n", (unsigned int)grading_coreid);

    debug_printf("argsrc = %d\n", GRADING_ARG_SRC_BI);

    if (grading_argument_src == GRADING_ARG_SRC_BI) {
        parse_bootinfo(grading_bootinfo);
        return;
    }

    if (grading_argument_src == GRADING_ARG_SRC_ARGV_BI) {
        if (grading_argc < 2) {
            grading_printf("argc < 2 !\n");
            return;
        }
        struct bootinfo *bi = (struct bootinfo *)strtol(grading_argv[1], NULL, 10);
        parse_bootinfo(bi);
        return;
    }

    // here we parse the cmdline as passed in by the second argument to the process.
    // this is mainly for BSP init where the invocation is `init <biaddr> <cmdline>`
    if (grading_argument_src == GRADING_ARG_SRC_CMDLINE) {
        if (grading_argc < 3) {
            grading_printf("argc < 3 !\n");
            return;
        }
        parse_cmdline(grading_argv[2]);
        return;
    }

    if (grading_argument_src == GRADING_ARG_SRC_ARGV) {
        if (grading_argc < 1) {
            grading_printf("argc < 1 !\n");
            return;
        }
        grading_printf("argv = [\"%s\"", grading_argv[0]);
        for (int i = 1; i < grading_argc; i++)
            grading_printf_nb(",\"%s\"", grading_argv[i]);
        grading_printf_nb("]\n");

        grading_proc_name = grading_argv[0];
        for (int i = 1; i < grading_argc; i++) {
            grading_handle_arg(&grading_options, grading_argv[i]);
        }
    }

    grading_argument_src = GRADING_ARG_SRC_DONE;
}
