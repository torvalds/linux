// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <test_progs.h>
#include "timer_start_delete_race.skel.h"

/*
 * Test for race between bpf_timer_start() and map element deletion.
 *
 * The race scenario:
 * - CPU 1: bpf_timer_start() proceeds to bpf_async_process() and is about
 *          to call hrtimer_start() but hasn't yet
 * - CPU 2: map_delete_elem() calls __bpf_async_cancel_and_free(), since
 *          timer is not scheduled yet hrtimer_try_to_cancel() is a nop,
 *          then calls bpf_async_refcount_put() dropping refcnt to zero
 *          and scheduling call_rcu_tasks_trace()
 * - CPU 1: continues and calls hrtimer_start()
 * - After RCU tasks trace grace period: memory is freed
 * - Timer callback fires on freed memory: UAF!
 *
 * This test stresses this race by having two threads:
 * - Thread 1: repeatedly starts timers
 * - Thread 2: repeatedly deletes map elements
 *
 * KASAN should detect use-after-free.
 */

#define ITERATIONS 1000

struct ctx {
	struct timer_start_delete_race *skel;
	volatile bool start;
	volatile bool stop;
	int errors;
};

static void *start_timer_thread(void *arg)
{
	struct ctx *ctx = arg;
	cpu_set_t cpuset;
	int fd, i;

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

	while (!ctx->start && !ctx->stop)
		usleep(1);
	if (ctx->stop)
		return NULL;

	fd = bpf_program__fd(ctx->skel->progs.start_timer);

	for (i = 0; i < ITERATIONS && !ctx->stop; i++) {
		LIBBPF_OPTS(bpf_test_run_opts, opts);
		int err;

		err = bpf_prog_test_run_opts(fd, &opts);
		if (err || opts.retval) {
			ctx->errors++;
			break;
		}
	}

	return NULL;
}

static void *delete_elem_thread(void *arg)
{
	struct ctx *ctx = arg;
	cpu_set_t cpuset;
	int fd, i;

	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

	while (!ctx->start && !ctx->stop)
		usleep(1);
	if (ctx->stop)
		return NULL;

	fd = bpf_program__fd(ctx->skel->progs.delete_elem);

	for (i = 0; i < ITERATIONS && !ctx->stop; i++) {
		LIBBPF_OPTS(bpf_test_run_opts, opts);
		int err;

		err = bpf_prog_test_run_opts(fd, &opts);
		if (err || opts.retval) {
			ctx->errors++;
			break;
		}
	}

	return NULL;
}

void test_timer_start_delete_race(void)
{
	struct timer_start_delete_race *skel;
	pthread_t threads[2];
	struct ctx ctx = {};
	int err;

	skel = timer_start_delete_race__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	ctx.skel = skel;

	err = pthread_create(&threads[0], NULL, start_timer_thread, &ctx);
	if (!ASSERT_OK(err, "create start_timer_thread")) {
		ctx.stop = true;
		goto cleanup;
	}

	err = pthread_create(&threads[1], NULL, delete_elem_thread, &ctx);
	if (!ASSERT_OK(err, "create delete_elem_thread")) {
		ctx.stop = true;
		pthread_join(threads[0], NULL);
		goto cleanup;
	}

	ctx.start = true;

	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);

	ASSERT_EQ(ctx.errors, 0, "thread_errors");

	/* Either KASAN will catch UAF or kernel will crash or nothing happens */
cleanup:
	timer_start_delete_race__destroy(skel);
}
