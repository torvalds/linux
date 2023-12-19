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
	printf("usage: resctrl_tests [-h] [-t test list] [-n no_of_bits] [-b benchmark_cmd [option]...]\n");
	printf("\t-b benchmark_cmd [option]...: run specified benchmark for MBM, MBA and CMT\n");
	printf("\t   default benchmark is builtin fill_buf\n");
	printf("\t-t test list: run tests specified in the test list, ");
	printf("e.g. -t mbm,mba,cmt,cat\n");
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

static void run_mbm_test(const char * const *benchmark_cmd, int cpu_no)
{
	int res;

	ksft_print_msg("Starting MBM BW change ...\n");

	if (test_prepare()) {
		ksft_exit_fail_msg("Abnormal failure when preparing for the test\n");
		return;
	}

	if (!validate_resctrl_feature_request("L3_MON", "mbm_total_bytes") ||
	    !validate_resctrl_feature_request("L3_MON", "mbm_local_bytes") ||
	    (get_vendor() != ARCH_INTEL)) {
		ksft_test_result_skip("Hardware does not support MBM or MBM is disabled\n");
		goto cleanup;
	}

	res = mbm_bw_change(cpu_no, benchmark_cmd);
	ksft_test_result(!res, "MBM: bw change\n");
	if ((get_vendor() == ARCH_INTEL) && res)
		ksft_print_msg("Intel MBM may be inaccurate when Sub-NUMA Clustering is enabled. Check BIOS configuration.\n");

cleanup:
	test_cleanup();
}

static void run_mba_test(const char * const *benchmark_cmd, int cpu_no)
{
	int res;

	ksft_print_msg("Starting MBA Schemata change ...\n");

	if (test_prepare()) {
		ksft_exit_fail_msg("Abnormal failure when preparing for the test\n");
		return;
	}

	if (!validate_resctrl_feature_request("MB", NULL) ||
	    !validate_resctrl_feature_request("L3_MON", "mbm_local_bytes") ||
	    (get_vendor() != ARCH_INTEL)) {
		ksft_test_result_skip("Hardware does not support MBA or MBA is disabled\n");
		goto cleanup;
	}

	res = mba_schemata_change(cpu_no, benchmark_cmd);
	ksft_test_result(!res, "MBA: schemata change\n");

cleanup:
	test_cleanup();
}

static void run_cmt_test(const char * const *benchmark_cmd, int cpu_no)
{
	int res;

	ksft_print_msg("Starting CMT test ...\n");

	if (test_prepare()) {
		ksft_exit_fail_msg("Abnormal failure when preparing for the test\n");
		return;
	}

	if (!validate_resctrl_feature_request("L3_MON", "llc_occupancy") ||
	    !validate_resctrl_feature_request("L3", NULL)) {
		ksft_test_result_skip("Hardware does not support CMT or CMT is disabled\n");
		goto cleanup;
	}

	res = cmt_resctrl_val(cpu_no, 5, benchmark_cmd);
	ksft_test_result(!res, "CMT: test\n");
	if ((get_vendor() == ARCH_INTEL) && res)
		ksft_print_msg("Intel CMT may be inaccurate when Sub-NUMA Clustering is enabled. Check BIOS configuration.\n");

cleanup:
	test_cleanup();
}

static void run_cat_test(int cpu_no, int no_of_bits)
{
	int res;

	ksft_print_msg("Starting CAT test ...\n");

	if (test_prepare()) {
		ksft_exit_fail_msg("Abnormal failure when preparing for the test\n");
		return;
	}

	if (!validate_resctrl_feature_request("L3", NULL)) {
		ksft_test_result_skip("Hardware does not support CAT or CAT is disabled\n");
		goto cleanup;
	}

	res = cat_perf_miss_val(cpu_no, no_of_bits, "L3");
	ksft_test_result(!res, "CAT: test\n");

cleanup:
	test_cleanup();
}

int main(int argc, char **argv)
{
	bool mbm_test = true, mba_test = true, cmt_test = true;
	const char *benchmark_cmd[BENCHMARK_ARGS] = {};
	int c, cpu_no = 1, i, no_of_bits = 0;
	char *span_str = NULL;
	bool cat_test = true;
	int tests = 0;
	int ret;

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
				benchmark_cmd[i] = argv[i + optind];
			benchmark_cmd[i] = NULL;

			goto last_arg;
		case 't':
			token = strtok(optarg, ",");

			mbm_test = false;
			mba_test = false;
			cmt_test = false;
			cat_test = false;
			while (token) {
				if (!strncmp(token, MBM_STR, sizeof(MBM_STR))) {
					mbm_test = true;
					tests++;
				} else if (!strncmp(token, MBA_STR, sizeof(MBA_STR))) {
					mba_test = true;
					tests++;
				} else if (!strncmp(token, CMT_STR, sizeof(CMT_STR))) {
					cmt_test = true;
					tests++;
				} else if (!strncmp(token, CAT_STR, sizeof(CAT_STR))) {
					cat_test = true;
					tests++;
				} else {
					printf("invalid argument\n");

					return -1;
				}
				token = strtok(NULL, ",");
			}
			break;
		case 'p':
			cpu_no = atoi(optarg);
			break;
		case 'n':
			no_of_bits = atoi(optarg);
			if (no_of_bits <= 0) {
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

	if (!benchmark_cmd[0]) {
		/* If no benchmark is given by "-b" argument, use fill_buf. */
		benchmark_cmd[0] = "fill_buf";
		ret = asprintf(&span_str, "%u", DEFAULT_SPAN);
		if (ret < 0)
			ksft_exit_fail_msg("Out of memory!\n");
		benchmark_cmd[1] = span_str;
		benchmark_cmd[2] = "1";
		benchmark_cmd[3] = "0";
		benchmark_cmd[4] = "false";
		benchmark_cmd[5] = NULL;
	}

	ksft_set_plan(tests ? : 4);

	if (mbm_test)
		run_mbm_test(benchmark_cmd, cpu_no);

	if (mba_test)
		run_mba_test(benchmark_cmd, cpu_no);

	if (cmt_test)
		run_cmt_test(benchmark_cmd, cpu_no);

	if (cat_test)
		run_cat_test(cpu_no, no_of_bits);

	free(span_str);
	ksft_finished();
}
