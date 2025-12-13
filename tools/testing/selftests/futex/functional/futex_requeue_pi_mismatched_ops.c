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
#include "../../kselftest_harness.h"

futex_t f1 = FUTEX_INITIALIZER;
futex_t f2 = FUTEX_INITIALIZER;
int child_ret = 0;

void *blocking_child(void *arg)
{
	child_ret = futex_wait(&f1, f1, NULL, FUTEX_PRIVATE_FLAG);
	if (child_ret < 0) {
		child_ret = -errno;
		ksft_exit_fail_msg("futex_wait\n");
	}
	return (void *)&child_ret;
}

TEST(requeue_pi_mismatched_ops)
{
	pthread_t child;
	int ret;

	if (pthread_create(&child, NULL, blocking_child, NULL))
		ksft_exit_fail_msg("pthread_create\n");

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
			if (ret == 1)
				ret = 0;
			else if (ret < 0)
				ksft_exit_fail_msg("futex_wake\n");
			else
				ksft_exit_fail_msg("futex_wake did not wake the child\n");
		} else {
			ksft_exit_fail_msg("futex_cmp_requeue_pi\n");
		}
	} else if (ret > 0) {
		ksft_test_result_fail("futex_cmp_requeue_pi failed to detect the mismatch\n");
	} else {
		ksft_exit_fail_msg("futex_cmp_requeue_pi found no waiters\n");
	}

	pthread_join(child, NULL);

	if (!ret && !child_ret)
		ksft_test_result_pass("futex_requeue_pi_mismatched_ops passed\n");
	else
		ksft_test_result_pass("futex_requeue_pi_mismatched_ops failed\n");
}

TEST_HARNESS_MAIN
