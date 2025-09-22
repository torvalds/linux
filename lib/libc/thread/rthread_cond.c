/*	$OpenBSD: rthread_cond.c,v 1.6 2024/01/10 04:28:43 cheloha Exp $ */
/*
 * Copyright (c) 2017 Martin Pieuchot <mpi@openbsd.org>
 * Copyright (c) 2012 Philip Guenther <guenther@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rthread.h"
#include "cancel.h"
#include "synch.h"

int
pthread_cond_init(pthread_cond_t *condp, const pthread_condattr_t *attr)
{
	pthread_cond_t cond;

	cond = calloc(1, sizeof(*cond));
	if (cond == NULL)
		return (ENOMEM);

	if (attr == NULL)
		cond->clock = CLOCK_REALTIME;
	else
		cond->clock = (*attr)->ca_clock;
	*condp = cond;

	return (0);
}
DEF_STRONG(pthread_cond_init);

int
pthread_cond_destroy(pthread_cond_t *condp)
{
	pthread_cond_t cond;

	cond = *condp;

	if (cond != NULL) {
		if (cond->mutex != NULL) {
#define MSG "pthread_cond_destroy on condvar with waiters!\n"
			write(2, MSG, sizeof(MSG) - 1);
#undef MSG
			return (EBUSY);
		}
		free(cond);
	}
	*condp = NULL;

	return (0);
}

int
_rthread_cond_timedwait(pthread_cond_t cond, pthread_mutex_t *mutexp,
    const struct timespec *abs)
{
	struct pthread_mutex *mutex = (struct pthread_mutex *)*mutexp;
	struct tib *tib = TIB_GET();
	pthread_t self = tib->tib_thread;
	int error, rv = 0, canceled = 0, mutex_count = 0;
	clockid_t clock = cond->clock;
	int seq = cond->seq;
	PREP_CANCEL_POINT(tib);

	_rthread_debug(5, "%p: cond_timed %p,%p (%p)\n", self,
	    (void *)cond, (void *)mutex, (void *)mutex->owner);

	ENTER_DELAYED_CANCEL_POINT(tib, self);

#if notyet
	/* mark the condvar as being associated with this mutex */
	if (cond->mutex == NULL)
		atomic_cas_ptr(&cond->mutex, NULL, mutex);

	if (cond->mutex != mutex) {
		LEAVE_CANCEL_POINT_INNER(tib, 1);
		return (EINVAL);
	}
#endif

	/* snag the count in case this is a recursive mutex */
	if (mutex->type == PTHREAD_MUTEX_RECURSIVE)
		mutex_count = mutex->count;

	pthread_mutex_unlock(mutexp);

	do {
		/* If ``seq'' wraps you deserve to lose a signal. */
		error = _twait(&cond->seq, seq, clock, abs);
		/*
		* If we took a normal signal (not from cancellation) then
		* we should just go back to sleep without changing state
		* (timeouts, etc).
		*/
	} while ((error == EINTR) &&
	   (tib->tib_canceled == 0 || (tib->tib_cantcancel & CANCEL_DISABLED)));

	/* if timeout or canceled, make note of that */
	if (error == ETIMEDOUT)
		rv = ETIMEDOUT;
	else if (error == EINTR)
		canceled = 1;

	pthread_mutex_lock(mutexp);

	/* restore the mutex's count */
	if (mutex->type == PTHREAD_MUTEX_RECURSIVE)
		mutex->count = mutex_count;

	LEAVE_CANCEL_POINT_INNER(tib, canceled);

	return rv;
}

int
pthread_cond_timedwait(pthread_cond_t *condp, pthread_mutex_t *mutexp,
    const struct timespec *abs)
{
	pthread_cond_t cond;
	int error;

	if (*condp == NULL) {
		if ((error = pthread_cond_init(condp, NULL)))
			return (error);
	}

	cond = *condp;
	if (abs == NULL || abs->tv_nsec < 0 || abs->tv_nsec >= 1000000000)
		return (EINVAL);

	return (_rthread_cond_timedwait(cond, mutexp, abs));
}

int
pthread_cond_wait(pthread_cond_t *condp, pthread_mutex_t *mutexp)
{
	pthread_cond_t cond;
	int error;

	if (*condp == NULL) {
		if ((error = pthread_cond_init(condp, NULL)))
			return (error);
	}

	cond = *condp;
	return (_rthread_cond_timedwait(cond, mutexp, NULL));
}

int
pthread_cond_signal(pthread_cond_t *condp)
{
	pthread_cond_t cond;
	int count;

	if (*condp == NULL)
		return (0);

	cond = *condp;

	atomic_inc_int(&cond->seq);
	count = _wake(&cond->seq, 1);

	_rthread_debug(5, "%p: cond_signal %p, %d awaken\n", pthread_self(),
	    (void *)cond, count);

	return (0);
}

int
pthread_cond_broadcast(pthread_cond_t *condp)
{
	pthread_cond_t cond;
	int count;

	if (*condp == NULL)
		return (0);

	cond = *condp;

	atomic_inc_int(&cond->seq);
#if notyet
	count = _requeue(&cond->seq, 1, INT_MAX, &cond->mutex->lock);
#else
	count = _wake(&cond->seq, INT_MAX);
#endif

	_rthread_debug(5, "%p: cond_broadcast %p, %d awaken\n", pthread_self(),
	    (void *)cond, count);

	return (0);
}
