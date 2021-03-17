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
#define MAX_DIFF		300
#define NUM_OF_RUNS		5

static void
show_bw_info(unsigned long *bw_imc, unsigned long *bw_resc, int span)
{
	unsigned long avg_bw_imc = 0, avg_bw_resc = 0;
	unsigned long sum_bw_imc = 0, sum_bw_resc = 0;
	long avg_diff = 0;
	int runs;

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
	avg_diff = avg_bw_resc - avg_bw_imc;

	printf("%sok MBM: diff within %d%%\n",
	       labs(avg_diff) > MAX_DIFF ? "not " : "", MAX_DIFF);
	tests_run++;
	printf("# avg_diff: %lu\n", labs(avg_diff));
	printf("# Span (MB): %d\n", span);
	printf("# avg_bw_imc: %lu\n", avg_bw_imc);
	printf("# avg_bw_resc: %lu\n", avg_bw_resc);
}

static int check_results(int span)
{
	unsigned long bw_imc[NUM_OF_RUNS], bw_resc[NUM_OF_RUNS];
	char temp[1024], *token_array[8];
	char output[] = RESULT_FILE_NAME;
	int runs;
	FILE *fp;

	printf("# Checking for pass/fail\n");

	fp = fopen(output, "r");
	if (!fp) {
		perror(output);

		return errno;
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

	show_bw_info(bw_imc, bw_resc, span);

	fclose(fp);

	return 0;
}

static int mbm_setup(int num, ...)
{
	struct resctrl_val_param *p;
	static int num_of_runs;
	va_list param;
	int ret = 0;

	/* Run NUM_OF_RUNS times */
	if (num_of_runs++ >= NUM_OF_RUNS)
		return -1;

	va_start(param, num);
	p = va_arg(param, struct resctrl_val_param *);
	va_end(param);

	/* Set up shemata with 100% allocation on the first run. */
	if (num_of_runs == 0)
		ret = write_schemata(p->ctrlgrp, "100", p->cpu_no,
				     p->resctrl_val);

	return ret;
}

void mbm_test_cleanup(void)
{
	remove(RESULT_FILE_NAME);
}

int mbm_bw_change(int span, int cpu_no, char *bw_report, char **benchmark_cmd)
{
	struct resctrl_val_param param = {
		.resctrl_val	= MBM_STR,
		.ctrlgrp	= "c1",
		.mongrp		= "m1",
		.span		= span,
		.cpu_no		= cpu_no,
		.mum_resctrlfs	= 1,
		.filename	= RESULT_FILE_NAME,
		.bw_report	=  bw_report,
		.setup		= mbm_setup
	};
	int ret;

	remove(RESULT_FILE_NAME);

	if (!validate_resctrl_feature_request("mbm"))
		return -1;

	ret = resctrl_val(benchmark_cmd, &param);
	if (ret)
		return ret;

	ret = check_results(span);
	if (ret)
		return ret;

	mbm_test_cleanup();

	return 0;
}
