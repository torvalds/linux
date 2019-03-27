/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)atexit.c	8.2 (Berkeley) 7/3/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <errno.h>
#include <link.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "atexit.h"
#include "un-namespace.h"
#include "block_abi.h"

#include "libc_private.h"

/**
 * The _Block_copy() function is provided by the block runtime.
 */
__attribute__((weak)) void*
_Block_copy(void*);

#define	ATEXIT_FN_EMPTY	0
#define	ATEXIT_FN_STD	1
#define	ATEXIT_FN_CXA	2

static pthread_mutex_t atexit_mutex = PTHREAD_MUTEX_INITIALIZER;

#define _MUTEX_LOCK(x)		if (__isthreaded) _pthread_mutex_lock(x)
#define _MUTEX_UNLOCK(x)	if (__isthreaded) _pthread_mutex_unlock(x)
#define _MUTEX_DESTROY(x)	if (__isthreaded) _pthread_mutex_destroy(x)

struct atexit {
	struct atexit *next;			/* next in list */
	int ind;				/* next index in this table */
	struct atexit_fn {
		int fn_type;			/* ATEXIT_? from above */
		union {
			void (*std_func)(void);
			void (*cxa_func)(void *);
		} fn_ptr;			/* function pointer */
		void *fn_arg;			/* argument for CXA callback */
		void *fn_dso;			/* shared module handle */
	} fns[ATEXIT_SIZE];			/* the table itself */
};

static struct atexit *__atexit;		/* points to head of LIFO stack */
typedef DECLARE_BLOCK(void, atexit_block, void);

int atexit_b(atexit_block);
int __cxa_atexit(void (*)(void *), void *, void *);

/*
 * Register the function described by 'fptr' to be called at application
 * exit or owning shared object unload time. This is a helper function
 * for atexit and __cxa_atexit.
 */
static int
atexit_register(struct atexit_fn *fptr)
{
	static struct atexit __atexit0;	/* one guaranteed table */
	struct atexit *p;

	_MUTEX_LOCK(&atexit_mutex);
	if ((p = __atexit) == NULL)
		__atexit = p = &__atexit0;
	else while (p->ind >= ATEXIT_SIZE) {
		struct atexit *old__atexit;
		old__atexit = __atexit;
	        _MUTEX_UNLOCK(&atexit_mutex);
		if ((p = (struct atexit *)malloc(sizeof(*p))) == NULL)
			return (-1);
		_MUTEX_LOCK(&atexit_mutex);
		if (old__atexit != __atexit) {
			/* Lost race, retry operation */
			_MUTEX_UNLOCK(&atexit_mutex);
			free(p);
			_MUTEX_LOCK(&atexit_mutex);
			p = __atexit;
			continue;
		}
		p->ind = 0;
		p->next = __atexit;
		__atexit = p;
	}
	p->fns[p->ind++] = *fptr;
	_MUTEX_UNLOCK(&atexit_mutex);
	return 0;
}

/*
 * Register a function to be performed at exit.
 */
int
atexit(void (*func)(void))
{
	struct atexit_fn fn;
	int error;

	fn.fn_type = ATEXIT_FN_STD;
	fn.fn_ptr.std_func = func;
	fn.fn_arg = NULL;
	fn.fn_dso = NULL;

	error = atexit_register(&fn);
	return (error);
}

/**
 * Register a block to be performed at exit.
 */
int
atexit_b(atexit_block func)
{
	struct atexit_fn fn;
	int error;
	if (_Block_copy == 0) {
		errno = ENOSYS;
		return -1;
	}
	func = _Block_copy(func);

	// Blocks are not C++ destructors, but they have the same signature (a
	// single void* parameter), so we can pretend that they are.
	fn.fn_type = ATEXIT_FN_CXA;
	fn.fn_ptr.cxa_func = (void(*)(void*))GET_BLOCK_FUNCTION(func);
	fn.fn_arg = func;
	fn.fn_dso = NULL;

	error = atexit_register(&fn);
	return (error);
}

/*
 * Register a function to be performed at exit or when an shared object
 * with given dso handle is unloaded dynamically.
 */
int
__cxa_atexit(void (*func)(void *), void *arg, void *dso)
{
	struct atexit_fn fn;
	int error;

	fn.fn_type = ATEXIT_FN_CXA;
	fn.fn_ptr.cxa_func = func;
	fn.fn_arg = arg;
	fn.fn_dso = dso;

	error = atexit_register(&fn);
	return (error);
}

#pragma weak __pthread_cxa_finalize
void __pthread_cxa_finalize(const struct dl_phdr_info *);

static int global_exit;

/*
 * Call all handlers registered with __cxa_atexit for the shared
 * object owning 'dso'.  Note: if 'dso' is NULL, then all remaining
 * handlers are called.
 */
void
__cxa_finalize(void *dso)
{
	struct dl_phdr_info phdr_info;
	struct atexit *p;
	struct atexit_fn fn;
	int n, has_phdr;

	if (dso != NULL) {
		has_phdr = _rtld_addr_phdr(dso, &phdr_info);
	} else {
		has_phdr = 0;
		global_exit = 1;
	}

	_MUTEX_LOCK(&atexit_mutex);
	for (p = __atexit; p; p = p->next) {
		for (n = p->ind; --n >= 0;) {
			if (p->fns[n].fn_type == ATEXIT_FN_EMPTY)
				continue; /* already been called */
			fn = p->fns[n];
			if (dso != NULL && dso != fn.fn_dso) {
				/* wrong DSO ? */
				if (!has_phdr || global_exit ||
				    !__elf_phdr_match_addr(&phdr_info,
				    fn.fn_ptr.cxa_func))
					continue;
			}
			/*
			  Mark entry to indicate that this particular handler
			  has already been called.
			*/
			p->fns[n].fn_type = ATEXIT_FN_EMPTY;
		        _MUTEX_UNLOCK(&atexit_mutex);
		
			/* Call the function of correct type. */
			if (fn.fn_type == ATEXIT_FN_CXA)
				fn.fn_ptr.cxa_func(fn.fn_arg);
			else if (fn.fn_type == ATEXIT_FN_STD)
				fn.fn_ptr.std_func();
			_MUTEX_LOCK(&atexit_mutex);
		}
	}
	_MUTEX_UNLOCK(&atexit_mutex);
	if (dso == NULL)
		_MUTEX_DESTROY(&atexit_mutex);

	if (has_phdr && !global_exit && &__pthread_cxa_finalize != NULL)
		__pthread_cxa_finalize(&phdr_info);
}
