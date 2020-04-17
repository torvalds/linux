// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2019-2020 Intel Corporation. All rights reserved.
//
// Author: Cezary Rojewski <cezary.rojewski@intel.com>
//

#include <sound/soc.h>
#include "compress.h"
#include "ops.h"
#include "probe.h"

struct snd_compr_ops sof_probe_compressed_ops = {
	.copy		= sof_probe_compr_copy,
};
EXPORT_SYMBOL(sof_probe_compressed_ops);

int sof_probe_compr_open(struct snd_compr_stream *cstream,
		struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev =
				snd_soc_component_get_drvdata(dai->component);
	int ret;

	ret = snd_sof_probe_compr_assign(sdev, cstream, dai);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to assign probe stream: %d\n", ret);
		return ret;
	}

	sdev->extractor_stream_tag = ret;
	return 0;
}
EXPORT_SYMBOL(sof_probe_compr_open);

int sof_probe_compr_free(struct snd_compr_stream *cstream,
		struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev =
				snd_soc_component_get_drvdata(dai->component);
	struct sof_probe_point_desc *desc;
	size_t num_desc;
	int i, ret;

	/* disconnect all probe points */
	ret = sof_ipc_probe_points_info(sdev, &desc, &num_desc);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to get probe points: %d\n", ret);
		goto exit;
	}

	for (i = 0; i < num_desc; i++)
		sof_ipc_probe_points_remove(sdev, &desc[i].buffer_id, 1);
	kfree(desc);

exit:
	ret = sof_ipc_probe_deinit(sdev);
	if (ret < 0)
		dev_err(dai->dev, "Failed to deinit probe: %d\n", ret);

	sdev->extractor_stream_tag = SOF_PROBE_INVALID_NODE_ID;
	snd_compr_free_pages(cstream);

	return snd_sof_probe_compr_free(sdev, cstream, dai);
}
EXPORT_SYMBOL(sof_probe_compr_free);

int sof_probe_compr_set_params(struct snd_compr_stream *cstream,
		struct snd_compr_params *params, struct snd_soc_dai *dai)
{
	struct snd_compr_runtime *rtd = cstream->runtime;
	struct snd_sof_dev *sdev =
				snd_soc_component_get_drvdata(dai->component);
	int ret;

	cstream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_SG;
	cstream->dma_buffer.dev.dev = sdev->dev;
	ret = snd_compr_malloc_pages(cstream, rtd->buffer_size);
	if (ret < 0)
		return ret;

	ret = snd_sof_probe_compr_set_params(sdev, cstream, params, dai);
	if (ret < 0)
		return ret;

	ret = sof_ipc_probe_init(sdev, sdev->extractor_stream_tag,
				 rtd->dma_bytes);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to init probe: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(sof_probe_compr_set_params);

int sof_probe_compr_trigger(struct snd_compr_stream *cstream, int cmd,
		struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev =
				snd_soc_component_get_drvdata(dai->component);

	return snd_sof_probe_compr_trigger(sdev, cstream, cmd, dai);
}
EXPORT_SYMBOL(sof_probe_compr_trigger);

int sof_probe_compr_pointer(struct snd_compr_stream *cstream,
		struct snd_compr_tstamp *tstamp, struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev =
				snd_soc_component_get_drvdata(dai->component);

	return snd_sof_probe_compr_pointer(sdev, cstream, tstamp, dai);
}
EXPORT_SYMBOL(sof_probe_compr_pointer);

int sof_probe_compr_copy(struct snd_compr_stream *cstream,
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
EXPORT_SYMBOL(sof_probe_compr_copy);
