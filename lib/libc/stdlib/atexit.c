/*	$OpenBSD: atexit.c,v 1.29 2022/12/27 17:10:06 jmc Exp $ */
/*
 * Copyright (c) 2002 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <tib.h>
#include <unistd.h>

#include "atexit.h"
#include "atfork.h"
#include "thread_private.h"

struct atexit *__atexit;
static int restartloop;

/* define and initialize the list */
struct atfork_listhead _atfork_list = TAILQ_HEAD_INITIALIZER(_atfork_list);


/*
 * Function pointers are stored in a linked list of pages. The list
 * is initially empty, and pages are allocated on demand. The first
 * function pointer in the first allocated page (the last one in
 * the linked list) is reserved for the cleanup function.
 *
 * Outside the following functions, all pages are mprotect()'ed
 * to prevent unintentional/malicious corruption.
 */

/*
 * Register a function to be performed at exit or when a shared object
 * with the given dso handle is unloaded dynamically.  Also used as
 * the backend for atexit().  For more info on this API, see:
 *
 *	http://www.codesourcery.com/cxx-abi/abi.html#dso-dtor
 */
int
__cxa_atexit(void (*func)(void *), void *arg, void *dso)
{
	struct atexit *p;
	struct atexit_fn *fnp;
	int pgsize = getpagesize();
	int ret = -1;

	if (pgsize < sizeof(*p))
		return (-1);
	_ATEXIT_LOCK();
	p = __atexit;
	if (p != NULL) {
		if (p->ind + 1 >= p->max)
			p = NULL;
		else if (mprotect(p, pgsize, PROT_READ | PROT_WRITE))
			goto unlock;
	}
	if (p == NULL) {
		p = mmap(NULL, pgsize, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED)
			goto unlock;
		if (__atexit == NULL) {
			memset(&p->fns[0], 0, sizeof(p->fns[0]));
			p->ind = 1;
		} else
			p->ind = 0;
		p->max = (pgsize - ((char *)&p->fns[0] - (char *)p)) /
		    sizeof(p->fns[0]);
		p->next = __atexit;
		__atexit = p;
	}
	fnp = &p->fns[p->ind++];
	fnp->fn_ptr = func;
	fnp->fn_arg = arg;
	fnp->fn_dso = dso;
	if (mprotect(p, pgsize, PROT_READ))
		goto unlock;
	restartloop = 1;
	ret = 0;
unlock:
	_ATEXIT_UNLOCK();
	return (ret);
}
DEF_STRONG(__cxa_atexit);

/*
 * Copy of atexit() used by libc and anything statically linked into the
 * executable.  This passes NULL for the dso, so the callbacks are only
 * invoked by exit() and not dlclose()
 */
int
atexit(void (*fn)(void))
{
	return (__cxa_atexit((void (*)(void *))fn, NULL, NULL));
}
DEF_STRONG(atexit);

void
_thread_finalize(void)
{
	struct tib *tib = TIB_GET();

	while (tib->tib_atexit) {
		struct thread_atexit_fn *fnp = tib->tib_atexit;
		tib->tib_atexit = fnp->next;
		fnp->func(fnp->arg);
		free(fnp);
	}
}

/*
 * Call all handlers registered with __cxa_atexit() for the shared
 * object owning 'dso'.
 * Note: if 'dso' is NULL, then all remaining handlers are called.
 */
void
__cxa_finalize(void *dso)
{
	struct atexit *p, *q;
	struct atexit_fn fn;
	int n, pgsize = getpagesize();
	static int call_depth;

	if (dso == NULL)
		_thread_finalize();

	_ATEXIT_LOCK();
	call_depth++;

restart:
	restartloop = 0;
	for (p = __atexit; p != NULL; p = p->next) {
		for (n = p->ind; --n >= 0;) {
			if (p->fns[n].fn_ptr == NULL)
				continue;	/* already called */
			if (dso != NULL && dso != p->fns[n].fn_dso)
				continue;	/* wrong DSO */

			/*
			 * Mark handler as having been already called to avoid
			 * dupes and loops, then call the appropriate function.
			 */
			fn = p->fns[n];
			if (mprotect(p, pgsize, PROT_READ | PROT_WRITE) == 0) {
				p->fns[n].fn_ptr = NULL;
				mprotect(p, pgsize, PROT_READ);
			}
			_ATEXIT_UNLOCK();
			(*fn.fn_ptr)(fn.fn_arg);
			_ATEXIT_LOCK();
			if (restartloop)
				goto restart;
		}
	}

	call_depth--;

	/*
	 * If called via exit(), unmap the pages since we have now run
	 * all the handlers.  We defer this until calldepth == 0 so that
	 * we don't unmap things prematurely if called recursively.
	 */
	if (dso == NULL && call_depth == 0) {
		for (p = __atexit; p != NULL; ) {
			q = p;
			p = p->next;
			munmap(q, pgsize);
		}
		__atexit = NULL;
	}
	_ATEXIT_UNLOCK();

	/*
	 * If unloading a DSO, unregister any atfork handlers registered
	 * by it.  Skip the locking if the list is currently empty.
	 */
	if (dso != NULL && TAILQ_FIRST(&_atfork_list) != NULL) {
		struct atfork_fn *af, *afnext;

		_ATFORK_LOCK();
		TAILQ_FOREACH_SAFE(af, &_atfork_list, fn_next, afnext)
			if (af->fn_dso == dso) {
				TAILQ_REMOVE(&_atfork_list, af, fn_next);
				free(af);
			}
		_ATFORK_UNLOCK();

	}
}
DEF_STRONG(__cxa_finalize);

/*
 * Register the cleanup function
 */
void
__atexit_register_cleanup(void (*func)(void))
{
	struct atexit *p;
	int pgsize = getpagesize();

	if (pgsize < sizeof(*p))
		return;
	_ATEXIT_LOCK();
	p = __atexit;
	while (p != NULL && p->next != NULL)
		p = p->next;
	if (p == NULL) {
		p = mmap(NULL, pgsize, PROT_READ | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED)
			goto unlock;
		p->ind = 1;
		p->max = (pgsize - ((char *)&p->fns[0] - (char *)p)) /
		    sizeof(p->fns[0]);
		p->next = NULL;
		__atexit = p;
	} else {
		if (mprotect(p, pgsize, PROT_READ | PROT_WRITE))
			goto unlock;
	}
	p->fns[0].fn_ptr = (void (*)(void *))func;
	p->fns[0].fn_arg = NULL;
	p->fns[0].fn_dso = NULL;
	mprotect(p, pgsize, PROT_READ);
	restartloop = 1;
unlock:
	_ATEXIT_UNLOCK();
}
