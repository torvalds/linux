// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra_pcm.c - Tegra PCM driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010,2012 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 * Scott Peterson <speterson@nvidia.com>
 * Vijay Mali <vmali@nvidia.com>
 *
 * Copyright (C) 2010 Google, Inc.
 * Iliyan Malchev <malchev@google.com>
 */

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include "tegra_pcm.h"

static const struct snd_pcm_hardware tegra_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.period_bytes_min	= 1024,
	.period_bytes_max	= PAGE_SIZE,
	.periods_min		= 2,
	.periods_max		= 8,
	.buffer_bytes_max	= PAGE_SIZE * 8,
	.fifo_size		= 4,
};

static const struct snd_dmaengine_pcm_config tegra_dmaengine_pcm_config = {
	.pcm_hardware = &tegra_pcm_hardware,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.prealloc_buffer_size = PAGE_SIZE * 8,
};

int tegra_pcm_platform_register(struct device *dev)
{
	return snd_dmaengine_pcm_register(dev, &tegra_dmaengine_pcm_config, 0);
}
EXPORT_SYMBOL_GPL(tegra_pcm_platform_register);

int tegra_pcm_platform_register_with_chan_names(struct device *dev,
				struct snd_dmaengine_pcm_config *config,
				char *txdmachan, char *rxdmachan)
{
	*config = tegra_dmaengine_pcm_config;
	config->dma_dev = dev->parent;
	config->chan_names[0] = txdmachan;
	config->chan_names[1] = rxdmachan;

	return snd_dmaengine_pcm_register(dev, config, 0);
}
EXPORT_SYMBOL_GPL(tegra_pcm_platform_register_with_chan_names);

void tegra_pcm_platform_unregister(struct device *dev)
{
	return snd_dmaengine_pcm_unregister(dev);
}
EXPORT_SYMBOL_GPL(tegra_pcm_platform_unregister);

int tegra_pcm_open(struct snd_soc_component *component,
		   struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_dmaengine_dai_dma_data *dmap;
	struct dma_chan *chan;
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int ret;

	if (rtd->dai_link->no_pcm)
		return 0;

	dmap = snd_soc_dai_get_dma_data(cpu_dai, substream);

	/* Set HW params now that initialization is complete */
	snd_soc_set_runtime_hwparams(substream, &tegra_pcm_hardware);

	/* Ensure period size is multiple of 8 */
	ret = snd_pcm_hw_constraint_step(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 0x8);
	if (ret) {
		dev_err(rtd->dev, "failed to set constraint %d\n", ret);
		return ret;
	}

	chan = dma_request_slave_channel(cpu_dai->dev, dmap->chan_name);
	if (!chan) {
		dev_err(cpu_dai->dev,
			"dmaengine request slave channel failed! (%s)\n",
			dmap->chan_name);
		return -ENODEV;
	}

	ret = snd_dmaengine_pcm_open(substream, chan);
	if (ret) {
		dev_err(rtd->dev,
			"dmaengine pcm open failed with err %d (%s)\n", ret,
			dmap->chan_name);

		dma_release_channel(chan);

		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_pcm_open);

int tegra_pcm_close(struct snd_soc_component *component,
		    struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	if (rtd->dai_link->no_pcm)
		return 0;

	snd_dmaengine_pcm_close_release_chan(substream);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_pcm_close);

int tegra_pcm_hw_params(struct snd_soc_component *component,
			struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_dmaengine_dai_dma_data *dmap;
	struct dma_slave_config slave_config;
	struct dma_chan *chan;
	int ret;

	if (rtd->dai_link->no_pcm)
		return 0;

	dmap = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);
	if (!dmap)
		return 0;

	chan = snd_dmaengine_pcm_get_chan(substream);

	ret = snd_hwparams_to_dma_slave_config(substream, params,
					       &slave_config);
	if (ret) {
		dev_err(rtd->dev, "hw params config failed with err %d\n", ret);
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		slave_config.dst_addr = dmap->addr;
		slave_config.dst_maxburst = 8;
	} else {
		slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		slave_config.src_addr = dmap->addr;
		slave_config.src_maxburst = 8;
	}

	ret = dmaengine_slave_config(chan, &slave_config);
	if (ret < 0) {
		dev_err(rtd->dev, "dma slave config failed with err %d\n", ret);
		return ret;
	}

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_pcm_hw_params);

int tegra_pcm_hw_free(struct snd_soc_component *component,
		      struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	if (rtd->dai_link->no_pcm)
		return 0;

	snd_pcm_set_runtime_buffer(substream, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_pcm_hw_free);

int tegra_pcm_mmap(struct snd_soc_component *component,
		   struct snd_pcm_substream *substream,
		   struct vm_area_struct *vma)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (rtd->dai_link->no_pcm)
		return 0;

	return dma_mmap_wc(substream->pcm->card->dev, vma, runtime->dma_area,
			   runtime->dma_addr, runtime->dma_bytes);
}
EXPORT_SYMBOL_GPL(tegra_pcm_mmap);

snd_pcm_uframes_t tegra_pcm_pointer(struct snd_soc_component *component,
				    struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_pointer(substream);
}
EXPORT_SYMBOL_GPL(tegra_pcm_pointer);

static int tegra_pcm_preallocate_dma_buffer(struct device *dev, struct snd_pcm *pcm, int stream,
					    size_t size)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	buf->area = dma_alloc_wc(dev, size, &buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;

	buf->private_data = NULL;
	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = dev;
	buf->bytes = size;

	return 0;
}

static void tegra_pcm_deallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;

	substream = pcm->streams[stream].substream;
	if (!substream)
		return;

	buf = &substream->dma_buffer;
	if (!buf->area)
		return;

	dma_free_wc(buf->dev.dev, buf->bytes, buf->area, buf->addr);
	buf->area = NULL;
}

static int tegra_pcm_dma_allocate(struct device *dev, struct snd_soc_pcm_runtime *rtd,
				  size_t size)
{
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret < 0)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = tegra_pcm_preallocate_dma_buffer(dev, pcm, SNDRV_PCM_STREAM_PLAYBACK, size);
		if (ret)
			goto err;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = tegra_pcm_preallocate_dma_buffer(dev, pcm, SNDRV_PCM_STREAM_CAPTURE, size);
		if (ret)
			goto err_free_play;
	}

	return 0;

err_free_play:
	tegra_pcm_deallocate_dma_buffer(pcm, SNDRV_PCM_STREAM_PLAYBACK);
err:
	return ret;
}

int tegra_pcm_construct(struct snd_soc_component *component,
			struct snd_soc_pcm_runtime *rtd)
{
	struct device *dev = component->dev;

	/*
	 * Fallback for backwards-compatibility with older device trees that
	 * have the iommus property in the virtual, top-level "sound" node.
	 */
	if (!of_get_property(dev->of_node, "iommus", NULL))
		dev = rtd->card->snd_card->dev;

	return tegra_pcm_dma_allocate(dev, rtd, tegra_pcm_hardware.buffer_bytes_max);
}
EXPORT_SYMBOL_GPL(tegra_pcm_construct);

void tegra_pcm_destruct(struct snd_soc_component *component,
			struct snd_pcm *pcm)
{
	tegra_pcm_deallocate_dma_buffer(pcm, SNDRV_PCM_STREAM_CAPTURE);
	tegra_pcm_deallocate_dma_buffer(pcm, SNDRV_PCM_STREAM_PLAYBACK);
}
EXPORT_SYMBOL_GPL(tegra_pcm_destruct);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra PCM ASoC driver");
MODULE_LICENSE("GPL");
