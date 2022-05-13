// SPDX-License-Identifier: GPL-2.0
/*
 * Basic test for sigtrap support.
 *
 * Copyright (C) 2021, Google LLC.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/hw_breakpoint.h>
#include <linux/string.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "cloexec.h"
#include "debug.h"
#include "event.h"
#include "tests.h"
#include "../perf-sys.h"

#define NUM_THREADS 5

static struct {
	int tids_want_signal;		/* Which threads still want a signal. */
	int signal_count;		/* Sanity check number of signals received. */
	volatile int iterate_on;	/* Variable to set breakpoint on. */
	siginfo_t first_siginfo;	/* First observed siginfo_t. */
} ctx;

#define TEST_SIG_DATA (~(unsigned long)(&ctx.iterate_on))

static struct perf_event_attr make_event_attr(void)
{
	struct perf_event_attr attr = {
		.type		= PERF_TYPE_BREAKPOINT,
		.size		= sizeof(attr),
		.sample_period	= 1,
		.disabled	= 1,
		.bp_addr	= (unsigned long)&ctx.iterate_on,
		.bp_type	= HW_BREAKPOINT_RW,
		.bp_len		= HW_BREAKPOINT_LEN_1,
		.inherit	= 1, /* Children inherit events ... */
		.inherit_thread = 1, /* ... but only cloned with CLONE_THREAD. */
		.remove_on_exec = 1, /* Required by sigtrap. */
		.sigtrap	= 1, /* Request synchronous SIGTRAP on event. */
		.sig_data	= TEST_SIG_DATA,
		.exclude_kernel = 1, /* To allow */
		.exclude_hv     = 1, /* running as !root */
	};
	return attr;
}

static void
sigtrap_handler(int signum __maybe_unused, siginfo_t *info, void *ucontext __maybe_unused)
{
	if (!__atomic_fetch_add(&ctx.signal_count, 1, __ATOMIC_RELAXED))
		ctx.first_siginfo = *info;
	__atomic_fetch_sub(&ctx.tids_want_signal, syscall(SYS_gettid), __ATOMIC_RELAXED);
}

static void *test_thread(void *arg)
{
	pthread_barrier_t *barrier = (pthread_barrier_t *)arg;
	pid_t tid = syscall(SYS_gettid);
	int i;

	pthread_barrier_wait(barrier);

	__atomic_fetch_add(&ctx.tids_want_signal, tid, __ATOMIC_RELAXED);
	for (i = 0; i < ctx.iterate_on - 1; i++)
		__atomic_fetch_add(&ctx.tids_want_signal, tid, __ATOMIC_RELAXED);

	return NULL;
}

static int run_test_threads(pthread_t *threads, pthread_barrier_t *barrier)
{
	int i;

	pthread_barrier_wait(barrier);
	for (i = 0; i < NUM_THREADS; i++)
		TEST_ASSERT_EQUAL("pthread_join() failed", pthread_join(threads[i], NULL), 0);

	return TEST_OK;
}

static int run_stress_test(int fd, pthread_t *threads, pthread_barrier_t *barrier)
{
	int ret;

	ctx.iterate_on = 3000;

	TEST_ASSERT_EQUAL("misfired signal?", ctx.signal_count, 0);
	TEST_ASSERT_EQUAL("enable failed", ioctl(fd, PERF_EVENT_IOC_ENABLE, 0), 0);
	ret = run_test_threads(threads, barrier);
	TEST_ASSERT_EQUAL("disable failed", ioctl(fd, PERF_EVENT_IOC_DISABLE, 0), 0);

	TEST_ASSERT_EQUAL("unexpected sigtraps", ctx.signal_count, NUM_THREADS * ctx.iterate_on);
	TEST_ASSERT_EQUAL("missing signals or incorrectly delivered", ctx.tids_want_signal, 0);
	TEST_ASSERT_VAL("unexpected si_addr", ctx.first_siginfo.si_addr == &ctx.iterate_on);
#if 0 /* FIXME: enable when libc's signal.h has si_perf_{type,data} */
	TEST_ASSERT_EQUAL("unexpected si_perf_type", ctx.first_siginfo.si_perf_type,
			  PERF_TYPE_BREAKPOINT);
	TEST_ASSERT_EQUAL("unexpected si_perf_data", ctx.first_siginfo.si_perf_data,
			  TEST_SIG_DATA);
#endif

	return ret;
}

static int test__sigtrap(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct perf_event_attr attr = make_event_attr();
	struct sigaction action = {};
	struct sigaction oldact;
	pthread_t threads[NUM_THREADS];
	pthread_barrier_t barrier;
	char sbuf[STRERR_BUFSIZE];
	int i, fd, ret = TEST_FAIL;

	if (!BP_SIGNAL_IS_SUPPORTED) {
		pr_debug("Test not supported on this architecture");
		return TEST_SKIP;
	}

	pthread_barrier_init(&barrier, NULL, NUM_THREADS + 1);

	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	action.sa_sigaction = sigtrap_handler;
	sigemptyset(&action.sa_mask);
	if (sigaction(SIGTRAP, &action, &oldact)) {
		pr_debug("FAILED sigaction(): %s\n", str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out;
	}

	fd = sys_perf_event_open(&attr, 0, -1, -1, perf_event_open_cloexec_flag());
	if (fd < 0) {
		pr_debug("FAILED sys_perf_event_open(): %s\n", str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_restore_sigaction;
	}

	for (i = 0; i < NUM_THREADS; i++) {
		if (pthread_create(&threads[i], NULL, test_thread, &barrier)) {
			pr_debug("FAILED pthread_create(): %s\n", str_error_r(errno, sbuf, sizeof(sbuf)));
			goto out_close_perf_event;
		}
	}

	ret = run_stress_test(fd, threads, &barrier);

out_close_perf_event:
	close(fd);
out_restore_sigaction:
	sigaction(SIGTRAP, &oldact, NULL);
out:
	pthread_barrier_destroy(&barrier);
	return ret;
}

DEFINE_SUITE("Sigtrap", sigtrap);
