/*
 * linux/kernel/power/user.c
 *
 * This file provides the user space interface for software suspend/resume.
 *
 * Copyright (C) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * This file is released under the GPLv2.
 *
 */

#include <linux/suspend.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pm.h>
#include <linux/fs.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/freezer.h>
#include <scsi/scsi_scan.h>

#include <asm/uaccess.h>

#include "power.h"

/*
 * NOTE: The SNAPSHOT_SET_SWAP_FILE and SNAPSHOT_PMOPS ioctls are obsolete and
 * will be removed in the future.  They are only preserved here for
 * compatibility with existing userland utilities.
 */
#define SNAPSHOT_SET_SWAP_FILE	_IOW(SNAPSHOT_IOC_MAGIC, 10, unsigned int)
#define SNAPSHOT_PMOPS		_IOW(SNAPSHOT_IOC_MAGIC, 12, unsigned int)

#define PMOPS_PREPARE	1
#define PMOPS_ENTER	2
#define PMOPS_FINISH	3

/*
 * NOTE: The following ioctl definitions are wrong and have been replaced with
 * correct ones.  They are only preserved here for compatibility with existing
 * userland utilities and will be removed in the future.
 */
#define SNAPSHOT_ATOMIC_SNAPSHOT	_IOW(SNAPSHOT_IOC_MAGIC, 3, void *)
#define SNAPSHOT_SET_IMAGE_SIZE		_IOW(SNAPSHOT_IOC_MAGIC, 6, unsigned long)
#define SNAPSHOT_AVAIL_SWAP		_IOR(SNAPSHOT_IOC_MAGIC, 7, void *)
#define SNAPSHOT_GET_SWAP_PAGE		_IOR(SNAPSHOT_IOC_MAGIC, 8, void *)


#define SNAPSHOT_MINOR	231

static struct snapshot_data {
	struct snapshot_handle handle;
	int swap;
	int mode;
	char frozen;
	char ready;
	char platform_support;
} snapshot_state;

atomic_t snapshot_device_available = ATOMIC_INIT(1);

static int snapshot_open(struct inode *inode, struct file *filp)
{
	struct snapshot_data *data;
	int error;

	mutex_lock(&pm_mutex);

	if (!atomic_add_unless(&snapshot_device_available, -1, 0)) {
		error = -EBUSY;
		goto Unlock;
	}

	if ((filp->f_flags & O_ACCMODE) == O_RDWR) {
		atomic_inc(&snapshot_device_available);
		error = -ENOSYS;
		goto Unlock;
	}
	if(create_basic_memory_bitmaps()) {
		atomic_inc(&snapshot_device_available);
		error = -ENOMEM;
		goto Unlock;
	}
	nonseekable_open(inode, filp);
	data = &snapshot_state;
	filp->private_data = data;
	memset(&data->handle, 0, sizeof(struct snapshot_handle));
	if ((filp->f_flags & O_ACCMODE) == O_RDONLY) {
		/* Hibernating.  The image device should be accessible. */
		data->swap = swsusp_resume_device ?
			swap_type_of(swsusp_resume_device, 0, NULL) : -1;
		data->mode = O_RDONLY;
		error = pm_notifier_call_chain(PM_HIBERNATION_PREPARE);
		if (error)
			pm_notifier_call_chain(PM_POST_HIBERNATION);
	} else {
		/*
		 * Resuming.  We may need to wait for the image device to
		 * appear.
		 */
		wait_for_device_probe();
		scsi_complete_async_scans();

		data->swap = -1;
		data->mode = O_WRONLY;
		error = pm_notifier_call_chain(PM_RESTORE_PREPARE);
		if (error)
			pm_notifier_call_chain(PM_POST_RESTORE);
	}
	if (error)
		atomic_inc(&snapshot_device_available);
	data->frozen = 0;
	data->ready = 0;
	data->platform_support = 0;

 Unlock:
	mutex_unlock(&pm_mutex);

	return error;
}

static int snapshot_release(struct inode *inode, struct file *filp)
{
	struct snapshot_data *data;

	mutex_lock(&pm_mutex);

	swsusp_free();
	free_basic_memory_bitmaps();
	data = filp->private_data;
	free_all_swap_pages(data->swap);
	if (data->frozen)
		thaw_processes();
	pm_notifier_call_chain(data->mode == O_RDONLY ?
			PM_POST_HIBERNATION : PM_POST_RESTORE);
	atomic_inc(&snapshot_device_available);

	mutex_unlock(&pm_mutex);

	return 0;
}

static ssize_t snapshot_read(struct file *filp, char __user *buf,
                             size_t count, loff_t *offp)
{
	struct snapshot_data *data;
	ssize_t res;

	mutex_lock(&pm_mutex);

	data = filp->private_data;
	if (!data->ready) {
		res = -ENODATA;
		goto Unlock;
	}
	res = snapshot_read_next(&data->handle, count);
	if (res > 0) {
		if (copy_to_user(buf, data_of(data->handle), res))
			res = -EFAULT;
		else
			*offp = data->handle.offset;
	}

 Unlock:
	mutex_unlock(&pm_mutex);

	return res;
}

static ssize_t snapshot_write(struct file *filp, const char __user *buf,
                              size_t count, loff_t *offp)
{
	struct snapshot_data *data;
	ssize_t res;

	mutex_lock(&pm_mutex);

	data = filp->private_data;
	res = snapshot_write_next(&data->handle, count);
	if (res > 0) {
		if (copy_from_user(data_of(data->handle), buf, res))
			res = -EFAULT;
		else
			*offp = data->handle.offset;
	}

	mutex_unlock(&pm_mutex);

	return res;
}

static long snapshot_ioctl(struct file *filp, unsigned int cmd,
							unsigned long arg)
{
	int error = 0;
	struct snapshot_data *data;
	loff_t size;
	sector_t offset;

	if (_IOC_TYPE(cmd) != SNAPSHOT_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > SNAPSHOT_IOC_MAXNR)
		return -ENOTTY;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!mutex_trylock(&pm_mutex))
		return -EBUSY;

	data = filp->private_data;

	switch (cmd) {

	case SNAPSHOT_FREEZE:
		if (data->frozen)
			break;

		printk("Syncing filesystems ... ");
		sys_sync();
		printk("done.\n");

		error = usermodehelper_disable();
		if (error)
			break;

		error = freeze_processes();
		if (error) {
			thaw_processes();
			usermodehelper_enable();
		}
		if (!error)
			data->frozen = 1;
		break;

	case SNAPSHOT_UNFREEZE:
		if (!data->frozen || data->ready)
			break;
		thaw_processes();
		usermodehelper_enable();
		data->frozen = 0;
		break;

	case SNAPSHOT_CREATE_IMAGE:
	case SNAPSHOT_ATOMIC_SNAPSHOT:
		if (data->mode != O_RDONLY || !data->frozen  || data->ready) {
			error = -EPERM;
			break;
		}
		error = hibernation_snapshot(data->platform_support);
		if (!error)
			error = put_user(in_suspend, (int __user *)arg);
		if (!error)
			data->ready = 1;
		break;

	case SNAPSHOT_ATOMIC_RESTORE:
		snapshot_write_finalize(&data->handle);
		if (data->mode != O_WRONLY || !data->frozen ||
		    !snapshot_image_loaded(&data->handle)) {
			error = -EPERM;
			break;
		}
		error = hibernation_restore(data->platform_support);
		break;

	case SNAPSHOT_FREE:
		swsusp_free();
		memset(&data->handle, 0, sizeof(struct snapshot_handle));
		data->ready = 0;
		break;

	case SNAPSHOT_PREF_IMAGE_SIZE:
	case SNAPSHOT_SET_IMAGE_SIZE:
		image_size = arg;
		break;

	case SNAPSHOT_GET_IMAGE_SIZE:
		if (!data->ready) {
			error = -ENODATA;
			break;
		}
		size = snapshot_get_image_size();
		size <<= PAGE_SHIFT;
		error = put_user(size, (loff_t __user *)arg);
		break;

	case SNAPSHOT_AVAIL_SWAP_SIZE:
	case SNAPSHOT_AVAIL_SWAP:
		size = count_swap_pages(data->swap, 1);
		size <<= PAGE_SHIFT;
		error = put_user(size, (loff_t __user *)arg);
		break;

	case SNAPSHOT_ALLOC_SWAP_PAGE:
	case SNAPSHOT_GET_SWAP_PAGE:
		if (data->swap < 0 || data->swap >= MAX_SWAPFILES) {
			error = -ENODEV;
			break;
		}
		offset = alloc_swapdev_block(data->swap);
		if (offset) {
			offset <<= PAGE_SHIFT;
			error = put_user(offset, (loff_t __user *)arg);
		} else {
			error = -ENOSPC;
		}
		break;

	case SNAPSHOT_FREE_SWAP_PAGES:
		if (data->swap < 0 || data->swap >= MAX_SWAPFILES) {
			error = -ENODEV;
			break;
		}
		free_all_swap_pages(data->swap);
		break;

	case SNAPSHOT_SET_SWAP_FILE: /* This ioctl is deprecated */
		if (!swsusp_swap_in_use()) {
			/*
			 * User space encodes device types as two-byte values,
			 * so we need to recode them
			 */
			if (old_decode_dev(arg)) {
				data->swap = swap_type_of(old_decode_dev(arg),
							0, NULL);
				if (data->swap < 0)
					error = -ENODEV;
			} else {
				data->swap = -1;
				error = -EINVAL;
			}
		} else {
			error = -EPERM;
		}
		break;

	case SNAPSHOT_S2RAM:
		if (!data->frozen) {
			error = -EPERM;
			break;
		}
		/*
		 * Tasks are frozen and the notifiers have been called with
		 * PM_HIBERNATION_PREPARE
		 */
		error = suspend_devices_and_enter(PM_SUSPEND_MEM);
		break;

	case SNAPSHOT_PLATFORM_SUPPORT:
		data->platform_support = !!arg;
		break;

	case SNAPSHOT_POWER_OFF:
		if (data->platform_support)
			error = hibernation_platform_enter();
		break;

	case SNAPSHOT_PMOPS: /* This ioctl is deprecated */
		error = -EINVAL;

		switch (arg) {

		case PMOPS_PREPARE:
			data->platform_support = 1;
			error = 0;
			break;

		case PMOPS_ENTER:
			if (data->platform_support)
				error = hibernation_platform_enter();
			break;

		case PMOPS_FINISH:
			if (data->platform_support)
				error = 0;
			break;

		default:
			printk(KERN_ERR "SNAPSHOT_PMOPS: invalid argument %ld\n", arg);

		}
		break;

	case SNAPSHOT_SET_SWAP_AREA:
		if (swsusp_swap_in_use()) {
			error = -EPERM;
		} else {
			struct resume_swap_area swap_area;
			dev_t swdev;

			error = copy_from_user(&swap_area, (void __user *)arg,
					sizeof(struct resume_swap_area));
			if (error) {
				error = -EFAULT;
				break;
			}

			/*
			 * User space encodes device types as two-byte values,
			 * so we need to recode them
			 */
			swdev = old_decode_dev(swap_area.dev);
			if (swdev) {
				offset = swap_area.offset;
				data->swap = swap_type_of(swdev, offset, NULL);
				if (data->swap < 0)
					error = -ENODEV;
			} else {
				data->swap = -1;
				error = -EINVAL;
			}
		}
		break;

	default:
		error = -ENOTTY;

	}

	mutex_unlock(&pm_mutex);

	return error;
}

static const struct file_operations snapshot_fops = {
	.open = snapshot_open,
	.release = snapshot_release,
	.read = snapshot_read,
	.write = snapshot_write,
	.llseek = no_llseek,
	.unlocked_ioctl = snapshot_ioctl,
};

static struct miscdevice snapshot_device = {
	.minor = SNAPSHOT_MINOR,
	.name = "snapshot",
	.fops = &snapshot_fops,
};

static int __init snapshot_device_init(void)
{
	return misc_register(&snapshot_device);
};

device_initcall(snapshot_device_init);
