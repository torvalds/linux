/*
 * Copyright (c) 2008 Intel Corporation
 * Author: Matthew Wilcox <willy@linux.intel.com>
 *
 * Distributed under the terms of the GNU GPL, version 2
 *
 * Counting semaphores allow up to <n> tasks to acquire the semaphore
 * simultaneously.
 */
#ifndef __LINUX_SEMAPHORE_H
#define __LINUX_SEMAPHORE_H

#include <linux/list.h>
#include <linux/spinlock.h>

/*
 * The spinlock controls access to the other members of the semaphore.
 * 'count' represents how many more tasks can acquire this semaphore.
 * Tasks waiting for the lock are kept on the wait_list.
 */
struct semaphore {
	spinlock_t		lock;
	unsigned int		count;
	struct list_head	wait_list;
};

#define __SEMAPHORE_INITIALIZER(name, n)				\
{									\
	.lock		= __SPIN_LOCK_UNLOCKED((name).lock),		\
	.count		= n,						\
	.wait_list	= LIST_HEAD_INIT((name).wait_list),		\
}

#define __DECLARE_SEMAPHORE_GENERIC(name, count) \
	struct semaphore name = __SEMAPHORE_INITIALIZER(name, count)

#define DECLARE_MUTEX(name)	__DECLARE_SEMAPHORE_GENERIC(name, 1)

static inline void sema_init(struct semaphore *sem, int val)
{
	static struct lock_class_key __key;
	*sem = (struct semaphore) __SEMAPHORE_INITIALIZER(*sem, val);
	lockdep_init_map(&sem->lock.dep_map, "semaphore->lock", &__key, 0);
}

#define init_MUTEX(sem)		sema_init(sem, 1)
#define init_MUTEX_LOCKED(sem)	sema_init(sem, 0)

/*
 * Attempt to acquire the semaphore.  If another task is already holding the
 * semaphore, sleep until the semaphore is released.
 */
extern void down(struct semaphore *sem);

/*
 * As down(), except the sleep may be interrupted by a signal.  If it is,
 * this function will return -EINTR.
 */
extern int __must_check down_interruptible(struct semaphore *sem);

/*
 * As down_interruptible(), except the sleep may only be interrupted by
 * signals which are fatal to this process.
 */
extern int __must_check down_killable(struct semaphore *sem);

/*
 * As down(), except this function will not sleep.  It will return 0 if it
 * acquired the semaphore and 1 if the semaphore was contended.  This
 * function may be called from any context, including interrupt and softirq.
 */
extern int __must_check down_trylock(struct semaphore *sem);

/*
 * As down(), except this function will return -ETIME if it fails to
 * acquire the semaphore within the specified number of jiffies.
 */
extern int __must_check down_timeout(struct semaphore *sem, long jiffies);

/*
 * Release the semaphore.  Unlike mutexes, up() may be called from any
 * context and even by tasks which have never called down().
 */
extern void up(struct semaphore *sem);

#endif /* __LINUX_SEMAPHORE_H */
