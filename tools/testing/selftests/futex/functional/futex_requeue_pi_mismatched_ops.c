// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2009
 *
 * DESCRIPTION
 *      1. Block a thread using FUTEX_WAIT
 *      2. Attempt to use FUTEX_CMP_REQUEUE_PI on the futex from 1.
 *      3. The kernel must detect the mismatch and return -EINVAL.
 *
 * AUTHOR
 *      Darren Hart <dvhart@linux.intel.com>
 *
 * HISTORY
 *      2009-Nov-9: Initial version by Darren Hart <dvhart@linux.intel.com>
 *
 *****************************************************************************/

#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "futextest.h"
#include "kselftest_harness.h"

futex_t f1 = FUTEX_INITIALIZER;
futex_t f2 = FUTEX_INITIALIZER;
int child_ret;

void *blocking_child(void *arg)
{
	struct __test_metadata *_metadata = (struct __test_metadata *)arg;

	child_ret = futex_wait(&f1, f1, NULL, FUTEX_PRIVATE_FLAG);
	if (child_ret < 0) {
		child_ret = -errno;
		ASSERT_EQ(child_ret, 0)
			TH_LOG("futex_wait failed: %s", strerror(errno));
	}
	return (void *)&child_ret;
}

TEST(requeue_pi_mismatched_ops)
{
	pthread_t child;
	int ret;

	ASSERT_EQ(pthread_create(&child, NULL, blocking_child, _metadata), 0)
		TH_LOG("pthread_create failed");

	/* Allow the child to block in the kernel. */
	sleep(1);

	/*
	 * The kernel should detect the waiter did not setup the
	 * q->requeue_pi_key and return -EINVAL. If it does not,
	 * it likely gave the lock to the child, which is now hung
	 * in the kernel.
	 */
	ret = futex_cmp_requeue_pi(&f1, f1, &f2, 1, 0, FUTEX_PRIVATE_FLAG);
	if (ret < 0) {
		if (errno == EINVAL) {
			/*
			 * The kernel correctly detected the mismatched
			 * requeue_pi target and aborted. Wake the child with
			 * FUTEX_WAKE.
			 */
			ret = futex_wake(&f1, 1, FUTEX_PRIVATE_FLAG);
			if (ret == 1) {
				ret = 0;
			} else if (ret < 0) {
				ASSERT_GE(ret, 0)
					TH_LOG("futex_wake failed: %s", strerror(errno));
			} else {
				ASSERT_TRUE(0)
					TH_LOG("futex_wake did not wake the child");
			}
		} else {
			ASSERT_TRUE(0)
				TH_LOG("futex_cmp_requeue_pi failed with unexpected errno: %s", strerror(errno));
		}
	} else if (ret > 0) {
		EXPECT_EQ(ret, 0)
			TH_LOG("futex_cmp_requeue_pi failed to detect the mismatch");
	} else {
		ASSERT_TRUE(0)
			TH_LOG("futex_cmp_requeue_pi found no waiters");
	}

	pthread_join(child, NULL);

	EXPECT_EQ(ret, 0)
		TH_LOG("Test failed: ret=%d", ret);
	EXPECT_EQ(child_ret, 0)
		TH_LOG("Child failed: child_ret=%d", child_ret);
}

TEST_HARNESS_MAIN
