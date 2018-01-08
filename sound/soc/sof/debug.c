// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Yan Wang <yan.wan@linux.intel.com>
 *
 * Generic debug routines used to export DSP MMIO and memories to userspace
 * for firmware debugging.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <uapi/sound/sof-ipc.h>
#include "sof-priv.h"
#include "ops.h"

static int sof_dfsentry_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t sof_dfsentry_read(struct file *file, char __user *buffer,
				 size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry_io *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	int size;
	u32 *buf;
	loff_t pos = *ppos;
	size_t ret;

	size = dfse->size;

	/* validate position & count */
	if (pos < 0)
		return -EINVAL;
	if (pos >= size || !count)
		return 0;
	if (count > size - pos)
		count = size - pos;

	/* intermediate buffer size must be u32 multiple */
	size = (count + 3) & ~3;
	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* copy from DSP MMIO */
	pm_runtime_get(sdev->dev);
	memcpy_fromio(buf,  dfse->buf + pos, size);
	pm_runtime_put(sdev->dev);

	/* copy to userspace */
	ret = copy_to_user(buffer, buf, count);
	kfree(buf);

	/* update count & position if copy succeeded */
	if (ret == count)
		return -EFAULT;
	count -= ret;
	*ppos = pos + count;

	return count;
}

static const struct file_operations sof_dfs_fops = {
	.open = sof_dfsentry_open,
	.read = sof_dfsentry_read,
	.llseek = default_llseek,
};

int snd_sof_debugfs_create_item(struct snd_sof_dev *sdev,
				void __iomem *base, size_t size,
				const char *name)
{
	struct snd_sof_dfsentry_io *dfse;

	if (!sdev)
		return -EINVAL;

	dfse = kzalloc(sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->buf = base;
	dfse->size = size;
	dfse->sdev = sdev;

	dfse->dfsentry = debugfs_create_file(name, 0444, sdev->debugfs_root,
					     dfse, &sof_dfs_fops);
	if (!dfse->dfsentry) {
		dev_err(sdev->dev, "cannot create debugfs entry.\n");
		kfree(dfse);
		return -ENODEV;
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_debugfs_create_item);

int snd_sof_dbg_init(struct snd_sof_dev *sdev)
{
	const struct snd_sof_dsp_ops *ops = sdev->ops;
	const struct snd_sof_debugfs_map *map;
	int err = 0, i;

	/* use "sof" as top level debugFS dir */
	sdev->debugfs_root = debugfs_create_dir("sof", NULL);
	if (IS_ERR_OR_NULL(sdev->debugfs_root)) {
		dev_err(sdev->dev, "error: failed to create debugfs directory\n");
		return -EINVAL;
	}

	/* create debugFS files for platform specific MMIO/DSP memories */
	for (i = 0; i < ops->debug_map_count; i++) {
		map = &ops->debug_map[i];

		err = snd_sof_debugfs_create_item(sdev, sdev->bar[map->bar] +
						  map->offset, map->size,
						  map->name);
		if (err < 0)
			dev_err(sdev->dev, "cannot create debugfs for %s\n",
				map->name);
	}

	return err;
}
EXPORT_SYMBOL(snd_sof_dbg_init);

void snd_sof_free_debug(struct snd_sof_dev *sdev)
{
	debugfs_remove_recursive(sdev->debugfs_root);
}
EXPORT_SYMBOL(snd_sof_free_debug);
