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
typedef struct {
	volatile unsigned long lock;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} spinlock_t;

#define SPIN_LOCK_UNLOCKED	(spinlock_t) { 0 }

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

#define spin_is_locked(x)	((x)->lock != 0)
#define spin_unlock_wait(x)	do { barrier(); } while (spin_is_locked(x))
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions.  They have a cost.
 */
static inline void _raw_spin_lock(spinlock_t *lock)
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

static inline void _raw_spin_unlock(spinlock_t *lock)
{
	assert_spin_locked(lock);

	lock->lock = 0;
}

#define _raw_spin_trylock(x) (!test_and_set_bit(0, &(x)->lock))

/*
 * Read-write spinlocks, allowing multiple readers but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts but no interrupt
 * writers. For those circumstances we can "mix" irq-safe locks - any writer
 * needs to get a irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */
typedef struct {
	spinlock_t lock;
	atomic_t counter;
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} rwlock_t;

#define RW_LOCK_BIAS		0x01000000
#define RW_LOCK_UNLOCKED	(rwlock_t) { { 0 }, { RW_LOCK_BIAS } }
#define rwlock_init(x)		do { *(x) = RW_LOCK_UNLOCKED; } while (0)

static inline void _raw_read_lock(rwlock_t *rw)
{
	_raw_spin_lock(&rw->lock);

	atomic_inc(&rw->counter);

	_raw_spin_unlock(&rw->lock);
}

static inline void _raw_read_unlock(rwlock_t *rw)
{
	_raw_spin_lock(&rw->lock);

	atomic_dec(&rw->counter);

	_raw_spin_unlock(&rw->lock);
}

static inline void _raw_write_lock(rwlock_t *rw)
{
	_raw_spin_lock(&rw->lock);
	atomic_set(&rw->counter, -1);
}

static inline void _raw_write_unlock(rwlock_t *rw)
{
	atomic_set(&rw->counter, 0);
	_raw_spin_unlock(&rw->lock);
}

#define _raw_read_trylock(lock) generic_raw_read_trylock(lock)

static inline int _raw_write_trylock(rwlock_t *rw)
{
	if (atomic_sub_and_test(RW_LOCK_BIAS, &rw->counter))
		return 1;
	
	atomic_add(RW_LOCK_BIAS, &rw->counter);

	return 0;
}

#endif /* __ASM_SH_SPINLOCK_H */

