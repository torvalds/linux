/* $Id: atomic.h,v 1.22 2001/07/11 23:56:07 davem Exp $
 * atomic.h: Thankfully the V9 is at least reasonable for this
 *           stuff.
 *
 * Copyright (C) 1996, 1997, 2000 David S. Miller (davem@redhat.com)
 */

#ifndef __ARCH_SPARC64_ATOMIC__
#define __ARCH_SPARC64_ATOMIC__

#include <linux/types.h>
#include <asm/system.h>

typedef struct { volatile int counter; } atomic_t;
typedef struct { volatile __s64 counter; } atomic64_t;

#define ATOMIC_INIT(i)		{ (i) }
#define ATOMIC64_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic64_read(v)	((v)->counter)

#define atomic_set(v, i)	(((v)->counter) = i)
#define atomic64_set(v, i)	(((v)->counter) = i)

extern void atomic_add(int, atomic_t *);
extern void atomic64_add(int, atomic64_t *);
extern void atomic_sub(int, atomic_t *);
extern void atomic64_sub(int, atomic64_t *);

extern int atomic_add_ret(int, atomic_t *);
extern int atomic64_add_ret(int, atomic64_t *);
extern int atomic_sub_ret(int, atomic_t *);
extern int atomic64_sub_ret(int, atomic64_t *);

#define atomic_dec_return(v) atomic_sub_ret(1, v)
#define atomic64_dec_return(v) atomic64_sub_ret(1, v)

#define atomic_inc_return(v) atomic_add_ret(1, v)
#define atomic64_inc_return(v) atomic64_add_ret(1, v)

#define atomic_sub_return(i, v) atomic_sub_ret(i, v)
#define atomic64_sub_return(i, v) atomic64_sub_ret(i, v)

#define atomic_add_return(i, v) atomic_add_ret(i, v)
#define atomic64_add_return(i, v) atomic64_add_ret(i, v)

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)
#define atomic64_inc_and_test(v) (atomic64_inc_return(v) == 0)

#define atomic_sub_and_test(i, v) (atomic_sub_ret(i, v) == 0)
#define atomic64_sub_and_test(i, v) (atomic64_sub_ret(i, v) == 0)

#define atomic_dec_and_test(v) (atomic_sub_ret(1, v) == 0)
#define atomic64_dec_and_test(v) (atomic64_sub_ret(1, v) == 0)

#define atomic_inc(v) atomic_add(1, v)
#define atomic64_inc(v) atomic64_add(1, v)

#define atomic_dec(v) atomic_sub(1, v)
#define atomic64_dec(v) atomic64_sub(1, v)

#define atomic_add_negative(i, v) (atomic_add_ret(i, v) < 0)
#define atomic64_add_negative(i, v) (atomic64_add_ret(i, v) < 0)

#define atomic_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static __inline__ int atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c != (u);
}

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

#define atomic64_cmpxchg(v, o, n) \
	((__typeof__((v)->counter))cmpxchg(&((v)->counter), (o), (n)))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

static __inline__ int atomic64_add_unless(atomic64_t *v, long a, long u)
{
	long c, old;
	c = atomic64_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic64_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c != (u);
}

#define atomic64_inc_not_zero(v) atomic64_add_unless((v), 1, 0)

/* Atomic operations are already serializing */
#ifdef CONFIG_SMP
#define smp_mb__before_atomic_dec()	membar_storeload_loadload();
#define smp_mb__after_atomic_dec()	membar_storeload_storestore();
#define smp_mb__before_atomic_inc()	membar_storeload_loadload();
#define smp_mb__after_atomic_inc()	membar_storeload_storestore();
#else
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()
#endif

#include <asm-generic/atomic.h>
#endif /* !(__ARCH_SPARC64_ATOMIC__) */
