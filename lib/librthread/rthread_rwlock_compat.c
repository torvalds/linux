/*	$OpenBSD: rthread_rwlock_compat.c,v 1.2 2022/05/14 14:52:20 cheloha Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
 * Copyright (c) 2012 Philip Guenther <guenther@openbsd.org>
 * All Rights Reserved.
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
/*
 * rwlocks
 */

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <pthread.h>

#include "rthread.h"

static _atomic_lock_t rwlock_init_lock = _SPINLOCK_UNLOCKED;

int
pthread_rwlock_init(pthread_rwlock_t *lockp,
    const pthread_rwlockattr_t *attrp __unused)
{
	pthread_rwlock_t lock;

	lock = calloc(1, sizeof(*lock));
	if (!lock)
		return (errno);
	lock->lock = _SPINLOCK_UNLOCKED;
	TAILQ_INIT(&lock->writers);

	*lockp = lock;

	return (0);
}
DEF_STD(pthread_rwlock_init);

int
pthread_rwlock_destroy(pthread_rwlock_t *lockp)
{
	pthread_rwlock_t lock;

	assert(lockp);
	lock = *lockp;
	if (lock) {
		if (lock->readers || !TAILQ_EMPTY(&lock->writers)) {
#define MSG "pthread_rwlock_destroy on rwlock with waiters!\n"
			write(2, MSG, sizeof(MSG) - 1);
#undef MSG
			return (EBUSY);
		}
		free(lock);
	}
	*lockp = NULL;

	return (0);
}

static int
_rthread_rwlock_ensure_init(pthread_rwlock_t *lockp)
{
	int ret = 0;

	/*
	 * If the rwlock is statically initialized, perform the dynamic
	 * initialization.
	 */
	if (*lockp == NULL)
	{
		_spinlock(&rwlock_init_lock);
		if (*lockp == NULL)
			ret = pthread_rwlock_init(lockp, NULL);
		_spinunlock(&rwlock_init_lock);
	}
	return (ret);
}


static int
_rthread_rwlock_rdlock(pthread_rwlock_t *lockp, const struct timespec *abstime,
    int try)
{
	pthread_rwlock_t lock;
	pthread_t thread = pthread_self();
	int error;

	if ((error = _rthread_rwlock_ensure_init(lockp)))
		return (error);

	lock = *lockp;
	_rthread_debug(5, "%p: rwlock_rdlock %p\n", (void *)thread,
	    (void *)lock);
	_spinlock(&lock->lock);

	/* writers have precedence */
	if (lock->owner == NULL && TAILQ_EMPTY(&lock->writers))
		lock->readers++;
	else if (try)
		error = EBUSY;
	else if (lock->owner == thread)
		error = EDEADLK;
	else {
		do {
			if (__thrsleep(lock, CLOCK_REALTIME, abstime,
			    &lock->lock, NULL) == EWOULDBLOCK)
				return (ETIMEDOUT);
			_spinlock(&lock->lock);
		} while (lock->owner != NULL || !TAILQ_EMPTY(&lock->writers));
		lock->readers++;
	}
	_spinunlock(&lock->lock);

	return (error);
}

int
pthread_rwlock_rdlock(pthread_rwlock_t *lockp)
{
	return (_rthread_rwlock_rdlock(lockp, NULL, 0));
}

int
pthread_rwlock_tryrdlock(pthread_rwlock_t *lockp)
{
	return (_rthread_rwlock_rdlock(lockp, NULL, 1));
}

int
pthread_rwlock_timedrdlock(pthread_rwlock_t *lockp,
    const struct timespec *abstime)
{
	if (abstime == NULL || !timespecisvalid(abstime))
		return (EINVAL);
	return (_rthread_rwlock_rdlock(lockp, abstime, 0));
}


static int
_rthread_rwlock_wrlock(pthread_rwlock_t *lockp, const struct timespec *abstime,
    int try)
{
	pthread_rwlock_t lock;
	pthread_t thread = pthread_self();
	int error;

	if ((error = _rthread_rwlock_ensure_init(lockp)))
		return (error);

	lock = *lockp;

	_rthread_debug(5, "%p: rwlock_timedwrlock %p\n", (void *)thread,
	    (void *)lock);
	_spinlock(&lock->lock);
	if (lock->readers == 0 && lock->owner == NULL)
		lock->owner = thread;
	else if (try)
		error = EBUSY;
	else if (lock->owner == thread)
		error = EDEADLK;
	else {
		int do_wait;

		/* gotta block */
		TAILQ_INSERT_TAIL(&lock->writers, thread, waiting);
		do {
			do_wait = __thrsleep(thread, CLOCK_REALTIME, abstime,
			    &lock->lock, NULL) != EWOULDBLOCK;
			_spinlock(&lock->lock);
		} while (lock->owner != thread && do_wait);

		if (lock->owner != thread) {
			/* timed out, sigh */
			TAILQ_REMOVE(&lock->writers, thread, waiting);
			error = ETIMEDOUT;
		}
	}
	_spinunlock(&lock->lock);

	return (error);
}

int
pthread_rwlock_wrlock(pthread_rwlock_t *lockp)
{
	return (_rthread_rwlock_wrlock(lockp, NULL, 0));
}

int
pthread_rwlock_trywrlock(pthread_rwlock_t *lockp)
{
	return (_rthread_rwlock_wrlock(lockp, NULL, 1));
}

int
pthread_rwlock_timedwrlock(pthread_rwlock_t *lockp,
    const struct timespec *abstime)
{
	if (abstime == NULL || !timespecisvalid(abstime))
		return (EINVAL);
	return (_rthread_rwlock_wrlock(lockp, abstime, 0));
}


int
pthread_rwlock_unlock(pthread_rwlock_t *lockp)
{
	pthread_rwlock_t lock;
	pthread_t thread = pthread_self();
	pthread_t next;
	int was_writer;

	lock = *lockp;

	_rthread_debug(5, "%p: rwlock_unlock %p\n", (void *)thread,
	    (void *)lock);
	_spinlock(&lock->lock);
	if (lock->owner != NULL) {
		assert(lock->owner == thread);
		was_writer = 1;
	} else {
		assert(lock->readers > 0);
		lock->readers--;
		if (lock->readers > 0)
			goto out;
		was_writer = 0;
	}

	lock->owner = next = TAILQ_FIRST(&lock->writers);
	if (next != NULL) {
		/* dequeue and wake first writer */
		TAILQ_REMOVE(&lock->writers, next, waiting);
		_spinunlock(&lock->lock);
		__thrwakeup(next, 1);
		return (0);
	}

	/* could there have been blocked readers?  wake them all */
	if (was_writer)
		__thrwakeup(lock, 0);
out:
	_spinunlock(&lock->lock);

	return (0);
}
