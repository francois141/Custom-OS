/*
 * Copyright (c) 2016, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef __SPAWN_ARGV_H
#define __SPAWN_ARGV_H 1


/**
 * @brief Tokenize the commandline arguments into an argv array
 *
 * @param[in]  cmdline  the program's commandline to be tokenized
 * @param[out] argc     number of arguments in the returned argv array
 * @param[out] buf      newly allocated memory backing the arguments (`\0` separated)
 *
 * @return newly allocated argv array holding pointers to the argv strings, or NULL on failure
 *
 * Note, the caller of this function is responsible for freeing the two returned pointers
 */
char **make_argv(const char *cmdline, int *_argc, char **buf);


/**
 * @brief constructs a comamdnline from the given argv array
 *
 * @param[in] argc  number of arguments in argv
 * @param[in] argv  command line arguments
 *
 * @returns newly allocated pointer for the command line, or NULL on failure
 *
 * Note, the caller of this function is responsible for freeing the returned pointer
 */
char *make_cmdline(int argc, const char *argv[]);

#endif /* __SPAWN_ARGV_H */
