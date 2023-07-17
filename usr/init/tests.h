#ifndef _INIT_TESTS_H_
#define _INIT_TESTS_H

#include <aos/aos_rpc.h>

TEST_SUITE_FOREACH(TEST_SUITE_GENERATE_FN)

errval_t test_suite_run(struct test_suite_config config);

#endif