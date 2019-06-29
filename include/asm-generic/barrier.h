/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic barrier definitions.
 *
 * It should be possible to use these on really simple architectures,
 * but it serves more as a starting point for new ports.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#ifndef __ASM_GENERIC_BARRIER_H
#define __ASM_GENERIC_BARRIER_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>

#ifndef nop
#define nop()	asm volatile ("nop")
#endif

/*
 * Force strict CPU ordering. And yes, this is required on UP too when we're
 * talking to devices.
 *
 * Fall back to compiler barriers if nothing better is provided.
 */

#ifndef mb
#define mb()	barrier()
#endif

#ifndef rmb
#define rmb()	mb()
#endif

#ifndef wmb
#define wmb()	mb()
#endif

#ifndef dma_rmb
#define dma_rmb()	rmb()
#endif

#ifndef dma_wmb
#define dma_wmb()	wmb()
#endif

#ifndef read_barrier_depends
#define read_barrier_depends()		do { } while (0)
#endif

#ifndef __smp_mb
#define __smp_mb()	mb()
#endif

#ifndef __smp_rmb
#define __smp_rmb()	rmb()
#endif

#ifndef __smp_wmb
#define __smp_wmb()	wmb()
#endif

#ifndef __smp_read_barrier_depends
#define __smp_read_barrier_depends()	read_barrier_depends()
#endif

#ifdef CONFIG_SMP

#ifndef smp_mb
#define smp_mb()	__smp_mb()
#endif

#ifndef smp_rmb
#define smp_rmb()	__smp_rmb()
#endif

#ifndef smp_wmb
#define smp_wmb()	__smp_wmb()
#endif

#ifndef smp_read_barrier_depends
#define smp_read_barrier_depends()	__smp_read_barrier_depends()
#endif

#else	/* !CONFIG_SMP */

#ifndef smp_mb
#define smp_mb()	barrier()
#endif

#ifndef smp_rmb
#define smp_rmb()	barrier()
#endif

#ifndef smp_wmb
#define smp_wmb()	barrier()
#endif

#ifndef smp_read_barrier_depends
#define smp_read_barrier_depends()	do { } while (0)
#endif

#endif	/* CONFIG_SMP */

#ifndef __smp_store_mb
#define __smp_store_mb(var, value)  do { WRITE_ONCE(var, value); __smp_mb(); } while (0)
#endif

#ifndef __smp_mb__before_atomic
#define __smp_mb__before_atomic()	__smp_mb()
#endif

#ifndef __smp_mb__after_atomic
#define __smp_mb__after_atomic()	__smp_mb()
#endif

#ifndef __smp_store_release
#define __smp_store_release(p, v)					\
do {									\
	compiletime_assert_atomic_type(*p);				\
	__smp_mb();							\
	WRITE_ONCE(*p, v);						\
} while (0)
#endif

#ifndef __smp_load_acquire
#define __smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	__smp_mb();							\
	___p1;								\
})
#endif

#ifdef CONFIG_SMP

#ifndef smp_store_mb
#define smp_store_mb(var, value)  __smp_store_mb(var, value)
#endif

#ifndef smp_mb__before_atomic
#define smp_mb__before_atomic()	__smp_mb__before_atomic()
#endif

#ifndef smp_mb__after_atomic
#define smp_mb__after_atomic()	__smp_mb__after_atomic()
#endif

#ifndef smp_store_release
#define smp_store_release(p, v) __smp_store_release(p, v)
#endif

#ifndef smp_load_acquire
#define smp_load_acquire(p) __smp_load_acquire(p)
#endif

#else	/* !CONFIG_SMP */

#ifndef smp_store_mb
#define smp_store_mb(var, value)  do { WRITE_ONCE(var, value); barrier(); } while (0)
#endif

#ifndef smp_mb__before_atomic
#define smp_mb__before_atomic()	barrier()
#endif

#ifndef smp_mb__after_atomic
#define smp_mb__after_atomic()	barrier()
#endif

#ifndef smp_store_release
#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	WRITE_ONCE(*p, v);						\
} while (0)
#endif

#ifndef smp_load_acquire
#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	barrier();							\
	___p1;								\
})
#endif

#endif	/* CONFIG_SMP */

/* Barriers for virtual machine guests when talking to an SMP host */
#define virt_mb() __smp_mb()
#define virt_rmb() __smp_rmb()
#define virt_wmb() __smp_wmb()
#define virt_read_barrier_depends() __smp_read_barrier_depends()
#define virt_store_mb(var, value) __smp_store_mb(var, value)
#define virt_mb__before_atomic() __smp_mb__before_atomic()
#define virt_mb__after_atomic()	__smp_mb__after_atomic()
#define virt_store_release(p, v) __smp_store_release(p, v)
#define virt_load_acquire(p) __smp_load_acquire(p)

/**
 * smp_acquire__after_ctrl_dep() - Provide ACQUIRE ordering after a control dependency
 *
 * A control dependency provides a LOAD->STORE order, the additional RMB
 * provides LOAD->LOAD order, together they provide LOAD->{LOAD,STORE} order,
 * aka. (load)-ACQUIRE.
 *
 * Architectures that do not do load speculation can have this be barrier().
 */
#ifndef smp_acquire__after_ctrl_dep
#define smp_acquire__after_ctrl_dep()		smp_rmb()
#endif

/**
 * smp_cond_load_relaxed() - (Spin) wait for cond with no ordering guarantees
 * @ptr: pointer to the variable to wait on
 * @cond: boolean expression to wait for
 *
 * Equivalent to using READ_ONCE() on the condition variable.
 *
 * Due to C lacking lambda expressions we load the value of *ptr into a
 * pre-named variable @VAL to be used in @cond.
 */
#ifndef smp_cond_load_relaxed
#define smp_cond_load_relaxed(ptr, cond_expr) ({		\
	typeof(ptr) __PTR = (ptr);				\
	typeof(*ptr) VAL;					\
	for (;;) {						\
		VAL = READ_ONCE(*__PTR);			\
		if (cond_expr)					\
			break;					\
		cpu_relax();					\
	}							\
	VAL;							\
})
#endif

/**
 * smp_cond_load_acquire() - (Spin) wait for cond with ACQUIRE ordering
 * @ptr: pointer to the variable to wait on
 * @cond: boolean expression to wait for
 *
 * Equivalent to using smp_load_acquire() on the condition variable but employs
 * the control dependency of the wait to reduce the barrier on many platforms.
 */
#ifndef smp_cond_load_acquire
#define smp_cond_load_acquire(ptr, cond_expr) ({		\
	typeof(*ptr) _val;					\
	_val = smp_cond_load_relaxed(ptr, cond_expr);		\
	smp_acquire__after_ctrl_dep();				\
	_val;							\
})
#endif

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_GENERIC_BARRIER_H */
