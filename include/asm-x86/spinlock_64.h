#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <asm/page.h>
#include <asm/processor.h>

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

static inline int __raw_spin_is_locked(raw_spinlock_t *lock)
{
	return *(volatile signed int *)(&(lock)->slock) <= 0;
}

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	asm volatile(
		"\n1:\t"
		LOCK_PREFIX " ; decl %0\n\t"
		"jns 2f\n"
		"3:\n"
		"rep;nop\n\t"
		"cmpl $0,%0\n\t"
		"jle 3b\n\t"
		"jmp 1b\n"
		"2:\t" : "=m" (lock->slock) : : "memory");
}

/*
 * Same as __raw_spin_lock, but reenable interrupts during spinning.
 */
#ifndef CONFIG_PROVE_LOCKING
static inline void __raw_spin_lock_flags(raw_spinlock_t *lock, unsigned long flags)
{
	asm volatile(
		"\n1:\t"
		LOCK_PREFIX " ; decl %0\n\t"
		"jns 5f\n"
		"testl $0x200, %1\n\t"	/* interrupts were disabled? */
		"jz 4f\n\t"
	        "sti\n"
		"3:\t"
		"rep;nop\n\t"
		"cmpl $0, %0\n\t"
		"jle 3b\n\t"
		"cli\n\t"
		"jmp 1b\n"
		"4:\t"
		"rep;nop\n\t"
		"cmpl $0, %0\n\t"
		"jg 1b\n\t"
		"jmp 4b\n"
		"5:\n\t"
		: "+m" (lock->slock) : "r" ((unsigned)flags) : "memory");
}
#endif

static inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	int oldval;

	asm volatile(
		"xchgl %0,%1"
		:"=q" (oldval), "=m" (lock->slock)
		:"0" (0) : "memory");

	return oldval > 0;
}

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	asm volatile("movl $1,%0" :"=m" (lock->slock) :: "memory");
}

static inline void __raw_spin_unlock_wait(raw_spinlock_t *lock)
{
	while (__raw_spin_is_locked(lock))
		cpu_relax();
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
 *
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 */

static inline int __raw_read_can_lock(raw_rwlock_t *lock)
{
	return (int)(lock)->lock > 0;
}

static inline int __raw_write_can_lock(raw_rwlock_t *lock)
{
	return (lock)->lock == RW_LOCK_BIAS;
}

static inline void __raw_read_lock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX "subl $1,(%0)\n\t"
		     "jns 1f\n"
		     "call __read_lock_failed\n"
		     "1:\n"
		     ::"D" (rw), "i" (RW_LOCK_BIAS) : "memory");
}

static inline void __raw_write_lock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX "subl %1,(%0)\n\t"
		     "jz 1f\n"
		     "\tcall __write_lock_failed\n\t"
		     "1:\n"
		     ::"D" (rw), "i" (RW_LOCK_BIAS) : "memory");
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
	asm volatile(LOCK_PREFIX " ; incl %0" :"=m" (rw->lock) : : "memory");
}

static inline void __raw_write_unlock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX " ; addl $" RW_LOCK_BIAS_STR ",%0"
				: "=m" (rw->lock) : : "memory");
}

#define _raw_spin_relax(lock)	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif /* __ASM_SPINLOCK_H */
