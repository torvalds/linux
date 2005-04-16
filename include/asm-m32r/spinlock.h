#ifndef _ASM_M32R_SPINLOCK_H
#define _ASM_M32R_SPINLOCK_H

/*
 *  linux/include/asm-m32r/spinlock.h
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/config.h>	/* CONFIG_DEBUG_SPINLOCK, CONFIG_SMP */
#include <linux/compiler.h>
#include <asm/atomic.h>
#include <asm/page.h>

extern int printk(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

#define RW_LOCK_BIAS		 0x01000000
#define RW_LOCK_BIAS_STR	"0x01000000"

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

typedef struct {
	volatile int slock;
#ifdef CONFIG_DEBUG_SPINLOCK
	unsigned magic;
#endif
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} spinlock_t;

#define SPINLOCK_MAGIC	0xdead4ead

#ifdef CONFIG_DEBUG_SPINLOCK
#define SPINLOCK_MAGIC_INIT	, SPINLOCK_MAGIC
#else
#define SPINLOCK_MAGIC_INIT	/* */
#endif

#define SPIN_LOCK_UNLOCKED (spinlock_t) { 1 SPINLOCK_MAGIC_INIT }

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define spin_is_locked(x)	(*(volatile int *)(&(x)->slock) <= 0)
#define spin_unlock_wait(x)	do { barrier(); } while(spin_is_locked(x))
#define _raw_spin_lock_flags(lock, flags) _raw_spin_lock(lock)

/**
 * _raw_spin_trylock - Try spin lock and return a result
 * @lock: Pointer to the lock variable
 *
 * _raw_spin_trylock() tries to get the lock and returns a result.
 * On the m32r, the result value is 1 (= Success) or 0 (= Failure).
 */
static inline int _raw_spin_trylock(spinlock_t *lock)
{
	int oldval;
	unsigned long tmp1, tmp2;

	/*
	 * lock->slock :  =1 : unlock
	 *             : <=0 : lock
	 * {
	 *   oldval = lock->slock; <--+ need atomic operation
	 *   lock->slock = 0;      <--+
	 * }
	 */
	__asm__ __volatile__ (
		"# spin_trylock			\n\t"
		"ldi	%1, #0;			\n\t"
		"mvfc	%2, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%3")
		"lock	%0, @%3;		\n\t"
		"unlock	%1, @%3;		\n\t"
		"mvtc	%2, psw;		\n\t"
		: "=&r" (oldval), "=&r" (tmp1), "=&r" (tmp2)
		: "r" (&lock->slock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);

	return (oldval > 0);
}

static inline void _raw_spin_lock(spinlock_t *lock)
{
	unsigned long tmp0, tmp1;

#ifdef CONFIG_DEBUG_SPINLOCK
	if (unlikely(lock->magic != SPINLOCK_MAGIC)) {
		printk("pc: %p\n", __builtin_return_address(0));
		BUG();
	}
#endif
	/*
	 * lock->slock :  =1 : unlock
	 *             : <=0 : lock
	 *
	 * for ( ; ; ) {
	 *   lock->slock -= 1;  <-- need atomic operation
	 *   if (lock->slock == 0) break;
	 *   for ( ; lock->slock <= 0 ; );
	 * }
	 */
	__asm__ __volatile__ (
		"# spin_lock			\n\t"
		".fillinsn			\n"
		"1:				\n\t"
		"mvfc	%1, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%2")
		"lock	%0, @%2;		\n\t"
		"addi	%0, #-1;		\n\t"
		"unlock	%0, @%2;		\n\t"
		"mvtc	%1, psw;		\n\t"
		"bltz	%0, 2f;			\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn			\n"
		"2:				\n\t"
		"ld	%0, @%2;		\n\t"
		"bgtz	%0, 1b;			\n\t"
		"bra	2b;			\n\t"
		LOCK_SECTION_END
		: "=&r" (tmp0), "=&r" (tmp1)
		: "r" (&lock->slock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline void _raw_spin_unlock(spinlock_t *lock)
{
#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(lock->magic != SPINLOCK_MAGIC);
	BUG_ON(!spin_is_locked(lock));
#endif
	mb();
	lock->slock = 1;
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
typedef struct {
	volatile int lock;
#ifdef CONFIG_DEBUG_SPINLOCK
	unsigned magic;
#endif
#ifdef CONFIG_PREEMPT
	unsigned int break_lock;
#endif
} rwlock_t;

#define RWLOCK_MAGIC	0xdeaf1eed

#ifdef CONFIG_DEBUG_SPINLOCK
#define RWLOCK_MAGIC_INIT	, RWLOCK_MAGIC
#else
#define RWLOCK_MAGIC_INIT	/* */
#endif

#define RW_LOCK_UNLOCKED (rwlock_t) { RW_LOCK_BIAS RWLOCK_MAGIC_INIT }

#define rwlock_init(x)	do { *(x) = RW_LOCK_UNLOCKED; } while(0)

/**
 * read_can_lock - would read_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define read_can_lock(x) ((int)(x)->lock > 0)

/**
 * write_can_lock - would write_trylock() succeed?
 * @lock: the rwlock in question.
 */
#define write_can_lock(x) ((x)->lock == RW_LOCK_BIAS)

/*
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 *
 * The inline assembly is non-obvious. Think about it.
 *
 * Changed to use the same technique as rw semaphores.  See
 * semaphore.h for details.  -ben
 */
/* the spinlock helpers are in arch/i386/kernel/semaphore.c */

static inline void _raw_read_lock(rwlock_t *rw)
{
	unsigned long tmp0, tmp1;

#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(rw->magic != RWLOCK_MAGIC);
#endif
	/*
	 * rw->lock :  >0 : unlock
	 *          : <=0 : lock
	 *
	 * for ( ; ; ) {
	 *   rw->lock -= 1;  <-- need atomic operation
	 *   if (rw->lock >= 0) break;
	 *   rw->lock += 1;  <-- need atomic operation
	 *   for ( ; rw->lock <= 0 ; );
	 * }
	 */
	__asm__ __volatile__ (
		"# read_lock			\n\t"
		".fillinsn			\n"
		"1:				\n\t"
		"mvfc	%1, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%2")
		"lock	%0, @%2;		\n\t"
		"addi	%0, #-1;		\n\t"
		"unlock	%0, @%2;		\n\t"
		"mvtc	%1, psw;		\n\t"
		"bltz	%0, 2f;			\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn			\n"
		"2:				\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%2")
		"lock	%0, @%2;		\n\t"
		"addi	%0, #1;			\n\t"
		"unlock	%0, @%2;		\n\t"
		"mvtc	%1, psw;		\n\t"
		".fillinsn			\n"
		"3:				\n\t"
		"ld	%0, @%2;		\n\t"
		"bgtz	%0, 1b;			\n\t"
		"bra	3b;			\n\t"
		LOCK_SECTION_END
		: "=&r" (tmp0), "=&r" (tmp1)
		: "r" (&rw->lock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline void _raw_write_lock(rwlock_t *rw)
{
	unsigned long tmp0, tmp1, tmp2;

#ifdef CONFIG_DEBUG_SPINLOCK
	BUG_ON(rw->magic != RWLOCK_MAGIC);
#endif
	/*
	 * rw->lock :  =RW_LOCK_BIAS_STR : unlock
	 *          : !=RW_LOCK_BIAS_STR : lock
	 *
	 * for ( ; ; ) {
	 *   rw->lock -= RW_LOCK_BIAS_STR;  <-- need atomic operation
	 *   if (rw->lock == 0) break;
	 *   rw->lock += RW_LOCK_BIAS_STR;  <-- need atomic operation
	 *   for ( ; rw->lock != RW_LOCK_BIAS_STR ; ) ;
	 * }
	 */
	__asm__ __volatile__ (
		"# write_lock					\n\t"
		"seth	%1, #high(" RW_LOCK_BIAS_STR ");	\n\t"
		"or3	%1, %1, #low(" RW_LOCK_BIAS_STR ");	\n\t"
		".fillinsn					\n"
		"1:						\n\t"
		"mvfc	%2, psw;				\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("%0", "r7", "%3")
		"lock	%0, @%3;				\n\t"
		"sub	%0, %1;					\n\t"
		"unlock	%0, @%3;				\n\t"
		"mvtc	%2, psw;				\n\t"
		"bnez	%0, 2f;					\n\t"
		LOCK_SECTION_START(".balign 4 \n\t")
		".fillinsn					\n"
		"2:						\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("%0", "r7", "%3")
		"lock	%0, @%3;				\n\t"
		"add	%0, %1;					\n\t"
		"unlock	%0, @%3;				\n\t"
		"mvtc	%2, psw;				\n\t"
		".fillinsn					\n"
		"3:						\n\t"
		"ld	%0, @%3;				\n\t"
		"beq	%0, %1, 1b;				\n\t"
		"bra	3b;					\n\t"
		LOCK_SECTION_END
		: "=&r" (tmp0), "=&r" (tmp1), "=&r" (tmp2)
		: "r" (&rw->lock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r7"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline void _raw_read_unlock(rwlock_t *rw)
{
	unsigned long tmp0, tmp1;

	__asm__ __volatile__ (
		"# read_unlock			\n\t"
		"mvfc	%1, psw;		\n\t"
		"clrpsw	#0x40 -> nop;		\n\t"
		DCACHE_CLEAR("%0", "r6", "%2")
		"lock	%0, @%2;		\n\t"
		"addi	%0, #1;			\n\t"
		"unlock	%0, @%2;		\n\t"
		"mvtc	%1, psw;		\n\t"
		: "=&r" (tmp0), "=&r" (tmp1)
		: "r" (&rw->lock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

static inline void _raw_write_unlock(rwlock_t *rw)
{
	unsigned long tmp0, tmp1, tmp2;

	__asm__ __volatile__ (
		"# write_unlock					\n\t"
		"seth	%1, #high(" RW_LOCK_BIAS_STR ");	\n\t"
		"or3	%1, %1, #low(" RW_LOCK_BIAS_STR ");	\n\t"
		"mvfc	%2, psw;				\n\t"
		"clrpsw	#0x40 -> nop;				\n\t"
		DCACHE_CLEAR("%0", "r7", "%3")
		"lock	%0, @%3;				\n\t"
		"add	%0, %1;					\n\t"
		"unlock	%0, @%3;				\n\t"
		"mvtc	%2, psw;				\n\t"
		: "=&r" (tmp0), "=&r" (tmp1), "=&r" (tmp2)
		: "r" (&rw->lock)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r7"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
}

#define _raw_read_trylock(lock) generic_raw_read_trylock(lock)

static inline int _raw_write_trylock(rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

#endif	/* _ASM_M32R_SPINLOCK_H */
