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

static int cmt_setup(struct resctrl_val_param *p)
{
	/* Run NUM_OF_RUNS times */
	if (p->num_of_runs >= NUM_OF_RUNS)
		return END_OF_TESTS;

	p->num_of_runs++;

	return 0;
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

		return errno;
	}

	while (fgets(temp, sizeof(temp), fp)) {
		char *token = strtok(temp, ":\t");
		int fields = 0;

		while (token) {
			token_array[fields++] = token;
			token = strtok(NULL, ":\t");
		}

		/* Field 3 is llc occ resc value */
		if (runs > 0)
			sum_llc_occu_resc += strtoul(token_array[3], NULL, 0);
		runs++;
	}
	fclose(fp);

	return show_cache_info(sum_llc_occu_resc, no_of_bits, span,
			       MAX_DIFF, MAX_DIFF_PERCENT, runs - 1,
			       true, true);
}

void cmt_test_cleanup(void)
{
	remove(RESULT_FILE_NAME);
}

int cmt_resctrl_val(int cpu_no, int n, const char * const *benchmark_cmd)
{
	const char * const *cmd = benchmark_cmd;
	const char *new_cmd[BENCHMARK_ARGS];
	unsigned long cache_size = 0;
	unsigned long long_mask;
	char *span_str = NULL;
	char cbm_mask[256];
	int count_of_bits;
	size_t span;
	int ret, i;

	ret = get_cbm_mask("L3", cbm_mask);
	if (ret)
		return ret;

	long_mask = strtoul(cbm_mask, NULL, 16);

	ret = get_cache_size(cpu_no, "L3", &cache_size);
	if (ret)
		return ret;
	ksft_print_msg("Cache size :%lu\n", cache_size);

	count_of_bits = count_bits(long_mask);

	if (n < 1 || n > count_of_bits) {
		ksft_print_msg("Invalid input value for numbr_of_bits n!\n");
		ksft_print_msg("Please enter value in range 1 to %d\n", count_of_bits);
		return -1;
	}

	struct resctrl_val_param param = {
		.resctrl_val	= CMT_STR,
		.ctrlgrp	= "c1",
		.mongrp		= "m1",
		.cpu_no		= cpu_no,
		.filename	= RESULT_FILE_NAME,
		.mask		= ~(long_mask << n) & long_mask,
		.num_of_runs	= 0,
		.setup		= cmt_setup,
	};

	span = cache_size * n / count_of_bits;

	if (strcmp(cmd[0], "fill_buf") == 0) {
		/* Duplicate the command to be able to replace span in it */
		for (i = 0; benchmark_cmd[i]; i++)
			new_cmd[i] = benchmark_cmd[i];
		new_cmd[i] = NULL;

		ret = asprintf(&span_str, "%zu", span);
		if (ret < 0)
			return -1;
		new_cmd[1] = span_str;
		cmd = new_cmd;
	}

	remove(RESULT_FILE_NAME);

	ret = resctrl_val(cmd, &param);
	if (ret)
		goto out;

	ret = check_results(&param, span, n);

out:
	cmt_test_cleanup();
	free(span_str);

	return ret;
}
