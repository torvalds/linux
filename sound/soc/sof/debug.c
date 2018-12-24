// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//
// Generic debug routines used to export DSP MMIO and memories to userspace
// for firmware debugging.
//

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include "sof-priv.h"
#include "ops.h"

static ssize_t sof_dfsentry_read(struct file *file, char __user *buffer,
				 size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry_io *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	int size;
	u32 *buf;
	loff_t pos = *ppos;
	size_t size_ret;
	int ret;

	size = dfse->size;

	/* validate position & count */
	if (pos < 0)
		return -EINVAL;
	if (pos >= size || !count)
		return 0;
	/* find the minimum. min() is not used since it adds sparse warnings */
	if (count > size - pos)
		count = size - pos;

	/* intermediate buffer size must be u32 multiple */
	size = round_up(count, 4);
	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* copy from DSP MMIO */
	pm_runtime_get_noresume(sdev->dev);

	memcpy_fromio(buf,  dfse->buf + pos, size);

	/*
	 * TODO: revisit to check if we need mark_last_busy, or if we
	 * should change to use xxx_put_sync[_suspend]().
	 */
	ret = pm_runtime_put_sync_autosuspend(sdev->dev);
	if (ret < 0)
		dev_warn(sdev->dev, "warn: debugFS failed to autosuspend %d\n",
			 ret);

	/* copy to userspace */
	size_ret = copy_to_user(buffer, buf, count);
	kfree(buf);

	/* update count & position if copy succeeded */
	if (size_ret == count)
		return -EFAULT;
	count -= size_ret;
	*ppos = pos + count;

	return count;
}

static const struct file_operations sof_dfs_fops = {
	.open = simple_open,
	.read = sof_dfsentry_read,
	.llseek = default_llseek,
};

/* create FS entry for debug files that can expose DSP memories, registers */
int snd_sof_debugfs_io_create_item(struct snd_sof_dev *sdev,
				   void __iomem *base, size_t size,
				   const char *name)
{
	struct snd_sof_dfsentry_io *dfse;

	if (!sdev)
		return -EINVAL;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->buf = base;
	dfse->size = size;
	dfse->sdev = sdev;

	dfse->dfsentry = debugfs_create_file(name, 0444, sdev->debugfs_root,
					     dfse, &sof_dfs_fops);
	if (!dfse->dfsentry) {
		/* can't rely on debugfs, only log error and keep going */
		dev_err(sdev->dev, "error: cannot create debugfs entry %s\n",
			name);
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_debugfs_io_create_item);

/* create FS entry for debug files to expose kernel memory */
int snd_sof_debugfs_buf_create_item(struct snd_sof_dev *sdev,
				    void *base, size_t size,
				    const char *name)
{
	struct snd_sof_dfsentry_buf *dfse;

	if (!sdev)
		return -EINVAL;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->buf = base;
	dfse->size = size;
	dfse->sdev = sdev;

	dfse->dfsentry = debugfs_create_file(name, 0444, sdev->debugfs_root,
					     dfse, &sof_dfs_fops);
	if (!dfse->dfsentry) {
		/* can't rely on debugfs, only log error and keep going */
		dev_err(sdev->dev, "error: cannot create debugfs entry %s\n",
			name);
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_debugfs_buf_create_item);

int snd_sof_dbg_init(struct snd_sof_dev *sdev)
{
	const struct snd_sof_dsp_ops *ops = sof_ops(sdev);
	const struct snd_sof_debugfs_map *map;
	int err = 0, i;

	/* use "sof" as top level debugFS dir */
	sdev->debugfs_root = debugfs_create_dir("sof", NULL);
	if (IS_ERR_OR_NULL(sdev->debugfs_root)) {
		dev_err(sdev->dev, "error: failed to create debugfs directory\n");
		return 0;
	}

	/* create debugFS files for platform specific MMIO/DSP memories */
	for (i = 0; i < ops->debug_map_count; i++) {
		map = &ops->debug_map[i];

		err = snd_sof_debugfs_io_create_item(sdev, sdev->bar[map->bar] +
						  map->offset, map->size,
						  map->name);
		/* errors are only due to memory allocation, not debugfs */
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_dbg_init);

void snd_sof_free_debug(struct snd_sof_dev *sdev)
{
	debugfs_remove_recursive(sdev->debugfs_root);
}
EXPORT_SYMBOL(snd_sof_free_debug);
