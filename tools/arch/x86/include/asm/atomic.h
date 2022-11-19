/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_ASM_X86_ATOMIC_H
#define _TOOLS_LINUX_ASM_X86_ATOMIC_H

#include <linux/compiler.h>
#include <linux/types.h>
#include "rmwcc.h"

#define LOCK_PREFIX "\n\tlock; "

#include <asm/asm.h>
#include <asm/cmpxchg.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#define ATOMIC_INIT(i)	{ (i) }

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
static inline int atomic_read(const atomic_t *v)
{
	return READ_ONCE((v)->counter);
}

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
static inline void atomic_set(atomic_t *v, int i)
{
	v->counter = i;
}

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
static inline void atomic_inc(atomic_t *v)
{
	asm volatile(LOCK_PREFIX "incl %0"
		     : "+m" (v->counter));
}

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static inline int atomic_dec_and_test(atomic_t *v)
{
	GEN_UNARY_RMWcc(LOCK_PREFIX "decl", v->counter, "%0", "e");
}

static __always_inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	return cmpxchg(&v->counter, old, new);
}

static inline int test_and_set_bit(long nr, unsigned long *addr)
{
	GEN_BINARY_RMWcc(LOCK_PREFIX __ASM_SIZE(bts), *addr, "Ir", nr, "%0", "c");
}

static inline int test_and_clear_bit(long nr, unsigned long *addr)
{
	GEN_BINARY_RMWcc(LOCK_PREFIX __ASM_SIZE(btc), *addr, "Ir", nr, "%0", "c");
}

#endif /* _TOOLS_LINUX_ASM_X86_ATOMIC_H */
