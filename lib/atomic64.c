// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic implementation of 64-bit atomics using spinlocks,
 * useful on processors that don't have 64-bit atomic instructions.
 *
 * Copyright © 2009 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 */
#include <linux/types.h>
#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/atomic.h>

/*
 * We use a hashed array of spinlocks to provide exclusive access
 * to each atomic64_t variable.  Since this is expected to used on
 * systems with small numbers of CPUs (<= 4 or so), we use a
 * relatively small array of 16 spinlocks to avoid wasting too much
 * memory on the spinlock array.
 */
#define NR_LOCKS	16

/*
 * Ensure each lock is in a separate cacheline.
 */
static union {
	arch_spinlock_t lock;
	char pad[L1_CACHE_BYTES];
} atomic64_lock[NR_LOCKS] __cacheline_aligned_in_smp = {
	[0 ... (NR_LOCKS - 1)] = {
		.lock =  __ARCH_SPIN_LOCK_UNLOCKED,
	},
};

static inline arch_spinlock_t *lock_addr(const atomic64_t *v)
{
	unsigned long addr = (unsigned long) v;

	addr >>= L1_CACHE_SHIFT;
	addr ^= (addr >> 8) ^ (addr >> 16);
	return &atomic64_lock[addr & (NR_LOCKS - 1)].lock;
}

s64 generic_atomic64_read(const atomic64_t *v)
{
	unsigned long flags;
	arch_spinlock_t *lock = lock_addr(v);
	s64 val;

	local_irq_save(flags);
	arch_spin_lock(lock);
	val = v->counter;
	arch_spin_unlock(lock);
	local_irq_restore(flags);
	return val;
}
EXPORT_SYMBOL(generic_atomic64_read);

void generic_atomic64_set(atomic64_t *v, s64 i)
{
	unsigned long flags;
	arch_spinlock_t *lock = lock_addr(v);

	local_irq_save(flags);
	arch_spin_lock(lock);
	v->counter = i;
	arch_spin_unlock(lock);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(generic_atomic64_set);

#define ATOMIC64_OP(op, c_op)						\
void generic_atomic64_##op(s64 a, atomic64_t *v)			\
{									\
	unsigned long flags;						\
	arch_spinlock_t *lock = lock_addr(v);				\
									\
	local_irq_save(flags);						\
	arch_spin_lock(lock);						\
	v->counter c_op a;						\
	arch_spin_unlock(lock);						\
	local_irq_restore(flags);					\
}									\
EXPORT_SYMBOL(generic_atomic64_##op);

#define ATOMIC64_OP_RETURN(op, c_op)					\
s64 generic_atomic64_##op##_return(s64 a, atomic64_t *v)		\
{									\
	unsigned long flags;						\
	arch_spinlock_t *lock = lock_addr(v);				\
	s64 val;							\
									\
	local_irq_save(flags);						\
	arch_spin_lock(lock);						\
	val = (v->counter c_op a);					\
	arch_spin_unlock(lock);						\
	local_irq_restore(flags);					\
	return val;							\
}									\
EXPORT_SYMBOL(generic_atomic64_##op##_return);

#define ATOMIC64_FETCH_OP(op, c_op)					\
s64 generic_atomic64_fetch_##op(s64 a, atomic64_t *v)			\
{									\
	unsigned long flags;						\
	arch_spinlock_t *lock = lock_addr(v);				\
	s64 val;							\
									\
	local_irq_save(flags);						\
	arch_spin_lock(lock);						\
	val = v->counter;						\
	v->counter c_op a;						\
	arch_spin_unlock(lock);						\
	local_irq_restore(flags);					\
	return val;							\
}									\
EXPORT_SYMBOL(generic_atomic64_fetch_##op);

#define ATOMIC64_OPS(op, c_op)						\
	ATOMIC64_OP(op, c_op)						\
	ATOMIC64_OP_RETURN(op, c_op)					\
	ATOMIC64_FETCH_OP(op, c_op)

ATOMIC64_OPS(add, +=)
ATOMIC64_OPS(sub, -=)

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(op, c_op)						\
	ATOMIC64_OP(op, c_op)						\
	ATOMIC64_FETCH_OP(op, c_op)

ATOMIC64_OPS(and, &=)
ATOMIC64_OPS(or, |=)
ATOMIC64_OPS(xor, ^=)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP

s64 generic_atomic64_dec_if_positive(atomic64_t *v)
{
	unsigned long flags;
	arch_spinlock_t *lock = lock_addr(v);
	s64 val;

	local_irq_save(flags);
	arch_spin_lock(lock);
	val = v->counter - 1;
	if (val >= 0)
		v->counter = val;
	arch_spin_unlock(lock);
	local_irq_restore(flags);
	return val;
}
EXPORT_SYMBOL(generic_atomic64_dec_if_positive);

s64 generic_atomic64_cmpxchg(atomic64_t *v, s64 o, s64 n)
{
	unsigned long flags;
	arch_spinlock_t *lock = lock_addr(v);
	s64 val;

	local_irq_save(flags);
	arch_spin_lock(lock);
	val = v->counter;
	if (val == o)
		v->counter = n;
	arch_spin_unlock(lock);
	local_irq_restore(flags);
	return val;
}
EXPORT_SYMBOL(generic_atomic64_cmpxchg);

s64 generic_atomic64_xchg(atomic64_t *v, s64 new)
{
	unsigned long flags;
	arch_spinlock_t *lock = lock_addr(v);
	s64 val;

	local_irq_save(flags);
	arch_spin_lock(lock);
	val = v->counter;
	v->counter = new;
	arch_spin_unlock(lock);
	local_irq_restore(flags);
	return val;
}
EXPORT_SYMBOL(generic_atomic64_xchg);

s64 generic_atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u)
{
	unsigned long flags;
	arch_spinlock_t *lock = lock_addr(v);
	s64 val;

	local_irq_save(flags);
	arch_spin_lock(lock);
	val = v->counter;
	if (val != u)
		v->counter += a;
	arch_spin_unlock(lock);
	local_irq_restore(flags);

	return val;
}
EXPORT_SYMBOL(generic_atomic64_fetch_add_unless);
