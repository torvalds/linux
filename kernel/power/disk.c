/*
 * kernel/power/disk.c - Suspend-to-disk support.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * Copyright (c) 2004 Pavel Machek <pavel@suse.cz>
 *
 * This file is released under the GPLv2.
 *
 */

#include <linux/suspend.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pm.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/freezer.h>

#include "power.h"


static int noresume = 0;
static char resume_file[256] = CONFIG_PM_STD_PARTITION;
dev_t swsusp_resume_device;
sector_t swsusp_resume_block;

enum {
	HIBERNATION_INVALID,
	HIBERNATION_PLATFORM,
	HIBERNATION_TEST,
	HIBERNATION_TESTPROC,
	HIBERNATION_SHUTDOWN,
	HIBERNATION_REBOOT,
	/* keep last */
	__HIBERNATION_AFTER_LAST
};
#define HIBERNATION_MAX (__HIBERNATION_AFTER_LAST-1)
#define HIBERNATION_FIRST (HIBERNATION_INVALID + 1)

static int hibernation_mode = HIBERNATION_SHUTDOWN;

static struct platform_hibernation_ops *hibernation_ops;

/**
 * hibernation_set_ops - set the global hibernate operations
 * @ops: the hibernation operations to use in subsequent hibernation transitions
 */

void hibernation_set_ops(struct platform_hibernation_ops *ops)
{
	if (ops && !(ops->begin && ops->end &&  ops->pre_snapshot
	    && ops->prepare && ops->finish && ops->enter && ops->pre_restore
	    && ops->restore_cleanup)) {
		WARN_ON(1);
		return;
	}
	mutex_lock(&pm_mutex);
	hibernation_ops = ops;
	if (ops)
		hibernation_mode = HIBERNATION_PLATFORM;
	else if (hibernation_mode == HIBERNATION_PLATFORM)
		hibernation_mode = HIBERNATION_SHUTDOWN;

	mutex_unlock(&pm_mutex);
}

#ifdef CONFIG_PM_DEBUG
static void hibernation_debug_sleep(void)
{
	printk(KERN_INFO "hibernation debug: Waiting for 5 seconds.\n");
	mdelay(5000);
}

static int hibernation_testmode(int mode)
{
	if (hibernation_mode == mode) {
		hibernation_debug_sleep();
		return 1;
	}
	return 0;
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
static int hibernation_testmode(int mode) { return 0; }
static int hibernation_test(int level) { return 0; }
#endif /* !CONFIG_PM_DEBUG */

/**
 *	platform_begin - tell the platform driver that we're starting
 *	hibernation
 */

static int platform_begin(int platform_mode)
{
	return (platform_mode && hibernation_ops) ?
		hibernation_ops->begin() : 0;
}

/**
 *	platform_end - tell the platform driver that we've entered the
 *	working state
 */

static void platform_end(int platform_mode)
{
	if (platform_mode && hibernation_ops)
		hibernation_ops->end();
}

/**
 *	platform_pre_snapshot - prepare the machine for hibernation using the
 *	platform driver if so configured and return an error code if it fails
 */

static int platform_pre_snapshot(int platform_mode)
{
	return (platform_mode && hibernation_ops) ?
		hibernation_ops->pre_snapshot() : 0;
}

/**
 *	platform_leave - prepare the machine for switching to the normal mode
 *	of operation using the platform driver (called with interrupts disabled)
 */

static void platform_leave(int platform_mode)
{
	if (platform_mode && hibernation_ops)
		hibernation_ops->leave();
}

/**
 *	platform_finish - switch the machine to the normal mode of operation
 *	using the platform driver (must be called after platform_prepare())
 */

static void platform_finish(int platform_mode)
{
	if (platform_mode && hibernation_ops)
		hibernation_ops->finish();
}

/**
 *	platform_pre_restore - prepare the platform for the restoration from a
 *	hibernation image.  If the restore fails after this function has been
 *	called, platform_restore_cleanup() must be called.
 */

static int platform_pre_restore(int platform_mode)
{
	return (platform_mode && hibernation_ops) ?
		hibernation_ops->pre_restore() : 0;
}

/**
 *	platform_restore_cleanup - switch the platform to the normal mode of
 *	operation after a failing restore.  If platform_pre_restore() has been
 *	called before the failing restore, this function must be called too,
 *	regardless of the result of platform_pre_restore().
 */

static void platform_restore_cleanup(int platform_mode)
{
	if (platform_mode && hibernation_ops)
		hibernation_ops->restore_cleanup();
}

/**
 *	platform_recover - recover the platform from a failure to suspend
 *	devices.
 */

static void platform_recover(int platform_mode)
{
	if (platform_mode && hibernation_ops && hibernation_ops->recover)
		hibernation_ops->recover();
}

/**
 *	create_image - freeze devices that need to be frozen with interrupts
 *	off, create the hibernation image and thaw those devices.  Control
 *	reappears in this routine after a restore.
 */

static int create_image(int platform_mode)
{
	int error;

	error = arch_prepare_suspend();
	if (error)
		return error;

	device_pm_lock();
	local_irq_disable();
	/* At this point, device_suspend() has been called, but *not*
	 * device_power_down(). We *must* call device_power_down() now.
	 * Otherwise, drivers for some devices (e.g. interrupt controllers)
	 * become desynchronized with the actual state of the hardware
	 * at resume time, and evil weirdness ensues.
	 */
	error = device_power_down(PMSG_FREEZE);
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to power down, "
			"aborting hibernation\n");
		goto Enable_irqs;
	}

	if (hibernation_test(TEST_CORE))
		goto Power_up;

	in_suspend = 1;
	save_processor_state();
	error = swsusp_arch_suspend();
	if (error)
		printk(KERN_ERR "PM: Error %d creating hibernation image\n",
			error);
	/* Restore control flow magically appears here */
	restore_processor_state();
	if (!in_suspend)
		platform_leave(platform_mode);
 Power_up:
	/* NOTE:  device_power_up() is just a resume() for devices
	 * that suspended with irqs off ... no overall powerup.
	 */
	device_power_up(in_suspend ?
		(error ? PMSG_RECOVER : PMSG_THAW) : PMSG_RESTORE);
 Enable_irqs:
	local_irq_enable();
	device_pm_unlock();
	return error;
}

/**
 *	hibernation_snapshot - quiesce devices and create the hibernation
 *	snapshot image.
 *	@platform_mode - if set, use the platform driver, if available, to
 *			 prepare the platform frimware for the power transition.
 *
 *	Must be called with pm_mutex held
 */

int hibernation_snapshot(int platform_mode)
{
	int error;

	error = platform_begin(platform_mode);
	if (error)
		return error;

	/* Free memory before shutting down devices. */
	error = swsusp_shrink_memory();
	if (error)
		goto Close;

	suspend_console();
	error = device_suspend(PMSG_FREEZE);
	if (error)
		goto Recover_platform;

	if (hibernation_test(TEST_DEVICES))
		goto Recover_platform;

	error = platform_pre_snapshot(platform_mode);
	if (error || hibernation_test(TEST_PLATFORM))
		goto Finish;

	error = disable_nonboot_cpus();
	if (!error) {
		if (hibernation_test(TEST_CPUS))
			goto Enable_cpus;

		if (hibernation_testmode(HIBERNATION_TEST))
			goto Enable_cpus;

		error = create_image(platform_mode);
		/* Control returns here after successful restore */
	}
 Enable_cpus:
	enable_nonboot_cpus();
 Finish:
	platform_finish(platform_mode);
 Resume_devices:
	device_resume(in_suspend ?
		(error ? PMSG_RECOVER : PMSG_THAW) : PMSG_RESTORE);
	resume_console();
 Close:
	platform_end(platform_mode);
	return error;

 Recover_platform:
	platform_recover(platform_mode);
	goto Resume_devices;
}

/**
 *	resume_target_kernel - prepare devices that need to be suspended with
 *	interrupts off, restore the contents of highmem that have not been
 *	restored yet from the image and run the low level code that will restore
 *	the remaining contents of memory and switch to the just restored target
 *	kernel.
 */

static int resume_target_kernel(void)
{
	int error;

	device_pm_lock();
	local_irq_disable();
	error = device_power_down(PMSG_QUIESCE);
	if (error) {
		printk(KERN_ERR "PM: Some devices failed to power down, "
			"aborting resume\n");
		goto Enable_irqs;
	}
	/* We'll ignore saved state, but this gets preempt count (etc) right */
	save_processor_state();
	error = restore_highmem();
	if (!error) {
		error = swsusp_arch_resume();
		/*
		 * The code below is only ever reached in case of a failure.
		 * Otherwise execution continues at place where
		 * swsusp_arch_suspend() was called
		 */
		BUG_ON(!error);
		/* This call to restore_highmem() undos the previous one */
		restore_highmem();
	}
	/*
	 * The only reason why swsusp_arch_resume() can fail is memory being
	 * very tight, so we have to free it as soon as we can to avoid
	 * subsequent failures
	 */
	swsusp_free();
	restore_processor_state();
	touch_softlockup_watchdog();
	device_power_up(PMSG_RECOVER);
 Enable_irqs:
	local_irq_enable();
	device_pm_unlock();
	return error;
}

/**
 *	hibernation_restore - quiesce devices and restore the hibernation
 *	snapshot image.  If successful, control returns in hibernation_snaphot()
 *	@platform_mode - if set, use the platform driver, if available, to
 *			 prepare the platform frimware for the transition.
 *
 *	Must be called with pm_mutex held
 */

int hibernation_restore(int platform_mode)
{
	int error;

	pm_prepare_console();
	suspend_console();
	error = device_suspend(PMSG_QUIESCE);
	if (error)
		goto Finish;

	error = platform_pre_restore(platform_mode);
	if (!error) {
		error = disable_nonboot_cpus();
		if (!error)
			error = resume_target_kernel();
		enable_nonboot_cpus();
	}
	platform_restore_cleanup(platform_mode);
	device_resume(PMSG_RECOVER);
 Finish:
	resume_console();
	pm_restore_console();
	return error;
}

/**
 *	hibernation_platform_enter - enter the hibernation state using the
 *	platform driver (if available)
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

	suspend_console();
	error = device_suspend(PMSG_HIBERNATE);
	if (error) {
		if (hibernation_ops->recover)
			hibernation_ops->recover();
		goto Resume_devices;
	}

	error = hibernation_ops->prepare();
	if (error)
		goto Resume_devices;

	error = disable_nonboot_cpus();
	if (error)
		goto Finish;

	device_pm_lock();
	local_irq_disable();
	error = device_power_down(PMSG_HIBERNATE);
	if (!error) {
		hibernation_ops->enter();
		/* We should never get here */
		while (1);
	}
	local_irq_enable();
	device_pm_unlock();

	/*
	 * We don't need to reenable the nonboot CPUs or resume consoles, since
	 * the system is going to be halted anyway.
	 */
 Finish:
	hibernation_ops->finish();
 Resume_devices:
	device_resume(PMSG_RESTORE);
	resume_console();
 Close:
	hibernation_ops->end();
	return error;
}

/**
 *	power_down - Shut the machine down for hibernation.
 *
 *	Use the platform driver, if configured so; otherwise try
 *	to power off or reboot.
 */

static void power_down(void)
{
	switch (hibernation_mode) {
	case HIBERNATION_TEST:
	case HIBERNATION_TESTPROC:
		break;
	case HIBERNATION_REBOOT:
		kernel_restart(NULL);
		break;
	case HIBERNATION_PLATFORM:
		hibernation_platform_enter();
	case HIBERNATION_SHUTDOWN:
		kernel_power_off();
		break;
	}
	kernel_halt();
	/*
	 * Valid image is on the disk, if we continue we risk serious data
	 * corruption after resume.
	 */
	printk(KERN_CRIT "PM: Please power down manually\n");
	while(1);
}

static int prepare_processes(void)
{
	int error = 0;

	if (freeze_processes()) {
		error = -EBUSY;
		thaw_processes();
	}
	return error;
}

/**
 *	hibernate - The granpappy of the built-in hibernation management
 */

int hibernate(void)
{
	int error;

	mutex_lock(&pm_mutex);
	/* The snapshot device should not be opened while we're running */
	if (!atomic_add_unless(&snapshot_device_available, -1, 0)) {
		error = -EBUSY;
		goto Unlock;
	}

	pm_prepare_console();
	error = pm_notifier_call_chain(PM_HIBERNATION_PREPARE);
	if (error)
		goto Exit;

	error = usermodehelper_disable();
	if (error)
		goto Exit;

	/* Allocate memory management structures */
	error = create_basic_memory_bitmaps();
	if (error)
		goto Exit;

	printk(KERN_INFO "PM: Syncing filesystems ... ");
	sys_sync();
	printk("done.\n");

	error = prepare_processes();
	if (error)
		goto Finish;

	if (hibernation_test(TEST_FREEZER))
		goto Thaw;

	if (hibernation_testmode(HIBERNATION_TESTPROC))
		goto Thaw;

	error = hibernation_snapshot(hibernation_mode == HIBERNATION_PLATFORM);
	if (in_suspend && !error) {
		unsigned int flags = 0;

		if (hibernation_mode == HIBERNATION_PLATFORM)
			flags |= SF_PLATFORM_MODE;
		pr_debug("PM: writing image.\n");
		error = swsusp_write(flags);
		swsusp_free();
		if (!error)
			power_down();
	} else {
		pr_debug("PM: Image restored successfully.\n");
		swsusp_free();
	}
 Thaw:
	thaw_processes();
 Finish:
	free_basic_memory_bitmaps();
	usermodehelper_enable();
 Exit:
	pm_notifier_call_chain(PM_POST_HIBERNATION);
	pm_restore_console();
	atomic_inc(&snapshot_device_available);
 Unlock:
	mutex_unlock(&pm_mutex);
	return error;
}


/**
 *	software_resume - Resume from a saved image.
 *
 *	Called as a late_initcall (so all devices are discovered and
 *	initialized), we call swsusp to see if we have a saved image or not.
 *	If so, we quiesce devices, the restore the saved image. We will
 *	return above (in hibernate() ) if everything goes well.
 *	Otherwise, we fail gracefully and return to the normally
 *	scheduled program.
 *
 */

static int software_resume(void)
{
	int error;
	unsigned int flags;

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
	if (!swsusp_resume_device) {
		if (!strlen(resume_file)) {
			mutex_unlock(&pm_mutex);
			return -ENOENT;
		}
		swsusp_resume_device = name_to_dev_t(resume_file);
		pr_debug("PM: Resume from partition %s\n", resume_file);
	} else {
		pr_debug("PM: Resume from partition %d:%d\n",
				MAJOR(swsusp_resume_device),
				MINOR(swsusp_resume_device));
	}

	if (noresume) {
		/**
		 * FIXME: If noresume is specified, we need to find the
		 * partition and reset it back to normal swap space.
		 */
		mutex_unlock(&pm_mutex);
		return 0;
	}

	pr_debug("PM: Checking hibernation image.\n");
	error = swsusp_check();
	if (error)
		goto Unlock;

	/* The snapshot device should not be opened while we're running */
	if (!atomic_add_unless(&snapshot_device_available, -1, 0)) {
		error = -EBUSY;
		goto Unlock;
	}

	pm_prepare_console();
	error = pm_notifier_call_chain(PM_RESTORE_PREPARE);
	if (error)
		goto Finish;

	error = usermodehelper_disable();
	if (error)
		goto Finish;

	error = create_basic_memory_bitmaps();
	if (error)
		goto Finish;

	pr_debug("PM: Preparing processes for restore.\n");
	error = prepare_processes();
	if (error) {
		swsusp_close(FMODE_READ);
		goto Done;
	}

	pr_debug("PM: Reading hibernation image.\n");

	error = swsusp_read(&flags);
	if (!error)
		hibernation_restore(flags & SF_PLATFORM_MODE);

	printk(KERN_ERR "PM: Restore failed, recovering.\n");
	swsusp_free();
	thaw_processes();
 Done:
	free_basic_memory_bitmaps();
	usermodehelper_enable();
 Finish:
	pm_notifier_call_chain(PM_POST_RESTORE);
	pm_restore_console();
	atomic_inc(&snapshot_device_available);
	/* For success case, the suspend path will release the lock */
 Unlock:
	mutex_unlock(&pm_mutex);
	pr_debug("PM: Resume from disk failed.\n");
	return error;
}

late_initcall(software_resume);


static const char * const hibernation_modes[] = {
	[HIBERNATION_PLATFORM]	= "platform",
	[HIBERNATION_SHUTDOWN]	= "shutdown",
	[HIBERNATION_REBOOT]	= "reboot",
	[HIBERNATION_TEST]	= "test",
	[HIBERNATION_TESTPROC]	= "testproc",
};

/**
 *	disk - Control hibernation mode
 *
 *	Suspend-to-disk can be handled in several ways. We have a few options
 *	for putting the system to sleep - using the platform driver (e.g. ACPI
 *	or other hibernation_ops), powering off the system or rebooting the
 *	system (for testing) as well as the two test modes.
 *
 *	The system can support 'platform', and that is known a priori (and
 *	encoded by the presence of hibernation_ops). However, the user may
 *	choose 'shutdown' or 'reboot' as alternatives, as well as one fo the
 *	test modes, 'test' or 'testproc'.
 *
 *	show() will display what the mode is currently set to.
 *	store() will accept one of
 *
 *	'platform'
 *	'shutdown'
 *	'reboot'
 *	'test'
 *	'testproc'
 *
 *	It will only change to 'platform' if the system
 *	supports it (as determined by having hibernation_ops).
 */

static ssize_t disk_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	int i;
	char *start = buf;

	for (i = HIBERNATION_FIRST; i <= HIBERNATION_MAX; i++) {
		if (!hibernation_modes[i])
			continue;
		switch (i) {
		case HIBERNATION_SHUTDOWN:
		case HIBERNATION_REBOOT:
		case HIBERNATION_TEST:
		case HIBERNATION_TESTPROC:
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

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	mutex_lock(&pm_mutex);
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
		case HIBERNATION_TEST:
		case HIBERNATION_TESTPROC:
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
	mutex_unlock(&pm_mutex);
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
	unsigned int maj, min;
	dev_t res;
	int ret = -EINVAL;

	if (sscanf(buf, "%u:%u", &maj, &min) != 2)
		goto out;

	res = MKDEV(maj,min);
	if (maj != MAJOR(res) || min != MINOR(res))
		goto out;

	mutex_lock(&pm_mutex);
	swsusp_resume_device = res;
	mutex_unlock(&pm_mutex);
	printk(KERN_INFO "PM: Starting manual resume from disk\n");
	noresume = 0;
	software_resume();
	ret = n;
 out:
	return ret;
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

static struct attribute * g[] = {
	&disk_attr.attr,
	&resume_attr.attr,
	&image_size_attr.attr,
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

static int __init noresume_setup(char *str)
{
	noresume = 1;
	return 1;
}

__setup("noresume", noresume_setup);
__setup("resume_offset=", resume_offset_setup);
__setup("resume=", resume_setup);
