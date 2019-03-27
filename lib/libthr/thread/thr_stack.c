/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Daniel Eischen <deischen@freebsd.org>
 * Copyright (c) 2000-2001 Jason Evans <jasone@freebsd.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <stdlib.h>
#include <pthread.h>
#include <link.h>

#include "thr_private.h"

/* Spare thread stack. */
struct stack {
	LIST_ENTRY(stack)	qe;		/* Stack queue linkage. */
	size_t			stacksize;	/* Stack size (rounded up). */
	size_t			guardsize;	/* Guard size. */
	void			*stackaddr;	/* Stack address. */
};

/*
 * Default sized (stack and guard) spare stack queue.  Stacks are cached
 * to avoid additional complexity managing mmap()ed stack regions.  Spare
 * stacks are used in LIFO order to increase cache locality.
 */
static LIST_HEAD(, stack)	dstackq = LIST_HEAD_INITIALIZER(dstackq);

/*
 * Miscellaneous sized (non-default stack and/or guard) spare stack queue.
 * Stacks are cached to avoid additional complexity managing mmap()ed
 * stack regions.  This list is unordered, since ordering on both stack
 * size and guard size would be more trouble than it's worth.  Stacks are
 * allocated from this cache on a first size match basis.
 */
static LIST_HEAD(, stack)	mstackq = LIST_HEAD_INITIALIZER(mstackq);

/**
 * Base address of the last stack allocated (including its red zone, if
 * there is one).  Stacks are allocated contiguously, starting beyond the
 * top of the main stack.  When a new stack is created, a red zone is
 * typically created (actually, the red zone is mapped with PROT_NONE) above
 * the top of the stack, such that the stack will not be able to grow all
 * the way to the bottom of the next stack.  This isn't fool-proof.  It is
 * possible for a stack to grow by a large amount, such that it grows into
 * the next stack, and as long as the memory within the red zone is never
 * accessed, nothing will prevent one thread stack from trouncing all over
 * the next.
 *
 * low memory
 *     . . . . . . . . . . . . . . . . . . 
 *    |                                   |
 *    |             stack 3               | start of 3rd thread stack
 *    +-----------------------------------+
 *    |                                   |
 *    |       Red Zone (guard page)       | red zone for 2nd thread
 *    |                                   |
 *    +-----------------------------------+
 *    |  stack 2 - _thr_stack_default     | top of 2nd thread stack
 *    |                                   |
 *    |                                   |
 *    |                                   |
 *    |                                   |
 *    |             stack 2               |
 *    +-----------------------------------+ <-- start of 2nd thread stack
 *    |                                   |
 *    |       Red Zone                    | red zone for 1st thread
 *    |                                   |
 *    +-----------------------------------+
 *    |  stack 1 - _thr_stack_default     | top of 1st thread stack
 *    |                                   |
 *    |                                   |
 *    |                                   |
 *    |                                   |
 *    |             stack 1               |
 *    +-----------------------------------+ <-- start of 1st thread stack
 *    |                                   |   (initial value of last_stack)
 *    |       Red Zone                    |
 *    |                                   | red zone for main thread
 *    +-----------------------------------+
 *    | USRSTACK - _thr_stack_initial     | top of main thread stack
 *    |                                   | ^
 *    |                                   | |
 *    |                                   | |
 *    |                                   | | stack growth
 *    |                                   |
 *    +-----------------------------------+ <-- start of main thread stack
 *                                              (USRSTACK)
 * high memory
 *
 */
static char *last_stack = NULL;

/*
 * Round size up to the nearest multiple of
 * _thr_page_size.
 */
static inline size_t
round_up(size_t size)
{
	if (size % _thr_page_size != 0)
		size = ((size / _thr_page_size) + 1) *
		    _thr_page_size;
	return size;
}

void
_thr_stack_fix_protection(struct pthread *thrd)
{

	mprotect((char *)thrd->attr.stackaddr_attr +
	    round_up(thrd->attr.guardsize_attr),
	    round_up(thrd->attr.stacksize_attr),
	    _rtld_get_stack_prot());
}

static void
singlethread_map_stacks_exec(void)
{
	int mib[2];
	struct rlimit rlim;
	u_long usrstack;
	size_t len;

	mib[0] = CTL_KERN;
	mib[1] = KERN_USRSTACK;
	len = sizeof(usrstack);
	if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), &usrstack, &len, NULL, 0)
	    == -1)
		return;
	if (getrlimit(RLIMIT_STACK, &rlim) == -1)
		return;
	mprotect((void *)(uintptr_t)(usrstack - rlim.rlim_cur),
	    rlim.rlim_cur, _rtld_get_stack_prot());
}

void
__thr_map_stacks_exec(void)
{
	struct pthread *curthread, *thrd;
	struct stack *st;

	if (!_thr_is_inited()) {
		singlethread_map_stacks_exec();
		return;
	}
	curthread = _get_curthread();
	THREAD_LIST_RDLOCK(curthread);
	LIST_FOREACH(st, &mstackq, qe)
		mprotect((char *)st->stackaddr + st->guardsize, st->stacksize,
		    _rtld_get_stack_prot());
	LIST_FOREACH(st, &dstackq, qe)
		mprotect((char *)st->stackaddr + st->guardsize, st->stacksize,
		    _rtld_get_stack_prot());
	TAILQ_FOREACH(thrd, &_thread_gc_list, gcle)
		_thr_stack_fix_protection(thrd);
	TAILQ_FOREACH(thrd, &_thread_list, tle)
		_thr_stack_fix_protection(thrd);
	THREAD_LIST_UNLOCK(curthread);
}

int
_thr_stack_alloc(struct pthread_attr *attr)
{
	struct pthread *curthread = _get_curthread();
	struct stack *spare_stack;
	size_t stacksize;
	size_t guardsize;
	char *stackaddr;

	/*
	 * Round up stack size to nearest multiple of _thr_page_size so
	 * that mmap() * will work.  If the stack size is not an even
	 * multiple, we end up initializing things such that there is
	 * unused space above the beginning of the stack, so the stack
	 * sits snugly against its guard.
	 */
	stacksize = round_up(attr->stacksize_attr);
	guardsize = round_up(attr->guardsize_attr);

	attr->stackaddr_attr = NULL;
	attr->flags &= ~THR_STACK_USER;

	/*
	 * Use the garbage collector lock for synchronization of the
	 * spare stack lists and allocations from usrstack.
	 */
	THREAD_LIST_WRLOCK(curthread);
	/*
	 * If the stack and guard sizes are default, try to allocate a stack
	 * from the default-size stack cache:
	 */
	if ((stacksize == THR_STACK_DEFAULT) &&
	    (guardsize == _thr_guard_default)) {
		if ((spare_stack = LIST_FIRST(&dstackq)) != NULL) {
			/* Use the spare stack. */
			LIST_REMOVE(spare_stack, qe);
			attr->stackaddr_attr = spare_stack->stackaddr;
		}
	}
	/*
	 * The user specified a non-default stack and/or guard size, so try to
	 * allocate a stack from the non-default size stack cache, using the
	 * rounded up stack size (stack_size) in the search:
	 */
	else {
		LIST_FOREACH(spare_stack, &mstackq, qe) {
			if (spare_stack->stacksize == stacksize &&
			    spare_stack->guardsize == guardsize) {
				LIST_REMOVE(spare_stack, qe);
				attr->stackaddr_attr = spare_stack->stackaddr;
				break;
			}
		}
	}
	if (attr->stackaddr_attr != NULL) {
		/* A cached stack was found.  Release the lock. */
		THREAD_LIST_UNLOCK(curthread);
	}
	else {
		/*
		 * Allocate a stack from or below usrstack, depending
		 * on the LIBPTHREAD_BIGSTACK_MAIN env variable.
		 */
		if (last_stack == NULL)
			last_stack = _usrstack - _thr_stack_initial -
			    _thr_guard_default;

		/* Allocate a new stack. */
		stackaddr = last_stack - stacksize - guardsize;

		/*
		 * Even if stack allocation fails, we don't want to try to
		 * use this location again, so unconditionally decrement
		 * last_stack.  Under normal operating conditions, the most
		 * likely reason for an mmap() error is a stack overflow of
		 * the adjacent thread stack.
		 */
		last_stack -= (stacksize + guardsize);

		/* Release the lock before mmap'ing it. */
		THREAD_LIST_UNLOCK(curthread);

		/* Map the stack and guard page together, and split guard
		   page from allocated space: */
		if ((stackaddr = mmap(stackaddr, stacksize + guardsize,
		     _rtld_get_stack_prot(), MAP_STACK,
		     -1, 0)) != MAP_FAILED &&
		    (guardsize == 0 ||
		     mprotect(stackaddr, guardsize, PROT_NONE) == 0)) {
			stackaddr += guardsize;
		} else {
			if (stackaddr != MAP_FAILED)
				munmap(stackaddr, stacksize + guardsize);
			stackaddr = NULL;
		}
		attr->stackaddr_attr = stackaddr;
	}
	if (attr->stackaddr_attr != NULL)
		return (0);
	else
		return (-1);
}

/* This function must be called with _thread_list_lock held. */
void
_thr_stack_free(struct pthread_attr *attr)
{
	struct stack *spare_stack;

	if ((attr != NULL) && ((attr->flags & THR_STACK_USER) == 0)
	    && (attr->stackaddr_attr != NULL)) {
		spare_stack = (struct stack *)
			((char *)attr->stackaddr_attr +
			attr->stacksize_attr - sizeof(struct stack));
		spare_stack->stacksize = round_up(attr->stacksize_attr);
		spare_stack->guardsize = round_up(attr->guardsize_attr);
		spare_stack->stackaddr = attr->stackaddr_attr;

		if (spare_stack->stacksize == THR_STACK_DEFAULT &&
		    spare_stack->guardsize == _thr_guard_default) {
			/* Default stack/guard size. */
			LIST_INSERT_HEAD(&dstackq, spare_stack, qe);
		} else {
			/* Non-default stack/guard size. */
			LIST_INSERT_HEAD(&mstackq, spare_stack, qe);
		}
		attr->stackaddr_attr = NULL;
	}
}
