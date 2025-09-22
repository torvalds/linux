/* $OpenBSD: rthread_stack.c,v 1.20 2021/09/17 15:20:21 deraadt Exp $ */

/* PUBLIC DOMAIN: No Rights Reserved. Marco S Hyman <marc@snafu.org> */

#include <sys/types.h>
#include <sys/mman.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "rthread.h"

/*
 * Follow uthread's example and keep around stacks that have default
 * attributes for possible reuse.
 */
static SLIST_HEAD(, stack) def_stacks = SLIST_HEAD_INITIALIZER(head);
static _atomic_lock_t def_stacks_lock = _SPINLOCK_UNLOCKED;

struct stack *
_rthread_alloc_stack(pthread_t thread)
{
	struct stack *stack;
	u_int32_t rnd;
	caddr_t base;
	caddr_t guard;
	size_t size;
	size_t guardsize;

	/* if the request uses the defaults, try to reuse one */
	if (thread->attr.stack_addr == NULL &&
	    thread->attr.stack_size == RTHREAD_STACK_SIZE_DEF &&
	    thread->attr.guard_size == _thread_pagesize) {
		_spinlock(&def_stacks_lock);
		stack = SLIST_FIRST(&def_stacks);
		if (stack != NULL) {
			SLIST_REMOVE_HEAD(&def_stacks, link);
			_spinunlock(&def_stacks_lock);
			return (stack);
		}
		_spinunlock(&def_stacks_lock);
	}

	/* allocate the stack struct that we'll return */
	stack = malloc(sizeof(*stack));
	if (stack == NULL)
		return (NULL);

	/* Smaller the stack, smaller the random bias */
	if (thread->attr.stack_size > _thread_pagesize)
		rnd = arc4random() & (_thread_pagesize - 1);
	else if (thread->attr.stack_size == _thread_pagesize)
		rnd = arc4random() & (_thread_pagesize / 16 - 1);
	else
		rnd = 0;
	rnd &= ~_STACKALIGNBYTES;

	/* If a stack address was provided, just fill in the details */
	if (thread->attr.stack_addr != NULL) {
		stack->base = base = thread->attr.stack_addr;
		stack->len  = thread->attr.stack_size;
#ifdef MACHINE_STACK_GROWS_UP
		stack->sp = base + rnd;
#else
		stack->sp = base + thread->attr.stack_size - (_STACKALIGNBYTES+1) - rnd;
#endif
		/*
		 * This impossible guardsize marks this stack as
		 * application allocated so it won't be freed or
		 * cached by _rthread_free_stack()
		 */
		stack->guardsize = 1;
		return (stack);
	}

	/* round up the requested sizes up to full pages */
	size = ROUND_TO_PAGE(thread->attr.stack_size);
	guardsize = ROUND_TO_PAGE(thread->attr.guard_size);

	/* check for overflow */
	if (size < thread->attr.stack_size ||
	    guardsize < thread->attr.guard_size ||
	    SIZE_MAX - size < guardsize) {
		free(stack);
		errno = EINVAL;
		return (NULL);
	}
	size += guardsize;

	/* actually allocate the real stack */
	base = mmap(NULL, size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANON | MAP_STACK, -1, 0);
	if (base == MAP_FAILED) {
		free(stack);
		return (NULL);
	}

#ifdef MACHINE_STACK_GROWS_UP
	guard = base + size - guardsize;
	stack->sp = base + rnd;
#else
	guard = base;
	stack->sp = base + size - (_STACKALIGNBYTES+1) - rnd;
#endif

	/* memory protect the guard region */
	if (guardsize != 0 && mprotect(guard, guardsize, PROT_NONE) == -1) {
		munmap(base, size);
		free(stack);
		return (NULL);
	}

	stack->base = base;
	stack->guardsize = guardsize;
	stack->len = size;
	return (stack);
}

void
_rthread_free_stack(struct stack *stack)
{
	if (stack->len == RTHREAD_STACK_SIZE_DEF + stack->guardsize &&
	    stack->guardsize == _thread_pagesize) {
		_spinlock(&def_stacks_lock);
		SLIST_INSERT_HEAD(&def_stacks, stack, link);
		_spinunlock(&def_stacks_lock);
	} else {
		/* unmap the storage unless it was application allocated */
		if (stack->guardsize != 1)
			munmap(stack->base, stack->len);
		free(stack);
	}
}
