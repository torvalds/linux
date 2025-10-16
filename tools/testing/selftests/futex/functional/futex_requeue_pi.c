// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2006-2008
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

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#include "atomic.h"
#include "futextest.h"
#include "../../kselftest_harness.h"

#define MAX_WAKE_ITERS 1000
#define THREAD_MAX 10
#define SIGNAL_PERIOD_US 100

atomic_t waiters_blocked = ATOMIC_INITIALIZER;
atomic_t waiters_woken = ATOMIC_INITIALIZER;

futex_t f1 = FUTEX_INITIALIZER;
futex_t f2 = FUTEX_INITIALIZER;
futex_t wake_complete = FUTEX_INITIALIZER;

struct thread_arg {
	long id;
	struct timespec *timeout;
	int lock;
	int ret;
};
#define THREAD_ARG_INITIALIZER { 0, NULL, 0, 0 }

FIXTURE(args)
{
};

FIXTURE_SETUP(args)
{
};

FIXTURE_TEARDOWN(args)
{
};

FIXTURE_VARIANT(args)
{
	long timeout_ns;
	bool broadcast;
	bool owner;
	bool locked;
};

/*
 * For a given timeout value, this macro creates a test input with all the
 * possible combinations of valid arguments
 */
#define FIXTURE_VARIANT_ADD_TIMEOUT(timeout)		\
							\
FIXTURE_VARIANT_ADD(args, t_##timeout)			\
{							\
	.timeout_ns = timeout,				\
};							\
							\
FIXTURE_VARIANT_ADD(args, t_##timeout##_broadcast)	\
{							\
	.timeout_ns = timeout,				\
	.broadcast = true,				\
};							\
							\
FIXTURE_VARIANT_ADD(args, t_##timeout##_broadcast_locked) \
{							\
	.timeout_ns = timeout,				\
	.broadcast = true,				\
	.locked = true,					\
};							\
							\
FIXTURE_VARIANT_ADD(args, t_##timeout##_broadcast_owner) \
{							\
	.timeout_ns = timeout,				\
	.broadcast = true,				\
	.owner = true,					\
};							\
							\
FIXTURE_VARIANT_ADD(args, t_##timeout##_locked)		\
{							\
	.timeout_ns = timeout,				\
	.locked = true,					\
};							\
							\
FIXTURE_VARIANT_ADD(args, t_##timeout##_owner)		\
{							\
	.timeout_ns = timeout,				\
	.owner = true,					\
};							\

FIXTURE_VARIANT_ADD_TIMEOUT(0);
FIXTURE_VARIANT_ADD_TIMEOUT(5000);
FIXTURE_VARIANT_ADD_TIMEOUT(500000);
FIXTURE_VARIANT_ADD_TIMEOUT(2000000000);

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
		ksft_exit_fail_msg("pthread_attr_setinheritsched\n");
		return -1;
	}

	ret = pthread_attr_setschedpolicy(&attr, policy);
	if (ret) {
		ksft_exit_fail_msg("pthread_attr_setschedpolicy\n");
		return -1;
	}

	schedp.sched_priority = prio;
	ret = pthread_attr_setschedparam(&attr, &schedp);
	if (ret) {
		ksft_exit_fail_msg("pthread_attr_setschedparam\n");
		return -1;
	}

	ret = pthread_create(pth, &attr, func, arg);
	if (ret) {
		ksft_exit_fail_msg("pthread_create\n");
		return -1;
	}
	return 0;
}


void *waiterfn(void *arg)
{
	struct thread_arg *args = (struct thread_arg *)arg;
	futex_t old_val;

	ksft_print_dbg_msg("Waiter %ld: running\n", args->id);
	/* Each thread sleeps for a different amount of time
	 * This is to avoid races, because we don't lock the
	 * external mutex here */
	usleep(1000 * (long)args->id);

	old_val = f1;
	atomic_inc(&waiters_blocked);
	ksft_print_dbg_msg("Calling futex_wait_requeue_pi: %p (%u) -> %p\n",
	     &f1, f1, &f2);
	args->ret = futex_wait_requeue_pi(&f1, old_val, &f2, args->timeout,
					  FUTEX_PRIVATE_FLAG);

	ksft_print_dbg_msg("waiter %ld woke with %d %s\n", args->id, args->ret,
	     args->ret < 0 ? strerror(errno) : "");
	atomic_inc(&waiters_woken);
	if (args->ret < 0) {
		if (args->timeout && errno == ETIMEDOUT)
			args->ret = 0;
		else {
			ksft_exit_fail_msg("futex_wait_requeue_pi\n");
		}
		futex_lock_pi(&f2, NULL, 0, FUTEX_PRIVATE_FLAG);
	}
	futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);

	ksft_print_dbg_msg("Waiter %ld: exiting with %d\n", args->id, args->ret);
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

	ksft_print_dbg_msg("Waker: waiting for waiters to block\n");
	while (waiters_blocked.val < THREAD_MAX)
		usleep(1000);
	usleep(1000);

	ksft_print_dbg_msg("Waker: Calling broadcast\n");
	if (args->lock) {
		ksft_print_dbg_msg("Calling FUTEX_LOCK_PI on mutex=%x @ %p\n", f2, &f2);
		futex_lock_pi(&f2, NULL, 0, FUTEX_PRIVATE_FLAG);
	}
 continue_requeue:
	old_val = f1;
	args->ret = futex_cmp_requeue_pi(&f1, old_val, &f2, nr_wake, nr_requeue,
				   FUTEX_PRIVATE_FLAG);
	if (args->ret < 0) {
		ksft_exit_fail_msg("FUTEX_CMP_REQUEUE_PI failed\n");
	} else if (++i < MAX_WAKE_ITERS) {
		task_count += args->ret;
		if (task_count < THREAD_MAX - waiters_woken.val)
			goto continue_requeue;
	} else {
		ksft_exit_fail_msg("max broadcast iterations (%d) reached with %d/%d tasks woken or requeued\n",
		       MAX_WAKE_ITERS, task_count, THREAD_MAX);
	}

	futex_wake(&wake_complete, 1, FUTEX_PRIVATE_FLAG);

	if (args->lock)
		futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);

	if (args->ret > 0)
		args->ret = task_count;

	ksft_print_dbg_msg("Waker: exiting with %d\n", args->ret);
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

	ksft_print_dbg_msg("Waker: waiting for waiters to block\n");
	while (waiters_blocked.val < THREAD_MAX)
		usleep(1000);
	usleep(1000);

	while (task_count < THREAD_MAX && waiters_woken.val < THREAD_MAX) {
		ksft_print_dbg_msg("task_count: %d, waiters_woken: %d\n",
		     task_count, waiters_woken.val);
		if (args->lock) {
			ksft_print_dbg_msg("Calling FUTEX_LOCK_PI on mutex=%x @ %p\n",
			    f2, &f2);
			futex_lock_pi(&f2, NULL, 0, FUTEX_PRIVATE_FLAG);
		}
		ksft_print_dbg_msg("Waker: Calling signal\n");
		/* cond_signal */
		old_val = f1;
		args->ret = futex_cmp_requeue_pi(&f1, old_val, &f2,
						 nr_wake, nr_requeue,
						 FUTEX_PRIVATE_FLAG);
		if (args->ret < 0)
			args->ret = -errno;
		ksft_print_dbg_msg("futex: %x\n", f2);
		if (args->lock) {
			ksft_print_dbg_msg("Calling FUTEX_UNLOCK_PI on mutex=%x @ %p\n",
			    f2, &f2);
			futex_unlock_pi(&f2, FUTEX_PRIVATE_FLAG);
		}
		ksft_print_dbg_msg("futex: %x\n", f2);
		if (args->ret < 0)
			ksft_exit_fail_msg("FUTEX_CMP_REQUEUE_PI failed\n");

		task_count += args->ret;
		usleep(SIGNAL_PERIOD_US);
		i++;
		/* we have to loop at least THREAD_MAX times */
		if (i > MAX_WAKE_ITERS + THREAD_MAX) {
			ksft_exit_fail_msg("max signaling iterations (%d) reached, giving up on pending waiters.\n",
			      MAX_WAKE_ITERS + THREAD_MAX);
		}
	}

	futex_wake(&wake_complete, 1, FUTEX_PRIVATE_FLAG);

	if (args->ret >= 0)
		args->ret = task_count;

	ksft_print_dbg_msg("Waker: exiting with %d\n", args->ret);
	ksft_print_dbg_msg("Waker: waiters_woken: %d\n", waiters_woken.val);
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
	if (args->ret || ret2)
		ksft_exit_fail_msg("third_party_blocker() futex error");

	pthread_exit((void *)&args->ret);
}

TEST_F(args, futex_requeue_pi)
{
	struct thread_arg blocker_arg = THREAD_ARG_INITIALIZER;
	struct thread_arg waker_arg = THREAD_ARG_INITIALIZER;
	pthread_t waiter[THREAD_MAX], waker, blocker;
	void *(*wakerfn)(void *) = signal_wakerfn;
	bool third_party_owner = variant->owner;
	long timeout_ns = variant->timeout_ns;
	bool broadcast = variant->broadcast;
	struct thread_arg args[THREAD_MAX];
	struct timespec ts, *tsp = NULL;
	bool lock = variant->locked;
	int *waiter_ret, i, ret = 0;

	ksft_print_msg(
		"\tArguments: broadcast=%d locked=%d owner=%d timeout=%ldns\n",
		broadcast, lock, third_party_owner, timeout_ns);

	if (timeout_ns) {
		time_t secs;

		ksft_print_dbg_msg("timeout_ns = %ld\n", timeout_ns);
		ret = clock_gettime(CLOCK_MONOTONIC, &ts);
		secs = (ts.tv_nsec + timeout_ns) / 1000000000;
		ts.tv_nsec = ((int64_t)ts.tv_nsec + timeout_ns) % 1000000000;
		ts.tv_sec += secs;
		ksft_print_dbg_msg("ts.tv_sec  = %ld\n", ts.tv_sec);
		ksft_print_dbg_msg("ts.tv_nsec = %ld\n", ts.tv_nsec);
		tsp = &ts;
	}

	if (broadcast)
		wakerfn = broadcast_wakerfn;

	if (third_party_owner) {
		if (create_rt_thread(&blocker, third_party_blocker,
				     (void *)&blocker_arg, SCHED_FIFO, 1)) {
			ksft_exit_fail_msg("Creating third party blocker thread failed\n");
		}
	}

	atomic_set(&waiters_woken, 0);
	for (i = 0; i < THREAD_MAX; i++) {
		args[i].id = i;
		args[i].timeout = tsp;
		ksft_print_dbg_msg("Starting thread %d\n", i);
		if (create_rt_thread(&waiter[i], waiterfn, (void *)&args[i],
				     SCHED_FIFO, 1)) {
			ksft_exit_fail_msg("Creating waiting thread failed\n");
		}
	}
	waker_arg.lock = lock;
	if (create_rt_thread(&waker, wakerfn, (void *)&waker_arg,
			     SCHED_FIFO, 1)) {
		ksft_exit_fail_msg("Creating waker thread failed\n");
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

	if (!ret) {
		if (*waiter_ret)
			ret = *waiter_ret;
		else if (waker_arg.ret < 0)
			ret = waker_arg.ret;
		else if (blocker_arg.ret)
			ret = blocker_arg.ret;
	}

	if (ret)
		ksft_test_result_fail("fail");
}

TEST_HARNESS_MAIN
