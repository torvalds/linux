/*	$OpenBSD: rthread.c,v 1.101 2025/07/17 02:21:44 deraadt Exp $ */
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
 * The heart of rthreads.  Basic functions like creating and joining
 * threads.
 */

#include <sys/types.h>
#ifndef NO_PIC
#include <elf.h>
#pragma weak _DYNAMIC
#endif
#ifdef __PROFIL_SRC__
#include <sys/gmon.h>
#endif

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include <tib.h>

#include <pthread.h>

#include "cancel.h"		/* in libc/include */
#include "rthread.h"
#include "rthread_cb.h"

/*
 * Call nonstandard functions via names in the reserved namespace:
 *	dlctl() -> _dlctl()
 *	getthrid -> _thread_sys_getthrid
 */
typeof(dlctl) dlctl asm("_dlctl") __attribute__((weak));
REDIRECT_SYSCALL(getthrid);

/* weak stub to be overridden by ld.so */
int	dlctl(void *handle, int cmd, void *data) { return 0; }

/*
 * libc's signal wrappers hide SIGTHR; we need to call the real syscall
 * stubs _thread_sys_* directly.
 */
REDIRECT_SYSCALL(sigaction);
REDIRECT_SYSCALL(sigprocmask);
REDIRECT_SYSCALL(thrkill);

static int concurrency_level;	/* not used */

int _threads_ready;
int _post_threaded;
size_t _thread_pagesize;
struct listhead _thread_list = LIST_HEAD_INITIALIZER(_thread_list);
_atomic_lock_t _thread_lock = _SPINLOCK_UNLOCKED;
static struct pthread_queue _thread_gc_list
    = TAILQ_HEAD_INITIALIZER(_thread_gc_list);
static _atomic_lock_t _thread_gc_lock = _SPINLOCK_UNLOCKED;
static struct pthread _initial_thread;

struct pthread_attr _rthread_attr_default = {
	.stack_addr			= NULL,
	.stack_size			= RTHREAD_STACK_SIZE_DEF,
/*	.guard_size		set in _rthread_init */
	.detach_state			= PTHREAD_CREATE_JOINABLE,
	.contention_scope		= PTHREAD_SCOPE_SYSTEM,
	.sched_policy			= SCHED_OTHER,
	.sched_param = { .sched_priority = 0 },
	.sched_inherit			= PTHREAD_INHERIT_SCHED,
};

/*
 * internal support functions
 */

static void
_rthread_start(void *v)
{
	pthread_t thread = v;
	void *retval;

	retval = thread->fn(thread->arg);
	pthread_exit(retval);
}

static void
sigthr_handler(__unused int sig)
{
	struct tib *tib = TIB_GET();
	pthread_t self = tib->tib_thread;

	/*
	 * Do nothing unless
	 * 1) pthread_cancel() has been called on this thread,
	 * 2) cancelation is enabled for it, and
	 * 3) we're not already in cancelation processing
	 */
	if (!tib->tib_canceled || tib->tib_cantcancel)
		return;

	/*
	 * If delaying cancels inside complex ops (pthread_cond_wait,
	 * pthread_join, etc), just mark that this has happened to
	 * prevent a race with going to sleep
	 */
	if (tib->tib_cancel_point & CANCEL_POINT_DELAYED) {
		self->delayed_cancel = 1;
		return;
	}

	/*
	 * otherwise, if in a cancel point or async cancels are
	 * enabled, then exit
	 */
	if (tib->tib_cancel_point ||
	    (tib->tib_thread_flags & TIB_THREAD_ASYNC_CANCEL))
		pthread_exit(PTHREAD_CANCELED);
}


/*
 * A few basic callbacks for libc.  The first couple are only used
 * on archs where there isn't a fast TCB_GET()
 */
#ifndef TCB_HAVE_MD_GET
static int *
multi_threaded_errnoptr(void)
{
        return (&TIB_GET()->tib_errno);
}

static void *
multi_threaded_tcb(void)
{
	return (TCB_GET());
}
#endif /* TCB_HAVE_MD_GET */

static void
_rthread_free(pthread_t thread)
{
	_spinlock(&_thread_gc_lock);
	TAILQ_INSERT_TAIL(&_thread_gc_list, thread, waiting);
	_spinunlock(&_thread_gc_lock);
}

static void
_thread_release(pthread_t thread)
{
	_spinlock(&_thread_lock);
	LIST_REMOVE(thread, threads);
	_spinunlock(&_thread_lock);

	_spinlock(&thread->flags_lock);
	if (thread->flags & THREAD_DETACHED) {
		_spinunlock(&thread->flags_lock);
		_rthread_free(thread);
	} else {
		thread->flags |= THREAD_DONE;
		_spinunlock(&thread->flags_lock);
		_sem_post(&thread->donesem);
	}
}

static void
_thread_key_zero(int key)
{
	pthread_t thread;
	struct rthread_storage *rs;

	LIST_FOREACH(thread, &_thread_list, threads) {
		for (rs = thread->local_storage; rs; rs = rs->next) {
			if (rs->keyid == key)
				rs->data = NULL;
		}
	}
}

void
_rthread_init(void)
{
	pthread_t thread = pthread_self();
	struct sigaction sa;

	if (_threads_ready)
		return;

	LIST_INSERT_HEAD(&_thread_list, thread, threads);

	_thread_pagesize = (size_t)sysconf(_SC_PAGESIZE);
	_rthread_attr_default.guard_size = _thread_pagesize;
	thread->attr = _rthread_attr_default;

	/* get libc to start using our callbacks */
	{
		struct thread_callbacks cb = { 0 };

#ifndef TCB_HAVE_MD_GET
		cb.tc_errnoptr		= multi_threaded_errnoptr;
		cb.tc_tcb		= multi_threaded_tcb;
#endif
		cb.tc_fork		= _thread_fork;
		cb.tc_vfork		= _thread_vfork;
		cb.tc_thread_release	= _thread_release;
		cb.tc_thread_key_zero	= _thread_key_zero;
		_thread_set_callbacks(&cb, sizeof(cb));
	}

#ifndef NO_PIC
	if (_DYNAMIC) {
		dlctl(NULL, DL_SETTHREADLCK, _rthread_dl_lock);
	}
#endif

	/*
	 * Set the handler on the signal used for cancelation and
	 * suspension, and make sure it's unblocked
	 */
	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigthr_handler;
	sigaction(SIGTHR, &sa, NULL);
	sigaddset(&sa.sa_mask, SIGTHR);
	sigprocmask(SIG_UNBLOCK, &sa.sa_mask, NULL);

	_threads_ready = 1;

	_malloc_init(1);

	_rthread_debug(1, "rthread init\n");
}

static void
_rthread_reaper(void)
{
	pthread_t thread;

restart:
	_spinlock(&_thread_gc_lock);
	TAILQ_FOREACH(thread, &_thread_gc_list, waiting) {
		if (thread->tib->tib_tid != 0)
			continue;
		TAILQ_REMOVE(&_thread_gc_list, thread, waiting);
		_spinunlock(&_thread_gc_lock);
		if (thread != &_initial_thread) {
			_rthread_debug(3, "rthread reaping %p stack %p\n",
			    (void *)thread, (void *)thread->stack);
			_rthread_free_stack(thread->stack);
			_dl_free_tib(thread->tib, sizeof(*thread));
		} else {
			/* initial thread isn't part of TIB allocation */
			_rthread_debug(3, "rthread reaping %p (initial)\n",
			    (void *)thread);
			_dl_free_tib(thread->tib, 0);
		}
		goto restart;
	}
	_spinunlock(&_thread_gc_lock);
}

/*
 * real pthread functions
 */

int
pthread_join(pthread_t thread, void **retval)
{
	int e;
	struct tib *tib = TIB_GET();
	pthread_t self;
	PREP_CANCEL_POINT(tib);

	if (_post_threaded) {
#define GREATSCOTT "great scott! serious repercussions on future events!\n"
		write(2, GREATSCOTT, sizeof(GREATSCOTT) - 1);
		abort();
	}
	if (!_threads_ready)
		_rthread_init();
	self = tib->tib_thread;

	e = 0;
	ENTER_DELAYED_CANCEL_POINT(tib, self);
	if (thread == NULL)
		e = EINVAL;
	else if (thread == self)
		e = EDEADLK;
	else if (thread->flags & THREAD_DETACHED)
		e = EINVAL;
	else if ((e = _sem_wait(&thread->donesem, 0, NULL,
	    &self->delayed_cancel)) == 0) {
		if (retval)
			*retval = thread->retval;

		/*
		 * We should be the last having a ref to this thread,
		 * but someone stupid or evil might haved detached it;
		 * in that case the thread will clean up itself
		 */
		if ((thread->flags & THREAD_DETACHED) == 0)
			_rthread_free(thread);
	}

	LEAVE_CANCEL_POINT_INNER(tib, e);
	_rthread_reaper();
	return (e);
}

int
pthread_detach(pthread_t thread)
{
	int rc = 0;

	_spinlock(&thread->flags_lock);
	if (thread->flags & THREAD_DETACHED) {
		rc = EINVAL;
		_spinunlock(&thread->flags_lock);
	} else if (thread->flags & THREAD_DONE) {
		_spinunlock(&thread->flags_lock);
		_rthread_free(thread);
	} else {
		thread->flags |= THREAD_DETACHED;
		_spinunlock(&thread->flags_lock);
	}
	_rthread_reaper();
	return (rc);
}

int
pthread_create(pthread_t *threadp, const pthread_attr_t *attr,
    void *(*start_routine)(void *), void *arg)
{
	extern int __isthreaded;
	struct tib *tib;
	pthread_t thread;
	struct __tfork param;
	int rc;

	if (!_threads_ready)
		_rthread_init();

	_rthread_reaper();

	tib = _dl_allocate_tib(sizeof(*thread));
	if (tib == NULL)
		return (ENOMEM);
	thread = tib->tib_thread;
	memset(thread, 0, sizeof(*thread));
	thread->tib = tib;
	thread->donesem.lock = _SPINLOCK_UNLOCKED;
	thread->flags_lock = _SPINLOCK_UNLOCKED;
	thread->fn = start_routine;
	thread->arg = arg;
	tib->tib_tid = -1;

	thread->attr = attr != NULL ? *(*attr) : _rthread_attr_default;
	if (thread->attr.sched_inherit == PTHREAD_INHERIT_SCHED) {
		pthread_t self = pthread_self();

		thread->attr.sched_policy = self->attr.sched_policy;
		thread->attr.sched_param = self->attr.sched_param;
	}
	if (thread->attr.detach_state == PTHREAD_CREATE_DETACHED)
		thread->flags |= THREAD_DETACHED;

	thread->stack = _rthread_alloc_stack(thread);
	if (!thread->stack) {
		rc = errno;
		goto fail1;
	}

	param.tf_tcb = TIB_TO_TCB(tib);
	param.tf_tid = &tib->tib_tid;
	param.tf_stack = thread->stack->sp;

	_spinlock(&_thread_lock);
	LIST_INSERT_HEAD(&_thread_list, thread, threads);
	_spinunlock(&_thread_lock);

	/* we're going to be multi-threaded real soon now */
	__isthreaded = 1;

#ifdef __PROFIL_SRC__
	/* Ignore errors. NULL is OK for a non-profiling case. */
	thread->gmonparam = _gmon_alloc();
#endif

	rc = __tfork_thread(&param, sizeof(param), _rthread_start, thread);
	if (rc != -1) {
		/* success */
		*threadp = thread;
		return (0);
	}
		
	rc = errno;

	_spinlock(&_thread_lock);
	LIST_REMOVE(thread, threads);
	_spinunlock(&_thread_lock);
	_rthread_free_stack(thread->stack);
fail1:
	_dl_free_tib(tib, sizeof(*thread));

	return (rc);
}

int
pthread_kill(pthread_t thread, int sig)
{
	struct tib *tib = thread->tib;

	if (sig == SIGTHR)
		return (EINVAL);
	if (thrkill(tib->tib_tid, sig, TIB_TO_TCB(tib)))
		return (errno);
	return (0);
}

int
pthread_cancel(pthread_t thread)
{
	struct tib *tib = thread->tib;
	pid_t tid = tib->tib_tid;

	if (tib->tib_canceled == 0 && tid != 0 &&
	    (tib->tib_cantcancel & CANCEL_DYING) == 0) {
		tib->tib_canceled = 1;

		if ((tib->tib_cantcancel & CANCEL_DISABLED) == 0) {
			thrkill(tid, SIGTHR, TIB_TO_TCB(tib));
			return (0);
		}
	}
	return (0);
}

void
pthread_testcancel(void)
{
	struct tib *tib = TIB_GET();

	if (tib->tib_canceled && (tib->tib_cantcancel & CANCEL_DISABLED) == 0)
		pthread_exit(PTHREAD_CANCELED);
}

int
pthread_setcancelstate(int state, int *oldstatep)
{
	struct tib *tib = TIB_GET();
	int oldstate;

	oldstate = tib->tib_cantcancel & CANCEL_DISABLED ?
	    PTHREAD_CANCEL_DISABLE : PTHREAD_CANCEL_ENABLE;
	if (state == PTHREAD_CANCEL_ENABLE) {
		tib->tib_cantcancel &= ~CANCEL_DISABLED;
	} else if (state == PTHREAD_CANCEL_DISABLE) {
		tib->tib_cantcancel |= CANCEL_DISABLED;
	} else {
		return (EINVAL);
	}
	if (oldstatep)
		*oldstatep = oldstate;

	return (0);
}
DEF_STD(pthread_setcancelstate);

int
pthread_setcanceltype(int type, int *oldtypep)
{
	struct tib *tib = TIB_GET();
	int oldtype;

	oldtype = tib->tib_thread_flags & TIB_THREAD_ASYNC_CANCEL ?
	    PTHREAD_CANCEL_ASYNCHRONOUS : PTHREAD_CANCEL_DEFERRED;
	if (type == PTHREAD_CANCEL_DEFERRED) {
		tib->tib_thread_flags &=~ TIB_THREAD_ASYNC_CANCEL;
	} else if (type == PTHREAD_CANCEL_ASYNCHRONOUS) {
		tib->tib_thread_flags |= TIB_THREAD_ASYNC_CANCEL;
	} else {
		return (EINVAL);
	}
	if (oldtypep)
		*oldtypep = oldtype;

	return (0);
}

void
pthread_cleanup_push(void (*fn)(void *), void *arg)
{
	struct rthread_cleanup_fn *clfn;
	pthread_t self = pthread_self();

	clfn = calloc(1, sizeof(*clfn));
	if (!clfn)
		return;
	clfn->fn = fn;
	clfn->arg = arg;
	clfn->next = self->cleanup_fns;
	self->cleanup_fns = clfn;
}

void
pthread_cleanup_pop(int execute)
{
	struct rthread_cleanup_fn *clfn;
	pthread_t self = pthread_self();

	clfn = self->cleanup_fns;
	if (clfn) {
		self->cleanup_fns = clfn->next;
		if (execute)
			clfn->fn(clfn->arg);
		free(clfn);
	}
}

int
pthread_getconcurrency(void)
{
	return (concurrency_level);
}

int
pthread_setconcurrency(int new_level)
{
	if (new_level < 0)
		return (EINVAL);
	concurrency_level = new_level;
	return (0);
}

/*
 * compat debug stuff
 */
void
_thread_dump_info(void)
{
	pthread_t thread;

	_spinlock(&_thread_lock);
	LIST_FOREACH(thread, &_thread_list, threads)
		printf("thread %d flags 0x%x name %s\n", thread->tib->tib_tid,
		    thread->tib->tib_thread_flags, thread->name);
	_spinunlock(&_thread_lock);
}

#ifndef NO_PIC
/*
 * _rthread_dl_lock() provides the locking for dlopen(), dlclose(), and
 * the function called via atexit() to invoke all destructors.  The latter
 * two call shared-object destructors, which may need to call dlclose(),
 * so this lock needs to permit recursive locking.
 * The specific code here was extracted from _rthread_mutex_lock() and
 * pthread_mutex_unlock() and simplified to use the static variables.
 */
void
_rthread_dl_lock(int what)
{
	static _atomic_lock_t lock = _SPINLOCK_UNLOCKED;
	static pthread_t owner = NULL;
	static struct pthread_queue lockers = TAILQ_HEAD_INITIALIZER(lockers);
	static int count = 0;

	if (what == 0) {
		pthread_t self = pthread_self();

		/* lock, possibly recursive */
		_spinlock(&lock);
		if (owner == NULL) {
			owner = self;
		} else if (owner != self) {
			TAILQ_INSERT_TAIL(&lockers, self, waiting);
			while (owner != self) {
				__thrsleep(self, 0, NULL, &lock, NULL);
				_spinlock(&lock);
			}
		}
		count++;
		_spinunlock(&lock);
	} else if (what == 1) {
		/* unlock, possibly recursive */
		if (--count == 0) {
			pthread_t next;

			_spinlock(&lock);
			owner = next = TAILQ_FIRST(&lockers);
			if (next != NULL)
				TAILQ_REMOVE(&lockers, next, waiting);
			_spinunlock(&lock);
			if (next != NULL)
				__thrwakeup(next, 1);
		}
	} else {
		/* reinit: used in child after fork to clear the queue */
		lock = _SPINLOCK_UNLOCKED;
		if (--count == 0)
			owner = NULL;
		TAILQ_INIT(&lockers);
	}
}
#endif
