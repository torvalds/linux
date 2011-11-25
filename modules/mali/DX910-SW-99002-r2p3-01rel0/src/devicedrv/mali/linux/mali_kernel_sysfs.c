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

#include <linux/fs.h>
#include <linux/device.h>
#include "mali_kernel_license.h"
#include "mali_kernel_linux.h"
#include "mali_ukk.h"

#if MALI_LICENSE_IS_GPL

#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include "mali_kernel_subsystem.h"
#include "mali_kernel_sysfs.h"
#include "mali_kernel_profiling.h"

static struct dentry *mali_debugfs_dir = NULL;

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


#if MALI_TIMELINE_PROFILING_ENABLED
static ssize_t profiling_record_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	r = sprintf(buf, "%u\n", _mali_profiling_is_recording() ? 1 : 0);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t profiling_record_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	if (val != 0)
	{
		u32 limit = MALI_PROFILING_MAX_BUFFER_ENTRIES; /* This can be made configurable at a later stage if we need to */

		/* check if we are already recording */
		if (MALI_TRUE == _mali_profiling_is_recording())
		{
			MALI_DEBUG_PRINT(3, ("Recording of profiling events already in progress\n"));
			return -EFAULT;
		}

		/* check if we need to clear out an old recording first */
		if (MALI_TRUE == _mali_profiling_have_recording())
		{
			if (_MALI_OSK_ERR_OK != _mali_profiling_clear())
			{
				MALI_DEBUG_PRINT(3, ("Failed to clear existing recording of profiling events\n"));
				return -EFAULT;
			}
		}

		/* start recording profiling data */
		if (_MALI_OSK_ERR_OK != _mali_profiling_start(&limit))
		{
			MALI_DEBUG_PRINT(3, ("Failed to start recording of profiling events\n"));
			return -EFAULT;
		}

		MALI_DEBUG_PRINT(3, ("Profiling recording started (max %u events)\n", limit));
	}
	else
	{
		/* stop recording profiling data */
		u32 count = 0;
		if (_MALI_OSK_ERR_OK != _mali_profiling_stop(&count))
		{
			MALI_DEBUG_PRINT(2, ("Failed to stop recording of profiling events\n"));
			return -EFAULT;
		}
		
		MALI_DEBUG_PRINT(2, ("Profiling recording stopped (recorded %u events)\n", count));
	}

	*ppos += cnt;
	return cnt;
}

static const struct file_operations profiling_record_fops = {
	.owner = THIS_MODULE,
	.read  = profiling_record_read,
	.write = profiling_record_write,
};

static void *profiling_events_start(struct seq_file *s, loff_t *pos)
{
	loff_t *spos;

	/* check if we have data avaiable */
	if (MALI_TRUE != _mali_profiling_have_recording())
	{
		return NULL;
	}

	spos = kmalloc(sizeof(loff_t), GFP_KERNEL);
	if (NULL == spos)
	{
		return NULL;
	}

	*spos = *pos;
	return spos;
}

static void *profiling_events_next(struct seq_file *s, void *v, loff_t *pos)
{
	loff_t *spos = v;

	/* check if we have data avaiable */
	if (MALI_TRUE != _mali_profiling_have_recording())
	{
		return NULL;
	}

	/* check if the next entry actually is avaiable */
	if (_mali_profiling_get_count() <= (u32)(*spos + 1))
	{
		return NULL;
	}

	*pos = ++*spos;
	return spos;
}

static void profiling_events_stop(struct seq_file *s, void *v)
{
	kfree(v);
}

static int profiling_events_show(struct seq_file *seq_file, void *v)
{
	loff_t *spos = v;
	u32 index;
	u64 timestamp;
	u32 event_id;
	u32 data[5];

	index = (u32)*spos;

	/* Retrieve all events */
	if (_MALI_OSK_ERR_OK == _mali_profiling_get_event(index, &timestamp, &event_id, data))
	{
		seq_printf(seq_file, "%llu %u %u %u %u %u %u\n", timestamp, event_id, data[0], data[1], data[2], data[3], data[4]);
		return 0;
	}

	return 0;
}

static const struct seq_operations profiling_events_seq_ops = {
	.start = profiling_events_start,
	.next  = profiling_events_next,
	.stop  = profiling_events_stop,
	.show  = profiling_events_show
};

static int profiling_events_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &profiling_events_seq_ops);
}

static const struct file_operations profiling_events_fops = {
	.owner = THIS_MODULE,
	.open = profiling_events_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static ssize_t profiling_proc_default_enable_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	int r;

	r = sprintf(buf, "%u\n", _mali_profiling_get_default_enable_state() ? 1 : 0);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t profiling_proc_default_enable_write(struct file *filp, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
	{
		return -EINVAL;
	}

	if (copy_from_user(&buf, ubuf, cnt))
	{
		return -EFAULT;
	}

	buf[cnt] = 0;

	ret = strict_strtoul(buf, 10, &val);
	if (ret < 0)
	{
		return ret;
	}

	_mali_profiling_set_default_enable_state(val != 0 ? MALI_TRUE : MALI_FALSE);

	*ppos += cnt;
	return cnt;
}

static const struct file_operations profiling_proc_default_enable_fops = {
	.owner = THIS_MODULE,
	.read  = profiling_proc_default_enable_read,
	.write = profiling_proc_default_enable_write,
};
#endif

static ssize_t memory_used_read(struct file *filp, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	size_t r;
	u32 mem = _mali_ukk_report_memory_usage();

	r = snprintf(buf, 64, "%u\n", mem);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static const struct file_operations memory_usage_fops = {
	.owner = THIS_MODULE,
	.read = memory_used_read,
};

int mali_sysfs_register(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	int err = 0;
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
	if(ERR_PTR(-ENODEV) == mali_debugfs_dir)
	{
		/* Debugfs not supported. */
		mali_debugfs_dir = NULL;
	}
	else
	{
		if(NULL != mali_debugfs_dir)
		{
			/* Debugfs directory created successfully; create files now */
#if MALI_TIMELINE_PROFILING_ENABLED
			struct dentry *mali_profiling_dir = debugfs_create_dir("profiling", mali_debugfs_dir);
			if (mali_profiling_dir != NULL)
			{
				struct dentry *mali_profiling_proc_dir = debugfs_create_dir("proc", mali_profiling_dir);
				if (mali_profiling_proc_dir != NULL)
				{
					struct dentry *mali_profiling_proc_default_dir = debugfs_create_dir("default", mali_profiling_proc_dir);
					if (mali_profiling_proc_default_dir != NULL)
					{
						debugfs_create_file("enable", 0600, mali_profiling_proc_default_dir, NULL, &profiling_proc_default_enable_fops);
					}
				}
				debugfs_create_file("record", 0600, mali_profiling_dir, NULL, &profiling_record_fops);
				debugfs_create_file("events", 0400, mali_profiling_dir, NULL, &profiling_events_fops);
			}
#endif

#if MALI_STATE_TRACKING
			debugfs_create_file("state_dump", 0400, mali_debugfs_dir, NULL, &mali_seq_internal_state_fops);
#endif

			debugfs_create_file("memory_usage", 0400, mali_debugfs_dir, NULL, &memory_usage_fops);
		}
	}

	/* Success! */
	return 0;

	/* Error handling */
init_mdev_err:
	class_destroy(device->mali_class);
init_class_err:

	return err;
}

int mali_sysfs_unregister(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	if(NULL != mali_debugfs_dir)
	{
		debugfs_remove_recursive(mali_debugfs_dir);
	}
	device_destroy(device->mali_class, dev);
	class_destroy(device->mali_class);

	return 0;
}

#else

/* Dummy implementations for non-GPL */

int mali_sysfs_register(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	return 0;
}

int mali_sysfs_unregister(struct mali_dev *device, dev_t dev, const char *mali_dev_name)
{
	return 0;
}


#endif
