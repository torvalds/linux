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

#define BENCHMARK_ARGS		64
#define BENCHMARK_ARG_SIZE	64

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
	printf("usage: resctrl_tests [-h] [-b \"benchmark_cmd [options]\"] [-t test list] [-n no_of_bits]\n");
	printf("\t-b benchmark_cmd [options]: run specified benchmark for MBM, MBA and CMT\n");
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

static void run_mbm_test(bool has_ben, char **benchmark_cmd, int span,
			 int cpu_no, char *bw_report)
{
	int res;

	ksft_print_msg("Starting MBM BW change ...\n");

	if (!validate_resctrl_feature_request(MBM_STR)) {
		ksft_test_result_skip("Hardware does not support MBM or MBM is disabled\n");
		return;
	}

	if (!has_ben)
		sprintf(benchmark_cmd[5], "%s", MBA_STR);
	res = mbm_bw_change(span, cpu_no, bw_report, benchmark_cmd);
	ksft_test_result(!res, "MBM: bw change\n");
	mbm_test_cleanup();
}

static void run_mba_test(bool has_ben, char **benchmark_cmd, int span,
			 int cpu_no, char *bw_report)
{
	int res;

	ksft_print_msg("Starting MBA Schemata change ...\n");

	if (!validate_resctrl_feature_request(MBA_STR)) {
		ksft_test_result_skip("Hardware does not support MBA or MBA is disabled\n");
		return;
	}

	if (!has_ben)
		sprintf(benchmark_cmd[1], "%d", span);
	res = mba_schemata_change(cpu_no, bw_report, benchmark_cmd);
	ksft_test_result(!res, "MBA: schemata change\n");
	mba_test_cleanup();
}

static void run_cmt_test(bool has_ben, char **benchmark_cmd, int cpu_no)
{
	int res;

	ksft_print_msg("Starting CMT test ...\n");
	if (!validate_resctrl_feature_request(CMT_STR)) {
		ksft_test_result_skip("Hardware does not support CMT or CMT is disabled\n");
		return;
	}

	if (!has_ben)
		sprintf(benchmark_cmd[5], "%s", CMT_STR);
	res = cmt_resctrl_val(cpu_no, 5, benchmark_cmd);
	ksft_test_result(!res, "CMT: test\n");
	cmt_test_cleanup();
}

static void run_cat_test(int cpu_no, int no_of_bits)
{
	int res;

	ksft_print_msg("Starting CAT test ...\n");

	if (!validate_resctrl_feature_request(CAT_STR)) {
		ksft_test_result_skip("Hardware does not support CAT or CAT is disabled\n");
		return;
	}

	res = cat_perf_miss_val(cpu_no, no_of_bits, "L3");
	ksft_test_result(!res, "CAT: test\n");
	cat_test_cleanup();
}

int main(int argc, char **argv)
{
	bool has_ben = false, mbm_test = true, mba_test = true, cmt_test = true;
	int c, cpu_no = 1, span = 250, argc_new = argc, i, no_of_bits = 0;
	char *benchmark_cmd[BENCHMARK_ARGS], bw_report[64], bm_type[64];
	char benchmark_cmd_area[BENCHMARK_ARGS][BENCHMARK_ARG_SIZE];
	int ben_ind, ben_count, tests = 0;
	bool cat_test = true;

	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-b") == 0) {
			ben_ind = i + 1;
			ben_count = argc - ben_ind;
			argc_new = ben_ind - 1;
			has_ben = true;
			break;
		}
	}

	while ((c = getopt(argc_new, argv, "ht:b:n:p:")) != -1) {
		char *token;

		switch (c) {
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

	ksft_print_header();

	/*
	 * Typically we need root privileges, because:
	 * 1. We write to resctrl FS
	 * 2. We execute perf commands
	 */
	if (geteuid() != 0)
		return ksft_exit_fail_msg("Not running as root, abort testing.\n");

	if (has_ben) {
		/* Extract benchmark command from command line. */
		for (i = ben_ind; i < argc; i++) {
			benchmark_cmd[i - ben_ind] = benchmark_cmd_area[i];
			sprintf(benchmark_cmd[i - ben_ind], "%s", argv[i]);
		}
		benchmark_cmd[ben_count] = NULL;
	} else {
		/* If no benchmark is given by "-b" argument, use fill_buf. */
		for (i = 0; i < 6; i++)
			benchmark_cmd[i] = benchmark_cmd_area[i];

		strcpy(benchmark_cmd[0], "fill_buf");
		sprintf(benchmark_cmd[1], "%d", span);
		strcpy(benchmark_cmd[2], "1");
		strcpy(benchmark_cmd[3], "1");
		strcpy(benchmark_cmd[4], "0");
		strcpy(benchmark_cmd[5], "");
		benchmark_cmd[6] = NULL;
	}

	sprintf(bw_report, "reads");
	sprintf(bm_type, "fill_buf");

	if (!check_resctrlfs_support())
		return ksft_exit_fail_msg("resctrl FS does not exist\n");

	filter_dmesg();

	ksft_set_plan(tests ? : 4);

	if ((get_vendor() == ARCH_INTEL) && mbm_test)
		run_mbm_test(has_ben, benchmark_cmd, span, cpu_no, bw_report);

	if ((get_vendor() == ARCH_INTEL) && mba_test)
		run_mba_test(has_ben, benchmark_cmd, span, cpu_no, bw_report);

	if (cmt_test)
		run_cmt_test(has_ben, benchmark_cmd, cpu_no);

	if (cat_test)
		run_cat_test(cpu_no, no_of_bits);

	umount_resctrlfs();

	return ksft_exit_pass();
}
