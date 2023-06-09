// SPDX-License-Identifier: GPL-2.0
/*
 * Benchmark scanning sysfs files for PMU information.
 *
 * Copyright 2023 Google LLC.
 */
#include <stdio.h>
#include "bench.h"
#include "util/debug.h"
#include "util/pmu.h"
#include "util/pmus.h"
#include "util/stat.h"
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/time64.h>
#include <subcmd/parse-options.h>

static unsigned int iterations = 100;

struct pmu_scan_result {
	char *name;
	int nr_aliases;
	int nr_formats;
	int nr_caps;
};

static const struct option options[] = {
	OPT_UINTEGER('i', "iterations", &iterations,
		"Number of iterations used to compute average"),
	OPT_END()
};

static const char *const bench_usage[] = {
	"perf bench internals pmu-scan <options>",
	NULL
};

static int nr_pmus;
static struct pmu_scan_result *results;

static int save_result(void)
{
	struct perf_pmu *pmu;
	struct list_head *list;
	struct pmu_scan_result *r;

	perf_pmu__scan(NULL);

	perf_pmus__for_each_pmu(pmu) {
		r = realloc(results, (nr_pmus + 1) * sizeof(*r));
		if (r == NULL)
			return -ENOMEM;

		results = r;
		r = results + nr_pmus;

		r->name = strdup(pmu->name);
		r->nr_caps = pmu->nr_caps;

		r->nr_aliases = 0;
		list_for_each(list, &pmu->aliases)
			r->nr_aliases++;

		r->nr_formats = 0;
		list_for_each(list, &pmu->format)
			r->nr_formats++;

		pr_debug("pmu[%d] name=%s, nr_caps=%d, nr_aliases=%d, nr_formats=%d\n",
			nr_pmus, r->name, r->nr_caps, r->nr_aliases, r->nr_formats);
		nr_pmus++;
	}

	perf_pmu__destroy();
	return 0;
}

static int check_result(void)
{
	struct pmu_scan_result *r;
	struct perf_pmu *pmu;
	struct list_head *list;
	int nr;

	for (int i = 0; i < nr_pmus; i++) {
		r = &results[i];
		pmu = perf_pmu__find(r->name);
		if (pmu == NULL) {
			pr_err("Cannot find PMU %s\n", r->name);
			return -1;
		}

		if (pmu->nr_caps != (u32)r->nr_caps) {
			pr_err("Unmatched number of event caps in %s: expect %d vs got %d\n",
				pmu->name, r->nr_caps, pmu->nr_caps);
			return -1;
		}

		nr = 0;
		list_for_each(list, &pmu->aliases)
			nr++;
		if (nr != r->nr_aliases) {
			pr_err("Unmatched number of event aliases in %s: expect %d vs got %d\n",
				pmu->name, r->nr_aliases, nr);
			return -1;
		}

		nr = 0;
		list_for_each(list, &pmu->format)
			nr++;
		if (nr != r->nr_formats) {
			pr_err("Unmatched number of event formats in %s: expect %d vs got %d\n",
				pmu->name, r->nr_formats, nr);
			return -1;
		}
	}
	return 0;
}

static void delete_result(void)
{
	for (int i = 0; i < nr_pmus; i++)
		free(results[i].name);
	free(results);

	results = NULL;
	nr_pmus = 0;
}

static int run_pmu_scan(void)
{
	struct stats stats;
	struct timeval start, end, diff;
	double time_average, time_stddev;
	u64 runtime_us;
	unsigned int i;
	int ret;

	init_stats(&stats);
	pr_info("Computing performance of sysfs PMU event scan for %u times\n",
		iterations);

	if (save_result() < 0) {
		pr_err("Failed to initialize PMU scan result\n");
		return -1;
	}

	for (i = 0; i < iterations; i++) {
		gettimeofday(&start, NULL);
		perf_pmu__scan(NULL);
		gettimeofday(&end, NULL);

		timersub(&end, &start, &diff);
		runtime_us = diff.tv_sec * USEC_PER_SEC + diff.tv_usec;
		update_stats(&stats, runtime_us);

		ret = check_result();
		perf_pmu__destroy();
		if (ret < 0)
			break;
	}

	time_average = avg_stats(&stats);
	time_stddev = stddev_stats(&stats);
	pr_info("  Average PMU scanning took: %.3f usec (+- %.3f usec)\n",
		time_average, time_stddev);

	delete_result();
	return 0;
}

int bench_pmu_scan(int argc, const char **argv)
{
	int err = 0;

	argc = parse_options(argc, argv, options, bench_usage, 0);
	if (argc) {
		usage_with_options(bench_usage, options);
		exit(EXIT_FAILURE);
	}

	err = run_pmu_scan();

	return err;
}
