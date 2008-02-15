/* MN10300 Semaphores
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_SEMAPHORE_H
#define _ASM_SEMAPHORE_H

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>

#define SEMAPHORE_DEBUG		0

/*
 * the semaphore definition
 * - if count is >0 then there are tokens available on the semaphore for down
 *   to collect
 * - if count is <=0 then there are no spare tokens, and anyone that wants one
 *   must wait
 * - if wait_list is not empty, then there are processes waiting for the
 *   semaphore
 */
struct semaphore {
	atomic_t		count;		/* it's not really atomic, it's
						 * just that certain modules
						 * expect to be able to access
						 * it directly */
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#if SEMAPHORE_DEBUG
	unsigned		__magic;
#endif
};

#if SEMAPHORE_DEBUG
# define __SEM_DEBUG_INIT(name) , (long)&(name).__magic
#else
# define __SEM_DEBUG_INIT(name)
#endif


#define __SEMAPHORE_INITIALIZER(name, init_count)			\
{									\
	.count		= ATOMIC_INIT(init_count),			\
	.wait_lock	= __SPIN_LOCK_UNLOCKED((name).wait_lock),	\
	.wait_list	= LIST_HEAD_INIT((name).wait_list)		\
	__SEM_DEBUG_INIT(name)						\
}

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name, 1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name, 0)

static inline void sema_init(struct semaphore *sem, int val)
{
	*sem = (struct semaphore) __SEMAPHORE_INITIALIZER(*sem, val);
}

static inline void init_MUTEX(struct semaphore *sem)
{
	sema_init(sem, 1);
}

static inline void init_MUTEX_LOCKED(struct semaphore *sem)
{
	sema_init(sem, 0);
}

extern void __down(struct semaphore *sem, unsigned long flags);
extern int  __down_interruptible(struct semaphore *sem, unsigned long flags);
extern void __up(struct semaphore *sem);

static inline void down(struct semaphore *sem)
{
	unsigned long flags;
	int count;

#if SEMAPHORE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	spin_lock_irqsave(&sem->wait_lock, flags);
	count = atomic_read(&sem->count);
	if (likely(count > 0)) {
		atomic_set(&sem->count, count - 1);
		spin_unlock_irqrestore(&sem->wait_lock, flags);
	} else {
		__down(sem, flags);
	}
}

static inline int down_interruptible(struct semaphore *sem)
{
	unsigned long flags;
	int count, ret = 0;

#if SEMAPHORE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	spin_lock_irqsave(&sem->wait_lock, flags);
	count = atomic_read(&sem->count);
	if (likely(count > 0)) {
		atomic_set(&sem->count, count - 1);
		spin_unlock_irqrestore(&sem->wait_lock, flags);
	} else {
		ret = __down_interruptible(sem, flags);
	}
	return ret;
}

/*
 * non-blockingly attempt to down() a semaphore.
 * - returns zero if we acquired it
 */
static inline int down_trylock(struct semaphore *sem)
{
	unsigned long flags;
	int count, success = 0;

#if SEMAPHORE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	spin_lock_irqsave(&sem->wait_lock, flags);
	count = atomic_read(&sem->count);
	if (likely(count > 0)) {
		atomic_set(&sem->count, count - 1);
		success = 1;
	}
	spin_unlock_irqrestore(&sem->wait_lock, flags);
	return !success;
}

static inline void up(struct semaphore *sem)
{
	unsigned long flags;

#if SEMAPHORE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	spin_lock_irqsave(&sem->wait_lock, flags);
	if (!list_empty(&sem->wait_list))
		__up(sem);
	else
		atomic_set(&sem->count, atomic_read(&sem->count) + 1);
	spin_unlock_irqrestore(&sem->wait_lock, flags);
}

static inline int sem_getcount(struct semaphore *sem)
{
	return atomic_read(&sem->count);
}

#endif /* __ASSEMBLY__ */

#endif
