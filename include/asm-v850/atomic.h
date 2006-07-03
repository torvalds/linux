/*
 * include/asm-v850/atomic.h -- Atomic operations
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_ATOMIC_H__
#define __V850_ATOMIC_H__


#include <asm/system.h>

#ifdef CONFIG_SMP
#error SMP not supported
#endif

typedef struct { int counter; } atomic_t;

#define ATOMIC_INIT(i)	{ (i) }

#ifdef __KERNEL__

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		(((v)->counter) = (i))

static inline int atomic_add_return (int i, volatile atomic_t *v)
{
	unsigned long flags;
	int res;

	local_irq_save (flags);
	res = v->counter + i;
	v->counter = res;
	local_irq_restore (flags);

	return res;
}

static __inline__ int atomic_sub_return (int i, volatile atomic_t *v)
{
	unsigned long flags;
	int res;

	local_irq_save (flags);
	res = v->counter - i;
	v->counter = res;
	local_irq_restore (flags);

	return res;
}

static __inline__ void atomic_clear_mask (unsigned long mask, unsigned long *addr)
{
	unsigned long flags;

	local_irq_save (flags);
	*addr &= ~mask;
	local_irq_restore (flags);
}

#endif

#define atomic_add(i, v)	atomic_add_return ((i), (v))
#define atomic_sub(i, v)	atomic_sub_return ((i), (v))

#define atomic_dec_return(v)	atomic_sub_return (1, (v))
#define atomic_inc_return(v)	atomic_add_return (1, (v))
#define atomic_inc(v) 		atomic_inc_return (v)
#define atomic_dec(v) 		atomic_dec_return (v)

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

#define atomic_sub_and_test(i,v)	(atomic_sub_return ((i), (v)) == 0)
#define atomic_dec_and_test(v)		(atomic_sub_return (1, (v)) == 0)
#define atomic_add_negative(i,v)	(atomic_add_return ((i), (v)) < 0)

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	unsigned long flags;

	local_irq_save(flags);
	ret = v->counter;
	if (likely(ret == old))
		v->counter = new;
	local_irq_restore(flags);

	return ret;
}

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static inline int atomic_add_unless(atomic_t *v, int a, int u)
{
	int ret;
	unsigned long flags;

	local_irq_save(flags);
	ret = v->counter;
	if (ret != u)
		v->counter += a;
	local_irq_restore(flags);

	return ret != u;
}

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

/* Atomic operations are already serializing on ARM */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#include <asm-generic/atomic.h>
#endif /* __V850_ATOMIC_H__ */
