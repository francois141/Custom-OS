/*
 * Copyright (c) 2016, ETH Zurich.
 * Copyright (c) 2022, The University of British Columbia.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef MULTIBOOT_H_
#define MULTIBOOT_H_

struct bootinfo;
struct mem_region;

/**
 * @brief looksup a module with the given name in the bootinfo struct
 *
 * @param[in] bi    pointer to the bootinfo struct
 * @param[in] name  name of the module to look up
 *
 * @return pointer to the memory region of the module, NULL if there was no module found
 *
 * This function returns a pointer to the mem_region struct stored in the bootinfo
 * struct's memory regions array that corresponds to 'name'. 'name' can either be
 * the absolute path of a binary (starting with '/') or simply the relative path of
 * the binary (relative to the directory build/armv8/sbin/).
 */
struct mem_region *multiboot_find_module(struct bootinfo *bi, const char *name);

/**
 * @brief obtains the command line of the module starting at the binary name
 *
 * @param[in] mod   the module to get the commandline options from
 *
 * @return pointer to the multiboot strings of this module starting at the binary name
 *
 * Note: the returned buffer must not be freed by the caller. It points to the multiboot
 * strings region.
 */
const char *multiboot_module_opts(struct mem_region *mod);

/**
 * @brief obtains name of the module line as written in the menu.lst file
 *
 * @param[in] mod  the module to get the raw string from
 *
 * @return pointer to the multiboot strings of this module
 *
 * Note: the returned buffer must not be freed by the caller. It points to the multiboot
 * strings region.
 */
const char *multiboot_module_rawstring(struct mem_region *mod);


/**
 * @brief obtains name of the module
 *
 * @param[in] mod  the module to get the name from
 *
 * @return pointer to a static buffer containing the module name
 *
 * Note: the returned buffer must not be freed. Calling the function multiple times
 * will overwrite the previous name in the buffer.
 *
 */
const char *multiboot_module_name(struct mem_region *mod);

#endif
