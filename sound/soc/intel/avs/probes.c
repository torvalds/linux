// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021-2022 Intel Corporation
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <sound/compress_driver.h>
#include <sound/hdaudio_ext.h>
#include <sound/hdaudio.h>
#include <sound/soc.h>
#include "avs.h"
#include "messages.h"

static int avs_dsp_init_probe(struct avs_dev *adev, union avs_connector_node_id node_id,
			      size_t buffer_size)
{
	struct avs_probe_cfg cfg = {{0}};
	struct avs_module_entry mentry;
	u8 dummy;
	int ret;

	ret = avs_get_module_entry(adev, &AVS_PROBE_MOD_UUID, &mentry);
	if (ret)
		return ret;

	/*
	 * Probe module uses no cycles, audio data format and input and output
	 * frame sizes are unused. It is also not owned by any pipeline.
	 */
	cfg.base.ibs = 1;
	/* BSS module descriptor is always segment of index=2. */
	cfg.base.is_pages = mentry.segments[2].flags.length;
	cfg.gtw_cfg.node_id = node_id;
	cfg.gtw_cfg.dma_buffer_size = buffer_size;

	return avs_dsp_init_module(adev, mentry.module_id, INVALID_PIPELINE_ID, 0, 0, &cfg,
				   sizeof(cfg), &dummy);
}

static void avs_dsp_delete_probe(struct avs_dev *adev)
{
	struct avs_module_entry mentry;
	int ret;

	ret = avs_get_module_entry(adev, &AVS_PROBE_MOD_UUID, &mentry);
	if (!ret)
		/* There is only ever one probe module instance. */
		avs_dsp_delete_module(adev, mentry.module_id, 0, INVALID_PIPELINE_ID, 0);
}

static inline struct hdac_ext_stream *avs_compr_get_host_stream(struct snd_compr_stream *cstream)
{
	return cstream->runtime->private_data;
}

static int avs_probe_compr_open(struct snd_compr_stream *cstream, struct snd_soc_dai *dai)
{
	struct avs_dev *adev = to_avs_dev(dai->dev);
	struct hdac_bus *bus = &adev->base.core;
	struct hdac_ext_stream *host_stream;

	if (adev->extractor) {
		dev_err(dai->dev, "Cannot open more than one extractor stream\n");
		return -EEXIST;
	}

	host_stream = snd_hdac_ext_cstream_assign(bus, cstream);
	if (!host_stream) {
		dev_err(dai->dev, "Failed to assign HDAudio stream for extraction\n");
		return -EBUSY;
	}

	adev->extractor = host_stream;
	hdac_stream(host_stream)->curr_pos = 0;
	cstream->runtime->private_data = host_stream;

	return 0;
}

static int avs_probe_compr_free(struct snd_compr_stream *cstream, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *host_stream = avs_compr_get_host_stream(cstream);
	struct avs_dev *adev = to_avs_dev(dai->dev);
	struct avs_probe_point_desc *desc;
	/* Extractor node identifier. */
	unsigned int vindex = INVALID_NODE_ID.vindex;
	size_t num_desc;
	int i, ret;

	/* Disconnect all probe points. */
	ret = avs_ipc_probe_get_points(adev, &desc, &num_desc);
	if (ret) {
		dev_err(dai->dev, "get probe points failed: %d\n", ret);
		ret = AVS_IPC_RET(ret);
		goto exit;
	}

	for (i = 0; i < num_desc; i++)
		if (desc[i].node_id.vindex == vindex)
			avs_ipc_probe_disconnect_points(adev, &desc[i].id, 1);
	kfree(desc);

exit:
	if (adev->num_probe_streams) {
		adev->num_probe_streams--;
		if (!adev->num_probe_streams) {
			avs_dsp_delete_probe(adev);
			avs_dsp_enable_d0ix(adev);
		}
	}

	snd_hdac_stream_cleanup(hdac_stream(host_stream));
	hdac_stream(host_stream)->prepared = 0;
	snd_hdac_ext_stream_release(host_stream, HDAC_EXT_STREAM_TYPE_HOST);

	snd_compr_free_pages(cstream);
	adev->extractor = NULL;

	return ret;
}

static int avs_probe_compr_set_params(struct snd_compr_stream *cstream,
				      struct snd_compr_params *params, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *host_stream = avs_compr_get_host_stream(cstream);
	struct snd_compr_runtime *rtd = cstream->runtime;
	struct avs_dev *adev = to_avs_dev(dai->dev);
	/* compr params do not store bit depth, default to S32_LE. */
	snd_pcm_format_t format = SNDRV_PCM_FORMAT_S32_LE;
	unsigned int format_val;
	int bps, ret;

	hdac_stream(host_stream)->bufsize = 0;
	hdac_stream(host_stream)->period_bytes = 0;
	hdac_stream(host_stream)->format_val = 0;
	cstream->dma_buffer.dev.type = SNDRV_DMA_TYPE_DEV_SG;
	cstream->dma_buffer.dev.dev = adev->dev;

	ret = snd_compr_malloc_pages(cstream, rtd->buffer_size);
	if (ret < 0)
		return ret;
	bps = snd_pcm_format_physical_width(format);
	if (bps < 0)
		return bps;
	format_val = snd_hdac_stream_format(params->codec.ch_out, bps, params->codec.sample_rate);
	ret = snd_hdac_stream_set_params(hdac_stream(host_stream), format_val);
	if (ret < 0)
		return ret;
	ret = snd_hdac_stream_setup(hdac_stream(host_stream), false);
	if (ret < 0)
		return ret;

	hdac_stream(host_stream)->prepared = 1;

	if (!adev->num_probe_streams) {
		union avs_connector_node_id node_id;

		/* D0ix not allowed during probing. */
		ret = avs_dsp_disable_d0ix(adev);
		if (ret)
			return ret;

		node_id.vindex = hdac_stream(host_stream)->stream_tag - 1;
		node_id.dma_type = AVS_DMA_HDA_HOST_INPUT;

		ret = avs_dsp_init_probe(adev, node_id, rtd->dma_bytes);
		if (ret < 0) {
			dev_err(dai->dev, "probe init failed: %d\n", ret);
			avs_dsp_enable_d0ix(adev);
			return ret;
		}
	}

	adev->num_probe_streams++;
	return 0;
}

static int avs_probe_compr_trigger(struct snd_compr_stream *cstream, int cmd,
				   struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *host_stream = avs_compr_get_host_stream(cstream);
	struct avs_dev *adev = to_avs_dev(dai->dev);
	struct hdac_bus *bus = &adev->base.core;
	unsigned long cookie;

	if (!hdac_stream(host_stream)->prepared)
		return -EPIPE;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		spin_lock_irqsave(&bus->reg_lock, cookie);
		snd_hdac_stream_start(hdac_stream(host_stream));
		spin_unlock_irqrestore(&bus->reg_lock, cookie);
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
		spin_lock_irqsave(&bus->reg_lock, cookie);
		snd_hdac_stream_stop(hdac_stream(host_stream));
		spin_unlock_irqrestore(&bus->reg_lock, cookie);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int avs_probe_compr_pointer(struct snd_compr_stream *cstream,
				   struct snd_compr_tstamp *tstamp, struct snd_soc_dai *dai)
{
	struct hdac_ext_stream *host_stream = avs_compr_get_host_stream(cstream);
	struct snd_soc_pcm_stream *pstream;

	pstream = &dai->driver->capture;
	tstamp->copied_total = hdac_stream(host_stream)->curr_pos;
	tstamp->sampling_rate = snd_pcm_rate_bit_to_rate(pstream->rates);

	return 0;
}

static int avs_probe_compr_copy(struct snd_soc_component *comp, struct snd_compr_stream *cstream,
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

static const struct snd_soc_cdai_ops avs_probe_cdai_ops = {
	.startup = avs_probe_compr_open,
	.shutdown = avs_probe_compr_free,
	.set_params = avs_probe_compr_set_params,
	.trigger = avs_probe_compr_trigger,
	.pointer = avs_probe_compr_pointer,
};

static const struct snd_soc_dai_ops avs_probe_dai_ops = {
	.compress_new = snd_soc_new_compress,
};

static const struct snd_compress_ops avs_probe_compress_ops = {
	.copy = avs_probe_compr_copy,
};

static struct snd_soc_dai_driver probe_cpu_dais[] = {
{
	.name = "Probe Extraction CPU DAI",
	.cops = &avs_probe_cdai_ops,
	.ops  = &avs_probe_dai_ops,
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

static const struct snd_soc_component_driver avs_probe_component_driver = {
	.name			= "avs-probe-compr",
	.compress_ops		= &avs_probe_compress_ops,
	.module_get_upon_open	= 1, /* increment refcount when a stream is opened */
};

int avs_probe_platform_register(struct avs_dev *adev, const char *name)
{
	return avs_soc_component_register(adev->dev, name, &avs_probe_component_driver,
					  probe_cpu_dais, ARRAY_SIZE(probe_cpu_dais));
}
