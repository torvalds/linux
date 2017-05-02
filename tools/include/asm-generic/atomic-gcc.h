#ifndef __TOOLS_ASM_GENERIC_ATOMIC_H
#define __TOOLS_ASM_GENERIC_ATOMIC_H

#include <linux/compiler.h>
#include <linux/types.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * Excerpts obtained from the Linux kernel sources.
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
	return ACCESS_ONCE((v)->counter);
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
	__sync_add_and_fetch(&v->counter, 1);
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
	return __sync_sub_and_fetch(&v->counter, 1) == 0;
}

#define cmpxchg(ptr, oldval, newval) \
	__sync_val_compare_and_swap(ptr, oldval, newval)

static inline int atomic_cmpxchg(atomic_t *v, int oldval, int newval)
{
	return cmpxchg(&(v)->counter, oldval, newval);
}

#endif /* __TOOLS_ASM_GENERIC_ATOMIC_H */
