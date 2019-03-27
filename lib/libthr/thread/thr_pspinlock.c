/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
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

_Static_assert(sizeof(struct pthread_spinlock) <= PAGE_SIZE,
    "pthread_spinlock is too large for off-page");

#define SPIN_COUNT 100000

__weak_reference(_pthread_spin_init, pthread_spin_init);
__weak_reference(_pthread_spin_destroy, pthread_spin_destroy);
__weak_reference(_pthread_spin_trylock, pthread_spin_trylock);
__weak_reference(_pthread_spin_lock, pthread_spin_lock);
__weak_reference(_pthread_spin_unlock, pthread_spin_unlock);

int
_pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
	struct pthread_spinlock	*lck;

	if (lock == NULL)
		return (EINVAL);
	if (pshared == PTHREAD_PROCESS_PRIVATE) {
		lck = malloc(sizeof(struct pthread_spinlock));
		if (lck == NULL)
			return (ENOMEM);
		*lock = lck;
	} else if (pshared == PTHREAD_PROCESS_SHARED) {
		lck = __thr_pshared_offpage(lock, 1);
		if (lck == NULL)
			return (EFAULT);
		*lock = THR_PSHARED_PTR;
	} else {
		return (EINVAL);
	}
	_thr_umutex_init(&lck->s_lock);
	return (0);
}

int
_pthread_spin_destroy(pthread_spinlock_t *lock)
{
	void *l;
	int ret;

	if (lock == NULL || *lock == NULL) {
		ret = EINVAL;
	} else if (*lock == THR_PSHARED_PTR) {
		l = __thr_pshared_offpage(lock, 0);
		if (l != NULL)
			__thr_pshared_destroy(l);
		ret = 0;
	} else {
		free(*lock);
		*lock = NULL;
		ret = 0;
	}
	return (ret);
}

int
_pthread_spin_trylock(pthread_spinlock_t *lock)
{
	struct pthread_spinlock	*lck;

	if (lock == NULL || *lock == NULL)
		return (EINVAL);
	lck = *lock == THR_PSHARED_PTR ? __thr_pshared_offpage(lock, 0) : *lock;
	if (lck == NULL)
		return (EINVAL);
	return (THR_UMUTEX_TRYLOCK(_get_curthread(), &lck->s_lock));
}

int
_pthread_spin_lock(pthread_spinlock_t *lock)
{
	struct pthread *curthread;
	struct pthread_spinlock	*lck;
	int count;

	if (lock == NULL)
		return (EINVAL);
	lck = *lock == THR_PSHARED_PTR ? __thr_pshared_offpage(lock, 0) : *lock;
	if (lck == NULL)
		return (EINVAL);

	curthread = _get_curthread();
	count = SPIN_COUNT;
	while (THR_UMUTEX_TRYLOCK(curthread, &lck->s_lock) != 0) {
		while (lck->s_lock.m_owner) {
			if (!_thr_is_smp) {
				_pthread_yield();
			} else {
				CPU_SPINWAIT;
				if (--count <= 0) {
					count = SPIN_COUNT;
					_pthread_yield();
				}
			}
		}
	}
	return (0);
}

int
_pthread_spin_unlock(pthread_spinlock_t *lock)
{
	struct pthread_spinlock	*lck;

	if (lock == NULL)
		return (EINVAL);
	lck = *lock == THR_PSHARED_PTR ? __thr_pshared_offpage(lock, 0) : *lock;
	if (lck == NULL)
		return (EINVAL);
	return (THR_UMUTEX_UNLOCK(_get_curthread(), &lck->s_lock));
}
