#include "cmdbuiltins.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <aos/aos_rpc.h>
#include <aos/deferred.h>
#include <aos/syscalls.h>
#include <aos/systime.h>
#include <aos/network.h>

#include <fs/fs.h>
#include <fs/dirent.h>

#include "../pathutil/pathutil.h"
#include "../tty/tty.h"
#include "../../init/proc_mgmt.h"

static errval_t _cmd_parse_int(const char *str, int *res)
{
    char *endptr;
    *res = strtol(str, &endptr, 10);
    if (endptr == NULL || *endptr != '\0') {
        return SYS_ERR_ILLEGAL_INVOCATION;
    }
    return SYS_ERR_OK;
}

static void _cmd_session_set_pid(struct shell_session *session, domainid_t pid)
{
    char *pid_var = malloc(sizeof(char) * 32);
    snprintf(pid_var, 32, "%d", pid);
    trie_insert(&session->vars, "!", pid_var);
}

static void _cmd_unexpected_num_args(char *builtin, size_t received, size_t expected)
{
    printf("%sUnexpected number of arguments for `%s`. Received %zu, but expected: %zu.\n%s",
           TTY_COLOR_BOLD_RED, builtin, received, expected, TTY_COLOR_RESET);
}

static void _cmd_incorrect_usage(const char *usage)
{
    printf("%susage: %s%s\n", TTY_COLOR_BOLD_RED, usage, TTY_COLOR_RESET);
}

static int _cmd_display_man(struct shell_session *session, char *name)
{
    struct cmd_builtin *builtin = trie_lookup(&session->cmds, name);
    if (builtin == NULL) {
        printf("%sman: unknown command `%s`%s\n", TTY_COLOR_BOLD_RED, name, TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    printf("%sNAME%s\n", TTY_COLOR_BOLD_BLUE, TTY_COLOR_RESET);
    printf("    %s - %s\n", name, builtin->help);
    if (builtin->usage != NULL) {
        printf("\n%sUSAGE%s\n", TTY_COLOR_BOLD_BLUE, TTY_COLOR_RESET);
        printf("    %s\n", builtin->usage);
    }
    if (builtin->description != NULL) {
        printf("\n%sDESCRIPTION%s\n", TTY_COLOR_BOLD_BLUE, TTY_COLOR_RESET);
        printf("    %s\n", builtin->description);
    }
    return EXIT_SUCCESS;
}

static int _cmd_builtin_shortcircuit(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    return strcmp(cmd->command, "true") == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int _cmd_builtin_man(struct shell_session *session, struct parsed_command *cmd)
{
    if (cmd->argc != 1) {
        _cmd_unexpected_num_args("man", cmd->argc, 1);
        return EXIT_FAILURE;
    }
    return _cmd_display_man(session, cmd->argv[0]);
}

static int _cmd_builtin_led(struct shell_session *session, struct parsed_command *cmd)
{
    // TODO implement this builtin
    (void)session;
    (void)cmd;
    printf("%sled: NYI%s\n", TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
    return EXIT_FAILURE;
}

static int _cmd_builtin_memtest(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    (void)cmd;
    const int num_iterations = 1;
    const int size           = 512;
    const int stride         = 40;
    printf("run_memtest: running %d iteration(s) of size=%d pages\n", num_iterations, size);
    for (int it = 0; it < num_iterations; ++it) {
        size_t alloc_size = size * BASE_PAGE_SIZE;
        printf("run_memtest(%d): attempting to allocate buffer of size=%zu bytes (%zu "
               "BASE_PAGE_SIZE).\n",
               it + 1, alloc_size, alloc_size / BASE_PAGE_SIZE);
        char *buf = malloc(sizeof(char) * alloc_size);
        if (buf == NULL) {
            printf("%srun_memtest(%d): malloc failed. attempted to allocate size=%zu bytes (%zu "
                   "BASE_PAGE_SIZE)\n%s",
                   TTY_COLOR_BOLD_RED, it + 1, alloc_size, alloc_size / BASE_PAGE_SIZE,
                   TTY_COLOR_RESET);
            return EXIT_FAILURE;
        }
        printf("run_memtest(%d): writing to buffer at: %p...", it + 1, buf);
        for (size_t i = 0; i < alloc_size; i += (alloc_size / stride)) {
            char expected = 'a' + ((i / 200) % 26);
            *(buf + i)    = expected;
        }
        printf("Done\n");
        printf("run_memtest(%d): reading/validating from buffer at: %p...", it + 1, buf);
        for (size_t i = 0; i < alloc_size; i += (alloc_size / stride)) {
            char expected = 'a' + ((i / 200) % 26);
            char value    = *(buf + i);
            if (value != expected) {
                printf("\n%srun_memtest(%d): unexpected value encountered during read; "
                       "expected='%c' but got='%c'%s\n",
                       TTY_COLOR_BOLD_RED, it + 1, expected, value, TTY_COLOR_RESET);
                return EXIT_FAILURE;
            }
        }
        printf("Done\n");
        free(buf);
        printf("run_memtest(%d): Completed iteration %d.\n", it + 1, it + 1);
    }
    printf("Completed test_frame_alloc.\n");
    return EXIT_SUCCESS;
}

static bool _cmd_command_requires_fs(const char *binary)
{
    return strlen(binary) >= 7 && strncasecmp("/SDCARD/", binary, 7) == 0;
}

static errval_t _cmd_builtin_dispatch_run(int argc, const char **argv, coreid_t core,
                                          domainid_t *pid, bool background, int *status,
                                          struct capref *frames)
{
    errval_t err = SYS_ERR_OK;
    // XXX provide some useful user message
    if (core != 0 && argc > 0 && _cmd_command_requires_fs(argv[0])) {
        printf("%sshell: spawning programs from the filesystem is not supported on cores != "
               "0.\n%sNOTE: "
               "spawning application `%s` on core 0 instead.%s\n",
               TTY_COLOR_BOLD_RED, TTY_COLOR_RED_BG, argv[0], TTY_COLOR_RESET);
        core = 0;
    }
    if (frames == NULL) {
        err = proc_mgmt_spawn_program_argv(argc, argv, core, pid);
    } else {
        err = proc_mgmt_spawn_mapped(argc, argv, 0, NULL, /*core*/ core, pid, frames[0], frames[1]);
    }
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "proc_mgmt_spawn_program_argv failed.");
        return EXIT_FAILURE;
    }
    *status = EXIT_SUCCESS;
    if (!background) {
        err = proc_mgmt_wait(*pid, status);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "proc_mgmt_wait failed");
            return EXIT_FAILURE;
        }
    }
    return SYS_ERR_OK;
}

static errval_t _cmd_alloc_frames(struct capref *frames, size_t count)
{
    errval_t err = SYS_ERR_OK;
    for (size_t i = 0; i < count; ++i) {
        size_t retbytes = 0;
        err             = frame_alloc(&frames[i], BASE_PAGE_SIZE, &retbytes);
        if (err_is_fail(err)) {
            for (size_t j = 0; j < i; ++j) {
                err = cap_destroy(frames[i]);
                if (err_is_fail(err)) {
                    DEBUG_ERR(err, "cap_destroy");
                }
            }
            return err;
        }
    }
    return SYS_ERR_OK;
}

static int _cmd_builtin_run(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    errval_t err  = SYS_ERR_OK;
    int      core = -1;
    if (strcmp(cmd->command, "run") == 0) {
        if (cmd->argc == 0) {
            _cmd_unexpected_num_args("run", 0, 1);
            return EXIT_FAILURE;
        }
    } else if (strcmp(cmd->command, "oncore") == 0) {
        if (cmd->argc <= 1) {
            _cmd_unexpected_num_args("oncore", cmd->argc, 2);
            return EXIT_FAILURE;
        }
        err = _cmd_parse_int(cmd->argv[0], &core);
        if (err_is_fail(err)) {
            printf("%soncore: invalid core_id `%s`%s\n", TTY_COLOR_BOLD_RED, cmd->argv[0],
                   TTY_COLOR_RESET);
            return EXIT_FAILURE;
        }
        // prepare the parsed_command for the next session
        cmd->argv += 1;
        --cmd->argc;
    }

    bool           background = false;
    struct capref *io_frames  = NULL;  ///< we override stdin, for background processes
    if (strcmp(cmd->argv[cmd->argc - 1], "&") == 0) {
        background = true;
        --cmd->argc;
        io_frames = malloc(sizeof(struct capref) * 2);
        err       = _cmd_alloc_frames(io_frames, 1);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "_cmd_alloc_frames");
            return EXIT_FAILURE;
        }
        io_frames[1] = NULL_CAP;  ///< stdout frame, don't override
    }

    domainid_t pid;
    int        status = EXIT_SUCCESS;
    err               = _cmd_builtin_dispatch_run(cmd->argc, (const char **)cmd->argv,
                                    core == -1 ? disp_get_current_core_id() : core, &pid,
                                                  background, &status, io_frames);

    _cmd_session_set_pid(session, pid);

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "_cmd_builtin_dispatch_run");
        return EXIT_FAILURE;
    }

    if (background) {
        ++cmd->argc;
        assert(capref_is_null(io_frames[1]));
        err = cap_destroy(io_frames[0]);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "cap_destroy");
            return EXIT_FAILURE;
        }
    }

    if (core != -1) {
        // undo the oncore changes
        cmd->argv -= 1;
        ++cmd->argc;
    }
    return status;
}

static const char *_cmd_ps_state_to_str(uint8_t state)
{
    static const char *states[] = {
        "UNKNOWN",   ///< the process state is unknown.
        "SPAWNING",  ///< the process is spawning.
        "RUNNING",   ///< the process is running normally.
        "PAUSED",    ///< the process has been paused
        "EXITED",    ///< the process has exited.
        "KILLED",    ///< the process has been killed.
    };
    return states[state];
}

static const char *_cmd_ps_state_to_color(uint8_t state)
{
    static const char *states[] = {
        "",          ///< the process state is unknown.
        "",          ///< the process is spawning.
        "\033[32m",  ///< the process is running normally.
        "\033[33m",  ///< the process has been paused
        "",          ///< the process has exited.
        "",          ///< the process has been killed.
    };
    return states[state];
}

static int _cmd_builtin_ps(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    errval_t err = SYS_ERR_OK;
    if (cmd->argc != 0) {
        _cmd_unexpected_num_args("ps", cmd->argc, 0);
        return EXIT_FAILURE;
    }
    struct aos_rpc *rpc       = aos_rpc_get_process_channel();
    domainid_t     *pids      = NULL;
    size_t          pid_count = 0;
    err                       = aos_rpc_proc_get_all_pids(rpc, &pids, &pid_count);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_proc_get_all_pids");
        return EXIT_FAILURE;
    }
    const size_t max_name_len = 128;
    char        *names        = malloc(pid_count * max_name_len * sizeof(char));
    size_t       pid_length = 3, name_length = 4;
    for (size_t index = 0; index < pid_count; ++index) {
        size_t len = floor(log10(pids[index]) + 1);
        if (len > pid_length) {
            pid_length = len;
        }
        err = aos_rpc_proc_get_name(rpc, pids[index], names + (index * max_name_len), max_name_len);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "aos_rpc_proc_get_name");
            free(pids);
            free(names);
            return EXIT_FAILURE;
        }
        size_t nlen = strlen(names + (index * max_name_len));
        if (nlen > name_length) {
            name_length = nlen;
        }
    }
    printf("\033[1mPID% *s  NAME% *s  CORE STATE     CMD\033[0m\n", pid_length - 3, "",
           name_length - 4, "");
    for (size_t index = 0; index < pid_count; ++index) {
        domainid_t pid = pids[index];
        coreid_t   core;
        char       cmdline[128];
        uint8_t    state;
        int        exit_code;
        err = aos_rpc_proc_get_status(rpc, pid, &core, cmdline, sizeof(cmdline) / sizeof(char),
                                      &state, &exit_code);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "aos_rpc_proc_get_status");
            free(pids);
            free(names);
            return EXIT_FAILURE;
        }
        printf("%*zu  %*s  %4zu %s%-8s\033[0m  %s\n", pid_length, pid, name_length,
               names + (index * max_name_len), core, _cmd_ps_state_to_color(state),
               _cmd_ps_state_to_str(state), cmdline);
    }
    free(pids);
    free(names);
    return EXIT_SUCCESS;
}

static int _cmd_builtin_kill(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    static const char *usage = "kill [pid]";
    if (cmd->argc != 1) {
        _cmd_unexpected_num_args("kill", cmd->argc, 1);
        return EXIT_FAILURE;
    }
    char  *endptr;
    size_t pid = strtoull(cmd->argv[0], &endptr, 10);
    if (endptr == NULL || *endptr != '\0') {
        _cmd_incorrect_usage(usage);
        return EXIT_FAILURE;
    }
    errval_t err = aos_rpc_proc_kill(aos_rpc_get_process_channel(), pid);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_proc_kill");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int _cmd_builtin_pause(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    static const char *usage = "pause [pid]";
    if (cmd->argc != 1) {
        _cmd_unexpected_num_args("pause", cmd->argc, 1);
        return EXIT_FAILURE;
    }
    char  *endptr;
    size_t pid = strtoull(cmd->argv[0], &endptr, 10);
    if (endptr == NULL || *endptr != '\0') {
        _cmd_incorrect_usage(usage);
        return EXIT_FAILURE;
    }
    errval_t err = aos_rpc_proc_pause(aos_rpc_get_process_channel(), pid);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_proc_pause");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int _cmd_builtin_resume(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    static const char *usage = "resume [pid]";
    if (cmd->argc != 1) {
        _cmd_unexpected_num_args("resume", cmd->argc, 1);
        return EXIT_FAILURE;
    }
    char  *endptr;
    size_t pid = strtoull(cmd->argv[0], &endptr, 10);
    if (endptr == NULL || *endptr != '\0') {
        _cmd_incorrect_usage(usage);
        return EXIT_FAILURE;
    }
    errval_t err = aos_rpc_proc_resume(aos_rpc_get_process_channel(), pid);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "aos_rpc_proc_resume");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int _cmd_builtin_pwd(struct shell_session *session, struct parsed_command *cmd)
{
    if (cmd->argc != 0) {
        _cmd_unexpected_num_args("pwd", cmd->argc, 0);
        return EXIT_FAILURE;
    }
    printf("%s\n", session_wd(session));
    return EXIT_SUCCESS;
}

static int _cmd_builtin_cd(struct shell_session *session, struct parsed_command *cmd)
{
    // NOTE because we are doing this on the logical level,
    //      something like "cd non_existent_directory/../good_directory" is possible.
    // XXX we treat "cd" as cd "/"
    if (cmd->argc > 1) {
        _cmd_unexpected_num_args("cd", cmd->argc, 1);
        return EXIT_FAILURE;
    }
    char *directory = (cmd->argc == 0) ? FS_ROOT_DIRECTORY : cmd->argv[0];
    if (!session_cd(session, directory)) {
        printf("%scd: no such file or directory: %s\n", TTY_COLOR_BOLD_RED, directory,
               TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int _cmd_builtin_fs_rm(struct shell_session *session, struct parsed_command *cmd)
{
    if (cmd->argc != 1) {
        _cmd_unexpected_num_args("rm", cmd->argc, 1);
        return EXIT_FAILURE;
    }
    errval_t err = SYS_ERR_OK;
    if (pathutil_is_abs_path(cmd->argv[0])) {
        err = mkdir(cmd->argv[0]);
    } else {
        char *path;
        if (!pathutil_concat_paths(session_wd(session), cmd->argv[0], &path)) {
            printf("%srm: invalid path '%s'\n%s", TTY_COLOR_BOLD_RED, cmd->argv[0], TTY_COLOR_RESET);
            return EXIT_FAILURE;
        }
        err = rm(path);
    }
    if (err_is_fail(err)) {
        printf("%srm: failed to remove file '%s'\n%s", TTY_COLOR_BOLD_RED, cmd->argv[0],
               TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int _cmd_builtin_network_send(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    if (cmd->argc != 4) {
        _cmd_unexpected_num_args("send", cmd->argc, 4);
        return EXIT_FAILURE;
    }

    if (strcmp(cmd->argv[0], "udp") != 0) {
        printf("%sOnly udp is supported%s\n", TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }

    uint8_t  ip[4];
    uint16_t port;
    if (sscanf(cmd->argv[2], "%hhu.%hhu.%hhu.%hhu:%hu", &ip[0], &ip[1], &ip[2], &ip[3], &port) != 5) {
        printf("%sWrong IPv4:port format%s\n", TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    uint32_t target_ip = *(uint32_t *)ip;
    uint16_t src_port  = atoi(cmd->argv[1]);
    printf("Sending packet to %s...\n", cmd->argv[1]);

    errval_t err = network_send(target_ip, port, SERVER_PROTOCOL_UDP, src_port,
                                strlen(cmd->argv[3]) + 1, cmd->argv[3]);
    if (err_is_fail(err)) {
        if (err_no(err) == NETWORK_ERR_IP_RESOLVE_TIMEOUT) {
            printf("%sCould not resolve ip %s%s\n", TTY_COLOR_BOLD_RED, cmd->argv[0],
                   TTY_COLOR_RESET);
        } else if (err_no(err) == NETWORK_ERR_REQUEST_TIMEOUT) {
            printf("%Network request timeout%s\n", TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
        } else {
            printf("%sAn error occured: %s%s\n", TTY_COLOR_BOLD_RED, err_getstring(err),
                   TTY_COLOR_RESET);
        }
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int _cmd_builtin_network_setio(struct shell_session *session, struct parsed_command *cmd){
    (void)session;
    if(cmd->argc != 1 && cmd->argc != 3){
        _cmd_unexpected_num_args("send", cmd->argc, 1);
        return EXIT_FAILURE;
    }

    if(strcmp(cmd->argv[0], "udp") != 0 && strcmp(cmd->argv[0], "serial") != 0){
        printf("%sOnly serial and udp are supported%s\n", TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }

    uint32_t target_ip = 0;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    bool is_network = false;
    if(strcmp(cmd->argv[0], "udp") == 0){
        uint8_t ip[4];
        if(sscanf(cmd->argv[2], "%hhu.%hhu.%hhu.%hhu:%hu", &ip[0], &ip[1], &ip[2], &ip[3], &dst_port) != 5){
            printf("%sWrong IPv4:port format%s\n", TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
            return EXIT_FAILURE;
        }
        target_ip = *(uint32_t*)ip;
        src_port = atoi(cmd->argv[1]);
        printf("Switching to io over UDP...\n", cmd->argv[1]);
        is_network = true;
    } else {
        printf("Switching to serial io\n");
    }

    
    errval_t err = network_set_io(is_network, false, target_ip, dst_port, src_port);
    if(err_is_fail(err)){
        printf("%sAn error occured: %s%s\n", TTY_COLOR_BOLD_RED, err_getstring(err), TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

static int _cmd_builtin_fs_rmdir(struct shell_session *session, struct parsed_command *cmd)
{
    if (cmd->argc != 1) {
        _cmd_unexpected_num_args("rmdir", cmd->argc, 1);
        return EXIT_FAILURE;
    }
    errval_t err = SYS_ERR_OK;
    if (pathutil_is_abs_path(cmd->argv[0])) {
        err = rmdir(cmd->argv[0]);
    } else {
        char *path;
        if (!pathutil_concat_paths(session_wd(session), cmd->argv[0], &path)) {
            printf("%srmdir: invalid path '%s'\n%s", TTY_COLOR_BOLD_RED, cmd->argv[0],
                   TTY_COLOR_RESET);
            return EXIT_FAILURE;
        }
        err = rmdir(path);
    }
    if (err_is_fail(err)) {
        printf("%srmdir: failed to remove directory '%s'\n%s", TTY_COLOR_BOLD_RED, cmd->argv[0],
               TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int _cmd_builtin_fs_mkdir(struct shell_session *session, struct parsed_command *cmd)
{
    if (cmd->argc != 1) {
        _cmd_unexpected_num_args("mkdir", cmd->argc, 1);
        return EXIT_FAILURE;
    }
    errval_t err = SYS_ERR_OK;
    if (pathutil_is_abs_path(cmd->argv[0])) {
        err = mkdir(cmd->argv[0]);
    } else {
        char *path;
        if (!pathutil_concat_paths(session_wd(session), cmd->argv[0], &path)) {
            printf("%smkdir: invalid path '%s'\n%s", TTY_COLOR_BOLD_RED, cmd->argv[0],
                   TTY_COLOR_RESET);
            return EXIT_FAILURE;
        }
        err = mkdir(path);
    }
    if (err_is_fail(err)) {
        printf("%smkdir: failed to create directory '%s'\n%s", TTY_COLOR_BOLD_RED, cmd->argv[0],
               TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int _cmd_builtin_time(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    (void)cmd;
    /// XXX time is handled separately to support timing pipelines
    printf("%sUnsupported position for `time`.\n%sNOTE: `time` it must be positioned before any "
           "other command.%s\n",
           TTY_COLOR_BOLD_RED, TTY_COLOR_RED_BG, TTY_COLOR_RESET);
    return EXIT_FAILURE;
}

static const char *cmd_builtin_group_names[] = { "Basic", "File Management", "Network", "Utility",
                                                 "Debug" };

static int _cmd_builtin_help(struct shell_session *session, struct parsed_command *cmd)
{
    if (cmd->argc == 1) {
        return _cmd_display_man(session, cmd->argv[0]);
    }
    if (cmd->argc != 0) {
        _cmd_unexpected_num_args("help", cmd->argc, 0);
        return EXIT_FAILURE;
    }
    size_t name_len = 0, desc_len = 0;
#define BUILTIN_LENGTH(NAME, GROUP, FN, DESCRIPTION, MAN_PAGE, NOTE)                               \
    {                                                                                              \
        name_len = MAX(name_len, strlen(#NAME));                                                   \
        desc_len = MAX(desc_len, strlen(DESCRIPTION));                                             \
    }
#define ALIAS_LENGTH(NAME, GROUP, DESCRIPTION, MAN_PAGE, NOTE)                                     \
    {                                                                                              \
        name_len = MAX(name_len, strlen(#NAME));                                                   \
        desc_len = MAX(desc_len, strlen(DESCRIPTION));                                             \
    }
    CMD_BUILTIN_FOREACH(BUILTIN_LENGTH, ALIAS_LENGTH)
#undef ALIAS_LENGTH
#undef BUILTIN_LENGTH

    int prev_group = -1;
#define PRINT_HELP_ALIAS(NAME, GROUP, DESCRIPTION, MAN_PAGE, NOTE)                                 \
    {                                                                                              \
        if (prev_group != GROUP) {                                                                 \
            if (prev_group != -1)                                                                  \
                printf("\n");                                                                      \
            printf("\033[1m%s Commands\033[0m\n", cmd_builtin_group_names[GROUP]);                 \
        }                                                                                          \
        printf("  \033[96m%-*s \033[0m%-*s\033[0m\033[35m[alias]\n\033[0m", name_len + 3, #NAME,   \
               desc_len + 3, DESCRIPTION);                                                         \
        prev_group = GROUP;                                                                        \
    }
#define PRINT_HELP_BUILTIN(NAME, GROUP, FN, DESCRIPTION, MAN_PAGE, NOTE)                           \
    {                                                                                              \
        if (prev_group != GROUP) {                                                                 \
            if (prev_group != -1)                                                                  \
                printf("\n");                                                                      \
            printf("\033[1m%s Commands\033[0m\n", cmd_builtin_group_names[GROUP]);                 \
        }                                                                                          \
        printf("  \033[96m%-*s \033[0m%-*s\033[0m\033[35m[builtin]\n\033[0m", name_len + 3, #NAME, \
               desc_len + 3, DESCRIPTION);                                                         \
        prev_group = GROUP;                                                                        \
    }

    CMD_BUILTIN_FOREACH(PRINT_HELP_BUILTIN, PRINT_HELP_ALIAS)
#undef PRINT_HELP_ALIAS
#undef PRINT_HELP_BUILTIN

    return EXIT_SUCCESS;
}

static int _cmd_builtin_exit(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    static const char *usage = "exit [status_code]";
    if (cmd->argc > 1) {
        _cmd_incorrect_usage(usage);
        return EXIT_FAILURE;
    }
    int code = EXIT_SUCCESS;
    if (cmd->argc == 1) {
        errval_t err = _cmd_parse_int(cmd->argv[0], &code);
        if (err_is_fail(err) || code < 0 || code > 255) {
            _cmd_incorrect_usage(usage);
            return EXIT_FAILURE;
        }
    }
    exit(code);
    return EXIT_SUCCESS;  // XXX we should never reach this point
}

static int _cmd_builtin_clear(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    (void)cmd;
    // XXX we ignore any additional arguments here intentionally
    tty_clear_screen();
    return EXIT_SUCCESS;
}

static int _cmd_builtin_reboot(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    (void)cmd;
    // XXX we ignore any additional arguments here intentionally
    errval_t err = sys_reboot();
    DEBUG_ERR(err, "sys_reboot failed");
    return EXIT_FAILURE;  // XXX if we reach this point, something has failed...
}

#define TEST_SUITE_ENABLE_IF(TEST)                                                                 \
    if (strcmp(#TEST, cmd->argv[pos]) == 0) {                                                      \
        if (TEST_SUITE_CONFIG_IS_TEST_ENABLED(config, TEST)) {                                     \
            _cmd_incorrect_usage(usage);                                                           \
            return EXIT_FAILURE;                                                                   \
        }                                                                                          \
        TEST_SUITE_CONFIG_ENABLE_TEST(config, TEST);                                               \
        found = true;                                                                              \
    }

static int _cmd_builtin_test(struct shell_session *session, struct parsed_command *cmd)
{
    (void)session;
    static const char *usage = "test [-q] [-v] [-c] [-a] [[TESTS_TO_RUN]]";
    // command has to start with flags
    size_t index = 0;
    bool   quick = false, verbose = false, continue_on_err = false, all = false;
    while (index < cmd->argc) {
        if (strlen(cmd->argv[index]) != 2 || cmd->argv[index][0] != '-')
            break;
        char flag = cmd->argv[index][1];
        if (flag == 'q') {
            if (quick) {
                _cmd_incorrect_usage(usage);
                return EXIT_FAILURE;
            }
            quick = true;
        } else if (flag == 'v') {
            if (verbose) {
                _cmd_incorrect_usage(usage);
                return EXIT_FAILURE;
            }
            verbose = true;
        } else if (flag == 'c') {
            if (continue_on_err) {
                _cmd_incorrect_usage(usage);
                return EXIT_FAILURE;
            }
            continue_on_err = true;
        } else if (flag == 'a') {
            if (all) {
                _cmd_incorrect_usage(usage);
                return EXIT_FAILURE;
            }
            all = true;
        }
        ++index;
    }

    if (all) {
        if (index != cmd->argc) {
            _cmd_incorrect_usage(usage);
            return EXIT_FAILURE;
        }
        struct test_suite_config config = { .quick           = quick,
                                            .verbose         = verbose,
                                            .continue_on_err = continue_on_err,
                                            .tests           = TEST_SUITE_ALL_TESTS };
        errval_t                 err = aos_rpc_test_suite_run(aos_rpc_get_init_channel(), config);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "test_suite_run");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    struct test_suite_config config = {
        .quick           = quick,
        .verbose         = verbose,
        .continue_on_err = continue_on_err,
        .tests           = TEST_SUITE_NO_TESTS,
    };

    for (size_t pos = index; pos < cmd->argc; ++pos) {
        bool found = false;
        TEST_SUITE_FOREACH(TEST_SUITE_ENABLE_IF)
        if (!found) {
            _cmd_incorrect_usage(usage);
            return EXIT_FAILURE;
        }
    }

    errval_t err = aos_rpc_test_suite_run(aos_rpc_get_init_channel(), config);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "test_suite_run");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#undef TEST_SUITE_ENABLE_IF

static struct cmd_builtin *_cmd_builtin_create(cmd_builtin_fn fn, char *help, char *usage,
                                               char *description, bool alias)
{
    struct cmd_builtin *builtin = malloc(sizeof(struct cmd_builtin));
    builtin->fn                 = fn;
    builtin->help               = help;
    builtin->alias              = alias;
    builtin->usage              = usage;
    builtin->description        = description;
    return builtin;
}

static void _cmd_register_builtin(struct trie *cmds, char *name, cmd_builtin_fn fn, char *help,
                                  char *usage, char *description, bool alias)
{
    struct cmd_builtin *builtin = _cmd_builtin_create(fn, help, usage, description, alias);
    trie_insert(cmds, name, builtin);
}

static int _cmd_wrap_alias(struct shell_session *session, struct parsed_command *cmd,
                           bool background, struct capref *frames, domainid_t *pid, bool unwrap_run)
{
    errval_t     err = SYS_ERR_OK;
    int          argc;
    const char **argv;

    struct cmd_builtin *builtin = trie_lookup(&session->cmds, cmd->command);
    bool                alias   = builtin != NULL && builtin->alias;

    bool is_run_cmd    = strcmp(cmd->command, "run") == 0;
    bool is_oncore_cmd = strcmp(cmd->command, "oncore") == 0;

    int core = disp_get_current_core_id();
    if (unwrap_run && (is_run_cmd || is_oncore_cmd)) {
        if ((is_run_cmd && cmd->argc == 0) || (is_oncore_cmd && cmd->argc < 2)) {
            _cmd_unexpected_num_args(is_run_cmd ? "run" : "oncore", cmd->argc, is_run_cmd ? 1 : 2);
            return EXIT_FAILURE;
        }
        if (strcmp(cmd->argv[cmd->argc - 1], "&") == 0) {
            printf("%s%s: attempting to run in the background as part of a list of "
                   "commands.\nIgnoring background directive and aborting.%s\n",
                   TTY_COLOR_BOLD_YELLOW, is_run_cmd ? "run" : "oncore", cmd->argv[0],
                   TTY_COLOR_RESET);
            assert(false);
        }
        if (is_oncore_cmd) {
            err = _cmd_parse_int(cmd->argv[0], &core);
            if (err_is_fail(err)) {
                printf("%soncore: invalid core_id `%s`%s\n", TTY_COLOR_BOLD_RED, cmd->argv[0],
                       TTY_COLOR_RESET);
                return EXIT_FAILURE;
            }
            argv = ((const char **)cmd->argv) + 1;
            argc = cmd->argc - 1;
        } else {
            argv = (const char **)cmd->argv;
            argc = cmd->argc;
        }
    } else {
        argc    = cmd->argc + 1 + (alias ? 2 : 0);
        argv    = malloc(sizeof(char *) * (argc));
        argv[0] = cmd->command;
        for (size_t i = 0; i < cmd->argc; ++i) {
            argv[i + 1] = cmd->argv[i];
        }
        if (alias) {
            argv[cmd->argc + 1] = "--wd";
            argv[cmd->argc + 2] = session_wd(session);
        }
    }
    int status = EXIT_SUCCESS;
    err        = _cmd_builtin_dispatch_run(argc, argv, core, pid, background, &status, frames);
    if (!is_run_cmd && !is_oncore_cmd)
        free(argv);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "_cmd_builtin_dispatch_run failed.");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int _cmd_alias_fn(struct shell_session *session, struct parsed_command *cmd)
{
    domainid_t pid;
    return _cmd_wrap_alias(session, cmd, /*background=*/false, NULL, &pid, /*unwrap_run*/ false);
}

static void _cmd_register_alias(struct trie *cmds, char *name, char *help, char *usage,
                                char *description)
{
    _cmd_register_builtin(cmds, name, _cmd_alias_fn, help, usage, description, /*alias*/ true);
}

void cmd_register_builtins(struct shell_session *session)
{
    _cmd_session_set_pid(session, disp_get_domain_id());
#define REGISTER_BUILTIN(NAME, GROUP, FN, HELP, USAGE, DESCRIPTION)                                \
    _cmd_register_builtin(&session->cmds, #NAME, FN, HELP, USAGE, DESCRIPTION, /*alias*/ false);
#define REGISTER_ALIAS(NAME, GROUP, HELP, USAGE, DESCRIPTION)                                      \
    _cmd_register_alias(&session->cmds, #NAME, HELP, USAGE, DESCRIPTION);

    CMD_BUILTIN_FOREACH(REGISTER_BUILTIN, REGISTER_ALIAS)
#undef REGISTER_ALIAS
#undef REGISTER_BUILTIN
}

int cmd_dispatch_command(struct shell_session *session, struct parsed_command *cmd)
{
    struct cmd_builtin *builtin = trie_lookup(&session->cmds, cmd->command);
    if (builtin == NULL) {
        printf("%scommand not found: %s%s\n", TTY_COLOR_BOLD_RED, cmd->command, TTY_COLOR_RESET);
        return EXIT_FAILURE;
    }
    _cmd_session_set_pid(session, disp_get_domain_id());
    int   exit_code = builtin->fn(session, cmd);
    char *exit_var  = malloc(sizeof(char) * 4);
    snprintf(exit_var, 4, "%d", (char)exit_code);
    trie_insert(&session->vars, "?", exit_var);

    return exit_code;
}

static int _cmd_dispatch_pipeline(struct shell_session *session, struct parsed_command_pipeline *pl,
                                  int exit, char op, size_t begin, size_t end)
{
    if (op != ';' && exit != -1) {
        if ((op == CMD_OPERATOR_LOR && exit == EXIT_SUCCESS)
            || (op == CMD_OPERATOR_LAND && exit != EXIT_SUCCESS)) {
            return exit;  ///< we short-circuit here
        }
    }
    size_t span = end - begin;
    if (span == 1) {
        return cmd_dispatch_command(session, &(pl->cmds[begin]));
    }
    errval_t err = SYS_ERR_OK;

    struct capref *io_frames = malloc(sizeof(struct capref) * (span + 1));
    io_frames[0] = io_frames[span] = NULL_CAP;
    err                            = _cmd_alloc_frames(&io_frames[1], span - 1);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "_cmd_alloc_frames");
        return EXIT_FAILURE;
    }

    domainid_t *pids = malloc(sizeof(domainid_t) * span);
    (void)pids;
    int status = EXIT_SUCCESS;
    // XXX for now we only support pipes (|) and >
    for (size_t i = begin; i < end; ++i) {
        char pipe = (i == begin) ? '-' : pl->ops[i - 1];
        assert(pipe == '-' || pipe == '|' || pipe == '>');
        struct capref frames[] = { io_frames[i - begin], io_frames[i - begin + 1] };
        if (pipe == '-' || pipe == '|') {
            err = _cmd_wrap_alias(session, &pl->cmds[i], /*background=*/true, frames,
                                  &pids[i - begin],
                                  /*unwrap_run*/ true);
        } else if (pipe == '>') {
            if (pl->cmds[i].argc != 0) {
                printf("%sshell: ignoring additional argument(s) passed as part of file "
                       "redirection (`%s`).%s\n",
                       TTY_COLOR_BOLD_RED, pl->cmds[i].command, TTY_COLOR_RESET);
            }
            if (strlen(pl->cmds[i].command) == 0) {
                printf("%sshell (warning): redirecting output to a file with an empty filename "
                       "(``).%s\n",
                       TTY_COLOR_BOLD_RED, TTY_COLOR_RESET);
            }
            const char  *filename = pl->cmds[i].command;
            int          argc     = 4 + (i + 1 == end ? 1 : 0);
            const char **argv     = malloc(sizeof(char *) * argc);
            argv[0]               = "tee";
            argv[1]               = filename;
            argv[2]               = "--wd";
            argv[3]               = session_wd(session);
            // XXX "command > out.txt" is "translated" into command | tee out.txt --wd <...>
            if (i + 1 == end) {
                // XXX "command > out.txt" is "translated" into command | tee out.txt --wd <..> -s
                argv[4] = "-s";
            }
            err = _cmd_builtin_dispatch_run(argc, argv, disp_get_current_core_id(), &pids[i - begin],
                                            /*background*/ true, &status, frames);
            free(argv);
        }
        _cmd_session_set_pid(session, pids[i - begin]);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "proc_mgmt_spawn_with_caps failed.");
            return EXIT_FAILURE;
        }
    }

    for (size_t i = 0; i < span; ++i) {
        err = proc_mgmt_wait(pids[i], &status);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "proc_mgmt_wait failed");
            return EXIT_FAILURE;
        }
    }

    assert(capref_is_null(io_frames[0]) && capref_is_null(io_frames[span]));
    for (size_t i = 1; i < span; ++i) {
        err = cap_destroy(io_frames[i]);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "cap_destroy");
            return EXIT_FAILURE;
        }
    }
    return status;
}

typedef struct {
    uint16_t ns, us, ms, s, min, h;
    uint32_t days;  ///< just to make sure we can represent any uint64_t timediff
} timedelta_t;

static void ns_to_timedelta(uint64_t timediff, timedelta_t *res)
{
    res->ns = timediff % 1000;
    timediff /= 1000;
    res->us = timediff % 1000;
    timediff /= 1000;
    res->ms = timediff % 1000;
    timediff /= 1000;
    res->s = timediff % 60;
    timediff /= 60;
    res->min = timediff % 60;
    timediff /= 60;
    res->h = timediff % 24;
    timediff /= 24;
    res->days = timediff;
}

static void printf_timedelta(timedelta_t delta)
{
    if (delta.days > 0) {
        printf("%ud:%02uh", delta.days, delta.h);
        return;
    }
    if (delta.h > 0) {
        printf("%uh:%um", delta.h, delta.min);
        return;
    }
    // XXX we intentionally never prints us/ns
    printf("%um:%u.%03us", delta.min, delta.s, delta.ms);
}

int cmd_dispatch_commands(struct shell_session *session, struct parsed_command_pipeline *pl)
{
    assert(pl->size > 0);
#ifdef CMD_BUILTIN_PRINT_PARSED
    if (pl->size > 1) {
        printf("[Parsed %zu Command(s)]\n", pl->size);
        for (size_t i = 0; i < pl->size; ++i) {
            char op = (i == 0) ? '-' : pl->ops[i - 1];
            printf("  %c Command(%d) = %s\n", op, i + 1, pl->cmds[i].command);
            for (size_t j = 0; j < pl->cmds[i].argc; ++j) {
                printf("     <argv[%d]='%s'>\n", j, pl->cmds[i].argv[j]);
            }
        }
    }
#endif

    uint64_t before, after;
    // time must be before any other command, we check for this case here.
    char *command = NULL;
    if (strcmp(pl->cmds[0].command, "time") == 0) {
        if (pl->cmds[0].argc == 0) {
            _cmd_incorrect_usage("time [command]");
            return EXIT_FAILURE;
        }
        before              = systime_to_ns(get_system_time());
        command             = pl->cmds[0].command;
        pl->cmds[0].command = pl->cmds[0].argv[0];
        ++pl->cmds[0].argv;
        --pl->cmds[0].argc;
    }

    size_t prev      = 0;
    char   curr_op   = '\0';
    int    prev_exit = -1;
    for (size_t i = 1; i < pl->size; ++i) {
        char op = pl->ops[i - 1];
        if (op == CMD_OPERATOR_LAND || op == CMD_OPERATOR_LOR || op == ';') {
            prev_exit = _cmd_dispatch_pipeline(session, pl, prev_exit, curr_op, prev, i);
            curr_op   = op;
            prev      = i;
        }
    }
    int status = _cmd_dispatch_pipeline(session, pl, prev_exit, curr_op, prev, pl->size);

    if (command != NULL) {
        ++pl->cmds[0].argc;
        --pl->cmds[0].argv;
        pl->cmds[0].command = command;

        after = systime_to_ns(get_system_time());
        timedelta_t delta;
        ns_to_timedelta(after - before, &delta);

        printf("\033[33mtook: ");
        printf_timedelta(delta);
        printf("\033[0m (real)\n");
    }

    return status;
}