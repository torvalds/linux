/*
 * linux/include/asm-xtensa/semaphore.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_SEMAPHORE_H
#define _XTENSA_SEMAPHORE_H

#include <asm/atomic.h>
#include <asm/system.h>
#include <linux/wait.h>
#include <linux/rwsem.h>

struct semaphore {
	atomic_t count;
	int sleepers;
	wait_queue_head_t wait;
#if WAITQUEUE_DEBUG
	long __magic;
#endif
};

#if WAITQUEUE_DEBUG
# define __SEM_DEBUG_INIT(name) \
		, (int)&(name).__magic
#else
# define __SEM_DEBUG_INIT(name)
#endif

#define __SEMAPHORE_INITIALIZER(name,count)			\
	{ ATOMIC_INIT(count), 					\
	  0,							\
	  __WAIT_QUEUE_HEAD_INITIALIZER((name).wait)		\
	__SEM_DEBUG_INIT(name) }

#define __MUTEX_INITIALIZER(name) \
	__SEMAPHORE_INITIALIZER(name, 1)

#define __DECLARE_SEMAPHORE_GENERIC(name,count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name,count)

#define DECLARE_MUTEX(name) __DECLARE_SEMAPHORE_GENERIC(name,1)
#define DECLARE_MUTEX_LOCKED(name) __DECLARE_SEMAPHORE_GENERIC(name,0)

extern inline void sema_init (struct semaphore *sem, int val)
{
/*
 *	*sem = (struct semaphore)__SEMAPHORE_INITIALIZER((*sem),val);
 *
 * i'd rather use the more flexible initialization above, but sadly
 * GCC 2.7.2.3 emits a bogus warning. EGCS doesnt. Oh well.
 */
	atomic_set(&sem->count, val);
	init_waitqueue_head(&sem->wait);
#if WAITQUEUE_DEBUG
	sem->__magic = (int)&sem->__magic;
#endif
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

extern spinlock_t semaphore_wake_lock;

extern __inline__ void down(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	if (atomic_sub_return(1, &sem->count) < 0)
		__down(sem);
}

extern __inline__ int down_interruptible(struct semaphore * sem)
{
	int ret = 0;
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	if (atomic_sub_return(1, &sem->count) < 0)
		ret = __down_interruptible(sem);
	return ret;
}

extern __inline__ int down_trylock(struct semaphore * sem)
{
	int ret = 0;
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif

	if (atomic_sub_return(1, &sem->count) < 0)
		ret = __down_trylock(sem);
	return ret;
}

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 */
extern __inline__ void up(struct semaphore * sem)
{
#if WAITQUEUE_DEBUG
	CHECK_MAGIC(sem->__magic);
#endif
	if (atomic_add_return(1, &sem->count) <= 0)
		__up(sem);
}

#endif /* _XTENSA_SEMAPHORE_H */
