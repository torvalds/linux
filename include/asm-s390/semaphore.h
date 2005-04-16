/*
 *  include/asm-s390/semaphore.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *
 *  Derived from "include/asm-i386/semaphore.h"
 *    (C) Copyright 1996 Linus Torvalds
 */

#ifndef _S390_SEMAPHORE_H
#define _S390_SEMAPHORE_H

#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

struct semaphore {
	/*
	 * Note that any negative value of count is equivalent to 0,
	 * but additionally indicates that some process(es) might be
	 * sleeping on `wait'.
	 */
	atomic_t count;
	wait_queue_head_t wait;
};

#define __SEMAPHORE_INITIALIZER(name,count) \
	{ ATOMIC_INIT(count), __WAIT_QUEUE_HEAD_INITIALIZER((name).wait) }

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INITIALIZER(name,1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

static inline void sema_init (struct semaphore *sem, int val)
{
	*sem = (struct semaphore) __SEMAPHORE_INITIALIZER((*sem),val);
}

static inline void init_MUTEX (struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED (struct semaphore *sem)
{
	sema_init(sem, 0);
}

asmlinkage void __down(struct semaphore * sem);
asmlinkage int  __down_interruptible(struct semaphore * sem);
asmlinkage int  __down_trylock(struct semaphore * sem);
asmlinkage void __up(struct semaphore * sem);

static inline void down(struct semaphore * sem)
{
	might_sleep();
	if (atomic_dec_return(&sem->count) < 0)
		__down(sem);
}

static inline int down_interruptible(struct semaphore * sem)
{
	int ret = 0;

	might_sleep();
	if (atomic_dec_return(&sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

static inline int down_trylock(struct semaphore * sem)
{
	int old_val, new_val;

	/*
	 * This inline assembly atomically implements the equivalent
	 * to the following C code:
	 *   old_val = sem->count.counter;
	 *   if ((new_val = old_val) > 0)
	 *       sem->count.counter = --new_val;
	 * In the ppc code this is called atomic_dec_if_positive.
	 */
	__asm__ __volatile__ (
		"   l    %0,0(%3)\n"
		"0: ltr  %1,%0\n"
		"   jle  1f\n"
		"   ahi  %1,-1\n"
		"   cs   %0,%1,0(%3)\n"
		"   jl   0b\n"
		"1:"
		: "=&d" (old_val), "=&d" (new_val), "=m" (sem->count.counter)
		: "a" (&sem->count.counter), "m" (sem->count.counter)
		: "cc", "memory" );
	return old_val <= 0;
}

static inline void up(struct semaphore * sem)
{
	if (atomic_inc_return(&sem->count) <= 0)
		__up(sem);
}

#endif
