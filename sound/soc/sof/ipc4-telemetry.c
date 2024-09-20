// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018-2023 Intel Corporation
//

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <sound/sof/debug.h>
#include <sound/sof/ipc4/header.h>
#include "sof-priv.h"
#include "ops.h"
#include "ipc4-telemetry.h"
#include "ipc4-priv.h"

static void __iomem *sof_ipc4_query_exception_address(struct snd_sof_dev *sdev)
{
	u32 type = SOF_IPC4_DEBUG_SLOT_TELEMETRY;
	size_t telemetry_slot_offset;
	u32 offset;

	telemetry_slot_offset = sof_ipc4_find_debug_slot_offset_by_type(sdev, type);
	if (!telemetry_slot_offset)
		return NULL;

	/* skip the first separator magic number */
	offset = telemetry_slot_offset + sizeof(u32);

	return sdev->bar[sdev->mailbox_bar] + offset;
}

static ssize_t sof_telemetry_entry_read(struct file *file, char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	void __iomem *io_addr;
	loff_t pos = *ppos;
	size_t size_ret;
	u8 *buf;

	if (pos < 0)
		return -EINVAL;
	/* skip the first separator magic number */
	if (pos >= SOF_IPC4_DEBUG_SLOT_SIZE - 4 || !count)
		return 0;
	if (count > SOF_IPC4_DEBUG_SLOT_SIZE - 4 - pos)
		count = SOF_IPC4_DEBUG_SLOT_SIZE - 4 - pos;

	io_addr = sof_ipc4_query_exception_address(sdev);
	if (!io_addr)
		return -EFAULT;

	buf = kzalloc(SOF_IPC4_DEBUG_SLOT_SIZE - 4, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy_fromio(buf, io_addr, SOF_IPC4_DEBUG_SLOT_SIZE - 4);
	size_ret = copy_to_user(buffer, buf + pos, count);
	if (size_ret) {
		kfree(buf);
		return -EFAULT;
	}

	*ppos = pos + count;
	kfree(buf);

	return count;
}

static const struct file_operations sof_telemetry_fops = {
	.open = simple_open,
	.read = sof_telemetry_entry_read,
};

void sof_ipc4_create_exception_debugfs_node(struct snd_sof_dev *sdev)
{
	struct snd_sof_dfsentry *dfse;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return;

	dfse->type = SOF_DFSENTRY_TYPE_IOMEM;
	dfse->size = SOF_IPC4_DEBUG_SLOT_SIZE - 4;
	dfse->access_type = SOF_DEBUGFS_ACCESS_ALWAYS;
	dfse->sdev = sdev;

	list_add(&dfse->list, &sdev->dfsentry_list);

	debugfs_create_file("exception", 0444, sdev->debugfs_root, dfse, &sof_telemetry_fops);
}
