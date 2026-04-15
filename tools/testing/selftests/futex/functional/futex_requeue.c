// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright Collabora Ltd., 2021
 *
 * futex cmp requeue test by André Almeida <andrealmeid@collabora.com>
 */

#include <pthread.h>
#include <limits.h>

#include "futextest.h"
#include "kselftest_harness.h"

#define timeout_ns  30000000
#define WAKE_WAIT_US 10000

volatile futex_t *f1;

void *waiterfn(void *arg)
{
	struct timespec to;

	to.tv_sec = 0;
	to.tv_nsec = timeout_ns;

	if (futex_wait(f1, *f1, &to, 0))
		printf("waiter failed errno %d\n", errno);

	return NULL;
}

TEST(requeue_single)
{
	volatile futex_t _f1 = 0;
	volatile futex_t f2 = 0;
	pthread_t waiter[10];

	f1 = &_f1;

	/*
	 * Requeue a waiter from f1 to f2, and wake f2.
	 */
	ASSERT_EQ(0, pthread_create(&waiter[0], NULL, waiterfn, NULL));

	usleep(WAKE_WAIT_US);

	EXPECT_EQ(1, futex_cmp_requeue(f1, 0, &f2, 0, 1, 0));
	EXPECT_EQ(1, futex_wake(&f2, 1, 0));
}

TEST(requeue_multiple)
{
	volatile futex_t _f1 = 0;
	volatile futex_t f2 = 0;
	pthread_t waiter[10];
	int i;

	f1 = &_f1;

	/*
	 * Create 10 waiters at f1. At futex_requeue, wake 3 and requeue 7.
	 * At futex_wake, wake INT_MAX (should be exactly 7).
	 */
	for (i = 0; i < 10; i++)
		ASSERT_EQ(0, pthread_create(&waiter[i], NULL, waiterfn, NULL));

	usleep(WAKE_WAIT_US);

	EXPECT_EQ(10, futex_cmp_requeue(f1, 0, &f2, 3, 7, 0));
	EXPECT_EQ(7, futex_wake(&f2, INT_MAX, 0));
}

TEST_HARNESS_MAIN
