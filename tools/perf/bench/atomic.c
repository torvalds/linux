// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Changbin Du <changbin.du@gmail.com>
 *
 * Benchmarks for atomic read-modify-write operations.
 *
 * Measures throughput (ops/sec) of contended atomic operations on a
 * single shared counter across multiple threads. Each thread performs
 * a hot loop of the selected operation -- compare-and-swap (cas) or
 * fetch-add (inc) -- competing with all other threads for the same
 * cache line. Threads run for a fixed runtime, so a system that
 * cannot make progress under heavy contention reports a low (or
 * zero) throughput instead of hanging.
 */
#include "bench.h"
#include <linux/compiler.h>
#include "../util/debug.h"

#ifndef HAVE_PTHREAD_BARRIER
int bench_atomic_cas(int argc __maybe_unused, const char **argv __maybe_unused)
{
	pr_err("%s: pthread_barrier_t unavailable, disabling this test...\n", __func__);
	return 0;
}

int bench_atomic_inc(int argc __maybe_unused, const char **argv __maybe_unused)
{
	pr_err("%s: pthread_barrier_t unavailable, disabling this test...\n", __func__);
	return 0;
}
#else /* HAVE_PTHREAD_BARRIER */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <inttypes.h>
#include <err.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time64.h>
#include <perf/cpumap.h>
#include <subcmd/parse-options.h>
#include "../util/stat.h"

static unsigned int threads;		/* 0: default to min(2, online CPUs) */
static unsigned int runtime	= 2;	/* seconds per repeat */

static const struct option options[] = {
	OPT_UINTEGER('T', "threads", &threads,
		     "Number of contending threads (default: 2, or 1 on single CPU)"),
	OPT_UINTEGER('R', "runtime", &runtime,
		     "Runtime of each repeat in seconds (default: 2)"),
	OPT_END()
};

static const char * const cas_usage[] = {
	"perf bench atomic cas <options>",
	NULL
};

static const char * const inc_usage[] = {
	"perf bench atomic inc <options>",
	NULL
};

enum atomic_op {
	OP_CAS,
	OP_INC,
};

static int bench_op = OP_CAS;

/*
 * The termination flag is polled between batches only, so that the
 * check does not perturb the measured loop.
 */
#define BATCH_OPS	1024

static unsigned long shared_counter __aligned(64);
static pthread_barrier_t start_barrier __aligned(64);
static bool done;

struct worker_stats {
	u64	runtime_ns;
	u64	ops;
};

static void cas_batch(void)
{
	unsigned long i;

	/*
	 * Each iteration counts as one successful compare-and-swap;
	 * retries and the loads between them are not counted.
	 */
	for (i = 0; i < BATCH_OPS; i++) {
		unsigned long old_val, new_val;

		old_val = __atomic_load_n(&shared_counter, __ATOMIC_RELAXED);
		do {
			new_val = old_val + 1;
		} while (!__atomic_compare_exchange_n(&shared_counter, &old_val,
						      new_val, false,
						      __ATOMIC_RELAXED,
						      __ATOMIC_RELAXED));
	}
}

static void inc_batch(void)
{
	unsigned long i;

	for (i = 0; i < BATCH_OPS; i++)
		__atomic_fetch_add(&shared_counter, 1, __ATOMIC_RELAXED);
}

static void *worker(void *arg)
{
	struct worker_stats *ws = (struct worker_stats *)arg;
	struct timespec tstart, tend;
	u64 ops = 0;

	pthread_barrier_wait(&start_barrier);

	clock_gettime(CLOCK_MONOTONIC, &tstart);
	while (!__atomic_load_n(&done, __ATOMIC_RELAXED)) {
		switch (bench_op) {
		case OP_CAS:
			cas_batch();
			break;
		case OP_INC:
			inc_batch();
			break;
		default:
			BUG_ON(1);
			break;
		}
		ops += BATCH_OPS;
	}
	clock_gettime(CLOCK_MONOTONIC, &tend);

	ws->runtime_ns	= (u64)(tend.tv_sec - tstart.tv_sec) * NSEC_PER_SEC +
			  (tend.tv_nsec - tstart.tv_nsec);
	ws->ops		= ops;

	return NULL;
}

static void print_default_format(struct worker_stats *wstats,
				 struct stats *throughput_stats)
{
	double thru_avg, thru_stddev;

	printf("  Threads: %u, runtime: %u sec, repeats: %u (warmup: 1)\n",
	       threads, runtime, bench_repeat);

	thru_avg    = avg_stats(throughput_stats);
	thru_stddev = stddev_stats(throughput_stats);

	printf("  Avg throughput: %'.0f ops/sec (+- %.2f%%)\n",
	       thru_avg, rel_stddev_stats(thru_stddev, thru_avg));

	/*
	 * Per-thread operation counts from the last measured repeat show
	 * whether any thread failed to make progress under contention.
	 */
	if (threads > 1) {
		u64 min_ops = UINT64_MAX, max_ops = 0, total_ops = 0;

		for (unsigned int t = 0; t < threads; t++) {
			if (wstats[t].ops < min_ops)
				min_ops = wstats[t].ops;
			if (wstats[t].ops > max_ops)
				max_ops = wstats[t].ops;
			total_ops += wstats[t].ops;
		}

		printf("  Per-thread ops (last repeat):\n");
		printf("    fastest:  %'" PRIu64 " ops\n", max_ops);
		printf("    slowest:  %'" PRIu64 " ops\n", min_ops);
		printf("    avg:      %'" PRIu64 " ops\n", total_ops / threads);
	}
}

static int run_benchmark(void)
{
	pthread_t *thread_ids;
	struct worker_stats *wstats;
	struct stats throughput_stats;
	unsigned int r, t;

	thread_ids = calloc(threads, sizeof(pthread_t));
	if (!thread_ids)
		err(EXIT_FAILURE, "calloc");

	wstats = calloc(threads, sizeof(struct worker_stats));
	if (!wstats)
		err(EXIT_FAILURE, "calloc");

	init_stats(&throughput_stats);

	for (r = 0; r < bench_repeat + 1; r++) {
		u64 max_runtime_ns = 0, total_ops = 0;
		double throughput;

		shared_counter = 0;
		__atomic_store_n(&done, false, __ATOMIC_RELAXED);
		pthread_barrier_init(&start_barrier, NULL, threads);

		for (t = 0; t < threads; t++) {
			if (pthread_create(&thread_ids[t], NULL,
					   worker, &wstats[t]))
				err(EXIT_FAILURE, "pthread_create");
		}

		sleep(runtime);
		__atomic_store_n(&done, true, __ATOMIC_RELAXED);

		for (t = 0; t < threads; t++)
			pthread_join(thread_ids[t], NULL);

		pthread_barrier_destroy(&start_barrier);

		for (t = 0; t < threads; t++) {
			total_ops += wstats[t].ops;
			if (wstats[t].runtime_ns > max_runtime_ns)
				max_runtime_ns = wstats[t].runtime_ns;
		}

		/*
		 * Exclude the first repeat (warmup) from statistics so
		 * that cache-cold and lazy-init effects are not counted.
		 */
		if (r > 0 && max_runtime_ns) {
			throughput = (double)total_ops /
				     ((double)max_runtime_ns / NSEC_PER_SEC);
			update_stats(&throughput_stats, (u64)throughput);
		}
	}

	switch (bench_format) {
	case BENCH_FORMAT_DEFAULT:
		print_default_format(wstats, &throughput_stats);
		break;

	case BENCH_FORMAT_SIMPLE:
		printf("%.0f\n", avg_stats(&throughput_stats));
		break;

	default:
		fprintf(stderr, "Unknown format: %d\n", bench_format);
		exit(EXIT_FAILURE);
	}

	free(thread_ids);
	free(wstats);

	return 0;
}

static int run_atomic_bench(int argc, const char **argv,
			    const char * const *usage)
{
	struct perf_cpu_map *cpu;

	if (parse_options(argc, argv, options, usage, 0)) {
		usage_with_options(usage, options);
		exit(EXIT_FAILURE);
	}

	if (!threads) {	/* default to 2 threads (1 on a single-CPU machine) */
		cpu = perf_cpu_map__new_online_cpus();
		threads = (cpu && perf_cpu_map__nr(cpu) > 1) ? 2 : 1;
		perf_cpu_map__put(cpu);
	}

	if (runtime < 1) {
		fprintf(stderr, "Invalid runtime: %u\n", runtime);
		return 1;
	}

	return run_benchmark();
}

int bench_atomic_cas(int argc, const char **argv)
{
	bench_op = OP_CAS;
	return run_atomic_bench(argc, argv, cas_usage);
}

int bench_atomic_inc(int argc, const char **argv)
{
	bench_op = OP_INC;
	return run_atomic_bench(argc, argv, inc_usage);
}
#endif /* HAVE_PTHREAD_BARRIER */
