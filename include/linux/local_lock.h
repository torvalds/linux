/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LOCAL_LOCK_H
#define _LINUX_LOCAL_LOCK_H

#include <linux/local_lock_internal.h>

/**
 * local_lock_init - Runtime initialize a lock instance
 * @lock:	The lock variable
 */
#define local_lock_init(lock)		__local_lock_init(lock)

/**
 * local_lock - Acquire a per CPU local lock
 * @lock:	The lock variable
 */
#define local_lock(lock)		__local_lock(__this_cpu_local_lock(lock))

/**
 * local_lock_irq - Acquire a per CPU local lock and disable interrupts
 * @lock:	The lock variable
 */
#define local_lock_irq(lock)		__local_lock_irq(__this_cpu_local_lock(lock))

/**
 * local_lock_irqsave - Acquire a per CPU local lock, save and disable
 *			 interrupts
 * @lock:	The lock variable
 * @flags:	Storage for interrupt flags
 */
#define local_lock_irqsave(lock, flags)				\
	__local_lock_irqsave(__this_cpu_local_lock(lock), flags)

/**
 * local_unlock - Release a per CPU local lock
 * @lock:	The lock variable
 */
#define local_unlock(lock)		__local_unlock(__this_cpu_local_lock(lock))

/**
 * local_unlock_irq - Release a per CPU local lock and enable interrupts
 * @lock:	The lock variable
 */
#define local_unlock_irq(lock)		__local_unlock_irq(__this_cpu_local_lock(lock))

/**
 * local_unlock_irqrestore - Release a per CPU local lock and restore
 *			      interrupt flags
 * @lock:	The lock variable
 * @flags:      Interrupt flags to restore
 */
#define local_unlock_irqrestore(lock, flags)			\
	__local_unlock_irqrestore(__this_cpu_local_lock(lock), flags)

/**
 * local_trylock_init - Runtime initialize a lock instance
 * @lock:	The lock variable
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
#define local_trylock(lock)		__local_trylock(__this_cpu_local_lock(lock))

#define local_lock_is_locked(lock)	__local_lock_is_locked(lock)

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
	__local_trylock_irqsave(__this_cpu_local_lock(lock), flags)

DEFINE_LOCK_GUARD_1(local_lock, local_lock_t __percpu,
		    local_lock(_T->lock),
		    local_unlock(_T->lock))
DEFINE_LOCK_GUARD_1(local_lock_irq, local_lock_t __percpu,
		    local_lock_irq(_T->lock),
		    local_unlock_irq(_T->lock))
DEFINE_LOCK_GUARD_1(local_lock_irqsave, local_lock_t __percpu,
		    local_lock_irqsave(_T->lock, _T->flags),
		    local_unlock_irqrestore(_T->lock, _T->flags),
		    unsigned long flags)

#define local_lock_nested_bh(_lock)				\
	__local_lock_nested_bh(__this_cpu_local_lock(_lock))

#define local_unlock_nested_bh(_lock)				\
	__local_unlock_nested_bh(__this_cpu_local_lock(_lock))

DEFINE_LOCK_GUARD_1(local_lock_nested_bh, local_lock_t __percpu,
		    local_lock_nested_bh(_T->lock),
		    local_unlock_nested_bh(_T->lock))

DEFINE_LOCK_GUARD_1(local_lock_init, local_lock_t, local_lock_init(_T->lock), /* */)

DECLARE_LOCK_GUARD_1_ATTRS(local_lock, __acquires(_T), __releases(*(local_lock_t __percpu **)_T))
#define class_local_lock_constructor(_T) WITH_LOCK_GUARD_1_ATTRS(local_lock, _T)
DECLARE_LOCK_GUARD_1_ATTRS(local_lock_irq, __acquires(_T), __releases(*(local_lock_t __percpu **)_T))
#define class_local_lock_irq_constructor(_T) WITH_LOCK_GUARD_1_ATTRS(local_lock_irq, _T)
DECLARE_LOCK_GUARD_1_ATTRS(local_lock_irqsave, __acquires(_T), __releases(*(local_lock_t __percpu **)_T))
#define class_local_lock_irqsave_constructor(_T) WITH_LOCK_GUARD_1_ATTRS(local_lock_irqsave, _T)
DECLARE_LOCK_GUARD_1_ATTRS(local_lock_nested_bh, __acquires(_T), __releases(*(local_lock_t __percpu **)_T))
#define class_local_lock_nested_bh_constructor(_T) WITH_LOCK_GUARD_1_ATTRS(local_lock_nested_bh, _T)
DECLARE_LOCK_GUARD_1_ATTRS(local_lock_init, __acquires(_T), __releases(*(local_lock_t **)_T))
#define class_local_lock_init_constructor(_T) WITH_LOCK_GUARD_1_ATTRS(local_lock_init, _T)

DEFINE_LOCK_GUARD_1(local_trylock_init, local_trylock_t, local_trylock_init(_T->lock), /* */)
DECLARE_LOCK_GUARD_1_ATTRS(local_trylock_init, __acquires(_T), __releases(*(local_trylock_t **)_T))
#define class_local_trylock_init_constructor(_T) WITH_LOCK_GUARD_1_ATTRS(local_trylock_init, _T)

#endif
