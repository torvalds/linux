/*
 * linux/sound/arm/ep93xx-pcm.c - EP93xx ALSA PCM interface
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 * Copyright (C) 2006 Applied Data Systems
 *
 * Rewritten for the SoC audio subsystem (Based on PXA2xx code):
 *   Copyright (c) 2008 Ryan Mallon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include <linux/platform_data/dma-ep93xx.h>
#include <mach/hardware.h>
#include <mach/ep93xx-regs.h>

#include "ep93xx-pcm.h"

static const struct snd_pcm_hardware ep93xx_pcm_hardware = {
	.info			= (SNDRV_PCM_INFO_MMAP		|
				   SNDRV_PCM_INFO_MMAP_VALID	|
				   SNDRV_PCM_INFO_INTERLEAVED	|
				   SNDRV_PCM_INFO_BLOCK_TRANSFER),
				   
	.rates			= SNDRV_PCM_RATE_8000_192000,
	.rate_min		= SNDRV_PCM_RATE_8000,
	.rate_max		= SNDRV_PCM_RATE_192000,
	
	.formats		= (SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S32_LE),
	
	.buffer_bytes_max	= 131072,
	.period_bytes_min	= 32,
	.period_bytes_max	= 32768,
	.periods_min		= 1,
	.periods_max		= 32,
	.fifo_size		= 32,
};

static bool ep93xx_pcm_dma_filter(struct dma_chan *chan, void *filter_param)
{
	struct ep93xx_dma_data *data = filter_param;

	if (data->direction == ep93xx_dma_chan_direction(chan)) {
		chan->private = data;
		return true;
	}

	return false;
}

static int ep93xx_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct ep93xx_pcm_dma_params *dma_params;
	struct ep93xx_dma_data *dma_data;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &ep93xx_pcm_hardware);

	dma_data = kmalloc(sizeof(*dma_data), GFP_KERNEL);
	if (!dma_data)
		return -ENOMEM;

	dma_params = snd_soc_dai_get_dma_data(cpu_dai, substream);
	dma_data->port = dma_params->dma_port;
	dma_data->name = dma_params->name;
	dma_data->direction = snd_pcm_substream_to_dma_direction(substream);

	ret = snd_dmaengine_pcm_open(substream, ep93xx_pcm_dma_filter, dma_data);
	if (ret) {
		kfree(dma_data);
		return ret;
	}

	snd_dmaengine_pcm_set_data(substream, dma_data);

	return 0;
}

static int ep93xx_pcm_close(struct snd_pcm_substream *substream)
{
	struct dma_data *dma_data = snd_dmaengine_pcm_get_data(substream);

	snd_dmaengine_pcm_close(substream);
	kfree(dma_data);
	return 0;
}

static int ep93xx_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int ep93xx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static int ep93xx_pcm_mmap(struct snd_pcm_substream *substream,
			   struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops ep93xx_pcm_ops = {
	.open		= ep93xx_pcm_open,
	.close		= ep93xx_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= ep93xx_pcm_hw_params,
	.hw_free	= ep93xx_pcm_hw_free,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= snd_dmaengine_pcm_pointer_no_residue,
	.mmap		= ep93xx_pcm_mmap,
};

static int ep93xx_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = ep93xx_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);
	buf->bytes = size;

	return (buf->area == NULL) ? -ENOMEM : 0;
}

static void ep93xx_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {		
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;
		
		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_writecombine(pcm->card->dev, buf->bytes, buf->area,
				      buf->addr);
		buf->area = NULL;
	}
}

static u64 ep93xx_pcm_dmamask = DMA_BIT_MASK(32);

static int ep93xx_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &ep93xx_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = ep93xx_pcm_preallocate_dma_buffer(pcm,
					SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			return ret;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = ep93xx_pcm_preallocate_dma_buffer(pcm,
					SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			return ret;
	}

	return 0;
}

static struct snd_soc_platform_driver ep93xx_soc_platform = {
	.ops		= &ep93xx_pcm_ops,
	.pcm_new	= &ep93xx_pcm_new,
	.pcm_free	= &ep93xx_pcm_free_dma_buffers,
};

static int __devinit ep93xx_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &ep93xx_soc_platform);
}

static int __devexit ep93xx_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver ep93xx_pcm_driver = {
	.driver = {
			.name = "ep93xx-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = ep93xx_soc_platform_probe,
	.remove = __devexit_p(ep93xx_soc_platform_remove),
};

module_platform_driver(ep93xx_pcm_driver);

MODULE_AUTHOR("Ryan Mallon");
MODULE_DESCRIPTION("EP93xx ALSA PCM interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ep93xx-pcm-audio");
