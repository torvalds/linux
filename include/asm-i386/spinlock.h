#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <asm/page.h>
#include <linux/config.h>
#include <linux/compiler.h>

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
		(*(volatile signed char *)(&(x)->slock) <= 0)

#define __raw_spin_lock_string \
	"\n1:\t" \
	"lock ; decb %0\n\t" \
	"jns 3f\n" \
	"2:\t" \
	"rep;nop\n\t" \
	"cmpb $0,%0\n\t" \
	"jle 2b\n\t" \
	"jmp 1b\n" \
	"3:\n\t"

#define __raw_spin_lock_string_flags \
	"\n1:\t" \
	"lock ; decb %0\n\t" \
	"jns 4f\n\t" \
	"2:\t" \
	"testl $0x200, %1\n\t" \
	"jz 3f\n\t" \
	"sti\n\t" \
	"3:\t" \
	"rep;nop\n\t" \
	"cmpb $0, %0\n\t" \
	"jle 3b\n\t" \
	"cli\n\t" \
	"jmp 1b\n" \
	"4:\n\t"

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	__asm__ __volatile__(
		__raw_spin_lock_string
		:"=m" (lock->slock) : : "memory");
}

static inline void __raw_spin_lock_flags(raw_spinlock_t *lock, unsigned long flags)
{
	__asm__ __volatile__(
		__raw_spin_lock_string_flags
		:"=m" (lock->slock) : "r" (flags) : "memory");
}

static inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	char oldval;
	__asm__ __volatile__(
		"xchgb %b0,%1"
		:"=q" (oldval), "=m" (lock->slock)
		:"0" (0) : "memory");
	return oldval > 0;
}

/*
 * __raw_spin_unlock based on writing $1 to the low byte.
 * This method works. Despite all the confusion.
 * (except on PPro SMP or if we are using OOSTORE, so we use xchgb there)
 * (PPro errata 66, 92)
 */

#if !defined(CONFIG_X86_OOSTORE) && !defined(CONFIG_X86_PPRO_FENCE)

#define __raw_spin_unlock_string \
	"movb $1,%0" \
		:"=m" (lock->slock) : : "memory"


static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__asm__ __volatile__(
		__raw_spin_unlock_string
	);
}

#else

#define __raw_spin_unlock_string \
	"xchgb %b0, %1" \
		:"=q" (oldval), "=m" (lock->slock) \
		:"0" (oldval) : "memory"

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	char oldval = 1;

	__asm__ __volatile__(
		__raw_spin_unlock_string
	);
}

#endif

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

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define __raw_read_can_lock(x)		((int)(x)->lock > 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
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
	asm volatile("lock ; addl $" RW_LOCK_BIAS_STR ", %0"
				 : "=m" (rw->lock) : : "memory");
}

#endif /* __ASM_SPINLOCK_H */
