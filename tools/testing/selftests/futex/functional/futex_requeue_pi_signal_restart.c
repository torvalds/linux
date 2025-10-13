// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2006-2008
 *
 * DESCRIPTION
 *      This test exercises the futex_wait_requeue_pi() signal handling both
 *      before and after the requeue. The first should be restarted by the
 *      kernel. The latter should return EWOULDBLOCK to the waiter.
 *
 * AUTHORS
 *      Darren Hart <dvhart@linux.intel.com>
 *
 * HISTORY
 *      2008-May-5: Initial version by Darren Hart <dvhart@linux.intel.com>
 *
 *****************************************************************************/

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atomic.h"
#include "futextest.h"
#include "../../kselftest_harness.h"

#define DELAY_US 100

futex_t f1 = FUTEX_INITIALIZER;
futex_t f2 = FUTEX_INITIALIZER;
atomic_t requeued = ATOMIC_INITIALIZER;

int waiter_ret = 0;

int create_rt_thread(pthread_t *pth, void*(*func)(void *), void *arg,
		     int policy, int prio)
{
	struct sched_param schedp;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	memset(&schedp, 0, sizeof(schedp));

	ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	if (ret)
		ksft_exit_fail_msg("pthread_attr_setinheritsched\n");

	ret = pthread_attr_setschedpolicy(&attr, policy);
	if (ret)
		ksft_exit_fail_msg("pthread_attr_setschedpolicy\n");

	schedp.sched_priority = prio;
	ret = pthread_attr_setschedparam(&attr, &schedp);
	if (ret)
		ksft_exit_fail_msg("pthread_attr_setschedparam\n");

	ret = pthread_create(pth, &attr, func, arg);
	if (ret)
		ksft_exit_fail_msg("pthread_create\n");

	return 0;
}

void handle_signal(int signo)
{
	ksft_print_dbg_msg("signal received %s requeue\n",
	     requeued.val ? "after" : "prior to");
}

void *waiterfn(void *arg)
{
	unsigned int old_val;
	int res;

	ksft_print_dbg_msg("Waiter running\n");
	ksft_print_dbg_msg("Calling FUTEX_LOCK_PI on f2=%x @ %p\n", f2, &f2);
	old_val = f1;
	res = futex_wait_requeue_pi(&f1, old_val, &(f2), NULL,
				    FUTEX_PRIVATE_FLAG);
	if (!requeued.val || errno != EWOULDBLOCK) {
		ksft_test_result_fail("unexpected return from futex_wait_requeue_pi: %d (%s)\n",
		     res, strerror(errno));
		ksft_print_dbg_msg("w2:futex: %x\n", f2);
		if (!res)
			futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);
	}

	pthread_exit(NULL);
}


TEST(futex_requeue_pi_signal_restart)
{
	unsigned int old_val;
	struct sigaction sa;
	pthread_t waiter;
	int res;

	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGUSR1, &sa, NULL))
		ksft_exit_fail_msg("sigaction\n");

	ksft_print_dbg_msg("m1:f2: %x\n", f2);
	ksft_print_dbg_msg("Creating waiter\n");
	res = create_rt_thread(&waiter, waiterfn, NULL, SCHED_FIFO, 1);
	if (res)
		ksft_exit_fail_msg("Creating waiting thread failed");

	ksft_print_dbg_msg("Calling FUTEX_LOCK_PI on f2=%x @ %p\n", f2, &f2);
	ksft_print_dbg_msg("m2:f2: %x\n", f2);
	futex_lock_pi(&f2, 0, 0, FUTEX_PRIVATE_FLAG);
	ksft_print_dbg_msg("m3:f2: %x\n", f2);

	while (1) {
		/*
		 * signal the waiter before requeue, waiter should automatically
		 * restart futex_wait_requeue_pi() in the kernel. Wait for the
		 * waiter to block on f1 again.
		 */
		ksft_print_dbg_msg("Issuing SIGUSR1 to waiter\n");
		pthread_kill(waiter, SIGUSR1);
		usleep(DELAY_US);

		ksft_print_dbg_msg("Requeueing waiter via FUTEX_CMP_REQUEUE_PI\n");
		old_val = f1;
		res = futex_cmp_requeue_pi(&f1, old_val, &(f2), 1, 0,
					   FUTEX_PRIVATE_FLAG);
		/*
		 * If res is non-zero, we either requeued the waiter or hit an
		 * error, break out and handle it. If it is zero, then the
		 * signal may have hit before the waiter was blocked on f1.
		 * Try again.
		 */
		if (res > 0) {
			atomic_set(&requeued, 1);
			break;
		} else if (res < 0) {
			ksft_exit_fail_msg("FUTEX_CMP_REQUEUE_PI failed\n");
		}
	}
	ksft_print_dbg_msg("m4:f2: %x\n", f2);

	/*
	 * Signal the waiter after requeue, waiter should return from
	 * futex_wait_requeue_pi() with EWOULDBLOCK. Join the thread here so the
	 * futex_unlock_pi() can't happen before the signal wakeup is detected
	 * in the kernel.
	 */
	ksft_print_dbg_msg("Issuing SIGUSR1 to waiter\n");
	pthread_kill(waiter, SIGUSR1);
	ksft_print_dbg_msg("Waiting for waiter to return\n");
	pthread_join(waiter, NULL);

	ksft_print_dbg_msg("Calling FUTEX_UNLOCK_PI on mutex=%x @ %p\n", f2, &f2);
	futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);
	ksft_print_dbg_msg("m5:f2: %x\n", f2);
}

TEST_HARNESS_MAIN
