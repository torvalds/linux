/**
 * Copyright (C) 2011 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


/**
 * @file mali_kernel_sysfs.c
 * Implementation of some sysfs data exports
 */
#include <linux/fs.h>       /* file system operations */
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

/* the mali kernel subsystem types */
#include "mali_kernel_subsystem.h"

#include "mali_kernel_linux.h"
#include "mali_kernel_sysfs.h"

#include "mali_kernel_license.h"

#if MALI_LICENSE_IS_GPL
static struct dentry *mali_debugfs_dir;
#endif


#if MALI_STATE_TRACKING
static int mali_seq_internal_state_show(struct seq_file *seq_file, void *v)
{
	u32 len = 0;
	u32 size;
	char *buf;

	size = seq_get_buf(seq_file, &buf);

	if(!size)
	{
			return -ENOMEM;
	}

	/* Create the internal state dump. */
	len  = snprintf(buf+len, size-len, "Mali device driver %s\n", SVN_REV_STRING);
	len += snprintf(buf+len, size-len, "License: %s\n\n", MALI_KERNEL_LINUX_LICENSE);

	len += _mali_kernel_core_dump_state(buf + len, size - len);

	seq_commit(seq_file, len);

	return 0;
}

static int mali_seq_internal_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, mali_seq_internal_state_show, NULL);
}

static const struct file_operations mali_seq_internal_state_fops = {
	.owner = THIS_MODULE,
	.open = mali_seq_internal_state_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif /* MALI_STATE_TRACKING */

int mali_sysfs_register(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	int err = 0;
#if MALI_LICENSE_IS_GPL
	struct device * mdev;

	device->mali_class = class_create(THIS_MODULE, mali_dev_name);
	if (IS_ERR(device->mali_class))
	{
		err = PTR_ERR(device->mali_class);
		goto init_class_err;
	}
	mdev = device_create(device->mali_class, NULL, dev, NULL, mali_dev_name);
	if (IS_ERR(mdev))
	{
		err = PTR_ERR(mdev);
		goto init_mdev_err;
	}

	mali_debugfs_dir = debugfs_create_dir(mali_dev_name, NULL);
	if(ERR_PTR(-ENODEV) == mali_debugfs_dir) {
		/* Debugfs not supported. */
		mali_debugfs_dir = NULL;
	} else {
		if(NULL != mali_debugfs_dir)
		{
			/* Debugfs directory created successfully; create files now */
#if MALI_STATE_TRACKING
			debugfs_create_file("state_dump", 0400, mali_debugfs_dir, NULL, &mali_seq_internal_state_fops);
#endif
		}
	}
#endif /* MALI_LICENSE_IS_GPL */

	/* Success! */
	return 0;

	/* Error handling */
#if MALI_LICENSE_IS_GPL
	if(NULL != mali_debugfs_dir)
	{
		debugfs_remove_recursive(mali_debugfs_dir);
	}
	device_destroy(device->mali_class, dev);
init_mdev_err:
	class_destroy(device->mali_class);
init_class_err:
#endif
	return err;
}

int mali_sysfs_unregister(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
#if MALI_LICENSE_IS_GPL
	if(NULL != mali_debugfs_dir)
	{
		debugfs_remove_recursive(mali_debugfs_dir);
	}
	device_destroy(device->mali_class, dev);
	class_destroy(device->mali_class);
#endif

	return 0;
}

