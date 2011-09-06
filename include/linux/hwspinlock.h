/*
 * Hardware spinlock public header
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com
 *
 * Contact: Ohad Ben-Cohen <ohad@wizery.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_HWSPINLOCK_H
#define __LINUX_HWSPINLOCK_H

#include <linux/err.h>
#include <linux/sched.h>
#include <linux/device.h>

/* hwspinlock mode argument */
#define HWLOCK_IRQSTATE	0x01	/* Disable interrupts, save state */
#define HWLOCK_IRQ	0x02	/* Disable interrupts, don't save state */

struct hwspinlock;
struct hwspinlock_device;
struct hwspinlock_ops;

/**
 * struct hwspinlock_pdata - platform data for hwspinlock drivers
 * @base_id: base id for this hwspinlock device
 *
 * hwspinlock devices provide system-wide hardware locks that are used
 * by remote processors that have no other way to achieve synchronization.
 *
 * To achieve that, each physical lock must have a system-wide id number
 * that is agreed upon, otherwise remote processors can't possibly assume
 * they're using the same hardware lock.
 *
 * Usually boards have a single hwspinlock device, which provides several
 * hwspinlocks, and in this case, they can be trivially numbered 0 to
 * (num-of-locks - 1).
 *
 * In case boards have several hwspinlocks devices, a different base id
 * should be used for each hwspinlock device (they can't all use 0 as
 * a starting id!).
 *
 * This platform data structure should be used to provide the base id
 * for each device (which is trivially 0 when only a single hwspinlock
 * device exists). It can be shared between different platforms, hence
 * its location.
 */
struct hwspinlock_pdata {
	int base_id;
};

#if defined(CONFIG_HWSPINLOCK) || defined(CONFIG_HWSPINLOCK_MODULE)

int hwspin_lock_register(struct hwspinlock_device *bank, struct device *dev,
		const struct hwspinlock_ops *ops, int base_id, int num_locks);
int hwspin_lock_unregister(struct hwspinlock_device *bank);
struct hwspinlock *hwspin_lock_request(void);
struct hwspinlock *hwspin_lock_request_specific(unsigned int id);
int hwspin_lock_free(struct hwspinlock *hwlock);
int hwspin_lock_get_id(struct hwspinlock *hwlock);
int __hwspin_lock_timeout(struct hwspinlock *, unsigned int, int,
							unsigned long *);
int __hwspin_trylock(struct hwspinlock *, int, unsigned long *);
void __hwspin_unlock(struct hwspinlock *, int, unsigned long *);

#else /* !CONFIG_HWSPINLOCK */

/*
 * We don't want these functions to fail if CONFIG_HWSPINLOCK is not
 * enabled. We prefer to silently succeed in this case, and let the
 * code path get compiled away. This way, if CONFIG_HWSPINLOCK is not
 * required on a given setup, users will still work.
 *
 * The only exception is hwspin_lock_register/hwspin_lock_unregister, with which
 * we _do_ want users to fail (no point in registering hwspinlock instances if
 * the framework is not available).
 *
 * Note: ERR_PTR(-ENODEV) will still be considered a success for NULL-checking
 * users. Others, which care, can still check this with IS_ERR.
 */
static inline struct hwspinlock *hwspin_lock_request(void)
{
	return ERR_PTR(-ENODEV);
}

static inline struct hwspinlock *hwspin_lock_request_specific(unsigned int id)
{
	return ERR_PTR(-ENODEV);
}

static inline int hwspin_lock_free(struct hwspinlock *hwlock)
{
	return 0;
}

static inline
int __hwspin_lock_timeout(struct hwspinlock *hwlock, unsigned int to,
					int mode, unsigned long *flags)
{
	return 0;
}

static inline
int __hwspin_trylock(struct hwspinlock *hwlock, int mode, unsigned long *flags)
{
	return 0;
}

static inline
void __hwspin_unlock(struct hwspinlock *hwlock, int mode, unsigned long *flags)
{
	return 0;
}

static inline int hwspin_lock_get_id(struct hwspinlock *hwlock)
{
	return 0;
}

#endif /* !CONFIG_HWSPINLOCK */

/**
 * hwspin_trylock_irqsave() - try to lock an hwspinlock, disable interrupts
 * @hwlock: an hwspinlock which we want to trylock
 * @flags: a pointer to where the caller's interrupt state will be saved at
 *
 * This function attempts to lock the underlying hwspinlock, and will
 * immediately fail if the hwspinlock is already locked.
 *
 * Upon a successful return from this function, preemption and local
 * interrupts are disabled (previous interrupts state is saved at @flags),
 * so the caller must not sleep, and is advised to release the hwspinlock
 * as soon as possible.
 *
 * Returns 0 if we successfully locked the hwspinlock, -EBUSY if
 * the hwspinlock was already taken, and -EINVAL if @hwlock is invalid.
 */
static inline
int hwspin_trylock_irqsave(struct hwspinlock *hwlock, unsigned long *flags)
{
	return __hwspin_trylock(hwlock, HWLOCK_IRQSTATE, flags);
}

/**
 * hwspin_trylock_irq() - try to lock an hwspinlock, disable interrupts
 * @hwlock: an hwspinlock which we want to trylock
 *
 * This function attempts to lock the underlying hwspinlock, and will
 * immediately fail if the hwspinlock is already locked.
 *
 * Upon a successful return from this function, preemption and local
 * interrupts are disabled, so the caller must not sleep, and is advised
 * to release the hwspinlock as soon as possible.
 *
 * Returns 0 if we successfully locked the hwspinlock, -EBUSY if
 * the hwspinlock was already taken, and -EINVAL if @hwlock is invalid.
 */
static inline int hwspin_trylock_irq(struct hwspinlock *hwlock)
{
	return __hwspin_trylock(hwlock, HWLOCK_IRQ, NULL);
}

/**
 * hwspin_trylock() - attempt to lock a specific hwspinlock
 * @hwlock: an hwspinlock which we want to trylock
 *
 * This function attempts to lock an hwspinlock, and will immediately fail
 * if the hwspinlock is already taken.
 *
 * Upon a successful return from this function, preemption is disabled,
 * so the caller must not sleep, and is advised to release the hwspinlock
 * as soon as possible. This is required in order to minimize remote cores
 * polling on the hardware interconnect.
 *
 * Returns 0 if we successfully locked the hwspinlock, -EBUSY if
 * the hwspinlock was already taken, and -EINVAL if @hwlock is invalid.
 */
static inline int hwspin_trylock(struct hwspinlock *hwlock)
{
	return __hwspin_trylock(hwlock, 0, NULL);
}

/**
 * hwspin_lock_timeout_irqsave() - lock hwspinlock, with timeout, disable irqs
 * @hwlock: the hwspinlock to be locked
 * @to: timeout value in msecs
 * @flags: a pointer to where the caller's interrupt state will be saved at
 *
 * This function locks the underlying @hwlock. If the @hwlock
 * is already taken, the function will busy loop waiting for it to
 * be released, but give up when @timeout msecs have elapsed.
 *
 * Upon a successful return from this function, preemption and local interrupts
 * are disabled (plus previous interrupt state is saved), so the caller must
 * not sleep, and is advised to release the hwspinlock as soon as possible.
 *
 * Returns 0 when the @hwlock was successfully taken, and an appropriate
 * error code otherwise (most notably an -ETIMEDOUT if the @hwlock is still
 * busy after @timeout msecs). The function will never sleep.
 */
static inline int hwspin_lock_timeout_irqsave(struct hwspinlock *hwlock,
				unsigned int to, unsigned long *flags)
{
	return __hwspin_lock_timeout(hwlock, to, HWLOCK_IRQSTATE, flags);
}

/**
 * hwspin_lock_timeout_irq() - lock hwspinlock, with timeout, disable irqs
 * @hwlock: the hwspinlock to be locked
 * @to: timeout value in msecs
 *
 * This function locks the underlying @hwlock. If the @hwlock
 * is already taken, the function will busy loop waiting for it to
 * be released, but give up when @timeout msecs have elapsed.
 *
 * Upon a successful return from this function, preemption and local interrupts
 * are disabled so the caller must not sleep, and is advised to release the
 * hwspinlock as soon as possible.
 *
 * Returns 0 when the @hwlock was successfully taken, and an appropriate
 * error code otherwise (most notably an -ETIMEDOUT if the @hwlock is still
 * busy after @timeout msecs). The function will never sleep.
 */
static inline
int hwspin_lock_timeout_irq(struct hwspinlock *hwlock, unsigned int to)
{
	return __hwspin_lock_timeout(hwlock, to, HWLOCK_IRQ, NULL);
}

/**
 * hwspin_lock_timeout() - lock an hwspinlock with timeout limit
 * @hwlock: the hwspinlock to be locked
 * @to: timeout value in msecs
 *
 * This function locks the underlying @hwlock. If the @hwlock
 * is already taken, the function will busy loop waiting for it to
 * be released, but give up when @timeout msecs have elapsed.
 *
 * Upon a successful return from this function, preemption is disabled
 * so the caller must not sleep, and is advised to release the hwspinlock
 * as soon as possible.
 * This is required in order to minimize remote cores polling on the
 * hardware interconnect.
 *
 * Returns 0 when the @hwlock was successfully taken, and an appropriate
 * error code otherwise (most notably an -ETIMEDOUT if the @hwlock is still
 * busy after @timeout msecs). The function will never sleep.
 */
static inline
int hwspin_lock_timeout(struct hwspinlock *hwlock, unsigned int to)
{
	return __hwspin_lock_timeout(hwlock, to, 0, NULL);
}

/**
 * hwspin_unlock_irqrestore() - unlock hwspinlock, restore irq state
 * @hwlock: a previously-acquired hwspinlock which we want to unlock
 * @flags: previous caller's interrupt state to restore
 *
 * This function will unlock a specific hwspinlock, enable preemption and
 * restore the previous state of the local interrupts. It should be used
 * to undo, e.g., hwspin_trylock_irqsave().
 *
 * @hwlock must be already locked before calling this function: it is a bug
 * to call unlock on a @hwlock that is already unlocked.
 */
static inline void hwspin_unlock_irqrestore(struct hwspinlock *hwlock,
							unsigned long *flags)
{
	__hwspin_unlock(hwlock, HWLOCK_IRQSTATE, flags);
}

/**
 * hwspin_unlock_irq() - unlock hwspinlock, enable interrupts
 * @hwlock: a previously-acquired hwspinlock which we want to unlock
 *
 * This function will unlock a specific hwspinlock, enable preemption and
 * enable local interrupts. Should be used to undo hwspin_lock_irq().
 *
 * @hwlock must be already locked (e.g. by hwspin_trylock_irq()) before
 * calling this function: it is a bug to call unlock on a @hwlock that is
 * already unlocked.
 */
static inline void hwspin_unlock_irq(struct hwspinlock *hwlock)
{
	__hwspin_unlock(hwlock, HWLOCK_IRQ, NULL);
}

/**
 * hwspin_unlock() - unlock hwspinlock
 * @hwlock: a previously-acquired hwspinlock which we want to unlock
 *
 * This function will unlock a specific hwspinlock and enable preemption
 * back.
 *
 * @hwlock must be already locked (e.g. by hwspin_trylock()) before calling
 * this function: it is a bug to call unlock on a @hwlock that is already
 * unlocked.
 */
static inline void hwspin_unlock(struct hwspinlock *hwlock)
{
	__hwspin_unlock(hwlock, 0, NULL);
}

#endif /* __LINUX_HWSPINLOCK_H */
