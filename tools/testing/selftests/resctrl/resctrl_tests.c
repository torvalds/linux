// SPDX-License-Identifier: GPL-2.0
/*
 * Resctrl tests
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include "resctrl.h"

/* Volatile memory sink to prevent compiler optimizations */
static volatile int sink_target;
volatile int *value_sink = &sink_target;

static struct resctrl_test *resctrl_tests[] = {
	&mbm_test,
	&mba_test,
	&cmt_test,
	&l3_cat_test,
	&l3_noncont_cat_test,
	&l2_noncont_cat_test,
};

static int detect_vendor(void)
{
	FILE *inf = fopen("/proc/cpuinfo", "r");
	int vendor_id = 0;
	char *s = NULL;
	char *res;

	if (!inf)
		return vendor_id;

	res = fgrep(inf, "vendor_id");

	if (res)
		s = strchr(res, ':');

	if (s && !strcmp(s, ": GenuineIntel\n"))
		vendor_id = ARCH_INTEL;
	else if (s && !strcmp(s, ": AuthenticAMD\n"))
		vendor_id = ARCH_AMD;

	fclose(inf);
	free(res);
	return vendor_id;
}

int get_vendor(void)
{
	static int vendor = -1;

	if (vendor == -1)
		vendor = detect_vendor();
	if (vendor == 0)
		ksft_print_msg("Can not get vendor info...\n");

	return vendor;
}

static void cmd_help(void)
{
	int i;

	printf("usage: resctrl_tests [-h] [-t test list] [-n no_of_bits] [-b benchmark_cmd [option]...]\n");
	printf("\t-b benchmark_cmd [option]...: run specified benchmark for MBM, MBA and CMT\n");
	printf("\t   default benchmark is builtin fill_buf\n");
	printf("\t-t test list: run tests/groups specified by the list, ");
	printf("e.g. -t mbm,mba,cmt,cat\n");
	printf("\t\tSupported tests (group):\n");
	for (i = 0; i < ARRAY_SIZE(resctrl_tests); i++) {
		if (resctrl_tests[i]->group)
			printf("\t\t\t%s (%s)\n", resctrl_tests[i]->name, resctrl_tests[i]->group);
		else
			printf("\t\t\t%s\n", resctrl_tests[i]->name);
	}
	printf("\t-n no_of_bits: run cache tests using specified no of bits in cache bit mask\n");
	printf("\t-p cpu_no: specify CPU number to run the test. 1 is default\n");
	printf("\t-h: help\n");
}

void tests_cleanup(void)
{
	mbm_test_cleanup();
	mba_test_cleanup();
	cmt_test_cleanup();
	cat_test_cleanup();
}

static int test_prepare(void)
{
	int res;

	res = signal_handler_register();
	if (res) {
		ksft_print_msg("Failed to register signal handler\n");
		return res;
	}

	res = mount_resctrlfs();
	if (res) {
		signal_handler_unregister();
		ksft_print_msg("Failed to mount resctrl FS\n");
		return res;
	}
	return 0;
}

static void test_cleanup(void)
{
	umount_resctrlfs();
	signal_handler_unregister();
}

static bool test_vendor_specific_check(const struct resctrl_test *test)
{
	if (!test->vendor_specific)
		return true;

	return get_vendor() & test->vendor_specific;
}

static void run_single_test(const struct resctrl_test *test, const struct user_params *uparams)
{
	int ret;

	if (test->disabled)
		return;

	if (!test_vendor_specific_check(test)) {
		ksft_test_result_skip("Hardware does not support %s\n", test->name);
		return;
	}

	ksft_print_msg("Starting %s test ...\n", test->name);

	if (test_prepare()) {
		ksft_exit_fail_msg("Abnormal failure when preparing for the test\n");
		return;
	}

	if (!test->feature_check(test)) {
		ksft_test_result_skip("Hardware does not support %s or %s is disabled\n",
				      test->name, test->name);
		goto cleanup;
	}

	ret = test->run_test(test, uparams);
	ksft_test_result(!ret, "%s: test\n", test->name);

cleanup:
	test_cleanup();
}

static void init_user_params(struct user_params *uparams)
{
	memset(uparams, 0, sizeof(*uparams));

	uparams->cpu = 1;
	uparams->bits = 0;
}

int main(int argc, char **argv)
{
	int tests = ARRAY_SIZE(resctrl_tests);
	bool test_param_seen = false;
	struct user_params uparams;
	char *span_str = NULL;
	int ret, c, i;

	init_user_params(&uparams);

	while ((c = getopt(argc, argv, "ht:b:n:p:")) != -1) {
		char *token;

		switch (c) {
		case 'b':
			/*
			 * First move optind back to the (first) optarg and
			 * then build the benchmark command using the
			 * remaining arguments.
			 */
			optind--;
			if (argc - optind >= BENCHMARK_ARGS)
				ksft_exit_fail_msg("Too long benchmark command");

			/* Extract benchmark command from command line. */
			for (i = 0; i < argc - optind; i++)
				uparams.benchmark_cmd[i] = argv[i + optind];
			uparams.benchmark_cmd[i] = NULL;

			goto last_arg;
		case 't':
			token = strtok(optarg, ",");

			if (!test_param_seen) {
				for (i = 0; i < ARRAY_SIZE(resctrl_tests); i++)
					resctrl_tests[i]->disabled = true;
				tests = 0;
				test_param_seen = true;
			}
			while (token) {
				bool found = false;

				for (i = 0; i < ARRAY_SIZE(resctrl_tests); i++) {
					if (!strcasecmp(token, resctrl_tests[i]->name) ||
					    (resctrl_tests[i]->group &&
					     !strcasecmp(token, resctrl_tests[i]->group))) {
						if (resctrl_tests[i]->disabled)
							tests++;
						resctrl_tests[i]->disabled = false;
						found = true;
					}
				}

				if (!found) {
					printf("invalid test: %s\n", token);

					return -1;
				}
				token = strtok(NULL, ",");
			}
			break;
		case 'p':
			uparams.cpu = atoi(optarg);
			break;
		case 'n':
			uparams.bits = atoi(optarg);
			if (uparams.bits <= 0) {
				printf("Bail out! invalid argument for no_of_bits\n");
				return -1;
			}
			break;
		case 'h':
			cmd_help();

			return 0;
		default:
			printf("invalid argument\n");

			return -1;
		}
	}
last_arg:

	ksft_print_header();

	/*
	 * Typically we need root privileges, because:
	 * 1. We write to resctrl FS
	 * 2. We execute perf commands
	 */
	if (geteuid() != 0)
		return ksft_exit_skip("Not running as root. Skipping...\n");

	if (!check_resctrlfs_support())
		return ksft_exit_skip("resctrl FS does not exist. Enable X86_CPU_RESCTRL config option.\n");

	if (umount_resctrlfs())
		return ksft_exit_skip("resctrl FS unmount failed.\n");

	filter_dmesg();

	if (!uparams.benchmark_cmd[0]) {
		/* If no benchmark is given by "-b" argument, use fill_buf. */
		uparams.benchmark_cmd[0] = "fill_buf";
		ret = asprintf(&span_str, "%u", DEFAULT_SPAN);
		if (ret < 0)
			ksft_exit_fail_msg("Out of memory!\n");
		uparams.benchmark_cmd[1] = span_str;
		uparams.benchmark_cmd[2] = "1";
		uparams.benchmark_cmd[3] = "0";
		uparams.benchmark_cmd[4] = "false";
		uparams.benchmark_cmd[5] = NULL;
	}

	ksft_set_plan(tests);

	for (i = 0; i < ARRAY_SIZE(resctrl_tests); i++)
		run_single_test(resctrl_tests[i], &uparams);

	free(span_str);
	ksft_finished();
}
