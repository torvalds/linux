// SPDX-License-Identifier: GPL-2.0
/*
 * Memory Bandwidth Monitoring (MBM) test
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include "resctrl.h"

#define RESULT_FILE_NAME	"result_mbm"
#define MAX_DIFF_PERCENT	8
#define NUM_OF_RUNS		5

static int
show_bw_info(unsigned long *bw_imc, unsigned long *bw_resc, size_t span)
{
	unsigned long sum_bw_imc = 0, sum_bw_resc = 0;
	long avg_bw_imc = 0, avg_bw_resc = 0;
	int runs, ret, avg_diff_per;
	float avg_diff = 0;

	/*
	 * Discard the first value which is inaccurate due to monitoring setup
	 * transition phase.
	 */
	for (runs = 1; runs < NUM_OF_RUNS ; runs++) {
		sum_bw_imc += bw_imc[runs];
		sum_bw_resc += bw_resc[runs];
	}

	avg_bw_imc = sum_bw_imc / 4;
	avg_bw_resc = sum_bw_resc / 4;
	avg_diff = (float)labs(avg_bw_resc - avg_bw_imc) / avg_bw_imc;
	avg_diff_per = (int)(avg_diff * 100);

	ret = avg_diff_per > MAX_DIFF_PERCENT;
	ksft_print_msg("%s Check MBM diff within %d%%\n",
		       ret ? "Fail:" : "Pass:", MAX_DIFF_PERCENT);
	ksft_print_msg("avg_diff_per: %d%%\n", avg_diff_per);
	ksft_print_msg("Span (MB): %zu\n", span / MB);
	ksft_print_msg("avg_bw_imc: %lu\n", avg_bw_imc);
	ksft_print_msg("avg_bw_resc: %lu\n", avg_bw_resc);

	return ret;
}

static int check_results(size_t span)
{
	unsigned long bw_imc[NUM_OF_RUNS], bw_resc[NUM_OF_RUNS];
	char temp[1024], *token_array[8];
	char output[] = RESULT_FILE_NAME;
	int runs, ret;
	FILE *fp;

	ksft_print_msg("Checking for pass/fail\n");

	fp = fopen(output, "r");
	if (!fp) {
		ksft_perror(output);

		return -1;
	}

	runs = 0;
	while (fgets(temp, sizeof(temp), fp)) {
		char *token = strtok(temp, ":\t");
		int i = 0;

		while (token) {
			token_array[i++] = token;
			token = strtok(NULL, ":\t");
		}

		bw_resc[runs] = strtoul(token_array[5], NULL, 0);
		bw_imc[runs] = strtoul(token_array[3], NULL, 0);
		runs++;
	}

	ret = show_bw_info(bw_imc, bw_resc, span);

	fclose(fp);

	return ret;
}

static int mbm_setup(const struct resctrl_test *test,
		     const struct user_params *uparams,
		     struct resctrl_val_param *p)
{
	int ret = 0;

	/* Run NUM_OF_RUNS times */
	if (p->num_of_runs >= NUM_OF_RUNS)
		return END_OF_TESTS;

	/* Set up shemata with 100% allocation on the first run. */
	if (p->num_of_runs == 0 && resctrl_resource_exists("MB"))
		ret = write_schemata(p->ctrlgrp, "100", uparams->cpu, test->resource);

	p->num_of_runs++;

	return ret;
}

static void mbm_test_cleanup(void)
{
	remove(RESULT_FILE_NAME);
}

static int mbm_run_test(const struct resctrl_test *test, const struct user_params *uparams)
{
	struct resctrl_val_param param = {
		.resctrl_val	= MBM_STR,
		.ctrlgrp	= "c1",
		.mongrp		= "m1",
		.filename	= RESULT_FILE_NAME,
		.bw_report	= "reads",
		.setup		= mbm_setup
	};
	int ret;

	remove(RESULT_FILE_NAME);

	ret = resctrl_val(test, uparams, uparams->benchmark_cmd, &param);
	if (ret)
		return ret;

	ret = check_results(DEFAULT_SPAN);
	if (ret && (get_vendor() == ARCH_INTEL))
		ksft_print_msg("Intel MBM may be inaccurate when Sub-NUMA Clustering is enabled. Check BIOS configuration.\n");

	return ret;
}

static bool mbm_feature_check(const struct resctrl_test *test)
{
	return resctrl_mon_feature_exists("L3_MON", "mbm_total_bytes") &&
	       resctrl_mon_feature_exists("L3_MON", "mbm_local_bytes");
}

struct resctrl_test mbm_test = {
	.name = "MBM",
	.resource = "MB",
	.vendor_specific = ARCH_INTEL,
	.feature_check = mbm_feature_check,
	.run_test = mbm_run_test,
	.cleanup = mbm_test_cleanup,
};
