// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2009
 *
 * DESCRIPTION
 *      Block on a futex and wait for timeout.
 *
 * AUTHOR
 *      Darren Hart <dvhart@linux.intel.com>
 *
 * HISTORY
 *      2009-Nov-6: Initial version by Darren Hart <dvhart@linux.intel.com>
 *      2021-Apr-26: More test cases by André Almeida <andrealmeid@collabora.com>
 *
 *****************************************************************************/

#include <pthread.h>

#include "futextest.h"
#include "futex2test.h"
#include "../../kselftest_harness.h"

static long timeout_ns = 100000;	/* 100us default timeout */
static futex_t futex_pi;
static pthread_barrier_t barrier;

/*
 * Get a PI lock and hold it forever, so the main thread lock_pi will block
 * and we can test the timeout
 */
void *get_pi_lock(void *arg)
{
	int ret;
	volatile futex_t lock = 0;

	ret = futex_lock_pi(&futex_pi, NULL, 0, 0);
	if (ret != 0)
		ksft_exit_fail_msg("futex_lock_pi failed\n");

	pthread_barrier_wait(&barrier);

	/* Blocks forever */
	ret = futex_wait(&lock, 0, NULL, 0);
	ksft_exit_fail_msg("futex_wait failed\n");

	return NULL;
}

/*
 * Check if the function returned the expected error
 */
static void test_timeout(int res, char *test_name, int err)
{
	if (!res || errno != err) {
		ksft_test_result_fail("%s returned %d\n", test_name,
				      res < 0 ? errno : res);
	} else {
		ksft_test_result_pass("%s succeeds\n", test_name);
	}
}

/*
 * Calculate absolute timeout and correct overflow
 */
static int futex_get_abs_timeout(clockid_t clockid, struct timespec *to,
				 long timeout_ns)
{
	if (clock_gettime(clockid, to))
		ksft_exit_fail_msg("clock_gettime failed\n");

	to->tv_nsec += timeout_ns;

	if (to->tv_nsec >= 1000000000) {
		to->tv_sec++;
		to->tv_nsec -= 1000000000;
	}

	return 0;
}

TEST(wait_bitset)
{
	futex_t f1 = FUTEX_INITIALIZER;
	struct timespec to;
	int res;

	/* initialize relative timeout */
	to.tv_sec = 0;
	to.tv_nsec = timeout_ns;

	res = futex_wait(&f1, f1, &to, 0);
	test_timeout(res, "futex_wait relative", ETIMEDOUT);

	/* FUTEX_WAIT_BITSET with CLOCK_REALTIME */
	if (futex_get_abs_timeout(CLOCK_REALTIME, &to, timeout_ns))
		ksft_test_result_error("get_time error");
	res = futex_wait_bitset(&f1, f1, &to, 1, FUTEX_CLOCK_REALTIME);
	test_timeout(res, "futex_wait_bitset realtime", ETIMEDOUT);

	/* FUTEX_WAIT_BITSET with CLOCK_MONOTONIC */
	if (futex_get_abs_timeout(CLOCK_MONOTONIC, &to, timeout_ns))
		ksft_test_result_error("get_time error");
	res = futex_wait_bitset(&f1, f1, &to, 1, 0);
	test_timeout(res, "futex_wait_bitset monotonic", ETIMEDOUT);
}

TEST(requeue_pi)
{
	futex_t f1 = FUTEX_INITIALIZER;
	struct timespec to;
	int res;

	/* FUTEX_WAIT_REQUEUE_PI with CLOCK_REALTIME */
	if (futex_get_abs_timeout(CLOCK_REALTIME, &to, timeout_ns))
		ksft_test_result_error("get_time error");
	res = futex_wait_requeue_pi(&f1, f1, &futex_pi, &to, FUTEX_CLOCK_REALTIME);
	test_timeout(res, "futex_wait_requeue_pi realtime", ETIMEDOUT);

	/* FUTEX_WAIT_REQUEUE_PI with CLOCK_MONOTONIC */
	if (futex_get_abs_timeout(CLOCK_MONOTONIC, &to, timeout_ns))
		ksft_test_result_error("get_time error");
	res = futex_wait_requeue_pi(&f1, f1, &futex_pi, &to, 0);
	test_timeout(res, "futex_wait_requeue_pi monotonic", ETIMEDOUT);

}

TEST(lock_pi)
{
	struct timespec to;
	pthread_t thread;
	int res;

	/* Create a thread that will lock forever so any waiter will timeout */
	pthread_barrier_init(&barrier, NULL, 2);
	pthread_create(&thread, NULL, get_pi_lock, NULL);

	/* Wait until the other thread calls futex_lock_pi() */
	pthread_barrier_wait(&barrier);
	pthread_barrier_destroy(&barrier);

	/*
	 * FUTEX_LOCK_PI with CLOCK_REALTIME
	 * Due to historical reasons, FUTEX_LOCK_PI supports only realtime
	 * clock, but requires the caller to not set CLOCK_REALTIME flag.
	 *
	 * If you call FUTEX_LOCK_PI with a monotonic clock, it'll be
	 * interpreted as a realtime clock, and (unless you mess your machine's
	 * time or your time machine) the monotonic clock value is always
	 * smaller than realtime and the syscall will timeout immediately.
	 */
	if (futex_get_abs_timeout(CLOCK_REALTIME, &to, timeout_ns))
		ksft_test_result_error("get_time error");
	res = futex_lock_pi(&futex_pi, &to, 0, 0);
	test_timeout(res, "futex_lock_pi realtime", ETIMEDOUT);

	/* Test operations that don't support FUTEX_CLOCK_REALTIME */
	res = futex_lock_pi(&futex_pi, NULL, 0, FUTEX_CLOCK_REALTIME);
	test_timeout(res, "futex_lock_pi invalid timeout flag", ENOSYS);
}

TEST(waitv)
{
	futex_t f1 = FUTEX_INITIALIZER;
	struct futex_waitv waitv = {
		.uaddr		= (uintptr_t)&f1,
		.val		= f1,
		.flags		= FUTEX_32,
		.__reserved	= 0,
	};
	struct timespec to;
	int res;

	/* futex_waitv with CLOCK_MONOTONIC */
	if (futex_get_abs_timeout(CLOCK_MONOTONIC, &to, timeout_ns))
		ksft_test_result_error("get_time error");
	res = futex_waitv(&waitv, 1, 0, &to, CLOCK_MONOTONIC);
	test_timeout(res, "futex_waitv monotonic", ETIMEDOUT);

	/* futex_waitv with CLOCK_REALTIME */
	if (futex_get_abs_timeout(CLOCK_REALTIME, &to, timeout_ns))
		ksft_test_result_error("get_time error");
	res = futex_waitv(&waitv, 1, 0, &to, CLOCK_REALTIME);
	test_timeout(res, "futex_waitv realtime", ETIMEDOUT);
}

TEST_HARNESS_MAIN
