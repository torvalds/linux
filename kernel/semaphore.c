/*
 * Copyright (c) 2008 Intel Corporation
 * Author: Matthew Wilcox <willy@linux.intel.com>
 *
 * Distributed under the terms of the GNU GPL, version 2
 */

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

/*
 * Some notes on the implementation:
 *
 * down_trylock() and up() can be called from interrupt context.
 * So we have to disable interrupts when taking the lock.
 *
 * The ->count variable, if positive, defines how many more tasks can
 * acquire the semaphore.  If negative, it represents how many tasks are
 * waiting on the semaphore (*).  If zero, no tasks are waiting, and no more
 * tasks can acquire the semaphore.
 *
 * (*) Except for the window between one task calling up() and the task
 * sleeping in a __down_common() waking up.  In order to avoid a third task
 * coming in and stealing the second task's wakeup, we leave the ->count
 * negative.  If we have a more complex situation, the ->count may become
 * zero or negative (eg a semaphore with count = 2, three tasks attempt to
 * acquire it, one sleeps, two finish and call up(), the second task to call
 * up() notices that the list is empty and just increments count).
 */

static noinline void __down(struct semaphore *sem);
static noinline int __down_interruptible(struct semaphore *sem);
static noinline int __down_killable(struct semaphore *sem);
static noinline int __down_timeout(struct semaphore *sem, long jiffies);
static noinline void __up(struct semaphore *sem);

void down(struct semaphore *sem)
{
	unsigned long flags;

	spin_lock_irqsave(&sem->lock, flags);
	if (unlikely(sem->count-- <= 0))
		__down(sem);
	spin_unlock_irqrestore(&sem->lock, flags);
}
EXPORT_SYMBOL(down);

int down_interruptible(struct semaphore *sem)
{
	unsigned long flags;
	int result = 0;

	spin_lock_irqsave(&sem->lock, flags);
	if (unlikely(sem->count-- <= 0))
		result = __down_interruptible(sem);
	spin_unlock_irqrestore(&sem->lock, flags);

	return result;
}
EXPORT_SYMBOL(down_interruptible);

int down_killable(struct semaphore *sem)
{
	unsigned long flags;
	int result = 0;

	spin_lock_irqsave(&sem->lock, flags);
	if (unlikely(sem->count-- <= 0))
		result = __down_killable(sem);
	spin_unlock_irqrestore(&sem->lock, flags);

	return result;
}
EXPORT_SYMBOL(down_killable);

/**
 * down_trylock - try to acquire the semaphore, without waiting
 * @sem: the semaphore to be acquired
 *
 * Try to acquire the semaphore atomically.  Returns 0 if the mutex has
 * been acquired successfully and 1 if it is contended.
 *
 * NOTE: This return value is inverted from both spin_trylock and
 * mutex_trylock!  Be careful about this when converting code.
 *
 * Unlike mutex_trylock, this function can be used from interrupt context,
 * and the semaphore can be released by any task or interrupt.
 */
int down_trylock(struct semaphore *sem)
{
	unsigned long flags;
	int count;

	spin_lock_irqsave(&sem->lock, flags);
	count = sem->count - 1;
	if (likely(count >= 0))
		sem->count = count;
	spin_unlock_irqrestore(&sem->lock, flags);

	return (count < 0);
}
EXPORT_SYMBOL(down_trylock);

int down_timeout(struct semaphore *sem, long jiffies)
{
	unsigned long flags;
	int result = 0;

	spin_lock_irqsave(&sem->lock, flags);
	if (unlikely(sem->count-- <= 0))
		result = __down_timeout(sem, jiffies);
	spin_unlock_irqrestore(&sem->lock, flags);

	return result;
}
EXPORT_SYMBOL(down_timeout);

void up(struct semaphore *sem)
{
	unsigned long flags;

	spin_lock_irqsave(&sem->lock, flags);
	if (likely(sem->count >= 0))
		sem->count++;
	else
		__up(sem);
	spin_unlock_irqrestore(&sem->lock, flags);
}
EXPORT_SYMBOL(up);

/* Functions for the contended case */

struct semaphore_waiter {
	struct list_head list;
	struct task_struct *task;
	int up;
};

/*
 * Wake up a process waiting on a semaphore.  We need to call this from both
 * __up and __down_common as it's possible to race a task into the semaphore
 * if it comes in at just the right time between two tasks calling up() and
 * a third task waking up.  This function assumes the wait_list is already
 * checked for being non-empty.
 */
static noinline void __sched __up_down_common(struct semaphore *sem)
{
	struct semaphore_waiter *waiter = list_first_entry(&sem->wait_list,
						struct semaphore_waiter, list);
	list_del(&waiter->list);
	waiter->up = 1;
	wake_up_process(waiter->task);
}

/*
 * Because this function is inlined, the 'state' parameter will be
 * constant, and thus optimised away by the compiler.  Likewise the
 * 'timeout' parameter for the cases without timeouts.
 */
static inline int __sched __down_common(struct semaphore *sem, long state,
								long timeout)
{
	int result = 0;
	struct task_struct *task = current;
	struct semaphore_waiter waiter;

	list_add_tail(&waiter.list, &sem->wait_list);
	waiter.task = task;
	waiter.up = 0;

	for (;;) {
		if (state == TASK_INTERRUPTIBLE && signal_pending(task))
			goto interrupted;
		if (state == TASK_KILLABLE && fatal_signal_pending(task))
			goto interrupted;
		if (timeout <= 0)
			goto timed_out;
		__set_task_state(task, state);
		spin_unlock_irq(&sem->lock);
		timeout = schedule_timeout(timeout);
		spin_lock_irq(&sem->lock);
		if (waiter.up)
			goto woken;
	}

 timed_out:
	list_del(&waiter.list);
	result = -ETIME;
	goto woken;
 interrupted:
	list_del(&waiter.list);
	result = -EINTR;
 woken:
	/*
	 * Account for the process which woke us up.  For the case where
	 * we're interrupted, we need to increment the count on our own
	 * behalf.  I don't believe we can hit the case where the
	 * sem->count hits zero, *and* there's a second task sleeping,
	 * but it doesn't hurt, that's not a commonly exercised path and
	 * it's not a performance path either.
	 */
	if (unlikely((++sem->count >= 0) && !list_empty(&sem->wait_list)))
		__up_down_common(sem);
	return result;
}

static noinline void __sched __down(struct semaphore *sem)
{
	__down_common(sem, TASK_UNINTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}

static noinline int __sched __down_interruptible(struct semaphore *sem)
{
	return __down_common(sem, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}

static noinline int __sched __down_killable(struct semaphore *sem)
{
	return __down_common(sem, TASK_KILLABLE, MAX_SCHEDULE_TIMEOUT);
}

static noinline int __sched __down_timeout(struct semaphore *sem, long jiffies)
{
	return __down_common(sem, TASK_UNINTERRUPTIBLE, jiffies);
}

static noinline void __sched __up(struct semaphore *sem)
{
	if (unlikely(list_empty(&sem->wait_list)))
		sem->count++;
	else
		__up_down_common(sem);
}
