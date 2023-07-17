#ifndef ARGPARSE_H_
#define ARGPARSE_H_

#include <aos/aos.h>

errval_t argv_to_cmdline(int argc, const char *argv[], char **cmdline);

errval_t cmdline_to_argv_blk(const char *cmdline, int *argc, char ***argv);
errval_t cmdline_to_argv(const char *cmdline, int *argc, char ***argv);

#endif /*ARGPARSE_H_*/