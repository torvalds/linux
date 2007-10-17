#ifndef _BFIN_SEMAPHORE_H
#define _BFIN_SEMAPHORE_H

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <asm/atomic.h>

/*
 * Interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * BFIN version by akbar hussain Lineo Inc  April 2001
 *
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

static inline void sema_init(struct semaphore *sem, int val)
{
	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER(*sem, val);
}

static inline void init_MUTEX(struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED(struct semaphore *sem)
{
	sema_init(sem, 0);
}

asmlinkage void __down(struct semaphore *sem);
asmlinkage int __down_interruptible(struct semaphore *sem);
asmlinkage int __down_trylock(struct semaphore *sem);
asmlinkage void __up(struct semaphore *sem);

extern spinlock_t semaphore_wake_lock;

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits.
 */
static inline void down(struct semaphore *sem)
{
	might_sleep();
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

static inline int down_interruptible(struct semaphore *sem)
{
	int ret = 0;

	might_sleep();
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return (ret);
}

static inline int down_trylock(struct semaphore *sem)
{
	int ret = 0;

	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_trylock(sem);
	return ret;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
static inline void up(struct semaphore *sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}

#endif				/* __ASSEMBLY__ */
#endif				/* _BFIN_SEMAPHORE_H */
