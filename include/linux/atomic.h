/* SPDX-License-Identifier: GPL-2.0 */
/* Atomic operations usable in machine independent code */
#ifndef _LINUX_ATOMIC_H
#define _LINUX_ATOMIC_H
#include <linux/types.h>

#include <asm/atomic.h>
#include <asm/barrier.h>

/*
 * Relaxed variants of xchg, cmpxchg and some atomic operations.
 *
 * We support four variants:
 *
 * - Fully ordered: The default implementation, no suffix required.
 * - Acquire: Provides ACQUIRE semantics, _acquire suffix.
 * - Release: Provides RELEASE semantics, _release suffix.
 * - Relaxed: No ordering guarantees, _relaxed suffix.
 *
 * For compound atomics performing both a load and a store, ACQUIRE
 * semantics apply only to the load and RELEASE semantics only to the
 * store portion of the operation. Note that a failed cmpxchg_acquire
 * does -not- imply any memory ordering constraints.
 *
 * See Documentation/memory-barriers.txt for ACQUIRE/RELEASE definitions.
 */

#ifndef atomic_read_acquire
#define  atomic_read_acquire(v)		smp_load_acquire(&(v)->counter)
#endif

#ifndef atomic_set_release
#define  atomic_set_release(v, i)	smp_store_release(&(v)->counter, (i))
#endif

/*
 * The idea here is to build acquire/release variants by adding explicit
 * barriers on top of the relaxed variant. In the case where the relaxed
 * variant is already fully ordered, no additional barriers are needed.
 *
 * Besides, if an arch has a special barrier for acquire/release, it could
 * implement its own __atomic_op_* and use the same framework for building
 * variants
 *
 * If an architecture overrides __atomic_op_acquire() it will probably want
 * to define smp_mb__after_spinlock().
 */
#ifndef __atomic_op_acquire
#define __atomic_op_acquire(op, args...)				\
({									\
	typeof(op##_relaxed(args)) __ret  = op##_relaxed(args);		\
	smp_mb__after_atomic();						\
	__ret;								\
})
#endif

#ifndef __atomic_op_release
#define __atomic_op_release(op, args...)				\
({									\
	smp_mb__before_atomic();					\
	op##_relaxed(args);						\
})
#endif

#ifndef __atomic_op_fence
#define __atomic_op_fence(op, args...)					\
({									\
	typeof(op##_relaxed(args)) __ret;				\
	smp_mb__before_atomic();					\
	__ret = op##_relaxed(args);					\
	smp_mb__after_atomic();						\
	__ret;								\
})
#endif

/* atomic_add_return_relaxed */
#ifndef atomic_add_return_relaxed
#define  atomic_add_return_relaxed	atomic_add_return
#define  atomic_add_return_acquire	atomic_add_return
#define  atomic_add_return_release	atomic_add_return

#else /* atomic_add_return_relaxed */

#ifndef atomic_add_return_acquire
#define  atomic_add_return_acquire(...)					\
	__atomic_op_acquire(atomic_add_return, __VA_ARGS__)
#endif

#ifndef atomic_add_return_release
#define  atomic_add_return_release(...)					\
	__atomic_op_release(atomic_add_return, __VA_ARGS__)
#endif

#ifndef atomic_add_return
#define  atomic_add_return(...)						\
	__atomic_op_fence(atomic_add_return, __VA_ARGS__)
#endif
#endif /* atomic_add_return_relaxed */

/* atomic_inc_return_relaxed */
#ifndef atomic_inc_return_relaxed
#define  atomic_inc_return_relaxed	atomic_inc_return
#define  atomic_inc_return_acquire	atomic_inc_return
#define  atomic_inc_return_release	atomic_inc_return

#else /* atomic_inc_return_relaxed */

#ifndef atomic_inc_return_acquire
#define  atomic_inc_return_acquire(...)					\
	__atomic_op_acquire(atomic_inc_return, __VA_ARGS__)
#endif

#ifndef atomic_inc_return_release
#define  atomic_inc_return_release(...)					\
	__atomic_op_release(atomic_inc_return, __VA_ARGS__)
#endif

#ifndef atomic_inc_return
#define  atomic_inc_return(...)						\
	__atomic_op_fence(atomic_inc_return, __VA_ARGS__)
#endif
#endif /* atomic_inc_return_relaxed */

/* atomic_sub_return_relaxed */
#ifndef atomic_sub_return_relaxed
#define  atomic_sub_return_relaxed	atomic_sub_return
#define  atomic_sub_return_acquire	atomic_sub_return
#define  atomic_sub_return_release	atomic_sub_return

#else /* atomic_sub_return_relaxed */

#ifndef atomic_sub_return_acquire
#define  atomic_sub_return_acquire(...)					\
	__atomic_op_acquire(atomic_sub_return, __VA_ARGS__)
#endif

#ifndef atomic_sub_return_release
#define  atomic_sub_return_release(...)					\
	__atomic_op_release(atomic_sub_return, __VA_ARGS__)
#endif

#ifndef atomic_sub_return
#define  atomic_sub_return(...)						\
	__atomic_op_fence(atomic_sub_return, __VA_ARGS__)
#endif
#endif /* atomic_sub_return_relaxed */

/* atomic_dec_return_relaxed */
#ifndef atomic_dec_return_relaxed
#define  atomic_dec_return_relaxed	atomic_dec_return
#define  atomic_dec_return_acquire	atomic_dec_return
#define  atomic_dec_return_release	atomic_dec_return

#else /* atomic_dec_return_relaxed */

#ifndef atomic_dec_return_acquire
#define  atomic_dec_return_acquire(...)					\
	__atomic_op_acquire(atomic_dec_return, __VA_ARGS__)
#endif

#ifndef atomic_dec_return_release
#define  atomic_dec_return_release(...)					\
	__atomic_op_release(atomic_dec_return, __VA_ARGS__)
#endif

#ifndef atomic_dec_return
#define  atomic_dec_return(...)						\
	__atomic_op_fence(atomic_dec_return, __VA_ARGS__)
#endif
#endif /* atomic_dec_return_relaxed */


/* atomic_fetch_add_relaxed */
#ifndef atomic_fetch_add_relaxed
#define atomic_fetch_add_relaxed	atomic_fetch_add
#define atomic_fetch_add_acquire	atomic_fetch_add
#define atomic_fetch_add_release	atomic_fetch_add

#else /* atomic_fetch_add_relaxed */

#ifndef atomic_fetch_add_acquire
#define atomic_fetch_add_acquire(...)					\
	__atomic_op_acquire(atomic_fetch_add, __VA_ARGS__)
#endif

#ifndef atomic_fetch_add_release
#define atomic_fetch_add_release(...)					\
	__atomic_op_release(atomic_fetch_add, __VA_ARGS__)
#endif

#ifndef atomic_fetch_add
#define atomic_fetch_add(...)						\
	__atomic_op_fence(atomic_fetch_add, __VA_ARGS__)
#endif
#endif /* atomic_fetch_add_relaxed */

/* atomic_fetch_inc_relaxed */
#ifndef atomic_fetch_inc_relaxed

#ifndef atomic_fetch_inc
#define atomic_fetch_inc(v)	        atomic_fetch_add(1, (v))
#define atomic_fetch_inc_relaxed(v)	atomic_fetch_add_relaxed(1, (v))
#define atomic_fetch_inc_acquire(v)	atomic_fetch_add_acquire(1, (v))
#define atomic_fetch_inc_release(v)	atomic_fetch_add_release(1, (v))
#else /* atomic_fetch_inc */
#define atomic_fetch_inc_relaxed	atomic_fetch_inc
#define atomic_fetch_inc_acquire	atomic_fetch_inc
#define atomic_fetch_inc_release	atomic_fetch_inc
#endif /* atomic_fetch_inc */

#else /* atomic_fetch_inc_relaxed */

#ifndef atomic_fetch_inc_acquire
#define atomic_fetch_inc_acquire(...)					\
	__atomic_op_acquire(atomic_fetch_inc, __VA_ARGS__)
#endif

#ifndef atomic_fetch_inc_release
#define atomic_fetch_inc_release(...)					\
	__atomic_op_release(atomic_fetch_inc, __VA_ARGS__)
#endif

#ifndef atomic_fetch_inc
#define atomic_fetch_inc(...)						\
	__atomic_op_fence(atomic_fetch_inc, __VA_ARGS__)
#endif
#endif /* atomic_fetch_inc_relaxed */

/* atomic_fetch_sub_relaxed */
#ifndef atomic_fetch_sub_relaxed
#define atomic_fetch_sub_relaxed	atomic_fetch_sub
#define atomic_fetch_sub_acquire	atomic_fetch_sub
#define atomic_fetch_sub_release	atomic_fetch_sub

#else /* atomic_fetch_sub_relaxed */

#ifndef atomic_fetch_sub_acquire
#define atomic_fetch_sub_acquire(...)					\
	__atomic_op_acquire(atomic_fetch_sub, __VA_ARGS__)
#endif

#ifndef atomic_fetch_sub_release
#define atomic_fetch_sub_release(...)					\
	__atomic_op_release(atomic_fetch_sub, __VA_ARGS__)
#endif

#ifndef atomic_fetch_sub
#define atomic_fetch_sub(...)						\
	__atomic_op_fence(atomic_fetch_sub, __VA_ARGS__)
#endif
#endif /* atomic_fetch_sub_relaxed */

/* atomic_fetch_dec_relaxed */
#ifndef atomic_fetch_dec_relaxed

#ifndef atomic_fetch_dec
#define atomic_fetch_dec(v)	        atomic_fetch_sub(1, (v))
#define atomic_fetch_dec_relaxed(v)	atomic_fetch_sub_relaxed(1, (v))
#define atomic_fetch_dec_acquire(v)	atomic_fetch_sub_acquire(1, (v))
#define atomic_fetch_dec_release(v)	atomic_fetch_sub_release(1, (v))
#else /* atomic_fetch_dec */
#define atomic_fetch_dec_relaxed	atomic_fetch_dec
#define atomic_fetch_dec_acquire	atomic_fetch_dec
#define atomic_fetch_dec_release	atomic_fetch_dec
#endif /* atomic_fetch_dec */

#else /* atomic_fetch_dec_relaxed */

#ifndef atomic_fetch_dec_acquire
#define atomic_fetch_dec_acquire(...)					\
	__atomic_op_acquire(atomic_fetch_dec, __VA_ARGS__)
#endif

#ifndef atomic_fetch_dec_release
#define atomic_fetch_dec_release(...)					\
	__atomic_op_release(atomic_fetch_dec, __VA_ARGS__)
#endif

#ifndef atomic_fetch_dec
#define atomic_fetch_dec(...)						\
	__atomic_op_fence(atomic_fetch_dec, __VA_ARGS__)
#endif
#endif /* atomic_fetch_dec_relaxed */

/* atomic_fetch_or_relaxed */
#ifndef atomic_fetch_or_relaxed
#define atomic_fetch_or_relaxed	atomic_fetch_or
#define atomic_fetch_or_acquire	atomic_fetch_or
#define atomic_fetch_or_release	atomic_fetch_or

#else /* atomic_fetch_or_relaxed */

#ifndef atomic_fetch_or_acquire
#define atomic_fetch_or_acquire(...)					\
	__atomic_op_acquire(atomic_fetch_or, __VA_ARGS__)
#endif

#ifndef atomic_fetch_or_release
#define atomic_fetch_or_release(...)					\
	__atomic_op_release(atomic_fetch_or, __VA_ARGS__)
#endif

#ifndef atomic_fetch_or
#define atomic_fetch_or(...)						\
	__atomic_op_fence(atomic_fetch_or, __VA_ARGS__)
#endif
#endif /* atomic_fetch_or_relaxed */

/* atomic_fetch_and_relaxed */
#ifndef atomic_fetch_and_relaxed
#define atomic_fetch_and_relaxed	atomic_fetch_and
#define atomic_fetch_and_acquire	atomic_fetch_and
#define atomic_fetch_and_release	atomic_fetch_and

#else /* atomic_fetch_and_relaxed */

#ifndef atomic_fetch_and_acquire
#define atomic_fetch_and_acquire(...)					\
	__atomic_op_acquire(atomic_fetch_and, __VA_ARGS__)
#endif

#ifndef atomic_fetch_and_release
#define atomic_fetch_and_release(...)					\
	__atomic_op_release(atomic_fetch_and, __VA_ARGS__)
#endif

#ifndef atomic_fetch_and
#define atomic_fetch_and(...)						\
	__atomic_op_fence(atomic_fetch_and, __VA_ARGS__)
#endif
#endif /* atomic_fetch_and_relaxed */

#ifdef atomic_andnot
/* atomic_fetch_andnot_relaxed */
#ifndef atomic_fetch_andnot_relaxed
#define atomic_fetch_andnot_relaxed	atomic_fetch_andnot
#define atomic_fetch_andnot_acquire	atomic_fetch_andnot
#define atomic_fetch_andnot_release	atomic_fetch_andnot

#else /* atomic_fetch_andnot_relaxed */

#ifndef atomic_fetch_andnot_acquire
#define atomic_fetch_andnot_acquire(...)					\
	__atomic_op_acquire(atomic_fetch_andnot, __VA_ARGS__)
#endif

#ifndef atomic_fetch_andnot_release
#define atomic_fetch_andnot_release(...)					\
	__atomic_op_release(atomic_fetch_andnot, __VA_ARGS__)
#endif

#ifndef atomic_fetch_andnot
#define atomic_fetch_andnot(...)						\
	__atomic_op_fence(atomic_fetch_andnot, __VA_ARGS__)
#endif
#endif /* atomic_fetch_andnot_relaxed */
#endif /* atomic_andnot */

/* atomic_fetch_xor_relaxed */
#ifndef atomic_fetch_xor_relaxed
#define atomic_fetch_xor_relaxed	atomic_fetch_xor
#define atomic_fetch_xor_acquire	atomic_fetch_xor
#define atomic_fetch_xor_release	atomic_fetch_xor

#else /* atomic_fetch_xor_relaxed */

#ifndef atomic_fetch_xor_acquire
#define atomic_fetch_xor_acquire(...)					\
	__atomic_op_acquire(atomic_fetch_xor, __VA_ARGS__)
#endif

#ifndef atomic_fetch_xor_release
#define atomic_fetch_xor_release(...)					\
	__atomic_op_release(atomic_fetch_xor, __VA_ARGS__)
#endif

#ifndef atomic_fetch_xor
#define atomic_fetch_xor(...)						\
	__atomic_op_fence(atomic_fetch_xor, __VA_ARGS__)
#endif
#endif /* atomic_fetch_xor_relaxed */


/* atomic_xchg_relaxed */
#ifndef atomic_xchg_relaxed
#define  atomic_xchg_relaxed		atomic_xchg
#define  atomic_xchg_acquire		atomic_xchg
#define  atomic_xchg_release		atomic_xchg

#else /* atomic_xchg_relaxed */

#ifndef atomic_xchg_acquire
#define  atomic_xchg_acquire(...)					\
	__atomic_op_acquire(atomic_xchg, __VA_ARGS__)
#endif

#ifndef atomic_xchg_release
#define  atomic_xchg_release(...)					\
	__atomic_op_release(atomic_xchg, __VA_ARGS__)
#endif

#ifndef atomic_xchg
#define  atomic_xchg(...)						\
	__atomic_op_fence(atomic_xchg, __VA_ARGS__)
#endif
#endif /* atomic_xchg_relaxed */

/* atomic_cmpxchg_relaxed */
#ifndef atomic_cmpxchg_relaxed
#define  atomic_cmpxchg_relaxed		atomic_cmpxchg
#define  atomic_cmpxchg_acquire		atomic_cmpxchg
#define  atomic_cmpxchg_release		atomic_cmpxchg

#else /* atomic_cmpxchg_relaxed */

#ifndef atomic_cmpxchg_acquire
#define  atomic_cmpxchg_acquire(...)					\
	__atomic_op_acquire(atomic_cmpxchg, __VA_ARGS__)
#endif

#ifndef atomic_cmpxchg_release
#define  atomic_cmpxchg_release(...)					\
	__atomic_op_release(atomic_cmpxchg, __VA_ARGS__)
#endif

#ifndef atomic_cmpxchg
#define  atomic_cmpxchg(...)						\
	__atomic_op_fence(atomic_cmpxchg, __VA_ARGS__)
#endif
#endif /* atomic_cmpxchg_relaxed */

#ifndef atomic_try_cmpxchg

#define __atomic_try_cmpxchg(type, _p, _po, _n)				\
({									\
	typeof(_po) __po = (_po);					\
	typeof(*(_po)) __r, __o = *__po;				\
	__r = atomic_cmpxchg##type((_p), __o, (_n));			\
	if (unlikely(__r != __o))					\
		*__po = __r;						\
	likely(__r == __o);						\
})

#define atomic_try_cmpxchg(_p, _po, _n)		__atomic_try_cmpxchg(, _p, _po, _n)
#define atomic_try_cmpxchg_relaxed(_p, _po, _n)	__atomic_try_cmpxchg(_relaxed, _p, _po, _n)
#define atomic_try_cmpxchg_acquire(_p, _po, _n)	__atomic_try_cmpxchg(_acquire, _p, _po, _n)
#define atomic_try_cmpxchg_release(_p, _po, _n)	__atomic_try_cmpxchg(_release, _p, _po, _n)

#else /* atomic_try_cmpxchg */
#define atomic_try_cmpxchg_relaxed	atomic_try_cmpxchg
#define atomic_try_cmpxchg_acquire	atomic_try_cmpxchg
#define atomic_try_cmpxchg_release	atomic_try_cmpxchg
#endif /* atomic_try_cmpxchg */

/* cmpxchg_relaxed */
#ifndef cmpxchg_relaxed
#define  cmpxchg_relaxed		cmpxchg
#define  cmpxchg_acquire		cmpxchg
#define  cmpxchg_release		cmpxchg

#else /* cmpxchg_relaxed */

#ifndef cmpxchg_acquire
#define  cmpxchg_acquire(...)						\
	__atomic_op_acquire(cmpxchg, __VA_ARGS__)
#endif

#ifndef cmpxchg_release
#define  cmpxchg_release(...)						\
	__atomic_op_release(cmpxchg, __VA_ARGS__)
#endif

#ifndef cmpxchg
#define  cmpxchg(...)							\
	__atomic_op_fence(cmpxchg, __VA_ARGS__)
#endif
#endif /* cmpxchg_relaxed */

/* cmpxchg64_relaxed */
#ifndef cmpxchg64_relaxed
#define  cmpxchg64_relaxed		cmpxchg64
#define  cmpxchg64_acquire		cmpxchg64
#define  cmpxchg64_release		cmpxchg64

#else /* cmpxchg64_relaxed */

#ifndef cmpxchg64_acquire
#define  cmpxchg64_acquire(...)						\
	__atomic_op_acquire(cmpxchg64, __VA_ARGS__)
#endif

#ifndef cmpxchg64_release
#define  cmpxchg64_release(...)						\
	__atomic_op_release(cmpxchg64, __VA_ARGS__)
#endif

#ifndef cmpxchg64
#define  cmpxchg64(...)							\
	__atomic_op_fence(cmpxchg64, __VA_ARGS__)
#endif
#endif /* cmpxchg64_relaxed */

/* xchg_relaxed */
#ifndef xchg_relaxed
#define  xchg_relaxed			xchg
#define  xchg_acquire			xchg
#define  xchg_release			xchg

#else /* xchg_relaxed */

#ifndef xchg_acquire
#define  xchg_acquire(...)		__atomic_op_acquire(xchg, __VA_ARGS__)
#endif

#ifndef xchg_release
#define  xchg_release(...)		__atomic_op_release(xchg, __VA_ARGS__)
#endif

#ifndef xchg
#define  xchg(...)			__atomic_op_fence(xchg, __VA_ARGS__)
#endif
#endif /* xchg_relaxed */

/**
 * atomic_fetch_add_unless - add unless the number is already a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, if @v was not already @u.
 * Returns the original value of @v.
 */
#ifndef atomic_fetch_add_unless
static inline int atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int c = atomic_read(v);

	do {
		if (unlikely(c == u))
			break;
	} while (!atomic_try_cmpxchg(v, &c, c + a));

	return c;
}
#endif

/**
 * atomic_add_unless - add unless the number is already a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, if @v was not already @u.
 * Returns true if the addition was done.
 */
static inline bool atomic_add_unless(atomic_t *v, int a, int u)
{
	return atomic_fetch_add_unless(v, a, u) != u;
}

/**
 * atomic_inc_not_zero - increment unless the number is zero
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1, if @v is non-zero.
 * Returns true if the increment was done.
 */
#ifndef atomic_inc_not_zero
#define atomic_inc_not_zero(v)		atomic_add_unless((v), 1, 0)
#endif

/**
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#ifndef atomic_inc_and_test
static inline bool atomic_inc_and_test(atomic_t *v)
{
	return atomic_inc_return(v) == 0;
}
#endif

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
#ifndef atomic_dec_and_test
static inline bool atomic_dec_and_test(atomic_t *v)
{
	return atomic_dec_return(v) == 0;
}
#endif

/**
 * atomic_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
#ifndef atomic_sub_and_test
static inline bool atomic_sub_and_test(int i, atomic_t *v)
{
	return atomic_sub_return(i, v) == 0;
}
#endif

/**
 * atomic_add_negative - add and test if negative
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
#ifndef atomic_add_negative
static inline bool atomic_add_negative(int i, atomic_t *v)
{
	return atomic_add_return(i, v) < 0;
}
#endif

#ifndef atomic_andnot
static inline void atomic_andnot(int i, atomic_t *v)
{
	atomic_and(~i, v);
}

static inline int atomic_fetch_andnot(int i, atomic_t *v)
{
	return atomic_fetch_and(~i, v);
}

static inline int atomic_fetch_andnot_relaxed(int i, atomic_t *v)
{
	return atomic_fetch_and_relaxed(~i, v);
}

static inline int atomic_fetch_andnot_acquire(int i, atomic_t *v)
{
	return atomic_fetch_and_acquire(~i, v);
}

static inline int atomic_fetch_andnot_release(int i, atomic_t *v)
{
	return atomic_fetch_and_release(~i, v);
}
#endif

#ifndef atomic_inc_unless_negative
static inline bool atomic_inc_unless_negative(atomic_t *p)
{
	int v, v1;
	for (v = 0; v >= 0; v = v1) {
		v1 = atomic_cmpxchg(p, v, v + 1);
		if (likely(v1 == v))
			return true;
	}
	return false;
}
#endif

#ifndef atomic_dec_unless_positive
static inline bool atomic_dec_unless_positive(atomic_t *p)
{
	int v, v1;
	for (v = 0; v <= 0; v = v1) {
		v1 = atomic_cmpxchg(p, v, v - 1);
		if (likely(v1 == v))
			return true;
	}
	return false;
}
#endif

/*
 * atomic_dec_if_positive - decrement by 1 if old value positive
 * @v: pointer of type atomic_t
 *
 * The function returns the old value of *v minus 1, even if
 * the atomic variable, v, was not decremented.
 */
#ifndef atomic_dec_if_positive
static inline int atomic_dec_if_positive(atomic_t *v)
{
	int c, old, dec;
	c = atomic_read(v);
	for (;;) {
		dec = c - 1;
		if (unlikely(dec < 0))
			break;
		old = atomic_cmpxchg((v), c, dec);
		if (likely(old == c))
			break;
		c = old;
	}
	return dec;
}
#endif

#define atomic_cond_read_relaxed(v, c)	smp_cond_load_relaxed(&(v)->counter, (c))
#define atomic_cond_read_acquire(v, c)	smp_cond_load_acquire(&(v)->counter, (c))

#ifdef CONFIG_GENERIC_ATOMIC64
#include <asm-generic/atomic64.h>
#endif

#ifndef atomic64_read_acquire
#define  atomic64_read_acquire(v)	smp_load_acquire(&(v)->counter)
#endif

#ifndef atomic64_set_release
#define  atomic64_set_release(v, i)	smp_store_release(&(v)->counter, (i))
#endif

/* atomic64_add_return_relaxed */
#ifndef atomic64_add_return_relaxed
#define  atomic64_add_return_relaxed	atomic64_add_return
#define  atomic64_add_return_acquire	atomic64_add_return
#define  atomic64_add_return_release	atomic64_add_return

#else /* atomic64_add_return_relaxed */

#ifndef atomic64_add_return_acquire
#define  atomic64_add_return_acquire(...)				\
	__atomic_op_acquire(atomic64_add_return, __VA_ARGS__)
#endif

#ifndef atomic64_add_return_release
#define  atomic64_add_return_release(...)				\
	__atomic_op_release(atomic64_add_return, __VA_ARGS__)
#endif

#ifndef atomic64_add_return
#define  atomic64_add_return(...)					\
	__atomic_op_fence(atomic64_add_return, __VA_ARGS__)
#endif
#endif /* atomic64_add_return_relaxed */

/* atomic64_inc_return_relaxed */
#ifndef atomic64_inc_return_relaxed
#define  atomic64_inc_return_relaxed	atomic64_inc_return
#define  atomic64_inc_return_acquire	atomic64_inc_return
#define  atomic64_inc_return_release	atomic64_inc_return

#else /* atomic64_inc_return_relaxed */

#ifndef atomic64_inc_return_acquire
#define  atomic64_inc_return_acquire(...)				\
	__atomic_op_acquire(atomic64_inc_return, __VA_ARGS__)
#endif

#ifndef atomic64_inc_return_release
#define  atomic64_inc_return_release(...)				\
	__atomic_op_release(atomic64_inc_return, __VA_ARGS__)
#endif

#ifndef atomic64_inc_return
#define  atomic64_inc_return(...)					\
	__atomic_op_fence(atomic64_inc_return, __VA_ARGS__)
#endif
#endif /* atomic64_inc_return_relaxed */


/* atomic64_sub_return_relaxed */
#ifndef atomic64_sub_return_relaxed
#define  atomic64_sub_return_relaxed	atomic64_sub_return
#define  atomic64_sub_return_acquire	atomic64_sub_return
#define  atomic64_sub_return_release	atomic64_sub_return

#else /* atomic64_sub_return_relaxed */

#ifndef atomic64_sub_return_acquire
#define  atomic64_sub_return_acquire(...)				\
	__atomic_op_acquire(atomic64_sub_return, __VA_ARGS__)
#endif

#ifndef atomic64_sub_return_release
#define  atomic64_sub_return_release(...)				\
	__atomic_op_release(atomic64_sub_return, __VA_ARGS__)
#endif

#ifndef atomic64_sub_return
#define  atomic64_sub_return(...)					\
	__atomic_op_fence(atomic64_sub_return, __VA_ARGS__)
#endif
#endif /* atomic64_sub_return_relaxed */

/* atomic64_dec_return_relaxed */
#ifndef atomic64_dec_return_relaxed
#define  atomic64_dec_return_relaxed	atomic64_dec_return
#define  atomic64_dec_return_acquire	atomic64_dec_return
#define  atomic64_dec_return_release	atomic64_dec_return

#else /* atomic64_dec_return_relaxed */

#ifndef atomic64_dec_return_acquire
#define  atomic64_dec_return_acquire(...)				\
	__atomic_op_acquire(atomic64_dec_return, __VA_ARGS__)
#endif

#ifndef atomic64_dec_return_release
#define  atomic64_dec_return_release(...)				\
	__atomic_op_release(atomic64_dec_return, __VA_ARGS__)
#endif

#ifndef atomic64_dec_return
#define  atomic64_dec_return(...)					\
	__atomic_op_fence(atomic64_dec_return, __VA_ARGS__)
#endif
#endif /* atomic64_dec_return_relaxed */


/* atomic64_fetch_add_relaxed */
#ifndef atomic64_fetch_add_relaxed
#define atomic64_fetch_add_relaxed	atomic64_fetch_add
#define atomic64_fetch_add_acquire	atomic64_fetch_add
#define atomic64_fetch_add_release	atomic64_fetch_add

#else /* atomic64_fetch_add_relaxed */

#ifndef atomic64_fetch_add_acquire
#define atomic64_fetch_add_acquire(...)					\
	__atomic_op_acquire(atomic64_fetch_add, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_add_release
#define atomic64_fetch_add_release(...)					\
	__atomic_op_release(atomic64_fetch_add, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_add
#define atomic64_fetch_add(...)						\
	__atomic_op_fence(atomic64_fetch_add, __VA_ARGS__)
#endif
#endif /* atomic64_fetch_add_relaxed */

/* atomic64_fetch_inc_relaxed */
#ifndef atomic64_fetch_inc_relaxed

#ifndef atomic64_fetch_inc
#define atomic64_fetch_inc(v)		atomic64_fetch_add(1, (v))
#define atomic64_fetch_inc_relaxed(v)	atomic64_fetch_add_relaxed(1, (v))
#define atomic64_fetch_inc_acquire(v)	atomic64_fetch_add_acquire(1, (v))
#define atomic64_fetch_inc_release(v)	atomic64_fetch_add_release(1, (v))
#else /* atomic64_fetch_inc */
#define atomic64_fetch_inc_relaxed	atomic64_fetch_inc
#define atomic64_fetch_inc_acquire	atomic64_fetch_inc
#define atomic64_fetch_inc_release	atomic64_fetch_inc
#endif /* atomic64_fetch_inc */

#else /* atomic64_fetch_inc_relaxed */

#ifndef atomic64_fetch_inc_acquire
#define atomic64_fetch_inc_acquire(...)					\
	__atomic_op_acquire(atomic64_fetch_inc, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_inc_release
#define atomic64_fetch_inc_release(...)					\
	__atomic_op_release(atomic64_fetch_inc, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_inc
#define atomic64_fetch_inc(...)						\
	__atomic_op_fence(atomic64_fetch_inc, __VA_ARGS__)
#endif
#endif /* atomic64_fetch_inc_relaxed */

/* atomic64_fetch_sub_relaxed */
#ifndef atomic64_fetch_sub_relaxed
#define atomic64_fetch_sub_relaxed	atomic64_fetch_sub
#define atomic64_fetch_sub_acquire	atomic64_fetch_sub
#define atomic64_fetch_sub_release	atomic64_fetch_sub

#else /* atomic64_fetch_sub_relaxed */

#ifndef atomic64_fetch_sub_acquire
#define atomic64_fetch_sub_acquire(...)					\
	__atomic_op_acquire(atomic64_fetch_sub, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_sub_release
#define atomic64_fetch_sub_release(...)					\
	__atomic_op_release(atomic64_fetch_sub, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_sub
#define atomic64_fetch_sub(...)						\
	__atomic_op_fence(atomic64_fetch_sub, __VA_ARGS__)
#endif
#endif /* atomic64_fetch_sub_relaxed */

/* atomic64_fetch_dec_relaxed */
#ifndef atomic64_fetch_dec_relaxed

#ifndef atomic64_fetch_dec
#define atomic64_fetch_dec(v)		atomic64_fetch_sub(1, (v))
#define atomic64_fetch_dec_relaxed(v)	atomic64_fetch_sub_relaxed(1, (v))
#define atomic64_fetch_dec_acquire(v)	atomic64_fetch_sub_acquire(1, (v))
#define atomic64_fetch_dec_release(v)	atomic64_fetch_sub_release(1, (v))
#else /* atomic64_fetch_dec */
#define atomic64_fetch_dec_relaxed	atomic64_fetch_dec
#define atomic64_fetch_dec_acquire	atomic64_fetch_dec
#define atomic64_fetch_dec_release	atomic64_fetch_dec
#endif /* atomic64_fetch_dec */

#else /* atomic64_fetch_dec_relaxed */

#ifndef atomic64_fetch_dec_acquire
#define atomic64_fetch_dec_acquire(...)					\
	__atomic_op_acquire(atomic64_fetch_dec, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_dec_release
#define atomic64_fetch_dec_release(...)					\
	__atomic_op_release(atomic64_fetch_dec, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_dec
#define atomic64_fetch_dec(...)						\
	__atomic_op_fence(atomic64_fetch_dec, __VA_ARGS__)
#endif
#endif /* atomic64_fetch_dec_relaxed */

/* atomic64_fetch_or_relaxed */
#ifndef atomic64_fetch_or_relaxed
#define atomic64_fetch_or_relaxed	atomic64_fetch_or
#define atomic64_fetch_or_acquire	atomic64_fetch_or
#define atomic64_fetch_or_release	atomic64_fetch_or

#else /* atomic64_fetch_or_relaxed */

#ifndef atomic64_fetch_or_acquire
#define atomic64_fetch_or_acquire(...)					\
	__atomic_op_acquire(atomic64_fetch_or, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_or_release
#define atomic64_fetch_or_release(...)					\
	__atomic_op_release(atomic64_fetch_or, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_or
#define atomic64_fetch_or(...)						\
	__atomic_op_fence(atomic64_fetch_or, __VA_ARGS__)
#endif
#endif /* atomic64_fetch_or_relaxed */

/* atomic64_fetch_and_relaxed */
#ifndef atomic64_fetch_and_relaxed
#define atomic64_fetch_and_relaxed	atomic64_fetch_and
#define atomic64_fetch_and_acquire	atomic64_fetch_and
#define atomic64_fetch_and_release	atomic64_fetch_and

#else /* atomic64_fetch_and_relaxed */

#ifndef atomic64_fetch_and_acquire
#define atomic64_fetch_and_acquire(...)					\
	__atomic_op_acquire(atomic64_fetch_and, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_and_release
#define atomic64_fetch_and_release(...)					\
	__atomic_op_release(atomic64_fetch_and, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_and
#define atomic64_fetch_and(...)						\
	__atomic_op_fence(atomic64_fetch_and, __VA_ARGS__)
#endif
#endif /* atomic64_fetch_and_relaxed */

#ifdef atomic64_andnot
/* atomic64_fetch_andnot_relaxed */
#ifndef atomic64_fetch_andnot_relaxed
#define atomic64_fetch_andnot_relaxed	atomic64_fetch_andnot
#define atomic64_fetch_andnot_acquire	atomic64_fetch_andnot
#define atomic64_fetch_andnot_release	atomic64_fetch_andnot

#else /* atomic64_fetch_andnot_relaxed */

#ifndef atomic64_fetch_andnot_acquire
#define atomic64_fetch_andnot_acquire(...)					\
	__atomic_op_acquire(atomic64_fetch_andnot, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_andnot_release
#define atomic64_fetch_andnot_release(...)					\
	__atomic_op_release(atomic64_fetch_andnot, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_andnot
#define atomic64_fetch_andnot(...)						\
	__atomic_op_fence(atomic64_fetch_andnot, __VA_ARGS__)
#endif
#endif /* atomic64_fetch_andnot_relaxed */
#endif /* atomic64_andnot */

/* atomic64_fetch_xor_relaxed */
#ifndef atomic64_fetch_xor_relaxed
#define atomic64_fetch_xor_relaxed	atomic64_fetch_xor
#define atomic64_fetch_xor_acquire	atomic64_fetch_xor
#define atomic64_fetch_xor_release	atomic64_fetch_xor

#else /* atomic64_fetch_xor_relaxed */

#ifndef atomic64_fetch_xor_acquire
#define atomic64_fetch_xor_acquire(...)					\
	__atomic_op_acquire(atomic64_fetch_xor, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_xor_release
#define atomic64_fetch_xor_release(...)					\
	__atomic_op_release(atomic64_fetch_xor, __VA_ARGS__)
#endif

#ifndef atomic64_fetch_xor
#define atomic64_fetch_xor(...)						\
	__atomic_op_fence(atomic64_fetch_xor, __VA_ARGS__)
#endif
#endif /* atomic64_fetch_xor_relaxed */


/* atomic64_xchg_relaxed */
#ifndef atomic64_xchg_relaxed
#define  atomic64_xchg_relaxed		atomic64_xchg
#define  atomic64_xchg_acquire		atomic64_xchg
#define  atomic64_xchg_release		atomic64_xchg

#else /* atomic64_xchg_relaxed */

#ifndef atomic64_xchg_acquire
#define  atomic64_xchg_acquire(...)					\
	__atomic_op_acquire(atomic64_xchg, __VA_ARGS__)
#endif

#ifndef atomic64_xchg_release
#define  atomic64_xchg_release(...)					\
	__atomic_op_release(atomic64_xchg, __VA_ARGS__)
#endif

#ifndef atomic64_xchg
#define  atomic64_xchg(...)						\
	__atomic_op_fence(atomic64_xchg, __VA_ARGS__)
#endif
#endif /* atomic64_xchg_relaxed */

/* atomic64_cmpxchg_relaxed */
#ifndef atomic64_cmpxchg_relaxed
#define  atomic64_cmpxchg_relaxed	atomic64_cmpxchg
#define  atomic64_cmpxchg_acquire	atomic64_cmpxchg
#define  atomic64_cmpxchg_release	atomic64_cmpxchg

#else /* atomic64_cmpxchg_relaxed */

#ifndef atomic64_cmpxchg_acquire
#define  atomic64_cmpxchg_acquire(...)					\
	__atomic_op_acquire(atomic64_cmpxchg, __VA_ARGS__)
#endif

#ifndef atomic64_cmpxchg_release
#define  atomic64_cmpxchg_release(...)					\
	__atomic_op_release(atomic64_cmpxchg, __VA_ARGS__)
#endif

#ifndef atomic64_cmpxchg
#define  atomic64_cmpxchg(...)						\
	__atomic_op_fence(atomic64_cmpxchg, __VA_ARGS__)
#endif
#endif /* atomic64_cmpxchg_relaxed */

#ifndef atomic64_try_cmpxchg

#define __atomic64_try_cmpxchg(type, _p, _po, _n)			\
({									\
	typeof(_po) __po = (_po);					\
	typeof(*(_po)) __r, __o = *__po;				\
	__r = atomic64_cmpxchg##type((_p), __o, (_n));			\
	if (unlikely(__r != __o))					\
		*__po = __r;						\
	likely(__r == __o);						\
})

#define atomic64_try_cmpxchg(_p, _po, _n)		__atomic64_try_cmpxchg(, _p, _po, _n)
#define atomic64_try_cmpxchg_relaxed(_p, _po, _n)	__atomic64_try_cmpxchg(_relaxed, _p, _po, _n)
#define atomic64_try_cmpxchg_acquire(_p, _po, _n)	__atomic64_try_cmpxchg(_acquire, _p, _po, _n)
#define atomic64_try_cmpxchg_release(_p, _po, _n)	__atomic64_try_cmpxchg(_release, _p, _po, _n)

#else /* atomic64_try_cmpxchg */
#define atomic64_try_cmpxchg_relaxed	atomic64_try_cmpxchg
#define atomic64_try_cmpxchg_acquire	atomic64_try_cmpxchg
#define atomic64_try_cmpxchg_release	atomic64_try_cmpxchg
#endif /* atomic64_try_cmpxchg */

/**
 * atomic64_fetch_add_unless - add unless the number is already a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, if @v was not already @u.
 * Returns the original value of @v.
 */
#ifndef atomic64_fetch_add_unless
static inline long long atomic64_fetch_add_unless(atomic64_t *v, long long a,
						  long long u)
{
	long long c = atomic64_read(v);

	do {
		if (unlikely(c == u))
			break;
	} while (!atomic64_try_cmpxchg(v, &c, c + a));

	return c;
}
#endif

/**
 * atomic64_add_unless - add unless the number is already a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, if @v was not already @u.
 * Returns true if the addition was done.
 */
static inline bool atomic64_add_unless(atomic64_t *v, long long a, long long u)
{
	return atomic64_fetch_add_unless(v, a, u) != u;
}

/**
 * atomic64_inc_not_zero - increment unless the number is zero
 * @v: pointer of type atomic64_t
 *
 * Atomically increments @v by 1, if @v is non-zero.
 * Returns true if the increment was done.
 */
#ifndef atomic64_inc_not_zero
#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1, 0)
#endif

/**
 * atomic64_inc_and_test - increment and test
 * @v: pointer of type atomic64_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#ifndef atomic64_inc_and_test
static inline bool atomic64_inc_and_test(atomic64_t *v)
{
	return atomic64_inc_return(v) == 0;
}
#endif

/**
 * atomic64_dec_and_test - decrement and test
 * @v: pointer of type atomic64_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
#ifndef atomic64_dec_and_test
static inline bool atomic64_dec_and_test(atomic64_t *v)
{
	return atomic64_dec_return(v) == 0;
}
#endif

/**
 * atomic64_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic64_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
#ifndef atomic64_sub_and_test
static inline bool atomic64_sub_and_test(long long i, atomic64_t *v)
{
	return atomic64_sub_return(i, v) == 0;
}
#endif

/**
 * atomic64_add_negative - add and test if negative
 * @i: integer value to add
 * @v: pointer of type atomic64_t
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
#ifndef atomic64_add_negative
static inline bool atomic64_add_negative(long long i, atomic64_t *v)
{
	return atomic64_add_return(i, v) < 0;
}
#endif

#ifndef atomic64_andnot
static inline void atomic64_andnot(long long i, atomic64_t *v)
{
	atomic64_and(~i, v);
}

static inline long long atomic64_fetch_andnot(long long i, atomic64_t *v)
{
	return atomic64_fetch_and(~i, v);
}

static inline long long atomic64_fetch_andnot_relaxed(long long i, atomic64_t *v)
{
	return atomic64_fetch_and_relaxed(~i, v);
}

static inline long long atomic64_fetch_andnot_acquire(long long i, atomic64_t *v)
{
	return atomic64_fetch_and_acquire(~i, v);
}

static inline long long atomic64_fetch_andnot_release(long long i, atomic64_t *v)
{
	return atomic64_fetch_and_release(~i, v);
}
#endif

#define atomic64_cond_read_relaxed(v, c)	smp_cond_load_relaxed(&(v)->counter, (c))
#define atomic64_cond_read_acquire(v, c)	smp_cond_load_acquire(&(v)->counter, (c))

#include <asm-generic/atomic-long.h>

#endif /* _LINUX_ATOMIC_H */
