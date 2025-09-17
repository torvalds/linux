// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2009
 *
 * DESCRIPTION
 *      Test if FUTEX_WAIT op returns -EWOULDBLOCK if the futex value differs
 *      from the expected one.
 *
 * AUTHOR
 *      Gowrishankar <gowrishankar.m@in.ibm.com>
 *
 * HISTORY
 *      2009-Nov-14: Initial version by Gowrishankar <gowrishankar.m@in.ibm.com>
 *
 *****************************************************************************/

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "futextest.h"
#include "futex2test.h"
#include "../../kselftest_harness.h"

#define timeout_ns 100000

TEST(futex_wait_wouldblock)
{
	struct timespec to = {.tv_sec = 0, .tv_nsec = timeout_ns};
	futex_t f1 = FUTEX_INITIALIZER;
	int res;

	ksft_print_dbg_msg("Calling futex_wait on f1: %u @ %p with val=%u\n", f1, &f1, f1+1);
	res = futex_wait(&f1, f1+1, &to, FUTEX_PRIVATE_FLAG);
	if (!res || errno != EWOULDBLOCK) {
		ksft_test_result_fail("futex_wait returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_wait\n");
	}
}

TEST(futex_waitv_wouldblock)
{
	struct timespec to = {.tv_sec = 0, .tv_nsec = timeout_ns};
	futex_t f1 = FUTEX_INITIALIZER;
	struct futex_waitv waitv = {
		.uaddr		= (uintptr_t)&f1,
		.val		= f1 + 1,
		.flags		= FUTEX_32,
		.__reserved	= 0,
	};
	int res;

	if (clock_gettime(CLOCK_MONOTONIC, &to))
		ksft_exit_fail_msg("clock_gettime failed %d\n", errno);

	to.tv_nsec += timeout_ns;

	if (to.tv_nsec >= 1000000000) {
		to.tv_sec++;
		to.tv_nsec -= 1000000000;
	}

	ksft_print_dbg_msg("Calling futex_waitv on f1: %u @ %p with val=%u\n", f1, &f1, f1+1);
	res = futex_waitv(&waitv, 1, 0, &to, CLOCK_MONOTONIC);
	if (!res || errno != EWOULDBLOCK) {
		ksft_test_result_fail("futex_waitv returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_waitv\n");
	}
}

TEST_HARNESS_MAIN
