/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995-1998 John Birrell <jb@cimlogic.com.au>
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

#include "namespace.h"
#include <errno.h>
#ifdef _PTHREAD_FORCED_UNWIND
#include <dlfcn.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/signalvar.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "thr_private.h"

static void	exit_thread(void) __dead2;

__weak_reference(_pthread_exit, pthread_exit);

#ifdef _PTHREAD_FORCED_UNWIND
static int message_printed;

static void thread_unwind(void) __dead2;
#ifdef PIC
static void thread_uw_init(void);
static _Unwind_Reason_Code thread_unwind_stop(int version,
	_Unwind_Action actions,
	int64_t exc_class,
	struct _Unwind_Exception *exc_obj,
	struct _Unwind_Context *context, void *stop_parameter);
/* unwind library pointers */
static _Unwind_Reason_Code (*uwl_forcedunwind)(struct _Unwind_Exception *,
	_Unwind_Stop_Fn, void *);
static unsigned long (*uwl_getcfa)(struct _Unwind_Context *);

static void
thread_uw_init(void)
{
	static int inited = 0;
	Dl_info dli;
	void *handle;
	void *forcedunwind, *getcfa;

	if (inited)
	    return;
	handle = RTLD_DEFAULT;
	if ((forcedunwind = dlsym(handle, "_Unwind_ForcedUnwind")) != NULL) {
	    if (dladdr(forcedunwind, &dli)) {
		/*
		 * Make sure the address is always valid by holding the library,
		 * also assume functions are in same library.
		 */
		if ((handle = dlopen(dli.dli_fname, RTLD_LAZY)) != NULL) {
		    forcedunwind = dlsym(handle, "_Unwind_ForcedUnwind");
		    getcfa = dlsym(handle, "_Unwind_GetCFA");
		    if (forcedunwind != NULL && getcfa != NULL) {
			uwl_getcfa = getcfa;
			atomic_store_rel_ptr((volatile void *)&uwl_forcedunwind,
				(uintptr_t)forcedunwind);
		    } else {
			dlclose(handle);
		    }
		}
	    }
	}
	inited = 1;
}

_Unwind_Reason_Code
_Unwind_ForcedUnwind(struct _Unwind_Exception *ex, _Unwind_Stop_Fn stop_func,
	void *stop_arg)
{
	return (*uwl_forcedunwind)(ex, stop_func, stop_arg);
}

unsigned long
_Unwind_GetCFA(struct _Unwind_Context *context)
{
	return (*uwl_getcfa)(context);
}
#else
#pragma weak _Unwind_GetCFA
#pragma weak _Unwind_ForcedUnwind
#endif /* PIC */

static void
thread_unwind_cleanup(_Unwind_Reason_Code code __unused,
    struct _Unwind_Exception *e __unused)
{
	/*
	 * Specification said that _Unwind_Resume should not be used here,
	 * instead, user should rethrow the exception. For C++ user, they
	 * should put "throw" sentence in catch(...) block.
	 */
	PANIC("exception should be rethrown");
}

static _Unwind_Reason_Code
thread_unwind_stop(int version __unused, _Unwind_Action actions,
	int64_t exc_class __unused,
	struct _Unwind_Exception *exc_obj __unused,
	struct _Unwind_Context *context, void *stop_parameter __unused)
{
	struct pthread *curthread = _get_curthread();
	struct pthread_cleanup *cur;
	uintptr_t cfa;
	int done = 0;

	/* XXX assume stack grows down to lower address */

	cfa = _Unwind_GetCFA(context);
	if (actions & _UA_END_OF_STACK ||
	    cfa >= (uintptr_t)curthread->unwind_stackend) {
		done = 1;
	}

	while ((cur = curthread->cleanup) != NULL &&
	       (done || (uintptr_t)cur <= cfa)) {
		__pthread_cleanup_pop_imp(1);
	}

	if (done) {
		/* Tell libc that it should call non-trivial TLS dtors. */
		__cxa_thread_call_dtors();

		exit_thread(); /* Never return! */
	}

	return (_URC_NO_REASON);
}

static void
thread_unwind(void)
{
	struct pthread  *curthread = _get_curthread();

	curthread->ex.exception_class = 0;
	curthread->ex.exception_cleanup = thread_unwind_cleanup;
	_Unwind_ForcedUnwind(&curthread->ex, thread_unwind_stop, NULL);
	PANIC("_Unwind_ForcedUnwind returned");
}

#endif

void
_thread_exitf(const char *fname, int lineno, const char *fmt, ...)
{
	va_list ap;

	/* Write an error message to the standard error file descriptor: */
	_thread_printf(STDERR_FILENO, "Fatal error '");

	va_start(ap, fmt);
	_thread_vprintf(STDERR_FILENO, fmt, ap);
	va_end(ap);

	_thread_printf(STDERR_FILENO, "' at line %d in file %s (errno = %d)\n",
	    lineno, fname, errno);

	abort();
}

void
_thread_exit(const char *fname, int lineno, const char *msg)
{

	_thread_exitf(fname, lineno, "%s", msg);
}

void
_pthread_exit(void *status)
{
	_pthread_exit_mask(status, NULL);
}

void
_pthread_exit_mask(void *status, sigset_t *mask)
{
	struct pthread *curthread = _get_curthread();

	/* Check if this thread is already in the process of exiting: */
	if (curthread->cancelling)
		PANIC("Thread %p has called "
		    "pthread_exit() from a destructor. POSIX 1003.1 "
		    "1996 s16.2.5.2 does not allow this!", curthread);

	/* Flag this thread as exiting. */
	curthread->cancelling = 1;
	curthread->no_cancel = 1;
	curthread->cancel_async = 0;
	curthread->cancel_point = 0;
	if (mask != NULL)
		__sys_sigprocmask(SIG_SETMASK, mask, NULL);
	if (curthread->unblock_sigcancel) {
		sigset_t set;

		curthread->unblock_sigcancel = 0;
		SIGEMPTYSET(set);
		SIGADDSET(set, SIGCANCEL);
		__sys_sigprocmask(SIG_UNBLOCK, mask, NULL);
	}
	
	/* Save the return value: */
	curthread->ret = status;
#ifdef _PTHREAD_FORCED_UNWIND

#ifdef PIC
	thread_uw_init();
	if (uwl_forcedunwind != NULL) {
#else
	if (_Unwind_ForcedUnwind != NULL) {
#endif
		if (curthread->unwind_disabled) {
			if (message_printed == 0) {
				message_printed = 1;
				_thread_printf(2, "Warning: old _pthread_cleanup_push was called, "
				  	"stack unwinding is disabled.\n");
			}
			goto cleanup;
		}
		thread_unwind();

	} else {
cleanup:
		while (curthread->cleanup != NULL) {
			__pthread_cleanup_pop_imp(1);
		}
		__cxa_thread_call_dtors();

		exit_thread();
	}

#else
	while (curthread->cleanup != NULL) {
		__pthread_cleanup_pop_imp(1);
	}
	__cxa_thread_call_dtors();

	exit_thread();
#endif /* _PTHREAD_FORCED_UNWIND */
}

static void
exit_thread(void)
{
	struct pthread *curthread = _get_curthread();

	free(curthread->name);
	curthread->name = NULL;

	/* Check if there is thread specific data: */
	if (curthread->specific != NULL) {
		/* Run the thread-specific data destructors: */
		_thread_cleanupspecific();
	}

	if (!_thr_isthreaded())
		exit(0);

	if (atomic_fetchadd_int(&_thread_active_threads, -1) == 1) {
		exit(0);
		/* Never reach! */
	}

	/* Tell malloc that the thread is exiting. */
	_malloc_thread_cleanup();

	THR_LOCK(curthread);
	curthread->state = PS_DEAD;
	if (curthread->flags & THR_FLAGS_NEED_SUSPEND) {
		curthread->cycle++;
		_thr_umtx_wake(&curthread->cycle, INT_MAX, 0);
	}
	if (!curthread->force_exit && SHOULD_REPORT_EVENT(curthread, TD_DEATH))
		_thr_report_death(curthread);
	/*
	 * Thread was created with initial refcount 1, we drop the
	 * reference count to allow it to be garbage collected.
	 */
	curthread->refcount--;
	_thr_try_gc(curthread, curthread); /* thread lock released */

#if defined(_PTHREADS_INVARIANTS)
	if (THR_IN_CRITICAL(curthread))
		PANIC("thread %p exits with resources held!", curthread);
#endif
	/*
	 * Kernel will do wakeup at the address, so joiner thread
	 * will be resumed if it is sleeping at the address.
	 */
	thr_exit(&curthread->tid);
	PANIC("thr_exit() returned");
	/* Never reach! */
}
