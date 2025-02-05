// SPDX-License-Identifier: GPL-2.0
/*
 * Cache Monitoring Technology (CMT) test
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include "resctrl.h"
#include <unistd.h>

#define RESULT_FILE_NAME	"result_cmt"
#define NUM_OF_RUNS		5
#define MAX_DIFF		2000000
#define MAX_DIFF_PERCENT	15

#define CON_MON_LCC_OCCUP_PATH		\
	"%s/%s/mon_data/mon_L3_%02d/llc_occupancy"

static int cmt_init(const struct resctrl_val_param *param, int domain_id)
{
	sprintf(llc_occup_path, CON_MON_LCC_OCCUP_PATH, RESCTRL_PATH,
		param->ctrlgrp, domain_id);

	return 0;
}

static int cmt_setup(const struct resctrl_test *test,
		     const struct user_params *uparams,
		     struct resctrl_val_param *p)
{
	/* Run NUM_OF_RUNS times */
	if (p->num_of_runs >= NUM_OF_RUNS)
		return END_OF_TESTS;

	p->num_of_runs++;

	return 0;
}

static int cmt_measure(const struct user_params *uparams,
		       struct resctrl_val_param *param, pid_t bm_pid)
{
	sleep(1);
	return measure_llc_resctrl(param->filename, bm_pid);
}

static int show_results_info(unsigned long sum_llc_val, int no_of_bits,
			     unsigned long cache_span, unsigned long max_diff,
			     unsigned long max_diff_percent, unsigned long num_of_runs,
			     bool platform)
{
	unsigned long avg_llc_val = 0;
	float diff_percent;
	long avg_diff = 0;
	int ret;

	avg_llc_val = sum_llc_val / num_of_runs;
	avg_diff = (long)(cache_span - avg_llc_val);
	diff_percent = ((float)cache_span - avg_llc_val) / cache_span * 100;

	ret = platform && abs((int)diff_percent) > max_diff_percent &&
	      labs(avg_diff) > max_diff;

	ksft_print_msg("%s Check cache miss rate within %lu%%\n",
		       ret ? "Fail:" : "Pass:", max_diff_percent);

	ksft_print_msg("Percent diff=%d\n", abs((int)diff_percent));

	show_cache_info(no_of_bits, avg_llc_val, cache_span, false);

	return ret;
}

static int check_results(struct resctrl_val_param *param, size_t span, int no_of_bits)
{
	char *token_array[8], temp[512];
	unsigned long sum_llc_occu_resc = 0;
	int runs = 0;
	FILE *fp;

	ksft_print_msg("Checking for pass/fail\n");
	fp = fopen(param->filename, "r");
	if (!fp) {
		ksft_perror("Error in opening file");

		return -1;
	}

	while (fgets(temp, sizeof(temp), fp)) {
		char *token = strtok(temp, ":\t");
		int fields = 0;

		while (token) {
			token_array[fields++] = token;
			token = strtok(NULL, ":\t");
		}

		/* Field 3 is llc occ resc value */
		sum_llc_occu_resc += strtoul(token_array[3], NULL, 0);
		runs++;
	}
	fclose(fp);

	return show_results_info(sum_llc_occu_resc, no_of_bits, span,
				 MAX_DIFF, MAX_DIFF_PERCENT, runs, true);
}

static void cmt_test_cleanup(void)
{
	remove(RESULT_FILE_NAME);
}

static int cmt_run_test(const struct resctrl_test *test, const struct user_params *uparams)
{
	struct fill_buf_param fill_buf = {};
	unsigned long cache_total_size = 0;
	int n = uparams->bits ? : 5;
	unsigned long long_mask;
	int count_of_bits;
	size_t span;
	int ret;

	ret = get_full_cbm("L3", &long_mask);
	if (ret)
		return ret;

	ret = get_cache_size(uparams->cpu, "L3", &cache_total_size);
	if (ret)
		return ret;
	ksft_print_msg("Cache size :%lu\n", cache_total_size);

	count_of_bits = count_bits(long_mask);

	if (n < 1 || n > count_of_bits) {
		ksft_print_msg("Invalid input value for numbr_of_bits n!\n");
		ksft_print_msg("Please enter value in range 1 to %d\n", count_of_bits);
		return -1;
	}

	struct resctrl_val_param param = {
		.ctrlgrp	= "c1",
		.filename	= RESULT_FILE_NAME,
		.mask		= ~(long_mask << n) & long_mask,
		.num_of_runs	= 0,
		.init		= cmt_init,
		.setup		= cmt_setup,
		.measure	= cmt_measure,
	};

	span = cache_portion_size(cache_total_size, param.mask, long_mask);

	if (uparams->fill_buf) {
		fill_buf.buf_size = span;
		fill_buf.memflush = uparams->fill_buf->memflush;
		param.fill_buf = &fill_buf;
	} else if (!uparams->benchmark_cmd[0]) {
		fill_buf.buf_size = span;
		fill_buf.memflush = true;
		param.fill_buf = &fill_buf;
	}

	remove(RESULT_FILE_NAME);

	ret = resctrl_val(test, uparams, &param);
	if (ret)
		return ret;

	ret = check_results(&param, span, n);
	if (ret && (get_vendor() == ARCH_INTEL) && !snc_kernel_support())
		ksft_print_msg("Kernel doesn't support Sub-NUMA Clustering but it is enabled on the system.\n");

	return ret;
}

static bool cmt_feature_check(const struct resctrl_test *test)
{
	return test_resource_feature_check(test) &&
	       resctrl_mon_feature_exists("L3_MON", "llc_occupancy");
}

struct resctrl_test cmt_test = {
	.name = "CMT",
	.resource = "L3",
	.feature_check = cmt_feature_check,
	.run_test = cmt_run_test,
	.cleanup = cmt_test_cleanup,
};
