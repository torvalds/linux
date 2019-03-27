/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 John Birrell <jb@cimlogic.com.au>.
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
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

#include <sys/types.h>
#include <pthread.h>
#include <libc_private.h>
#include <spinlock.h>

#include "thr_private.h"

#define	MAX_SPINLOCKS	72

/*
 * These data structures are used to trace all spinlocks
 * in libc.
 */
struct spinlock_extra {
	spinlock_t	*owner;
	struct umutex	lock;
};

static struct umutex		spinlock_static_lock = DEFAULT_UMUTEX;
static struct spinlock_extra	extra[MAX_SPINLOCKS];
static int			spinlock_count;
static int			initialized;

static void	init_spinlock(spinlock_t *lck);

/*
 * These are for compatibility only.  Spinlocks of this type
 * are deprecated.
 */

void
__thr_spinunlock(spinlock_t *lck)
{
	struct spinlock_extra	*_extra;

	_extra = lck->thr_extra;
	THR_UMUTEX_UNLOCK(_get_curthread(), &_extra->lock);
}

void
__thr_spinlock(spinlock_t *lck)
{
	struct spinlock_extra *_extra;

	if (!_thr_isthreaded())
		PANIC("Spinlock called when not threaded.");
	if (!initialized)
		PANIC("Spinlocks not initialized.");
	if (lck->thr_extra == NULL)
		init_spinlock(lck);
	_extra = lck->thr_extra;
	THR_UMUTEX_LOCK(_get_curthread(), &_extra->lock);
}

static void
init_spinlock(spinlock_t *lck)
{
	struct pthread *curthread = _get_curthread();

	THR_UMUTEX_LOCK(curthread, &spinlock_static_lock);
	if ((lck->thr_extra == NULL) && (spinlock_count < MAX_SPINLOCKS)) {
		lck->thr_extra = &extra[spinlock_count];
		_thr_umutex_init(&extra[spinlock_count].lock);
		extra[spinlock_count].owner = lck;
		spinlock_count++;
	}
	THR_UMUTEX_UNLOCK(curthread, &spinlock_static_lock);
	if (lck->thr_extra == NULL)
		PANIC("Warning: exceeded max spinlocks");
}

void
_thr_spinlock_init(void)
{
	int i;

	_thr_umutex_init(&spinlock_static_lock);
	if (initialized != 0) {
		/*
		 * called after fork() to reset state of libc spin locks,
		 * it is not quite right since libc may be in inconsistent
		 * state, resetting the locks to allow current thread to be
		 * able to hold them may not help things too much, but
		 * anyway, we do our best.
		 * it is better to do pthread_atfork in libc.
		 */
		for (i = 0; i < spinlock_count; i++)
			_thr_umutex_init(&extra[i].lock);
	} else {
		initialized = 1;
	}
}
