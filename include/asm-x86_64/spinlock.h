#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <asm/page.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 *
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

#define __raw_spin_is_locked(x) \
		(*(volatile signed int *)(&(x)->slock) <= 0)

#define __raw_spin_lock_string \
	"\n1:\t" \
	"lock ; decl %0\n\t" \
	"js 2f\n" \
	LOCK_SECTION_START("") \
	"2:\t" \
	"rep;nop\n\t" \
	"cmpl $0,%0\n\t" \
	"jle 2b\n\t" \
	"jmp 1b\n" \
	LOCK_SECTION_END

#define __raw_spin_unlock_string \
	"movl $1,%0" \
		:"=m" (lock->slock) : : "memory"

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	__asm__ __volatile__(
		__raw_spin_lock_string
		:"=m" (lock->slock) : : "memory");
}

#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)

static inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	int oldval;

	__asm__ __volatile__(
		"xchgl %0,%1"
		:"=q" (oldval), "=m" (lock->slock)
		:"0" (0) : "memory");

	return oldval > 0;
}

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__asm__ __volatile__(
		__raw_spin_unlock_string
	);
}

#define __raw_spin_unlock_wait(lock) \
	do { while (__raw_spin_is_locked(lock)) cpu_relax(); } while (0)

/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 *
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 *
 * The inline assembly is non-obvious. Think about it.
 *
 * Changed to use the same technique as rw semaphores.  See
 * semaphore.h for details.  -ben
 *
 * the helpers are in arch/i386/kernel/semaphore.c
 */

#define __raw_read_can_lock(x)		((int)(x)->lock > 0)
#define __raw_write_can_lock(x)		((x)->lock == RW_LOCK_BIAS)

static inline void __raw_read_lock(raw_rwlock_t *rw)
{
	__build_read_lock(rw, "__read_lock_failed");
}

static inline void __raw_write_lock(raw_rwlock_t *rw)
{
	__build_write_lock(rw, "__write_lock_failed");
}

static inline int __raw_read_trylock(raw_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	atomic_dec(count);
	if (atomic_read(count) >= 0)
		return 1;
	atomic_inc(count);
	return 0;
}

static inline int __raw_write_trylock(raw_rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

static inline void __raw_read_unlock(raw_rwlock_t *rw)
{
	asm volatile("lock ; incl %0" :"=m" (rw->lock) : : "memory");
}

static inline void __raw_write_unlock(raw_rwlock_t *rw)
{
	asm volatile("lock ; addl $" RW_LOCK_BIAS_STR ",%0"
				: "=m" (rw->lock) : : "memory");
}

#endif /* __ASM_SPINLOCK_H */
