// SPDX-License-Identifier: GPL-2.0
/*
 * Benchmark of /proc/kallsyms parsing.
 *
 * Copyright 2020 Google LLC.
 */
#include <stdlib.h>
#include "bench.h"
#include "../util/stat.h"
#include <linux/time64.h>
#include <subcmd/parse-options.h>
#include <symbol/kallsyms.h>

static unsigned int iterations = 100;

static const struct option options[] = {
	OPT_UINTEGER('i', "iterations", &iterations,
		"Number of iterations used to compute average"),
	OPT_END()
};

static const char *const bench_usage[] = {
	"perf bench internals kallsyms-parse <options>",
	NULL
};

static int bench_process_symbol(void *arg __maybe_unused,
				const char *name __maybe_unused,
				char type __maybe_unused,
				u64 start __maybe_unused)
{
	return 0;
}

static int do_kallsyms_parse(void)
{
	struct timeval start, end, diff;
	u64 runtime_us;
	unsigned int i;
	double time_average, time_stddev;
	int err;
	struct stats time_stats;

	init_stats(&time_stats);

	for (i = 0; i < iterations; i++) {
		gettimeofday(&start, NULL);
		err = kallsyms__parse("/proc/kallsyms", NULL,
				bench_process_symbol);
		if (err)
			return err;

		gettimeofday(&end, NULL);
		timersub(&end, &start, &diff);
		runtime_us = diff.tv_sec * USEC_PER_SEC + diff.tv_usec;
		update_stats(&time_stats, runtime_us);
	}

	time_average = avg_stats(&time_stats) / USEC_PER_MSEC;
	time_stddev = stddev_stats(&time_stats) / USEC_PER_MSEC;
	printf("  Average kallsyms__parse took: %.3f ms (+- %.3f ms)\n",
		time_average, time_stddev);
	return 0;
}

int bench_kallsyms_parse(int argc, const char **argv)
{
	argc = parse_options(argc, argv, options, bench_usage, 0);
	if (argc) {
		usage_with_options(bench_usage, options);
		exit(EXIT_FAILURE);
	}

	return do_kallsyms_parse();
}
