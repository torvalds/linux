#ifndef _H8300_SEMAPHORE_HELPER_H
#define _H8300_SEMAPHORE_HELPER_H

/*
 * SMP- and interrupt-safe semaphores helper functions.
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * based on
 * m68k version by Andreas Schwab
 */

#include <linux/config.h>
#include <linux/errno.h>

/*
 * These two _must_ execute atomically wrt each other.
 */
static inline void wake_one_more(struct semaphore * sem)
{
	atomic_inc((atomic_t *)&sem->sleepers);
}

static inline int waking_non_zero(struct semaphore *sem)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	ret = 0;
	if (sem->sleepers > 0) {
		sem->sleepers--;
		ret = 1;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_interruptible:
 *	1	got the lock
 *	0	go to sleep
 *	-EINTR	interrupted
 */
static inline int waking_non_zero_interruptible(struct semaphore *sem,
						struct task_struct *tsk)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	ret = 0;
	if (sem->sleepers > 0) {
		sem->sleepers--;
		ret = 1;
	} else if (signal_pending(tsk)) {
		atomic_inc(&sem->count);
		ret = -EINTR;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

/*
 * waking_non_zero_trylock:
 *	1	failed to lock
 *	0	got the lock
 */
static inline int waking_non_zero_trylock(struct semaphore *sem)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&semaphore_wake_lock, flags);
	ret = 1;
	if (sem->sleepers <= 0)
		atomic_inc(&sem->count);
	else {
		sem->sleepers--;
		ret = 0;
	}
	spin_unlock_irqrestore(&semaphore_wake_lock, flags);
	return ret;
}

#endif
