/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LOCAL_LOCK_H
#define _LINUX_LOCAL_LOCK_H

#include <linux/local_lock_internal.h>

/**
 * local_lock_init - Runtime initialize a lock instance
 */
#define local_lock_init(lock)		__local_lock_init(lock)

/**
 * local_lock - Acquire a per CPU local lock
 * @lock:	The lock variable
 */
#define local_lock(lock)		__local_lock(this_cpu_ptr(lock))

/**
 * local_lock_irq - Acquire a per CPU local lock and disable interrupts
 * @lock:	The lock variable
 */
#define local_lock_irq(lock)		__local_lock_irq(this_cpu_ptr(lock))

/**
 * local_lock_irqsave - Acquire a per CPU local lock, save and disable
 *			 interrupts
 * @lock:	The lock variable
 * @flags:	Storage for interrupt flags
 */
#define local_lock_irqsave(lock, flags)				\
	__local_lock_irqsave(this_cpu_ptr(lock), flags)

/**
 * local_unlock - Release a per CPU local lock
 * @lock:	The lock variable
 */
#define local_unlock(lock)		__local_unlock(this_cpu_ptr(lock))

/**
 * local_unlock_irq - Release a per CPU local lock and enable interrupts
 * @lock:	The lock variable
 */
#define local_unlock_irq(lock)		__local_unlock_irq(this_cpu_ptr(lock))

/**
 * local_unlock_irqrestore - Release a per CPU local lock and restore
 *			      interrupt flags
 * @lock:	The lock variable
 * @flags:      Interrupt flags to restore
 */
#define local_unlock_irqrestore(lock, flags)			\
	__local_unlock_irqrestore(this_cpu_ptr(lock), flags)

/**
 * local_lock_init - Runtime initialize a lock instance
 */
#define local_trylock_init(lock)	__local_trylock_init(lock)

/**
 * local_trylock - Try to acquire a per CPU local lock
 * @lock:	The lock variable
 *
 * The function can be used in any context such as NMI or HARDIRQ. Due to
 * locking constrains it will _always_ fail to acquire the lock in NMI or
 * HARDIRQ context on PREEMPT_RT.
 */
#define local_trylock(lock)		__local_trylock(this_cpu_ptr(lock))

/**
 * local_trylock_irqsave - Try to acquire a per CPU local lock, save and disable
 *			   interrupts if acquired
 * @lock:	The lock variable
 * @flags:	Storage for interrupt flags
 *
 * The function can be used in any context such as NMI or HARDIRQ. Due to
 * locking constrains it will _always_ fail to acquire the lock in NMI or
 * HARDIRQ context on PREEMPT_RT.
 */
#define local_trylock_irqsave(lock, flags)			\
	__local_trylock_irqsave(this_cpu_ptr(lock), flags)

DEFINE_GUARD(local_lock, local_lock_t __percpu*,
	     local_lock(_T),
	     local_unlock(_T))
DEFINE_GUARD(local_lock_irq, local_lock_t __percpu*,
	     local_lock_irq(_T),
	     local_unlock_irq(_T))
DEFINE_LOCK_GUARD_1(local_lock_irqsave, local_lock_t __percpu,
		    local_lock_irqsave(_T->lock, _T->flags),
		    local_unlock_irqrestore(_T->lock, _T->flags),
		    unsigned long flags)

#define local_lock_nested_bh(_lock)				\
	__local_lock_nested_bh(this_cpu_ptr(_lock))

#define local_unlock_nested_bh(_lock)				\
	__local_unlock_nested_bh(this_cpu_ptr(_lock))

DEFINE_GUARD(local_lock_nested_bh, local_lock_t __percpu*,
	     local_lock_nested_bh(_T),
	     local_unlock_nested_bh(_T))

#endif
