/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * pm_runtime.h - Device run-time power management helper functions.
 *
 * Copyright (C) 2009 Rafael J. Wysocki <rjw@sisk.pl>
 */

#ifndef _LINUX_PM_RUNTIME_H
#define _LINUX_PM_RUNTIME_H

#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/pm.h>

#include <linux/jiffies.h>

/* Runtime PM flag argument bits */
#define RPM_ASYNC		0x01	/* Request is asynchronous */
#define RPM_NOWAIT		0x02	/* Don't wait for concurrent
					    state change */
#define RPM_GET_PUT		0x04	/* Increment/decrement the
					    usage_count */
#define RPM_AUTO		0x08	/* Use autosuspend_delay */
#define RPM_TRANSPARENT	0x10	/* Succeed if runtime PM is disabled */

/*
 * Use this for defining a set of PM operations to be used in all situations
 * (system suspend, hibernation or runtime PM).
 *
 * Note that the behaviour differs from the deprecated UNIVERSAL_DEV_PM_OPS()
 * macro, which uses the provided callbacks for both runtime PM and system
 * sleep, while DEFINE_RUNTIME_DEV_PM_OPS() uses pm_runtime_force_suspend()
 * and pm_runtime_force_resume() for its system sleep callbacks.
 *
 * If the underlying dev_pm_ops struct symbol has to be exported, use
 * EXPORT_RUNTIME_DEV_PM_OPS() or EXPORT_GPL_RUNTIME_DEV_PM_OPS() instead.
 */
#define DEFINE_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn) \
	_DEFINE_DEV_PM_OPS(name, pm_runtime_force_suspend, \
			   pm_runtime_force_resume, suspend_fn, \
			   resume_fn, idle_fn)

#define EXPORT_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn) \
	EXPORT_DEV_PM_OPS(name) = { \
		RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	}
#define EXPORT_GPL_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn) \
	EXPORT_GPL_DEV_PM_OPS(name) = { \
		RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	}
#define EXPORT_NS_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn, ns) \
	EXPORT_NS_DEV_PM_OPS(name, ns) = { \
		RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	}
#define EXPORT_NS_GPL_RUNTIME_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn, ns) \
	EXPORT_NS_GPL_DEV_PM_OPS(name, ns) = { \
		RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	}

#ifdef CONFIG_PM
extern struct workqueue_struct *pm_wq;

static inline bool queue_pm_work(struct work_struct *work)
{
	return queue_work(pm_wq, work);
}

extern int pm_generic_runtime_suspend(struct device *dev);
extern int pm_generic_runtime_resume(struct device *dev);
extern int pm_runtime_force_suspend(struct device *dev);

extern int __pm_runtime_idle(struct device *dev, int rpmflags);
extern int __pm_runtime_suspend(struct device *dev, int rpmflags);
extern int __pm_runtime_resume(struct device *dev, int rpmflags);
extern int pm_runtime_get_if_active(struct device *dev);
extern int pm_runtime_get_if_in_use(struct device *dev);
extern int pm_schedule_suspend(struct device *dev, unsigned int delay);
extern int __pm_runtime_set_status(struct device *dev, unsigned int status);
extern void pm_runtime_barrier(struct device *dev);
extern bool pm_runtime_block_if_disabled(struct device *dev);
extern void pm_runtime_unblock(struct device *dev);
extern void pm_runtime_enable(struct device *dev);
extern void __pm_runtime_disable(struct device *dev, bool check_resume);
extern void pm_runtime_allow(struct device *dev);
extern void pm_runtime_forbid(struct device *dev);
extern void pm_runtime_no_callbacks(struct device *dev);
extern void pm_runtime_irq_safe(struct device *dev);
extern void __pm_runtime_use_autosuspend(struct device *dev, bool use);
extern void pm_runtime_set_autosuspend_delay(struct device *dev, int delay);
extern u64 pm_runtime_autosuspend_expiration(struct device *dev);
extern void pm_runtime_set_memalloc_noio(struct device *dev, bool enable);
extern void pm_runtime_get_suppliers(struct device *dev);
extern void pm_runtime_put_suppliers(struct device *dev);
extern void pm_runtime_new_link(struct device *dev);
extern void pm_runtime_drop_link(struct device_link *link);
extern void pm_runtime_release_supplier(struct device_link *link);

int devm_pm_runtime_set_active_enabled(struct device *dev);
extern int devm_pm_runtime_enable(struct device *dev);
int devm_pm_runtime_get_noresume(struct device *dev);

/**
 * pm_suspend_ignore_children - Set runtime PM behavior regarding children.
 * @dev: Target device.
 * @enable: Whether or not to ignore possible dependencies on children.
 *
 * The dependencies of @dev on its children will not be taken into account by
 * the runtime PM framework going forward if @enable is %true, or they will
 * be taken into account otherwise.
 */
static inline void pm_suspend_ignore_children(struct device *dev, bool enable)
{
	dev->power.ignore_children = enable;
}

/**
 * pm_runtime_get_noresume - Bump up runtime PM usage counter of a device.
 * @dev: Target device.
 */
static inline void pm_runtime_get_noresume(struct device *dev)
{
	atomic_inc(&dev->power.usage_count);
}

/**
 * pm_runtime_put_noidle - Drop runtime PM usage counter of a device.
 * @dev: Target device.
 *
 * Decrement the runtime PM usage counter of @dev unless it is 0 already.
 */
static inline void pm_runtime_put_noidle(struct device *dev)
{
	atomic_add_unless(&dev->power.usage_count, -1, 0);
}

/**
 * pm_runtime_suspended - Check whether or not a device is runtime-suspended.
 * @dev: Target device.
 *
 * Note that the return value of this function can only be trusted if it is
 * called under the runtime PM lock of @dev or under conditions in which
 * runtime PM cannot be either disabled or enabled for @dev and its runtime PM
 * status cannot change.
 *
 * Return:
 * * %true: @dev has runtime PM enabled and its status is %RPM_SUSPENDED.
 * * %false: Otherwise.
 */
static inline bool pm_runtime_suspended(struct device *dev)
{
	return dev->power.runtime_status == RPM_SUSPENDED
		&& !dev->power.disable_depth;
}

/**
 * pm_runtime_active - Check whether or not a device is runtime-active.
 * @dev: Target device.
 *
 * Note that the return value of this function can only be trusted if it is
 * called under the runtime PM lock of @dev or under conditions in which
 * runtime PM cannot be either disabled or enabled for @dev and its runtime PM
 * status cannot change.
 *
 * Return:
 * * %true: Runtime PM is disabled for @dev or its status is %RPM_ACTIVE.
 * * %false: Otherwise.
 */
static inline bool pm_runtime_active(struct device *dev)
{
	return dev->power.runtime_status == RPM_ACTIVE
		|| dev->power.disable_depth;
}

/**
 * pm_runtime_status_suspended - Check if runtime PM status is "suspended".
 * @dev: Target device.
 *
 * Note that the return value of this function can only be trusted if it is
 * called under the runtime PM lock of @dev or under conditions in which the
 * runtime PM status of @dev cannot change.
 *
 * Return:
 * * %true: Runtime PM status of @dev is %RPM_SUSPENDED.
 * * %false: Otherwise.
 */
static inline bool pm_runtime_status_suspended(struct device *dev)
{
	return dev->power.runtime_status == RPM_SUSPENDED;
}

/**
 * pm_runtime_enabled - Check if runtime PM is enabled.
 * @dev: Target device.
 *
 * Note that the return value of this function can only be trusted if it is
 * called under the runtime PM lock of @dev or under conditions in which
 * runtime PM cannot be either disabled or enabled for @dev.
 *
 * Return:
 * * %true: Runtime PM is enabled for @dev.
 * * %false: Otherwise.
 */
static inline bool pm_runtime_enabled(struct device *dev)
{
	return !dev->power.disable_depth;
}

/**
 * pm_runtime_blocked - Check if runtime PM enabling is blocked.
 * @dev: Target device.
 *
 * Do not call this function outside system suspend/resume code paths.
 *
 * Return:
 * * %true: Runtime PM enabling is blocked for @dev.
 * * %false: Otherwise.
 */
static inline bool pm_runtime_blocked(struct device *dev)
{
	return dev->power.last_status == RPM_BLOCKED;
}

/**
 * pm_runtime_has_no_callbacks - Check if runtime PM callbacks may be present.
 * @dev: Target device.
 *
 * Return:
 * * %true: @dev is marked as having no runtime PM callbacks.
 * * %false: Otherwise.
 */
static inline bool pm_runtime_has_no_callbacks(struct device *dev)
{
	return dev->power.no_callbacks;
}

/**
 * pm_runtime_mark_last_busy - Update the last access time of a device.
 * @dev: Target device.
 *
 * Update the last access time of @dev used by the runtime PM autosuspend
 * mechanism to the current time as returned by ktime_get_mono_fast_ns().
 */
static inline void pm_runtime_mark_last_busy(struct device *dev)
{
	WRITE_ONCE(dev->power.last_busy, ktime_get_mono_fast_ns());
}

/**
 * pm_runtime_is_irq_safe - Check if runtime PM can work in interrupt context.
 * @dev: Target device.
 *
 * Return:
 * * %true: @dev has been marked as an "IRQ-safe" device, in which case its
 *   runtime PM callbacks can be expected to work correctly from interrupt
 *   handlers.
 * * %false: Otherwise.
 */
static inline bool pm_runtime_is_irq_safe(struct device *dev)
{
	return dev->power.irq_safe;
}

extern u64 pm_runtime_suspended_time(struct device *dev);

#else /* !CONFIG_PM */

static inline bool queue_pm_work(struct work_struct *work) { return false; }

static inline int pm_generic_runtime_suspend(struct device *dev) { return 0; }
static inline int pm_generic_runtime_resume(struct device *dev) { return 0; }
static inline int pm_runtime_force_suspend(struct device *dev) { return 0; }

static inline int __pm_runtime_idle(struct device *dev, int rpmflags)
{
	return -ENOSYS;
}
static inline int __pm_runtime_suspend(struct device *dev, int rpmflags)
{
	return -ENOSYS;
}
static inline int __pm_runtime_resume(struct device *dev, int rpmflags)
{
	return 1;
}
static inline int pm_schedule_suspend(struct device *dev, unsigned int delay)
{
	return -ENOSYS;
}
static inline int pm_runtime_get_if_in_use(struct device *dev)
{
	return -EINVAL;
}
static inline int pm_runtime_get_if_active(struct device *dev)
{
	return -EINVAL;
}
static inline int __pm_runtime_set_status(struct device *dev,
					    unsigned int status) { return 0; }
static inline void pm_runtime_barrier(struct device *dev) {}
static inline bool pm_runtime_block_if_disabled(struct device *dev) { return true; }
static inline void pm_runtime_unblock(struct device *dev) {}
static inline void pm_runtime_enable(struct device *dev) {}
static inline void __pm_runtime_disable(struct device *dev, bool c) {}
static inline bool pm_runtime_blocked(struct device *dev) { return true; }
static inline void pm_runtime_allow(struct device *dev) {}
static inline void pm_runtime_forbid(struct device *dev) {}

static inline int devm_pm_runtime_set_active_enabled(struct device *dev) { return 0; }
static inline int devm_pm_runtime_enable(struct device *dev) { return 0; }
static inline int devm_pm_runtime_get_noresume(struct device *dev) { return 0; }

static inline void pm_suspend_ignore_children(struct device *dev, bool enable) {}
static inline void pm_runtime_get_noresume(struct device *dev) {}
static inline void pm_runtime_put_noidle(struct device *dev) {}
static inline bool pm_runtime_suspended(struct device *dev) { return false; }
static inline bool pm_runtime_active(struct device *dev) { return true; }
static inline bool pm_runtime_status_suspended(struct device *dev) { return false; }
static inline bool pm_runtime_enabled(struct device *dev) { return false; }

static inline void pm_runtime_no_callbacks(struct device *dev) {}
static inline void pm_runtime_irq_safe(struct device *dev) {}
static inline bool pm_runtime_is_irq_safe(struct device *dev) { return false; }

static inline bool pm_runtime_has_no_callbacks(struct device *dev) { return false; }
static inline void pm_runtime_mark_last_busy(struct device *dev) {}
static inline void __pm_runtime_use_autosuspend(struct device *dev,
						bool use) {}
static inline void pm_runtime_set_autosuspend_delay(struct device *dev,
						int delay) {}
static inline u64 pm_runtime_autosuspend_expiration(
				struct device *dev) { return 0; }
static inline void pm_runtime_set_memalloc_noio(struct device *dev,
						bool enable){}
static inline void pm_runtime_get_suppliers(struct device *dev) {}
static inline void pm_runtime_put_suppliers(struct device *dev) {}
static inline void pm_runtime_new_link(struct device *dev) {}
static inline void pm_runtime_drop_link(struct device_link *link) {}
static inline void pm_runtime_release_supplier(struct device_link *link) {}

#endif /* !CONFIG_PM */

#ifdef CONFIG_PM_SLEEP

bool pm_runtime_need_not_resume(struct device *dev);
int pm_runtime_force_resume(struct device *dev);

#else /* !CONFIG_PM_SLEEP */

static inline bool pm_runtime_need_not_resume(struct device *dev) {return true; }
static inline int pm_runtime_force_resume(struct device *dev) { return -ENXIO; }

#endif /* CONFIG_PM_SLEEP */

/**
 * pm_runtime_idle - Conditionally initiate autosuspend of a device or suspend it.
 * @dev: Target device.
 *
 * Invoke the "idle check" callback of @dev and, depending on its return value,
 * initiate autosuspend of @dev or suspend it (depending on whether or not
 * autosuspend has been enabled for it).
 *
 * Return:
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter non-zero, Runtime PM status change
 *   ongoing or device not in %RPM_ACTIVE state.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-EINPROGRESS: Suspend already in progress.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 * * Other values and conditions for the above values are possible as returned
 *   by Runtime PM idle and suspend callbacks.
 */
static inline int pm_runtime_idle(struct device *dev)
{
	return __pm_runtime_idle(dev, 0);
}

/**
 * pm_runtime_suspend - Suspend a device synchronously.
 * @dev: Target device.
 *
 * Return:
 * * %1: Success; device was already suspended.
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter non-zero or Runtime PM status change
 *   ongoing.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 * * Other values and conditions for the above values are possible as returned
 *   by Runtime PM suspend callbacks.
 */
static inline int pm_runtime_suspend(struct device *dev)
{
	return __pm_runtime_suspend(dev, 0);
}

/**
 * pm_runtime_autosuspend - Update the last access time and initiate autosuspend
 * of a device.
 * @dev: Target device.
 *
 * First update the last access time, then initiate autosuspend of @dev or
 * suspend it (depending on whether or not autosuspend is enabled for it)
 * without engaging its "idle check" callback.
 *
 * Return:
 * * %1: Success; device was already suspended.
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter non-zero or Runtime PM status change
 *   ongoing.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 * * Other values and conditions for the above values are possible as returned
 *   by Runtime PM suspend callbacks.
 */
static inline int pm_runtime_autosuspend(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	return __pm_runtime_suspend(dev, RPM_AUTO);
}

/**
 * pm_runtime_resume - Resume a device synchronously.
 * @dev: Target device.
 *
 * Return:
 * * %1: Success; @dev is already %RPM_ACTIVE.
 * * %0: Success.
 * * Error code on failure.
 */
static inline int pm_runtime_resume(struct device *dev)
{
	return __pm_runtime_resume(dev, 0);
}

/**
 * pm_request_idle - Request an asynchronous idle check for a device.
 * @dev: Target device.
 *
 * Asynchronously request the PM core to evaluate whether @dev can be idled
 * or suspended, invoking its ->runtime_idle() callback if provided.
 *
 * Return:
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter non-zero, Runtime PM status change
 *   ongoing or device not in %RPM_ACTIVE state.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-EINPROGRESS: Suspend already in progress.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 */
static inline int pm_request_idle(struct device *dev)
{
	return __pm_runtime_idle(dev, RPM_ASYNC);
}

/**
 * pm_request_resume - Request an asynchronous runtime resume for a device.
 * @dev: Target device.
 *
 * Asynchronously request the PM core to resume @dev to %RPM_ACTIVE state
 * without modifying its usage counter.
 *
 * Return:
 * * %1: Success; @dev is already %RPM_ACTIVE.
 * * %0: Success.
 * * Error code on failure.
 */
static inline int pm_request_resume(struct device *dev)
{
	return __pm_runtime_resume(dev, RPM_ASYNC);
}

/**
 * pm_request_autosuspend - Update access time and request delayed suspension.
 * @dev: Target device.
 *
 * Update the last access time of @dev and asynchronously request the PM core
 * to suspend it after the autosuspend delay has elapsed.
 *
 * Return:
 * * %1: Success; device was already suspended.
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter non-zero or Runtime PM status change
 *   ongoing.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-EINPROGRESS: Suspend already in progress.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 */
static inline int pm_request_autosuspend(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	return __pm_runtime_suspend(dev, RPM_ASYNC | RPM_AUTO);
}

/**
 * pm_runtime_get - Increment usage counter and request asynchronous resume.
 * @dev: Target device.
 *
 * Increment the runtime PM usage counter of @dev and, if the device is
 * currently suspended, asynchronously request the PM core to resume it.
 *
 * Return:
 * * %1: Success; @dev is already %RPM_ACTIVE.
 * * %0: Success; runtime-resume was queued.
 * * Error code on failure.
 */
static inline int pm_runtime_get(struct device *dev)
{
	return __pm_runtime_resume(dev, RPM_GET_PUT | RPM_ASYNC);
}

/**
 * pm_runtime_get_sync - Bump up usage counter of a device and resume it.
 * @dev: Target device.
 *
 * Bump up the runtime PM usage counter of @dev and carry out runtime-resume of
 * it synchronously.
 *
 * Note that the runtime PM usage counter of @dev remains incremented in all
 * cases, even if it returns an error code. Consider using
 * pm_runtime_resume_and_get() instead, especially if the return value is
 * checked by the caller, as this is likely to result in cleaner code.
 *
 * Return:
 * * %1: Success; @dev is already %RPM_ACTIVE.
 * * %0: Success.
 * * Error code on failure.
 */
static inline int pm_runtime_get_sync(struct device *dev)
{
	return __pm_runtime_resume(dev, RPM_GET_PUT);
}

static inline int pm_runtime_get_active(struct device *dev, int rpmflags)
{
	int ret;

	ret = __pm_runtime_resume(dev, RPM_GET_PUT | rpmflags);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	return 0;
}

/**
 * pm_runtime_resume_and_get - Bump up usage counter of a device and resume it.
 * @dev: Target device.
 *
 * Resume @dev synchronously and if that is successful, increment its runtime
 * PM usage counter.
 *
 * Return:
 * * %0: Success; @dev is active and its usage counter has been incremented.
 * * Negative error code on failure; usage counter is unchanged.
 */
static inline int pm_runtime_resume_and_get(struct device *dev)
{
	return pm_runtime_get_active(dev, 0);
}

/**
 * pm_runtime_put - Drop device usage counter and request asynchronous idle check.
 * @dev: Target device.
 *
 * Decrement the runtime PM usage counter of @dev. If the counter reaches zero
 * and the device has no active child dependencies, asynchronously request the
 * PM core to idle or suspend the device.
 */
static inline void pm_runtime_put(struct device *dev)
{
	__pm_runtime_idle(dev, RPM_GET_PUT | RPM_ASYNC);
}

/**
 * __pm_runtime_put_autosuspend - Drop device usage counter and queue autosuspend if 0.
 * @dev: Target device.
 *
 * Decrement the runtime PM usage counter of @dev and if it turns out to be
 * equal to 0, queue up a work item for @dev like in pm_request_autosuspend().
 *
 * Return:
 * * %1: Success. Usage counter dropped to zero, but device was already suspended.
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter became non-zero or Runtime PM status
 *   change ongoing.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-EINPROGRESS: Suspend already in progress.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 */
static inline int __pm_runtime_put_autosuspend(struct device *dev)
{
	return __pm_runtime_suspend(dev, RPM_GET_PUT | RPM_ASYNC | RPM_AUTO);
}

/**
 * pm_runtime_put_autosuspend - Update the last access time, drop usage counter
 * and request autosuspend.
 * @dev: Target device.
 *
 * Update the last access time of @dev and decrement its runtime PM usage
 * counter. If the counter drops to zero, asynchronously request the PM core to
 * suspend the device once its autosuspend delay has elapsed.
 *
 * Return:
 * * %1: Success. Usage counter dropped to zero, but device was already suspended.
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter became non-zero or Runtime PM status
 *   change ongoing.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-EINPROGRESS: Suspend already in progress.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 */
static inline int pm_runtime_put_autosuspend(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	return __pm_runtime_put_autosuspend(dev);
}

DEFINE_GUARD(pm_runtime_noresume, struct device *,
	     pm_runtime_get_noresume(_T), pm_runtime_put_noidle(_T));

DEFINE_GUARD(pm_runtime_active, struct device *,
	     pm_runtime_get_sync(_T), pm_runtime_put(_T));
DEFINE_GUARD(pm_runtime_active_auto, struct device *,
	     pm_runtime_get_sync(_T), pm_runtime_put_autosuspend(_T));
/*
 * Use the following guards with ACQUIRE()/ACQUIRE_ERR().
 *
 * The difference between the "_try" and "_try_enabled" variants is that the
 * former do not produce an error when runtime PM is disabled for the given
 * device.
 */
DEFINE_GUARD_COND(pm_runtime_active, _try,
		  pm_runtime_get_active(_T, RPM_TRANSPARENT), _RET == 0)
DEFINE_GUARD_COND(pm_runtime_active, _try_enabled,
		  pm_runtime_resume_and_get(_T), _RET == 0)
DEFINE_GUARD_COND(pm_runtime_active_auto, _try,
		  pm_runtime_get_active(_T, RPM_TRANSPARENT), _RET == 0)
DEFINE_GUARD_COND(pm_runtime_active_auto, _try_enabled,
		  pm_runtime_resume_and_get(_T), _RET == 0)

/* ACQUIRE() wrapper macros for the guards defined above. */

#define PM_RUNTIME_ACQUIRE(_dev, _var)			\
	ACQUIRE(pm_runtime_active_try, _var)(_dev)

#define PM_RUNTIME_ACQUIRE_AUTOSUSPEND(_dev, _var)	\
	ACQUIRE(pm_runtime_active_auto_try, _var)(_dev)

#define PM_RUNTIME_ACQUIRE_IF_ENABLED(_dev, _var)	\
	ACQUIRE(pm_runtime_active_try_enabled, _var)(_dev)

#define PM_RUNTIME_ACQUIRE_IF_ENABLED_AUTOSUSPEND(_dev, _var)	\
	ACQUIRE(pm_runtime_active_auto_try_enabled, _var)(_dev)

/*
 * ACQUIRE_ERR() wrapper macro for guard pm_runtime_active.
 *
 * Always check PM_RUNTIME_ACQUIRE_ERR() after using one of the
 * PM_RUNTIME_ACQUIRE*() macros defined above (yes, it can be used with
 * any of them) and if it is nonzero, avoid accessing the given device.
 */
#define PM_RUNTIME_ACQUIRE_ERR(_var_ptr)	\
	ACQUIRE_ERR(pm_runtime_active, _var_ptr)

/**
 * pm_runtime_put_sync - Drop device usage counter and run "idle check" if 0.
 * @dev: Target device.
 *
 * Decrement the runtime PM usage counter of @dev. If the counter drops to zero,
 * synchronously evaluate and trigger idle/suspend handling.
 *
 * Note that this does not update the last access time, but it does respect
 * existing autosuspend timers. If @dev uses autosuspend, consider using
 * pm_runtime_put_sync_autosuspend() or pm_runtime_put_sync_suspend() instead.
 *
 * The runtime PM usage counter of @dev remains decremented in all cases, even
 * if it returns an error code.
 *
 * Return:
 * * %1: Success. Usage counter dropped to zero, but device was already suspended.
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter became non-zero or Runtime PM status
 *   change ongoing.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 * * Other values and conditions for the above values are possible as returned
 *   by Runtime PM suspend callbacks.
 */
static inline int pm_runtime_put_sync(struct device *dev)
{
	return __pm_runtime_idle(dev, RPM_GET_PUT);
}

/**
 * pm_runtime_put_sync_suspend - Drop device usage counter and suspend if 0.
 * @dev: Target device.
 *
 * Decrement the runtime PM usage counter of @dev. If the counter drops to zero,
 * suspend the device synchronously.
 *
 * This API differs from pm_runtime_put_sync() and
 * pm_runtime_put_sync_autosuspend() in that it ignores any outstanding
 * autosuspend delays.
 *
 * The runtime PM usage counter of @dev remains decremented in all cases, even
 * if it returns an error code.
 *
 * Return:
 * * %1: Success. Usage counter dropped to zero, but device was already suspended.
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter became non-zero or Runtime PM status
 *   change ongoing.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 * * Other values and conditions for the above values are possible as returned
 *   by Runtime PM suspend callbacks.
 */
static inline int pm_runtime_put_sync_suspend(struct device *dev)
{
	return __pm_runtime_suspend(dev, RPM_GET_PUT);
}

/**
 * pm_runtime_put_sync_autosuspend - Update the last access time of a device,
 * drop device usage counter and autosuspend if 0.
 * @dev: Target device.
 *
 * Update the last access time of @dev and decrement its runtime PM usage
 * counter. If the counter drops to zero, synchronously suspend the device (or
 * schedule autosuspend if the delay has not elapsed).
 *
 * Prefer this API over pm_runtime_put_sync() for devices that use autosuspend.
 *
 * The runtime PM usage counter of @dev remains decremented in all cases, even
 * if it returns an error code.
 *
 * Return:
 * * %1: Success. Usage counter dropped to zero, but device was already suspended.
 * * %0: Success.
 * * %-EINVAL: Runtime PM error.
 * * %-EACCES: Runtime PM disabled.
 * * %-EAGAIN: Runtime PM usage counter became non-zero or Runtime PM status
 *   change ongoing.
 * * %-EBUSY: Runtime PM child_count non-zero.
 * * %-EPERM: Device PM QoS resume latency 0.
 * * %-EINPROGRESS: Suspend already in progress.
 * * %-ENOSYS: %CONFIG_PM not enabled.
 * * Other values and conditions for the above values are possible as returned
 *   by Runtime PM suspend callbacks.
 */
static inline int pm_runtime_put_sync_autosuspend(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	return __pm_runtime_suspend(dev, RPM_GET_PUT | RPM_AUTO);
}

/**
 * pm_runtime_set_active - Set runtime PM status to "active" and clear errors.
 * @dev: Target device.
 *
 * Set the runtime PM status of @dev to %RPM_ACTIVE and ensure that its
 * dependencies will be taken into account. Also clear the device's error
 * status (@dev->power.runtime_error).
 *
 * It is only valid to call this function if runtime PM is disabled or if
 * @dev->power.runtime_error is set.
 *
 * This will fail if suppliers cannot be resumed, or if the parent is not in
 * the correct state.
 *
 * Return:
 * * %0: Success.
 * * Error code on failure.
 */
static inline int pm_runtime_set_active(struct device *dev)
{
	return __pm_runtime_set_status(dev, RPM_ACTIVE);
}

/**
 * pm_runtime_set_suspended - Set runtime PM status to "suspended" and clear errors.
 * @dev: Target device.
 *
 * Set the runtime PM status of @dev to %RPM_SUSPENDED and ensure that its
 * dependencies will be taken into account. Also clear the device's error
 * status (@dev->power.runtime_error).
 *
 * It is only valid to call this function if runtime PM is disabled or if
 * @dev->power.runtime_error is set.
 *
 * Return:
 * * %0: Success.
 * * Error code on failure.
 */
static inline int pm_runtime_set_suspended(struct device *dev)
{
	return __pm_runtime_set_status(dev, RPM_SUSPENDED);
}

/**
 * pm_runtime_disable - Disable runtime PM for a device.
 * @dev: Target device.
 *
 * Prevent the runtime PM framework from working with @dev by incrementing its
 * "disable" counter.
 *
 * If the counter is zero when this function runs and there is a pending runtime
 * resume request for @dev, it will be resumed.  If the counter is still zero at
 * that point, this function cancels all pending runtime PM requests for @dev
 * and waits for its runtime PM operations to complete (if any).
 *
 * For each invocation of this function for @dev, there must be a matching
 * pm_runtime_enable() call, so that runtime PM is eventually enabled for it
 * again.
 */
static inline void pm_runtime_disable(struct device *dev)
{
	__pm_runtime_disable(dev, true);
}

/**
 * pm_runtime_use_autosuspend - Allow autosuspend to be used for a device.
 * @dev: Target device.
 *
 * Allow the runtime PM autosuspend mechanism to be used for @dev whenever
 * requested (or "autosuspend" will be handled as direct runtime-suspend for
 * it).
 *
 * NOTE: It's important to undo this with pm_runtime_dont_use_autosuspend()
 * at driver exit time unless your driver initially enabled pm_runtime
 * with devm_pm_runtime_enable() (which handles it for you).
 */
static inline void pm_runtime_use_autosuspend(struct device *dev)
{
	__pm_runtime_use_autosuspend(dev, true);
}

/**
 * pm_runtime_dont_use_autosuspend - Prevent autosuspend from being used.
 * @dev: Target device.
 *
 * Prevent the runtime PM autosuspend mechanism from being used for @dev which
 * means that "autosuspend" will be handled as direct runtime-suspend for it
 * going forward.
 */
static inline void pm_runtime_dont_use_autosuspend(struct device *dev)
{
	__pm_runtime_use_autosuspend(dev, false);
}

#endif
