/*
 *  linux/include/asm-arm26/atomic.h
 *
 *  Copyright (c) 1996 Russell King.
 *  Modified for arm26 by Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   25-11-2004 IM	Updated for 2.6.9
 *   27-06-1996	RMK	Created
 *   13-04-1997	RMK	Made functions atomic!
 *   07-12-1997	RMK	Upgraded for v2.1.
 *   26-08-1998	PJB	Added #ifdef __KERNEL__
 *
 *  FIXME - its probably worth seeing what these compile into...
 */
#ifndef __ASM_ARM_ATOMIC_H
#define __ASM_ARM_ATOMIC_H

#include <linux/config.h>

#ifdef CONFIG_SMP
#error SMP is NOT supported
#endif

typedef struct { volatile int counter; } atomic_t;

#define ATOMIC_INIT(i)	{ (i) }

#ifdef __KERNEL__
#include <asm/system.h>

#define atomic_read(v) ((v)->counter)
#define atomic_set(v,i)	(((v)->counter) = (i))

static inline int atomic_add_return(int i, atomic_t *v)
{
        unsigned long flags;
        int val;

        local_irq_save(flags);
        val = v->counter;
        v->counter = val += i;
        local_irq_restore(flags);

        return val;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
        unsigned long flags;
        int val;

        local_irq_save(flags);
        val = v->counter;
        v->counter = val -= i;
        local_irq_restore(flags);

        return val;
}

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

static inline void atomic_clear_mask(unsigned long mask, unsigned long *addr)
{
        unsigned long flags;

        local_irq_save(flags);
        *addr &= ~mask;
        local_irq_restore(flags);
}

#define atomic_add(i, v)        (void) atomic_add_return(i, v)
#define atomic_inc(v)           (void) atomic_add_return(1, v)
#define atomic_sub(i, v)        (void) atomic_sub_return(i, v)
#define atomic_dec(v)           (void) atomic_sub_return(1, v)

#define atomic_inc_and_test(v)  (atomic_add_return(1, v) == 0)
#define atomic_dec_and_test(v)  (atomic_sub_return(1, v) == 0)
#define atomic_inc_return(v)    (atomic_add_return(1, v))
#define atomic_dec_return(v)    (atomic_sub_return(1, v))

#define atomic_add_negative(i,v) (atomic_add_return(i, v) < 0)

/* Atomic operations are already serializing on ARM26 */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#include <asm-generic/atomic.h>
#endif
#endif
