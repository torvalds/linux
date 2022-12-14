// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014  Google, Inc.
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/rtmutex.h>
#include "internal.h"

static DEFINE_RT_MUTEX(pmsg_lock);

static ssize_t write_pmsg(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct pstore_record record;
	int ret;

	if (!count)
		return 0;

	pstore_record_init(&record, psinfo);
	record.type = PSTORE_TYPE_PMSG;
	record.size = count;

	/* check outside lock, page in any data. write_user also checks */
	if (!access_ok(buf, count))
		return -EFAULT;

	rt_mutex_lock(&pmsg_lock);
	ret = psinfo->write_user(&record, buf);
	rt_mutex_unlock(&pmsg_lock);
	return ret ? ret : count;
}

static const struct file_operations pmsg_fops = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
	.write		= write_pmsg,
};

static struct class *pmsg_class;
static int pmsg_major;
#define PMSG_NAME "pmsg"
#undef pr_fmt
#define pr_fmt(fmt) PMSG_NAME ": " fmt

static char *pmsg_devnode(struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0220;
	return NULL;
}

void pstore_register_pmsg(void)
{
	struct device *pmsg_device;

	pmsg_major = register_chrdev(0, PMSG_NAME, &pmsg_fops);
	if (pmsg_major < 0) {
		pr_err("register_chrdev failed\n");
		goto err;
	}

	pmsg_class = class_create(THIS_MODULE, PMSG_NAME);
	if (IS_ERR(pmsg_class)) {
		pr_err("device class file already in use\n");
		goto err_class;
	}
	pmsg_class->devnode = pmsg_devnode;

	pmsg_device = device_create(pmsg_class, NULL, MKDEV(pmsg_major, 0),
					NULL, "%s%d", PMSG_NAME, 0);
	if (IS_ERR(pmsg_device)) {
		pr_err("failed to create device\n");
		goto err_device;
	}
	return;

err_device:
	class_destroy(pmsg_class);
err_class:
	unregister_chrdev(pmsg_major, PMSG_NAME);
err:
	return;
}

void pstore_unregister_pmsg(void)
{
	device_destroy(pmsg_class, MKDEV(pmsg_major, 0));
	class_destroy(pmsg_class);
	unregister_chrdev(pmsg_major, PMSG_NAME);
}
