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
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pm.h>

#include "power.h"


extern suspend_disk_method_t pm_disk_mode;

extern int swsusp_shrink_memory(void);
extern int swsusp_suspend(void);
extern int swsusp_write(struct pbe *pblist, unsigned int nr_pages);
extern int swsusp_check(void);
extern int swsusp_read(struct pbe **pblist_ptr);
extern void swsusp_close(void);
extern int swsusp_resume(void);


static int noresume = 0;
char resume_file[256] = CONFIG_PM_STD_PARTITION;
dev_t swsusp_resume_device;

/**
 *	power_down - Shut machine down for hibernate.
 *	@mode:		Suspend-to-disk mode
 *
 *	Use the platform driver, if configured so, and return gracefully if it
 *	fails.
 *	Otherwise, try to power off and reboot. If they fail, halt the machine,
 *	there ain't no turning back.
 */

static void power_down(suspend_disk_method_t mode)
{
	int error = 0;

	switch(mode) {
	case PM_DISK_PLATFORM:
		kernel_power_off_prepare();
		error = pm_ops->enter(PM_SUSPEND_DISK);
		break;
	case PM_DISK_SHUTDOWN:
		kernel_power_off();
		break;
	case PM_DISK_REBOOT:
		kernel_restart(NULL);
		break;
	}
	kernel_halt();
	/* Valid image is on the disk, if we continue we risk serious data corruption
	   after resume. */
	printk(KERN_CRIT "Please power me down manually\n");
	while(1);
}


static int in_suspend __nosavedata = 0;


static inline void platform_finish(void)
{
	if (pm_disk_mode == PM_DISK_PLATFORM) {
		if (pm_ops && pm_ops->finish)
			pm_ops->finish(PM_SUSPEND_DISK);
	}
}

static int prepare_processes(void)
{
	int error;

	pm_prepare_console();
	sys_sync();
	disable_nonboot_cpus();

	if (freeze_processes()) {
		error = -EBUSY;
		goto thaw;
	}

	if (pm_disk_mode == PM_DISK_PLATFORM) {
		if (pm_ops && pm_ops->prepare) {
			if ((error = pm_ops->prepare(PM_SUSPEND_DISK)))
				goto thaw;
		}
	}

	/* Free memory before shutting down devices. */
	if (!(error = swsusp_shrink_memory()))
		return 0;
thaw:
	thaw_processes();
	enable_nonboot_cpus();
	pm_restore_console();
	return error;
}

static void unprepare_processes(void)
{
	platform_finish();
	thaw_processes();
	enable_nonboot_cpus();
	pm_restore_console();
}

/**
 *	pm_suspend_disk - The granpappy of power management.
 *
 *	If we're going through the firmware, then get it over with quickly.
 *
 *	If not, then call swsusp to do its thing, then figure out how
 *	to power down the system.
 */

int pm_suspend_disk(void)
{
	int error;

	error = prepare_processes();
	if (error)
		return error;

	error = device_suspend(PMSG_FREEZE);
	if (error) {
		printk("Some devices failed to suspend\n");
		unprepare_processes();
		return error;
	}

	pr_debug("PM: snapshotting memory.\n");
	in_suspend = 1;
	if ((error = swsusp_suspend()))
		goto Done;

	if (in_suspend) {
		device_resume();
		pr_debug("PM: writing image.\n");
		error = swsusp_write(pagedir_nosave, nr_copy_pages);
		if (!error)
			power_down(pm_disk_mode);
		else {
			swsusp_free();
			unprepare_processes();
			return error;
		}
	} else
		pr_debug("PM: Image restored successfully.\n");

	swsusp_free();
 Done:
	device_resume();
	unprepare_processes();
	return error;
}


/**
 *	software_resume - Resume from a saved image.
 *
 *	Called as a late_initcall (so all devices are discovered and
 *	initialized), we call swsusp to see if we have a saved image or not.
 *	If so, we quiesce devices, the restore the saved image. We will
 *	return above (in pm_suspend_disk() ) if everything goes well.
 *	Otherwise, we fail gracefully and return to the normally
 *	scheduled program.
 *
 */

static int software_resume(void)
{
	int error;

	down(&pm_sem);
	if (!swsusp_resume_device) {
		if (!strlen(resume_file)) {
			up(&pm_sem);
			return -ENOENT;
		}
		swsusp_resume_device = name_to_dev_t(resume_file);
		pr_debug("swsusp: Resume From Partition %s\n", resume_file);
	} else {
		pr_debug("swsusp: Resume From Partition %d:%d\n",
			 MAJOR(swsusp_resume_device), MINOR(swsusp_resume_device));
	}

	if (noresume) {
		/**
		 * FIXME: If noresume is specified, we need to find the partition
		 * and reset it back to normal swap space.
		 */
		up(&pm_sem);
		return 0;
	}

	pr_debug("PM: Checking swsusp image.\n");

	if ((error = swsusp_check()))
		goto Done;

	pr_debug("PM: Preparing processes for restore.\n");

	if ((error = prepare_processes())) {
		swsusp_close();
		goto Done;
	}

	pr_debug("PM: Reading swsusp image.\n");

	if ((error = swsusp_read(&pagedir_nosave))) {
		swsusp_free();
		goto Thaw;
	}

	pr_debug("PM: Preparing devices for restore.\n");

	if ((error = device_suspend(PMSG_FREEZE))) {
		printk("Some devices failed to suspend\n");
		swsusp_free();
		goto Thaw;
	}

	mb();

	pr_debug("PM: Restoring saved image.\n");
	swsusp_resume();
	pr_debug("PM: Restore failed, recovering.n");
	device_resume();
 Thaw:
	unprepare_processes();
 Done:
	/* For success case, the suspend path will release the lock */
	up(&pm_sem);
	pr_debug("PM: Resume from disk failed.\n");
	return 0;
}

late_initcall(software_resume);


static char * pm_disk_modes[] = {
	[PM_DISK_FIRMWARE]	= "firmware",
	[PM_DISK_PLATFORM]	= "platform",
	[PM_DISK_SHUTDOWN]	= "shutdown",
	[PM_DISK_REBOOT]	= "reboot",
};

/**
 *	disk - Control suspend-to-disk mode
 *
 *	Suspend-to-disk can be handled in several ways. The greatest
 *	distinction is who writes memory to disk - the firmware or the OS.
 *	If the firmware does it, we assume that it also handles suspending
 *	the system.
 *	If the OS does it, then we have three options for putting the system
 *	to sleep - using the platform driver (e.g. ACPI or other PM registers),
 *	powering off the system or rebooting the system (for testing).
 *
 *	The system will support either 'firmware' or 'platform', and that is
 *	known a priori (and encoded in pm_ops). But, the user may choose
 *	'shutdown' or 'reboot' as alternatives.
 *
 *	show() will display what the mode is currently set to.
 *	store() will accept one of
 *
 *	'firmware'
 *	'platform'
 *	'shutdown'
 *	'reboot'
 *
 *	It will only change to 'firmware' or 'platform' if the system
 *	supports it (as determined from pm_ops->pm_disk_mode).
 */

static ssize_t disk_show(struct subsystem * subsys, char * buf)
{
	return sprintf(buf, "%s\n", pm_disk_modes[pm_disk_mode]);
}


static ssize_t disk_store(struct subsystem * s, const char * buf, size_t n)
{
	int error = 0;
	int i;
	int len;
	char *p;
	suspend_disk_method_t mode = 0;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	down(&pm_sem);
	for (i = PM_DISK_FIRMWARE; i < PM_DISK_MAX; i++) {
		if (!strncmp(buf, pm_disk_modes[i], len)) {
			mode = i;
			break;
		}
	}
	if (mode) {
		if (mode == PM_DISK_SHUTDOWN || mode == PM_DISK_REBOOT)
			pm_disk_mode = mode;
		else {
			if (pm_ops && pm_ops->enter &&
			    (mode == pm_ops->pm_disk_mode))
				pm_disk_mode = mode;
			else
				error = -EINVAL;
		}
	} else
		error = -EINVAL;

	pr_debug("PM: suspend-to-disk mode set to '%s'\n",
		 pm_disk_modes[mode]);
	up(&pm_sem);
	return error ? error : n;
}

power_attr(disk);

static ssize_t resume_show(struct subsystem * subsys, char *buf)
{
	return sprintf(buf,"%d:%d\n", MAJOR(swsusp_resume_device),
		       MINOR(swsusp_resume_device));
}

static ssize_t resume_store(struct subsystem *subsys, const char *buf, size_t n)
{
	unsigned int maj, min;
	dev_t res;
	int ret = -EINVAL;

	if (sscanf(buf, "%u:%u", &maj, &min) != 2)
		goto out;

	res = MKDEV(maj,min);
	if (maj != MAJOR(res) || min != MINOR(res))
		goto out;

	down(&pm_sem);
	swsusp_resume_device = res;
	up(&pm_sem);
	printk("Attempting manual resume\n");
	noresume = 0;
	software_resume();
	ret = n;
out:
	return ret;
}

power_attr(resume);

static ssize_t image_size_show(struct subsystem * subsys, char *buf)
{
	return sprintf(buf, "%u\n", image_size);
}

static ssize_t image_size_store(struct subsystem * subsys, const char * buf, size_t n)
{
	unsigned int size;

	if (sscanf(buf, "%u", &size) == 1) {
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
	return sysfs_create_group(&power_subsys.kset.kobj,&attr_group);
}

core_initcall(pm_disk_init);


static int __init resume_setup(char *str)
{
	if (noresume)
		return 1;

	strncpy( resume_file, str, 255 );
	return 1;
}

static int __init noresume_setup(char *str)
{
	noresume = 1;
	return 1;
}

__setup("noresume", noresume_setup);
__setup("resume=", resume_setup);
