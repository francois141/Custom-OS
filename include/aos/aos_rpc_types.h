#ifndef AOS_RPC_TYPES_H
#define AOS_RPC_TYPES_H

#include <aos/aos.h>
#include <fs/fs.h>

struct aos_generic_rpc_request {
    enum {
        AOS_RPC_REQUEST_TYPE_GENERIC_NUMBER,
        AOS_RPC_REQUEST_TYPE_GENERIC_STRING,
        AOS_RPC_REQUEST_TYPE_SETUP_CHANNEL,
        AOS_RPC_REQUEST_TYPE_MEMSERVER,
        AOS_RPC_REQUEST_TYPE_TERMINAL,
        AOS_RPC_REQUEST_TYPE_TERMINAL_STR,
        AOS_RPC_REQUEST_TYPE_PROC_MGMT,
        AOS_RPC_REQUEST_TYPE_FILESYSTEM,
        AOS_RPC_REQUEST_TYPE_TEST_SUITE,
        AOS_RPC_REQUEST_TYPE_DISTCAP,
        AOS_RPC_REQUEST_TYPE_NETWORK,
    } type;
};

struct aos_generic_rpc_response {
    enum {
        AOS_RPC_RESPONSE_TYPE_NONE,  // just used to "hold" an err without any actual response data
        AOS_RPC_RESPONSE_TYPE_GENERIC_NUMBER,
        AOS_RPC_RESPONSE_TYPE_GENERIC_STRING,
        AOS_RPC_RESPONSE_TYPE_MEMSERVER,
        AOS_RPC_RESPONSE_TYPE_TERMINAL,
        AOS_RPC_RESPONSE_TYPE_TERMINAL_STR,
        AOS_RPC_RESPONSE_TYPE_PROC_MGMT,
        AOS_RPC_RESPONSE_TYPE_TEST_SUITE,
        AOS_RPC_RESPONSE_TYPE_DISTCAP,
        AOS_RPC_RESPONSE_TYPE_NETWORK
    } type;
    errval_t err;
};

struct aos_generic_number_rpc_request {
    struct aos_generic_rpc_request base;
    uintptr_t                      val;  ///< the number to send
};

struct aos_generic_number_rpc_response {
    struct aos_generic_rpc_response base;
};

// when sending this request bytes containing the string are packed after this "header".
struct aos_generic_string_rpc_request {
    struct aos_generic_rpc_request base;
    size_t                         size;  ///< size of the string.
};

struct aos_generic_string_rpc_response {
    struct aos_generic_rpc_response base;
};

struct aos_memserver_rpc_request {
    struct aos_generic_rpc_request base;
    size_t                         size;
    size_t                         alignment;
};

struct aos_memserver_rpc_response {
    struct aos_generic_rpc_response base;
    size_t                          retbytes;
};

struct aos_terminal_rpc_request {
    struct aos_generic_rpc_request base;
    enum { AOS_TERMINAL_RPC_REQUEST_TYPE_PUTCHAR, AOS_TERMINAL_RPC_REQUEST_TYPE_GETCHAR } ttype;
    union {
        struct {
            char c;  ///< the character to send.
        } putchar;
        struct {
        } getchar;
    } u;
};

// when sending this request bytes containing the string are packed after this "header".
struct aos_terminal_str_rpc_request {
    struct aos_generic_rpc_request base;
    enum { AOS_TERMINAL_STR_RPC_REQUEST_TYPE_PUTSTR, AOS_TERMINAL_STR_RPC_REQUEST_TYPE_GETSTR } ttype;
    size_t                         size;  ///< size of the string
    char buf[0];
};

struct aos_terminal_str_rpc_response {
    struct aos_generic_rpc_response base;
    enum { AOS_TERMINAL_STR_RPC_RESPONSE_TYPE_PUTSTR, AOS_TERMINAL_STR_RPC_RESPONSE_TYPE_GETSTR } ttype;
    size_t                         size;  ///< size of the string
    char buf[0];
};

struct aos_terminal_rpc_response {
    struct aos_generic_rpc_response base;
    enum { AOS_TERMINAL_RPC_RESPONSE_TYPE_PUTCHAR, AOS_TERMINAL_RPC_RESPONSE_TYPE_GETCHAR } ttype;
    union {
        struct {
        } putchar;
        struct {
            char c;  ///< the received character
        } getchar;
    } u;
};

struct aos_proc_mgmt_rpc_request {
    struct aos_generic_rpc_request base;
    enum {
        AOS_RPC_PROC_MGMT_REQUEST_SPAWN_CMDLINE,
        AOS_RPC_PROC_MGMT_REQUEST_SPAWN_DEFAULT,
        AOS_RPC_PROC_MGMT_REQUEST_ALL_PIDS,
        AOS_RPC_PROC_MGMT_REQUEST_STATUS,
        AOS_RPC_PROC_MGMT_REQUEST_NAME,
        AOS_RPC_PROC_MGMT_REQUEST_PID,
        AOS_RPC_PROC_MGMT_REQUEST_PAUSE,
        AOS_RPC_PROC_MGMT_REQUEST_RESUME,
        AOS_RPC_PROC_MGMT_REQUEST_WAIT,
        AOS_RPC_PROC_MGMT_REQUEST_EXIT,
        AOS_RPC_PROC_MGMT_REQUEST_KILL,
        AOS_RPC_PROC_MGMT_REQUEST_KILLALL
    } proc_type;
    // a core id of -1 means it concerns all cores
    coreid_t core;
};

// used for everything except the spawn and exit command
struct aos_proc_mgmt_rpc_basic_request {
    struct aos_proc_mgmt_rpc_request base;
    domainid_t                       pid;
    char                             name[0];
};

struct aos_proc_mgmt_rpc_exit_request {
    struct aos_proc_mgmt_rpc_request base;
    domainid_t                       pid;
    int                              exit_code;
};

struct aos_proc_mgmt_rpc_spawn_request {
    struct aos_proc_mgmt_rpc_request base;
    // only used for cmdline spawn
    int capc;
    // is the path for the default spawn request
    char cmdline[0];
};

struct aos_proc_mgmt_rpc_response {
    struct aos_generic_rpc_response base;
    domainid_t                      pid;
    char                            name[0];
};

struct aos_proc_mgmt_rpc_all_pid_response {
    struct aos_generic_rpc_response base;
    size_t                          num;
    domainid_t                      pids[0];
};

struct aos_proc_mgmt_rpc_status_response {
    struct aos_generic_rpc_response base;
    struct proc_status              status;
};

struct aos_proc_mgmt_rpc_wait_response {
    struct aos_generic_rpc_response base;
    int                             exit_code;
};

struct aos_filesystem_request {
    struct aos_generic_rpc_request base;
    enum {
        AOS_RPC_FILESYSTEM_OPEN,
        AOS_RPC_FILESYSTEM_READ,
        AOS_RPC_FILESYSTEM_WRITE,
        AOS_RPC_FILESYSTEM_SEEK,
        AOS_RPC_FILESYSTEM_TELL,
        AOS_RPC_FILESYSTEM_CLOSE,
        AOS_RPC_FILESYSTEM_DIR_OPEN,
        AOS_RPC_FILESYSTEM_DIR_NEXT,
        AOS_RPC_FILESYSTEM_DIR_CLOSE,
        AOS_RPC_FILESYSTEM_MKDIR,
        AOS_RPC_FILESYSTEM_RMDIR,
        AOS_RPC_FILESYSTEM_MKFILE,
        AOS_RPC_FILESYSTEM_RMFILE,
        AOS_RPC_FILESYSTEM_IS_DIRECTORY,
        AOS_RPC_FILESYSTEM_STAT,
    } request_type;
};

struct aos_filesystem_rpc_open_request {
    struct aos_filesystem_request base;
    int flags;
    char path[512];
};

struct aos_filesystem_rpc_read_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    size_t len;
};

struct aos_filesystem_rpc_write_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    size_t len;
    char buffer[512];
};

struct aos_filesystem_rpc_seek_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    int whence;
    off_t offset;
};

struct aos_filesystem_rpc_tell_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
};

struct aos_filesystem_rpc_close_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
};

struct aos_filesystem_rpc_dir_open_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    char path[512];
};

struct aos_filesystem_rpc_dir_next_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
};

struct aos_filesystem_rpc_dir_close_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
};

struct aos_filesystem_rpc_mkdir_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    char path[512];
};

struct aos_filesystem_rpc_rmdir_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    char path[512];
};

struct aos_filesystem_rpc_mkfile_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    char path[512];
};

struct aos_filesystem_rpc_rmfile_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    char path[512];
};

struct aos_filesystem_rpc_is_directory_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
    char path[512];
};

struct aos_filesystem_rpc_stat_request {
    struct aos_filesystem_request base;
    struct fat32_handle* fat32_handle_addr;
};

struct aos_filesystem_response {
    struct aos_generic_rpc_response base;
};

struct aos_filesystem_rpc_open_response {
    struct aos_filesystem_response base;
    struct fat32_handle* fat32_handle_addr;
};

struct aos_filesystem_rpc_read_response {
    struct aos_filesystem_response base;
    struct fat32_handle* fat32_handle_addr;
    size_t len;
    char buffer[512];
};

struct aos_filesystem_rpc_write_response {
    struct aos_filesystem_response base;
    struct fat32_handle* fat32_handle_addr;
    size_t bytes_written;
};

struct aos_filesystem_rpc_seek_response {
    struct aos_filesystem_response base;
    struct fat32_handle* fat32_handle_addr;
};

struct aos_filesystem_rpc_tell_response {
    struct aos_filesystem_response base;
    size_t position;
};

struct aos_filesystem_rpc_close_response {
    struct aos_filesystem_response base;
};

struct aos_filesystem_rpc_dir_open_response {
    struct aos_filesystem_response base;
    struct fat32_handle* fat32_handle_addr;
};

struct aos_filesystem_rpc_dir_next_response {
    struct aos_filesystem_response base;
    struct fat32_handle* fat32_handle_addr;
    char name[512];
};

struct aos_filesystem_rpc_dir_close_response {
    struct aos_filesystem_response base;
};

struct aos_filesystem_rpc_mkdir_response {
    struct aos_filesystem_response base;
};

struct aos_filesystem_rpc_rmdir_response {
    struct aos_filesystem_response base;
};

struct aos_filesystem_rpc_is_directory_response {
    struct aos_filesystem_response base;
    bool is_directory;
};

struct aos_filesystem_rpc_mkfile_response {
    struct aos_filesystem_response base;
    struct fat32_handle *fat32_handle_addr;
};

struct aos_filesystem_rpc_rmfile_response {
    struct aos_filesystem_response base;
};

struct aos_filesystem_rpc_stat_response {
    struct aos_filesystem_response base;
    struct fs_fileinfo file_info;
};

struct aos_network_basic_request {
    struct aos_generic_rpc_request base;
    enum {
        AOS_RPC_NETWORK_REQUEST_INIT,
        AOS_RPC_NETWORK_REQUEST_RECEIVE,
        AOS_RPC_NETWORK_REQUEST_PING,
        AOS_RPC_NETWORK_REQUEST_SEND,
        AOS_RPC_NETWORK_LISTEN,
        AOS_RPC_NETWORK_SET_IO,
    } type;
};

struct aos_network_request_init {
    struct aos_network_basic_request base;
    uint8_t mac[6];
};

struct aos_network_packet_request {
    struct aos_network_basic_request base;
    size_t packet_size;
    uint8_t packet[0];
};

struct aos_network_ping_request {
    struct aos_network_basic_request base;
    uint32_t ip;
};

struct aos_network_send_request {
    struct aos_network_basic_request base;
    domainid_t pid;
    bool is_tcp;
    uint32_t target_ip;
    uint16_t target_port;
    uint16_t host_port;
    uint16_t data_size;
    char data[0];
};

struct aos_network_listen_request {
    struct aos_network_basic_request base;
    uint16_t port;
    domainid_t pid;
    bool is_tcp;
};

struct aos_network_setio_request {
    struct aos_network_basic_request base;
    bool is_network;
    bool is_tcp;
    uint32_t ip;
    uint16_t src_port;
    uint16_t dst_port;
};

struct aos_network_basic_response {
    struct aos_generic_rpc_response base;
    enum {
        AOS_RPC_NETWORK_RESPONSE_PING
    } type;
};

struct aos_network_ping_response {
    struct aos_network_basic_response base;
    uint32_t ping_ms;
};

#define TEST_SUITE_FOREACH(TEST)                                                                   \
    TEST(ram_alloc)                                                                                \
    TEST(malloc)                                                                                   \
    TEST(stress_malloc)                                                                            \
    TEST(frame_alloc)                                                                              \
    TEST(frame_page_fault_handler)                                                                 \
    TEST(frame_page_fault_handler_no_write)                                                        \
    TEST(frame_map_huge_frame)                                                                     \
    TEST(stress_frame_alloc)                                                                       \
    TEST(stress_frame_alloc_arbitrary_sizes)                                                       \
    TEST(stress_frame_alloc_arbitrary_sizes_cyclic)                                                \
    TEST(stress_frame_alloc_small_alloc_sizes)                                                     \
    TEST(stress_frame_alloc_with_pagefault_handler)                                                \
    TEST(concurrent_paging)                                                                        \
    TEST(proc_spawn)                                                                               \
    TEST(stress_proc_mgmt)

#define TEST_SUITE_GENERATE_FN(TEST)   errval_t test_##TEST(bool quick, bool verbose);
#define TEST_SUITE_DEFINE_FN(TEST)     errval_t test_##TEST(bool quick, bool verbose)
#define TEST_SUITE_GENERATE_ENUM(TEST) test_type_##TEST,

enum test_suite_test_type {
    TEST_SUITE_FOREACH(TEST_SUITE_GENERATE_ENUM)
    test_suite_test_type_count  /// < used to count the number of tests defined
};

typedef int32_t test_suite_tests_field_t[(test_suite_test_type_count + 32 - 1) / 32];

#define TEST_SUITE_ALL_TESTS {[0 ... (((test_suite_test_type_count + 32 - 1) / 32)  - 1)] = -1}
#define TEST_SUITE_NO_TESTS {0}
#define TEST_SUITE_CONFIG_ENABLE_TEST(tc, TEST) \
    (tc).tests[(test_type_##TEST) / 32] |= 1 << ((test_type_##TEST) % 32);
#define TEST_SUITE_CONFIG_IS_TEST_ENABLED(tc, TEST) \
    (((tc).tests[(test_type_##TEST) / 32] & (1 << ((test_type_##TEST) % 32) )) != 0)

struct test_suite_config {
    test_suite_tests_field_t tests;  ///< bit field indicating which tests to run
    bool quick;                      ///< if set runs the tests in quick mode, i.e., with smaller constants.
    bool verbose;                    ///< enables additional printing during testing.
    bool continue_on_err;            ///< if set continues after a single test failed.
};

struct aos_test_suite_rpc_request {
    struct aos_generic_rpc_request base;
    struct test_suite_config config;
};

struct aos_test_suite_rpc_response {
    struct aos_generic_rpc_response base;
};

struct aos_distcap_base_request {
    struct aos_generic_rpc_request base;
    enum {
        AOS_RPC_DISTCAP_DELETE,
        AOS_RPC_DISTCAP_REVOKE,
        AOS_RPC_DISTCAP_RETYPE,
        AOS_RPC_DISTCAP_DELETE_SYNC,
        AOS_RPC_DISTCAP_REVOKE_SYNC,
        AOS_RPC_DISTCAP_RETYPE_SYNC,
    } type;
};

struct aos_distcap_delete_request {
    struct aos_distcap_base_request base;
    capaddr_t src;
    uint8_t level;
};

struct aos_distcap_revoke_request {
    struct aos_distcap_base_request base;
    capaddr_t src;
    uint8_t level;
};

struct aos_distcap_retype_request {
    struct aos_distcap_base_request base;
    capaddr_t src;
    gensize_t offset;
    enum objtype new_type;
    gensize_t objsize;
    size_t count;
    capaddr_t to;
    capaddr_t slot;
    int to_level;
};

#endif 
