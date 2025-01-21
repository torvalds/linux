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

#define RESULT_FILE_NAME	"result_cat"
#define NUM_OF_RUNS		5

/*
 * Minimum difference in LLC misses between a test with n+1 bits CBM to the
 * test with n bits is MIN_DIFF_PERCENT_PER_BIT * (n - 1). With e.g. 5 vs 4
 * bits in the CBM mask, the minimum difference must be at least
 * MIN_DIFF_PERCENT_PER_BIT * (4 - 1) = 3 percent.
 *
 * The relationship between number of used CBM bits and difference in LLC
 * misses is not expected to be linear. With a small number of bits, the
 * margin is smaller than with larger number of bits. For selftest purposes,
 * however, linear approach is enough because ultimately only pass/fail
 * decision has to be made and distinction between strong and stronger
 * signal is irrelevant.
 */
#define MIN_DIFF_PERCENT_PER_BIT	1UL

static int show_results_info(__u64 sum_llc_val, int no_of_bits,
			     unsigned long cache_span,
			     unsigned long min_diff_percent,
			     unsigned long num_of_runs, bool platform,
			     __s64 *prev_avg_llc_val)
{
	__u64 avg_llc_val = 0;
	float avg_diff;
	int ret = 0;

	avg_llc_val = sum_llc_val / num_of_runs;
	if (*prev_avg_llc_val) {
		float delta = (__s64)(avg_llc_val - *prev_avg_llc_val);

		avg_diff = delta / *prev_avg_llc_val;
		ret = platform && (avg_diff * 100) < (float)min_diff_percent;

		ksft_print_msg("%s Check cache miss rate changed more than %.1f%%\n",
			       ret ? "Fail:" : "Pass:", (float)min_diff_percent);

		ksft_print_msg("Percent diff=%.1f\n", avg_diff * 100);
	}
	*prev_avg_llc_val = avg_llc_val;

	show_cache_info(no_of_bits, avg_llc_val, cache_span, true);

	return ret;
}

/* Remove the highest bit from CBM */
static unsigned long next_mask(unsigned long current_mask)
{
	return current_mask & (current_mask >> 1);
}

static int check_results(struct resctrl_val_param *param, const char *cache_type,
			 unsigned long cache_total_size, unsigned long full_cache_mask,
			 unsigned long current_mask)
{
	char *token_array[8], temp[512];
	__u64 sum_llc_perf_miss = 0;
	__s64 prev_avg_llc_val = 0;
	unsigned long alloc_size;
	int runs = 0;
	int fail = 0;
	int ret;
	FILE *fp;

	ksft_print_msg("Checking for pass/fail\n");
	fp = fopen(param->filename, "r");
	if (!fp) {
		ksft_perror("Cannot open file");

		return -1;
	}

	while (fgets(temp, sizeof(temp), fp)) {
		char *token = strtok(temp, ":\t");
		int fields = 0;
		int bits;

		while (token) {
			token_array[fields++] = token;
			token = strtok(NULL, ":\t");
		}

		sum_llc_perf_miss += strtoull(token_array[3], NULL, 0);
		runs++;

		if (runs < NUM_OF_RUNS)
			continue;

		if (!current_mask) {
			ksft_print_msg("Unexpected empty cache mask\n");
			break;
		}

		alloc_size = cache_portion_size(cache_total_size, current_mask, full_cache_mask);

		bits = count_bits(current_mask);

		ret = show_results_info(sum_llc_perf_miss, bits,
					alloc_size / 64,
					MIN_DIFF_PERCENT_PER_BIT * (bits - 1),
					runs, get_vendor() == ARCH_INTEL,
					&prev_avg_llc_val);
		if (ret)
			fail = 1;

		runs = 0;
		sum_llc_perf_miss = 0;
		current_mask = next_mask(current_mask);
	}

	fclose(fp);

	return fail;
}

static void cat_test_cleanup(void)
{
	remove(RESULT_FILE_NAME);
}

/*
 * cat_test - Execute CAT benchmark and measure cache misses
 * @test:		Test information structure
 * @uparams:		User supplied parameters
 * @param:		Parameters passed to cat_test()
 * @span:		Buffer size for the benchmark
 * @current_mask	Start mask for the first iteration
 *
 * Run CAT selftest by varying the allocated cache portion and comparing the
 * impact on cache misses (the result analysis is done in check_results()
 * and show_results_info(), not in this function).
 *
 * One bit is removed from the CAT allocation bit mask (in current_mask) for
 * each subsequent test which keeps reducing the size of the allocated cache
 * portion. A single test flushes the buffer, reads it to warm up the cache,
 * and reads the buffer again. The cache misses are measured during the last
 * read pass.
 *
 * Return:		0 when the test was run, < 0 on error.
 */
static int cat_test(const struct resctrl_test *test,
		    const struct user_params *uparams,
		    struct resctrl_val_param *param,
		    size_t span, unsigned long current_mask)
{
	struct perf_event_read pe_read;
	struct perf_event_attr pea;
	cpu_set_t old_affinity;
	unsigned char *buf;
	char schemata[64];
	int ret, i, pe_fd;
	pid_t bm_pid;

	if (strcmp(param->filename, "") == 0)
		sprintf(param->filename, "stdio");

	bm_pid = getpid();

	/* Taskset benchmark to specified cpu */
	ret = taskset_benchmark(bm_pid, uparams->cpu, &old_affinity);
	if (ret)
		return ret;

	/* Write benchmark to specified con_mon grp, mon_grp in resctrl FS*/
	ret = write_bm_pid_to_resctrl(bm_pid, param->ctrlgrp, param->mongrp);
	if (ret)
		goto reset_affinity;

	perf_event_attr_initialize(&pea, PERF_COUNT_HW_CACHE_MISSES);
	perf_event_initialize_read_format(&pe_read);
	pe_fd = perf_open(&pea, bm_pid, uparams->cpu);
	if (pe_fd < 0) {
		ret = -1;
		goto reset_affinity;
	}

	buf = alloc_buffer(span, 1);
	if (!buf) {
		ret = -1;
		goto pe_close;
	}

	while (current_mask) {
		snprintf(schemata, sizeof(schemata), "%lx", param->mask & ~current_mask);
		ret = write_schemata("", schemata, uparams->cpu, test->resource);
		if (ret)
			goto free_buf;
		snprintf(schemata, sizeof(schemata), "%lx", current_mask);
		ret = write_schemata(param->ctrlgrp, schemata, uparams->cpu, test->resource);
		if (ret)
			goto free_buf;

		for (i = 0; i < NUM_OF_RUNS; i++) {
			mem_flush(buf, span);
			fill_cache_read(buf, span, true);

			ret = perf_event_reset_enable(pe_fd);
			if (ret)
				goto free_buf;

			fill_cache_read(buf, span, true);

			ret = perf_event_measure(pe_fd, &pe_read, param->filename, bm_pid);
			if (ret)
				goto free_buf;
		}
		current_mask = next_mask(current_mask);
	}

free_buf:
	free(buf);
pe_close:
	close(pe_fd);
reset_affinity:
	taskset_restore(bm_pid, &old_affinity);

	return ret;
}

static int cat_run_test(const struct resctrl_test *test, const struct user_params *uparams)
{
	unsigned long long_mask, start_mask, full_cache_mask;
	unsigned long cache_total_size = 0;
	int n = uparams->bits;
	unsigned int start;
	int count_of_bits;
	size_t span;
	int ret;

	ret = get_full_cbm(test->resource, &full_cache_mask);
	if (ret)
		return ret;
	/* Get the largest contiguous exclusive portion of the cache */
	ret = get_mask_no_shareable(test->resource, &long_mask);
	if (ret)
		return ret;

	/* Get L3/L2 cache size */
	ret = get_cache_size(uparams->cpu, test->resource, &cache_total_size);
	if (ret)
		return ret;
	ksft_print_msg("Cache size :%lu\n", cache_total_size);

	count_of_bits = count_contiguous_bits(long_mask, &start);

	if (!n)
		n = count_of_bits / 2;

	if (n > count_of_bits - 1) {
		ksft_print_msg("Invalid input value for no_of_bits n!\n");
		ksft_print_msg("Please enter value in range 1 to %d\n",
			       count_of_bits - 1);
		return -1;
	}
	start_mask = create_bit_mask(start, n);

	struct resctrl_val_param param = {
		.ctrlgrp	= "c1",
		.filename	= RESULT_FILE_NAME,
		.num_of_runs	= 0,
	};
	param.mask = long_mask;
	span = cache_portion_size(cache_total_size, start_mask, full_cache_mask);

	remove(param.filename);

	ret = cat_test(test, uparams, &param, span, start_mask);
	if (ret)
		return ret;

	ret = check_results(&param, test->resource,
			    cache_total_size, full_cache_mask, start_mask);
	return ret;
}

static bool arch_supports_noncont_cat(const struct resctrl_test *test)
{
	/* AMD always supports non-contiguous CBM. */
	if (get_vendor() == ARCH_AMD)
		return true;

#if defined(__i386__) || defined(__x86_64__) /* arch */
	unsigned int eax, ebx, ecx, edx;
	/* Intel support for non-contiguous CBM needs to be discovered. */
	if (!strcmp(test->resource, "L3"))
		__cpuid_count(0x10, 1, eax, ebx, ecx, edx);
	else if (!strcmp(test->resource, "L2"))
		__cpuid_count(0x10, 2, eax, ebx, ecx, edx);
	else
		return false;

	return ((ecx >> 3) & 1);
#endif /* end arch */

	return false;
}

static int noncont_cat_run_test(const struct resctrl_test *test,
				const struct user_params *uparams)
{
	unsigned long full_cache_mask, cont_mask, noncont_mask;
	unsigned int sparse_masks;
	int bit_center, ret;
	char schemata[64];

	/* Check to compare sparse_masks content to CPUID output. */
	ret = resource_info_unsigned_get(test->resource, "sparse_masks", &sparse_masks);
	if (ret)
		return ret;

	if (arch_supports_noncont_cat(test) != sparse_masks) {
		ksft_print_msg("Hardware and kernel differ on non-contiguous CBM support!\n");
		return 1;
	}

	/* Write checks initialization. */
	ret = get_full_cbm(test->resource, &full_cache_mask);
	if (ret < 0)
		return ret;
	bit_center = count_bits(full_cache_mask) / 2;

	/*
	 * The bit_center needs to be at least 3 to properly calculate the CBM
	 * hole in the noncont_mask. If it's smaller return an error since the
	 * cache mask is too short and that shouldn't happen.
	 */
	if (bit_center < 3)
		return -EINVAL;
	cont_mask = full_cache_mask >> bit_center;

	/* Contiguous mask write check. */
	snprintf(schemata, sizeof(schemata), "%lx", cont_mask);
	ret = write_schemata("", schemata, uparams->cpu, test->resource);
	if (ret) {
		ksft_print_msg("Write of contiguous CBM failed\n");
		return 1;
	}

	/*
	 * Non-contiguous mask write check. CBM has a 0xf hole approximately in the middle.
	 * Output is compared with support information to catch any edge case errors.
	 */
	noncont_mask = ~(0xfUL << (bit_center - 2)) & full_cache_mask;
	snprintf(schemata, sizeof(schemata), "%lx", noncont_mask);
	ret = write_schemata("", schemata, uparams->cpu, test->resource);
	if (ret && sparse_masks)
		ksft_print_msg("Non-contiguous CBMs supported but write of non-contiguous CBM failed\n");
	else if (ret && !sparse_masks)
		ksft_print_msg("Non-contiguous CBMs not supported and write of non-contiguous CBM failed as expected\n");
	else if (!ret && !sparse_masks)
		ksft_print_msg("Non-contiguous CBMs not supported but write of non-contiguous CBM succeeded\n");

	return !ret == !sparse_masks;
}

static bool noncont_cat_feature_check(const struct resctrl_test *test)
{
	if (!resctrl_resource_exists(test->resource))
		return false;

	return resource_info_file_exists(test->resource, "sparse_masks");
}

struct resctrl_test l3_cat_test = {
	.name = "L3_CAT",
	.group = "CAT",
	.resource = "L3",
	.feature_check = test_resource_feature_check,
	.run_test = cat_run_test,
	.cleanup = cat_test_cleanup,
};

struct resctrl_test l3_noncont_cat_test = {
	.name = "L3_NONCONT_CAT",
	.group = "CAT",
	.resource = "L3",
	.feature_check = noncont_cat_feature_check,
	.run_test = noncont_cat_run_test,
};

struct resctrl_test l2_noncont_cat_test = {
	.name = "L2_NONCONT_CAT",
	.group = "CAT",
	.resource = "L2",
	.feature_check = noncont_cat_feature_check,
	.run_test = noncont_cat_run_test,
};
