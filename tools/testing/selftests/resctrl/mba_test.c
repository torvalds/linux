// SPDX-License-Identifier: GPL-2.0
/*
 * Memory Bandwidth Allocation (MBA) test
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include "resctrl.h"

#define RESULT_FILE_NAME	"result_mba"
#define NUM_OF_RUNS		5
#define MAX_DIFF_PERCENT	8
#define ALLOCATION_MAX		100
#define ALLOCATION_MIN		10
#define ALLOCATION_STEP		10

static int mba_init(const struct resctrl_val_param *param, int domain_id)
{
	int ret;

	ret = initialize_read_mem_bw_imc();
	if (ret)
		return ret;

	initialize_mem_bw_resctrl(param, domain_id);

	return 0;
}

/*
 * Change schemata percentage from 100 to 10%. Write schemata to specified
 * con_mon grp, mon_grp in resctrl FS.
 * For each allocation, run 5 times in order to get average values.
 */
static int mba_setup(const struct resctrl_test *test,
		     const struct user_params *uparams,
		     struct resctrl_val_param *p)
{
	static unsigned int allocation = ALLOCATION_MIN;
	static int runs_per_allocation;
	char allocation_str[64];
	int ret;

	if (runs_per_allocation >= NUM_OF_RUNS)
		runs_per_allocation = 0;

	/* Only set up schemata once every NUM_OF_RUNS of allocations */
	if (runs_per_allocation++ != 0)
		return 0;

	if (allocation > ALLOCATION_MAX)
		return END_OF_TESTS;

	sprintf(allocation_str, "%d", allocation);

	ret = write_schemata(p->ctrlgrp, allocation_str, uparams->cpu, test->resource);
	if (ret < 0)
		return ret;

	allocation += ALLOCATION_STEP;

	return 0;
}

static int mba_measure(const struct user_params *uparams,
		       struct resctrl_val_param *param, pid_t bm_pid)
{
	return measure_read_mem_bw(uparams, param, bm_pid);
}

static bool show_mba_info(unsigned long *bw_imc, unsigned long *bw_resc)
{
	unsigned int allocation;
	bool ret = false;
	int runs;

	ksft_print_msg("Results are displayed in (MB)\n");
	/* Memory bandwidth from 100% down to 10% */
	for (allocation = 0; allocation < ALLOCATION_MAX / ALLOCATION_STEP;
	     allocation++) {
		unsigned long sum_bw_imc = 0, sum_bw_resc = 0;
		long avg_bw_imc, avg_bw_resc;
		int avg_diff_per;
		float avg_diff;

		for (runs = NUM_OF_RUNS * allocation;
		     runs < NUM_OF_RUNS * allocation + NUM_OF_RUNS ; runs++) {
			sum_bw_imc += bw_imc[runs];
			sum_bw_resc += bw_resc[runs];
		}

		avg_bw_imc = sum_bw_imc / NUM_OF_RUNS;
		avg_bw_resc = sum_bw_resc / NUM_OF_RUNS;
		if (avg_bw_imc < THROTTLE_THRESHOLD || avg_bw_resc < THROTTLE_THRESHOLD) {
			ksft_print_msg("Bandwidth below threshold (%d MiB). Dropping results from MBA schemata %u.\n",
				       THROTTLE_THRESHOLD,
				       ALLOCATION_MIN + ALLOCATION_STEP * allocation);
			continue;
		}

		avg_diff = (float)labs(avg_bw_resc - avg_bw_imc) / avg_bw_imc;
		avg_diff_per = (int)(avg_diff * 100);

		ksft_print_msg("%s Check MBA diff within %d%% for schemata %u\n",
			       avg_diff_per > MAX_DIFF_PERCENT ?
			       "Fail:" : "Pass:",
			       MAX_DIFF_PERCENT,
			       ALLOCATION_MIN + ALLOCATION_STEP * allocation);

		ksft_print_msg("avg_diff_per: %d%%\n", avg_diff_per);
		ksft_print_msg("avg_bw_imc: %lu\n", avg_bw_imc);
		ksft_print_msg("avg_bw_resc: %lu\n", avg_bw_resc);
		if (avg_diff_per > MAX_DIFF_PERCENT)
			ret = true;
	}

	ksft_print_msg("%s Check schemata change using MBA\n",
		       ret ? "Fail:" : "Pass:");
	if (ret)
		ksft_print_msg("At least one test failed\n");

	return ret;
}

static int check_results(void)
{
	unsigned long bw_resc[NUM_OF_RUNS * ALLOCATION_MAX / ALLOCATION_STEP];
	unsigned long bw_imc[NUM_OF_RUNS * ALLOCATION_MAX / ALLOCATION_STEP];
	char *token_array[8], output[] = RESULT_FILE_NAME, temp[512];
	int runs;
	FILE *fp;

	fp = fopen(output, "r");
	if (!fp) {
		ksft_perror(output);

		return -1;
	}

	runs = 0;
	while (fgets(temp, sizeof(temp), fp)) {
		char *token = strtok(temp, ":\t");
		int fields = 0;

		while (token) {
			token_array[fields++] = token;
			token = strtok(NULL, ":\t");
		}

		/* Field 3 is perf imc value */
		bw_imc[runs] = strtoul(token_array[3], NULL, 0);
		/* Field 5 is resctrl value */
		bw_resc[runs] = strtoul(token_array[5], NULL, 0);
		runs++;
	}

	fclose(fp);

	return show_mba_info(bw_imc, bw_resc);
}

static void mba_test_cleanup(void)
{
	remove(RESULT_FILE_NAME);
}

static int mba_run_test(const struct resctrl_test *test, const struct user_params *uparams)
{
	struct resctrl_val_param param = {
		.ctrlgrp	= "c1",
		.filename	= RESULT_FILE_NAME,
		.init		= mba_init,
		.setup		= mba_setup,
		.measure	= mba_measure,
	};
	struct fill_buf_param fill_buf = {};
	int ret;

	remove(RESULT_FILE_NAME);

	if (uparams->fill_buf) {
		fill_buf.buf_size = uparams->fill_buf->buf_size;
		fill_buf.memflush = uparams->fill_buf->memflush;
		param.fill_buf = &fill_buf;
	} else if (!uparams->benchmark_cmd[0]) {
		ssize_t buf_size;

		buf_size = get_fill_buf_size(uparams->cpu, "L3");
		if (buf_size < 0)
			return buf_size;
		fill_buf.buf_size = buf_size;
		fill_buf.memflush = true;
		param.fill_buf = &fill_buf;
	}

	ret = resctrl_val(test, uparams, &param);
	if (ret)
		return ret;

	ret = check_results();
	if (ret && (get_vendor() == ARCH_INTEL) && !snc_kernel_support())
		ksft_print_msg("Kernel doesn't support Sub-NUMA Clustering but it is enabled on the system.\n");

	return ret;
}

static bool mba_feature_check(const struct resctrl_test *test)
{
	return test_resource_feature_check(test) &&
	       resctrl_mon_feature_exists("L3_MON", "mbm_local_bytes");
}

struct resctrl_test mba_test = {
	.name = "MBA",
	.resource = "MB",
	.vendor_specific = ARCH_INTEL,
	.feature_check = mba_feature_check,
	.run_test = mba_run_test,
	.cleanup = mba_test_cleanup,
};
