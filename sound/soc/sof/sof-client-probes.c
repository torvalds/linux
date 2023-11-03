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
#include <linux/string_helpers.h>
#include <linux/stddef.h>

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

	ret = ops->startup(cdev, cstream, dai, &priv->extractor_stream_tag);
	if (ret) {
		dev_err(dai->dev, "Failed to startup probe stream: %d\n", ret);
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
	const struct sof_probes_ipc_ops *ipc = priv->ipc_ops;
	struct sof_probe_point_desc *desc;
	size_t num_desc;
	int i, ret;

	/* disconnect all probe points */
	ret = ipc->points_info(cdev, &desc, &num_desc);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to get probe points: %d\n", ret);
		goto exit;
	}

	for (i = 0; i < num_desc; i++)
		ipc->points_remove(cdev, &desc[i].buffer_id, 1);
	kfree(desc);

exit:
	ret = ipc->deinit(cdev);
	if (ret < 0)
		dev_err(dai->dev, "Failed to deinit probe: %d\n", ret);

	priv->extractor_stream_tag = SOF_PROBES_INVALID_NODE_ID;
	snd_compr_free_pages(cstream);

	ret = ops->shutdown(cdev, cstream, dai);

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
	const struct sof_probes_ipc_ops *ipc = priv->ipc_ops;
	int ret;

	cstream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_SG;
	cstream->dma_buffer.dev.dev = sof_client_get_dma_dev(cdev);
	ret = snd_compr_malloc_pages(cstream, rtd->buffer_size);
	if (ret < 0)
		return ret;

	ret = ops->set_params(cdev, cstream, params, dai);
	if (ret)
		return ret;

	ret = ipc->init(cdev, priv->extractor_stream_tag, rtd->dma_bytes);
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

static ssize_t sof_probes_dfs_points_read(struct file *file, char __user *to,
					  size_t count, loff_t *ppos)
{
	struct sof_client_dev *cdev = file->private_data;
	struct sof_probes_priv *priv = cdev->data;
	struct device *dev = &cdev->auxdev.dev;
	struct sof_probe_point_desc *desc;
	const struct sof_probes_ipc_ops *ipc = priv->ipc_ops;
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

	ret = ipc->points_info(cdev, &desc, &num_desc);
	if (ret < 0)
		goto pm_error;

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

pm_error:
	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs read failed to idle %d\n", err);

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
	const struct sof_probes_ipc_ops *ipc = priv->ipc_ops;
	struct device *dev = &cdev->auxdev.dev;
	struct sof_probe_point_desc *desc;
	u32 num_elems, *array;
	size_t bytes;
	int ret, err;

	if (priv->extractor_stream_tag == SOF_PROBES_INVALID_NODE_ID) {
		dev_warn(dev, "no extractor stream running\n");
		return -ENOENT;
	}

	ret = parse_int_array_user(from, count, (int **)&array);
	if (ret < 0)
		return ret;

	num_elems = *array;
	bytes = sizeof(*array) * num_elems;
	if (bytes % sizeof(*desc)) {
		ret = -EINVAL;
		goto exit;
	}

	desc = (struct sof_probe_point_desc *)&array[1];

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(dev, "debugfs write failed to resume %d\n", ret);
		goto exit;
	}

	ret = ipc->points_add(cdev, desc, bytes / sizeof(*desc));
	if (!ret)
		ret = count;

	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs write failed to idle %d\n", err);
exit:
	kfree(array);
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
	const struct sof_probes_ipc_ops *ipc = priv->ipc_ops;
	struct device *dev = &cdev->auxdev.dev;
	int ret, err;
	u32 *array;

	if (priv->extractor_stream_tag == SOF_PROBES_INVALID_NODE_ID) {
		dev_warn(dev, "no extractor stream running\n");
		return -ENOENT;
	}

	ret = parse_int_array_user(from, count, (int **)&array);
	if (ret < 0)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0) {
		dev_err_ratelimited(dev, "debugfs write failed to resume %d\n", ret);
		goto exit;
	}

	ret = ipc->points_remove(cdev, &array[1], array[0]);
	if (!ret)
		ret = count;

	pm_runtime_mark_last_busy(dev);
	err = pm_runtime_put_autosuspend(dev);
	if (err < 0)
		dev_err_ratelimited(dev, "debugfs write failed to idle %d\n", err);
exit:
	kfree(array);
	return ret;
}

static const struct file_operations sof_probes_points_remove_fops = {
	.open = simple_open,
	.write = sof_probes_dfs_points_remove_write,
	.llseek = default_llseek,

	.owner = THIS_MODULE,
};

static const struct snd_soc_dai_ops sof_probes_dai_ops = {
	.compress_new = snd_soc_new_compress,
};

static struct snd_soc_dai_driver sof_probes_dai_drv[] = {
{
	.name = "Probe Extraction CPU DAI",
	.ops  = &sof_probes_dai_ops,
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
	.legacy_dai_naming = 1,
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

	ops = dev_get_platdata(dev);
	if (!ops) {
		dev_err(dev, "missing platform data\n");
		return -ENODEV;
	}
	if (!ops->startup || !ops->shutdown || !ops->set_params || !ops->trigger ||
	    !ops->pointer) {
		dev_err(dev, "missing platform callback(s)\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->host_ops = ops;

	switch (sof_client_get_ipc_type(cdev)) {
#ifdef CONFIG_SND_SOC_SOF_IPC4
	case SOF_IPC_TYPE_4:
		priv->ipc_ops = &ipc4_probe_ops;
		break;
#endif
#ifdef CONFIG_SND_SOC_SOF_IPC3
	case SOF_IPC_TYPE_3:
		priv->ipc_ops = &ipc3_probe_ops;
		break;
#endif
	default:
		dev_err(dev, "Matching IPC ops not found.");
		return -ENODEV;
	}

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
	{ .name = "snd_sof.acp-probes", },
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
