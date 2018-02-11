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
 *      This test excercises the futex syscall op codes needed for requeuing
 *      priority inheritance aware POSIX condition variables and mutexes.
 *
 * AUTHORS
 *      Sripathi Kodi <sripathik@in.ibm.com>
 *      Darren Hart <dvhart@linux.intel.com>
 *
 * HISTORY
 *      2008-Jan-13: Initial version by Sripathi Kodi <sripathik@in.ibm.com>
 *      2009-Nov-6: futex test adaptation by Darren Hart <dvhart@linux.intel.com>
 *
 *****************************************************************************/

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include "atomic.h"
#include "futextest.h"
#include "logging.h"

#define TEST_NAME "futex-requeue-pi"
#define MAX_WAKE_ITERS 1000
#define THREAD_MAX 10
#define SIGNAL_PERIOD_US 100

atomic_t waiters_blocked = ATOMIC_INITIALIZER;
atomic_t waiters_woken = ATOMIC_INITIALIZER;

futex_t f1 = FUTEX_INITIALIZER;
futex_t f2 = FUTEX_INITIALIZER;
futex_t wake_complete = FUTEX_INITIALIZER;

/* Test option defaults */
static long timeout_ns;
static int broadcast;
static int owner;
static int locked;

struct thread_arg {
	long id;
	struct timespec *timeout;
	int lock;
	int ret;
};
#define THREAD_ARG_INITIALIZER { 0, NULL, 0, 0 }

void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -b	Broadcast wakeup (all waiters)\n");
	printf("  -c	Use color\n");
	printf("  -h	Display this help message\n");
	printf("  -l	Lock the pi futex across requeue\n");
	printf("  -o	Use a third party pi futex owner during requeue (cancels -l)\n");
	printf("  -t N	Timeout in nanoseconds (default: 0)\n");
	printf("  -v L	Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

int create_rt_thread(pthread_t *pth, void*(*func)(void *), void *arg,
		     int policy, int prio)
{
	int ret;
	struct sched_param schedp;
	pthread_attr_t attr;

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


void *waiterfn(void *arg)
{
	struct thread_arg *args = (struct thread_arg *)arg;
	futex_t old_val;

	info("Waiter %ld: running\n", args->id);
	/* Each thread sleeps for a different amount of time
	 * This is to avoid races, because we don't lock the
	 * external mutex here */
	usleep(1000 * (long)args->id);

	old_val = f1;
	atomic_inc(&waiters_blocked);
	info("Calling futex_wait_requeue_pi: %p (%u) -> %p\n",
	     &f1, f1, &f2);
	args->ret = futex_wait_requeue_pi(&f1, old_val, &f2, args->timeout,
					  FUTEX_PRIVATE_FLAG);

	info("waiter %ld woke with %d %s\n", args->id, args->ret,
	     args->ret < 0 ? strerror(errno) : "");
	atomic_inc(&waiters_woken);
	if (args->ret < 0) {
		if (args->timeout && errno == ETIMEDOUT)
			args->ret = 0;
		else {
			args->ret = RET_ERROR;
			error("futex_wait_requeue_pi\n", errno);
		}
		futex_lock_pi(&f2, NULL, 0, FUTEX_PRIVATE_FLAG);
	}
	futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);

	info("Waiter %ld: exiting with %d\n", args->id, args->ret);
	pthread_exit((void *)&args->ret);
}

void *broadcast_wakerfn(void *arg)
{
	struct thread_arg *args = (struct thread_arg *)arg;
	int nr_requeue = INT_MAX;
	int task_count = 0;
	futex_t old_val;
	int nr_wake = 1;
	int i = 0;

	info("Waker: waiting for waiters to block\n");
	while (waiters_blocked.val < THREAD_MAX)
		usleep(1000);
	usleep(1000);

	info("Waker: Calling broadcast\n");
	if (args->lock) {
		info("Calling FUTEX_LOCK_PI on mutex=%x @ %p\n", f2, &f2);
		futex_lock_pi(&f2, NULL, 0, FUTEX_PRIVATE_FLAG);
	}
 continue_requeue:
	old_val = f1;
	args->ret = futex_cmp_requeue_pi(&f1, old_val, &f2, nr_wake, nr_requeue,
				   FUTEX_PRIVATE_FLAG);
	if (args->ret < 0) {
		args->ret = RET_ERROR;
		error("FUTEX_CMP_REQUEUE_PI failed\n", errno);
	} else if (++i < MAX_WAKE_ITERS) {
		task_count += args->ret;
		if (task_count < THREAD_MAX - waiters_woken.val)
			goto continue_requeue;
	} else {
		error("max broadcast iterations (%d) reached with %d/%d tasks woken or requeued\n",
		       0, MAX_WAKE_ITERS, task_count, THREAD_MAX);
		args->ret = RET_ERROR;
	}

	futex_wake(&wake_complete, 1, FUTEX_PRIVATE_FLAG);

	if (args->lock)
		futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);

	if (args->ret > 0)
		args->ret = task_count;

	info("Waker: exiting with %d\n", args->ret);
	pthread_exit((void *)&args->ret);
}

void *signal_wakerfn(void *arg)
{
	struct thread_arg *args = (struct thread_arg *)arg;
	unsigned int old_val;
	int nr_requeue = 0;
	int task_count = 0;
	int nr_wake = 1;
	int i = 0;

	info("Waker: waiting for waiters to block\n");
	while (waiters_blocked.val < THREAD_MAX)
		usleep(1000);
	usleep(1000);

	while (task_count < THREAD_MAX && waiters_woken.val < THREAD_MAX) {
		info("task_count: %d, waiters_woken: %d\n",
		     task_count, waiters_woken.val);
		if (args->lock) {
			info("Calling FUTEX_LOCK_PI on mutex=%x @ %p\n",
			     f2, &f2);
			futex_lock_pi(&f2, NULL, 0, FUTEX_PRIVATE_FLAG);
		}
		info("Waker: Calling signal\n");
		/* cond_signal */
		old_val = f1;
		args->ret = futex_cmp_requeue_pi(&f1, old_val, &f2,
						 nr_wake, nr_requeue,
						 FUTEX_PRIVATE_FLAG);
		if (args->ret < 0)
			args->ret = -errno;
		info("futex: %x\n", f2);
		if (args->lock) {
			info("Calling FUTEX_UNLOCK_PI on mutex=%x @ %p\n",
			     f2, &f2);
			futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);
		}
		info("futex: %x\n", f2);
		if (args->ret < 0) {
			error("FUTEX_CMP_REQUEUE_PI failed\n", errno);
			args->ret = RET_ERROR;
			break;
		}

		task_count += args->ret;
		usleep(SIGNAL_PERIOD_US);
		i++;
		/* we have to loop at least THREAD_MAX times */
		if (i > MAX_WAKE_ITERS + THREAD_MAX) {
			error("max signaling iterations (%d) reached, giving up on pending waiters.\n",
			      0, MAX_WAKE_ITERS + THREAD_MAX);
			args->ret = RET_ERROR;
			break;
		}
	}

	futex_wake(&wake_complete, 1, FUTEX_PRIVATE_FLAG);

	if (args->ret >= 0)
		args->ret = task_count;

	info("Waker: exiting with %d\n", args->ret);
	info("Waker: waiters_woken: %d\n", waiters_woken.val);
	pthread_exit((void *)&args->ret);
}

void *third_party_blocker(void *arg)
{
	struct thread_arg *args = (struct thread_arg *)arg;
	int ret2 = 0;

	args->ret = futex_lock_pi(&f2, NULL, 0, FUTEX_PRIVATE_FLAG);
	if (args->ret)
		goto out;
	args->ret = futex_wait(&wake_complete, wake_complete, NULL,
			       FUTEX_PRIVATE_FLAG);
	ret2 = futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);

 out:
	if (args->ret || ret2) {
		error("third_party_blocker() futex error", 0);
		args->ret = RET_ERROR;
	}

	pthread_exit((void *)&args->ret);
}

int unit_test(int broadcast, long lock, int third_party_owner, long timeout_ns)
{
	void *(*wakerfn)(void *) = signal_wakerfn;
	struct thread_arg blocker_arg = THREAD_ARG_INITIALIZER;
	struct thread_arg waker_arg = THREAD_ARG_INITIALIZER;
	pthread_t waiter[THREAD_MAX], waker, blocker;
	struct timespec ts, *tsp = NULL;
	struct thread_arg args[THREAD_MAX];
	int *waiter_ret;
	int i, ret = RET_PASS;

	if (timeout_ns) {
		time_t secs;

		info("timeout_ns = %ld\n", timeout_ns);
		ret = clock_gettime(CLOCK_MONOTONIC, &ts);
		secs = (ts.tv_nsec + timeout_ns) / 1000000000;
		ts.tv_nsec = ((int64_t)ts.tv_nsec + timeout_ns) % 1000000000;
		ts.tv_sec += secs;
		info("ts.tv_sec  = %ld\n", ts.tv_sec);
		info("ts.tv_nsec = %ld\n", ts.tv_nsec);
		tsp = &ts;
	}

	if (broadcast)
		wakerfn = broadcast_wakerfn;

	if (third_party_owner) {
		if (create_rt_thread(&blocker, third_party_blocker,
				     (void *)&blocker_arg, SCHED_FIFO, 1)) {
			error("Creating third party blocker thread failed\n",
			      errno);
			ret = RET_ERROR;
			goto out;
		}
	}

	atomic_set(&waiters_woken, 0);
	for (i = 0; i < THREAD_MAX; i++) {
		args[i].id = i;
		args[i].timeout = tsp;
		info("Starting thread %d\n", i);
		if (create_rt_thread(&waiter[i], waiterfn, (void *)&args[i],
				     SCHED_FIFO, 1)) {
			error("Creating waiting thread failed\n", errno);
			ret = RET_ERROR;
			goto out;
		}
	}
	waker_arg.lock = lock;
	if (create_rt_thread(&waker, wakerfn, (void *)&waker_arg,
			     SCHED_FIFO, 1)) {
		error("Creating waker thread failed\n", errno);
		ret = RET_ERROR;
		goto out;
	}

	/* Wait for threads to finish */
	/* Store the first error or failure encountered in waiter_ret */
	waiter_ret = &args[0].ret;
	for (i = 0; i < THREAD_MAX; i++)
		pthread_join(waiter[i],
			     *waiter_ret ? NULL : (void **)&waiter_ret);

	if (third_party_owner)
		pthread_join(blocker, NULL);
	pthread_join(waker, NULL);

out:
	if (!ret) {
		if (*waiter_ret)
			ret = *waiter_ret;
		else if (waker_arg.ret < 0)
			ret = waker_arg.ret;
		else if (blocker_arg.ret)
			ret = blocker_arg.ret;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	int c, ret;

	while ((c = getopt(argc, argv, "bchlot:v:")) != -1) {
		switch (c) {
		case 'b':
			broadcast = 1;
			break;
		case 'c':
			log_color(1);
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(0);
		case 'l':
			locked = 1;
			break;
		case 'o':
			owner = 1;
			locked = 0;
			break;
		case 't':
			timeout_ns = atoi(optarg);
			break;
		case 'v':
			log_verbosity(atoi(optarg));
			break;
		default:
			usage(basename(argv[0]));
			exit(1);
		}
	}

	ksft_print_header();
	ksft_print_msg("%s: Test requeue functionality\n", basename(argv[0]));
	ksft_print_msg(
		"\tArguments: broadcast=%d locked=%d owner=%d timeout=%ldns\n",
		broadcast, locked, owner, timeout_ns);

	/*
	 * FIXME: unit_test is obsolete now that we parse options and the
	 * various style of runs are done by run.sh - simplify the code and move
	 * unit_test into main()
	 */
	ret = unit_test(broadcast, locked, owner, timeout_ns);

	print_result(TEST_NAME, ret);
	return ret;
}
