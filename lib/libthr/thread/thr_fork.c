/*
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
 * Copyright (c) 2003 Daniel Eischen <deischen@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/syscall.h>
#include "namespace.h"
#include <errno.h>
#include <link.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <spinlock.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "rtld_lock.h"
#include "thr_private.h"

__weak_reference(_pthread_atfork, pthread_atfork);

int
_pthread_atfork(void (*prepare)(void), void (*parent)(void),
    void (*child)(void))
{
	struct pthread *curthread;
	struct pthread_atfork *af;

	_thr_check_init();

	if ((af = malloc(sizeof(struct pthread_atfork))) == NULL)
		return (ENOMEM);

	curthread = _get_curthread();
	af->prepare = prepare;
	af->parent = parent;
	af->child = child;
	THR_CRITICAL_ENTER(curthread);
	_thr_rwl_wrlock(&_thr_atfork_lock);
	TAILQ_INSERT_TAIL(&_thr_atfork_list, af, qe);
	_thr_rwl_unlock(&_thr_atfork_lock);
	THR_CRITICAL_LEAVE(curthread);
	return (0);
}

void
__pthread_cxa_finalize(struct dl_phdr_info *phdr_info)
{
	atfork_head    temp_list = TAILQ_HEAD_INITIALIZER(temp_list);
	struct pthread *curthread;
	struct pthread_atfork *af, *af1;

	_thr_check_init();

	curthread = _get_curthread();
	THR_CRITICAL_ENTER(curthread);
	_thr_rwl_wrlock(&_thr_atfork_lock);
	TAILQ_FOREACH_SAFE(af, &_thr_atfork_list, qe, af1) {
		if (__elf_phdr_match_addr(phdr_info, af->prepare) ||
		    __elf_phdr_match_addr(phdr_info, af->parent) ||
		    __elf_phdr_match_addr(phdr_info, af->child)) {
			TAILQ_REMOVE(&_thr_atfork_list, af, qe);
			TAILQ_INSERT_TAIL(&temp_list, af, qe);
		}
	}
	_thr_rwl_unlock(&_thr_atfork_lock);
	THR_CRITICAL_LEAVE(curthread);
	while ((af = TAILQ_FIRST(&temp_list)) != NULL) {
		TAILQ_REMOVE(&temp_list, af, qe);
		free(af);
	}
	_thr_tsd_unload(phdr_info);
	_thr_sigact_unload(phdr_info);
}

__weak_reference(__thr_fork, _fork);

pid_t
__thr_fork(void)
{
	struct pthread *curthread;
	struct pthread_atfork *af;
	pid_t ret;
	int errsave, cancelsave;
	int was_threaded;
	int rtld_locks[MAX_RTLD_LOCKS];

	if (!_thr_is_inited())
		return (__sys_fork());

	curthread = _get_curthread();
	cancelsave = curthread->no_cancel;
	curthread->no_cancel = 1;
	_thr_rwl_rdlock(&_thr_atfork_lock);

	/* Run down atfork prepare handlers. */
	TAILQ_FOREACH_REVERSE(af, &_thr_atfork_list, atfork_head, qe) {
		if (af->prepare != NULL)
			af->prepare();
	}

	/*
	 * Block all signals until we reach a safe point.
	 */
	_thr_signal_block(curthread);
	_thr_signal_prefork();

	/*
	 * All bets are off as to what should happen soon if the parent
	 * process was not so kindly as to set up pthread fork hooks to
	 * relinquish all running threads.
	 */
	if (_thr_isthreaded() != 0) {
		was_threaded = 1;
		__thr_malloc_prefork(curthread);
		_malloc_prefork();
		__thr_pshared_atfork_pre();
		_rtld_atfork_pre(rtld_locks);
	} else {
		was_threaded = 0;
	}

	/*
	 * Fork a new process.
	 * There is no easy way to pre-resolve the __sys_fork symbol
	 * without performing the fork.  Use the syscall(2)
	 * indirection, the syscall symbol is resolved in
	 * _thr_rtld_init() with side-effect free call.
	 */
	ret = syscall(SYS_fork);
	if (ret == 0) {
		/* Child process */
		errsave = errno;
		curthread->cancel_pending = 0;
		curthread->flags &= ~(THR_FLAGS_NEED_SUSPEND|THR_FLAGS_DETACHED);

		/*
		 * Thread list will be reinitialized, and later we call
		 * _libpthread_init(), it will add us back to list.
		 */
		curthread->tlflags &= ~TLFLAGS_IN_TDLIST;

		/* before thr_self() */
		if (was_threaded)
			__thr_malloc_postfork(curthread);

		/* child is a new kernel thread. */
		thr_self(&curthread->tid);

		/* clear other threads locked us. */
		_thr_umutex_init(&curthread->lock);
		_mutex_fork(curthread);

		_thr_signal_postfork_child();

		if (was_threaded) {
			_rtld_atfork_post(rtld_locks);
			__thr_pshared_atfork_post();
		}
		_thr_setthreaded(0);

		/* reinitalize library. */
		_libpthread_init(curthread);

		/* atfork is reinitialized by _libpthread_init()! */
		_thr_rwl_rdlock(&_thr_atfork_lock);

		if (was_threaded) {
			_thr_setthreaded(1);
			_malloc_postfork();
			_thr_setthreaded(0);
		}

		/* Ready to continue, unblock signals. */ 
		_thr_signal_unblock(curthread);

		/* Run down atfork child handlers. */
		TAILQ_FOREACH(af, &_thr_atfork_list, qe) {
			if (af->child != NULL)
				af->child();
		}
		_thr_rwlock_unlock(&_thr_atfork_lock);
		curthread->no_cancel = cancelsave;
	} else {
		/* Parent process */
		errsave = errno;

		_thr_signal_postfork();

		if (was_threaded) {
			__thr_malloc_postfork(curthread);
			_rtld_atfork_post(rtld_locks);
			__thr_pshared_atfork_post();
			_malloc_postfork();
		}

		/* Ready to continue, unblock signals. */ 
		_thr_signal_unblock(curthread);

		/* Run down atfork parent handlers. */
		TAILQ_FOREACH(af, &_thr_atfork_list, qe) {
			if (af->parent != NULL)
				af->parent();
		}

		_thr_rwlock_unlock(&_thr_atfork_lock);
		curthread->no_cancel = cancelsave;
		/* test async cancel */
		if (curthread->cancel_async)
			_thr_testcancel(curthread);
	}
	errno = errsave;

	return (ret);
}
