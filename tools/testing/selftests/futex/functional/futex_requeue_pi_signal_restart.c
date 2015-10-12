/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2006-2008
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
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
#include "logging.h"

#define DELAY_US 100

futex_t f1 = FUTEX_INITIALIZER;
futex_t f2 = FUTEX_INITIALIZER;
atomic_t requeued = ATOMIC_INITIALIZER;

int waiter_ret = 0;

void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -c	Use color\n");
	printf("  -h	Display this help message\n");
	printf("  -v L	Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

int create_rt_thread(pthread_t *pth, void*(*func)(void *), void *arg,
		     int policy, int prio)
{
	struct sched_param schedp;
	pthread_attr_t attr;
	int ret;

	pthread_attr_init(&attr);
	memset(&schedp, 0, sizeof(schedp));

	ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
	if (ret) {
		error("pthread_attr_setinheritsched\n", ret);
		return -1;
	}

	ret = pthread_attr_setschedpolicy(&attr, policy);
	if (ret) {
		error("pthread_attr_setschedpolicy\n", ret);
		return -1;
	}

	schedp.sched_priority = prio;
	ret = pthread_attr_setschedparam(&attr, &schedp);
	if (ret) {
		error("pthread_attr_setschedparam\n", ret);
		return -1;
	}

	ret = pthread_create(pth, &attr, func, arg);
	if (ret) {
		error("pthread_create\n", ret);
		return -1;
	}
	return 0;
}

void handle_signal(int signo)
{
	info("signal received %s requeue\n",
	     requeued.val ? "after" : "prior to");
}

void *waiterfn(void *arg)
{
	unsigned int old_val;
	int res;

	waiter_ret = RET_PASS;

	info("Waiter running\n");
	info("Calling FUTEX_LOCK_PI on f2=%x @ %p\n", f2, &f2);
	old_val = f1;
	res = futex_wait_requeue_pi(&f1, old_val, &(f2), NULL,
				    FUTEX_PRIVATE_FLAG);
	if (!requeued.val || errno != EWOULDBLOCK) {
		fail("unexpected return from futex_wait_requeue_pi: %d (%s)\n",
		     res, strerror(errno));
		info("w2:futex: %x\n", f2);
		if (!res)
			futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);
		waiter_ret = RET_FAIL;
	}

	info("Waiter exiting with %d\n", waiter_ret);
	pthread_exit(NULL);
}


int main(int argc, char *argv[])
{
	unsigned int old_val;
	struct sigaction sa;
	pthread_t waiter;
	int c, res, ret = RET_PASS;

	while ((c = getopt(argc, argv, "chv:")) != -1) {
		switch (c) {
		case 'c':
			log_color(1);
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(0);
		case 'v':
			log_verbosity(atoi(optarg));
			break;
		default:
			usage(basename(argv[0]));
			exit(1);
		}
	}

	printf("%s: Test signal handling during requeue_pi\n",
	       basename(argv[0]));
	printf("\tArguments: <none>\n");

	sa.sa_handler = handle_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(SIGUSR1, &sa, NULL)) {
		error("sigaction\n", errno);
		exit(1);
	}

	info("m1:f2: %x\n", f2);
	info("Creating waiter\n");
	res = create_rt_thread(&waiter, waiterfn, NULL, SCHED_FIFO, 1);
	if (res) {
		error("Creating waiting thread failed", res);
		ret = RET_ERROR;
		goto out;
	}

	info("Calling FUTEX_LOCK_PI on f2=%x @ %p\n", f2, &f2);
	info("m2:f2: %x\n", f2);
	futex_lock_pi(&f2, 0, 0, FUTEX_PRIVATE_FLAG);
	info("m3:f2: %x\n", f2);

	while (1) {
		/*
		 * signal the waiter before requeue, waiter should automatically
		 * restart futex_wait_requeue_pi() in the kernel. Wait for the
		 * waiter to block on f1 again.
		 */
		info("Issuing SIGUSR1 to waiter\n");
		pthread_kill(waiter, SIGUSR1);
		usleep(DELAY_US);

		info("Requeueing waiter via FUTEX_CMP_REQUEUE_PI\n");
		old_val = f1;
		res = futex_cmp_requeue_pi(&f1, old_val, &(f2), 1, 0,
					   FUTEX_PRIVATE_FLAG);
		/*
		 * If res is non-zero, we either requeued the waiter or hit an
		 * error, break out and handle it. If it is zero, then the
		 * signal may have hit before the the waiter was blocked on f1.
		 * Try again.
		 */
		if (res > 0) {
			atomic_set(&requeued, 1);
			break;
		} else if (res < 0) {
			error("FUTEX_CMP_REQUEUE_PI failed\n", errno);
			ret = RET_ERROR;
			break;
		}
	}
	info("m4:f2: %x\n", f2);

	/*
	 * Signal the waiter after requeue, waiter should return from
	 * futex_wait_requeue_pi() with EWOULDBLOCK. Join the thread here so the
	 * futex_unlock_pi() can't happen before the signal wakeup is detected
	 * in the kernel.
	 */
	info("Issuing SIGUSR1 to waiter\n");
	pthread_kill(waiter, SIGUSR1);
	info("Waiting for waiter to return\n");
	pthread_join(waiter, NULL);

	info("Calling FUTEX_UNLOCK_PI on mutex=%x @ %p\n", f2, &f2);
	futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);
	info("m5:f2: %x\n", f2);

 out:
	if (ret == RET_PASS && waiter_ret)
		ret = waiter_ret;

	print_result(ret);
	return ret;
}
