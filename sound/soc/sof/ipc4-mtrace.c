// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.

#include <linux/debugfs.h>
#include <linux/sched/signal.h>
#include <sound/sof/ipc4/header.h>
#include "sof-priv.h"
#include "ipc4-priv.h"

/*
 * debug info window is organized in 16 (equal sized) pages:
 *
 *	------------------------
 *	| Page0  - descriptors |
 *	------------------------
 *	| Page1  - slot0       |
 *	------------------------
 *	| Page2  - slot1       |
 *	------------------------
 *	|         ...          |
 *	------------------------
 *	| Page14  - slot13     |
 *	------------------------
 *	| Page15  - slot14     |
 *	------------------------
 *
 * The slot size == page size
 *
 * The first page contains descriptors for the remaining 15 cores
 * The slot descriptor is:
 * u32 res_id;
 * u32 type;
 * u32 vma;
 *
 * Log buffer slots have the following layout:
 * u32 host_read_ptr;
 * u32 dsp_write_ptr;
 * u8 buffer[];
 *
 * The two pointers are offsets within the buffer.
 */

#define FW_EPOCH_DELTA				11644473600LL

#define MAX_ALLOWED_LIBRARIES			16

#define SOF_IPC4_INVALID_SLOT_OFFSET		0xffffffff

 /* for debug and critical types */
#define SOF_MTRACE_SLOT_CORE_MASK		GENMASK(7, 0)
#define SOF_MTRACE_SLOT_TYPE_MASK		GENMASK(31, 8)

#define DEFAULT_AGING_TIMER_PERIOD_MS		0x100
#define DEFAULT_FIFO_FULL_TIMER_PERIOD_MS	0x1000

/* ipc4 log level and source definitions for logs_priorities_mask */
#define SOF_MTRACE_LOG_LEVEL_CRITICAL		BIT(0)
#define SOF_MTRACE_LOG_LEVEL_ERROR		BIT(1)
#define SOF_MTRACE_LOG_LEVEL_WARNING		BIT(2)
#define SOF_MTRACE_LOG_LEVEL_INFO		BIT(3)
#define SOF_MTRACE_LOG_LEVEL_VERBOSE		BIT(4)
#define SOF_MTRACE_LOG_SOURCE_INFRA		BIT(5) /* log source 0 */
#define SOF_MTRACE_LOG_SOURCE_HAL		BIT(6)
#define SOF_MTRACE_LOG_SOURCE_MODULE		BIT(7)
#define SOF_MTRACE_LOG_SOURCE_AUDIO		BIT(8)
#define SOF_MTRACE_LOG_SOURCE_SCHEDULER		BIT(9)
#define SOF_MTRACE_LOG_SOURCE_ULP_INFRA		BIT(10)
#define SOF_MTRACE_LOG_SOURCE_ULP_MODULE	BIT(11)
#define SOF_MTRACE_LOG_SOURCE_VISION		BIT(12) /* log source 7 */
#define DEFAULT_LOGS_PRIORITIES_MASK	(SOF_MTRACE_LOG_LEVEL_CRITICAL | \
					 SOF_MTRACE_LOG_LEVEL_ERROR |	 \
					 SOF_MTRACE_LOG_LEVEL_WARNING |	 \
					 SOF_MTRACE_LOG_LEVEL_INFO |	 \
					 SOF_MTRACE_LOG_SOURCE_INFRA |	 \
					 SOF_MTRACE_LOG_SOURCE_HAL |	 \
					 SOF_MTRACE_LOG_SOURCE_MODULE |	 \
					 SOF_MTRACE_LOG_SOURCE_AUDIO)

struct sof_log_state_info {
	u32 aging_timer_period;
	u32 fifo_full_timer_period;
	u32 enable;
	u32 logs_priorities_mask[MAX_ALLOWED_LIBRARIES];
} __packed;

enum sof_mtrace_state {
	SOF_MTRACE_DISABLED,
	SOF_MTRACE_INITIALIZING,
	SOF_MTRACE_ENABLED,
};

struct sof_mtrace_core_data {
	struct snd_sof_dev *sdev;

	int id;
	u32 slot_offset;
	void *log_buffer;
	struct mutex buffer_lock; /* for log_buffer alloc/free */
	u32 host_read_ptr;
	u32 dsp_write_ptr;
	/* pos update IPC arrived before the slot offset is known, queried */
	bool delayed_pos_update;
	wait_queue_head_t trace_sleep;
};

struct sof_mtrace_priv {
	struct snd_sof_dev *sdev;
	enum sof_mtrace_state mtrace_state;
	struct sof_log_state_info state_info;

	struct sof_mtrace_core_data cores[];
};

static int sof_ipc4_mtrace_dfs_open(struct inode *inode, struct file *file)
{
	struct sof_mtrace_core_data *core_data = inode->i_private;
	int ret;

	mutex_lock(&core_data->buffer_lock);

	if (core_data->log_buffer) {
		ret = -EBUSY;
		goto out;
	}

	ret = debugfs_file_get(file->f_path.dentry);
	if (unlikely(ret))
		goto out;

	core_data->log_buffer = kmalloc(SOF_IPC4_DEBUG_SLOT_SIZE, GFP_KERNEL);
	if (!core_data->log_buffer) {
		debugfs_file_put(file->f_path.dentry);
		ret = -ENOMEM;
		goto out;
	}

	ret = simple_open(inode, file);
	if (ret) {
		kfree(core_data->log_buffer);
		debugfs_file_put(file->f_path.dentry);
	}

out:
	mutex_unlock(&core_data->buffer_lock);

	return ret;
}

static bool sof_wait_mtrace_avail(struct sof_mtrace_core_data *core_data)
{
	wait_queue_entry_t wait;

	/* data immediately available */
	if (core_data->host_read_ptr != core_data->dsp_write_ptr)
		return true;

	/* wait for available trace data from FW */
	init_waitqueue_entry(&wait, current);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&core_data->trace_sleep, &wait);

	if (!signal_pending(current)) {
		/* set timeout to max value, no error code */
		schedule_timeout(MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&core_data->trace_sleep, &wait);

	if (core_data->host_read_ptr != core_data->dsp_write_ptr)
		return true;

	return false;
}

static ssize_t sof_ipc4_mtrace_dfs_read(struct file *file, char __user *buffer,
					size_t count, loff_t *ppos)
{
	struct sof_mtrace_core_data *core_data = file->private_data;
	u32 log_buffer_offset, log_buffer_size, read_ptr, write_ptr;
	struct snd_sof_dev *sdev = core_data->sdev;
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	void *log_buffer = core_data->log_buffer;
	loff_t lpos = *ppos;
	u32 avail;
	int ret;

	/* check pos and count */
	if (lpos < 0)
		return -EINVAL;
	if (!count || count < sizeof(avail))
		return 0;

	/* get available count based on current host offset */
	if (!sof_wait_mtrace_avail(core_data)) {
		/* No data available */
		avail = 0;
		if (copy_to_user(buffer, &avail, sizeof(avail)))
			return -EFAULT;

		return 0;
	}

	if (core_data->slot_offset == SOF_IPC4_INVALID_SLOT_OFFSET)
		return 0;

	/* The log data buffer starts after the two pointer in the slot */
	log_buffer_offset =  core_data->slot_offset + (sizeof(u32) * 2);
	/* The log data size excludes the pointers */
	log_buffer_size = SOF_IPC4_DEBUG_SLOT_SIZE - (sizeof(u32) * 2);

	read_ptr = core_data->host_read_ptr;
	write_ptr = core_data->dsp_write_ptr;

	if (read_ptr < write_ptr)
		avail = write_ptr - read_ptr;
	else
		avail = log_buffer_size - read_ptr + write_ptr;

	if (!avail)
		return 0;

	if (avail > log_buffer_size)
		avail = log_buffer_size;

	/* Need space for the initial u32 of the avail */
	if (avail > count - sizeof(avail))
		avail = count - sizeof(avail);

	if (sof_debug_check_flag(SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS))
		dev_dbg(sdev->dev,
			"core%d, host read: %#x, dsp write: %#x, avail: %#x\n",
			core_data->id, read_ptr, write_ptr, avail);

	if (read_ptr < write_ptr) {
		/* Read data between read pointer and write pointer */
		sof_mailbox_read(sdev, log_buffer_offset + read_ptr, log_buffer, avail);
	} else {
		/* read from read pointer to end of the slot */
		sof_mailbox_read(sdev, log_buffer_offset + read_ptr, log_buffer,
				 avail - write_ptr);
		/* read from slot start to write pointer */
		if (write_ptr)
			sof_mailbox_read(sdev, log_buffer_offset,
					 (u8 *)(log_buffer) + avail - write_ptr,
					 write_ptr);
	}

	/* first write the number of bytes we have gathered */
	ret = copy_to_user(buffer, &avail, sizeof(avail));
	if (ret)
		return -EFAULT;

	/* Followed by the data itself */
	ret = copy_to_user(buffer + sizeof(avail), log_buffer, avail);
	if (ret)
		return -EFAULT;

	/* Update the host_read_ptr in the slot for this core */
	read_ptr += avail;
	if (read_ptr >= log_buffer_size)
		read_ptr -= log_buffer_size;
	sof_mailbox_write(sdev, core_data->slot_offset, &read_ptr, sizeof(read_ptr));

	/* Only update the host_read_ptr if mtrace is enabled */
	if (priv->mtrace_state != SOF_MTRACE_DISABLED)
		core_data->host_read_ptr = read_ptr;

	/*
	 * Ask for a new buffer from user space for the next chunk, not
	 * streaming due to the heading number of bytes value.
	 */
	*ppos += count;

	return count;
}

static int sof_ipc4_mtrace_dfs_release(struct inode *inode, struct file *file)
{
	struct sof_mtrace_core_data *core_data = inode->i_private;

	debugfs_file_put(file->f_path.dentry);

	mutex_lock(&core_data->buffer_lock);
	kfree(core_data->log_buffer);
	core_data->log_buffer = NULL;
	mutex_unlock(&core_data->buffer_lock);

	return 0;
}

static const struct file_operations sof_dfs_mtrace_fops = {
	.open = sof_ipc4_mtrace_dfs_open,
	.read = sof_ipc4_mtrace_dfs_read,
	.llseek = default_llseek,
	.release = sof_ipc4_mtrace_dfs_release,

	.owner = THIS_MODULE,
};

static ssize_t sof_ipc4_priority_mask_dfs_read(struct file *file, char __user *to,
					       size_t count, loff_t *ppos)
{
	struct sof_mtrace_priv *priv = file->private_data;
	int i, ret, offset, remaining;
	char *buf;

	/*
	 * one entry (14 char + new line = 15):
	 * " 0: 000001ef"
	 *
	 * 16 * 15 + 1 = 241
	 */
	buf = kzalloc(241, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < MAX_ALLOWED_LIBRARIES; i++) {
		offset = strlen(buf);
		remaining = 241 - offset;
		snprintf(buf + offset, remaining, "%2d: 0x%08x\n", i,
			 priv->state_info.logs_priorities_mask[i]);
	}

	ret = simple_read_from_buffer(to, count, ppos, buf, strlen(buf));

	kfree(buf);
	return ret;
}

static ssize_t sof_ipc4_priority_mask_dfs_write(struct file *file,
						const char __user *from,
						size_t count, loff_t *ppos)
{
	struct sof_mtrace_priv *priv = file->private_data;
	unsigned int id;
	char *buf;
	u32 mask;
	int ret;

	/*
	 * To update Nth mask entry, write:
	 * "N,0x1234" or "N,1234" to the debugfs file
	 * The mask will be interpreted as hexadecimal number
	 */
	buf = memdup_user_nul(from, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	ret = sscanf(buf, "%u,0x%x", &id, &mask);
	if (ret != 2) {
		ret = sscanf(buf, "%u,%x", &id, &mask);
		if (ret != 2) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (id >= MAX_ALLOWED_LIBRARIES) {
		ret = -EINVAL;
		goto out;
	}

	priv->state_info.logs_priorities_mask[id] = mask;
	ret = count;

out:
	kfree(buf);
	return ret;
}

static const struct file_operations sof_dfs_priority_mask_fops = {
	.open = simple_open,
	.read = sof_ipc4_priority_mask_dfs_read,
	.write = sof_ipc4_priority_mask_dfs_write,
	.llseek = default_llseek,

	.owner = THIS_MODULE,
};

static int mtrace_debugfs_create(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct dentry *dfs_root;
	char dfs_name[100];
	int i;

	dfs_root = debugfs_create_dir("mtrace", sdev->debugfs_root);
	if (IS_ERR_OR_NULL(dfs_root))
		return 0;

	/* Create files for the logging parameters */
	debugfs_create_u32("aging_timer_period", 0644, dfs_root,
			   &priv->state_info.aging_timer_period);
	debugfs_create_u32("fifo_full_timer_period", 0644, dfs_root,
			   &priv->state_info.fifo_full_timer_period);
	debugfs_create_file("logs_priorities_mask", 0644, dfs_root, priv,
			    &sof_dfs_priority_mask_fops);

	/* Separate log files per core */
	for (i = 0; i < sdev->num_cores; i++) {
		snprintf(dfs_name, sizeof(dfs_name), "core%d", i);
		debugfs_create_file(dfs_name, 0444, dfs_root, &priv->cores[i],
				    &sof_dfs_mtrace_fops);
	}

	return 0;
}

static int ipc4_mtrace_enable(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_ipc4_msg msg;
	u64 system_time;
	ktime_t kt;
	int ret;

	if (priv->mtrace_state != SOF_MTRACE_DISABLED)
		return 0;

	msg.primary = SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MOD_ID(SOF_IPC4_MOD_INIT_BASEFW_MOD_ID);
	msg.primary |= SOF_IPC4_MOD_INSTANCE(SOF_IPC4_MOD_INIT_BASEFW_INSTANCE_ID);
	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_SYSTEM_TIME);

	/* The system time is in usec, UTC, epoch is 1601-01-01 00:00:00 */
	kt = ktime_add_us(ktime_get_real(), FW_EPOCH_DELTA * USEC_PER_SEC);
	system_time = ktime_to_us(kt);
	msg.data_size = sizeof(system_time);
	msg.data_ptr = &system_time;
	ret = iops->set_get_data(sdev, &msg, msg.data_size, true);
	if (ret)
		return ret;

	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_ENABLE_LOGS);

	priv->state_info.enable = 1;

	msg.data_size = sizeof(priv->state_info);
	msg.data_ptr = &priv->state_info;

	priv->mtrace_state = SOF_MTRACE_INITIALIZING;
	ret = iops->set_get_data(sdev, &msg, msg.data_size, true);
	if (ret) {
		priv->mtrace_state = SOF_MTRACE_DISABLED;
		return ret;
	}

	priv->mtrace_state = SOF_MTRACE_ENABLED;

	return 0;
}

static void ipc4_mtrace_disable(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	const struct sof_ipc_ops *iops = sdev->ipc->ops;
	struct sof_ipc4_msg msg;
	int i;

	if (priv->mtrace_state == SOF_MTRACE_DISABLED)
		return;

	msg.primary = SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);
	msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg.primary |= SOF_IPC4_MOD_ID(SOF_IPC4_MOD_INIT_BASEFW_MOD_ID);
	msg.primary |= SOF_IPC4_MOD_INSTANCE(SOF_IPC4_MOD_INIT_BASEFW_INSTANCE_ID);
	msg.extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_FW_PARAM_ENABLE_LOGS);

	priv->state_info.enable = 0;

	msg.data_size = sizeof(priv->state_info);
	msg.data_ptr = &priv->state_info;
	iops->set_get_data(sdev, &msg, msg.data_size, true);

	priv->mtrace_state = SOF_MTRACE_DISABLED;

	for (i = 0; i < sdev->num_cores; i++) {
		struct sof_mtrace_core_data *core_data = &priv->cores[i];

		core_data->host_read_ptr = 0;
		core_data->dsp_write_ptr = 0;
		wake_up(&core_data->trace_sleep);
	}
}

/*
 * Each DSP core logs to a dedicated slot.
 * Parse the slot descriptors at debug_box offset to find the debug log slots
 * and map them to cores.
 * There are 15 slots and therefore 15 descriptors to check (MAX_MTRACE_SLOTS)
 */
static void sof_mtrace_find_core_slots(struct snd_sof_dev *sdev)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct sof_mtrace_core_data *core_data;
	u32 slot_desc_type_offset, type, core;
	int i;

	for (i = 0; i < SOF_IPC4_MAX_DEBUG_SLOTS; i++) {
		/* The type is the second u32 in the slot descriptor */
		slot_desc_type_offset = sdev->debug_box.offset;
		slot_desc_type_offset += SOF_IPC4_DEBUG_DESCRIPTOR_SIZE * i + sizeof(u32);
		sof_mailbox_read(sdev, slot_desc_type_offset, &type, sizeof(type));

		if ((type & SOF_MTRACE_SLOT_TYPE_MASK) == SOF_IPC4_DEBUG_SLOT_DEBUG_LOG) {
			core = type & SOF_MTRACE_SLOT_CORE_MASK;

			if (core >= sdev->num_cores) {
				dev_dbg(sdev->dev, "core%u is invalid for slot%d\n",
					core, i);
				continue;
			}

			core_data = &priv->cores[core];
			/*
			 * The area reserved for descriptors have the same size
			 * as a slot.
			 * In other words: slot0 starts at
			 * debug_box + SOF_MTRACE_SLOT_SIZE offset
			 */
			core_data->slot_offset = sdev->debug_box.offset;
			core_data->slot_offset += SOF_IPC4_DEBUG_SLOT_SIZE * (i + 1);
			dev_dbg(sdev->dev, "slot%d is used for core%u\n", i, core);
			if (core_data->delayed_pos_update) {
				sof_ipc4_mtrace_update_pos(sdev, core);
				core_data->delayed_pos_update = false;
			}
		} else if (type) {
			dev_dbg(sdev->dev, "slot%d is not a log slot (%#x)\n", i, type);
		}
	}
}

static int ipc4_mtrace_init(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_mtrace_priv *priv;
	int i, ret;

	if (sdev->fw_trace_data) {
		dev_err(sdev->dev, "fw_trace_data has been already allocated\n");
		return -EBUSY;
	}

	if (!ipc4_data->mtrace_log_bytes ||
	    ipc4_data->mtrace_type != SOF_IPC4_MTRACE_INTEL_CAVS_2) {
		sdev->fw_trace_is_supported = false;
		return 0;
	}

	priv = devm_kzalloc(sdev->dev, struct_size(priv, cores, sdev->num_cores),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sdev->fw_trace_data = priv;

	/* Set initial values for mtrace parameters */
	priv->state_info.aging_timer_period = DEFAULT_AGING_TIMER_PERIOD_MS;
	priv->state_info.fifo_full_timer_period = DEFAULT_FIFO_FULL_TIMER_PERIOD_MS;
	/* Only enable basefw logs initially (index 0 is always basefw) */
	priv->state_info.logs_priorities_mask[0] = DEFAULT_LOGS_PRIORITIES_MASK;

	for (i = 0; i < sdev->num_cores; i++) {
		struct sof_mtrace_core_data *core_data = &priv->cores[i];

		init_waitqueue_head(&core_data->trace_sleep);
		mutex_init(&core_data->buffer_lock);
		core_data->sdev = sdev;
		core_data->id = i;
	}

	ret = ipc4_mtrace_enable(sdev);
	if (ret) {
		/*
		 * Mark firmware tracing as not supported and return 0 to not
		 * block the whole audio stack
		 */
		sdev->fw_trace_is_supported = false;
		dev_dbg(sdev->dev, "initialization failed, fw tracing is disabled\n");
		return 0;
	}

	sof_mtrace_find_core_slots(sdev);

	ret = mtrace_debugfs_create(sdev);
	if (ret)
		ipc4_mtrace_disable(sdev);

	return ret;
}

static void ipc4_mtrace_free(struct snd_sof_dev *sdev)
{
	ipc4_mtrace_disable(sdev);
}

static int sof_ipc4_mtrace_update_pos_all_cores(struct snd_sof_dev *sdev)
{
	int i;

	for (i = 0; i < sdev->num_cores; i++)
		sof_ipc4_mtrace_update_pos(sdev, i);

	return 0;
}

int sof_ipc4_mtrace_update_pos(struct snd_sof_dev *sdev, int core)
{
	struct sof_mtrace_priv *priv = sdev->fw_trace_data;
	struct sof_mtrace_core_data *core_data;

	if (!sdev->fw_trace_is_supported ||
	    priv->mtrace_state == SOF_MTRACE_DISABLED)
		return 0;

	if (core >= sdev->num_cores)
		return -EINVAL;

	core_data = &priv->cores[core];

	if (core_data->slot_offset == SOF_IPC4_INVALID_SLOT_OFFSET) {
		core_data->delayed_pos_update = true;
		return 0;
	}

	/* Read out the dsp_write_ptr from the slot for this core */
	sof_mailbox_read(sdev, core_data->slot_offset + sizeof(u32),
			 &core_data->dsp_write_ptr, 4);
	core_data->dsp_write_ptr -= core_data->dsp_write_ptr % 4;

	if (sof_debug_check_flag(SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS))
		dev_dbg(sdev->dev, "core%d, host read: %#x, dsp write: %#x",
			core, core_data->host_read_ptr, core_data->dsp_write_ptr);

	wake_up(&core_data->trace_sleep);

	return 0;
}

static void ipc4_mtrace_fw_crashed(struct snd_sof_dev *sdev)
{
	/*
	 * The DSP might not be able to send SOF_IPC4_NOTIFY_LOG_BUFFER_STATUS
	 * messages anymore, so check the log buffer status on all
	 * cores and process any pending messages.
	 */
	sof_ipc4_mtrace_update_pos_all_cores(sdev);
}

static int ipc4_mtrace_resume(struct snd_sof_dev *sdev)
{
	return ipc4_mtrace_enable(sdev);
}

static void ipc4_mtrace_suspend(struct snd_sof_dev *sdev, pm_message_t pm_state)
{
	ipc4_mtrace_disable(sdev);
}

const struct sof_ipc_fw_tracing_ops ipc4_mtrace_ops = {
	.init = ipc4_mtrace_init,
	.free = ipc4_mtrace_free,
	.fw_crashed = ipc4_mtrace_fw_crashed,
	.suspend = ipc4_mtrace_suspend,
	.resume = ipc4_mtrace_resume,
};
