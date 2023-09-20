/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Generic C implementation of atomic counter operations. Do not include in
 * machine independent code.
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#ifndef __ASM_GENERIC_ATOMIC_H
#define __ASM_GENERIC_ATOMIC_H

#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#ifdef CONFIG_SMP

/* we can build all atomic primitives from cmpxchg */

#define ATOMIC_OP(op, c_op)						\
static inline void generic_atomic_##op(int i, atomic_t *v)		\
{									\
	int c, old;							\
									\
	c = v->counter;							\
	while ((old = arch_cmpxchg(&v->counter, c, c c_op i)) != c)	\
		c = old;						\
}

#define ATOMIC_OP_RETURN(op, c_op)					\
static inline int generic_atomic_##op##_return(int i, atomic_t *v)	\
{									\
	int c, old;							\
									\
	c = v->counter;							\
	while ((old = arch_cmpxchg(&v->counter, c, c c_op i)) != c)	\
		c = old;						\
									\
	return c c_op i;						\
}

#define ATOMIC_FETCH_OP(op, c_op)					\
static inline int generic_atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	int c, old;							\
									\
	c = v->counter;							\
	while ((old = arch_cmpxchg(&v->counter, c, c c_op i)) != c)	\
		c = old;						\
									\
	return c;							\
}

#else

#include <linux/irqflags.h>

#define ATOMIC_OP(op, c_op)						\
static inline void generic_atomic_##op(int i, atomic_t *v)		\
{									\
	unsigned long flags;						\
									\
	raw_local_irq_save(flags);					\
	v->counter = v->counter c_op i;					\
	raw_local_irq_restore(flags);					\
}

#define ATOMIC_OP_RETURN(op, c_op)					\
static inline int generic_atomic_##op##_return(int i, atomic_t *v)	\
{									\
	unsigned long flags;						\
	int ret;							\
									\
	raw_local_irq_save(flags);					\
	ret = (v->counter = v->counter c_op i);				\
	raw_local_irq_restore(flags);					\
									\
	return ret;							\
}

#define ATOMIC_FETCH_OP(op, c_op)					\
static inline int generic_atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	unsigned long flags;						\
	int ret;							\
									\
	raw_local_irq_save(flags);					\
	ret = v->counter;						\
	v->counter = v->counter c_op i;					\
	raw_local_irq_restore(flags);					\
									\
	return ret;							\
}

#endif /* CONFIG_SMP */

ATOMIC_OP_RETURN(add, +)
ATOMIC_OP_RETURN(sub, -)

ATOMIC_FETCH_OP(add, +)
ATOMIC_FETCH_OP(sub, -)
ATOMIC_FETCH_OP(and, &)
ATOMIC_FETCH_OP(or, |)
ATOMIC_FETCH_OP(xor, ^)

ATOMIC_OP(add, +)
ATOMIC_OP(sub, -)
ATOMIC_OP(and, &)
ATOMIC_OP(or, |)
ATOMIC_OP(xor, ^)

#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define arch_atomic_add_return			generic_atomic_add_return
#define arch_atomic_sub_return			generic_atomic_sub_return

#define arch_atomic_fetch_add			generic_atomic_fetch_add
#define arch_atomic_fetch_sub			generic_atomic_fetch_sub
#define arch_atomic_fetch_and			generic_atomic_fetch_and
#define arch_atomic_fetch_or			generic_atomic_fetch_or
#define arch_atomic_fetch_xor			generic_atomic_fetch_xor

#define arch_atomic_add				generic_atomic_add
#define arch_atomic_sub				generic_atomic_sub
#define arch_atomic_and				generic_atomic_and
#define arch_atomic_or				generic_atomic_or
#define arch_atomic_xor				generic_atomic_xor

#define arch_atomic_read(v)			READ_ONCE((v)->counter)
#define arch_atomic_set(v, i)			WRITE_ONCE(((v)->counter), (i))

#endif /* __ASM_GENERIC_ATOMIC_H */
