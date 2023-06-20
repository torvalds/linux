// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2019-2022 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//
// SOF client support:
//  Ranjani Sridharan <ranjani.sridharan@linux.intel.com>
//  Peter Ujfalusi <peter.ujfalusi@linux.intel.com>
//

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/sof/header.h>
#include "sof-client.h"
#include "sof-client-probes.h"

#define SOF_PROBES_SUSPEND_DELAY_MS 3000
/* only extraction supported for now */
#define SOF_PROBES_NUM_DAI_LINKS 1

#define SOF_PROBES_INVALID_NODE_ID UINT_MAX

static bool __read_mostly sof_probes_enabled;
module_param_named(enable, sof_probes_enabled, bool, 0444);
MODULE_PARM_DESC(enable, "Enable SOF probes support");

struct sof_probes_priv {
	struct dentry *dfs_points;
	struct dentry *dfs_points_remove;
	u32 extractor_stream_tag;
	struct snd_soc_card card;

	const struct sof_probes_host_ops *host_ops;
};

struct sof_probe_point_desc {
	unsigned int buffer_id;
	unsigned int purpose;
	unsigned int stream_tag;
} __packed;

struct sof_probe_dma {
	unsigned int stream_tag;
	unsigned int dma_buffer_size;
} __packed;

struct sof_ipc_probe_dma_add_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	struct sof_probe_dma dma[];
} __packed;

struct sof_ipc_probe_info_params {
	struct sof_ipc_reply rhdr;
	unsigned int num_elems;
	union {
		struct sof_probe_dma dma[0];
		struct sof_probe_point_desc desc[0];
	};
} __packed;

struct sof_ipc_probe_point_add_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	struct sof_probe_point_desc desc[];
} __packed;

struct sof_ipc_probe_point_remove_params {
	struct sof_ipc_cmd_hdr hdr;
	unsigned int num_elems;
	unsigned int buffer_id[];
} __packed;

/**
 * sof_probes_init - initialize data probing
 * @cdev:		SOF client device
 * @stream_tag:		Extractor stream tag
 * @buffer_size:	DMA buffer size to set for extractor
 *
 * Host chooses whether extraction is supported or not by providing
 * valid stream tag to DSP. Once specified, stream described by that
 * tag will be tied to DSP for extraction for the entire lifetime of
 * probe.
 *
 * Probing is initialized only once and each INIT request must be
 * matched by DEINIT call.
 */
static int sof_probes_init(struct sof_client_dev *cdev, u32 stream_tag,
			   size_t buffer_size)
{
	struct sof_ipc_probe_dma_add_params *msg;
	size_t size = struct_size(msg, dma, 1);
	struct sof_ipc_reply reply;
	int ret;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->hdr.size = size;
	msg->hdr.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_INIT;
	msg->num_elems = 1;
	msg->dma[0].stream_tag = stream_tag;
	msg->dma[0].dma_buffer_size = buffer_size;

	ret = sof_client_ipc_tx_message(cdev, msg, &reply, sizeof(reply));
	kfree(msg);
	return ret;
}

/**
 * sof_probes_deinit - cleanup after data probing
 * @cdev:		SOF client device
 *
 * Host sends DEINIT request to free previously initialized probe
 * on DSP side once it is no longer needed. DEINIT only when there
 * are no probes connected and with all injectors detached.
 */
static int sof_probes_deinit(struct sof_client_dev *cdev)
{
	struct sof_ipc_cmd_hdr msg;
	struct sof_ipc_reply reply;

	msg.size = sizeof(msg);
	msg.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_DEINIT;

	return sof_client_ipc_tx_message(cdev, &msg, &reply, sizeof(reply));
}

static int sof_probes_info(struct sof_client_dev *cdev, unsigned int cmd,
			   void **params, size_t *num_params)
{
	size_t max_msg_size = sof_client_get_ipc_max_payload_size(cdev);
	struct sof_ipc_probe_info_params msg = {{{0}}};
	struct sof_ipc_probe_info_params *reply;
	size_t bytes;
	int ret;

	*params = NULL;
	*num_params = 0;

	reply = kzalloc(max_msg_size, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;
	msg.rhdr.hdr.size = sizeof(msg);
	msg.rhdr.hdr.cmd = SOF_IPC_GLB_PROBE | cmd;

	ret = sof_client_ipc_tx_message(cdev, &msg, reply, max_msg_size);
	if (ret < 0 || reply->rhdr.error < 0)
		goto exit;

	if (!reply->num_elems)
		goto exit;

	if (cmd == SOF_IPC_PROBE_DMA_INFO)
		bytes = sizeof(reply->dma[0]);
	else
		bytes = sizeof(reply->desc[0]);
	bytes *= reply->num_elems;
	*params = kmemdup(&reply->dma[0], bytes, GFP_KERNEL);
	if (!*params) {
		ret = -ENOMEM;
		goto exit;
	}
	*num_params = reply->num_elems;

exit:
	kfree(reply);
	return ret;
}

/**
 * sof_probes_points_info - retrieve list of active probe points
 * @cdev:		SOF client device
 * @desc:	Returned list of active probes
 * @num_desc:	Returned count of active probes
 *
 * Host sends PROBE_POINT_INFO request to obtain list of active probe
 * points, valid for disconnection when given probe is no longer
 * required.
 */
static int sof_probes_points_info(struct sof_client_dev *cdev,
				  struct sof_probe_point_desc **desc,
				  size_t *num_desc)
{
	return sof_probes_info(cdev, SOF_IPC_PROBE_POINT_INFO,
			       (void **)desc, num_desc);
}

/**
 * sof_probes_points_add - connect specified probes
 * @cdev:		SOF client device
 * @desc:	List of probe points to connect
 * @num_desc:	Number of elements in @desc
 *
 * Dynamically connects to provided set of endpoints. Immediately
 * after connection is established, host must be prepared to
 * transfer data from or to target stream given the probing purpose.
 *
 * Each probe point should be removed using PROBE_POINT_REMOVE
 * request when no longer needed.
 */
static int sof_probes_points_add(struct sof_client_dev *cdev,
				 struct sof_probe_point_desc *desc,
				 size_t num_desc)
{
	struct sof_ipc_probe_point_add_params *msg;
	size_t size = struct_size(msg, desc, num_desc);
	struct sof_ipc_reply reply;
	int ret;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->hdr.size = size;
	msg->num_elems = num_desc;
	msg->hdr.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_POINT_ADD;
	memcpy(&msg->desc[0], desc, size - sizeof(*msg));

	ret = sof_client_ipc_tx_message(cdev, msg, &reply, sizeof(reply));
	kfree(msg);
	return ret;
}

/**
 * sof_probes_points_remove - disconnect specified probes
 * @cdev:		SOF client device
 * @buffer_id:		List of probe points to disconnect
 * @num_buffer_id:	Number of elements in @desc
 *
 * Removes previously connected probes from list of active probe
 * points and frees all resources on DSP side.
 */
static int sof_probes_points_remove(struct sof_client_dev *cdev,
				    unsigned int *buffer_id, size_t num_buffer_id)
{
	struct sof_ipc_probe_point_remove_params *msg;
	size_t size = struct_size(msg, buffer_id, num_buffer_id);
	struct sof_ipc_reply reply;
	int ret;

	msg = kmalloc(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	msg->hdr.size = size;
	msg->num_elems = num_buffer_id;
	msg->hdr.cmd = SOF_IPC_GLB_PROBE | SOF_IPC_PROBE_POINT_REMOVE;
	memcpy(&msg->buffer_id[0], buffer_id, size - sizeof(*msg));

	ret = sof_client_ipc_tx_message(cdev, msg, &reply, sizeof(reply));
	kfree(msg);
	return ret;
}

static int sof_probes_compr_startup(struct snd_compr_stream *cstream,
				    struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_component_get_drvdata(dai->component);
	struct sof_client_dev *cdev = snd_soc_card_get_drvdata(card);
	struct sof_probes_priv *priv = cdev->data;
	const struct sof_probes_host_ops *ops = priv->host_ops;
	int ret;

	if (sof_client_get_fw_state(cdev) == SOF_FW_CRASHED)
		return -ENODEV;

	ret = sof_client_core_module_get(cdev);
	if (ret)
		return ret;

	ret = ops->assign(cdev, cstream, dai, &priv->extractor_stream_tag);
	if (ret) {
		dev_err(dai->dev, "Failed to assign probe stream: %d\n", ret);
		priv->extractor_stream_tag = SOF_PROBES_INVALID_NODE_ID;
		sof_client_core_module_put(cdev);
	}

	return ret;
}

static int sof_probes_compr_shutdown(struct snd_compr_stream *cstream,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_component_get_drvdata(dai->component);
	struct sof_client_dev *cdev = snd_soc_card_get_drvdata(card);
	struct sof_probes_priv *priv = cdev->data;
	const struct sof_probes_host_ops *ops = priv->host_ops;
	struct sof_probe_point_desc *desc;
	size_t num_desc;
	int i, ret;

	/* disconnect all probe points */
	ret = sof_probes_points_info(cdev, &desc, &num_desc);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to get probe points: %d\n", ret);
		goto exit;
	}

	for (i = 0; i < num_desc; i++)
		sof_probes_points_remove(cdev, &desc[i].buffer_id, 1);
	kfree(desc);

exit:
	ret = sof_probes_deinit(cdev);
	if (ret < 0)
		dev_err(dai->dev, "Failed to deinit probe: %d\n", ret);

	priv->extractor_stream_tag = SOF_PROBES_INVALID_NODE_ID;
	snd_compr_free_pages(cstream);

	ret = ops->free(cdev, cstream, dai);

	sof_client_core_module_put(cdev);

	return ret;
}

static int sof_probes_compr_set_params(struct snd_compr_stream *cstream,
				       struct snd_compr_params *params,
				       struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_component_get_drvdata(dai->component);
	struct sof_client_dev *cdev = snd_soc_card_get_drvdata(card);
	struct snd_compr_runtime *rtd = cstream->runtime;
	struct sof_probes_priv *priv = cdev->data;
	const struct sof_probes_host_ops *ops = priv->host_ops;
	int ret;

	cstream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_SG;
	cstream->dma_buffer.dev.dev = sof_client_get_dma_dev(cdev);
	ret = snd_compr_malloc_pages(cstream, rtd->buffer_size);
	if (ret < 0)
		return ret;

	ret = ops->set_params(cdev, cstream, params, dai);
	if (ret)
		return ret;

	ret = sof_probes_init(cdev, priv->extractor_stream_tag, rtd->dma_bytes);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to init probe: %d\n", ret);
		return ret;
	}

	return 0;
}

static int sof_probes_compr_trigger(struct snd_compr_stream *cstream, int cmd,
				    struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_component_get_drvdata(dai->component);
	struct sof_client_dev *cdev = snd_soc_card_get_drvdata(card);
	struct sof_probes_priv *priv = cdev->data;
	const struct sof_probes_host_ops *ops = priv->host_ops;

	return ops->trigger(cdev, cstream, cmd, dai);
}

static int sof_probes_compr_pointer(struct snd_compr_stream *cstream,
				    struct snd_compr_tstamp *tstamp,
				    struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_component_get_drvdata(dai->component);
	struct sof_client_dev *cdev = snd_soc_card_get_drvdata(card);
	struct sof_probes_priv *priv = cdev->data;
	const struct sof_probes_host_ops *ops = priv->host_ops;

	return ops->pointer(cdev, cstream, tstamp, dai);
}

static const struct snd_soc_cdai_ops sof_probes_compr_ops = {
	.startup = sof_probes_compr_startup,
	.shutdown = sof_probes_compr_shutdown,
	.set_params = sof_probes_compr_set_params,
	.trigger = sof_probes_compr_trigger,
	.pointer = sof_probes_compr_pointer,
};

static int sof_probes_compr_copy(struct snd_soc_component *component,
				 struct snd_compr_stream *cstream,
				 char __user *buf, size_t count)
{
	struct snd_compr_runtime *rtd = cstream->runtime;
	unsigned int offset, n;
	void *ptr;
	int ret;

	if (count > rtd->buffer_size)
		count = rtd->buffer_size;

	div_u64_rem(rtd->total_bytes_transferred, rtd->buffer_size, &offset);
	ptr = rtd->dma_area + offset;
	n = rtd->buffer_size - offset;

	if (count < n) {
		ret = copy_to_user(buf, ptr, count);
	} else {
		ret = copy_to_user(buf, ptr, n);
		ret += copy_to_user(buf + n, rtd->dma_area, count - n);
	}

	if (ret)
		return count - ret;
	return count;
}

static const struct snd_compress_ops sof_probes_compressed_ops = {
	.copy = sof_probes_compr_copy,
};

/**
 * strsplit_u32 - Split string into sequence of u32 tokens
 * @buf:	String to split into tokens.
 * @delim:	String containing delimiter characters.
 * @tkns:	Returned u32 sequence pointer.
 * @num_tkns:	Returned number of tokens obtained.
 */
static int strsplit_u32(char *buf, const char *delim, u32 **tkns, size_t *num_tkns)
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

	while ((s = strsep(&buf, delim)) != NULL) {
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
	if (!(*tkns)) {
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
	ret = strsplit_u32(buf, ",", tkns, num_tkns);
exit:
	kfree(buf);
	return ret;
}

static ssize_t sof_probes_dfs_points_read(struct file *file, char __user *to,
					  size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_probes_priv *priv = cdev->data;
	struct device *dev = &cdev->auxdev.dev;
	struct sof_probe_point_desc *desc;
	int remaining, offset;
	size_t num_desc;
	char *buf;
	int i, ret, err;

	if (priv->extractor_stream_tag == SOF_PROBES_INVALID_NODE_ID) {
		dev_warn(dev, "no extractor stream running\n");
		return -ENOENT;
	}

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(dev, "debugfs read failed to resume %d\n", ret);
		goto exit;
	}

	ret = sof_probes_points_info(cdev, &desc, &num_desc);
	if (ret < 0)
		goto exit;

	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs read failed to idle %d\n", err);

	for (i = 0; i < num_desc; i++) {
		offset = strlen(buf);
		remaining = PAGE_SIZE - offset;
		ret = snprintf(buf + offset, remaining,
			       "Id: %#010x  Purpose: %u  Node id: %#x\n",
				desc[i].buffer_id, desc[i].purpose, desc[i].stream_tag);
		if (ret < 0 || ret >= remaining) {
			/* truncate the output buffer at the last full line */
			buf[offset] = '\0';
			break;
		}
	}

	ret = simple_read_from_buffer(to, count, ppos, buf, strlen(buf));

	kfree(desc);
exit:
	kfree(buf);
	return ret;
}

static ssize_t
sof_probes_dfs_points_write(struct file *file, const char __user *from,
			    size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_probes_priv *priv = cdev->data;
	struct device *dev = &cdev->auxdev.dev;
	struct sof_probe_point_desc *desc;
	size_t num_tkns, bytes;
	u32 *tkns;
	int ret, err;

	if (priv->extractor_stream_tag == SOF_PROBES_INVALID_NODE_ID) {
		dev_warn(dev, "no extractor stream running\n");
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

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(dev, "debugfs write failed to resume %d\n", ret);
		goto exit;
	}

	ret = sof_probes_points_add(cdev, desc, bytes / sizeof(*desc));
	if (!ret)
		ret = count;

	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs write failed to idle %d\n", err);
exit:
	kfree(tkns);
	return ret;
}

static const struct file_operations sof_probes_points_fops = {
	.open = simple_open,
	.read = sof_probes_dfs_points_read,
	.write = sof_probes_dfs_points_write,
	.llseek = default_llseek,

	.owner = THIS_MODULE,
};

static ssize_t
sof_probes_dfs_points_remove_write(struct file *file, const char __user *from,
				   size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_probes_priv *priv = cdev->data;
	struct device *dev = &cdev->auxdev.dev;
	size_t num_tkns;
	u32 *tkns;
	int ret, err;

	if (priv->extractor_stream_tag == SOF_PROBES_INVALID_NODE_ID) {
		dev_warn(dev, "no extractor stream running\n");
		return -ENOENT;
	}

	ret = tokenize_input(from, count, ppos, &tkns, &num_tkns);
	if (ret < 0)
		return ret;
	if (!num_tkns) {
		ret = -EINVAL;
		goto exit;
	}

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err_ratelimited(dev, "debugfs write failed to resume %d\n", ret);
		goto exit;
	}

	ret = sof_probes_points_remove(cdev, tkns, num_tkns);
	if (!ret)
		ret = count;

	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs write failed to idle %d\n", err);
exit:
	kfree(tkns);
	return ret;
}

static const struct file_operations sof_probes_points_remove_fops = {
	.open = simple_open,
	.write = sof_probes_dfs_points_remove_write,
	.llseek = default_llseek,

	.owner = THIS_MODULE,
};

static struct snd_soc_dai_driver sof_probes_dai_drv[] = {
{
	.name = "Probe Extraction CPU DAI",
	.compress_new = snd_soc_new_compress,
	.cops = &sof_probes_compr_ops,
	.capture = {
		.stream_name = "Probe Extraction",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_48000,
		.rate_min = 48000,
		.rate_max = 48000,
	},
},
};

static const struct snd_soc_component_driver sof_probes_component = {
	.name = "sof-probes-component",
	.compress_ops = &sof_probes_compressed_ops,
	.module_get_upon_open = 1,
};

SND_SOC_DAILINK_DEF(dummy, DAILINK_COMP_ARRAY(COMP_DUMMY()));

static int sof_probes_client_probe(struct auxiliary_device *auxdev,
				   const struct auxiliary_device_id *id)
{
	struct sof_client_dev *cdev = auxiliary_dev_to_sof_client_dev(auxdev);
	struct dentry *dfsroot = sof_client_get_debugfs_root(cdev);
	struct device *dev = &auxdev->dev;
	struct snd_soc_dai_link_component platform_component[] = {
		{
			.name = dev_name(dev),
		}
	};
	struct snd_soc_card *card;
	struct sof_probes_priv *priv;
	struct snd_soc_dai_link_component *cpus;
	struct sof_probes_host_ops *ops;
	struct snd_soc_dai_link *links;
	int ret;

	/* do not set up the probes support if it is not enabled */
	if (!sof_probes_enabled)
		return -ENXIO;

	if (!dev->platform_data) {
		dev_err(dev, "missing platform data\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ops = dev->platform_data;

	if (!ops->assign || !ops->free || !ops->set_params || !ops->trigger ||
	    !ops->pointer) {
		dev_err(dev, "missing platform callback(s)\n");
		return -ENODEV;
	}

	priv->host_ops = ops;
	cdev->data = priv;

	/* register probes component driver and dai */
	ret = devm_snd_soc_register_component(dev, &sof_probes_component,
					      sof_probes_dai_drv,
					      ARRAY_SIZE(sof_probes_dai_drv));
	if (ret < 0) {
		dev_err(dev, "failed to register SOF probes DAI driver %d\n", ret);
		return ret;
	}

	/* set client data */
	priv->extractor_stream_tag = SOF_PROBES_INVALID_NODE_ID;

	/* create read-write probes_points debugfs entry */
	priv->dfs_points = debugfs_create_file("probe_points", 0644, dfsroot,
					       cdev, &sof_probes_points_fops);

	/* create read-write probe_points_remove debugfs entry */
	priv->dfs_points_remove = debugfs_create_file("probe_points_remove", 0644,
						      dfsroot, cdev,
						      &sof_probes_points_remove_fops);

	links = devm_kcalloc(dev, SOF_PROBES_NUM_DAI_LINKS, sizeof(*links), GFP_KERNEL);
	cpus = devm_kcalloc(dev, SOF_PROBES_NUM_DAI_LINKS, sizeof(*cpus), GFP_KERNEL);
	if (!links || !cpus) {
		debugfs_remove(priv->dfs_points);
		debugfs_remove(priv->dfs_points_remove);
		return -ENOMEM;
	}

	/* extraction DAI link */
	links[0].name = "Compress Probe Capture";
	links[0].id = 0;
	links[0].cpus = &cpus[0];
	links[0].num_cpus = 1;
	links[0].cpus->dai_name = "Probe Extraction CPU DAI";
	links[0].codecs = dummy;
	links[0].num_codecs = 1;
	links[0].platforms = platform_component;
	links[0].num_platforms = ARRAY_SIZE(platform_component);
	links[0].nonatomic = 1;

	card = &priv->card;

	card->dev = dev;
	card->name = "sof-probes";
	card->owner = THIS_MODULE;
	card->num_links = SOF_PROBES_NUM_DAI_LINKS;
	card->dai_link = links;

	/* set idle_bias_off to prevent the core from resuming the card->dev */
	card->dapm.idle_bias_off = true;

	snd_soc_card_set_drvdata(card, cdev);

	ret = devm_snd_soc_register_card(dev, card);
	if (ret < 0) {
		debugfs_remove(priv->dfs_points);
		debugfs_remove(priv->dfs_points_remove);
		dev_err(dev, "Probes card register failed %d\n", ret);
		return ret;
	}

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(dev, SOF_PROBES_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_idle(dev);

	return 0;
}

static void sof_probes_client_remove(struct auxiliary_device *auxdev)
{
	struct sof_client_dev *cdev = auxiliary_dev_to_sof_client_dev(auxdev);
	struct sof_probes_priv *priv = cdev->data;

	if (!sof_probes_enabled)
		return;

	pm_runtime_disable(&auxdev->dev);
	debugfs_remove(priv->dfs_points);
	debugfs_remove(priv->dfs_points_remove);
}

static const struct auxiliary_device_id sof_probes_client_id_table[] = {
	{ .name = "snd_sof.hda-probes", },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, sof_probes_client_id_table);

/* driver name will be set based on KBUILD_MODNAME */
static struct auxiliary_driver sof_probes_client_drv = {
	.probe = sof_probes_client_probe,
	.remove = sof_probes_client_remove,

	.id_table = sof_probes_client_id_table,
};

module_auxiliary_driver(sof_probes_client_drv);

MODULE_DESCRIPTION("SOF Probes Client Driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);
