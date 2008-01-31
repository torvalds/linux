#ifndef _X86_SPINLOCK_H_
#define _X86_SPINLOCK_H_

#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <linux/compiler.h>

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 *
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * These are fair FIFO ticket locks, which are currently limited to 256
 * CPUs.
 *
 * (the type definitions are in asm/spinlock_types.h)
 */

#ifdef CONFIG_X86_32
typedef char _slock_t;
# define LOCK_INS_DEC "decb"
# define LOCK_INS_XCH "xchgb"
# define LOCK_INS_MOV "movb"
# define LOCK_INS_CMP "cmpb"
# define LOCK_PTR_REG "a"
#else
typedef int _slock_t;
# define LOCK_INS_DEC "decl"
# define LOCK_INS_XCH "xchgl"
# define LOCK_INS_MOV "movl"
# define LOCK_INS_CMP "cmpl"
# define LOCK_PTR_REG "D"
#endif

#if defined(CONFIG_X86_32) && \
	(defined(CONFIG_X86_OOSTORE) || defined(CONFIG_X86_PPRO_FENCE))
/*
 * On PPro SMP or if we are using OOSTORE, we use a locked operation to unlock
 * (PPro errata 66, 92)
 */
# define UNLOCK_LOCK_PREFIX LOCK_PREFIX
#else
# define UNLOCK_LOCK_PREFIX
#endif

/*
 * Ticket locks are conceptually two parts, one indicating the current head of
 * the queue, and the other indicating the current tail. The lock is acquired
 * by atomically noting the tail and incrementing it by one (thus adding
 * ourself to the queue and noting our position), then waiting until the head
 * becomes equal to the the initial value of the tail.
 *
 * We use an xadd covering *both* parts of the lock, to increment the tail and
 * also load the position of the head, which takes care of memory ordering
 * issues and should be optimal for the uncontended case. Note the tail must be
 * in the high part, because a wide xadd increment of the low part would carry
 * up and contaminate the high part.
 *
 * With fewer than 2^8 possible CPUs, we can use x86's partial registers to
 * save some instructions and make the code more elegant. There really isn't
 * much between them in performance though, especially as locks are out of line.
 */
#if (NR_CPUS < 256)
static inline int __raw_spin_is_locked(raw_spinlock_t *lock)
{
	int tmp = *(volatile signed int *)(&(lock)->slock);

	return (((tmp >> 8) & 0xff) != (tmp & 0xff));
}

static inline int __raw_spin_is_contended(raw_spinlock_t *lock)
{
	int tmp = *(volatile signed int *)(&(lock)->slock);

	return (((tmp >> 8) & 0xff) - (tmp & 0xff)) > 1;
}

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	short inc = 0x0100;

	__asm__ __volatile__ (
		LOCK_PREFIX "xaddw %w0, %1\n"
		"1:\t"
		"cmpb %h0, %b0\n\t"
		"je 2f\n\t"
		"rep ; nop\n\t"
		"movb %1, %b0\n\t"
		/* don't need lfence here, because loads are in-order */
		"jmp 1b\n"
		"2:"
		:"+Q" (inc), "+m" (lock->slock)
		:
		:"memory", "cc");
}

#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)

static inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	int tmp;
	short new;

	asm volatile(
		"movw %2,%w0\n\t"
		"cmpb %h0,%b0\n\t"
		"jne 1f\n\t"
		"movw %w0,%w1\n\t"
		"incb %h1\n\t"
		"lock ; cmpxchgw %w1,%2\n\t"
		"1:"
		"sete %b1\n\t"
		"movzbl %b1,%0\n\t"
		:"=&a" (tmp), "=Q" (new), "+m" (lock->slock)
		:
		: "memory", "cc");

	return tmp;
}

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__asm__ __volatile__(
		UNLOCK_LOCK_PREFIX "incb %0"
		:"+m" (lock->slock)
		:
		:"memory", "cc");
}
#else
static inline int __raw_spin_is_locked(raw_spinlock_t *lock)
{
	int tmp = *(volatile signed int *)(&(lock)->slock);

	return (((tmp >> 16) & 0xffff) != (tmp & 0xffff));
}

static inline int __raw_spin_is_contended(raw_spinlock_t *lock)
{
	int tmp = *(volatile signed int *)(&(lock)->slock);

	return (((tmp >> 16) & 0xffff) - (tmp & 0xffff)) > 1;
}

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	int inc = 0x00010000;
	int tmp;

	__asm__ __volatile__ (
		"lock ; xaddl %0, %1\n"
		"movzwl %w0, %2\n\t"
		"shrl $16, %0\n\t"
		"1:\t"
		"cmpl %0, %2\n\t"
		"je 2f\n\t"
		"rep ; nop\n\t"
		"movzwl %1, %2\n\t"
		/* don't need lfence here, because loads are in-order */
		"jmp 1b\n"
		"2:"
		:"+Q" (inc), "+m" (lock->slock), "=r" (tmp)
		:
		:"memory", "cc");
}

#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)

static inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	int tmp;
	int new;

	asm volatile(
		"movl %2,%0\n\t"
		"movl %0,%1\n\t"
		"roll $16, %0\n\t"
		"cmpl %0,%1\n\t"
		"jne 1f\n\t"
		"addl $0x00010000, %1\n\t"
		"lock ; cmpxchgl %1,%2\n\t"
		"1:"
		"sete %b1\n\t"
		"movzbl %b1,%0\n\t"
		:"=&a" (tmp), "=r" (new), "+m" (lock->slock)
		:
		: "memory", "cc");

	return tmp;
}

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	__asm__ __volatile__(
		UNLOCK_LOCK_PREFIX "incw %0"
		:"+m" (lock->slock)
		:
		:"memory", "cc");
}
#endif

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

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
static inline int __raw_read_can_lock(raw_rwlock_t *lock)
{
	return (int)(lock)->lock > 0;
}

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
static inline int __raw_write_can_lock(raw_rwlock_t *lock)
{
	return (lock)->lock == RW_LOCK_BIAS;
}

static inline void __raw_read_lock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX " subl $1,(%0)\n\t"
		     "jns 1f\n"
		     "call __read_lock_failed\n\t"
		     "1:\n"
		     ::LOCK_PTR_REG (rw) : "memory");
}

static inline void __raw_write_lock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX " subl %1,(%0)\n\t"
		     "jz 1f\n"
		     "call __write_lock_failed\n\t"
		     "1:\n"
		     ::LOCK_PTR_REG (rw), "i" (RW_LOCK_BIAS) : "memory");
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
	asm volatile(LOCK_PREFIX "incl %0" :"+m" (rw->lock) : : "memory");
}

static inline void __raw_write_unlock(raw_rwlock_t *rw)
{
	asm volatile(LOCK_PREFIX "addl %1, %0"
		     : "+m" (rw->lock) : "i" (RW_LOCK_BIAS) : "memory");
}

#define _raw_spin_relax(lock)	cpu_relax()
#define _raw_read_relax(lock)	cpu_relax()
#define _raw_write_relax(lock)	cpu_relax()

#endif
