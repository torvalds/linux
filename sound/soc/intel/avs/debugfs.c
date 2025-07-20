// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>
#include <linux/string_helpers.h>
#include <sound/soc.h>
#include "avs.h"
#include "messages.h"

static unsigned int __kfifo_fromio(struct kfifo *fifo, const void __iomem *src, unsigned int len)
{
	struct __kfifo *__fifo = &fifo->kfifo;
	unsigned int l, off;

	len = min(len, kfifo_avail(fifo));
	off = __fifo->in & __fifo->mask;
	l = min(len, kfifo_size(fifo) - off);

	memcpy_fromio(__fifo->data + off, src, l);
	memcpy_fromio(__fifo->data, src + l, len - l);
	/* Make sure data copied from SRAM is visible to all CPUs. */
	smp_mb();
	__fifo->in += len;

	return len;
}

bool avs_logging_fw(struct avs_dev *adev)
{
	return kfifo_initialized(&adev->trace_fifo);
}

void avs_dump_fw_log(struct avs_dev *adev, const void __iomem *src, unsigned int len)
{
	__kfifo_fromio(&adev->trace_fifo, src, len);
}

void avs_dump_fw_log_wakeup(struct avs_dev *adev, const void __iomem *src, unsigned int len)
{
	avs_dump_fw_log(adev, src, len);
	wake_up(&adev->trace_waitq);
}

static ssize_t fw_regs_read(struct file *file, char __user *to, size_t count, loff_t *ppos)
{
	struct avs_dev *adev = file->private_data;
	char *buf;
	int ret;

	buf = kzalloc(AVS_FW_REGS_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy_fromio(buf, avs_sram_addr(adev, AVS_FW_REGS_WINDOW), AVS_FW_REGS_SIZE);

	ret = simple_read_from_buffer(to, count, ppos, buf, AVS_FW_REGS_SIZE);
	kfree(buf);
	return ret;
}

static const struct file_operations fw_regs_fops = {
	.open = simple_open,
	.read = fw_regs_read,
};

static ssize_t debug_window_read(struct file *file, char __user *to, size_t count, loff_t *ppos)
{
	struct avs_dev *adev = file->private_data;
	size_t size;
	char *buf;
	int ret;

	size = adev->hw_cfg.dsp_cores * AVS_WINDOW_CHUNK_SIZE;
	buf = kzalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy_fromio(buf, avs_sram_addr(adev, AVS_DEBUG_WINDOW), size);

	ret = simple_read_from_buffer(to, count, ppos, buf, size);
	kfree(buf);
	return ret;
}

static const struct file_operations debug_window_fops = {
	.open = simple_open,
	.read = debug_window_read,
};

static ssize_t probe_points_read(struct file *file, char __user *to, size_t count, loff_t *ppos)
{
	struct avs_dev *adev = file->private_data;
	struct avs_probe_point_desc *desc;
	size_t num_desc, len = 0;
	char *buf;
	int i, ret;

	/* Prevent chaining, send and dump IPC value just once. */
	if (*ppos)
		return 0;

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = avs_ipc_probe_get_points(adev, &desc, &num_desc);
	if (ret) {
		ret = AVS_IPC_RET(ret);
		goto exit;
	}

	for (i = 0; i < num_desc; i++) {
		ret = snprintf(buf + len, PAGE_SIZE - len,
			       "Id: %#010x  Purpose: %d  Node id: %#x\n",
			       desc[i].id.value, desc[i].purpose, desc[i].node_id.val);
		if (ret < 0)
			goto free_desc;
		len += ret;
	}

	ret = simple_read_from_buffer(to, count, ppos, buf, len);
free_desc:
	kfree(desc);
exit:
	kfree(buf);
	return ret;
}

static ssize_t probe_points_write(struct file *file, const char __user *from, size_t count,
				  loff_t *ppos)
{
	struct avs_dev *adev = file->private_data;
	struct avs_probe_point_desc *desc;
	u32 *array, num_elems;
	size_t bytes;
	int ret;

	ret = parse_int_array_user(from, count, (int **)&array);
	if (ret)
		return ret;

	num_elems = *array;
	bytes = sizeof(*array) * num_elems;
	if (bytes % sizeof(*desc)) {
		ret = -EINVAL;
		goto exit;
	}

	desc = (struct avs_probe_point_desc *)&array[1];
	ret = avs_ipc_probe_connect_points(adev, desc, bytes / sizeof(*desc));
	if (ret)
		ret = AVS_IPC_RET(ret);
	else
		ret = count;
exit:
	kfree(array);
	return ret;
}

static const struct file_operations probe_points_fops = {
	.open = simple_open,
	.read = probe_points_read,
	.write = probe_points_write,
};

static ssize_t probe_points_disconnect_write(struct file *file, const char __user *from,
					     size_t count, loff_t *ppos)
{
	struct avs_dev *adev = file->private_data;
	union avs_probe_point_id *id;
	u32 *array, num_elems;
	size_t bytes;
	int ret;

	ret = parse_int_array_user(from, count, (int **)&array);
	if (ret)
		return ret;

	num_elems = *array;
	bytes = sizeof(*array) * num_elems;
	if (bytes % sizeof(*id)) {
		ret = -EINVAL;
		goto exit;
	}

	id = (union avs_probe_point_id *)&array[1];
	ret = avs_ipc_probe_disconnect_points(adev, id, bytes / sizeof(*id));
	if (ret)
		ret = AVS_IPC_RET(ret);
	else
		ret = count;
exit:
	kfree(array);
	return ret;
}

static const struct file_operations probe_points_disconnect_fops = {
	.open = simple_open,
	.write = probe_points_disconnect_write,
	.llseek = default_llseek,
};

static ssize_t strace_read(struct file *file, char __user *to, size_t count, loff_t *ppos)
{
	struct avs_dev *adev = file->private_data;
	struct kfifo *fifo = &adev->trace_fifo;
	unsigned int copied;

	if (kfifo_is_empty(fifo)) {
		DEFINE_WAIT(wait);

		prepare_to_wait(&adev->trace_waitq, &wait, TASK_INTERRUPTIBLE);
		if (!signal_pending(current))
			schedule();
		finish_wait(&adev->trace_waitq, &wait);
	}

	if (kfifo_to_user(fifo, to, count, &copied))
		return -EFAULT;
	*ppos += copied;
	return copied;
}

static int strace_open(struct inode *inode, struct file *file)
{
	struct avs_dev *adev = inode->i_private;
	int ret;

	if (!try_module_get(adev->dev->driver->owner))
		return -ENODEV;

	if (kfifo_initialized(&adev->trace_fifo))
		return -EBUSY;

	ret = kfifo_alloc(&adev->trace_fifo, PAGE_SIZE, GFP_KERNEL);
	if (ret < 0)
		return ret;

	file->private_data = adev;
	return 0;
}

static int strace_release(struct inode *inode, struct file *file)
{
	union avs_notify_msg msg = AVS_NOTIFICATION(LOG_BUFFER_STATUS);
	struct avs_dev *adev = file->private_data;
	unsigned long resource_mask;
	unsigned long flags, i;
	u32 num_cores;

	resource_mask = adev->logged_resources;
	num_cores = adev->hw_cfg.dsp_cores;

	spin_lock_irqsave(&adev->trace_lock, flags);

	/* Gather any remaining logs. */
	for_each_set_bit(i, &resource_mask, num_cores) {
		msg.log.core = i;
		avs_dsp_op(adev, log_buffer_status, &msg);
	}

	kfifo_free(&adev->trace_fifo);

	spin_unlock_irqrestore(&adev->trace_lock, flags);

	module_put(adev->dev->driver->owner);
	return 0;
}

static const struct file_operations strace_fops = {
	.llseek = default_llseek,
	.read = strace_read,
	.open = strace_open,
	.release = strace_release,
};

#define DISABLE_TIMERS	UINT_MAX

static int enable_logs(struct avs_dev *adev, u32 resource_mask, u32 *priorities)
{
	int ret;

	/* Logging demands D0i0 state from DSP. */
	if (!adev->logged_resources) {
		pm_runtime_get_sync(adev->dev);

		ret = avs_dsp_disable_d0ix(adev);
		if (ret)
			goto err_d0ix;
	}

	ret = avs_ipc_set_system_time(adev);
	if (ret && ret != AVS_IPC_NOT_SUPPORTED) {
		ret = AVS_IPC_RET(ret);
		goto err_ipc;
	}

	ret = avs_dsp_op(adev, enable_logs, AVS_LOG_ENABLE, adev->aging_timer_period,
			 adev->fifo_full_timer_period, resource_mask, priorities);
	if (ret)
		goto err_ipc;

	adev->logged_resources |= resource_mask;
	return 0;

err_ipc:
	if (!adev->logged_resources) {
		avs_dsp_enable_d0ix(adev);
err_d0ix:
		pm_runtime_mark_last_busy(adev->dev);
		pm_runtime_put_autosuspend(adev->dev);
	}

	return ret;
}

static int disable_logs(struct avs_dev *adev, u32 resource_mask)
{
	int ret;

	/* Check if there's anything to do. */
	if (!adev->logged_resources)
		return 0;

	ret = avs_dsp_op(adev, enable_logs, AVS_LOG_DISABLE, DISABLE_TIMERS, DISABLE_TIMERS,
			 resource_mask, NULL);

	/*
	 * If IPC fails causing recovery, logged_resources is already zero
	 * so unsetting bits is still safe.
	 */
	adev->logged_resources &= ~resource_mask;

	/* If that's the last resource, allow for D3. */
	if (!adev->logged_resources) {
		avs_dsp_enable_d0ix(adev);
		pm_runtime_mark_last_busy(adev->dev);
		pm_runtime_put_autosuspend(adev->dev);
	}

	return ret;
}

static ssize_t trace_control_read(struct file *file, char __user *to, size_t count, loff_t *ppos)
{
	struct avs_dev *adev = file->private_data;
	char buf[64];
	int len;

	len = snprintf(buf, sizeof(buf), "0x%08x\n", adev->logged_resources);

	return simple_read_from_buffer(to, count, ppos, buf, len);
}

static ssize_t trace_control_write(struct file *file, const char __user *from, size_t count,
				   loff_t *ppos)
{
	struct avs_dev *adev = file->private_data;
	u32 *array, num_elems;
	u32 resource_mask;
	int ret;

	ret = parse_int_array_user(from, count, (int **)&array);
	if (ret)
		return ret;

	num_elems = *array;
	if (!num_elems) {
		ret = -EINVAL;
		goto free_array;
	}

	/*
	 * Disable if just resource mask is provided - no log priority flags.
	 *
	 * Enable input format:   mask, prio1, .., prioN
	 * Where 'N' equals number of bits set in the 'mask'.
	 */
	resource_mask = array[1];
	if (num_elems == 1) {
		ret = disable_logs(adev, resource_mask);
	} else {
		if (num_elems != (hweight_long(resource_mask) + 1)) {
			ret = -EINVAL;
			goto free_array;
		}

		ret = enable_logs(adev, resource_mask, &array[2]);
	}

	if (!ret)
		ret = count;
free_array:
	kfree(array);
	return ret;
}

static const struct file_operations trace_control_fops = {
	.llseek = default_llseek,
	.read = trace_control_read,
	.write = trace_control_write,
	.open = simple_open,
};

void avs_debugfs_init(struct avs_dev *adev)
{
	init_waitqueue_head(&adev->trace_waitq);
	spin_lock_init(&adev->trace_lock);

	adev->debugfs_root = debugfs_create_dir("avs", snd_soc_debugfs_root);

	/* Initialize timer periods with recommended defaults. */
	adev->aging_timer_period = 10;
	adev->fifo_full_timer_period = 10;

	debugfs_create_file("strace", 0444, adev->debugfs_root, adev, &strace_fops);
	debugfs_create_file("trace_control", 0644, adev->debugfs_root, adev, &trace_control_fops);
	debugfs_create_file("fw_regs", 0444, adev->debugfs_root, adev, &fw_regs_fops);
	debugfs_create_file("debug_window", 0444, adev->debugfs_root, adev, &debug_window_fops);

	debugfs_create_u32("trace_aging_period", 0644, adev->debugfs_root,
			   &adev->aging_timer_period);
	debugfs_create_u32("trace_fifo_full_period", 0644, adev->debugfs_root,
			   &adev->fifo_full_timer_period);

	debugfs_create_file("probe_points", 0644, adev->debugfs_root, adev, &probe_points_fops);
	debugfs_create_file("probe_points_disconnect", 0200, adev->debugfs_root, adev,
			    &probe_points_disconnect_fops);
}

void avs_debugfs_exit(struct avs_dev *adev)
{
	debugfs_remove_recursive(adev->debugfs_root);
}
