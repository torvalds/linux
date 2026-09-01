/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _FUTEX_THREAD_H
#define _FUTEX_THREAD_H
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "kselftest_harness.h"

#define USEC_PER_SEC		1000000L
#define WAIT_FOR_THREAD_SECS	1
#define WAIT_FOR_THREAD_USECS	(WAIT_FOR_THREAD_SECS * USEC_PER_SEC)
#define WAIT_THREAD_RETRIES	100

struct futex_thread {
	pthread_t		thread;
	pthread_barrier_t	barrier;
	pid_t			tid;
	int			(*threadfn)(void *arg);
	void			*arg;
	int			retval;
};

static inline int __wait_for_thread(FILE *fp, struct __test_metadata *_metadata)
{
	unsigned int sleep_time_us = WAIT_FOR_THREAD_USECS / WAIT_THREAD_RETRIES;
	char buf[80] = "";

	for (int i = 0; i < WAIT_THREAD_RETRIES; i++) {
		if (!fgets(buf, sizeof(buf), fp))
			return EIO;
		if (!strncmp(buf, "futex", 5))
			return 0;
		usleep(sleep_time_us);
		rewind(fp);
	}

	TH_LOG("/proc/$PID/wchan contains \"%s\". Trying to continue.", buf);
	return 0;
}

static void *__futex_thread_fn(void *arg)
{
	struct futex_thread *t = arg;

	t->tid = gettid();
	pthread_barrier_wait(&t->barrier);
	t->retval = t->threadfn(t->arg);
	return NULL;
}

/**
 * futex_wait_for_thread - Wait for the child thread to sleep in the futex context
 * @t:          Thread handle.
 * @_metadata:	Test metadata for TH_LOG() context
 */
static inline int futex_wait_for_thread(struct futex_thread *t, struct __test_metadata *_metadata)
{
	char fname[80];
	FILE *fp;
	int res;

	snprintf(fname, sizeof(fname), "/proc/%d/wchan", t->tid);
	fp = fopen(fname, "r");
	if (!fp) {
		/* If /proc/... is not available, sleep */
		if (errno != ENOENT)
			return errno;
		TH_LOG("/proc/$PID/wchan not accessible, continue with sleep()");
		sleep(WAIT_FOR_THREAD_SECS);
		return 0;
	}

	res = __wait_for_thread(fp, _metadata);
	fclose(fp);
	return res;
}

/**
 * futex_thread_create - Create a new thread for testing.
 * @t:        The handle of the newly created thread.
 * @threadfn: The new thread starts execution by invoking threadfn
 * @arg:      The parameters passed to threadfn.
 */
static inline int futex_thread_create(struct futex_thread *t, int (*threadfn)(void *), void *arg)
{
	pthread_barrier_init(&t->barrier, NULL, 2);

	t->tid = 0;
	t->threadfn = threadfn;
	t->arg = arg;

	if (pthread_create(&t->thread, NULL, __futex_thread_fn, t) < 0) {
		int ret = errno;
		pthread_barrier_destroy(&t->barrier);
		return ret;
	}

	pthread_barrier_wait(&t->barrier);
	return 0;
}

/**
 * futex_thread_destroy - Wait for and reclaim the resources of the thread.
 * @t:      Thread handle.
 */
static inline int futex_thread_destroy(struct futex_thread *t)
{
	pthread_join(t->thread, NULL);
	pthread_barrier_destroy(&t->barrier);
	return t->retval;
}

#endif
