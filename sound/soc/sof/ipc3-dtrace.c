// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>

#include <linux/debugfs.h>
#include <linux/sched/signal.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ops.h"
#include "sof-utils.h"
#include "ipc3-priv.h"

#define TRACE_FILTER_ELEMENTS_PER_ENTRY 4
#define TRACE_FILTER_MAX_CONFIG_STRING_LENGTH 1024

enum sof_dtrace_state {
	SOF_DTRACE_DISABLED,
	SOF_DTRACE_STOPPED,
	SOF_DTRACE_INITIALIZING,
	SOF_DTRACE_ENABLED,
};

struct sof_dtrace_priv {
	struct snd_dma_buffer dmatb;
	struct snd_dma_buffer dmatp;
	int dma_trace_pages;
	wait_queue_head_t trace_sleep;
	u32 host_offset;
	bool dtrace_error;
	bool dtrace_draining;
	enum sof_dtrace_state dtrace_state;
};

static bool trace_pos_update_expected(struct sof_dtrace_priv *priv)
{
	if (priv->dtrace_state == SOF_DTRACE_ENABLED ||
	    priv->dtrace_state == SOF_DTRACE_INITIALIZING)
		return true;

	return false;
}

static int trace_filter_append_elem(struct snd_sof_dev *sdev, u32 key, u32 value,
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
	int log_level, pipe_id, comp_id, read, ret;
	int len = strlen(line);
	int cnt = *counter;
	u32 uuid_id;

	/* ignore empty content */
	ret = sscanf(line, " %n", &read);
	if (!ret && read == len)
		return len;

	ret = sscanf(line, " %d %x %d %d %n", &log_level, &uuid_id, &pipe_id, &comp_id, &read);
	if (ret != TRACE_FILTER_ELEMENTS_PER_ENTRY || read != len) {
		dev_err(sdev->dev, "Invalid trace filter entry '%s'\n", line);
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
			dev_err(sdev->dev,
				"Parsing filter entry '%s' failed with %d\n",
				entry, entry_len);
			return -EINVAL;
		}
	}

	*out_elem_cnt = cnt;

	return 0;
}

static int ipc3_trace_update_filter(struct snd_sof_dev *sdev, int num_elems,
				    struct sof_ipc_trace_filter_elem *elems)
{
	struct sof_ipc_trace_filter *msg;
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

	ret = pm_runtime_resume_and_get(sdev->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err(sdev->dev, "enabling device failed: %d\n", ret);
		goto error;
	}
	ret = sof_ipc_tx_message_no_reply(sdev->ipc, msg, msg->hdr.size);
	pm_runtime_mark_last_busy(sdev->dev);
	pm_runtime_put_autosuspend(sdev->dev);

error:
	kfree(msg);
	return ret;
}

static ssize_t dfsentry_trace_filter_write(struct file *file, const char __user *from,
					   size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct sof_ipc_trace_filter_elem *elems = NULL;
	struct snd_sof_dev *sdev = dfse->sdev;
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

	if (copy_from_user(string, from, count)) {
		ret = -EFAULT;
		goto error;
	}
	string[count] = '\0';

	ret = trace_filter_parse(sdev, string, &num_elems, &elems);
	if (ret < 0)
		goto error;

	if (num_elems) {
		ret = ipc3_trace_update_filter(sdev, num_elems, elems);
		if (ret < 0) {
			dev_err(sdev->dev, "Filter update failed: %d\n", ret);
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
	.write = dfsentry_trace_filter_write,
	.llseek = default_llseek,
};

static int debugfs_create_trace_filter(struct snd_sof_dev *sdev)
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

static bool sof_dtrace_set_host_offset(struct sof_dtrace_priv *priv, u32 new_offset)
{
	u32 host_offset = READ_ONCE(priv->host_offset);

	if (host_offset != new_offset) {
		/* This is a bit paranoid and unlikely that it is needed */
		u32 ret = cmpxchg(&priv->host_offset, host_offset, new_offset);

		if (ret == host_offset)
			return true;
	}

	return false;
}

static size_t sof_dtrace_avail(struct snd_sof_dev *sdev,
			       loff_t pos, size_t buffer_size)
{
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;
	loff_t host_offset = READ_ONCE(priv->host_offset);

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

static size_t sof_wait_dtrace_avail(struct snd_sof_dev *sdev, loff_t pos,
				    size_t buffer_size)
{
	size_t ret = sof_dtrace_avail(sdev, pos, buffer_size);
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;
	wait_queue_entry_t wait;

	/* data immediately available */
	if (ret)
		return ret;

	if (priv->dtrace_draining && !trace_pos_update_expected(priv)) {
		/*
		 * tracing has ended and all traces have been
		 * read by client, return EOF
		 */
		priv->dtrace_draining = false;
		return 0;
	}

	/* wait for available trace data from FW */
	init_waitqueue_entry(&wait, current);
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&priv->trace_sleep, &wait);

	if (!signal_pending(current)) {
		/* set timeout to max value, no error code */
		schedule_timeout(MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&priv->trace_sleep, &wait);

	return sof_dtrace_avail(sdev, pos, buffer_size);
}

static ssize_t dfsentry_dtrace_read(struct file *file, char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;
	unsigned long rem;
	loff_t lpos = *ppos;
	size_t avail, buffer_size = dfse->size;
	u64 lpos_64;

	/* make sure we know about any failures on the DSP side */
	priv->dtrace_error = false;

	/* check pos and count */
	if (lpos < 0)
		return -EINVAL;
	if (!count)
		return 0;

	/* check for buffer wrap and count overflow */
	lpos_64 = lpos;
	lpos = do_div(lpos_64, buffer_size);

	/* get available count based on current host offset */
	avail = sof_wait_dtrace_avail(sdev, lpos, buffer_size);
	if (priv->dtrace_error) {
		dev_err(sdev->dev, "trace IO error\n");
		return -EIO;
	}

	/* no new trace data */
	if (!avail)
		return 0;

	/* make sure count is <= avail */
	if (count > avail)
		count = avail;

	/*
	 * make sure that all trace data is available for the CPU as the trace
	 * data buffer might be allocated from non consistent memory.
	 * Note: snd_dma_buffer_sync() is called for normal audio playback and
	 *	 capture streams also.
	 */
	snd_dma_buffer_sync(&priv->dmatb, SNDRV_DMA_SYNC_CPU);
	/* copy available trace data to debugfs */
	rem = copy_to_user(buffer, ((u8 *)(dfse->buf) + lpos), count);
	if (rem)
		return -EFAULT;

	*ppos += count;

	/* move debugfs reading position */
	return count;
}

static int dfsentry_dtrace_release(struct inode *inode, struct file *file)
{
	struct snd_sof_dfsentry *dfse = inode->i_private;
	struct snd_sof_dev *sdev = dfse->sdev;
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;

	/* avoid duplicate traces at next open */
	if (priv->dtrace_state != SOF_DTRACE_ENABLED)
		sof_dtrace_set_host_offset(priv, 0);

	return 0;
}

static const struct file_operations sof_dfs_dtrace_fops = {
	.open = simple_open,
	.read = dfsentry_dtrace_read,
	.llseek = default_llseek,
	.release = dfsentry_dtrace_release,
};

static int debugfs_create_dtrace(struct snd_sof_dev *sdev)
{
	struct sof_dtrace_priv *priv;
	struct snd_sof_dfsentry *dfse;
	int ret;

	if (!sdev)
		return -EINVAL;

	priv = sdev->fw_trace_data;

	ret = debugfs_create_trace_filter(sdev);
	if (ret < 0)
		dev_warn(sdev->dev, "failed to create filter debugfs file: %d", ret);

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->type = SOF_DFSENTRY_TYPE_BUF;
	dfse->buf = priv->dmatb.area;
	dfse->size = priv->dmatb.bytes;
	dfse->sdev = sdev;

	debugfs_create_file("trace", 0444, sdev->debugfs_root, dfse,
			    &sof_dfs_dtrace_fops);

	return 0;
}

static int ipc3_dtrace_enable(struct snd_sof_dev *sdev)
{
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;
	struct sof_ipc_dma_trace_params_ext params;
	int ret;

	if (!sdev->fw_trace_is_supported)
		return 0;

	if (priv->dtrace_state == SOF_DTRACE_ENABLED || !priv->dma_trace_pages)
		return -EINVAL;

	if (priv->dtrace_state == SOF_DTRACE_STOPPED)
		goto start;

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
	params.buffer.phy_addr = priv->dmatp.addr;
	params.buffer.size = priv->dmatb.bytes;
	params.buffer.pages = priv->dma_trace_pages;
	params.stream_tag = 0;

	sof_dtrace_set_host_offset(priv, 0);
	priv->dtrace_draining = false;

	ret = sof_dtrace_host_init(sdev, &priv->dmatb, &params);
	if (ret < 0) {
		dev_err(sdev->dev, "Host dtrace init failed: %d\n", ret);
		return ret;
	}
	dev_dbg(sdev->dev, "stream_tag: %d\n", params.stream_tag);

	/* send IPC to the DSP */
	priv->dtrace_state = SOF_DTRACE_INITIALIZING;
	ret = sof_ipc_tx_message_no_reply(sdev->ipc, &params, sizeof(params));
	if (ret < 0) {
		dev_err(sdev->dev, "can't set params for DMA for trace %d\n", ret);
		goto trace_release;
	}

start:
	priv->dtrace_state = SOF_DTRACE_ENABLED;

	ret = sof_dtrace_host_trigger(sdev, SNDRV_PCM_TRIGGER_START);
	if (ret < 0) {
		dev_err(sdev->dev, "Host dtrace trigger start failed: %d\n", ret);
		goto trace_release;
	}

	return 0;

trace_release:
	priv->dtrace_state = SOF_DTRACE_DISABLED;
	sof_dtrace_host_release(sdev);
	return ret;
}

static int ipc3_dtrace_init(struct snd_sof_dev *sdev)
{
	struct sof_dtrace_priv *priv;
	int ret;

	/* dtrace is only supported with SOF_IPC */
	if (sdev->pdata->ipc_type != SOF_IPC)
		return -EOPNOTSUPP;

	if (sdev->fw_trace_data) {
		dev_err(sdev->dev, "fw_trace_data has been already allocated\n");
		return -EBUSY;
	}

	priv = devm_kzalloc(sdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	sdev->fw_trace_data = priv;

	/* set false before start initialization */
	priv->dtrace_state = SOF_DTRACE_DISABLED;

	/* allocate trace page table buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev,
				  PAGE_SIZE, &priv->dmatp);
	if (ret < 0) {
		dev_err(sdev->dev, "can't alloc page table for trace %d\n", ret);
		return ret;
	}

	/* allocate trace data buffer */
	ret = snd_dma_alloc_dir_pages(SNDRV_DMA_TYPE_DEV_SG, sdev->dev,
				      DMA_FROM_DEVICE, DMA_BUF_SIZE_FOR_TRACE,
				      &priv->dmatb);
	if (ret < 0) {
		dev_err(sdev->dev, "can't alloc buffer for trace %d\n", ret);
		goto page_err;
	}

	/* create compressed page table for audio firmware */
	ret = snd_sof_create_page_table(sdev->dev, &priv->dmatb,
					priv->dmatp.area, priv->dmatb.bytes);
	if (ret < 0)
		goto table_err;

	priv->dma_trace_pages = ret;
	dev_dbg(sdev->dev, "dma_trace_pages: %d\n", priv->dma_trace_pages);

	if (sdev->first_boot) {
		ret = debugfs_create_dtrace(sdev);
		if (ret < 0)
			goto table_err;
	}

	init_waitqueue_head(&priv->trace_sleep);

	ret = ipc3_dtrace_enable(sdev);
	if (ret < 0)
		goto table_err;

	return 0;
table_err:
	priv->dma_trace_pages = 0;
	snd_dma_free_pages(&priv->dmatb);
page_err:
	snd_dma_free_pages(&priv->dmatp);
	return ret;
}

int ipc3_dtrace_posn_update(struct snd_sof_dev *sdev,
			    struct sof_ipc_dma_trace_posn *posn)
{
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;

	if (!sdev->fw_trace_is_supported)
		return 0;

	if (trace_pos_update_expected(priv) &&
	    sof_dtrace_set_host_offset(priv, posn->host_offset))
		wake_up(&priv->trace_sleep);

	if (posn->overflow != 0)
		dev_err(sdev->dev,
			"DSP trace buffer overflow %u bytes. Total messages %d\n",
			posn->overflow, posn->messages);

	return 0;
}

/* an error has occurred within the DSP that prevents further trace */
static void ipc3_dtrace_fw_crashed(struct snd_sof_dev *sdev)
{
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;

	if (priv->dtrace_state == SOF_DTRACE_ENABLED) {
		priv->dtrace_error = true;
		wake_up(&priv->trace_sleep);
	}
}

static void ipc3_dtrace_release(struct snd_sof_dev *sdev, bool only_stop)
{
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;
	struct sof_ipc_cmd_hdr hdr;
	int ret;

	if (!sdev->fw_trace_is_supported || priv->dtrace_state == SOF_DTRACE_DISABLED)
		return;

	ret = sof_dtrace_host_trigger(sdev, SNDRV_PCM_TRIGGER_STOP);
	if (ret < 0)
		dev_err(sdev->dev, "Host dtrace trigger stop failed: %d\n", ret);
	priv->dtrace_state = SOF_DTRACE_STOPPED;

	/*
	 * stop and free trace DMA in the DSP. TRACE_DMA_FREE is only supported from
	 * ABI 3.20.0 onwards
	 */
	if (v->abi_version >= SOF_ABI_VER(3, 20, 0)) {
		hdr.size = sizeof(hdr);
		hdr.cmd = SOF_IPC_GLB_TRACE_MSG | SOF_IPC_TRACE_DMA_FREE;

		ret = sof_ipc_tx_message_no_reply(sdev->ipc, &hdr, hdr.size);
		if (ret < 0)
			dev_err(sdev->dev, "DMA_TRACE_FREE failed with error: %d\n", ret);
	}

	if (only_stop)
		goto out;

	ret = sof_dtrace_host_release(sdev);
	if (ret < 0)
		dev_err(sdev->dev, "Host dtrace release failed %d\n", ret);

	priv->dtrace_state = SOF_DTRACE_DISABLED;

out:
	priv->dtrace_draining = true;
	wake_up(&priv->trace_sleep);
}

static void ipc3_dtrace_suspend(struct snd_sof_dev *sdev, pm_message_t pm_state)
{
	ipc3_dtrace_release(sdev, pm_state.event == SOF_DSP_PM_D0);
}

static int ipc3_dtrace_resume(struct snd_sof_dev *sdev)
{
	return ipc3_dtrace_enable(sdev);
}

static void ipc3_dtrace_free(struct snd_sof_dev *sdev)
{
	struct sof_dtrace_priv *priv = sdev->fw_trace_data;

	/* release trace */
	ipc3_dtrace_release(sdev, false);

	if (priv->dma_trace_pages) {
		snd_dma_free_pages(&priv->dmatb);
		snd_dma_free_pages(&priv->dmatp);
		priv->dma_trace_pages = 0;
	}
}

const struct sof_ipc_fw_tracing_ops ipc3_dtrace_ops = {
	.init = ipc3_dtrace_init,
	.free = ipc3_dtrace_free,
	.fw_crashed = ipc3_dtrace_fw_crashed,
	.suspend = ipc3_dtrace_suspend,
	.resume = ipc3_dtrace_resume,
};
