#include <stdlib.h>
#include <stdio.h>

#include <aos/debug.h>
#include <aos/deferred.h>
#include <aos/aos_rpc.h>

#define NUM_WRITE_ITERATIONS 1
#define NUM_READ_ITERATIONS  1

int main(int argc, char *argv[])
{
    if (argc == 1) {
        domainid_t pid = disp_get_domain_id();
        for (int i = 0; i < NUM_WRITE_ITERATIONS; ++i) {
            printf("Hello! from serial_tester (pid=%u)\n", pid);
        }
        printf("Starting to read in serial_tester (pid=%u)\n", pid);
        for (int i = 0; i < NUM_READ_ITERATIONS; ++i) {
            char string[128];
            scanf("%s", string);
            printf("got string: \"%s\" in serial_tester (pid=%u)\n", string, pid);
        }
        printf("serial_tester (pid=%u) done.\n", pid);
        return EXIT_SUCCESS;
    } else if (argc == 2) {
        char *endptr;
        int procs = strtol(argv[1], &endptr, 10);
        if (endptr == NULL || *endptr != '\0') {
            printf("Illegal usage: serial_tester [num_procs]\n");
            return EXIT_FAILURE;
        }
        errval_t err = SYS_ERR_OK;
        domainid_t *pids = malloc(sizeof(domainid_t) * procs);
        for (int i = 0; i < procs; ++i) {
            err = proc_mgmt_spawn_program_argv(1, (const char **) argv, /*core=*/ 0, &pids[i]);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "proc_mgmt_spawn_program_argv failed");
                return EXIT_FAILURE;
            }
        }
        int status = EXIT_SUCCESS;
        for (int i = 0; i < procs; ++i) {
            err = proc_mgmt_wait(pids[i], &status);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "proc_mgmt_wait failed");
                return EXIT_FAILURE;
            }
        }
        return EXIT_SUCCESS;
    } else {
        printf("Illegal usage: serial_tester [num_procs]\n");
        return EXIT_FAILURE;
    }
}