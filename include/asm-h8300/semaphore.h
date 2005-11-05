#ifndef _H8300_SEMAPHORE_H
#define _H8300_SEMAPHORE_H

#define RW_LOCK_BIAS		 0x01000000

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>

#include <asm/system.h>
#include <asm/atomic.h>

/*
 * Interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * H8/300 version by Yoshinori Sato
 */


struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.count		= ATOMIC_INIT(n),				\
	.sleepers	= 0,						\
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)	\
}

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

static inline void sema_init (struct semaphore *sem, int val)
{
	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER(*sem, val);
}

static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage int  __down_failed_trylock(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage int  __down_trylock(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);

extern spinlock_t semaphore_wake_lock;

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/m68k/lib/semaphore.S
 */
static inline void down(struct semaphore * sem)
{
	register atomic_t *count asm("er0");

	might_sleep();

	count = &(sem->count);
	__asm__ __volatile__(
		"stc ccr,r3l\n\t"
		"orc #0x80,ccr\n\t"
		"mov.l %2, er1\n\t"
		"dec.l #1,er1\n\t"
		"mov.l er1,%0\n\t"
		"bpl 1f\n\t"
		"ldc r3l,ccr\n\t"
		"mov.l %1,er0\n\t"
		"jsr @___down\n\t"
		"bra 2f\n"
		"1:\n\t"
		"ldc r3l,ccr\n"
		"2:"
		: "=m"(*count)
		: "g"(sem),"m"(*count)
		: "cc",  "er1", "er2", "er3");
}

static inline int down_interruptible(struct semaphore * sem)
{
	register atomic_t *count asm("er0");

	might_sleep();

	count = &(sem->count);
	__asm__ __volatile__(
		"stc ccr,r1l\n\t"
		"orc #0x80,ccr\n\t"
		"mov.l %3, er2\n\t"
		"dec.l #1,er2\n\t"
		"mov.l er2,%1\n\t"
		"bpl 1f\n\t"
		"ldc r1l,ccr\n\t"
		"mov.l %2,er0\n\t"
		"jsr @___down_interruptible\n\t"
		"bra 2f\n"
		"1:\n\t"
		"ldc r1l,ccr\n\t"
		"sub.l %0,%0\n\t"
		"2:\n\t"
		: "=r" (count),"=m" (*count)
		: "g"(sem),"m"(*count)
		: "cc", "er1", "er2", "er3");
	return (int)count;
}

static inline int down_trylock(struct semaphore * sem)
{
	register atomic_t *count asm("er0");

	count = &(sem->count);
	__asm__ __volatile__(
		"stc ccr,r3l\n\t"
		"orc #0x80,ccr\n\t"
		"mov.l %3,er2\n\t"
		"dec.l #1,er2\n\t"
		"mov.l er2,%0\n\t"
		"bpl 1f\n\t"
		"ldc r3l,ccr\n\t"
		"jmp @3f\n\t"
		LOCK_SECTION_START(".align 2\n\t")
		"3:\n\t"
		"mov.l %2,er0\n\t"
		"jsr @___down_trylock\n\t"
		"jmp @2f\n\t"
		LOCK_SECTION_END
		"1:\n\t"
		"ldc r3l,ccr\n\t"
		"sub.l %1,%1\n"
		"2:"
		: "=m" (*count),"=r"(count)
		: "g"(sem),"m"(*count)
		: "cc", "er1","er2", "er3");
	return (int)count;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static inline void up(struct semaphore * sem)
{
	register atomic_t *count asm("er0");

	count = &(sem->count);
	__asm__ __volatile__(
		"stc ccr,r3l\n\t"
		"orc #0x80,ccr\n\t"
		"mov.l %2,er1\n\t"
		"inc.l #1,er1\n\t"
		"mov.l er1,%0\n\t"
		"ldc r3l,ccr\n\t"
		"sub.l er2,er2\n\t"
		"cmp.l er2,er1\n\t"
		"bgt 1f\n\t"
		"mov.l %1,er0\n\t"
		"jsr @___up\n"
		"1:"
		: "=m"(*count)
		: "g"(sem),"m"(*count)
		: "cc", "er1", "er2", "er3");
}

#endif /* __ASSEMBLY__ */

#endif
