// SPDX-License-Identifier: GPL-2.0
/*
 * Benchmark find_next_bit and related bit operations.
 *
 * Copyright 2020 Google LLC.
 */
#include <stdlib.h>
#include "bench.h"
#include "../util/stat.h"
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/time64.h>
#include <subcmd/parse-options.h>

static unsigned int outer_iterations = 5;
static unsigned int inner_iterations = 100000;

static const struct option options[] = {
	OPT_UINTEGER('i', "outer-iterations", &outer_iterations,
		"Number of outer iterations used"),
	OPT_UINTEGER('j', "inner-iterations", &inner_iterations,
		"Number of inner iterations used"),
	OPT_END()
};

static const char *const bench_usage[] = {
	"perf bench mem find_bit <options>",
	NULL
};

static unsigned int accumulator;
static unsigned int use_of_val;

static noinline void workload(int val)
{
	use_of_val += val;
	accumulator++;
}

#if (defined(__i386__) || defined(__x86_64__)) && defined(__GCC_ASM_FLAG_OUTPUTS__)
static bool asm_test_bit(long nr, const unsigned long *addr)
{
	bool oldbit;

	asm volatile("bt %2,%1"
		     : "=@ccc" (oldbit)
		     : "m" (*(unsigned long *)addr), "Ir" (nr) : "memory");

	return oldbit;
}
#else
#define asm_test_bit test_bit
#endif

static int do_for_each_set_bit(unsigned int num_bits)
{
	unsigned long *to_test = bitmap_zalloc(num_bits);
	struct timeval start, end, diff;
	u64 runtime_us;
	struct stats fb_time_stats, tb_time_stats;
	double time_average, time_stddev;
	unsigned int bit, i, j;
	unsigned int set_bits, skip;
	unsigned int old;

	init_stats(&fb_time_stats);
	init_stats(&tb_time_stats);

	for (set_bits = 1; set_bits <= num_bits; set_bits <<= 1) {
		bitmap_zero(to_test, num_bits);
		skip = num_bits / set_bits;
		for (i = 0; i < num_bits; i += skip)
			set_bit(i, to_test);

		for (i = 0; i < outer_iterations; i++) {
			old = accumulator;
			gettimeofday(&start, NULL);
			for (j = 0; j < inner_iterations; j++) {
				for_each_set_bit(bit, to_test, num_bits)
					workload(bit);
			}
			gettimeofday(&end, NULL);
			assert(old + (inner_iterations * set_bits) == accumulator);
			timersub(&end, &start, &diff);
			runtime_us = diff.tv_sec * USEC_PER_SEC + diff.tv_usec;
			update_stats(&fb_time_stats, runtime_us);

			old = accumulator;
			gettimeofday(&start, NULL);
			for (j = 0; j < inner_iterations; j++) {
				for (bit = 0; bit < num_bits; bit++) {
					if (asm_test_bit(bit, to_test))
						workload(bit);
				}
			}
			gettimeofday(&end, NULL);
			assert(old + (inner_iterations * set_bits) == accumulator);
			timersub(&end, &start, &diff);
			runtime_us = diff.tv_sec * USEC_PER_SEC + diff.tv_usec;
			update_stats(&tb_time_stats, runtime_us);
		}

		printf("%d operations %d bits set of %d bits\n",
			inner_iterations, set_bits, num_bits);
		time_average = avg_stats(&fb_time_stats);
		time_stddev = stddev_stats(&fb_time_stats);
		printf("  Average for_each_set_bit took: %.3f usec (+- %.3f usec)\n",
			time_average, time_stddev);
		time_average = avg_stats(&tb_time_stats);
		time_stddev = stddev_stats(&tb_time_stats);
		printf("  Average test_bit loop took:    %.3f usec (+- %.3f usec)\n",
			time_average, time_stddev);

		if (use_of_val == accumulator)  /* Try to avoid compiler tricks. */
			printf("\n");
	}
	bitmap_free(to_test);
	return 0;
}

int bench_mem_find_bit(int argc, const char **argv)
{
	int err = 0, i;

	argc = parse_options(argc, argv, options, bench_usage, 0);
	if (argc) {
		usage_with_options(bench_usage, options);
		exit(EXIT_FAILURE);
	}

	for (i = 1; i <= 2048; i <<= 1)
		do_for_each_set_bit(i);

	return err;
}
