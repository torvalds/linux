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
#include "../util/synthetic-events.h"
#include "../util/target.h"
#include "../util/thread_map.h"
#include "../util/tool.h"
#include <linux/err.h>
#include <linux/time64.h>
#include <subcmd/parse-options.h>

static unsigned int iterations = 10000;

static const struct option options[] = {
	OPT_UINTEGER('i', "iterations", &iterations,
		"Number of iterations used to compute average"),
	OPT_END()
};

static const char *const usage[] = {
	"perf bench internals synthesize <options>",
	NULL
};


static int do_synthesize(struct perf_session *session,
			struct perf_thread_map *threads,
			struct target *target, bool data_mmap)
{
	const unsigned int nr_threads_synthesize = 1;
	struct timeval start, end, diff;
	u64 runtime_us;
	unsigned int i;
	double average;
	int err;

	gettimeofday(&start, NULL);
	for (i = 0; i < iterations; i++) {
		err = machine__synthesize_threads(&session->machines.host,
						target, threads, data_mmap,
						nr_threads_synthesize);
		if (err)
			return err;
	}

	gettimeofday(&end, NULL);
	timersub(&end, &start, &diff);
	runtime_us = diff.tv_sec * USEC_PER_SEC + diff.tv_usec;
	average = (double)runtime_us/(double)iterations;
	printf("Average %ssynthesis took: %f usec\n",
		data_mmap ? "data " : "", average);
	return 0;
}

int bench_synthesize(int argc, const char **argv)
{
	struct perf_tool tool;
	struct perf_session *session;
	struct target target = {
		.pid = "self",
	};
	struct perf_thread_map *threads;
	int err;

	argc = parse_options(argc, argv, options, usage, 0);

	session = perf_session__new(NULL, false, NULL);
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
	perf_tool__fill_defaults(&tool);

	err = do_synthesize(session, threads, &target, false);
	if (err)
		goto err_out;

	err = do_synthesize(session, threads, &target, true);

err_out:
	if (threads)
		perf_thread_map__put(threads);

	perf_session__delete(session);
	return err;
}
