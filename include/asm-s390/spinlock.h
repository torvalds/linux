/*
 *  include/asm-s390/spinlock.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/spinlock.h"
 */

#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <linux/smp.h>

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)

static inline int
_raw_compare_and_swap(volatile unsigned int *lock,
		      unsigned int old, unsigned int new)
{
	asm volatile(
		"	cs	%0,%3,%1"
		: "=d" (old), "=Q" (*lock)
		: "0" (old), "d" (new), "Q" (*lock)
		: "cc", "memory" );
	return old;
}

#else /* __GNUC__ */

static inline int
_raw_compare_and_swap(volatile unsigned int *lock,
		      unsigned int old, unsigned int new)
{
	asm volatile(
		"	cs	%0,%3,0(%4)"
		: "=d" (old), "=m" (*lock)
		: "0" (old), "d" (new), "a" (lock), "m" (*lock)
		: "cc", "memory" );
	return old;
}

#endif /* __GNUC__ */

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

#define __raw_spin_is_locked(x) ((x)->owner_cpu != 0)
#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)
#define __raw_spin_unlock_wait(lock) \
	do { while (__raw_spin_is_locked(lock)) \
		 _raw_spin_relax(lock); } while (0)

extern void _raw_spin_lock_wait(raw_spinlock_t *, unsigned int pc);
extern int _raw_spin_trylock_retry(raw_spinlock_t *, unsigned int pc);
extern void _raw_spin_relax(raw_spinlock_t *lock);

static inline void __raw_spin_lock(raw_spinlock_t *lp)
{
	unsigned long pc = 1 | (unsigned long) __builtin_return_address(0);
	int old;

	old = _raw_compare_and_swap(&lp->owner_cpu, 0, ~smp_processor_id());
	if (likely(old == 0)) {
		lp->owner_pc = pc;
		return;
	}
	_raw_spin_lock_wait(lp, pc);
}

static inline int __raw_spin_trylock(raw_spinlock_t *lp)
{
	unsigned long pc = 1 | (unsigned long) __builtin_return_address(0);
	int old;

	old = _raw_compare_and_swap(&lp->owner_cpu, 0, ~smp_processor_id());
	if (likely(old == 0)) {
		lp->owner_pc = pc;
		return 1;
	}
	return _raw_spin_trylock_retry(lp, pc);
}

static inline void __raw_spin_unlock(raw_spinlock_t *lp)
{
	lp->owner_pc = 0;
	_raw_compare_and_swap(&lp->owner_cpu, lp->owner_cpu, 0);
}
		
/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define __raw_read_can_lock(x) ((int)(x)->lock >= 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define __raw_write_can_lock(x) ((x)->lock == 0)

extern void _raw_read_lock_wait(raw_rwlock_t *lp);
extern int _raw_read_trylock_retry(raw_rwlock_t *lp);
extern void _raw_write_lock_wait(raw_rwlock_t *lp);
extern int _raw_write_trylock_retry(raw_rwlock_t *lp);

static inline void __raw_read_lock(raw_rwlock_t *rw)
{
	unsigned int old;
	old = rw->lock & 0x7fffffffU;
	if (_raw_compare_and_swap(&rw->lock, old, old + 1) != old)
		_raw_read_lock_wait(rw);
}

static inline void __raw_read_unlock(raw_rwlock_t *rw)
{
	unsigned int old, cmp;

	old = rw->lock;
	do {
		cmp = old;
		old = _raw_compare_and_swap(&rw->lock, old, old - 1);
	} while (cmp != old);
}

static inline void __raw_write_lock(raw_rwlock_t *rw)
{
	if (unlikely(_raw_compare_and_swap(&rw->lock, 0, 0x80000000) != 0))
		_raw_write_lock_wait(rw);
}

static inline void __raw_write_unlock(raw_rwlock_t *rw)
{
	_raw_compare_and_swap(&rw->lock, 0x80000000, 0);
}

static inline int __raw_read_trylock(raw_rwlock_t *rw)
{
	unsigned int old;
	old = rw->lock & 0x7fffffffU;
	if (likely(_raw_compare_and_swap(&rw->lock, old, old + 1) == old))
		return 1;
	return _raw_read_trylock_retry(rw);
}

static inline int __raw_write_trylock(raw_rwlock_t *rw)
{
	if (likely(_raw_compare_and_swap(&rw->lock, 0, 0x80000000) == 0))
		return 1;
	return _raw_write_trylock_retry(rw);
}

#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
