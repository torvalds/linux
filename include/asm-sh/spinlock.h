/*
 * include/asm-sh/spinlock.h
 *
 * Copyright (C) 2002, 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_SPINLOCK_H
#define __ASM_SH_SPINLOCK_H

#include <asm/atomic.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

#define __raw_spin_is_locked(x)	((x)->lock != 0)
#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)
#define __raw_spin_unlock_wait(x) \
	do { cpu_relax(); } while (__raw_spin_is_locked(x))

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions.  They have a cost.
 */
static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	__asm__ __volatile__ (
		"1:\n\t"
		"tas.b @%0\n\t"
		"bf/s 1b\n\t"
		"nop\n\t"
		: "=r" (lock->lock)
		: "r" (&lock->lock)
		: "t", "memory"
	);
}

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	assert_spin_locked(lock);

	lock->lock = 0;
}

#define __raw_spin_trylock(x) (!test_and_set_bit(0, &(x)->lock))

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts but no interrupt
 * writers. For those circumstances we can "mix" irq-safe locks - any writer
 * needs to get a irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */

static inline void __raw_read_lock(raw_rwlock_t *rw)
{
	__raw_spin_lock(&rw->lock);

	atomic_inc(&rw->counter);

	__raw_spin_unlock(&rw->lock);
}

static inline void __raw_read_unlock(raw_rwlock_t *rw)
{
	__raw_spin_lock(&rw->lock);

	atomic_dec(&rw->counter);

	__raw_spin_unlock(&rw->lock);
}

static inline void __raw_write_lock(raw_rwlock_t *rw)
{
	__raw_spin_lock(&rw->lock);
	atomic_set(&rw->counter, -1);
}

static inline void __raw_write_unlock(raw_rwlock_t *rw)
{
	atomic_set(&rw->counter, 0);
	__raw_spin_unlock(&rw->lock);
}

#define __raw_read_trylock(lock) generic__raw_read_trylock(lock)

static inline int __raw_write_trylock(raw_rwlock_t *rw)
{
	if (atomic_sub_and_test(RW_LOCK_BIAS, &rw->counter))
		return 1;
	
	atomic_add(RW_LOCK_BIAS, &rw->counter);

	return 0;
}

#endif /* __ASM_SH_SPINLOCK_H */
