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

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_PROBES)
#include "probe.h"

/**
 * strsplit_u32 - Split string into sequence of u32 tokens
 * @buf:	String to split into tokens.
 * @delim:	String containing delimiter characters.
 * @tkns:	Returned u32 sequence pointer.
 * @num_tkns:	Returned number of tokens obtained.
 */
static int
strsplit_u32(char **buf, const char *delim, u32 **tkns, size_t *num_tkns)
{
	char *s;
	u32 *data, *tmp;
	size_t count = 0;
	size_t cap = 32;
	int ret = 0;

	*tkns = NULL;
	*num_tkns = 0;
	data = kcalloc(cap, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	while ((s = strsep(buf, delim)) != NULL) {
		ret = kstrtouint(s, 0, data + count);
		if (ret)
			goto exit;
		if (++count >= cap) {
			cap *= 2;
			tmp = krealloc(data, cap * sizeof(*data), GFP_KERNEL);
			if (!tmp) {
				ret = -ENOMEM;
				goto exit;
			}
			data = tmp;
		}
	}

	if (!count)
		goto exit;
	*tkns = kmemdup(data, count * sizeof(*data), GFP_KERNEL);
	if (*tkns == NULL) {
		ret = -ENOMEM;
		goto exit;
	}
	*num_tkns = count;

exit:
	kfree(data);
	return ret;
}

static int tokenize_input(const char __user *from, size_t count,
		loff_t *ppos, u32 **tkns, size_t *num_tkns)
{
	char *buf;
	int ret;

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = simple_write_to_buffer(buf, count, ppos, from, count);
	if (ret != count) {
		ret = ret >= 0 ? -EIO : ret;
		goto exit;
	}

	buf[count] = '\0';
	ret = strsplit_u32((char **)&buf, ",", tkns, num_tkns);
exit:
	kfree(buf);
	return ret;
}

static ssize_t probe_points_read(struct file *file,
		char __user *to, size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	struct sof_probe_point_desc *desc;
	size_t num_desc, len = 0;
	char *buf;
	int i, ret;

	if (sdev->extractor_stream_tag == SOF_PROBE_INVALID_NODE_ID) {
		dev_warn(sdev->dev, "no extractor stream running\n");
		return -ENOENT;
	}

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = sof_ipc_probe_points_info(sdev, &desc, &num_desc);
	if (ret < 0)
		goto exit;

	for (i = 0; i < num_desc; i++) {
		ret = snprintf(buf + len, PAGE_SIZE - len,
			"Id: %#010x  Purpose: %d  Node id: %#x\n",
			desc[i].buffer_id, desc[i].purpose, desc[i].stream_tag);
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

static ssize_t probe_points_write(struct file *file,
		const char __user *from, size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	struct sof_probe_point_desc *desc;
	size_t num_tkns, bytes;
	u32 *tkns;
	int ret;

	if (sdev->extractor_stream_tag == SOF_PROBE_INVALID_NODE_ID) {
		dev_warn(sdev->dev, "no extractor stream running\n");
		return -ENOENT;
	}

	ret = tokenize_input(from, count, ppos, &tkns, &num_tkns);
	if (ret < 0)
		return ret;
	bytes = sizeof(*tkns) * num_tkns;
	if (!num_tkns || (bytes % sizeof(*desc))) {
		ret = -EINVAL;
		goto exit;
	}

	desc = (struct sof_probe_point_desc *)tkns;
	ret = sof_ipc_probe_points_add(sdev,
			desc, bytes / sizeof(*desc));
	if (!ret)
		ret = count;
exit:
	kfree(tkns);
	return ret;
}

static const struct file_operations probe_points_fops = {
	.open = simple_open,
	.read = probe_points_read,
	.write = probe_points_write,
	.llseek = default_llseek,
};

static ssize_t probe_points_remove_write(struct file *file,
		const char __user *from, size_t count, loff_t *ppos)
{
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	size_t num_tkns;
	u32 *tkns;
	int ret;

	if (sdev->extractor_stream_tag == SOF_PROBE_INVALID_NODE_ID) {
		dev_warn(sdev->dev, "no extractor stream running\n");
		return -ENOENT;
	}

	ret = tokenize_input(from, count, ppos, &tkns, &num_tkns);
	if (ret < 0)
		return ret;
	if (!num_tkns) {
		ret = -EINVAL;
		goto exit;
	}

	ret = sof_ipc_probe_points_remove(sdev, tkns, num_tkns);
	if (!ret)
		ret = count;
exit:
	kfree(tkns);
	return ret;
}

static const struct file_operations probe_points_remove_fops = {
	.open = simple_open,
	.write = probe_points_remove_write,
	.llseek = default_llseek,
};

static int snd_sof_debugfs_probe_item(struct snd_sof_dev *sdev,
				 const char *name, mode_t mode,
				 const struct file_operations *fops)
{
	struct snd_sof_dfsentry *dfse;

	dfse = devm_kzalloc(sdev->dev, sizeof(*dfse), GFP_KERNEL);
	if (!dfse)
		return -ENOMEM;

	dfse->type = SOF_DFSENTRY_TYPE_BUF;
	dfse->sdev = sdev;

	debugfs_create_file(name, mode, sdev->debugfs_root, dfse, fops);
	/* add to dfsentry list */
	list_add(&dfse->list, &sdev->dfsentry_list);

	return 0;
}
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST)
#define MAX_IPC_FLOOD_DURATION_MS 1000
#define MAX_IPC_FLOOD_COUNT 10000
#define IPC_FLOOD_TEST_RESULT_LEN 512

static int sof_debug_ipc_flood_test(struct snd_sof_dev *sdev,
				    struct snd_sof_dfsentry *dfse,
				    bool flood_duration_test,
				    unsigned long ipc_duration_ms,
				    unsigned long ipc_count)
{
	struct sof_ipc_cmd_hdr hdr;
	struct sof_ipc_reply reply;
	u64 min_response_time = U64_MAX;
	ktime_t start, end, test_end;
	u64 avg_response_time = 0;
	u64 max_response_time = 0;
	u64 ipc_response_time;
	int i = 0;
	int ret;

	/* configure test IPC */
	hdr.cmd = SOF_IPC_GLB_TEST_MSG | SOF_IPC_TEST_IPC_FLOOD;
	hdr.size = sizeof(hdr);

	/* set test end time for duration flood test */
	if (flood_duration_test)
		test_end = ktime_get_ns() + ipc_duration_ms * NSEC_PER_MSEC;

	/* send test IPC's */
	while (1) {
		start = ktime_get();
		ret = sof_ipc_tx_message(sdev->ipc, hdr.cmd, &hdr, hdr.size,
					 &reply, sizeof(reply));
		end = ktime_get();

		if (ret < 0)
			break;

		/* compute min and max response times */
		ipc_response_time = ktime_to_ns(ktime_sub(end, start));
		min_response_time = min(min_response_time, ipc_response_time);
		max_response_time = max(max_response_time, ipc_response_time);

		/* sum up response times */
		avg_response_time += ipc_response_time;
		i++;

		/* test complete? */
		if (flood_duration_test) {
			if (ktime_to_ns(end) >= test_end)
				break;
		} else {
			if (i == ipc_count)
				break;
		}
	}

	if (ret < 0)
		dev_err(sdev->dev,
			"error: ipc flood test failed at %d iterations\n", i);

	/* return if the first IPC fails */
	if (!i)
		return ret;

	/* compute average response time */
	do_div(avg_response_time, i);

	/* clear previous test output */
	memset(dfse->cache_buf, 0, IPC_FLOOD_TEST_RESULT_LEN);

	if (flood_duration_test) {
		dev_dbg(sdev->dev, "IPC Flood test duration: %lums\n",
			ipc_duration_ms);
		snprintf(dfse->cache_buf, IPC_FLOOD_TEST_RESULT_LEN,
			 "IPC Flood test duration: %lums\n", ipc_duration_ms);
	}

	dev_dbg(sdev->dev,
		"IPC Flood count: %d, Avg response time: %lluns\n",
		i, avg_response_time);
	dev_dbg(sdev->dev, "Max response time: %lluns\n",
		max_response_time);
	dev_dbg(sdev->dev, "Min response time: %lluns\n",
		min_response_time);

	/* format output string */
	snprintf(dfse->cache_buf + strlen(dfse->cache_buf),
		 IPC_FLOOD_TEST_RESULT_LEN - strlen(dfse->cache_buf),
		 "IPC Flood count: %d\nAvg response time: %lluns\n",
		 i, avg_response_time);

	snprintf(dfse->cache_buf + strlen(dfse->cache_buf),
		 IPC_FLOOD_TEST_RESULT_LEN - strlen(dfse->cache_buf),
		 "Max response time: %lluns\nMin response time: %lluns\n",
		 max_response_time, min_response_time);

	return ret;
}
#endif

static ssize_t sof_dfsentry_write(struct file *file, const char __user *buffer,
				  size_t count, loff_t *ppos)
{
#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST)
	struct snd_sof_dfsentry *dfse = file->private_data;
	struct snd_sof_dev *sdev = dfse->sdev;
	unsigned long ipc_duration_ms = 0;
	bool flood_duration_test = false;
	unsigned long ipc_count = 0;
	struct dentry *dentry;
	int err;
#endif
	size_t size;
	char *string;
	int ret;

	string = kzalloc(count, GFP_KERNEL);
	if (!string)
		return -ENOMEM;

	size = simple_write_to_buffer(string, count, ppos, buffer, count);
	ret = size;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST)
	/*
	 * write op is only supported for ipc_flood_count or
	 * ipc_flood_duration_ms debugfs entries atm.
	 * ipc_flood_count floods the DSP with the number of IPC's specified.
	 * ipc_duration_ms test floods the DSP for the time specified
	 * in the debugfs entry.
	 */
	dentry = file->f_path.dentry;
	if (strcmp(dentry->d_name.name, "ipc_flood_count") &&
	    strcmp(dentry->d_name.name, "ipc_flood_duration_ms")) {
		ret = -EINVAL;
		goto out;
	}

	if (!strcmp(dentry->d_name.name, "ipc_flood_duration_ms"))
		flood_duration_test = true;

	/* test completion criterion */
	if (flood_duration_test)
		ret = kstrtoul(string, 0, &ipc_duration_ms);
	else
		ret = kstrtoul(string, 0, &ipc_count);
	if (ret < 0)
		goto out;

	/* limit max duration/ipc count for flood test */
	if (flood_duration_test) {
		if (!ipc_duration_ms) {
			ret = size;
			goto out;
		}

		/* find the minimum. min() is not used to avoid warnings */
		if (ipc_duration_ms > MAX_IPC_FLOOD_DURATION_MS)
			ipc_duration_ms = MAX_IPC_FLOOD_DURATION_MS;
	} else {
		if (!ipc_count) {
			ret = size;
			goto out;
		}

		/* find the minimum. min() is not used to avoid warnings */
		if (ipc_count > MAX_IPC_FLOOD_COUNT)
			ipc_count = MAX_IPC_FLOOD_COUNT;
	}

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(sdev->dev,
				    "error: debugfs write failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		goto out;
	}

	/* flood test */
	ret = sof_debug_ipc_flood_test(sdev, dfse, flood_duration_test,
				       ipc_duration_ms, ipc_count);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: debugfs write failed to idle %d\n",
				    err);

	/* return size if test is successful */
	if (ret >= 0)
		ret = size;
out:
#endif
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

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST)
	struct dentry *dentry;

	dentry = file->f_path.dentry;
	if ((!strcmp(dentry->d_name.name, "ipc_flood_count") ||
	     !strcmp(dentry->d_name.name, "ipc_flood_duration_ms")) &&
	    dfse->cache_buf) {
		if (*ppos)
			return 0;

		count = strlen(dfse->cache_buf);
		size_ret = copy_to_user(buffer, dfse->cache_buf, count);
		if (size_ret)
			return -EFAULT;

		*ppos += count;
		return count;
	}
#endif
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
int snd_sof_debugfs_io_item(struct snd_sof_dev *sdev,
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
EXPORT_SYMBOL_GPL(snd_sof_debugfs_io_item);

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

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST)
	/*
	 * cache_buf is unused for SOF_DFSENTRY_TYPE_BUF debugfs entries.
	 * So, use it to save the results of the last IPC flood test.
	 */
	dfse->cache_buf = devm_kzalloc(sdev->dev, IPC_FLOOD_TEST_RESULT_LEN,
				       GFP_KERNEL);
	if (!dfse->cache_buf)
		return -ENOMEM;
#endif

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

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0 && ret != -EACCES) {
		pm_runtime_put_noidle(sdev->dev);
		dev_err(sdev->dev, "error: enabling device failed: %d\n", ret);
		goto error;
	}

	ret = sof_ipc_tx_message(sdev->ipc, msg.cmd, &msg, msg.size, reply, SOF_IPC_MSG_MAX_SIZE);
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
		ret = snprintf(buf + len, buff_size - len, "zone %d.%d used %#8x free %#8x\n",
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
	const struct snd_sof_dsp_ops *ops = sof_ops(sdev);
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

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_PROBES)
	err = snd_sof_debugfs_probe_item(sdev, "probe_points",
			0644, &probe_points_fops);
	if (err < 0)
		return err;
	err = snd_sof_debugfs_probe_item(sdev, "probe_points_remove",
			0200, &probe_points_remove_fops);
	if (err < 0)
		return err;
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_IPC_FLOOD_TEST)
	/* create read-write ipc_flood_count debugfs entry */
	err = snd_sof_debugfs_buf_item(sdev, NULL, 0,
				       "ipc_flood_count", 0666);

	/* errors are only due to memory allocation, not debugfs */
	if (err < 0)
		return err;

	/* create read-write ipc_flood_duration_ms debugfs entry */
	err = snd_sof_debugfs_buf_item(sdev, NULL, 0,
				       "ipc_flood_duration_ms", 0666);

	/* errors are only due to memory allocation, not debugfs */
	if (err < 0)
		return err;
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(snd_sof_dbg_init);

void snd_sof_free_debug(struct snd_sof_dev *sdev)
{
	debugfs_remove_recursive(sdev->debugfs_root);
}
EXPORT_SYMBOL_GPL(snd_sof_free_debug);

void snd_sof_handle_fw_exception(struct snd_sof_dev *sdev)
{
	if (IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_RETAIN_DSP_CONTEXT) ||
	    (sof_core_debug & SOF_DBG_RETAIN_CTX)) {
		/* should we prevent DSP entering D3 ? */
		dev_info(sdev->dev, "info: preventing DSP entering D3 state to preserve context\n");
		pm_runtime_get_noresume(sdev->dev);
	}

	/* dump vital information to the logs */
	snd_sof_dsp_dbg_dump(sdev, SOF_DBG_DUMP_REGS | SOF_DBG_DUMP_MBOX);
	snd_sof_ipc_dump(sdev);
	snd_sof_trace_notify_for_error(sdev);
}
EXPORT_SYMBOL(snd_sof_handle_fw_exception);
