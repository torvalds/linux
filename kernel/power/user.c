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
#include <linux/compat.h>
#include <linux/console.h>
#include <linux/cpu.h>
#include <linux/freezer.h>

#include <asm/uaccess.h>

#include "power.h"


#define SNAPSHOT_MINOR	231

static struct snapshot_data {
	struct snapshot_handle handle;
	int swap;
	int mode;
	bool frozen;
	bool ready;
	bool platform_support;
	bool free_bitmaps;
} snapshot_state;

atomic_t snapshot_device_available = ATOMIC_INIT(1);

static int snapshot_open(struct inode *inode, struct file *filp)
{
	struct snapshot_data *data;
	int error;

	lock_system_sleep();

	if (!atomic_add_unless(&snapshot_device_available, -1, 0)) {
		error = -EBUSY;
		goto Unlock;
	}

	if ((filp->f_flags & O_ACCMODE) == O_RDWR) {
		atomic_inc(&snapshot_device_available);
		error = -ENOSYS;
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

		data->swap = -1;
		data->mode = O_WRONLY;
		error = pm_notifier_call_chain(PM_RESTORE_PREPARE);
		if (!error) {
			error = create_basic_memory_bitmaps();
			data->free_bitmaps = !error;
		}
		if (error)
			pm_notifier_call_chain(PM_POST_RESTORE);
	}
	if (error)
		atomic_inc(&snapshot_device_available);

	data->frozen = false;
	data->ready = false;
	data->platform_support = false;

 Unlock:
	unlock_system_sleep();

	return error;
}

static int snapshot_release(struct inode *inode, struct file *filp)
{
	struct snapshot_data *data;

	lock_system_sleep();

	swsusp_free();
	data = filp->private_data;
	free_all_swap_pages(data->swap);
	if (data->frozen) {
		pm_restore_gfp_mask();
		free_basic_memory_bitmaps();
		thaw_processes();
	} else if (data->free_bitmaps) {
		free_basic_memory_bitmaps();
	}
	pm_notifier_call_chain(data->mode == O_RDONLY ?
			PM_POST_HIBERNATION : PM_POST_RESTORE);
	atomic_inc(&snapshot_device_available);

	unlock_system_sleep();

	return 0;
}

static ssize_t snapshot_read(struct file *filp, char __user *buf,
                             size_t count, loff_t *offp)
{
	struct snapshot_data *data;
	ssize_t res;
	loff_t pg_offp = *offp & ~PAGE_MASK;

	lock_system_sleep();

	data = filp->private_data;
	if (!data->ready) {
		res = -ENODATA;
		goto Unlock;
	}
	if (!pg_offp) { /* on page boundary? */
		res = snapshot_read_next(&data->handle);
		if (res <= 0)
			goto Unlock;
	} else {
		res = PAGE_SIZE - pg_offp;
	}

	res = simple_read_from_buffer(buf, count, &pg_offp,
			data_of(data->handle), res);
	if (res > 0)
		*offp += res;

 Unlock:
	unlock_system_sleep();

	return res;
}

static ssize_t snapshot_write(struct file *filp, const char __user *buf,
                              size_t count, loff_t *offp)
{
	struct snapshot_data *data;
	ssize_t res;
	loff_t pg_offp = *offp & ~PAGE_MASK;

	lock_system_sleep();

	data = filp->private_data;

	if (!pg_offp) {
		res = snapshot_write_next(&data->handle);
		if (res <= 0)
			goto unlock;
	} else {
		res = PAGE_SIZE - pg_offp;
	}

	res = simple_write_to_buffer(data_of(data->handle), res, &pg_offp,
			buf, count);
	if (res > 0)
		*offp += res;
unlock:
	unlock_system_sleep();

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

	lock_device_hotplug();
	data = filp->private_data;

	switch (cmd) {

	case SNAPSHOT_FREEZE:
		if (data->frozen)
			break;

		printk("Syncing filesystems ... ");
		sys_sync();
		printk("done.\n");

		error = freeze_processes();
		if (error)
			break;

		error = create_basic_memory_bitmaps();
		if (error)
			thaw_processes();
		else
			data->frozen = true;

		break;

	case SNAPSHOT_UNFREEZE:
		if (!data->frozen || data->ready)
			break;
		pm_restore_gfp_mask();
		free_basic_memory_bitmaps();
		data->free_bitmaps = false;
		thaw_processes();
		data->frozen = false;
		break;

	case SNAPSHOT_CREATE_IMAGE:
		if (data->mode != O_RDONLY || !data->frozen  || data->ready) {
			error = -EPERM;
			break;
		}
		pm_restore_gfp_mask();
		error = hibernation_snapshot(data->platform_support);
		if (!error) {
			error = put_user(in_suspend, (int __user *)arg);
			data->ready = !freezer_test_done && !error;
			freezer_test_done = false;
		}
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
		data->ready = false;
		/*
		 * It is necessary to thaw kernel threads here, because
		 * SNAPSHOT_CREATE_IMAGE may be invoked directly after
		 * SNAPSHOT_FREE.  In that case, if kernel threads were not
		 * thawed, the preallocation of memory carried out by
		 * hibernation_snapshot() might run into problems (i.e. it
		 * might fail or even deadlock).
		 */
		thaw_kernel_threads();
		break;

	case SNAPSHOT_PREF_IMAGE_SIZE:
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
		size = count_swap_pages(data->swap, 1);
		size <<= PAGE_SHIFT;
		error = put_user(size, (loff_t __user *)arg);
		break;

	case SNAPSHOT_ALLOC_SWAP_PAGE:
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
		data->ready = false;
		break;

	case SNAPSHOT_PLATFORM_SUPPORT:
		data->platform_support = !!arg;
		break;

	case SNAPSHOT_POWER_OFF:
		if (data->platform_support)
			error = hibernation_platform_enter();
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
			swdev = new_decode_dev(swap_area.dev);
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

	unlock_device_hotplug();
	mutex_unlock(&pm_mutex);

	return error;
}

#ifdef CONFIG_COMPAT

struct compat_resume_swap_area {
	compat_loff_t offset;
	u32 dev;
} __packed;

static long
snapshot_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	BUILD_BUG_ON(sizeof(loff_t) != sizeof(compat_loff_t));

	switch (cmd) {
	case SNAPSHOT_GET_IMAGE_SIZE:
	case SNAPSHOT_AVAIL_SWAP_SIZE:
	case SNAPSHOT_ALLOC_SWAP_PAGE: {
		compat_loff_t __user *uoffset = compat_ptr(arg);
		loff_t offset;
		mm_segment_t old_fs;
		int err;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = snapshot_ioctl(file, cmd, (unsigned long) &offset);
		set_fs(old_fs);
		if (!err && put_user(offset, uoffset))
			err = -EFAULT;
		return err;
	}

	case SNAPSHOT_CREATE_IMAGE:
		return snapshot_ioctl(file, cmd,
				      (unsigned long) compat_ptr(arg));

	case SNAPSHOT_SET_SWAP_AREA: {
		struct compat_resume_swap_area __user *u_swap_area =
			compat_ptr(arg);
		struct resume_swap_area swap_area;
		mm_segment_t old_fs;
		int err;

		err = get_user(swap_area.offset, &u_swap_area->offset);
		err |= get_user(swap_area.dev, &u_swap_area->dev);
		if (err)
			return -EFAULT;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = snapshot_ioctl(file, SNAPSHOT_SET_SWAP_AREA,
				     (unsigned long) &swap_area);
		set_fs(old_fs);
		return err;
	}

	default:
		return snapshot_ioctl(file, cmd, arg);
	}
}

#endif /* CONFIG_COMPAT */

static const struct file_operations snapshot_fops = {
	.open = snapshot_open,
	.release = snapshot_release,
	.read = snapshot_read,
	.write = snapshot_write,
	.llseek = no_llseek,
	.unlocked_ioctl = snapshot_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = snapshot_compat_ioctl,
#endif
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
