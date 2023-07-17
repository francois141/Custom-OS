#ifndef LIBBARRELFISH_CMDBUILTINS_H
#define LIBBARRELFISH_CMDBUILTINS_H

#include "../containers/trie.h"
#include "../session/session.h"
#include "cmdparse.h"

// #define CMD_BUILTIN_PRINT_PARSED

typedef int (*cmd_builtin_fn)(struct shell_session *session, struct parsed_command *);

struct cmd_builtin {
    cmd_builtin_fn fn;
    char *help;
    char *usage;
    char *description;
    bool  alias;  ///< alias required --wd <...>
};

// Builtin Command Groups
#define CMD_BUILTIN_GROUP_BASIC      0
#define CMD_BUILTIN_GROUP_FILESYSTEM 1
#define CMD_BUILTIN_GROUP_NETWORK    2
#define CMD_BUILTIN_GROUP_UTIL       3
#define CMD_BUILTIN_GROUP_DEBUG      4

#define CMD_BUILTIN_FOREACH(BUILTIN, ALIAS)                                                         \
    BUILTIN(man, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_man, "display a manual pages",               \
            "man <builtin>", NULL)                                                                  \
    ALIAS(echo, CMD_BUILTIN_GROUP_BASIC, "writes the first argument to standard output",            \
          "echo <message>",                                                                         \
          "prints the provided <message> to stdout.\n    NOTE: `echo` only accepts a single "       \
          "argument (use quotes for a \"message with spaces\").")                                   \
    BUILTIN(led, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_led, "turns the LED on/off", NULL, NULL)     \
    BUILTIN(run_memtest, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_memtest,                             \
            "runs a memtest in a user-level thread", NULL, NULL)                                    \
    BUILTIN(oncore, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_run,                                      \
            "run an application on a specific core", "oncore <core_id> <command> [&]", NULL)        \
    BUILTIN(run, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_run,                                         \
            "run an application with the given command line", "run <command> [&]", NULL)            \
    BUILTIN(ps, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_ps, "show the currently running processes",   \
            "ps", NULL)                                                                             \
    BUILTIN(kill, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_kill,                                       \
            "kills the process with the specified pid", "kill <pid>", NULL)                         \
    BUILTIN(pause, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_pause,                                     \
            "pauses the process with the specified pid", "pause <pid>", NULL)                       \
    BUILTIN(resume, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_resume,                                   \
            "resumes the process with the specified pid", "resume <pid>", NULL)                     \
    BUILTIN(help, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_help, "show the available commands",        \
            "help [builtin]", NULL)                                                                 \
    BUILTIN(exit, CMD_BUILTIN_GROUP_BASIC, _cmd_builtin_exit, "exits the active shell session",     \
            "exit [status_code]", NULL)                                                             \
    BUILTIN(pwd, CMD_BUILTIN_GROUP_FILESYSTEM, _cmd_builtin_pwd, "return working directory name",   \
            "pwd", NULL)                                                                            \
    BUILTIN(cd, CMD_BUILTIN_GROUP_FILESYSTEM, _cmd_builtin_cd, "change the working directory",      \
            "cd <path>", "the specified <path> can be either relative or absolute.")                \
    ALIAS(ls, CMD_BUILTIN_GROUP_FILESYSTEM, "list directory contents",                              \
          "ls [-al] [directories...]", NULL)                                                        \
    ALIAS(cat, CMD_BUILTIN_GROUP_FILESYSTEM, "concatenate and print files", "cat [files...]", NULL) \
    ALIAS(tee, CMD_BUILTIN_GROUP_FILESYSTEM, "duplicate standard input", NULL, NULL)                \
    BUILTIN(mkdir, CMD_BUILTIN_GROUP_FILESYSTEM, _cmd_builtin_fs_mkdir, "make directories",         \
            "mkdir [directory]", NULL)                                                              \
    BUILTIN(rmdir, CMD_BUILTIN_GROUP_FILESYSTEM, _cmd_builtin_fs_rmdir, "remove directories",       \
            "rmdir [directory]", NULL)                                                              \
    BUILTIN(rm, CMD_BUILTIN_GROUP_FILESYSTEM, _cmd_builtin_fs_rm, "remove directory entries",       \
            "rm [file]", NULL)                                                                      \
    ALIAS(ping, CMD_BUILTIN_GROUP_NETWORK, "ping IP address", NULL, NULL)                           \
    BUILTIN(send, CMD_BUILTIN_GROUP_NETWORK, _cmd_builtin_network_send, "send UDP packet",          \
            "send udp  <src_port> <ip:port> data", NULL)                                            \
    ALIAS(listen, CMD_BUILTIN_GROUP_NETWORK, "listen on some port", "listen <udp> port", NULL)      \
    BUILTIN(setio, CMD_BUILTIN_GROUP_NETWORK, _cmd_builtin_network_setio, "set io method",          \
            "setio <serial> / setio <udp> <src_port> <ip:port>", NULL)                              \
    BUILTIN(time, CMD_BUILTIN_GROUP_UTIL, _cmd_builtin_time,                                        \
            "measures the time taken to execute another command", "time <command>",                 \
            "NOTE: `time` it must be positioned before any other command.")                         \
    BUILTIN(clear, CMD_BUILTIN_GROUP_UTIL, _cmd_builtin_clear, "clears the screen", "clear [...]",  \
            NULL)                                                                                   \
    BUILTIN(reboot, CMD_BUILTIN_GROUP_UTIL, _cmd_builtin_reboot, "reboots the system",              \
            "reboot [...]", NULL)                                                                   \
    BUILTIN(false, CMD_BUILTIN_GROUP_UTIL, _cmd_builtin_shortcircuit, "returns EXIT_FAILURE",       \
            "false", NULL)                                                                          \
    BUILTIN(true, CMD_BUILTIN_GROUP_UTIL, _cmd_builtin_shortcircuit, "returns EXIT_SUCCESS",        \
            "true", NULL)                                                                           \
    BUILTIN(test, CMD_BUILTIN_GROUP_UTIL, _cmd_builtin_test,                                        \
            "run the specified tests in user-level", "test [-aq]", NULL)

void cmd_register_builtins(struct shell_session *session);

int cmd_dispatch_command(struct shell_session *session, struct parsed_command *cmd);
int cmd_dispatch_commands(struct shell_session *session, struct parsed_command_pipeline *pl);

#endif  // LIBBARRELFISH_CMDBUILTINS_H