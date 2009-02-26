/*
 *  pm.h - Power management interface
 *
 *  Copyright (C) 2000 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_PM_H
#define _LINUX_PM_H

#include <linux/list.h>

/*
 * Callbacks for platform drivers to implement.
 */
extern void (*pm_idle)(void);
extern void (*pm_power_off)(void);
extern void (*pm_power_off_prepare)(void);

/*
 * Device power management
 */

struct device;

typedef struct pm_message {
	int event;
} pm_message_t;

/**
 * struct dev_pm_ops - device PM callbacks
 *
 * Several driver power state transitions are externally visible, affecting
 * the state of pending I/O queues and (for drivers that touch hardware)
 * interrupts, wakeups, DMA, and other hardware state.  There may also be
 * internal transitions to various low power modes, which are transparent
 * to the rest of the driver stack (such as a driver that's ON gating off
 * clocks which are not in active use).
 *
 * The externally visible transitions are handled with the help of the following
 * callbacks included in this structure:
 *
 * @prepare: Prepare the device for the upcoming transition, but do NOT change
 *	its hardware state.  Prevent new children of the device from being
 *	registered after @prepare() returns (the driver's subsystem and
 *	generally the rest of the kernel is supposed to prevent new calls to the
 *	probe method from being made too once @prepare() has succeeded).  If
 *	@prepare() detects a situation it cannot handle (e.g. registration of a
 *	child already in progress), it may return -EAGAIN, so that the PM core
 *	can execute it once again (e.g. after the new child has been registered)
 *	to recover from the race condition.  This method is executed for all
 *	kinds of suspend transitions and is followed by one of the suspend
 *	callbacks: @suspend(), @freeze(), or @poweroff().
 *	The PM core executes @prepare() for all devices before starting to
 *	execute suspend callbacks for any of them, so drivers may assume all of
 *	the other devices to be present and functional while @prepare() is being
 *	executed.  In particular, it is safe to make GFP_KERNEL memory
 *	allocations from within @prepare().  However, drivers may NOT assume
 *	anything about the availability of the user space at that time and it
 *	is not correct to request firmware from within @prepare() (it's too
 *	late to do that).  [To work around this limitation, drivers may
 *	register suspend and hibernation notifiers that are executed before the
 *	freezing of tasks.]
 *
 * @complete: Undo the changes made by @prepare().  This method is executed for
 *	all kinds of resume transitions, following one of the resume callbacks:
 *	@resume(), @thaw(), @restore().  Also called if the state transition
 *	fails before the driver's suspend callback (@suspend(), @freeze(),
 *	@poweroff()) can be executed (e.g. if the suspend callback fails for one
 *	of the other devices that the PM core has unsuccessfully attempted to
 *	suspend earlier).
 *	The PM core executes @complete() after it has executed the appropriate
 *	resume callback for all devices.
 *
 * @suspend: Executed before putting the system into a sleep state in which the
 *	contents of main memory are preserved.  Quiesce the device, put it into
 *	a low power state appropriate for the upcoming system state (such as
 *	PCI_D3hot), and enable wakeup events as appropriate.
 *
 * @resume: Executed after waking the system up from a sleep state in which the
 *	contents of main memory were preserved.  Put the device into the
 *	appropriate state, according to the information saved in memory by the
 *	preceding @suspend().  The driver starts working again, responding to
 *	hardware events and software requests.  The hardware may have gone
 *	through a power-off reset, or it may have maintained state from the
 *	previous suspend() which the driver may rely on while resuming.  On most
 *	platforms, there are no restrictions on availability of resources like
 *	clocks during @resume().
 *
 * @freeze: Hibernation-specific, executed before creating a hibernation image.
 *	Quiesce operations so that a consistent image can be created, but do NOT
 *	otherwise put the device into a low power device state and do NOT emit
 *	system wakeup events.  Save in main memory the device settings to be
 *	used by @restore() during the subsequent resume from hibernation or by
 *	the subsequent @thaw(), if the creation of the image or the restoration
 *	of main memory contents from it fails.
 *
 * @thaw: Hibernation-specific, executed after creating a hibernation image OR
 *	if the creation of the image fails.  Also executed after a failing
 *	attempt to restore the contents of main memory from such an image.
 *	Undo the changes made by the preceding @freeze(), so the device can be
 *	operated in the same way as immediately before the call to @freeze().
 *
 * @poweroff: Hibernation-specific, executed after saving a hibernation image.
 *	Quiesce the device, put it into a low power state appropriate for the
 *	upcoming system state (such as PCI_D3hot), and enable wakeup events as
 *	appropriate.
 *
 * @restore: Hibernation-specific, executed after restoring the contents of main
 *	memory from a hibernation image.  Driver starts working again,
 *	responding to hardware events and software requests.  Drivers may NOT
 *	make ANY assumptions about the hardware state right prior to @restore().
 *	On most platforms, there are no restrictions on availability of
 *	resources like clocks during @restore().
 *
 * @suspend_noirq: Complete the operations of ->suspend() by carrying out any
 *	actions required for suspending the device that need interrupts to be
 *	disabled
 *
 * @resume_noirq: Prepare for the execution of ->resume() by carrying out any
 *	actions required for resuming the device that need interrupts to be
 *	disabled
 *
 * @freeze_noirq: Complete the operations of ->freeze() by carrying out any
 *	actions required for freezing the device that need interrupts to be
 *	disabled
 *
 * @thaw_noirq: Prepare for the execution of ->thaw() by carrying out any
 *	actions required for thawing the device that need interrupts to be
 *	disabled
 *
 * @poweroff_noirq: Complete the operations of ->poweroff() by carrying out any
 *	actions required for handling the device that need interrupts to be
 *	disabled
 *
 * @restore_noirq: Prepare for the execution of ->restore() by carrying out any
 *	actions required for restoring the operations of the device that need
 *	interrupts to be disabled
 *
 * All of the above callbacks, except for @complete(), return error codes.
 * However, the error codes returned by the resume operations, @resume(),
 * @thaw(), @restore(), @resume_noirq(), @thaw_noirq(), and @restore_noirq() do
 * not cause the PM core to abort the resume transition during which they are
 * returned.  The error codes returned in that cases are only printed by the PM
 * core to the system logs for debugging purposes.  Still, it is recommended
 * that drivers only return error codes from their resume methods in case of an
 * unrecoverable failure (i.e. when the device being handled refuses to resume
 * and becomes unusable) to allow us to modify the PM core in the future, so
 * that it can avoid attempting to handle devices that failed to resume and
 * their children.
 *
 * It is allowed to unregister devices while the above callbacks are being
 * executed.  However, it is not allowed to unregister a device from within any
 * of its own callbacks.
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
	int (*suspend_noirq)(struct device *dev);
	int (*resume_noirq)(struct device *dev);
	int (*freeze_noirq)(struct device *dev);
	int (*thaw_noirq)(struct device *dev);
	int (*poweroff_noirq)(struct device *dev);
	int (*restore_noirq)(struct device *dev);
};

/**
 * PM_EVENT_ messages
 *
 * The following PM_EVENT_ messages are defined for the internal use of the PM
 * core, in order to provide a mechanism allowing the high level suspend and
 * hibernation code to convey the necessary information to the device PM core
 * code:
 *
 * ON		No transition.
 *
 * FREEZE 	System is going to hibernate, call ->prepare() and ->freeze()
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

#define PM_EVENT_ON		0x0000
#define PM_EVENT_FREEZE 	0x0001
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

/**
 * Device power management states
 *
 * These state labels are used internally by the PM core to indicate the current
 * status of a device with respect to the PM core operations.
 *
 * DPM_ON		Device is regarded as operational.  Set this way
 *			initially and when ->complete() is about to be called.
 *			Also set when ->prepare() fails.
 *
 * DPM_PREPARING	Device is going to be prepared for a PM transition.  Set
 *			when ->prepare() is about to be called.
 *
 * DPM_RESUMING		Device is going to be resumed.  Set when ->resume(),
 *			->thaw(), or ->restore() is about to be called.
 *
 * DPM_SUSPENDING	Device has been prepared for a power transition.  Set
 *			when ->prepare() has just succeeded.
 *
 * DPM_OFF		Device is regarded as inactive.  Set immediately after
 *			->suspend(), ->freeze(), or ->poweroff() has succeeded.
 *			Also set when ->resume()_noirq, ->thaw_noirq(), or
 *			->restore_noirq() is about to be called.
 *
 * DPM_OFF_IRQ		Device is in a "deep sleep".  Set immediately after
 *			->suspend_noirq(), ->freeze_noirq(), or
 *			->poweroff_noirq() has just succeeded.
 */

enum dpm_state {
	DPM_INVALID,
	DPM_ON,
	DPM_PREPARING,
	DPM_RESUMING,
	DPM_SUSPENDING,
	DPM_OFF,
	DPM_OFF_IRQ,
};

struct dev_pm_info {
	pm_message_t		power_state;
	unsigned		can_wakeup:1;
	unsigned		should_wakeup:1;
	enum dpm_state		status;		/* Owned by the PM core */
#ifdef	CONFIG_PM_SLEEP
	struct list_head	entry;
#endif
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
 * 		and software requests.  The hardware may have gone through
 * 		a power-off reset, or it may have maintained state from the
 * 		previous suspend() which the driver will rely on while
 * 		resuming.  On most platforms, there are no restrictions on
 * 		availability of resources like clocks during resume().
 *
 * Other transitions are triggered by messages sent using suspend().  All
 * these transitions quiesce the driver, so that I/O queues are inactive.
 * That commonly entails turning off IRQs and DMA; there may be rules
 * about how to quiesce that are specific to the bus or the device's type.
 * (For example, network drivers mark the link state.)  Other details may
 * differ according to the message:
 *
 * SUSPEND	Quiesce, enter a low power device state appropriate for
 * 		the upcoming system state (such as PCI_D3hot), and enable
 * 		wakeup events as appropriate.
 *
 * HIBERNATE	Enter a low power device state appropriate for the hibernation
 * 		state (eg. ACPI S4) and enable wakeup events as appropriate.
 *
 * FREEZE	Quiesce operations so that a consistent image can be saved;
 * 		but do NOT otherwise enter a low power device state, and do
 * 		NOT emit system wakeup events.
 *
 * PRETHAW	Quiesce as if for FREEZE; additionally, prepare for restoring
 * 		the system from a snapshot taken after an earlier FREEZE.
 * 		Some drivers will need to reset their hardware state instead
 * 		of preserving it, to ensure that it's never mistaken for the
 * 		state which that earlier snapshot had set up.
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
extern int sysdev_resume(void);
extern void device_power_up(pm_message_t state);
extern void device_resume(pm_message_t state);

extern void device_pm_unlock(void);
extern int sysdev_suspend(pm_message_t state);
extern int device_power_down(pm_message_t state);
extern int device_suspend(pm_message_t state);
extern int device_prepare_suspend(pm_message_t state);

extern void __suspend_report_result(const char *function, void *fn, int ret);

#define suspend_report_result(fn, ret)					\
	do {								\
		__suspend_report_result(__func__, fn, ret);		\
	} while (0)

#else /* !CONFIG_PM_SLEEP */

static inline int device_suspend(pm_message_t state)
{
	return 0;
}

#define suspend_report_result(fn, ret)		do {} while (0)

#endif /* !CONFIG_PM_SLEEP */

/*
 * Global Power Management flags
 * Used to keep APM and ACPI from both being active
 */
extern unsigned int	pm_flags;

#define PM_APM	1
#define PM_ACPI	2

#endif /* _LINUX_PM_H */
