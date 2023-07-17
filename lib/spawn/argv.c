/*
 * Copyright (c) 2016, ETH Zurich.
 * Copyright (c) 2022 The University of British Columbia
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <aos/aos.h>
#include <barrelfish_kpi/init.h>
#include <spawn/argv.h>

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
char **make_argv(const char *cmdline, int *_argc, char **buf)
{
    char **argv = calloc(MAX_CMDLINE_ARGS + 1, sizeof(char *));
    if (!argv)
        return NULL;

    /* Carefully calculate the length of the command line. */
    size_t len = strnlen(cmdline, PATH_MAX + 1);
    if (len > PATH_MAX)
        return NULL;

    /* Copy the command line, as we'll chop it up. */
    *buf = malloc(len + 1);
    if (!*buf) {
        free(argv);
        return NULL;
    }
    strncpy(*buf, cmdline, len + 1);
    (*buf)[len] = '\0';

    int    argc = 0;
    size_t i    = 0;
    while (i < len && argc < MAX_CMDLINE_ARGS) {
        /* Skip leading whitespace. */
        while (i < len && isspace((unsigned char)(*buf)[i]))
            i++;

        /* We may have just walked off the end. */
        if (i >= len)
            break;

        if ((*buf)[i] == '"') {
            /* If the first character is ", then we need to scan until the
             * closing ". */

            /* The next argument starts *after* the opening ". */
            i++;
            argv[argc] = &(*buf)[i];
            argc++;

            /* Find the closing ". */
            while (i < len && (*buf)[i] != '"')
                i++;

            /* If we've found a ", overwrite it to null-terminate the string.
             * Otherwise, let the argument be terminated by end-of-line. */
            if (i < len) {
                (*buf)[i] = '\0';
                i++;
            }
        } else {
            /* Otherwise grab everything until the next whitespace
             * character. */

            /* The next argument starts here. */
            argv[argc] = &(*buf)[i];
            argc++;

            /* Find the next whitespace (if any). */
            while (i < len && !isspace((unsigned char)(*buf)[i]))
                i++;

            /* Null-terminate the string by overwriting the first whitespace
             * character, unless we're at the end, in which case the null at
             * the end of buf will terminate this argument. */
            if (i < len) {
                (*buf)[i] = '\0';
                i++;
            }
        }
    }
    /* (*buf)[argc] == NULL */

    *_argc = argc;
    return argv;
}

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
char *make_cmdline(int argc, const char *argv[])
{
    assert(argv);

    char *cmdline = NULL;

    // figure out the length of the cmdline
    size_t len = 0;
    for (int i = 0; i < argc; i++) {
        assert(argv[i]);
        len += strlen(argv[i]) + 1;  // +1 for the additional space
    }

    if (len > 0) {
        cmdline = malloc(len + 1);
        if (cmdline == NULL) {
            return NULL;
        }

        int offset = snprintf(cmdline, len, "%s", argv[0]);
        for (int i = 1; i < argc; i++) {
            offset += snprintf(cmdline + offset, len - offset, " %s", argv[i]);
        }

        // null terminate the string
        cmdline[len] = '\0';
    }

    return cmdline;
}