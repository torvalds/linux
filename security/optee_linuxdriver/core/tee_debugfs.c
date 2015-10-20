/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#include "linux/tee_core.h"
#include "tee_debugfs.h"

static struct dentry *tee_debugfs_dir;

static ssize_t tee_trace_read(struct file *filp, char __user *userbuf,
			      size_t count, loff_t *ppos)
{
	struct tee *tee = filp->private_data;

	char buff[258];
	int len = sprintf(buff, "device=%s\n NO LOG AVAILABLE\n", tee->name);

	return simple_read_from_buffer(userbuf, count, ppos, buff, len);
}

static const struct file_operations log_tee_ops = {
	.read = tee_trace_read,
	.open = simple_open,
	.llseek = generic_file_llseek,
};

void tee_create_debug_dir(struct tee *tee)
{
	struct dentry *entry;
	struct device *dev = tee->miscdev.this_device;

	if (!tee_debugfs_dir)
		return;

	tee->dbg_dir = debugfs_create_dir(dev_name(dev), tee_debugfs_dir);
	if (!tee->dbg_dir)
		goto error_create_file;

	entry = debugfs_create_file("log", S_IRUGO, tee->dbg_dir,
				    tee, &log_tee_ops);
	if (!entry)
		goto error_create_file;

	return;

error_create_file:
	dev_err(dev, "can't create debugfs file\n");
	tee_delete_debug_dir(tee);
}

void tee_delete_debug_dir(struct tee *tee)
{
	if (!tee || !tee->dbg_dir)
		return;

	debugfs_remove_recursive(tee->dbg_dir);
}

void __init tee_init_debugfs(void)
{
	if (debugfs_initialized()) {
		tee_debugfs_dir = debugfs_create_dir("tee", NULL);
		if (IS_ERR(tee_debugfs_dir))
			pr_err("can't create debugfs dir\n");
	}
}

void __exit tee_exit_debugfs(void)
{
	if (tee_debugfs_dir)
		debugfs_remove(tee_debugfs_dir);
}
