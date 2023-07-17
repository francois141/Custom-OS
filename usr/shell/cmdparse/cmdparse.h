#ifndef LIBBARRELFISH_CMDPARSE_H
#define LIBBARRELFISH_CMDPARSE_H

#include <stdlib.h>
#include <stdbool.h>

#include <aos/debug.h>

#include "../containers/trie.h"

#define CMD_OPERATOR_LAND '^'  ///< represent && (logical and) in a single symbol
#define CMD_OPERATOR_LOR  'v'  ///< represent || (logical or) in a single symbol

struct parsed_command {
    char  *command;
    size_t argc;
    char **argv;
};

struct parsed_command_pipeline {
    size_t                 size;
    struct parsed_command *cmds;
    char                  *ops;
    char                  *err_str;  ///< contains string specific to the current error or NULL
};

enum parse_mode { PARSE_MODE_COMMAND, PARSE_MODE_ARGUMENT, PARSE_MODE_VARIABLE, PARSE_MODE_NONE };

struct parsed_autocomplete {
    enum parse_mode mode;
    char           *ctx, *buf;
    size_t          begin, end;
    size_t          position;  ///< relevant for PARSE_MODE_ARGUMENT: argument position
};

struct parsed_define {
    bool  valid;
    char *key;
    char *value;
    char *err_str;  ///< contains string specific to the current error or NULL
};

struct parsed_autocomplete cmd_autocomplete(struct trie *vars, char *line, size_t cursor);

errval_t cmd_parse_line(struct trie *vars, char *line, struct parsed_command_pipeline *pl);

errval_t cmd_parse_define(struct trie *vars, char *command, struct parsed_define *pd);

typedef struct {
    size_t      begin;
    const char *color;
} cmdline_color_t;

size_t cmdline_color(struct trie *vars, char *line, cmdline_color_t **colors);
char  *cmdline_apply_colors(const char *line, size_t begin, size_t len, size_t count,
                            cmdline_color_t *colors);

errval_t command_pipeline_deinit(struct parsed_command_pipeline *pl);

#endif  // LIBBARRELFISH_CMDPARSE_H
