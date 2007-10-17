#ifndef _ALPHA_SEMAPHORE_H
#define _ALPHA_SEMAPHORE_H

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 * (C) Copyright 1996, 2000 Richard Henderson
 */

#include <asm/current.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/compiler.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

struct semaphore {
	atomic_t count;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INITIALIZER(name, n)			\
{								\
	.count	= ATOMIC_INIT(n),				\
  	.wait	= __WAIT_QUEUE_HEAD_INITIALIZER((name).wait),	\
}

#define __DECLARE_SEMAPHORE_GENERIC(name,count)		\
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name)		__DECLARE_SEMAPHORE_GENERIC(name,1)

static inline void sema_init(struct semaphore *sem, int val)
{
	/*
	 * Logically, 
	 *   *sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
	 * except that gcc produces better initializing by parts yet.
	 */

	atomic_set(&sem->count, val);
	init_waitqueue_head(&sem->wait);
}

static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}

extern void down(struct semaphore *);
extern void __down_failed(struct semaphore *);
extern int  down_interruptible(struct semaphore *);
extern int  __down_failed_interruptible(struct semaphore *);
extern int  down_trylock(struct semaphore *);
extern void up(struct semaphore *);
extern void __up_wakeup(struct semaphore *);

/*
 * Hidden out of line code is fun, but extremely messy.  Rely on newer
 * compilers to do a respectable job with this.  The contention cases
 * are handled out of line in arch/alpha/kernel/semaphore.c.
 */

static inline void __down(struct semaphore *sem)
{
	long count;
	might_sleep();
	count = atomic_dec_return(&sem->count);
	if (unlikely(count < 0))
		__down_failed(sem);
}

static inline int __down_interruptible(struct semaphore *sem)
{
	long count;
	might_sleep();
	count = atomic_dec_return(&sem->count);
	if (unlikely(count < 0))
		return __down_failed_interruptible(sem);
	return 0;
}

/*
 * down_trylock returns 0 on success, 1 if we failed to get the lock.
 */

static inline int __down_trylock(struct semaphore *sem)
{
	long ret;

	/* "Equivalent" C:

	   do {
		ret = ldl_l;
		--ret;
		if (ret < 0)
			break;
		ret = stl_c = ret;
	   } while (ret == 0);
	*/
	__asm__ __volatile__(
		"1:	ldl_l	%0,%1\n"
		"	subl	%0,1,%0\n"
		"	blt	%0,2f\n"
		"	stl_c	%0,%1\n"
		"	beq	%0,3f\n"
		"	mb\n"
		"2:\n"
		".subsection 2\n"
		"3:	br	1b\n"
		".previous"
		: "=&r" (ret), "=m" (sem->count)
		: "m" (sem->count));

	return ret < 0;
}

static inline void __up(struct semaphore *sem)
{
	if (unlikely(atomic_inc_return(&sem->count) <= 0))
		__up_wakeup(sem);
}

#if !defined(CONFIG_DEBUG_SEMAPHORE)
extern inline void down(struct semaphore *sem)
{
	__down(sem);
}
extern inline int down_interruptible(struct semaphore *sem)
{
	return __down_interruptible(sem);
}
extern inline int down_trylock(struct semaphore *sem)
{
	return __down_trylock(sem);
}
extern inline void up(struct semaphore *sem)
{
	__up(sem);
}
#endif

#endif
