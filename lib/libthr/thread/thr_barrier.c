/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 David Xu <davidxu@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

_Static_assert(sizeof(struct pthread_barrier) <= PAGE_SIZE,
    "pthread_barrier is too large for off-page");

__weak_reference(_pthread_barrier_init,		pthread_barrier_init);
__weak_reference(_pthread_barrier_wait,		pthread_barrier_wait);
__weak_reference(_pthread_barrier_destroy,	pthread_barrier_destroy);

int
_pthread_barrier_destroy(pthread_barrier_t *barrier)
{
	pthread_barrier_t bar;
	struct pthread *curthread;
	int pshared;

	if (barrier == NULL || *barrier == NULL)
		return (EINVAL);

	if (*barrier == THR_PSHARED_PTR) {
		bar = __thr_pshared_offpage(barrier, 0);
		if (bar == NULL) {
			*barrier = NULL;
			return (0);
		}
		pshared = 1;
	} else {
		bar = *barrier;
		pshared = 0;
	}
	curthread = _get_curthread();
	THR_UMUTEX_LOCK(curthread, &bar->b_lock);
	if (bar->b_destroying) {
		THR_UMUTEX_UNLOCK(curthread, &bar->b_lock);
		return (EBUSY);
	}
	bar->b_destroying = 1;
	do {
		if (bar->b_waiters > 0) {
			bar->b_destroying = 0;
			THR_UMUTEX_UNLOCK(curthread, &bar->b_lock);
			return (EBUSY);
		}
		if (bar->b_refcount != 0) {
			_thr_ucond_wait(&bar->b_cv, &bar->b_lock, NULL, 0);
			THR_UMUTEX_LOCK(curthread, &bar->b_lock);
		} else
			break;
	} while (1);
	bar->b_destroying = 0;
	THR_UMUTEX_UNLOCK(curthread, &bar->b_lock);

	*barrier = NULL;
	if (pshared)
		__thr_pshared_destroy(barrier);
	else
		free(bar);
	return (0);
}

int
_pthread_barrier_init(pthread_barrier_t * __restrict barrier,
    const pthread_barrierattr_t * __restrict attr, unsigned count)
{
	pthread_barrier_t bar;
	int pshared;

	if (barrier == NULL || count == 0 || count > INT_MAX)
		return (EINVAL);

	if (attr == NULL || *attr == NULL ||
	    (*attr)->pshared == PTHREAD_PROCESS_PRIVATE) {
		bar = calloc(1, sizeof(struct pthread_barrier));
		if (bar == NULL)
			return (ENOMEM);
		*barrier = bar;
		pshared = 0;
	} else {
		bar = __thr_pshared_offpage(barrier, 1);
		if (bar == NULL)
			return (EFAULT);
		*barrier = THR_PSHARED_PTR;
		pshared = 1;
	}

	_thr_umutex_init(&bar->b_lock);
	_thr_ucond_init(&bar->b_cv);
	if (pshared) {
		bar->b_lock.m_flags |= USYNC_PROCESS_SHARED;
		bar->b_cv.c_flags |= USYNC_PROCESS_SHARED;
	}
	bar->b_count = count;
	return (0);
}

int
_pthread_barrier_wait(pthread_barrier_t *barrier)
{
	struct pthread *curthread;
	pthread_barrier_t bar;
	int64_t cycle;
	int ret;

	if (barrier == NULL || *barrier == NULL)
		return (EINVAL);

	if (*barrier == THR_PSHARED_PTR) {
		bar = __thr_pshared_offpage(barrier, 0);
		if (bar == NULL)
			return (EINVAL);
	} else {
		bar = *barrier;
	}
	curthread = _get_curthread();
	THR_UMUTEX_LOCK(curthread, &bar->b_lock);
	if (++bar->b_waiters == bar->b_count) {
		/* Current thread is lastest thread */
		bar->b_waiters = 0;
		bar->b_cycle++;
		_thr_ucond_broadcast(&bar->b_cv);
		THR_UMUTEX_UNLOCK(curthread, &bar->b_lock);
		ret = PTHREAD_BARRIER_SERIAL_THREAD;
	} else {
		cycle = bar->b_cycle;
		bar->b_refcount++;
		do {
			_thr_ucond_wait(&bar->b_cv, &bar->b_lock, NULL, 0);
			THR_UMUTEX_LOCK(curthread, &bar->b_lock);
			/* test cycle to avoid bogus wakeup */
		} while (cycle == bar->b_cycle);
		if (--bar->b_refcount == 0 && bar->b_destroying)
			_thr_ucond_broadcast(&bar->b_cv);
		THR_UMUTEX_UNLOCK(curthread, &bar->b_lock);
		ret = 0;
	}
	return (ret);
}
