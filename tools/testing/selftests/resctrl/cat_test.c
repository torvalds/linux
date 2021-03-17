// SPDX-License-Identifier: GPL-2.0
/*
 * Cache Allocation Technology (CAT) test
 *
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *    Sai Praneeth Prakhya <sai.praneeth.prakhya@intel.com>,
 *    Fenghua Yu <fenghua.yu@intel.com>
 */
#include "resctrl.h"
#include <unistd.h>

#define RESULT_FILE_NAME1	"result_cat1"
#define RESULT_FILE_NAME2	"result_cat2"
#define NUM_OF_RUNS		5
#define MAX_DIFF_PERCENT	4
#define MAX_DIFF		1000000

static int count_of_bits;
static char cbm_mask[256];
static unsigned long long_mask;
static unsigned long cache_size;

/*
 * Change schemata. Write schemata to specified
 * con_mon grp, mon_grp in resctrl FS.
 * Run 5 times in order to get average values.
 */
static int cat_setup(int num, ...)
{
	struct resctrl_val_param *p;
	char schemata[64];
	va_list param;
	int ret = 0;

	va_start(param, num);
	p = va_arg(param, struct resctrl_val_param *);
	va_end(param);

	/* Run NUM_OF_RUNS times */
	if (p->num_of_runs >= NUM_OF_RUNS)
		return -1;

	if (p->num_of_runs == 0) {
		sprintf(schemata, "%lx", p->mask);
		ret = write_schemata(p->ctrlgrp, schemata, p->cpu_no,
				     p->resctrl_val);
	}
	p->num_of_runs++;

	return ret;
}

static int check_results(struct resctrl_val_param *param)
{
	char *token_array[8], temp[512];
	unsigned long sum_llc_perf_miss = 0;
	int runs = 0, no_of_bits = 0;
	FILE *fp;

	ksft_print_msg("Checking for pass/fail\n");
	fp = fopen(param->filename, "r");
	if (!fp) {
		perror("# Cannot open file");

		return errno;
	}

	while (fgets(temp, sizeof(temp), fp)) {
		char *token = strtok(temp, ":\t");
		int fields = 0;

		while (token) {
			token_array[fields++] = token;
			token = strtok(NULL, ":\t");
		}
		/*
		 * Discard the first value which is inaccurate due to monitoring
		 * setup transition phase.
		 */
		if (runs > 0)
			sum_llc_perf_miss += strtoul(token_array[3], NULL, 0);
		runs++;
	}

	fclose(fp);
	no_of_bits = count_bits(param->mask);

	return show_cache_info(sum_llc_perf_miss, no_of_bits, param->span / 64,
			       MAX_DIFF, MAX_DIFF_PERCENT, NUM_OF_RUNS,
			       !is_amd, false);
}

void cat_test_cleanup(void)
{
	remove(RESULT_FILE_NAME1);
	remove(RESULT_FILE_NAME2);
}

int cat_perf_miss_val(int cpu_no, int n, char *cache_type)
{
	unsigned long l_mask, l_mask_1;
	int ret, pipefd[2], sibling_cpu_no;
	char pipe_message;
	pid_t bm_pid;

	cache_size = 0;

	ret = remount_resctrlfs(true);
	if (ret)
		return ret;

	if (!validate_resctrl_feature_request("cat"))
		return -1;

	/* Get default cbm mask for L3/L2 cache */
	ret = get_cbm_mask(cache_type, cbm_mask);
	if (ret)
		return ret;

	long_mask = strtoul(cbm_mask, NULL, 16);

	/* Get L3/L2 cache size */
	ret = get_cache_size(cpu_no, cache_type, &cache_size);
	if (ret)
		return ret;
	ksft_print_msg("Cache size :%lu\n", cache_size);

	/* Get max number of bits from default-cabm mask */
	count_of_bits = count_bits(long_mask);

	if (!n)
		n = count_of_bits / 2;

	if (n > count_of_bits - 1) {
		ksft_print_msg("Invalid input value for no_of_bits n!\n");
		ksft_print_msg("Please enter value in range 1 to %d\n",
			       count_of_bits - 1);
		return -1;
	}

	/* Get core id from same socket for running another thread */
	sibling_cpu_no = get_core_sibling(cpu_no);
	if (sibling_cpu_no < 0)
		return -1;

	struct resctrl_val_param param = {
		.resctrl_val	= CAT_STR,
		.cpu_no		= cpu_no,
		.mum_resctrlfs	= 0,
		.setup		= cat_setup,
	};

	l_mask = long_mask >> n;
	l_mask_1 = ~l_mask & long_mask;

	/* Set param values for parent thread which will be allocated bitmask
	 * with (max_bits - n) bits
	 */
	param.span = cache_size * (count_of_bits - n) / count_of_bits;
	strcpy(param.ctrlgrp, "c2");
	strcpy(param.mongrp, "m2");
	strcpy(param.filename, RESULT_FILE_NAME2);
	param.mask = l_mask;
	param.num_of_runs = 0;

	if (pipe(pipefd)) {
		perror("# Unable to create pipe");
		return errno;
	}

	bm_pid = fork();

	/* Set param values for child thread which will be allocated bitmask
	 * with n bits
	 */
	if (bm_pid == 0) {
		param.mask = l_mask_1;
		strcpy(param.ctrlgrp, "c1");
		strcpy(param.mongrp, "m1");
		param.span = cache_size * n / count_of_bits;
		strcpy(param.filename, RESULT_FILE_NAME1);
		param.num_of_runs = 0;
		param.cpu_no = sibling_cpu_no;
	}

	remove(param.filename);

	ret = cat_val(&param);
	if (ret)
		return ret;

	ret = check_results(&param);
	if (ret)
		return ret;

	if (bm_pid == 0) {
		/* Tell parent that child is ready */
		close(pipefd[0]);
		pipe_message = 1;
		if (write(pipefd[1], &pipe_message, sizeof(pipe_message)) <
		    sizeof(pipe_message)) {
			close(pipefd[1]);
			perror("# failed signaling parent process");
			return errno;
		}

		close(pipefd[1]);
		while (1)
			;
	} else {
		/* Parent waits for child to be ready. */
		close(pipefd[1]);
		pipe_message = 0;
		while (pipe_message != 1) {
			if (read(pipefd[0], &pipe_message,
				 sizeof(pipe_message)) < sizeof(pipe_message)) {
				perror("# failed reading from child process");
				break;
			}
		}
		close(pipefd[0]);
		kill(bm_pid, SIGKILL);
	}

	cat_test_cleanup();
	if (bm_pid)
		umount_resctrlfs();

	return 0;
}
