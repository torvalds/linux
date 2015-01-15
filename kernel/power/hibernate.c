/*
 * kernel/power/hibernate.c - Hibernation (a.k.a suspend-to-disk) support.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2004 Pavel Machek <pavel@ucw.cz>
 * Copyright (c) 2009 Rafael J. Wysocki, Novell Inc.
 * Copyright (C) 2012 Bojan Smojver <bojan@rexursive.com>
 *
 * This file is released under the GPLv2.
 */

#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pm.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/freezer.h>
#include <linux/gfp.h>
#include <linux/syscore_ops.h>
#include <linux/ctype.h>
#include <linux/genhd.h>
#include <linux/ktime.h>
#include <trace/events/power.h>

#include "power.h"


static int nocompress;
static int noresume;
static int nohibernate;
static int resume_wait;
static unsigned int resume_delay;
static char resume_file[256] = CONFIG_PM_STD_PARTITION;
dev_t swsusp_resume_device;
sector_t swsusp_resume_block;
__visible int in_suspend __nosavedata;

enum {
	HIBERNATION_INVALID,
	HIBERNATION_PLATFORM,
	HIBERNATION_SHUTDOWN,
	HIBERNATION_REBOOT,
#ifdef CONFIG_SUSPEND
	HIBERNATION_SUSPEND,
#endif
	/* keep last */
	__HIBERNATION_AFTER_LAST
};
#define HIBERNATION_MAX (__HIBERNATION_AFTER_LAST-1)
#define HIBERNATION_FIRST (HIBERNATION_INVALID + 1)

static int hibernation_mode = HIBERNATION_SHUTDOWN;

bool freezer_test_done;

static const struct platform_hibernation_ops *hibernation_ops;

bool hibernation_available(void)
{
	return (nohibernate == 0);
}

/**
 * hibernation_set_ops - Set the global hibernate operations.
 * @ops: Hibernation operations to use in subsequent hibernation transitions.
 */
void hibernation_set_ops(const struct platform_hibernation_ops *ops)
{
	if (ops && !(ops->begin && ops->end &&  ops->pre_snapshot
	    && ops->prepare && ops->finish && ops->enter && ops->pre_restore
	    && ops->restore_cleanup && ops->leave)) {
		WARN_ON(1);
		return;
	}
	lock_system_sleep();
	hibernation_ops = ops;
	if (ops)
		hibernation_mode = HIBERNATION_PLATFORM;
	else if (hibernation_mode == HIBERNATION_PLATFORM)
		hibernation_mode = HIBERNATION_SHUTDOWN;

	unlock_system_sleep();
}
EXPORT_SYMBOL_GPL(hibernation_set_ops);

static bool entering_platform_hibernation;

bool system_entering_hibernation(void)
{
	return entering_platform_hibernation;
}
EXPORT_SYMBOL(system_entering_hibernation);

#ifdef CONFIG_PM_DEBUG
static void hibernation_debug_sleep(void)
{
	printk(KERN_INFO "hibernation debug: Waiting for 5 seconds.\n");
	mdelay(5000);
}

static int hibernation_test(int level)
{
	if (pm_test_level == level) {
		hibernation_debug_sleep();
		return 1;
	}
	return 0;
}
#else /* !CONFIG_PM_DEBUG */
static int hibernation_test(int level) { return 0; }
#endif /* !CONFIG_PM_DEBUG */

/**
 * platform_begin - Call platform to start hibernation.
 * @platform_mode: Whether or not to use the platform driver.
 */
static int platform_begin(int platform_mode)
{
	return (platform_mode && hibernation_ops) ?
		hibernation_ops->begin() : 0;
}

/**
 * platform_end - Call platform to finish transition to the working state.
 * @platform_mode: Whether or not to use the platform driver.
 */
static void platform_end(int platform_mode)
{
	if (platform_mode && hibernation_ops)
		hibernation_ops->end();
}

/**
 * platform_pre_snapshot - Call platform to prepare the machine for hibernation.
 * @platform_mode: Whether or not to use the platform driver.
 *
 * Use the platform driver to prepare the system for creating a hibernate image,
 * if so configured, and return an error code if that fails.
 */

static int platform_pre_snapshot(int platform_mode)
{
	return (platform_mode && hibernation_ops) ?
		hibernation_ops->pre_snapshot() : 0;
}

/**
 * platform_leave - Call platform to prepare a transition to the working state.
 * @platform_mode: Whether or not to use the platform driver.
 *
 * Use the platform driver prepare to prepare the machine for switching to the
 * normal mode of operation.
 *
 * This routine is called on one CPU with interrupts disabled.
 */
static void platform_leave(int platform_mode)
{
	if (platform_mode && hibernation_ops)
		hibernation_ops->leave();
}

/**
 * platform_finish - Call platform to switch the system to the working state.
 * @platform_mode: Whether or not to use the platform driver.
 *
 * Use the platform driver to switch the machine to the normal mode of
 * operation.
 *
 * This routine must be called after platform_prepare().
 */
static void platform_finish(int platform_mode)
{
	if (platform_mode && hibernation_ops)
		hibernation_ops->finish();
}

/**
 * platform_pre_restore - Prepare for hibernate image restoration.
 * @platform_mode: Whether or not to use the platform driver.
 *
 * Use the platform driver to prepare the system for resume from a hibernation
 * image.
 *
 * If the restore fails after this function has been called,
 * platform_restore_cleanup() must be called.
 */
static int platform_pre_restore(int platform_mode)
{
	return (platform_mode && hibernation_ops) ?
		hibernation_ops->pre_restore() : 0;
}

/**
 * platform_restore_cleanup - Switch to the working state after failing restore.
 * @platform_mode: Whether or not to use the platform driver.
 *
 * Use the platform driver to switch the system to the normal mode of operation
 * after a failing restore.
 *
 * If platform_pre_restore() has been called before the failing restore, this
 * function must be called too, regardless of the result of
 * platform_pre_restore().
 */
static void platform_restore_cleanup(int platform_mode)
{
	if (platform_mode && hibernation_ops)
		hibernation_ops->restore_cleanup();
}

/**
 * platform_recover - Recover from a failure to suspend devices.
 * @platform_mode: Whether or not to use the platform driver.
 */
static void platform_recover(int platform_mode)
{
	if (platform_mode && hibernation_ops && hibernation_ops->recover)
		hibernation_ops->recover();
}

/**
 * swsusp_show_speed - Print time elapsed between two events during hibernation.
 * @start: Starting event.
 * @stop: Final event.
 * @nr_pages: Number of memory pages processed between @start and @stop.
 * @msg: Additional diagnostic message to print.
 */
void swsusp_show_speed(ktime_t start, ktime_t stop,
		      unsigned nr_pages, char *msg)
{
	ktime_t diff;
	u64 elapsed_centisecs64;
	unsigned int centisecs;
	unsigned int k;
	unsigned int kps;

	diff = ktime_sub(stop, start);
	elapsed_centisecs64 = ktime_divns(diff, 10*NSEC_PER_MSEC);
	centisecs = elapsed_centisecs64;
	if (centisecs == 0)
		centisecs = 1;	/* avoid div-by-zero */
	k = nr_pages * (PAGE_SIZE / 1024);
	kps = (k * 100) / centisecs;
	printk(KERN_INFO "PM: %s %u kbytes in %u.%02u seconds (%u.%02u MB/s)\n",
			msg, k,
			centisecs / 100, centisecs % 100,
			kps / 1000, (kps % 1000) / 10);
}

/**
 * create_image - Create a hibernation image.
 * @platform_mode: Whether or not to use the platform driver.
 *
 * Execute device drivers' "late" and "noirq" freeze callbacks, create a
 * hibernation image and run the drivers' "noirq" and "early" thaw callbacks.
 *
 * Control reappears in this routine after the subsequent restore.
 */
static int create_image(int platform_mode)
{
	int error;

	error = dpm_suspend_end(PMSG_FREEZE);
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to power down, "
			"aborting hibernation\n");
		return error;
	}

	error = platform_pre_snapshot(platform_mode);
	if (error || hibernation_test(TEST_PLATFORM))
		goto Platform_finish;

	error = disable_nonboot_cpus();
	if (error || hibernation_test(TEST_CPUS))
		goto Enable_cpus;

	local_irq_disable();

	error = syscore_suspend();
	if (error) {
		printk(KERN_ERR "PM: Some system devices failed to power down, "
			"aborting hibernation\n");
		goto Enable_irqs;
	}

	if (hibernation_test(TEST_CORE) || pm_wakeup_pending())
		goto Power_up;

	in_suspend = 1;
	save_processor_state();
	trace_suspend_resume(TPS("machine_suspend"), PM_EVENT_HIBERNATE, true);
	error = swsusp_arch_suspend();
	trace_suspend_resume(TPS("machine_suspend"), PM_EVENT_HIBERNATE, false);
	if (error)
		printk(KERN_ERR "PM: Error %d creating hibernation image\n",
			error);
	/* Restore control flow magically appears here */
	restore_processor_state();
	if (!in_suspend)
		events_check_enabled = false;

	platform_leave(platform_mode);

 Power_up:
	syscore_resume();

 Enable_irqs:
	local_irq_enable();

 Enable_cpus:
	enable_nonboot_cpus();

 Platform_finish:
	platform_finish(platform_mode);

	dpm_resume_start(in_suspend ?
		(error ? PMSG_RECOVER : PMSG_THAW) : PMSG_RESTORE);

	return error;
}

/**
 * hibernation_snapshot - Quiesce devices and create a hibernation image.
 * @platform_mode: If set, use platform driver to prepare for the transition.
 *
 * This routine must be called with pm_mutex held.
 */
int hibernation_snapshot(int platform_mode)
{
	pm_message_t msg;
	int error;

	error = platform_begin(platform_mode);
	if (error)
		goto Close;

	/* Preallocate image memory before shutting down devices. */
	error = hibernate_preallocate_memory();
	if (error)
		goto Close;

	error = freeze_kernel_threads();
	if (error)
		goto Cleanup;

	if (hibernation_test(TEST_FREEZER)) {

		/*
		 * Indicate to the caller that we are returning due to a
		 * successful freezer test.
		 */
		freezer_test_done = true;
		goto Thaw;
	}

	error = dpm_prepare(PMSG_FREEZE);
	if (error) {
		dpm_complete(PMSG_RECOVER);
		goto Thaw;
	}

	suspend_console();
	pm_restrict_gfp_mask();

	error = dpm_suspend(PMSG_FREEZE);

	if (error || hibernation_test(TEST_DEVICES))
		platform_recover(platform_mode);
	else
		error = create_image(platform_mode);

	/*
	 * In the case that we call create_image() above, the control
	 * returns here (1) after the image has been created or the
	 * image creation has failed and (2) after a successful restore.
	 */

	/* We may need to release the preallocated image pages here. */
	if (error || !in_suspend)
		swsusp_free();

	msg = in_suspend ? (error ? PMSG_RECOVER : PMSG_THAW) : PMSG_RESTORE;
	dpm_resume(msg);

	if (error || !in_suspend)
		pm_restore_gfp_mask();

	resume_console();
	dpm_complete(msg);

 Close:
	platform_end(platform_mode);
	return error;

 Thaw:
	thaw_kernel_threads();
 Cleanup:
	swsusp_free();
	goto Close;
}

/**
 * resume_target_kernel - Restore system state from a hibernation image.
 * @platform_mode: Whether or not to use the platform driver.
 *
 * Execute device drivers' "noirq" and "late" freeze callbacks, restore the
 * contents of highmem that have not been restored yet from the image and run
 * the low-level code that will restore the remaining contents of memory and
 * switch to the just restored target kernel.
 */
static int resume_target_kernel(bool platform_mode)
{
	int error;

	error = dpm_suspend_end(PMSG_QUIESCE);
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to power down, "
			"aborting resume\n");
		return error;
	}

	error = platform_pre_restore(platform_mode);
	if (error)
		goto Cleanup;

	error = disable_nonboot_cpus();
	if (error)
		goto Enable_cpus;

	local_irq_disable();

	error = syscore_suspend();
	if (error)
		goto Enable_irqs;

	save_processor_state();
	error = restore_highmem();
	if (!error) {
		error = swsusp_arch_resume();
		/*
		 * The code below is only ever reached in case of a failure.
		 * Otherwise, execution continues at the place where
		 * swsusp_arch_suspend() was called.
		 */
		BUG_ON(!error);
		/*
		 * This call to restore_highmem() reverts the changes made by
		 * the previous one.
		 */
		restore_highmem();
	}
	/*
	 * The only reason why swsusp_arch_resume() can fail is memory being
	 * very tight, so we have to free it as soon as we can to avoid
	 * subsequent failures.
	 */
	swsusp_free();
	restore_processor_state();
	touch_softlockup_watchdog();

	syscore_resume();

 Enable_irqs:
	local_irq_enable();

 Enable_cpus:
	enable_nonboot_cpus();

 Cleanup:
	platform_restore_cleanup(platform_mode);

	dpm_resume_start(PMSG_RECOVER);

	return error;
}

/**
 * hibernation_restore - Quiesce devices and restore from a hibernation image.
 * @platform_mode: If set, use platform driver to prepare for the transition.
 *
 * This routine must be called with pm_mutex held.  If it is successful, control
 * reappears in the restored target kernel in hibernation_snapshot().
 */
int hibernation_restore(int platform_mode)
{
	int error;

	pm_prepare_console();
	suspend_console();
	pm_restrict_gfp_mask();
	error = dpm_suspend_start(PMSG_QUIESCE);
	if (!error) {
		error = resume_target_kernel(platform_mode);
		/*
		 * The above should either succeed and jump to the new kernel,
		 * or return with an error. Otherwise things are just
		 * undefined, so let's be paranoid.
		 */
		BUG_ON(!error);
	}
	dpm_resume_end(PMSG_RECOVER);
	pm_restore_gfp_mask();
	resume_console();
	pm_restore_console();
	return error;
}

/**
 * hibernation_platform_enter - Power off the system using the platform driver.
 */
int hibernation_platform_enter(void)
{
	int error;

	if (!hibernation_ops)
		return -ENOSYS;

	/*
	 * We have cancelled the power transition by running
	 * hibernation_ops->finish() before saving the image, so we should let
	 * the firmware know that we're going to enter the sleep state after all
	 */
	error = hibernation_ops->begin();
	if (error)
		goto Close;

	entering_platform_hibernation = true;
	suspend_console();
	error = dpm_suspend_start(PMSG_HIBERNATE);
	if (error) {
		if (hibernation_ops->recover)
			hibernation_ops->recover();
		goto Resume_devices;
	}

	error = dpm_suspend_end(PMSG_HIBERNATE);
	if (error)
		goto Resume_devices;

	error = hibernation_ops->prepare();
	if (error)
		goto Platform_finish;

	error = disable_nonboot_cpus();
	if (error)
		goto Platform_finish;

	local_irq_disable();
	syscore_suspend();
	if (pm_wakeup_pending()) {
		error = -EAGAIN;
		goto Power_up;
	}

	hibernation_ops->enter();
	/* We should never get here */
	while (1);

 Power_up:
	syscore_resume();
	local_irq_enable();
	enable_nonboot_cpus();

 Platform_finish:
	hibernation_ops->finish();

	dpm_resume_start(PMSG_RESTORE);

 Resume_devices:
	entering_platform_hibernation = false;
	dpm_resume_end(PMSG_RESTORE);
	resume_console();

 Close:
	hibernation_ops->end();

	return error;
}

/**
 * power_down - Shut the machine down for hibernation.
 *
 * Use the platform driver, if configured, to put the system into the sleep
 * state corresponding to hibernation, or try to power it off or reboot,
 * depending on the value of hibernation_mode.
 */
static void power_down(void)
{
#ifdef CONFIG_SUSPEND
	int error;
#endif

	switch (hibernation_mode) {
	case HIBERNATION_REBOOT:
		kernel_restart(NULL);
		break;
	case HIBERNATION_PLATFORM:
		hibernation_platform_enter();
	case HIBERNATION_SHUTDOWN:
		if (pm_power_off)
			kernel_power_off();
		break;
#ifdef CONFIG_SUSPEND
	case HIBERNATION_SUSPEND:
		error = suspend_devices_and_enter(PM_SUSPEND_MEM);
		if (error) {
			if (hibernation_ops)
				hibernation_mode = HIBERNATION_PLATFORM;
			else
				hibernation_mode = HIBERNATION_SHUTDOWN;
			power_down();
		}
		/*
		 * Restore swap signature.
		 */
		error = swsusp_unmark();
		if (error)
			printk(KERN_ERR "PM: Swap will be unusable! "
			                "Try swapon -a.\n");
		return;
#endif
	}
	kernel_halt();
	/*
	 * Valid image is on the disk, if we continue we risk serious data
	 * corruption after resume.
	 */
	printk(KERN_CRIT "PM: Please power down manually\n");
	while (1)
		cpu_relax();
}

/**
 * hibernate - Carry out system hibernation, including saving the image.
 */
int hibernate(void)
{
	int error;

	if (!hibernation_available()) {
		pr_debug("PM: Hibernation not available.\n");
		return -EPERM;
	}

	lock_system_sleep();
	/* The snapshot device should not be opened while we're running */
	if (!atomic_add_unless(&snapshot_device_available, -1, 0)) {
		error = -EBUSY;
		goto Unlock;
	}

	pm_prepare_console();
	error = pm_notifier_call_chain(PM_HIBERNATION_PREPARE);
	if (error)
		goto Exit;

	printk(KERN_INFO "PM: Syncing filesystems ... ");
	sys_sync();
	printk("done.\n");

	error = freeze_processes();
	if (error)
		goto Exit;

	lock_device_hotplug();
	/* Allocate memory management structures */
	error = create_basic_memory_bitmaps();
	if (error)
		goto Thaw;

	error = hibernation_snapshot(hibernation_mode == HIBERNATION_PLATFORM);
	if (error || freezer_test_done)
		goto Free_bitmaps;

	if (in_suspend) {
		unsigned int flags = 0;

		if (hibernation_mode == HIBERNATION_PLATFORM)
			flags |= SF_PLATFORM_MODE;
		if (nocompress)
			flags |= SF_NOCOMPRESS_MODE;
		else
		        flags |= SF_CRC32_MODE;

		pr_debug("PM: writing image.\n");
		error = swsusp_write(flags);
		swsusp_free();
		if (!error)
			power_down();
		in_suspend = 0;
		pm_restore_gfp_mask();
	} else {
		pr_debug("PM: Image restored successfully.\n");
	}

 Free_bitmaps:
	free_basic_memory_bitmaps();
 Thaw:
	unlock_device_hotplug();
	thaw_processes();

	/* Don't bother checking whether freezer_test_done is true */
	freezer_test_done = false;
 Exit:
	pm_notifier_call_chain(PM_POST_HIBERNATION);
	pm_restore_console();
	atomic_inc(&snapshot_device_available);
 Unlock:
	unlock_system_sleep();
	return error;
}


/**
 * software_resume - Resume from a saved hibernation image.
 *
 * This routine is called as a late initcall, when all devices have been
 * discovered and initialized already.
 *
 * The image reading code is called to see if there is a hibernation image
 * available for reading.  If that is the case, devices are quiesced and the
 * contents of memory is restored from the saved image.
 *
 * If this is successful, control reappears in the restored target kernel in
 * hibernation_snaphot() which returns to hibernate().  Otherwise, the routine
 * attempts to recover gracefully and make the kernel return to the normal mode
 * of operation.
 */
static int software_resume(void)
{
	int error;
	unsigned int flags;

	/*
	 * If the user said "noresume".. bail out early.
	 */
	if (noresume || !hibernation_available())
		return 0;

	/*
	 * name_to_dev_t() below takes a sysfs buffer mutex when sysfs
	 * is configured into the kernel. Since the regular hibernate
	 * trigger path is via sysfs which takes a buffer mutex before
	 * calling hibernate functions (which take pm_mutex) this can
	 * cause lockdep to complain about a possible ABBA deadlock
	 * which cannot happen since we're in the boot code here and
	 * sysfs can't be invoked yet. Therefore, we use a subclass
	 * here to avoid lockdep complaining.
	 */
	mutex_lock_nested(&pm_mutex, SINGLE_DEPTH_NESTING);

	if (swsusp_resume_device)
		goto Check_image;

	if (!strlen(resume_file)) {
		error = -ENOENT;
		goto Unlock;
	}

	pr_debug("PM: Checking hibernation image partition %s\n", resume_file);

	if (resume_delay) {
		printk(KERN_INFO "Waiting %dsec before reading resume device...\n",
			resume_delay);
		ssleep(resume_delay);
	}

	/* Check if the device is there */
	swsusp_resume_device = name_to_dev_t(resume_file);

	/*
	 * name_to_dev_t is ineffective to verify parition if resume_file is in
	 * integer format. (e.g. major:minor)
	 */
	if (isdigit(resume_file[0]) && resume_wait) {
		int partno;
		while (!get_gendisk(swsusp_resume_device, &partno))
			msleep(10);
	}

	if (!swsusp_resume_device) {
		/*
		 * Some device discovery might still be in progress; we need
		 * to wait for this to finish.
		 */
		wait_for_device_probe();

		if (resume_wait) {
			while ((swsusp_resume_device = name_to_dev_t(resume_file)) == 0)
				msleep(10);
			async_synchronize_full();
		}

		swsusp_resume_device = name_to_dev_t(resume_file);
		if (!swsusp_resume_device) {
			error = -ENODEV;
			goto Unlock;
		}
	}

 Check_image:
	pr_debug("PM: Hibernation image partition %d:%d present\n",
		MAJOR(swsusp_resume_device), MINOR(swsusp_resume_device));

	pr_debug("PM: Looking for hibernation image.\n");
	error = swsusp_check();
	if (error)
		goto Unlock;

	/* The snapshot device should not be opened while we're running */
	if (!atomic_add_unless(&snapshot_device_available, -1, 0)) {
		error = -EBUSY;
		swsusp_close(FMODE_READ);
		goto Unlock;
	}

	pm_prepare_console();
	error = pm_notifier_call_chain(PM_RESTORE_PREPARE);
	if (error)
		goto Close_Finish;

	pr_debug("PM: Preparing processes for restore.\n");
	error = freeze_processes();
	if (error)
		goto Close_Finish;

	pr_debug("PM: Loading hibernation image.\n");

	lock_device_hotplug();
	error = create_basic_memory_bitmaps();
	if (error)
		goto Thaw;

	error = swsusp_read(&flags);
	swsusp_close(FMODE_READ);
	if (!error)
		hibernation_restore(flags & SF_PLATFORM_MODE);

	printk(KERN_ERR "PM: Failed to load hibernation image, recovering.\n");
	swsusp_free();
	free_basic_memory_bitmaps();
 Thaw:
	unlock_device_hotplug();
	thaw_processes();
 Finish:
	pm_notifier_call_chain(PM_POST_RESTORE);
	pm_restore_console();
	atomic_inc(&snapshot_device_available);
	/* For success case, the suspend path will release the lock */
 Unlock:
	mutex_unlock(&pm_mutex);
	pr_debug("PM: Hibernation image not present or could not be loaded.\n");
	return error;
 Close_Finish:
	swsusp_close(FMODE_READ);
	goto Finish;
}

late_initcall_sync(software_resume);


static const char * const hibernation_modes[] = {
	[HIBERNATION_PLATFORM]	= "platform",
	[HIBERNATION_SHUTDOWN]	= "shutdown",
	[HIBERNATION_REBOOT]	= "reboot",
#ifdef CONFIG_SUSPEND
	[HIBERNATION_SUSPEND]	= "suspend",
#endif
};

/*
 * /sys/power/disk - Control hibernation mode.
 *
 * Hibernation can be handled in several ways.  There are a few different ways
 * to put the system into the sleep state: using the platform driver (e.g. ACPI
 * or other hibernation_ops), powering it off or rebooting it (for testing
 * mostly).
 *
 * The sysfs file /sys/power/disk provides an interface for selecting the
 * hibernation mode to use.  Reading from this file causes the available modes
 * to be printed.  There are 3 modes that can be supported:
 *
 *	'platform'
 *	'shutdown'
 *	'reboot'
 *
 * If a platform hibernation driver is in use, 'platform' will be supported
 * and will be used by default.  Otherwise, 'shutdown' will be used by default.
 * The selected option (i.e. the one corresponding to the current value of
 * hibernation_mode) is enclosed by a square bracket.
 *
 * To select a given hibernation mode it is necessary to write the mode's
 * string representation (as returned by reading from /sys/power/disk) back
 * into /sys/power/disk.
 */

static ssize_t disk_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	int i;
	char *start = buf;

	if (!hibernation_available())
		return sprintf(buf, "[disabled]\n");

	for (i = HIBERNATION_FIRST; i <= HIBERNATION_MAX; i++) {
		if (!hibernation_modes[i])
			continue;
		switch (i) {
		case HIBERNATION_SHUTDOWN:
		case HIBERNATION_REBOOT:
#ifdef CONFIG_SUSPEND
		case HIBERNATION_SUSPEND:
#endif
			break;
		case HIBERNATION_PLATFORM:
			if (hibernation_ops)
				break;
			/* not a valid mode, continue with loop */
			continue;
		}
		if (i == hibernation_mode)
			buf += sprintf(buf, "[%s] ", hibernation_modes[i]);
		else
			buf += sprintf(buf, "%s ", hibernation_modes[i]);
	}
	buf += sprintf(buf, "\n");
	return buf-start;
}

static ssize_t disk_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t n)
{
	int error = 0;
	int i;
	int len;
	char *p;
	int mode = HIBERNATION_INVALID;

	if (!hibernation_available())
		return -EPERM;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	lock_system_sleep();
	for (i = HIBERNATION_FIRST; i <= HIBERNATION_MAX; i++) {
		if (len == strlen(hibernation_modes[i])
		    && !strncmp(buf, hibernation_modes[i], len)) {
			mode = i;
			break;
		}
	}
	if (mode != HIBERNATION_INVALID) {
		switch (mode) {
		case HIBERNATION_SHUTDOWN:
		case HIBERNATION_REBOOT:
#ifdef CONFIG_SUSPEND
		case HIBERNATION_SUSPEND:
#endif
			hibernation_mode = mode;
			break;
		case HIBERNATION_PLATFORM:
			if (hibernation_ops)
				hibernation_mode = mode;
			else
				error = -EINVAL;
		}
	} else
		error = -EINVAL;

	if (!error)
		pr_debug("PM: Hibernation mode set to '%s'\n",
			 hibernation_modes[mode]);
	unlock_system_sleep();
	return error ? error : n;
}

power_attr(disk);

static ssize_t resume_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	return sprintf(buf,"%d:%d\n", MAJOR(swsusp_resume_device),
		       MINOR(swsusp_resume_device));
}

static ssize_t resume_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t n)
{
	dev_t res;
	int len = n;
	char *name;

	if (len && buf[len-1] == '\n')
		len--;
	name = kstrndup(buf, len, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	res = name_to_dev_t(name);
	kfree(name);
	if (!res)
		return -EINVAL;

	lock_system_sleep();
	swsusp_resume_device = res;
	unlock_system_sleep();
	printk(KERN_INFO "PM: Starting manual resume from disk\n");
	noresume = 0;
	software_resume();
	return n;
}

power_attr(resume);

static ssize_t image_size_show(struct kobject *kobj, struct kobj_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "%lu\n", image_size);
}

static ssize_t image_size_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned long size;

	if (sscanf(buf, "%lu", &size) == 1) {
		image_size = size;
		return n;
	}

	return -EINVAL;
}

power_attr(image_size);

static ssize_t reserved_size_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", reserved_size);
}

static ssize_t reserved_size_store(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t n)
{
	unsigned long size;

	if (sscanf(buf, "%lu", &size) == 1) {
		reserved_size = size;
		return n;
	}

	return -EINVAL;
}

power_attr(reserved_size);

static struct attribute * g[] = {
	&disk_attr.attr,
	&resume_attr.attr,
	&image_size_attr.attr,
	&reserved_size_attr.attr,
	NULL,
};


static struct attribute_group attr_group = {
	.attrs = g,
};


static int __init pm_disk_init(void)
{
	return sysfs_create_group(power_kobj, &attr_group);
}

core_initcall(pm_disk_init);


static int __init resume_setup(char *str)
{
	if (noresume)
		return 1;

	strncpy( resume_file, str, 255 );
	return 1;
}

static int __init resume_offset_setup(char *str)
{
	unsigned long long offset;

	if (noresume)
		return 1;

	if (sscanf(str, "%llu", &offset) == 1)
		swsusp_resume_block = offset;

	return 1;
}

static int __init hibernate_setup(char *str)
{
	if (!strncmp(str, "noresume", 8))
		noresume = 1;
	else if (!strncmp(str, "nocompress", 10))
		nocompress = 1;
	else if (!strncmp(str, "no", 2)) {
		noresume = 1;
		nohibernate = 1;
	}
	return 1;
}

static int __init noresume_setup(char *str)
{
	noresume = 1;
	return 1;
}

static int __init resumewait_setup(char *str)
{
	resume_wait = 1;
	return 1;
}

static int __init resumedelay_setup(char *str)
{
	int rc = kstrtouint(str, 0, &resume_delay);

	if (rc)
		return rc;
	return 1;
}

static int __init nohibernate_setup(char *str)
{
	noresume = 1;
	nohibernate = 1;
	return 1;
}

static int __init kaslr_nohibernate_setup(char *str)
{
	return nohibernate_setup(str);
}

__setup("noresume", noresume_setup);
__setup("resume_offset=", resume_offset_setup);
__setup("resume=", resume_setup);
__setup("hibernate=", hibernate_setup);
__setup("resumewait", resumewait_setup);
__setup("resumedelay=", resumedelay_setup);
__setup("nohibernate", nohibernate_setup);
__setup("kaslr", kaslr_nohibernate_setup);
