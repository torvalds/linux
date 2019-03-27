/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005, David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <errno.h>
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

int	_pthread_timedjoin_np(pthread_t pthread, void **thread_return,
	const struct timespec *abstime);
static int join_common(pthread_t, void **, const struct timespec *);

__weak_reference(_pthread_join, pthread_join);
__weak_reference(_pthread_timedjoin_np, pthread_timedjoin_np);

static void backout_join(void *arg)
{
	struct pthread *pthread = (struct pthread *)arg;
	struct pthread *curthread = _get_curthread();

	THR_THREAD_LOCK(curthread, pthread);
	pthread->joiner = NULL;
	THR_THREAD_UNLOCK(curthread, pthread);
}

int
_pthread_join(pthread_t pthread, void **thread_return)
{
	return (join_common(pthread, thread_return, NULL));
}

int
_pthread_timedjoin_np(pthread_t pthread, void **thread_return,
	const struct timespec *abstime)
{
	if (abstime == NULL || abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
	    abstime->tv_nsec >= 1000000000)
		return (EINVAL);

	return (join_common(pthread, thread_return, abstime));
}

/*
 * Cancellation behavior:
 *   if the thread is canceled, joinee is not recycled.
 */
static int
join_common(pthread_t pthread, void **thread_return,
	const struct timespec *abstime)
{
	struct pthread *curthread = _get_curthread();
	struct timespec ts, ts2, *tsp;
	void *tmp;
	long tid;
	int ret = 0;

	if (pthread == NULL)
		return (EINVAL);

	if (pthread == curthread)
		return (EDEADLK);

	if ((ret = _thr_find_thread(curthread, pthread, 1)) != 0)
		return (ESRCH);

	if ((pthread->flags & THR_FLAGS_DETACHED) != 0) {
		ret = EINVAL;
	} else if (pthread->joiner != NULL) {
		/* Multiple joiners are not supported. */
		ret = ENOTSUP;
	}
	if (ret) {
		THR_THREAD_UNLOCK(curthread, pthread);
		return (ret);
	}
	/* Set the running thread to be the joiner: */
	pthread->joiner = curthread;

	THR_THREAD_UNLOCK(curthread, pthread);

	THR_CLEANUP_PUSH(curthread, backout_join, pthread);
	_thr_cancel_enter(curthread);

	tid = pthread->tid;
	while (pthread->tid != TID_TERMINATED) {
		_thr_testcancel(curthread);
		if (abstime != NULL) {
			clock_gettime(CLOCK_REALTIME, &ts);
			TIMESPEC_SUB(&ts2, abstime, &ts);
			if (ts2.tv_sec < 0) {
				ret = ETIMEDOUT;
				break;
			}
			tsp = &ts2;
		} else
			tsp = NULL;
		ret = _thr_umtx_wait(&pthread->tid, tid, tsp);
		if (ret == ETIMEDOUT)
			break;
	}

	_thr_cancel_leave(curthread, 0);
	THR_CLEANUP_POP(curthread, 0);

	if (ret == ETIMEDOUT) {
		THR_THREAD_LOCK(curthread, pthread);
		pthread->joiner = NULL;
		THR_THREAD_UNLOCK(curthread, pthread);
	} else {
		ret = 0;
		tmp = pthread->ret;
		THR_THREAD_LOCK(curthread, pthread);
		pthread->flags |= THR_FLAGS_DETACHED;
		pthread->joiner = NULL;
		_thr_try_gc(curthread, pthread); /* thread lock released */

		if (thread_return != NULL)
			*thread_return = tmp;
	}
	return (ret);
}
