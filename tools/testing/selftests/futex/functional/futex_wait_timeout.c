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
#include "kselftest_harness.h"

static long timeout_ns = 100000;	/* 100us default timeout */
static futex_t futex_pi;
static pthread_barrier_t barrier;

/*
 * Get a PI lock and hold it forever, so the main thread lock_pi will block
 * and we can test the timeout
 */
void *get_pi_lock(void *arg)
{
	struct __test_metadata *_metadata = (struct __test_metadata *)arg;
	int ret;
	volatile futex_t lock = 0;

	ret = futex_lock_pi(&futex_pi, NULL, 0, 0);
	ASSERT_EQ(ret, 0)
		TH_LOG("futex_lock_pi failed");

	pthread_barrier_wait(&barrier);

	/* Blocks forever */
	ret = futex_wait(&lock, 0, NULL, 0);
	ASSERT_TRUE(0)
		TH_LOG("futex_wait returned unexpectedly: %d", ret);

	return NULL;
}

#define TEST_TIMEOUT(_res, _test_name, _err) do {				\
	if ((_res) < 0 && errno == ENOSYS && (_err) != ENOSYS) {		\
		SKIP(return, "%s is not supported (ENOSYS)", _test_name);	\
	}									\
	EXPECT_EQ((_res), -1)							\
		TH_LOG("%s returned unexpected result: %d", _test_name, (_res));\
	if ((_res) == -1) {							\
		EXPECT_EQ(errno, (_err)) {					\
			TH_LOG("%s returned unexpected errno: %d (expected %d)",\
			       _test_name, errno, (_err));			\
		}								\
	}									\
} while (0)

#define GET_ABS_TIMEOUT(_clockid, _to, _timeout_ns) do {		\
	ASSERT_EQ(clock_gettime((_clockid), (_to)), 0)			\
		TH_LOG("clock_gettime failed");				\
	(_to)->tv_nsec += (_timeout_ns);				\
	if ((_to)->tv_nsec >= 1000000000) {				\
		(_to)->tv_sec++;					\
		(_to)->tv_nsec -= 1000000000;				\
	}								\
} while (0)

TEST(wait_bitset)
{
	futex_t f1 = FUTEX_INITIALIZER;
	struct timespec to;
	int res;

	/* initialize relative timeout */
	to.tv_sec = 0;
	to.tv_nsec = timeout_ns;

	res = futex_wait(&f1, f1, &to, 0);
	TEST_TIMEOUT(res, "futex_wait relative", ETIMEDOUT);

	/* FUTEX_WAIT_BITSET with CLOCK_REALTIME */
	GET_ABS_TIMEOUT(CLOCK_REALTIME, &to, timeout_ns);
	res = futex_wait_bitset(&f1, f1, &to, 1, FUTEX_CLOCK_REALTIME);
	TEST_TIMEOUT(res, "futex_wait_bitset realtime", ETIMEDOUT);

	/* FUTEX_WAIT_BITSET with CLOCK_MONOTONIC */
	GET_ABS_TIMEOUT(CLOCK_MONOTONIC, &to, timeout_ns);
	res = futex_wait_bitset(&f1, f1, &to, 1, 0);
	TEST_TIMEOUT(res, "futex_wait_bitset monotonic", ETIMEDOUT);
}

TEST(requeue_pi)
{
	futex_t f1 = FUTEX_INITIALIZER;
	struct timespec to;
	int res;

	/* FUTEX_WAIT_REQUEUE_PI with CLOCK_REALTIME */
	GET_ABS_TIMEOUT(CLOCK_REALTIME, &to, timeout_ns);
	res = futex_wait_requeue_pi(&f1, f1, &futex_pi, &to, FUTEX_CLOCK_REALTIME);
	TEST_TIMEOUT(res, "futex_wait_requeue_pi realtime", ETIMEDOUT);

	/* FUTEX_WAIT_REQUEUE_PI with CLOCK_MONOTONIC */
	GET_ABS_TIMEOUT(CLOCK_MONOTONIC, &to, timeout_ns);
	res = futex_wait_requeue_pi(&f1, f1, &futex_pi, &to, 0);
	TEST_TIMEOUT(res, "futex_wait_requeue_pi monotonic", ETIMEDOUT);
}

TEST(lock_pi)
{
	struct timespec to;
	pthread_t thread;
	int res;

	/* Create a thread that will lock forever so any waiter will timeout */
	pthread_barrier_init(&barrier, NULL, 2);
	ASSERT_EQ(pthread_create(&thread, NULL, get_pi_lock, _metadata), 0)
		TH_LOG("pthread_create failed");

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
	GET_ABS_TIMEOUT(CLOCK_REALTIME, &to, timeout_ns);
	res = futex_lock_pi(&futex_pi, &to, 0, 0);
	TEST_TIMEOUT(res, "futex_lock_pi realtime", ETIMEDOUT);

	/* Test operations that don't support FUTEX_CLOCK_REALTIME */
	res = futex_lock_pi(&futex_pi, NULL, 0, FUTEX_CLOCK_REALTIME);
	TEST_TIMEOUT(res, "futex_lock_pi invalid timeout flag", ENOSYS);
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

	if (!is_futex_waitv_supported())
		SKIP(return, "futex_waitv syscall not supported");

	/* futex_waitv with CLOCK_MONOTONIC */
	GET_ABS_TIMEOUT(CLOCK_MONOTONIC, &to, timeout_ns);
	res = futex_waitv(&waitv, 1, 0, &to, CLOCK_MONOTONIC);
	TEST_TIMEOUT(res, "futex_waitv monotonic", ETIMEDOUT);

	/* futex_waitv with CLOCK_REALTIME */
	GET_ABS_TIMEOUT(CLOCK_REALTIME, &to, timeout_ns);
	res = futex_waitv(&waitv, 1, 0, &to, CLOCK_REALTIME);
	TEST_TIMEOUT(res, "futex_waitv realtime", ETIMEDOUT);
}

TEST_HARNESS_MAIN
