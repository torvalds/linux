/*	$OpenBSD: rthread_spin_lock.c,v 1.5 2020/04/06 00:01:08 pirofti Exp $	*/
/*
 * Copyright (c) 2012 Paul Irofti <paul@irofti.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#include <stdlib.h>

#include <pthread.h>

#include "rthread.h"

int
pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
	pthread_spinlock_t l = NULL;

	if (lock == NULL)
		return (EINVAL);

	if (pshared != PTHREAD_PROCESS_PRIVATE)
		return (ENOTSUP);

	l = calloc(1, sizeof *l);
	if (l == NULL)
		return (ENOMEM);

	l->lock = _SPINLOCK_UNLOCKED;
	*lock = l;
	return (0);
}

int
pthread_spin_destroy(pthread_spinlock_t *lock)
{
	if (lock == NULL || *lock == NULL)
		return (EINVAL);

	if ((*lock)->owner != NULL)
		return (EBUSY);

	free(*lock);
	*lock = NULL;
	return (0);
}

int
pthread_spin_trylock(pthread_spinlock_t *lock)
{
	pthread_t self = pthread_self();
	pthread_spinlock_t l;

	if (lock == NULL || *lock == NULL)
		return (EINVAL);

	l = *lock;

	if (l->owner == self)
		return (EDEADLK);
	if (!_spinlocktry(&l->lock))
		return (EBUSY);

	l->owner = self;
	return (0);
}

int
pthread_spin_lock(pthread_spinlock_t *lock)
{
	pthread_t self = pthread_self();
	pthread_spinlock_t l;

	if (lock == NULL || *lock == NULL)
		return (EINVAL);

	l = *lock;

	if (l->owner == self)
		return (EDEADLK);

	_spinlock(&l->lock);
	l->owner = self;
	return (0);
}

int
pthread_spin_unlock(pthread_spinlock_t *lock)
{
	pthread_t self = pthread_self();
	pthread_spinlock_t l;

	if (lock == NULL || *lock == NULL)
		return (EINVAL);

	l = *lock;

	if (l->owner != self)
		return (EPERM);

	l->owner = NULL;
	_spinunlock(&l->lock);
	return (0);
}
