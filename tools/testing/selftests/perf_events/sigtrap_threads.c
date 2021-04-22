// SPDX-License-Identifier: GPL-2.0
/*
 * Test for perf events with SIGTRAP across all threads.
 *
 * Copyright (C) 2021, Google LLC.
 */

#define _GNU_SOURCE

/* We need the latest siginfo from the kernel repo. */
#include <sys/types.h>
#include <asm/siginfo.h>
#define __have_siginfo_t 1
#define __have_sigval_t 1
#define __have_sigevent_t 1
#define __siginfo_t_defined
#define __sigval_t_defined
#define __sigevent_t_defined
#define _BITS_SIGINFO_CONSTS_H 1
#define _BITS_SIGEVENT_CONSTS_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../kselftest_harness.h"

#define NUM_THREADS 5

/* Data shared between test body, threads, and signal handler. */
static struct {
	int tids_want_signal;		/* Which threads still want a signal. */
	int signal_count;		/* Sanity check number of signals received. */
	volatile int iterate_on;	/* Variable to set breakpoint on. */
	siginfo_t first_siginfo;	/* First observed siginfo_t. */
} ctx;

/* Unique value to check si_perf is correctly set from perf_event_attr::sig_data. */
#define TEST_SIG_DATA(addr) (~(unsigned long)(addr))

static struct perf_event_attr make_event_attr(bool enabled, volatile void *addr)
{
	struct perf_event_attr attr = {
		.type		= PERF_TYPE_BREAKPOINT,
		.size		= sizeof(attr),
		.sample_period	= 1,
		.disabled	= !enabled,
		.bp_addr	= (unsigned long)addr,
		.bp_type	= HW_BREAKPOINT_RW,
		.bp_len		= HW_BREAKPOINT_LEN_1,
		.inherit	= 1, /* Children inherit events ... */
		.inherit_thread = 1, /* ... but only cloned with CLONE_THREAD. */
		.remove_on_exec = 1, /* Required by sigtrap. */
		.sigtrap	= 1, /* Request synchronous SIGTRAP on event. */
		.sig_data	= TEST_SIG_DATA(addr),
	};
	return attr;
}

static void sigtrap_handler(int signum, siginfo_t *info, void *ucontext)
{
	if (info->si_code != TRAP_PERF) {
		fprintf(stderr, "%s: unexpected si_code %d\n", __func__, info->si_code);
		return;
	}

	/*
	 * The data in siginfo_t we're interested in should all be the same
	 * across threads.
	 */
	if (!__atomic_fetch_add(&ctx.signal_count, 1, __ATOMIC_RELAXED))
		ctx.first_siginfo = *info;
	__atomic_fetch_sub(&ctx.tids_want_signal, syscall(__NR_gettid), __ATOMIC_RELAXED);
}

static void *test_thread(void *arg)
{
	pthread_barrier_t *barrier = (pthread_barrier_t *)arg;
	pid_t tid = syscall(__NR_gettid);
	int iter;
	int i;

	pthread_barrier_wait(barrier);

	__atomic_fetch_add(&ctx.tids_want_signal, tid, __ATOMIC_RELAXED);
	iter = ctx.iterate_on; /* read */
	for (i = 0; i < iter - 1; i++) {
		__atomic_fetch_add(&ctx.tids_want_signal, tid, __ATOMIC_RELAXED);
		ctx.iterate_on = iter; /* idempotent write */
	}

	return NULL;
}

FIXTURE(sigtrap_threads)
{
	struct sigaction oldact;
	pthread_t threads[NUM_THREADS];
	pthread_barrier_t barrier;
	int fd;
};

FIXTURE_SETUP(sigtrap_threads)
{
	struct perf_event_attr attr = make_event_attr(false, &ctx.iterate_on);
	struct sigaction action = {};
	int i;

	memset(&ctx, 0, sizeof(ctx));

	/* Initialize sigtrap handler. */
	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	action.sa_sigaction = sigtrap_handler;
	sigemptyset(&action.sa_mask);
	ASSERT_EQ(sigaction(SIGTRAP, &action, &self->oldact), 0);

	/* Initialize perf event. */
	self->fd = syscall(__NR_perf_event_open, &attr, 0, -1, -1, PERF_FLAG_FD_CLOEXEC);
	ASSERT_NE(self->fd, -1);

	/* Spawn threads inheriting perf event. */
	pthread_barrier_init(&self->barrier, NULL, NUM_THREADS + 1);
	for (i = 0; i < NUM_THREADS; i++)
		ASSERT_EQ(pthread_create(&self->threads[i], NULL, test_thread, &self->barrier), 0);
}

FIXTURE_TEARDOWN(sigtrap_threads)
{
	pthread_barrier_destroy(&self->barrier);
	close(self->fd);
	sigaction(SIGTRAP, &self->oldact, NULL);
}

static void run_test_threads(struct __test_metadata *_metadata,
			     FIXTURE_DATA(sigtrap_threads) *self)
{
	int i;

	pthread_barrier_wait(&self->barrier);
	for (i = 0; i < NUM_THREADS; i++)
		ASSERT_EQ(pthread_join(self->threads[i], NULL), 0);
}

TEST_F(sigtrap_threads, remain_disabled)
{
	run_test_threads(_metadata, self);
	EXPECT_EQ(ctx.signal_count, 0);
	EXPECT_NE(ctx.tids_want_signal, 0);
}

TEST_F(sigtrap_threads, enable_event)
{
	EXPECT_EQ(ioctl(self->fd, PERF_EVENT_IOC_ENABLE, 0), 0);
	run_test_threads(_metadata, self);

	EXPECT_EQ(ctx.signal_count, NUM_THREADS);
	EXPECT_EQ(ctx.tids_want_signal, 0);
	EXPECT_EQ(ctx.first_siginfo.si_addr, &ctx.iterate_on);
	EXPECT_EQ(ctx.first_siginfo.si_errno, PERF_TYPE_BREAKPOINT);
	EXPECT_EQ(ctx.first_siginfo.si_perf, TEST_SIG_DATA(&ctx.iterate_on));

	/* Check enabled for parent. */
	ctx.iterate_on = 0;
	EXPECT_EQ(ctx.signal_count, NUM_THREADS + 1);
}

/* Test that modification propagates to all inherited events. */
TEST_F(sigtrap_threads, modify_and_enable_event)
{
	struct perf_event_attr new_attr = make_event_attr(true, &ctx.iterate_on);

	EXPECT_EQ(ioctl(self->fd, PERF_EVENT_IOC_MODIFY_ATTRIBUTES, &new_attr), 0);
	run_test_threads(_metadata, self);

	EXPECT_EQ(ctx.signal_count, NUM_THREADS);
	EXPECT_EQ(ctx.tids_want_signal, 0);
	EXPECT_EQ(ctx.first_siginfo.si_addr, &ctx.iterate_on);
	EXPECT_EQ(ctx.first_siginfo.si_errno, PERF_TYPE_BREAKPOINT);
	EXPECT_EQ(ctx.first_siginfo.si_perf, TEST_SIG_DATA(&ctx.iterate_on));

	/* Check enabled for parent. */
	ctx.iterate_on = 0;
	EXPECT_EQ(ctx.signal_count, NUM_THREADS + 1);
}

/* Stress test event + signal handling. */
TEST_F(sigtrap_threads, signal_stress)
{
	ctx.iterate_on = 3000;

	EXPECT_EQ(ioctl(self->fd, PERF_EVENT_IOC_ENABLE, 0), 0);
	run_test_threads(_metadata, self);
	EXPECT_EQ(ioctl(self->fd, PERF_EVENT_IOC_DISABLE, 0), 0);

	EXPECT_EQ(ctx.signal_count, NUM_THREADS * ctx.iterate_on);
	EXPECT_EQ(ctx.tids_want_signal, 0);
	EXPECT_EQ(ctx.first_siginfo.si_addr, &ctx.iterate_on);
	EXPECT_EQ(ctx.first_siginfo.si_errno, PERF_TYPE_BREAKPOINT);
	EXPECT_EQ(ctx.first_siginfo.si_perf, TEST_SIG_DATA(&ctx.iterate_on));
}

TEST_HARNESS_MAIN
