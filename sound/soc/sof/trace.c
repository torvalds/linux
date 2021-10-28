// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/sched/signal.h>
#include "sof-priv.h"
#include "ops.h"

#define TRACE_FILTER_ELEMENTS_PER_ENTRY 4
#define TRACE_FILTER_MAX_CONFIG_STRING_LENGTH 1024

static int trace_filter_append_elem(struct snd_sof_dev *sdev, uint32_t key, uint32_t value,
				    struct sof_ipc_trace_filter_elem *elem_list,
				    int capacity, int *counter)
{
	if (*counter >= capacity)
		return -ENOMEM;

	elem_list[*counter].key = key;
	elem_list[*counter].value = value;
	++*counter;

	return 0;
}

static int trace_filter_parse_entry(struct snd_sof_dev *sdev, const char *line,
				    struct sof_ipc_trace_filter_elem *elem,
				    int capacity, int *counter)
{
	int len = strlen(line);
	int cnt = *counter;
	uint32_t uuid_id;
	int log_level;
	int pipe_id;
	int comp_id;
	int read;
	int ret;

	/* ignore empty content */
	ret = sscanf(line, " %n", &read);
	if (!ret && read == len)
		return len;

	ret = sscanf(line, " %d %x %d %d %n", &log_level, &uuid_id, &pipe_id, &comp_id, &read);
	if (ret != TRACE_FILTER_ELEMENTS_PER_ENTRY || read != len) {
		dev_err(sdev->dev, "error: invalid trace filter entry '%s'\n", line);
		return -EINVAL;
	}

	if (uuid_id > 0) {
		ret = trace_filter_append_elem(sdev, SOF_IPC_TRACE_FILTER_ELEM_BY_UUID,
					       uuid_id, elem, capacity, &cnt);
		if (ret)
			return ret;
	}
	if (pipe_id >= 0) {
		ret = trace_filter_append_elem(sdev, SOF_IPC_TRACE_FILTER_ELEM_BY_PIPE,
					       pipe_id, elem, capacity, &cnt);
		if (ret)
			return ret;
	}
	if (comp_id >= 0) {
		ret = trace_filter_append_elem(sdev, SOF_IPC_TRACE_FILTER_ELEM_BY_COMP,
					       comp_id, elem, capacity, &cnt);
		if (ret)
			return ret;
	}

	ret = trace_filter_append_elem(sdev, SOF_IPC_TRACE_FILTER_ELEM_SET_LEVEL |
				       SOF_IPC_TRACE_FILTER_ELEM_FIN,
				       log_level, elem, capacity, &cnt);
	if (ret)
		return ret;

	/* update counter only when parsing whole entry passed */
	*counter = cnt;

	return len;
}

static int trace_filter_parse(struct snd_sof_dev *sdev, char *string,
			      int *out_elem_cnt,
			      struct sof_ipc_trace_filter_elem **out)
{
	static const char entry_delimiter[] = ";";
	char *entry = string;
	int capacity = 0;
	int entry_len;
	int cnt = 0;

	/*
	 * Each entry contains at least 1, up to TRACE_FILTER_ELEMENTS_PER_ENTRY
	 * IPC elements, depending on content. Calculate IPC elements capacity
	 * for the input string where each element is set.
	 */
	while (entry) {
		capacity += TRACE_FILTER_ELEMENTS_PER_ENTRY;
		entry = strchr(entry + 1, entry_delimiter[0]);
	}
	*out = kmalloc(capacity * sizeof(**out), GFP_KERNEL);
	if (!*out)
		return -ENOMEM;

	/* split input string by ';', and parse each entry separately in trace_filter_parse_entry */
	while ((entry = strsep(&string, entry_delimiter))) {
		entry_len = trace_filter_parse_entry(sdev, entry, *out, capacity, &cnt);
		if (entry_len < 0) {
			dev_err(sdev->dev, "error: %s failed for '%s', %d\n", __func__, entry,
				entry_len);
			return -EINVAL;
		}
	}

	*out_elem_cnt = cnt;

	return 0;
}

static int sof_ipc_trace_update_filter(struct snd_sof_dev *sdev, int num_elems,
				       struct sof_ipc_trace_filter_elem *elems)
{
	struct sof_ipc_trace_filter *msg;
	struct sof_ipc_reply reply;
	size_t size;
	int ret;

	size = struct_size(msg, elems, num_elems);
	if (size > SOF_IPC_MSG_MAX_SIZE)
		return -EINVAL;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->hdr.size = size;
	msg->hdr.cmd = SOF_IPC_GLB_TRACE_MSG | SOF_IPC_TRACE_FILTER_UPDATE;
	msg->elem_cnt = num_elems;
	memcpy(&msg->elems[0], elems, num_elems * sizeof(*elems));

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_noidle(sdev->dev);
		dev_err(sdev->dev, "error: enabling device failed: %d\n", ret);
		goto error;
	}
	ret = sof_ipc_tx_message(sdev->ipc, msg->hdr.cmd, msg, msg->hdr.size,
				 &reply, sizeof(reply));
	pm_runtime_mark_last_busy(sdev->dev);
	pm_runtime_put_autosuspend(sdev->dev);

error:
	kfree(msg);
	return ret ? ret : reply.error;
}

static ssize_t sof_dfsentry_trace_filter_write(struct file *file, const char __user *from,
					       size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct sof_ipc_trace_filter_elem *elems = NULL;
	struct snd_sof_dev *sdev = dfse->sdev;
	loff_t pos = 0;
	int num_elems;
	char *string;
	int ret;

	if (count > TRACE_FILTER_MAX_CONFIG_STRING_LENGTH) {
		dev_err(sdev->dev, "%s too long input, %zu > %d\n", __func__, count,
			TRACE_FILTER_MAX_CONFIG_STRING_LENGTH);
		return -EINVAL;
	}

	string = kmalloc(count + 1, GFP_KERNEL);
	if (!string)
		return -ENOMEM;

	/* assert null termination */
	string[count] = 0;
	ret = simple_write_to_buffer(string, count, &pos, from, count);
	if (ret < 0)
		goto error;

	ret = trace_filter_parse(sdev, string, &num_elems, &elems);
	if (ret < 0) {
		dev_err(sdev->dev, "error: fail in trace_filter_parse, %d\n", ret);
		goto error;
	}

	if (num_elems) {
		ret = sof_ipc_trace_update_filter(sdev, num_elems, elems);
		if (ret < 0) {
			dev_err(sdev->dev, "error: fail in sof_ipc_trace_update_filter %d\n", ret);
			goto error;
		}
	}
	ret = count;
error:
	kfree(string);
	kfree(elems);
	return ret;
}

static const struct file_operations sof_dfs_trace_filter_fops = {
	.open = simple_open,
	.write = sof_dfsentry_trace_filter_write,
	.llseek = default_llseek,
};

static int trace_debugfs_filter_create(struct snd_sof_dev *sdev)
{
	struct snd_sof_dfsentry *dfse;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->sdev = sdev;
	dfse->type = SOF_DFSENTRY_TYPE_BUF;

	debugfs_create_file("filter", 0200, sdev->debugfs_root, dfse,
			    &sof_dfs_trace_filter_fops);
	/* add to dfsentry list */
	list_add(&dfse->list, &sdev->dfsentry_list);

	return 0;
}

static size_t sof_trace_avail(struct snd_sof_dev *sdev,
			      loff_t pos, size_t buffer_size)
{
	loff_t host_offset = READ_ONCE(sdev->host_offset);

	/*
	 * If host offset is less than local pos, it means write pointer of
	 * host DMA buffer has been wrapped. We should output the trace data
	 * at the end of host DMA buffer at first.
	 */
	if (host_offset < pos)
		return buffer_size - pos;

	/* If there is available trace data now, it is unnecessary to wait. */
	if (host_offset > pos)
		return host_offset - pos;

	return 0;
}

static size_t sof_wait_trace_avail(struct snd_sof_dev *sdev,
				   loff_t pos, size_t buffer_size)
{
	wait_queue_entry_t wait;
	size_t ret = sof_trace_avail(sdev, pos, buffer_size);

	/* data immediately available */
	if (ret)
		return ret;

	if (!sdev->dtrace_is_enabled && sdev->dtrace_draining) {
		/*
		 * tracing has ended and all traces have been
		 * read by client, return EOF
		 */
		sdev->dtrace_draining = false;
		return 0;
	}

	/* wait for available trace data from FW */
	init_waitqueue_entry(&wait, current);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&sdev->trace_sleep, &wait);

	if (!signal_pending(current)) {
		/* set timeout to max value, no error code */
		schedule_timeout(MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&sdev->trace_sleep, &wait);

	return sof_trace_avail(sdev, pos, buffer_size);
}

static ssize_t sof_dfsentry_trace_read(struct file *file, char __user *buffer,
				       size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	unsigned long rem;
	loff_t lpos = *ppos;
	size_t avail, buffer_size = dfse->size;
	u64 lpos_64;

	/* make sure we know about any failures on the DSP side */
	sdev->dtrace_error = false;

	/* check pos and count */
	if (lpos < 0)
		return -EINVAL;
	if (!count)
		return 0;

	/* check for buffer wrap and count overflow */
	lpos_64 = lpos;
	lpos = do_div(lpos_64, buffer_size);

	if (count > buffer_size - lpos) /* min() not used to avoid sparse warnings */
		count = buffer_size - lpos;

	/* get available count based on current host offset */
	avail = sof_wait_trace_avail(sdev, lpos, buffer_size);
	if (sdev->dtrace_error) {
		dev_err(sdev->dev, "error: trace IO error\n");
		return -EIO;
	}

	/* make sure count is <= avail */
	count = avail > count ? count : avail;

	/* copy available trace data to debugfs */
	rem = copy_to_user(buffer, ((u8 *)(dfse->buf) + lpos), count);
	if (rem)
		return -EFAULT;

	*ppos += count;

	/* move debugfs reading position */
	return count;
}

static int sof_dfsentry_trace_release(struct inode *inode, struct file *file)
{
	struct snd_sof_dfsentry *dfse = inode->i_private;
	struct snd_sof_dev *sdev = dfse->sdev;

	/* avoid duplicate traces at next open */
	if (!sdev->dtrace_is_enabled)
		sdev->host_offset = 0;

	return 0;
}

static const struct file_operations sof_dfs_trace_fops = {
	.open = simple_open,
	.read = sof_dfsentry_trace_read,
	.llseek = default_llseek,
	.release = sof_dfsentry_trace_release,
};

static int trace_debugfs_create(struct snd_sof_dev *sdev)
{
	struct snd_sof_dfsentry *dfse;
	int ret;

	if (!sdev)
		return -EINVAL;

	ret = trace_debugfs_filter_create(sdev);
	if (ret < 0)
		dev_err(sdev->dev, "error: fail in %s, %d", __func__, ret);

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->type = SOF_DFSENTRY_TYPE_BUF;
	dfse->buf = sdev->dmatb.area;
	dfse->size = sdev->dmatb.bytes;
	dfse->sdev = sdev;

	debugfs_create_file("trace", 0444, sdev->debugfs_root, dfse,
			    &sof_dfs_trace_fops);

	return 0;
}

int snd_sof_init_trace_ipc(struct snd_sof_dev *sdev)
{
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;
	struct sof_ipc_dma_trace_params_ext params;
	struct sof_ipc_reply ipc_reply;
	int ret;

	if (!sdev->dtrace_is_supported)
		return 0;

	if (sdev->dtrace_is_enabled || !sdev->dma_trace_pages)
		return -EINVAL;

	/* set IPC parameters */
	params.hdr.cmd = SOF_IPC_GLB_TRACE_MSG;
	/* PARAMS_EXT is only supported from ABI 3.7.0 onwards */
	if (v->abi_version >= SOF_ABI_VER(3, 7, 0)) {
		params.hdr.size = sizeof(struct sof_ipc_dma_trace_params_ext);
		params.hdr.cmd |= SOF_IPC_TRACE_DMA_PARAMS_EXT;
		params.timestamp_ns = ktime_get(); /* in nanosecond */
	} else {
		params.hdr.size = sizeof(struct sof_ipc_dma_trace_params);
		params.hdr.cmd |= SOF_IPC_TRACE_DMA_PARAMS;
	}
	params.buffer.phy_addr = sdev->dmatp.addr;
	params.buffer.size = sdev->dmatb.bytes;
	params.buffer.pages = sdev->dma_trace_pages;
	params.stream_tag = 0;

	sdev->host_offset = 0;
	sdev->dtrace_draining = false;

	ret = snd_sof_dma_trace_init(sdev, &params.stream_tag);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: fail in snd_sof_dma_trace_init %d\n", ret);
		return ret;
	}
	dev_dbg(sdev->dev, "stream_tag: %d\n", params.stream_tag);

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc,
				 params.hdr.cmd, &params, sizeof(params),
				 &ipc_reply, sizeof(ipc_reply));
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: can't set params for DMA for trace %d\n", ret);
		goto trace_release;
	}

	ret = snd_sof_dma_trace_trigger(sdev, SNDRV_PCM_TRIGGER_START);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: snd_sof_dma_trace_trigger: start: %d\n", ret);
		goto trace_release;
	}

	sdev->dtrace_is_enabled = true;

	return 0;

trace_release:
	snd_sof_dma_trace_release(sdev);
	return ret;
}

int snd_sof_init_trace(struct snd_sof_dev *sdev)
{
	int ret;

	if (!sdev->dtrace_is_supported)
		return 0;

	/* set false before start initialization */
	sdev->dtrace_is_enabled = false;

	/* allocate trace page table buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev,
				  PAGE_SIZE, &sdev->dmatp);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: can't alloc page table for trace %d\n", ret);
		return ret;
	}

	/* allocate trace data buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV_SG, sdev->dev,
				  DMA_BUF_SIZE_FOR_TRACE, &sdev->dmatb);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: can't alloc buffer for trace %d\n", ret);
		goto page_err;
	}

	/* create compressed page table for audio firmware */
	ret = snd_sof_create_page_table(sdev->dev, &sdev->dmatb,
					sdev->dmatp.area, sdev->dmatb.bytes);
	if (ret < 0)
		goto table_err;

	sdev->dma_trace_pages = ret;
	dev_dbg(sdev->dev, "dma_trace_pages: %d\n", sdev->dma_trace_pages);

	if (sdev->first_boot) {
		ret = trace_debugfs_create(sdev);
		if (ret < 0)
			goto table_err;
	}

	init_waitqueue_head(&sdev->trace_sleep);

	ret = snd_sof_init_trace_ipc(sdev);
	if (ret < 0)
		goto table_err;

	return 0;
table_err:
	sdev->dma_trace_pages = 0;
	snd_dma_free_pages(&sdev->dmatb);
page_err:
	snd_dma_free_pages(&sdev->dmatp);
	return ret;
}
EXPORT_SYMBOL(snd_sof_init_trace);

int snd_sof_trace_update_pos(struct snd_sof_dev *sdev,
			     struct sof_ipc_dma_trace_posn *posn)
{
	if (!sdev->dtrace_is_supported)
		return 0;

	if (sdev->dtrace_is_enabled && sdev->host_offset != posn->host_offset) {
		sdev->host_offset = posn->host_offset;
		wake_up(&sdev->trace_sleep);
	}

	if (posn->overflow != 0)
		dev_err(sdev->dev,
			"error: DSP trace buffer overflow %u bytes. Total messages %d\n",
			posn->overflow, posn->messages);

	return 0;
}

/* an error has occurred within the DSP that prevents further trace */
void snd_sof_trace_notify_for_error(struct snd_sof_dev *sdev)
{
	if (!sdev->dtrace_is_supported)
		return;

	if (sdev->dtrace_is_enabled) {
		sdev->dtrace_error = true;
		wake_up(&sdev->trace_sleep);
	}
}
EXPORT_SYMBOL(snd_sof_trace_notify_for_error);

void snd_sof_release_trace(struct snd_sof_dev *sdev)
{
	int ret;

	if (!sdev->dtrace_is_supported || !sdev->dtrace_is_enabled)
		return;

	ret = snd_sof_dma_trace_trigger(sdev, SNDRV_PCM_TRIGGER_STOP);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: snd_sof_dma_trace_trigger: stop: %d\n", ret);

	ret = snd_sof_dma_trace_release(sdev);
	if (ret < 0)
		dev_err(sdev->dev,
			"error: fail in snd_sof_dma_trace_release %d\n", ret);

	sdev->dtrace_is_enabled = false;
	sdev->dtrace_draining = true;
	wake_up(&sdev->trace_sleep);
}
EXPORT_SYMBOL(snd_sof_release_trace);

void snd_sof_free_trace(struct snd_sof_dev *sdev)
{
	if (!sdev->dtrace_is_supported)
		return;

	snd_sof_release_trace(sdev);

	if (sdev->dma_trace_pages) {
		snd_dma_free_pages(&sdev->dmatb);
		snd_dma_free_pages(&sdev->dmatp);
		sdev->dma_trace_pages = 0;
	}
}
EXPORT_SYMBOL(snd_sof_free_trace);
