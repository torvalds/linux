/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  pm.h - Power management interface
 *
 *  Copyright (C) 2000 Andrew Henroid
 */

#ifndef _LINUX_PM_H
#define _LINUX_PM_H

#include <linux/export.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>

/*
 * Callbacks for platform drivers to implement.
 */
extern void (*pm_power_off)(void);

struct device; /* we have a circular dep with device.h */
#ifdef CONFIG_VT_CONSOLE_SLEEP
extern void pm_vt_switch_required(struct device *dev, bool required);
extern void pm_vt_switch_unregister(struct device *dev);
#else
static inline void pm_vt_switch_required(struct device *dev, bool required)
{
}
static inline void pm_vt_switch_unregister(struct device *dev)
{
}
#endif /* CONFIG_VT_CONSOLE_SLEEP */

#ifdef CONFIG_CXL_SUSPEND
bool cxl_mem_active(void);
#else
static inline bool cxl_mem_active(void)
{
	return false;
}
#endif

/*
 * Device power management
 */


#ifdef CONFIG_PM
extern const char power_group_name[];		/* = "power" */
#else
#define power_group_name	NULL
#endif

typedef struct pm_message {
	int event;
} pm_message_t;

/**
 * struct dev_pm_ops - device PM callbacks.
 *
 * @prepare: The principal role of this callback is to prevent new children of
 *	the device from being registered after it has returned (the driver's
 *	subsystem and generally the rest of the kernel is supposed to prevent
 *	new calls to the probe method from being made too once @prepare() has
 *	succeeded).  If @prepare() detects a situation it cannot handle (e.g.
 *	registration of a child already in progress), it may return -EAGAIN, so
 *	that the PM core can execute it once again (e.g. after a new child has
 *	been registered) to recover from the race condition.
 *	This method is executed for all kinds of suspend transitions and is
 *	followed by one of the suspend callbacks: @suspend(), @freeze(), or
 *	@poweroff().  If the transition is a suspend to memory or standby (that
 *	is, not related to hibernation), the return value of @prepare() may be
 *	used to indicate to the PM core to leave the device in runtime suspend
 *	if applicable.  Namely, if @prepare() returns a positive number, the PM
 *	core will understand that as a declaration that the device appears to be
 *	runtime-suspended and it may be left in that state during the entire
 *	transition and during the subsequent resume if all of its descendants
 *	are left in runtime suspend too.  If that happens, @complete() will be
 *	executed directly after @prepare() and it must ensure the proper
 *	functioning of the device after the system resume.
 *	The PM core executes subsystem-level @prepare() for all devices before
 *	starting to invoke suspend callbacks for any of them, so generally
 *	devices may be assumed to be functional or to respond to runtime resume
 *	requests while @prepare() is being executed.  However, device drivers
 *	may NOT assume anything about the availability of user space at that
 *	time and it is NOT valid to request firmware from within @prepare()
 *	(it's too late to do that).  It also is NOT valid to allocate
 *	substantial amounts of memory from @prepare() in the GFP_KERNEL mode.
 *	[To work around these limitations, drivers may register suspend and
 *	hibernation notifiers to be executed before the freezing of tasks.]
 *
 * @complete: Undo the changes made by @prepare().  This method is executed for
 *	all kinds of resume transitions, following one of the resume callbacks:
 *	@resume(), @thaw(), @restore().  Also called if the state transition
 *	fails before the driver's suspend callback: @suspend(), @freeze() or
 *	@poweroff(), can be executed (e.g. if the suspend callback fails for one
 *	of the other devices that the PM core has unsuccessfully attempted to
 *	suspend earlier).
 *	The PM core executes subsystem-level @complete() after it has executed
 *	the appropriate resume callbacks for all devices.  If the corresponding
 *	@prepare() at the beginning of the suspend transition returned a
 *	positive number and the device was left in runtime suspend (without
 *	executing any suspend and resume callbacks for it), @complete() will be
 *	the only callback executed for the device during resume.  In that case,
 *	@complete() must be prepared to do whatever is necessary to ensure the
 *	proper functioning of the device after the system resume.  To this end,
 *	@complete() can check the power.direct_complete flag of the device to
 *	learn whether (unset) or not (set) the previous suspend and resume
 *	callbacks have been executed for it.
 *
 * @suspend: Executed before putting the system into a sleep state in which the
 *	contents of main memory are preserved.  The exact action to perform
 *	depends on the device's subsystem (PM domain, device type, class or bus
 *	type), but generally the device must be quiescent after subsystem-level
 *	@suspend() has returned, so that it doesn't do any I/O or DMA.
 *	Subsystem-level @suspend() is executed for all devices after invoking
 *	subsystem-level @prepare() for all of them.
 *
 * @suspend_late: Continue operations started by @suspend().  For a number of
 *	devices @suspend_late() may point to the same callback routine as the
 *	runtime suspend callback.
 *
 * @resume: Executed after waking the system up from a sleep state in which the
 *	contents of main memory were preserved.  The exact action to perform
 *	depends on the device's subsystem, but generally the driver is expected
 *	to start working again, responding to hardware events and software
 *	requests (the device itself may be left in a low-power state, waiting
 *	for a runtime resume to occur).  The state of the device at the time its
 *	driver's @resume() callback is run depends on the platform and subsystem
 *	the device belongs to.  On most platforms, there are no restrictions on
 *	availability of resources like clocks during @resume().
 *	Subsystem-level @resume() is executed for all devices after invoking
 *	subsystem-level @resume_noirq() for all of them.
 *
 * @resume_early: Prepare to execute @resume().  For a number of devices
 *	@resume_early() may point to the same callback routine as the runtime
 *	resume callback.
 *
 * @freeze: Hibernation-specific, executed before creating a hibernation image.
 *	Analogous to @suspend(), but it should not enable the device to signal
 *	wakeup events or change its power state.  The majority of subsystems
 *	(with the notable exception of the PCI bus type) expect the driver-level
 *	@freeze() to save the device settings in memory to be used by @restore()
 *	during the subsequent resume from hibernation.
 *	Subsystem-level @freeze() is executed for all devices after invoking
 *	subsystem-level @prepare() for all of them.
 *
 * @freeze_late: Continue operations started by @freeze().  Analogous to
 *	@suspend_late(), but it should not enable the device to signal wakeup
 *	events or change its power state.
 *
 * @thaw: Hibernation-specific, executed after creating a hibernation image OR
 *	if the creation of an image has failed.  Also executed after a failing
 *	attempt to restore the contents of main memory from such an image.
 *	Undo the changes made by the preceding @freeze(), so the device can be
 *	operated in the same way as immediately before the call to @freeze().
 *	Subsystem-level @thaw() is executed for all devices after invoking
 *	subsystem-level @thaw_noirq() for all of them.  It also may be executed
 *	directly after @freeze() in case of a transition error.
 *
 * @thaw_early: Prepare to execute @thaw().  Undo the changes made by the
 *	preceding @freeze_late().
 *
 * @poweroff: Hibernation-specific, executed after saving a hibernation image.
 *	Analogous to @suspend(), but it need not save the device's settings in
 *	memory.
 *	Subsystem-level @poweroff() is executed for all devices after invoking
 *	subsystem-level @prepare() for all of them.
 *
 * @poweroff_late: Continue operations started by @poweroff().  Analogous to
 *	@suspend_late(), but it need not save the device's settings in memory.
 *
 * @restore: Hibernation-specific, executed after restoring the contents of main
 *	memory from a hibernation image, analogous to @resume().
 *
 * @restore_early: Prepare to execute @restore(), analogous to @resume_early().
 *
 * @suspend_noirq: Complete the actions started by @suspend().  Carry out any
 *	additional operations required for suspending the device that might be
 *	racing with its driver's interrupt handler, which is guaranteed not to
 *	run while @suspend_noirq() is being executed.
 *	It generally is expected that the device will be in a low-power state
 *	(appropriate for the target system sleep state) after subsystem-level
 *	@suspend_noirq() has returned successfully.  If the device can generate
 *	system wakeup signals and is enabled to wake up the system, it should be
 *	configured to do so at that time.  However, depending on the platform
 *	and device's subsystem, @suspend() or @suspend_late() may be allowed to
 *	put the device into the low-power state and configure it to generate
 *	wakeup signals, in which case it generally is not necessary to define
 *	@suspend_noirq().
 *
 * @resume_noirq: Prepare for the execution of @resume() by carrying out any
 *	operations required for resuming the device that might be racing with
 *	its driver's interrupt handler, which is guaranteed not to run while
 *	@resume_noirq() is being executed.
 *
 * @freeze_noirq: Complete the actions started by @freeze().  Carry out any
 *	additional operations required for freezing the device that might be
 *	racing with its driver's interrupt handler, which is guaranteed not to
 *	run while @freeze_noirq() is being executed.
 *	The power state of the device should not be changed by either @freeze(),
 *	or @freeze_late(), or @freeze_noirq() and it should not be configured to
 *	signal system wakeup by any of these callbacks.
 *
 * @thaw_noirq: Prepare for the execution of @thaw() by carrying out any
 *	operations required for thawing the device that might be racing with its
 *	driver's interrupt handler, which is guaranteed not to run while
 *	@thaw_noirq() is being executed.
 *
 * @poweroff_noirq: Complete the actions started by @poweroff().  Analogous to
 *	@suspend_noirq(), but it need not save the device's settings in memory.
 *
 * @restore_noirq: Prepare for the execution of @restore() by carrying out any
 *	operations required for thawing the device that might be racing with its
 *	driver's interrupt handler, which is guaranteed not to run while
 *	@restore_noirq() is being executed.  Analogous to @resume_noirq().
 *
 * @runtime_suspend: Prepare the device for a condition in which it won't be
 *	able to communicate with the CPU(s) and RAM due to power management.
 *	This need not mean that the device should be put into a low-power state.
 *	For example, if the device is behind a link which is about to be turned
 *	off, the device may remain at full power.  If the device does go to low
 *	power and is capable of generating runtime wakeup events, remote wakeup
 *	(i.e., a hardware mechanism allowing the device to request a change of
 *	its power state via an interrupt) should be enabled for it.
 *
 * @runtime_resume: Put the device into the fully active state in response to a
 *	wakeup event generated by hardware or at the request of software.  If
 *	necessary, put the device into the full-power state and restore its
 *	registers, so that it is fully operational.
 *
 * @runtime_idle: Device appears to be inactive and it might be put into a
 *	low-power state if all of the necessary conditions are satisfied.
 *	Check these conditions, and return 0 if it's appropriate to let the PM
 *	core queue a suspend request for the device.
 *
 * Several device power state transitions are externally visible, affecting
 * the state of pending I/O queues and (for drivers that touch hardware)
 * interrupts, wakeups, DMA, and other hardware state.  There may also be
 * internal transitions to various low-power modes which are transparent
 * to the rest of the driver stack (such as a driver that's ON gating off
 * clocks which are not in active use).
 *
 * The externally visible transitions are handled with the help of callbacks
 * included in this structure in such a way that, typically, two levels of
 * callbacks are involved.  First, the PM core executes callbacks provided by PM
 * domains, device types, classes and bus types.  They are the subsystem-level
 * callbacks expected to execute callbacks provided by device drivers, although
 * they may choose not to do that.  If the driver callbacks are executed, they
 * have to collaborate with the subsystem-level callbacks to achieve the goals
 * appropriate for the given system transition, given transition phase and the
 * subsystem the device belongs to.
 *
 * All of the above callbacks, except for @complete(), return error codes.
 * However, the error codes returned by @resume(), @thaw(), @restore(),
 * @resume_noirq(), @thaw_noirq(), and @restore_noirq(), do not cause the PM
 * core to abort the resume transition during which they are returned.  The
 * error codes returned in those cases are only printed to the system logs for
 * debugging purposes.  Still, it is recommended that drivers only return error
 * codes from their resume methods in case of an unrecoverable failure (i.e.
 * when the device being handled refuses to resume and becomes unusable) to
 * allow the PM core to be modified in the future, so that it can avoid
 * attempting to handle devices that failed to resume and their children.
 *
 * It is allowed to unregister devices while the above callbacks are being
 * executed.  However, a callback routine MUST NOT try to unregister the device
 * it was called for, although it may unregister children of that device (for
 * example, if it detects that a child was unplugged while the system was
 * asleep).
 *
 * There also are callbacks related to runtime power management of devices.
 * Again, as a rule these callbacks are executed by the PM core for subsystems
 * (PM domains, device types, classes and bus types) and the subsystem-level
 * callbacks are expected to invoke the driver callbacks.  Moreover, the exact
 * actions to be performed by a device driver's callbacks generally depend on
 * the platform and subsystem the device belongs to.
 *
 * Refer to Documentation/power/runtime_pm.rst for more information about the
 * role of the @runtime_suspend(), @runtime_resume() and @runtime_idle()
 * callbacks in device runtime power management.
 */
struct dev_pm_ops {
	int (*prepare)(struct device *dev);
	void (*complete)(struct device *dev);
	int (*suspend)(struct device *dev);
	int (*resume)(struct device *dev);
	int (*freeze)(struct device *dev);
	int (*thaw)(struct device *dev);
	int (*poweroff)(struct device *dev);
	int (*restore)(struct device *dev);
	int (*suspend_late)(struct device *dev);
	int (*resume_early)(struct device *dev);
	int (*freeze_late)(struct device *dev);
	int (*thaw_early)(struct device *dev);
	int (*poweroff_late)(struct device *dev);
	int (*restore_early)(struct device *dev);
	int (*suspend_noirq)(struct device *dev);
	int (*resume_noirq)(struct device *dev);
	int (*freeze_noirq)(struct device *dev);
	int (*thaw_noirq)(struct device *dev);
	int (*poweroff_noirq)(struct device *dev);
	int (*restore_noirq)(struct device *dev);
	int (*runtime_suspend)(struct device *dev);
	int (*runtime_resume)(struct device *dev);
	int (*runtime_idle)(struct device *dev);
};

#define SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	.suspend = pm_sleep_ptr(suspend_fn), \
	.resume = pm_sleep_ptr(resume_fn), \
	.freeze = pm_sleep_ptr(suspend_fn), \
	.thaw = pm_sleep_ptr(resume_fn), \
	.poweroff = pm_sleep_ptr(suspend_fn), \
	.restore = pm_sleep_ptr(resume_fn),

#define LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	.suspend_late = pm_sleep_ptr(suspend_fn), \
	.resume_early = pm_sleep_ptr(resume_fn), \
	.freeze_late = pm_sleep_ptr(suspend_fn), \
	.thaw_early = pm_sleep_ptr(resume_fn), \
	.poweroff_late = pm_sleep_ptr(suspend_fn), \
	.restore_early = pm_sleep_ptr(resume_fn),

#define NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	.suspend_noirq = pm_sleep_ptr(suspend_fn), \
	.resume_noirq = pm_sleep_ptr(resume_fn), \
	.freeze_noirq = pm_sleep_ptr(suspend_fn), \
	.thaw_noirq = pm_sleep_ptr(resume_fn), \
	.poweroff_noirq = pm_sleep_ptr(suspend_fn), \
	.restore_noirq = pm_sleep_ptr(resume_fn),

#define RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	.runtime_suspend = suspend_fn, \
	.runtime_resume = resume_fn, \
	.runtime_idle = idle_fn,

#ifdef CONFIG_PM_SLEEP
#define SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#else
#define SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#endif

#ifdef CONFIG_PM_SLEEP
#define SET_LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#else
#define SET_LATE_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#endif

#ifdef CONFIG_PM_SLEEP
#define SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#else
#define SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn)
#endif

#ifdef CONFIG_PM
#define SET_RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
	RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn)
#else
#define SET_RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn)
#endif

#define _DEFINE_DEV_PM_OPS(name, \
			   suspend_fn, resume_fn, \
			   runtime_suspend_fn, runtime_resume_fn, idle_fn) \
const struct dev_pm_ops name = { \
	SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	RUNTIME_PM_OPS(runtime_suspend_fn, runtime_resume_fn, idle_fn) \
}

#define _EXPORT_PM_OPS(name, license, ns)				\
	const struct dev_pm_ops name;					\
	__EXPORT_SYMBOL(name, license, ns);				\
	const struct dev_pm_ops name

#define _DISCARD_PM_OPS(name, license, ns)				\
	static __maybe_unused const struct dev_pm_ops __static_##name

#ifdef CONFIG_PM
#define _EXPORT_DEV_PM_OPS(name, license, ns)		_EXPORT_PM_OPS(name, license, ns)
#define EXPORT_PM_FN_GPL(name)				EXPORT_SYMBOL_GPL(name)
#define EXPORT_PM_FN_NS_GPL(name, ns)			EXPORT_SYMBOL_NS_GPL(name, ns)
#else
#define _EXPORT_DEV_PM_OPS(name, license, ns)		_DISCARD_PM_OPS(name, license, ns)
#define EXPORT_PM_FN_GPL(name)
#define EXPORT_PM_FN_NS_GPL(name, ns)
#endif

#ifdef CONFIG_PM_SLEEP
#define _EXPORT_DEV_SLEEP_PM_OPS(name, license, ns)	_EXPORT_PM_OPS(name, license, ns)
#else
#define _EXPORT_DEV_SLEEP_PM_OPS(name, license, ns)	_DISCARD_PM_OPS(name, license, ns)
#endif

#define EXPORT_DEV_PM_OPS(name)				_EXPORT_DEV_PM_OPS(name, "", "")
#define EXPORT_GPL_DEV_PM_OPS(name)			_EXPORT_DEV_PM_OPS(name, "GPL", "")
#define EXPORT_NS_DEV_PM_OPS(name, ns)			_EXPORT_DEV_PM_OPS(name, "", #ns)
#define EXPORT_NS_GPL_DEV_PM_OPS(name, ns)		_EXPORT_DEV_PM_OPS(name, "GPL", #ns)

#define EXPORT_DEV_SLEEP_PM_OPS(name)			_EXPORT_DEV_SLEEP_PM_OPS(name, "", "")
#define EXPORT_GPL_DEV_SLEEP_PM_OPS(name)		_EXPORT_DEV_SLEEP_PM_OPS(name, "GPL", "")
#define EXPORT_NS_DEV_SLEEP_PM_OPS(name, ns)		_EXPORT_DEV_SLEEP_PM_OPS(name, "", #ns)
#define EXPORT_NS_GPL_DEV_SLEEP_PM_OPS(name, ns)	_EXPORT_DEV_SLEEP_PM_OPS(name, "GPL", #ns)

/*
 * Use this if you want to use the same suspend and resume callbacks for suspend
 * to RAM and hibernation.
 *
 * If the underlying dev_pm_ops struct symbol has to be exported, use
 * EXPORT_SIMPLE_DEV_PM_OPS() or EXPORT_GPL_SIMPLE_DEV_PM_OPS() instead.
 */
#define DEFINE_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
	_DEFINE_DEV_PM_OPS(name, suspend_fn, resume_fn, NULL, NULL, NULL)

#define EXPORT_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
	EXPORT_DEV_SLEEP_PM_OPS(name) = { \
		SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	}
#define EXPORT_GPL_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
	EXPORT_GPL_DEV_SLEEP_PM_OPS(name) = { \
		SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	}
#define EXPORT_NS_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn, ns)	\
	EXPORT_NS_DEV_SLEEP_PM_OPS(name, ns) = { \
		SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	}
#define EXPORT_NS_GPL_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn, ns)	\
	EXPORT_NS_GPL_DEV_SLEEP_PM_OPS(name, ns) = { \
		SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	}

/* Deprecated. Use DEFINE_SIMPLE_DEV_PM_OPS() instead. */
#define SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn) \
const struct dev_pm_ops __maybe_unused name = { \
	SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
}

/*
 * Use this for defining a set of PM operations to be used in all situations
 * (system suspend, hibernation or runtime PM).
 * NOTE: In general, system suspend callbacks, .suspend() and .resume(), should
 * be different from the corresponding runtime PM callbacks, .runtime_suspend(),
 * and .runtime_resume(), because .runtime_suspend() always works on an already
 * quiescent device, while .suspend() should assume that the device may be doing
 * something when it is called (it should ensure that the device will be
 * quiescent after it has returned).  Therefore it's better to point the "late"
 * suspend and "early" resume callback pointers, .suspend_late() and
 * .resume_early(), to the same routines as .runtime_suspend() and
 * .runtime_resume(), respectively (and analogously for hibernation).
 *
 * Deprecated. You most likely don't want this macro. Use
 * DEFINE_RUNTIME_DEV_PM_OPS() instead.
 */
#define UNIVERSAL_DEV_PM_OPS(name, suspend_fn, resume_fn, idle_fn) \
const struct dev_pm_ops __maybe_unused name = { \
	SET_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
	SET_RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn) \
}

/*
 * Use this if you want to have the suspend and resume callbacks be called
 * with IRQs disabled.
 */
#define DEFINE_NOIRQ_DEV_PM_OPS(name, suspend_fn, resume_fn) \
const struct dev_pm_ops name = { \
	NOIRQ_SYSTEM_SLEEP_PM_OPS(suspend_fn, resume_fn) \
}

#define pm_ptr(_ptr) PTR_IF(IS_ENABLED(CONFIG_PM), (_ptr))
#define pm_sleep_ptr(_ptr) PTR_IF(IS_ENABLED(CONFIG_PM_SLEEP), (_ptr))

/*
 * PM_EVENT_ messages
 *
 * The following PM_EVENT_ messages are defined for the internal use of the PM
 * core, in order to provide a mechanism allowing the high level suspend and
 * hibernation code to convey the necessary information to the device PM core
 * code:
 *
 * ON		No transition.
 *
 * FREEZE	System is going to hibernate, call ->prepare() and ->freeze()
 *		for all devices.
 *
 * SUSPEND	System is going to suspend, call ->prepare() and ->suspend()
 *		for all devices.
 *
 * HIBERNATE	Hibernation image has been saved, call ->prepare() and
 *		->poweroff() for all devices.
 *
 * QUIESCE	Contents of main memory are going to be restored from a (loaded)
 *		hibernation image, call ->prepare() and ->freeze() for all
 *		devices.
 *
 * RESUME	System is resuming, call ->resume() and ->complete() for all
 *		devices.
 *
 * THAW		Hibernation image has been created, call ->thaw() and
 *		->complete() for all devices.
 *
 * RESTORE	Contents of main memory have been restored from a hibernation
 *		image, call ->restore() and ->complete() for all devices.
 *
 * RECOVER	Creation of a hibernation image or restoration of the main
 *		memory contents from a hibernation image has failed, call
 *		->thaw() and ->complete() for all devices.
 *
 * The following PM_EVENT_ messages are defined for internal use by
 * kernel subsystems.  They are never issued by the PM core.
 *
 * USER_SUSPEND		Manual selective suspend was issued by userspace.
 *
 * USER_RESUME		Manual selective resume was issued by userspace.
 *
 * REMOTE_WAKEUP	Remote-wakeup request was received from the device.
 *
 * AUTO_SUSPEND		Automatic (device idle) runtime suspend was
 *			initiated by the subsystem.
 *
 * AUTO_RESUME		Automatic (device needed) runtime resume was
 *			requested by a driver.
 */

#define PM_EVENT_INVALID	(-1)
#define PM_EVENT_ON		0x0000
#define PM_EVENT_FREEZE		0x0001
#define PM_EVENT_SUSPEND	0x0002
#define PM_EVENT_HIBERNATE	0x0004
#define PM_EVENT_QUIESCE	0x0008
#define PM_EVENT_RESUME		0x0010
#define PM_EVENT_THAW		0x0020
#define PM_EVENT_RESTORE	0x0040
#define PM_EVENT_RECOVER	0x0080
#define PM_EVENT_USER		0x0100
#define PM_EVENT_REMOTE		0x0200
#define PM_EVENT_AUTO		0x0400

#define PM_EVENT_SLEEP		(PM_EVENT_SUSPEND | PM_EVENT_HIBERNATE)
#define PM_EVENT_USER_SUSPEND	(PM_EVENT_USER | PM_EVENT_SUSPEND)
#define PM_EVENT_USER_RESUME	(PM_EVENT_USER | PM_EVENT_RESUME)
#define PM_EVENT_REMOTE_RESUME	(PM_EVENT_REMOTE | PM_EVENT_RESUME)
#define PM_EVENT_AUTO_SUSPEND	(PM_EVENT_AUTO | PM_EVENT_SUSPEND)
#define PM_EVENT_AUTO_RESUME	(PM_EVENT_AUTO | PM_EVENT_RESUME)

#define PMSG_INVALID	((struct pm_message){ .event = PM_EVENT_INVALID, })
#define PMSG_ON		((struct pm_message){ .event = PM_EVENT_ON, })
#define PMSG_FREEZE	((struct pm_message){ .event = PM_EVENT_FREEZE, })
#define PMSG_QUIESCE	((struct pm_message){ .event = PM_EVENT_QUIESCE, })
#define PMSG_SUSPEND	((struct pm_message){ .event = PM_EVENT_SUSPEND, })
#define PMSG_HIBERNATE	((struct pm_message){ .event = PM_EVENT_HIBERNATE, })
#define PMSG_RESUME	((struct pm_message){ .event = PM_EVENT_RESUME, })
#define PMSG_THAW	((struct pm_message){ .event = PM_EVENT_THAW, })
#define PMSG_RESTORE	((struct pm_message){ .event = PM_EVENT_RESTORE, })
#define PMSG_RECOVER	((struct pm_message){ .event = PM_EVENT_RECOVER, })
#define PMSG_USER_SUSPEND	((struct pm_message) \
					{ .event = PM_EVENT_USER_SUSPEND, })
#define PMSG_USER_RESUME	((struct pm_message) \
					{ .event = PM_EVENT_USER_RESUME, })
#define PMSG_REMOTE_RESUME	((struct pm_message) \
					{ .event = PM_EVENT_REMOTE_RESUME, })
#define PMSG_AUTO_SUSPEND	((struct pm_message) \
					{ .event = PM_EVENT_AUTO_SUSPEND, })
#define PMSG_AUTO_RESUME	((struct pm_message) \
					{ .event = PM_EVENT_AUTO_RESUME, })

#define PMSG_IS_AUTO(msg)	(((msg).event & PM_EVENT_AUTO) != 0)

/*
 * Device run-time power management status.
 *
 * These status labels are used internally by the PM core to indicate the
 * current status of a device with respect to the PM core operations.  They do
 * not reflect the actual power state of the device or its status as seen by the
 * driver.
 *
 * RPM_ACTIVE		Device is fully operational.  Indicates that the device
 *			bus type's ->runtime_resume() callback has completed
 *			successfully.
 *
 * RPM_SUSPENDED	Device bus type's ->runtime_suspend() callback has
 *			completed successfully.  The device is regarded as
 *			suspended.
 *
 * RPM_RESUMING		Device bus type's ->runtime_resume() callback is being
 *			executed.
 *
 * RPM_SUSPENDING	Device bus type's ->runtime_suspend() callback is being
 *			executed.
 */

enum rpm_status {
	RPM_INVALID = -1,
	RPM_ACTIVE = 0,
	RPM_RESUMING,
	RPM_SUSPENDED,
	RPM_SUSPENDING,
};

/*
 * Device run-time power management request types.
 *
 * RPM_REQ_NONE		Do nothing.
 *
 * RPM_REQ_IDLE		Run the device bus type's ->runtime_idle() callback
 *
 * RPM_REQ_SUSPEND	Run the device bus type's ->runtime_suspend() callback
 *
 * RPM_REQ_AUTOSUSPEND	Same as RPM_REQ_SUSPEND, but not until the device has
 *			been inactive for as long as power.autosuspend_delay
 *
 * RPM_REQ_RESUME	Run the device bus type's ->runtime_resume() callback
 */

enum rpm_request {
	RPM_REQ_NONE = 0,
	RPM_REQ_IDLE,
	RPM_REQ_SUSPEND,
	RPM_REQ_AUTOSUSPEND,
	RPM_REQ_RESUME,
};

struct wakeup_source;
struct wake_irq;
struct pm_domain_data;

struct pm_subsys_data {
	spinlock_t lock;
	unsigned int refcount;
#ifdef CONFIG_PM_CLK
	unsigned int clock_op_might_sleep;
	struct mutex clock_mutex;
	struct list_head clock_list;
#endif
#ifdef CONFIG_PM_GENERIC_DOMAINS
	struct pm_domain_data *domain_data;
#endif
};

/*
 * Driver flags to control system suspend/resume behavior.
 *
 * These flags can be set by device drivers at the probe time.  They need not be
 * cleared by the drivers as the driver core will take care of that.
 *
 * NO_DIRECT_COMPLETE: Do not apply direct-complete optimization to the device.
 * SMART_PREPARE: Take the driver ->prepare callback return value into account.
 * SMART_SUSPEND: Avoid resuming the device from runtime suspend.
 * MAY_SKIP_RESUME: Allow driver "noirq" and "early" callbacks to be skipped.
 *
 * See Documentation/driver-api/pm/devices.rst for details.
 */
#define DPM_FLAG_NO_DIRECT_COMPLETE	BIT(0)
#define DPM_FLAG_SMART_PREPARE		BIT(1)
#define DPM_FLAG_SMART_SUSPEND		BIT(2)
#define DPM_FLAG_MAY_SKIP_RESUME	BIT(3)

struct dev_pm_info {
	pm_message_t		power_state;
	unsigned int		can_wakeup:1;
	unsigned int		async_suspend:1;
	bool			in_dpm_list:1;	/* Owned by the PM core */
	bool			is_prepared:1;	/* Owned by the PM core */
	bool			is_suspended:1;	/* Ditto */
	bool			is_noirq_suspended:1;
	bool			is_late_suspended:1;
	bool			no_pm:1;
	bool			early_init:1;	/* Owned by the PM core */
	bool			direct_complete:1;	/* Owned by the PM core */
	u32			driver_flags;
	spinlock_t		lock;
#ifdef CONFIG_PM_SLEEP
	struct list_head	entry;
	struct completion	completion;
	struct wakeup_source	*wakeup;
	bool			wakeup_path:1;
	bool			syscore:1;
	bool			no_pm_callbacks:1;	/* Owned by the PM core */
	unsigned int		must_resume:1;	/* Owned by the PM core */
	unsigned int		may_skip_resume:1;	/* Set by subsystems */
#else
	unsigned int		should_wakeup:1;
#endif
#ifdef CONFIG_PM
	struct hrtimer		suspend_timer;
	u64			timer_expires;
	struct work_struct	work;
	wait_queue_head_t	wait_queue;
	struct wake_irq		*wakeirq;
	atomic_t		usage_count;
	atomic_t		child_count;
	unsigned int		disable_depth:3;
	unsigned int		idle_notification:1;
	unsigned int		request_pending:1;
	unsigned int		deferred_resume:1;
	unsigned int		needs_force_resume:1;
	unsigned int		runtime_auto:1;
	bool			ignore_children:1;
	unsigned int		no_callbacks:1;
	unsigned int		irq_safe:1;
	unsigned int		use_autosuspend:1;
	unsigned int		timer_autosuspends:1;
	unsigned int		memalloc_noio:1;
	unsigned int		links_count;
	enum rpm_request	request;
	enum rpm_status		runtime_status;
	enum rpm_status		last_status;
	int			runtime_error;
	int			autosuspend_delay;
	u64			last_busy;
	u64			active_time;
	u64			suspended_time;
	u64			accounting_timestamp;
#endif
	struct pm_subsys_data	*subsys_data;  /* Owned by the subsystem. */
	void (*set_latency_tolerance)(struct device *, s32);
	struct dev_pm_qos	*qos;
};

extern int dev_pm_get_subsys_data(struct device *dev);
extern void dev_pm_put_subsys_data(struct device *dev);

/**
 * struct dev_pm_domain - power management domain representation.
 *
 * @ops: Power management operations associated with this domain.
 * @start: Called when a user needs to start the device via the domain.
 * @detach: Called when removing a device from the domain.
 * @activate: Called before executing probe routines for bus types and drivers.
 * @sync: Called after successful driver probe.
 * @dismiss: Called after unsuccessful driver probe and after driver removal.
 * @set_performance_state: Called to request a new performance state.
 *
 * Power domains provide callbacks that are executed during system suspend,
 * hibernation, system resume and during runtime PM transitions instead of
 * subsystem-level and driver-level callbacks.
 */
struct dev_pm_domain {
	struct dev_pm_ops	ops;
	int (*start)(struct device *dev);
	void (*detach)(struct device *dev, bool power_off);
	int (*activate)(struct device *dev);
	void (*sync)(struct device *dev);
	void (*dismiss)(struct device *dev);
	int (*set_performance_state)(struct device *dev, unsigned int state);
};

/*
 * The PM_EVENT_ messages are also used by drivers implementing the legacy
 * suspend framework, based on the ->suspend() and ->resume() callbacks common
 * for suspend and hibernation transitions, according to the rules below.
 */

/* Necessary, because several drivers use PM_EVENT_PRETHAW */
#define PM_EVENT_PRETHAW PM_EVENT_QUIESCE

/*
 * One transition is triggered by resume(), after a suspend() call; the
 * message is implicit:
 *
 * ON		Driver starts working again, responding to hardware events
 *		and software requests.  The hardware may have gone through
 *		a power-off reset, or it may have maintained state from the
 *		previous suspend() which the driver will rely on while
 *		resuming.  On most platforms, there are no restrictions on
 *		availability of resources like clocks during resume().
 *
 * Other transitions are triggered by messages sent using suspend().  All
 * these transitions quiesce the driver, so that I/O queues are inactive.
 * That commonly entails turning off IRQs and DMA; there may be rules
 * about how to quiesce that are specific to the bus or the device's type.
 * (For example, network drivers mark the link state.)  Other details may
 * differ according to the message:
 *
 * SUSPEND	Quiesce, enter a low power device state appropriate for
 *		the upcoming system state (such as PCI_D3hot), and enable
 *		wakeup events as appropriate.
 *
 * HIBERNATE	Enter a low power device state appropriate for the hibernation
 *		state (eg. ACPI S4) and enable wakeup events as appropriate.
 *
 * FREEZE	Quiesce operations so that a consistent image can be saved;
 *		but do NOT otherwise enter a low power device state, and do
 *		NOT emit system wakeup events.
 *
 * PRETHAW	Quiesce as if for FREEZE; additionally, prepare for restoring
 *		the system from a snapshot taken after an earlier FREEZE.
 *		Some drivers will need to reset their hardware state instead
 *		of preserving it, to ensure that it's never mistaken for the
 *		state which that earlier snapshot had set up.
 *
 * A minimally power-aware driver treats all messages as SUSPEND, fully
 * reinitializes its device during resume() -- whether or not it was reset
 * during the suspend/resume cycle -- and can't issue wakeup events.
 *
 * More power-aware drivers may also use low power states at runtime as
 * well as during system sleep states like PM_SUSPEND_STANDBY.  They may
 * be able to use wakeup events to exit from runtime low-power states,
 * or from system low-power states such as standby or suspend-to-RAM.
 */

#ifdef CONFIG_PM_SLEEP
extern void device_pm_lock(void);
extern void dpm_resume_start(pm_message_t state);
extern void dpm_resume_end(pm_message_t state);
extern void dpm_resume_noirq(pm_message_t state);
extern void dpm_resume_early(pm_message_t state);
extern void dpm_resume(pm_message_t state);
extern void dpm_complete(pm_message_t state);

extern void device_pm_unlock(void);
extern int dpm_suspend_end(pm_message_t state);
extern int dpm_suspend_start(pm_message_t state);
extern int dpm_suspend_noirq(pm_message_t state);
extern int dpm_suspend_late(pm_message_t state);
extern int dpm_suspend(pm_message_t state);
extern int dpm_prepare(pm_message_t state);

extern void __suspend_report_result(const char *function, struct device *dev, void *fn, int ret);

#define suspend_report_result(dev, fn, ret)				\
	do {								\
		__suspend_report_result(__func__, dev, fn, ret);	\
	} while (0)

extern int device_pm_wait_for_dev(struct device *sub, struct device *dev);
extern void dpm_for_each_dev(void *data, void (*fn)(struct device *, void *));

extern int pm_generic_prepare(struct device *dev);
extern int pm_generic_suspend_late(struct device *dev);
extern int pm_generic_suspend_noirq(struct device *dev);
extern int pm_generic_suspend(struct device *dev);
extern int pm_generic_resume_early(struct device *dev);
extern int pm_generic_resume_noirq(struct device *dev);
extern int pm_generic_resume(struct device *dev);
extern int pm_generic_freeze_noirq(struct device *dev);
extern int pm_generic_freeze_late(struct device *dev);
extern int pm_generic_freeze(struct device *dev);
extern int pm_generic_thaw_noirq(struct device *dev);
extern int pm_generic_thaw_early(struct device *dev);
extern int pm_generic_thaw(struct device *dev);
extern int pm_generic_restore_noirq(struct device *dev);
extern int pm_generic_restore_early(struct device *dev);
extern int pm_generic_restore(struct device *dev);
extern int pm_generic_poweroff_noirq(struct device *dev);
extern int pm_generic_poweroff_late(struct device *dev);
extern int pm_generic_poweroff(struct device *dev);
extern void pm_generic_complete(struct device *dev);

extern bool dev_pm_skip_resume(struct device *dev);
extern bool dev_pm_skip_suspend(struct device *dev);

#else /* !CONFIG_PM_SLEEP */

#define device_pm_lock() do {} while (0)
#define device_pm_unlock() do {} while (0)

static inline int dpm_suspend_start(pm_message_t state)
{
	return 0;
}

#define suspend_report_result(dev, fn, ret)	do {} while (0)

static inline int device_pm_wait_for_dev(struct device *a, struct device *b)
{
	return 0;
}

static inline void dpm_for_each_dev(void *data, void (*fn)(struct device *, void *))
{
}

#define pm_generic_prepare		NULL
#define pm_generic_suspend_late		NULL
#define pm_generic_suspend_noirq	NULL
#define pm_generic_suspend		NULL
#define pm_generic_resume_early		NULL
#define pm_generic_resume_noirq		NULL
#define pm_generic_resume		NULL
#define pm_generic_freeze_noirq		NULL
#define pm_generic_freeze_late		NULL
#define pm_generic_freeze		NULL
#define pm_generic_thaw_noirq		NULL
#define pm_generic_thaw_early		NULL
#define pm_generic_thaw			NULL
#define pm_generic_restore_noirq	NULL
#define pm_generic_restore_early	NULL
#define pm_generic_restore		NULL
#define pm_generic_poweroff_noirq	NULL
#define pm_generic_poweroff_late	NULL
#define pm_generic_poweroff		NULL
#define pm_generic_complete		NULL
#endif /* !CONFIG_PM_SLEEP */

/* How to reorder dpm_list after device_move() */
enum dpm_order {
	DPM_ORDER_NONE,
	DPM_ORDER_DEV_AFTER_PARENT,
	DPM_ORDER_PARENT_BEFORE_DEV,
	DPM_ORDER_DEV_LAST,
};

#endif /* _LINUX_PM_H */
