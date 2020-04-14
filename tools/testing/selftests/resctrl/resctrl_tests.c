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

bool is_amd;

void detect_amd(void)
{
	FILE *inf = fopen("/proc/cpuinfo", "r");
	char *res;

	if (!inf)
		return;

	res = fgrep(inf, "vendor_id");

	if (res) {
		char *s = strchr(res, ':');

		is_amd = s && !strcmp(s, ": AuthenticAMD\n");
		free(res);
	}
	fclose(inf);
}

static void cmd_help(void)
{
	printf("usage: resctrl_tests [-h] [-b \"benchmark_cmd [options]\"] [-t test list] [-n no_of_bits]\n");
	printf("\t-b benchmark_cmd [options]: run specified benchmark for MBM, MBA and CQM");
	printf("\t default benchmark is builtin fill_buf\n");
	printf("\t-t test list: run tests specified in the test list, ");
	printf("e.g. -t mbm, mba, cqm, cat\n");
	printf("\t-n no_of_bits: run cache tests using specified no of bits in cache bit mask\n");
	printf("\t-p cpu_no: specify CPU number to run the test. 1 is default\n");
	printf("\t-h: help\n");
}

void tests_cleanup(void)
{
	mbm_test_cleanup();
	mba_test_cleanup();
	cqm_test_cleanup();
	cat_test_cleanup();
}

int main(int argc, char **argv)
{
	bool has_ben = false, mbm_test = true, mba_test = true, cqm_test = true;
	int res, c, cpu_no = 1, span = 250, argc_new = argc, i, no_of_bits = 5;
	char *benchmark_cmd[BENCHMARK_ARGS], bw_report[64], bm_type[64];
	char benchmark_cmd_area[BENCHMARK_ARGS][BENCHMARK_ARG_SIZE];
	int ben_ind, ben_count;
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

	while ((c = getopt(argc_new, argv, "ht:b:")) != -1) {
		char *token;

		switch (c) {
		case 't':
			token = strtok(optarg, ",");

			mbm_test = false;
			mba_test = false;
			cqm_test = false;
			cat_test = false;
			while (token) {
				if (!strcmp(token, "mbm")) {
					mbm_test = true;
				} else if (!strcmp(token, "mba")) {
					mba_test = true;
				} else if (!strcmp(token, "cqm")) {
					cqm_test = true;
				} else if (!strcmp(token, "cat")) {
					cat_test = true;
				} else {
					printf("invalid argument\n");

					return -1;
				}
				token = strtok(NULL, ":\t");
			}
			break;
		case 'p':
			cpu_no = atoi(optarg);
			break;
		case 'n':
			no_of_bits = atoi(optarg);
			break;
		case 'h':
			cmd_help();

			return 0;
		default:
			printf("invalid argument\n");

			return -1;
		}
	}

	printf("TAP version 13\n");

	/*
	 * Typically we need root privileges, because:
	 * 1. We write to resctrl FS
	 * 2. We execute perf commands
	 */
	if (geteuid() != 0)
		printf("# WARNING: not running as root, tests may fail.\n");

	/* Detect AMD vendor */
	detect_amd();

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

	check_resctrlfs_support();
	filter_dmesg();

	if (!is_amd && mbm_test) {
		printf("# Starting MBM BW change ...\n");
		if (!has_ben)
			sprintf(benchmark_cmd[5], "%s", "mba");
		res = mbm_bw_change(span, cpu_no, bw_report, benchmark_cmd);
		printf("%sok MBM: bw change\n", res ? "not " : "");
		mbm_test_cleanup();
		tests_run++;
	}

	if (!is_amd && mba_test) {
		printf("# Starting MBA Schemata change ...\n");
		if (!has_ben)
			sprintf(benchmark_cmd[1], "%d", span);
		res = mba_schemata_change(cpu_no, bw_report, benchmark_cmd);
		printf("%sok MBA: schemata change\n", res ? "not " : "");
		mba_test_cleanup();
		tests_run++;
	}

	if (cqm_test) {
		printf("# Starting CQM test ...\n");
		if (!has_ben)
			sprintf(benchmark_cmd[5], "%s", "cqm");
		res = cqm_resctrl_val(cpu_no, no_of_bits, benchmark_cmd);
		printf("%sok CQM: test\n", res ? "not " : "");
		cqm_test_cleanup();
		tests_run++;
	}

	if (cat_test) {
		printf("# Starting CAT test ...\n");
		res = cat_perf_miss_val(cpu_no, no_of_bits, "L3");
		printf("%sok CAT: test\n", res ? "not " : "");
		tests_run++;
		cat_test_cleanup();
	}

	printf("1..%d\n", tests_run);

	return 0;
}
