/*	$OpenBSD: rthread.c,v 1.11 2025/07/16 16:22:58 deraadt Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
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
 * The infrastructure of rthreads
 */

#include <sys/types.h>
#include <sys/futex.h>
#include <sys/atomic.h>
#include <sys/gmon.h>

#include <pthread.h>
#include <stdlib.h>
#include <tib.h>
#include <unistd.h>
#include <assert.h>

#include "rthread.h"

#define RTHREAD_ENV_DEBUG	"RTHREAD_DEBUG"

int _rthread_debug_level;

static int _threads_inited;

struct pthread _initial_thread = {
	.flags_lock = _SPINLOCK_UNLOCKED,
	.name = "Original thread",
};

/*
 * internal support functions
 */
void
_spinlock(volatile _atomic_lock_t *lock)
{
	while (_atomic_lock(lock))
		sched_yield();
	membar_enter_after_atomic();
}
DEF_STRONG(_spinlock);

int
_spinlocktry(volatile _atomic_lock_t *lock)
{
	if (_atomic_lock(lock) == 0) {
		membar_enter_after_atomic();
		return 1;
	}
	return 0;
}

void
_spinunlock(volatile _atomic_lock_t *lock)
{
	membar_exit();
	*lock = _ATOMIC_LOCK_UNLOCKED;
}
DEF_STRONG(_spinunlock);

#ifdef __CMTX_CAS

/*
 * CAS+futex locks
 */

void
__cmtx_init(struct __cmtx *cmtx)
{
	cmtx->lock = __CMTX_UNLOCKED;
}

int
__cmtx_enter_try(struct __cmtx *cmtx)
{
	if (atomic_cas_uint(&cmtx->lock,
	    __CMTX_UNLOCKED, __CMTX_LOCKED) == __CMTX_UNLOCKED) {
		membar_enter_after_atomic();
		return (1);
	}

	return (0);
}

void
__cmtx_enter(struct __cmtx *cmtx)
{
	unsigned int locked;

	locked = atomic_cas_uint(&cmtx->lock,
	    __CMTX_UNLOCKED, __CMTX_LOCKED);
	if (locked == __CMTX_UNLOCKED) {
		membar_enter_after_atomic();
		return;
	}

	/* add adaptive spin here */

	do {
		switch (locked) {
		case __CMTX_LOCKED:
			locked = atomic_cas_uint(&cmtx->lock,
			    __CMTX_LOCKED, __CMTX_CONTENDED);
			if (locked == __CMTX_UNLOCKED)
				break;

			/* lock is LOCKED -> CONTENDED or was CONTENDED */
			/* FALLTHROUGH */
		case __CMTX_CONTENDED:
			futex(&cmtx->lock, FUTEX_WAIT_PRIVATE,
			    __CMTX_CONTENDED, NULL, NULL);
			break;
		}

		locked = atomic_cas_uint(&cmtx->lock,
		    __CMTX_UNLOCKED, __CMTX_CONTENDED);
	} while (locked != __CMTX_UNLOCKED);

	membar_enter_after_atomic();
}

void
__cmtx_leave(struct __cmtx *cmtx)
{
	unsigned int locked;

	membar_exit_before_atomic();
	locked = atomic_cas_uint(&cmtx->lock,
	    __CMTX_LOCKED, __CMTX_UNLOCKED);
	if (locked != __CMTX_LOCKED) {
		assert(locked != __CMTX_UNLOCKED);
		cmtx->lock = __CMTX_UNLOCKED;
		futex(&cmtx->lock, FUTEX_WAKE_PRIVATE, 1, NULL, NULL);
	}
}

#else /* __CMTX_CAS */

/*
 * spinlock+futex locks
 */

void
__cmtx_init(struct __cmtx *cmtx)
{
	cmtx->spin = _SPINLOCK_UNLOCKED;
	cmtx->lock = __CMTX_UNLOCKED;
}

int
__cmtx_enter_try(struct __cmtx *cmtx)
{
	unsigned int locked;

	_spinlock(&cmtx->spin);
	locked = cmtx->lock;
	if (locked == __CMTX_UNLOCKED)
		cmtx->lock = __CMTX_LOCKED;
	_spinunlock(&cmtx->spin);

	/* spinlocks provide enough membars */

	return (locked == __CMTX_UNLOCKED);
}

void
__cmtx_enter(struct __cmtx *cmtx)
{
	unsigned int locked;

	_spinlock(&cmtx->spin);
	locked = cmtx->lock;
	switch (locked) {
	case __CMTX_UNLOCKED:
		cmtx->lock = __CMTX_LOCKED;
		break;
	case __CMTX_LOCKED:
		cmtx->lock = __CMTX_CONTENDED;
		break;
	}
	_spinunlock(&cmtx->spin);

	while (locked != __CMTX_UNLOCKED) {
		futex(&cmtx->lock, FUTEX_WAIT_PRIVATE,
		    __CMTX_CONTENDED, NULL, NULL);

		_spinlock(&cmtx->spin);
		locked = cmtx->lock;
		switch (locked) {
		case __CMTX_UNLOCKED:
		case __CMTX_LOCKED:
			cmtx->lock = __CMTX_CONTENDED;
			break;
		}
		_spinunlock(&cmtx->spin);
	}

	/* spinlocks provide enough membars */
}

void
__cmtx_leave(struct __cmtx *cmtx)
{
	unsigned int locked;

	/* spinlocks provide enough membars */

	_spinlock(&cmtx->spin);
	locked = cmtx->lock;
	cmtx->lock = __CMTX_UNLOCKED;
	_spinunlock(&cmtx->spin);

	if (locked != __CMTX_LOCKED) {
		assert(locked != __CMTX_UNLOCKED);
		futex(&cmtx->lock, FUTEX_WAKE_PRIVATE, 1, NULL, NULL);
	}
}

#endif /* __CMTX_CAS */

/*
 * recursive mutex
 */

void
__rcmtx_init(struct __rcmtx *rcmtx)
{
	__cmtx_init(&rcmtx->mtx);
	rcmtx->owner = NULL;
	rcmtx->depth = 0;
}

int
__rcmtx_enter_try(struct __rcmtx *rcmtx)
{
	pthread_t self = pthread_self();

	if (rcmtx->owner != self) {
		if (__cmtx_enter_try(&rcmtx->mtx) == 0)
			return (0);
		assert(rcmtx->owner == NULL);
		rcmtx->owner = self;
		assert(rcmtx->depth == 0);
	}

	rcmtx->depth++;

	return (1);
}

void
__rcmtx_enter(struct __rcmtx *rcmtx)
{
	pthread_t self = pthread_self();

	if (rcmtx->owner != self) {
		__cmtx_enter(&rcmtx->mtx);
		assert(rcmtx->owner == NULL);
		rcmtx->owner = self;
		assert(rcmtx->depth == 0);
	}

	rcmtx->depth++;
}

void
__rcmtx_leave(struct __rcmtx *rcmtx)
{
	assert(rcmtx->owner == pthread_self());
	if (--rcmtx->depth == 0) {
		rcmtx->owner = NULL;
		__cmtx_leave(&rcmtx->mtx);
	}
}

static void
_rthread_init(void)
{
	pthread_t thread = &_initial_thread;
	struct tib *tib;

	if (_threads_inited)
		return;

	tib = TIB_GET();
	tib->tib_thread = thread;
	thread->tib = tib;

	thread->donesem.lock = _SPINLOCK_UNLOCKED;
	tib->tib_thread_flags = TIB_THREAD_INITIAL_STACK;

	/*
	 * Set the debug level from an environment string.
	 * Bogus values are silently ignored.
	 */
	if (!issetugid()) {
		char *envp = getenv(RTHREAD_ENV_DEBUG);

		if (envp != NULL) {
			char *rem;

			_rthread_debug_level = (int) strtol(envp, &rem, 0);
			if (*rem != '\0' || _rthread_debug_level < 0)
				_rthread_debug_level = 0;
		}
	}

	_threads_inited = 1;

	/* Ignore errors. NULL is OK for a non-profiling case. */
	thread->gmonparam = _gmon_alloc();
}

/*
 * real pthread functions
 */
pthread_t
pthread_self(void)
{
	if (__predict_false(!_threads_inited))
		_rthread_init();

	return TIB_GET()->tib_thread;
}
DEF_STRONG(pthread_self);

void
pthread_exit(void *retval)
{
	struct rthread_cleanup_fn *clfn;
	struct tib *tib;
	pthread_t thread = pthread_self();

	tib = thread->tib;

	if (tib->tib_cantcancel & CANCEL_DYING) {
		/*
		 * Called pthread_exit() from destructor or cancelation
		 * handler: blow up.  XXX write something to stderr?
		 */
		abort();
		//_exit(42);
	}

	tib->tib_cantcancel |= CANCEL_DYING;

	thread->retval = retval;

	for (clfn = thread->cleanup_fns; clfn; ) {
		struct rthread_cleanup_fn *oclfn = clfn;
		clfn = clfn->next;
		oclfn->fn(oclfn->arg);
		free(oclfn);
	}
	_thread_finalize();
	_rthread_tls_destructors(thread);

	if (_thread_cb.tc_thread_release != NULL)
		_thread_cb.tc_thread_release(thread);

	__threxit(&tib->tib_tid);
	for(;;);
}
DEF_STRONG(pthread_exit);

int
pthread_equal(pthread_t t1, pthread_t t2)
{
	return (t1 == t2);
}

