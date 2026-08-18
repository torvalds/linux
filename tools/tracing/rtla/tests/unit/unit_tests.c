// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <check.h>
#include <stdbool.h>

#include "../../src/utils.h"
#include "../../src/cli.h"

Suite *utils_suite(void);
Suite *actions_suite(void);
Suite *osnoise_top_cli_suite(void);
Suite *osnoise_hist_cli_suite(void);
Suite *timerlat_top_cli_suite(void);
Suite *timerlat_hist_cli_suite(void);
Suite *cli_opt_callback_suite(void);

int main(int argc, char *argv[])
{
	int num_failed;
	SRunner *sr;

	in_unit_test = true;

	sr = srunner_create(utils_suite());
	srunner_add_suite(sr, cli_opt_callback_suite());
	srunner_add_suite(sr, actions_suite());
	srunner_add_suite(sr, osnoise_top_cli_suite());
	srunner_add_suite(sr, osnoise_hist_cli_suite());
	srunner_add_suite(sr, timerlat_top_cli_suite());
	srunner_add_suite(sr, timerlat_hist_cli_suite());

	srunner_run_all(sr, CK_VERBOSE);
	num_failed = srunner_ntests_failed(sr);

	srunner_free(sr);

	return (num_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
