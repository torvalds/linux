// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025. Huawei Technologies Co., Ltd */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <test_progs.h>

#include "free_timer.skel.h"

struct run_ctx {
	struct bpf_program *start_prog;
	struct bpf_program *overwrite_prog;
	pthread_barrier_t notify;
	int loop;
	bool start;
	bool stop;
};

static void start_threads(struct run_ctx *ctx)
{
	ctx->start = true;
}

static void stop_threads(struct run_ctx *ctx)
{
	ctx->stop = true;
	/* Guarantee the order between ->stop and ->start */
	__atomic_store_n(&ctx->start, true, __ATOMIC_RELEASE);
}

static int wait_for_start(struct run_ctx *ctx)
{
	while (!__atomic_load_n(&ctx->start, __ATOMIC_ACQUIRE))
		usleep(10);

	return ctx->stop;
}

static void *overwrite_timer_fn(void *arg)
{
	struct run_ctx *ctx = arg;
	int loop, fd, err;
	cpu_set_t cpuset;
	long ret = 0;

	/* Pin on CPU 0 */
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

	/* Is the thread being stopped ? */
	err = wait_for_start(ctx);
	if (err)
		return NULL;

	fd = bpf_program__fd(ctx->overwrite_prog);
	loop = ctx->loop;
	while (loop-- > 0) {
		LIBBPF_OPTS(bpf_test_run_opts, opts);

		/* Wait for start thread to complete */
		pthread_barrier_wait(&ctx->notify);

		/* Overwrite timers */
		err = bpf_prog_test_run_opts(fd, &opts);
		if (err)
			ret |= 1;
		else if (opts.retval)
			ret |= 2;

		/* Notify start thread to start timers */
		pthread_barrier_wait(&ctx->notify);
	}

	return (void *)ret;
}

static void *start_timer_fn(void *arg)
{
	struct run_ctx *ctx = arg;
	int loop, fd, err;
	cpu_set_t cpuset;
	long ret = 0;

	/* Pin on CPU 1 */
	CPU_ZERO(&cpuset);
	CPU_SET(1, &cpuset);
	pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

	/* Is the thread being stopped ? */
	err = wait_for_start(ctx);
	if (err)
		return NULL;

	fd = bpf_program__fd(ctx->start_prog);
	loop = ctx->loop;
	while (loop-- > 0) {
		LIBBPF_OPTS(bpf_test_run_opts, opts);

		/* Run the prog to start timer */
		err = bpf_prog_test_run_opts(fd, &opts);
		if (err)
			ret |= 4;
		else if (opts.retval)
			ret |= 8;

		/* Notify overwrite thread to do overwrite */
		pthread_barrier_wait(&ctx->notify);

		/* Wait for overwrite thread to complete */
		pthread_barrier_wait(&ctx->notify);
	}

	return (void *)ret;
}

void test_free_timer(void)
{
	struct free_timer *skel;
	struct bpf_program *prog;
	struct run_ctx ctx;
	pthread_t tid[2];
	void *ret;
	int err;

	skel = free_timer__open_and_load();
	if (!skel && errno == EOPNOTSUPP) {
		test__skip();
		return;
	}
	if (!ASSERT_OK_PTR(skel, "open_load"))
		return;

	memset(&ctx, 0, sizeof(ctx));

	prog = bpf_object__find_program_by_name(skel->obj, "start_timer");
	if (!ASSERT_OK_PTR(prog, "find start prog"))
		goto out;
	ctx.start_prog = prog;

	prog = bpf_object__find_program_by_name(skel->obj, "overwrite_timer");
	if (!ASSERT_OK_PTR(prog, "find overwrite prog"))
		goto out;
	ctx.overwrite_prog = prog;

	pthread_barrier_init(&ctx.notify, NULL, 2);
	ctx.loop = 10;

	err = pthread_create(&tid[0], NULL, start_timer_fn, &ctx);
	if (!ASSERT_OK(err, "create start_timer"))
		goto out;

	err = pthread_create(&tid[1], NULL, overwrite_timer_fn, &ctx);
	if (!ASSERT_OK(err, "create overwrite_timer")) {
		stop_threads(&ctx);
		goto out;
	}

	start_threads(&ctx);

	ret = NULL;
	err = pthread_join(tid[0], &ret);
	ASSERT_EQ(err | (long)ret, 0, "start_timer");
	ret = NULL;
	err = pthread_join(tid[1], &ret);
	ASSERT_EQ(err | (long)ret, 0, "overwrite_timer");
out:
	free_timer__destroy(skel);
}
