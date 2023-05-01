// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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
#include <sound/sof/ext_manifest.h>
#include <sound/sof/debug.h>
#include "sof-priv.h"
#include "ops.h"

static ssize_t sof_dfsentry_write(struct file *file, const char __user *buffer,
				  size_t count, loff_t *ppos)
{
	size_t size;
	char *string;
	int ret;

	string = kzalloc(count+1, GFP_KERNEL);
	if (!string)
		return -ENOMEM;

	size = simple_write_to_buffer(string, count, ppos, buffer, count);
	ret = size;

	kfree(string);
	return ret;
}

static ssize_t sof_dfsentry_read(struct file *file, char __user *buffer,
				 size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	loff_t pos = *ppos;
	size_t size_ret;
	int skip = 0;
	int size;
	u8 *buf;

	size = dfse->size;

	/* validate position & count */
	if (pos < 0)
		return -EINVAL;
	if (pos >= size || !count)
		return 0;
	/* find the minimum. min() is not used since it adds sparse warnings */
	if (count > size - pos)
		count = size - pos;

	/* align io read start to u32 multiple */
	pos = ALIGN_DOWN(pos, 4);

	/* intermediate buffer size must be u32 multiple */
	size = ALIGN(count, 4);

	/* if start position is unaligned, read extra u32 */
	if (unlikely(pos != *ppos)) {
		skip = *ppos - pos;
		if (pos + size + 4 < dfse->size)
			size += 4;
	}

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (dfse->type == SOF_DFSENTRY_TYPE_IOMEM) {
#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_ENABLE_DEBUGFS_CACHE)
		/*
		 * If the DSP is active: copy from IO.
		 * If the DSP is suspended:
		 *	- Copy from IO if the memory is always accessible.
		 *	- Otherwise, copy from cached buffer.
		 */
		if (pm_runtime_active(sdev->dev) ||
		    dfse->access_type == SOF_DEBUGFS_ACCESS_ALWAYS) {
			memcpy_fromio(buf, dfse->io_mem + pos, size);
		} else {
			dev_info(sdev->dev,
				 "Copying cached debugfs data\n");
			memcpy(buf, dfse->cache_buf + pos, size);
		}
#else
		/* if the DSP is in D3 */
		if (!pm_runtime_active(sdev->dev) &&
		    dfse->access_type == SOF_DEBUGFS_ACCESS_D0_ONLY) {
			dev_err(sdev->dev,
				"error: debugfs entry cannot be read in DSP D3\n");
			kfree(buf);
			return -EINVAL;
		}

		memcpy_fromio(buf, dfse->io_mem + pos, size);
#endif
	} else {
		memcpy(buf, ((u8 *)(dfse->buf) + pos), size);
	}

	/* copy to userspace */
	size_ret = copy_to_user(buffer, buf + skip, count);

	kfree(buf);

	/* update count & position if copy succeeded */
	if (size_ret)
		return -EFAULT;

	*ppos = pos + count;

	return count;
}

static const struct file_operations sof_dfs_fops = {
	.open = simple_open,
	.read = sof_dfsentry_read,
	.llseek = default_llseek,
	.write = sof_dfsentry_write,
};

/* create FS entry for debug files that can expose DSP memories, registers */
static int snd_sof_debugfs_io_item(struct snd_sof_dev *sdev,
				   void __iomem *base, size_t size,
				   const char *name,
				   enum sof_debugfs_access_type access_type)
{
	struct snd_sof_dfsentry *dfse;

	if (!sdev)
		return -EINVAL;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->type = SOF_DFSENTRY_TYPE_IOMEM;
	dfse->io_mem = base;
	dfse->size = size;
	dfse->sdev = sdev;
	dfse->access_type = access_type;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_ENABLE_DEBUGFS_CACHE)
	/*
	 * allocate cache buffer that will be used to save the mem window
	 * contents prior to suspend
	 */
	if (access_type == SOF_DEBUGFS_ACCESS_D0_ONLY) {
		dfse->cache_buf = devm_kzalloc(sdev->dev, size, GFP_KERNEL);
		if (!dfse->cache_buf)
			return -ENOMEM;
	}
#endif

	debugfs_create_file(name, 0444, sdev->debugfs_root, dfse,
			    &sof_dfs_fops);

	/* add to dfsentry list */
	list_add(&dfse->list, &sdev->dfsentry_list);

	return 0;
}

int snd_sof_debugfs_add_region_item_iomem(struct snd_sof_dev *sdev,
					  enum snd_sof_fw_blk_type blk_type, u32 offset,
					  size_t size, const char *name,
					  enum sof_debugfs_access_type access_type)
{
	int bar = snd_sof_dsp_get_bar_index(sdev, blk_type);

	if (bar < 0)
		return bar;

	return snd_sof_debugfs_io_item(sdev, sdev->bar[bar] + offset, size, name,
				       access_type);
}
EXPORT_SYMBOL_GPL(snd_sof_debugfs_add_region_item_iomem);

/* create FS entry for debug files to expose kernel memory */
int snd_sof_debugfs_buf_item(struct snd_sof_dev *sdev,
			     void *base, size_t size,
			     const char *name, mode_t mode)
{
	struct snd_sof_dfsentry *dfse;

	if (!sdev)
		return -EINVAL;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->type = SOF_DFSENTRY_TYPE_BUF;
	dfse->buf = base;
	dfse->size = size;
	dfse->sdev = sdev;

	debugfs_create_file(name, mode, sdev->debugfs_root, dfse,
			    &sof_dfs_fops);
	/* add to dfsentry list */
	list_add(&dfse->list, &sdev->dfsentry_list);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_sof_debugfs_buf_item);

static int memory_info_update(struct snd_sof_dev *sdev, char *buf, size_t buff_size)
{
	struct sof_ipc_cmd_hdr msg = {
		.size = sizeof(struct sof_ipc_cmd_hdr),
		.cmd = SOF_IPC_GLB_DEBUG | SOF_IPC_DEBUG_MEM_USAGE,
	};
	struct sof_ipc_dbg_mem_usage *reply;
	int len;
	int ret;
	int i;

	reply = kmalloc(SOF_IPC_MSG_MAX_SIZE, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	ret = pm_runtime_resume_and_get(sdev->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err(sdev->dev, "error: enabling device failed: %d\n", ret);
		goto error;
	}

	ret = sof_ipc_tx_message(sdev->ipc, &msg, msg.size, reply, SOF_IPC_MSG_MAX_SIZE);
	pm_runtime_mark_last_busy(sdev->dev);
	pm_runtime_put_autosuspend(sdev->dev);
	if (ret < 0 || reply->rhdr.error < 0) {
		ret = min(ret, reply->rhdr.error);
		dev_err(sdev->dev, "error: reading memory info failed, %d\n", ret);
		goto error;
	}

	if (struct_size(reply, elems, reply->num_elems) != reply->rhdr.hdr.size) {
		dev_err(sdev->dev, "error: invalid memory info ipc struct size, %d\n",
			reply->rhdr.hdr.size);
		ret = -EINVAL;
		goto error;
	}

	for (i = 0, len = 0; i < reply->num_elems; i++) {
		ret = scnprintf(buf + len, buff_size - len, "zone %d.%d used %#8x free %#8x\n",
				reply->elems[i].zone, reply->elems[i].id,
				reply->elems[i].used, reply->elems[i].free);
		if (ret < 0)
			goto error;
		len += ret;
	}

	ret = len;
error:
	kfree(reply);
	return ret;
}

static ssize_t memory_info_read(struct file *file, char __user *to, size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	int data_length;

	/* read memory info from FW only once for each file read */
	if (!*ppos) {
		dfse->buf_data_size = 0;
		data_length = memory_info_update(sdev, dfse->buf, dfse->size);
		if (data_length < 0)
			return data_length;
		dfse->buf_data_size = data_length;
	}

	return simple_read_from_buffer(to, count, ppos, dfse->buf, dfse->buf_data_size);
}

static int memory_info_open(struct inode *inode, struct file *file)
{
	struct snd_sof_dfsentry *dfse = inode->i_private;
	struct snd_sof_dev *sdev = dfse->sdev;

	file->private_data = dfse;

	/* allocate buffer memory only in first open run, to save memory when unused */
	if (!dfse->buf) {
		dfse->buf = devm_kmalloc(sdev->dev, PAGE_SIZE, GFP_KERNEL);
		if (!dfse->buf)
			return -ENOMEM;
		dfse->size = PAGE_SIZE;
	}

	return 0;
}

static const struct file_operations memory_info_fops = {
	.open = memory_info_open,
	.read = memory_info_read,
	.llseek = default_llseek,
};

int snd_sof_dbg_memory_info_init(struct snd_sof_dev *sdev)
{
	struct snd_sof_dfsentry *dfse;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	/* don't allocate buffer before first usage, to save memory when unused */
	dfse->type = SOF_DFSENTRY_TYPE_BUF;
	dfse->sdev = sdev;

	debugfs_create_file("memory_info", 0444, sdev->debugfs_root, dfse, &memory_info_fops);

	/* add to dfsentry list */
	list_add(&dfse->list, &sdev->dfsentry_list);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_sof_dbg_memory_info_init);

int snd_sof_dbg_init(struct snd_sof_dev *sdev)
{
	struct snd_sof_dsp_ops *ops = sof_ops(sdev);
	const struct snd_sof_debugfs_map *map;
	int i;
	int err;

	/* use "sof" as top level debugFS dir */
	sdev->debugfs_root = debugfs_create_dir("sof", NULL);

	/* init dfsentry list */
	INIT_LIST_HEAD(&sdev->dfsentry_list);

	/* create debugFS files for platform specific MMIO/DSP memories */
	for (i = 0; i < ops->debug_map_count; i++) {
		map = &ops->debug_map[i];

		err = snd_sof_debugfs_io_item(sdev, sdev->bar[map->bar] +
					      map->offset, map->size,
					      map->name, map->access_type);
		/* errors are only due to memory allocation, not debugfs */
		if (err < 0)
			return err;
	}

	return snd_sof_debugfs_buf_item(sdev, &sdev->fw_state,
					sizeof(sdev->fw_state),
					"fw_state", 0444);
}
EXPORT_SYMBOL_GPL(snd_sof_dbg_init);

void snd_sof_free_debug(struct snd_sof_dev *sdev)
{
	debugfs_remove_recursive(sdev->debugfs_root);
}
EXPORT_SYMBOL_GPL(snd_sof_free_debug);

static const struct soc_fw_state_info {
	enum sof_fw_state state;
	const char *name;
} fw_state_dbg[] = {
	{SOF_FW_BOOT_NOT_STARTED, "SOF_FW_BOOT_NOT_STARTED"},
	{SOF_FW_BOOT_PREPARE, "SOF_FW_BOOT_PREPARE"},
	{SOF_FW_BOOT_IN_PROGRESS, "SOF_FW_BOOT_IN_PROGRESS"},
	{SOF_FW_BOOT_FAILED, "SOF_FW_BOOT_FAILED"},
	{SOF_FW_BOOT_READY_FAILED, "SOF_FW_BOOT_READY_FAILED"},
	{SOF_FW_BOOT_READY_OK, "SOF_FW_BOOT_READY_OK"},
	{SOF_FW_BOOT_COMPLETE, "SOF_FW_BOOT_COMPLETE"},
	{SOF_FW_CRASHED, "SOF_FW_CRASHED"},
};

static void snd_sof_dbg_print_fw_state(struct snd_sof_dev *sdev, const char *level)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fw_state_dbg); i++) {
		if (sdev->fw_state == fw_state_dbg[i].state) {
			dev_printk(level, sdev->dev, "fw_state: %s (%d)\n",
				   fw_state_dbg[i].name, i);
			return;
		}
	}

	dev_printk(level, sdev->dev, "fw_state: UNKNOWN (%d)\n", sdev->fw_state);
}

void snd_sof_dsp_dbg_dump(struct snd_sof_dev *sdev, const char *msg, u32 flags)
{
	char *level = (flags & SOF_DBG_DUMP_OPTIONAL) ? KERN_DEBUG : KERN_ERR;
	bool print_all = sof_debug_check_flag(SOF_DBG_PRINT_ALL_DUMPS);

	if (flags & SOF_DBG_DUMP_OPTIONAL && !print_all)
		return;

	if (sof_ops(sdev)->dbg_dump && !sdev->dbg_dump_printed) {
		dev_printk(level, sdev->dev,
			   "------------[ DSP dump start ]------------\n");
		if (msg)
			dev_printk(level, sdev->dev, "%s\n", msg);
		snd_sof_dbg_print_fw_state(sdev, level);
		sof_ops(sdev)->dbg_dump(sdev, flags);
		dev_printk(level, sdev->dev,
			   "------------[ DSP dump end ]------------\n");
		if (!print_all)
			sdev->dbg_dump_printed = true;
	} else if (msg) {
		dev_printk(level, sdev->dev, "%s\n", msg);
	}
}
EXPORT_SYMBOL(snd_sof_dsp_dbg_dump);

static void snd_sof_ipc_dump(struct snd_sof_dev *sdev)
{
	if (sof_ops(sdev)->ipc_dump  && !sdev->ipc_dump_printed) {
		dev_err(sdev->dev, "------------[ IPC dump start ]------------\n");
		sof_ops(sdev)->ipc_dump(sdev);
		dev_err(sdev->dev, "------------[ IPC dump end ]------------\n");
		if (!sof_debug_check_flag(SOF_DBG_PRINT_ALL_DUMPS))
			sdev->ipc_dump_printed = true;
	}
}

void snd_sof_handle_fw_exception(struct snd_sof_dev *sdev, const char *msg)
{
	if (IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_RETAIN_DSP_CONTEXT) ||
	    sof_debug_check_flag(SOF_DBG_RETAIN_CTX)) {
		/* should we prevent DSP entering D3 ? */
		if (!sdev->ipc_dump_printed)
			dev_info(sdev->dev,
				 "preventing DSP entering D3 state to preserve context\n");
		pm_runtime_get_noresume(sdev->dev);
	}

	/* dump vital information to the logs */
	snd_sof_ipc_dump(sdev);
	snd_sof_dsp_dbg_dump(sdev, msg, SOF_DBG_DUMP_REGS | SOF_DBG_DUMP_MBOX);
	sof_fw_trace_fw_crashed(sdev);
}
EXPORT_SYMBOL(snd_sof_handle_fw_exception);
