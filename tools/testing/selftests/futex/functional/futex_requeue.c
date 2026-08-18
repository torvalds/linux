// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright Collabora Ltd., 2021
 *
 * futex cmp requeue test by André Almeida <andrealmeid@collabora.com>
 */

#include <limits.h>
#include <pthread.h>
#include <string.h>

#include "futextest.h"
#include "futex_thread.h"
#include "kselftest_harness.h"

struct waiter_args {
	struct __test_metadata	*_metadata;
	unsigned int		n_threads;
};

volatile futex_t *f1;

static int waiterfn(void *arg)
{
	struct __test_metadata *_metadata;
	struct waiter_args *wargs = arg;
	struct timespec to = { };
	int res;

	_metadata = wargs->_metadata;
	to.tv_sec = (wargs->n_threads + 1) * WAIT_FOR_THREAD_SECS;

	res = futex_wait(f1, *f1, &to, 0);
	if (res) {
		EXPECT_EQ(res, 0)
			TH_LOG("waiter failed errno %d: %s", errno, strerror(errno));
	}

	return 0;
}

TEST(requeue_single)
{
	struct waiter_args wargs = { ._metadata = _metadata, .n_threads = 1 };
	struct futex_thread waiter;
	volatile futex_t _f1 = 0;
	volatile futex_t f2 = 0;

	f1 = &_f1;

	/*
	 * Requeue a waiter from f1 to f2, and wake f2.
	 */
	ASSERT_EQ(futex_thread_create(&waiter, waiterfn, &wargs), 0)
		TH_LOG("pthread_create failed");

	ASSERT_EQ(futex_wait_for_thread(&waiter, _metadata), 0)
		TH_LOG("Wait for thread failed");

	EXPECT_EQ(futex_cmp_requeue(f1, 0, &f2, 0, 1, 0), 1);
	EXPECT_EQ(futex_wake(&f2, 1, 0), 1);

	EXPECT_EQ(futex_thread_destroy(&waiter), 0);
}

TEST(requeue_multiple)
{
	struct waiter_args wargs = { ._metadata = _metadata, .n_threads = 10 };
	struct futex_thread waiter[10];
	volatile futex_t _f1 = 0;
	volatile futex_t f2 = 0;

	f1 = &_f1;

	/*
	 * Create 10 waiters at f1. At futex_requeue, wake 3 and requeue 7.
	 * At futex_wake, wake INT_MAX (should be exactly 7).
	 */
	for (int i = 0; i < 10; i++) {
		ASSERT_EQ(futex_thread_create(&waiter[i], waiterfn, &wargs), 0)
			TH_LOG("pthread_create failed for waiter %d", i);
	}

	for (int i = 0; i < 10; i++) {
		ASSERT_EQ(futex_wait_for_thread(&waiter[i], _metadata), 0)
			TH_LOG("Wait for waiter thread %d failed", i);
	}

	EXPECT_EQ(futex_cmp_requeue(f1, 0, &f2, 3, 7, 0), 10);
	EXPECT_EQ(futex_wake(&f2, INT_MAX, 0), 7);

	for (int i = 0; i < 10; i++)
		EXPECT_EQ(futex_thread_destroy(&waiter[i]), 0);
}

TEST_HARNESS_MAIN
