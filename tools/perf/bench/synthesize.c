// SPDX-License-Identifier: GPL-2.0
/*
 * Benchmark synthesis of perf events such as at the start of a 'perf
 * record'. Synthesis is done on the current process and the 'dummy' event
 * handlers are invoked that support dump_trace but otherwise do nothing.
 *
 * Copyright 2019 Google LLC.
 */
#include <stdio.h>
#include "bench.h"
#include "../util/debug.h"
#include "../util/session.h"
#include "../util/stat.h"
#include "../util/synthetic-events.h"
#include "../util/target.h"
#include "../util/thread_map.h"
#include "../util/tool.h"
#include "../util/util.h"
#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/time64.h>
#include <subcmd/parse-options.h>

static unsigned int min_threads = 1;
static unsigned int max_threads = UINT_MAX;
static unsigned int single_iterations = 10000;
static unsigned int multi_iterations = 10;
static bool run_st;
static bool run_mt;

static const struct option options[] = {
	OPT_BOOLEAN('s', "st", &run_st, "Run single threaded benchmark"),
	OPT_BOOLEAN('t', "mt", &run_mt, "Run multi-threaded benchmark"),
	OPT_UINTEGER('m', "min-threads", &min_threads,
		"Minimum number of threads in multithreaded bench"),
	OPT_UINTEGER('M', "max-threads", &max_threads,
		"Maximum number of threads in multithreaded bench"),
	OPT_UINTEGER('i', "single-iterations", &single_iterations,
		"Number of iterations used to compute single-threaded average"),
	OPT_UINTEGER('I', "multi-iterations", &multi_iterations,
		"Number of iterations used to compute multi-threaded average"),
	OPT_END()
};

static const char *const bench_usage[] = {
	"perf bench internals synthesize <options>",
	NULL
};

static atomic_t event_count;

static int process_synthesized_event(struct perf_tool *tool __maybe_unused,
				     union perf_event *event __maybe_unused,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	atomic_inc(&event_count);
	return 0;
}

static int do_run_single_threaded(struct perf_session *session,
				struct perf_thread_map *threads,
				struct target *target, bool data_mmap)
{
	const unsigned int nr_threads_synthesize = 1;
	struct timeval start, end, diff;
	u64 runtime_us;
	unsigned int i;
	double time_average, time_stddev, event_average, event_stddev;
	int err;
	struct stats time_stats, event_stats;

	init_stats(&time_stats);
	init_stats(&event_stats);

	for (i = 0; i < single_iterations; i++) {
		atomic_set(&event_count, 0);
		gettimeofday(&start, NULL);
		err = __machine__synthesize_threads(&session->machines.host,
						NULL,
						target, threads,
						process_synthesized_event,
						data_mmap,
						nr_threads_synthesize);
		if (err)
			return err;

		gettimeofday(&end, NULL);
		timersub(&end, &start, &diff);
		runtime_us = diff.tv_sec * USEC_PER_SEC + diff.tv_usec;
		update_stats(&time_stats, runtime_us);
		update_stats(&event_stats, atomic_read(&event_count));
	}

	time_average = avg_stats(&time_stats);
	time_stddev = stddev_stats(&time_stats);
	printf("  Average %ssynthesis took: %.3f usec (+- %.3f usec)\n",
		data_mmap ? "data " : "", time_average, time_stddev);

	event_average = avg_stats(&event_stats);
	event_stddev = stddev_stats(&event_stats);
	printf("  Average num. events: %.3f (+- %.3f)\n",
		event_average, event_stddev);

	printf("  Average time per event %.3f usec\n",
		time_average / event_average);
	return 0;
}

static int run_single_threaded(void)
{
	struct perf_session *session;
	struct target target = {
		.pid = "self",
	};
	struct perf_thread_map *threads;
	int err;

	perf_set_singlethreaded();
	session = perf_session__new(NULL, NULL);
	if (IS_ERR(session)) {
		pr_err("Session creation failed.\n");
		return PTR_ERR(session);
	}
	threads = thread_map__new_by_pid(getpid());
	if (!threads) {
		pr_err("Thread map creation failed.\n");
		err = -ENOMEM;
		goto err_out;
	}

	puts(
"Computing performance of single threaded perf event synthesis by\n"
"synthesizing events on the perf process itself:");

	err = do_run_single_threaded(session, threads, &target, false);
	if (err)
		goto err_out;

	err = do_run_single_threaded(session, threads, &target, true);

err_out:
	if (threads)
		perf_thread_map__put(threads);

	perf_session__delete(session);
	return err;
}

static int do_run_multi_threaded(struct target *target,
				unsigned int nr_threads_synthesize)
{
	struct timeval start, end, diff;
	u64 runtime_us;
	unsigned int i;
	double time_average, time_stddev, event_average, event_stddev;
	int err;
	struct stats time_stats, event_stats;
	struct perf_session *session;

	init_stats(&time_stats);
	init_stats(&event_stats);
	for (i = 0; i < multi_iterations; i++) {
		session = perf_session__new(NULL, NULL);
		if (IS_ERR(session))
			return PTR_ERR(session);

		atomic_set(&event_count, 0);
		gettimeofday(&start, NULL);
		err = __machine__synthesize_threads(&session->machines.host,
						NULL,
						target, NULL,
						process_synthesized_event,
						false,
						nr_threads_synthesize);
		if (err) {
			perf_session__delete(session);
			return err;
		}

		gettimeofday(&end, NULL);
		timersub(&end, &start, &diff);
		runtime_us = diff.tv_sec * USEC_PER_SEC + diff.tv_usec;
		update_stats(&time_stats, runtime_us);
		update_stats(&event_stats, atomic_read(&event_count));
		perf_session__delete(session);
	}

	time_average = avg_stats(&time_stats);
	time_stddev = stddev_stats(&time_stats);
	printf("    Average synthesis took: %.3f usec (+- %.3f usec)\n",
		time_average, time_stddev);

	event_average = avg_stats(&event_stats);
	event_stddev = stddev_stats(&event_stats);
	printf("    Average num. events: %.3f (+- %.3f)\n",
		event_average, event_stddev);

	printf("    Average time per event %.3f usec\n",
		time_average / event_average);
	return 0;
}

static int run_multi_threaded(void)
{
	struct target target = {
		.cpu_list = "0"
	};
	unsigned int nr_threads_synthesize;
	int err;

	if (max_threads == UINT_MAX)
		max_threads = sysconf(_SC_NPROCESSORS_ONLN);

	puts(
"Computing performance of multi threaded perf event synthesis by\n"
"synthesizing events on CPU 0:");

	for (nr_threads_synthesize = min_threads;
	     nr_threads_synthesize <= max_threads;
	     nr_threads_synthesize++) {
		if (nr_threads_synthesize == 1)
			perf_set_singlethreaded();
		else
			perf_set_multithreaded();

		printf("  Number of synthesis threads: %u\n",
			nr_threads_synthesize);

		err = do_run_multi_threaded(&target, nr_threads_synthesize);
		if (err)
			return err;
	}
	perf_set_singlethreaded();
	return 0;
}

int bench_synthesize(int argc, const char **argv)
{
	int err = 0;

	argc = parse_options(argc, argv, options, bench_usage, 0);
	if (argc) {
		usage_with_options(bench_usage, options);
		exit(EXIT_FAILURE);
	}

	/*
	 * If neither single threaded or multi-threaded are specified, default
	 * to running just single threaded.
	 */
	if (!run_st && !run_mt)
		run_st = true;

	if (run_st)
		err = run_single_threaded();

	if (!err && run_mt)
		err = run_multi_threaded();

	return err;
}
