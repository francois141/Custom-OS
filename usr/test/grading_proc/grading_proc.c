/**
* \file
* \brief Process running the grading tests
*/

#include <stdlib.h>
#include <stdio.h>

#include <aos/aos.h>
#include <aos/deferred.h>
#include <aos/aos_rpc.h>
#include <grading/grading.h>
#include <grading/state.h>
#include <grading/io.h>

int main(int argc, char *argv[])
{
    // This process just does the grading part, then returns
    grading_setup_noninit(&argc, &argv);

    grading_parse_arguments();
    grading_test_late();

   return EXIT_SUCCESS;
}
