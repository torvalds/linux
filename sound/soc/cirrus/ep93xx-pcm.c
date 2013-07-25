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
#include <linux/platform_device.h>
#include <linux/dmaengine.h>

#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include <linux/platform_data/dma-ep93xx.h>

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

static const struct snd_dmaengine_pcm_config ep93xx_dmaengine_pcm_config = {
	.pcm_hardware = &ep93xx_pcm_hardware,
	.compat_filter_fn = ep93xx_pcm_dma_filter,
	.prealloc_buffer_size = 131072,
};

static int ep93xx_soc_platform_probe(struct platform_device *pdev)
{
	return snd_dmaengine_pcm_register(&pdev->dev,
		&ep93xx_dmaengine_pcm_config,
		SND_DMAENGINE_PCM_FLAG_NO_RESIDUE |
		SND_DMAENGINE_PCM_FLAG_NO_DT |
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}

static int ep93xx_soc_platform_remove(struct platform_device *pdev)
{
	snd_dmaengine_pcm_unregister(&pdev->dev);
	return 0;
}

static struct platform_driver ep93xx_pcm_driver = {
	.driver = {
			.name = "ep93xx-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = ep93xx_soc_platform_probe,
	.remove = ep93xx_soc_platform_remove,
};

module_platform_driver(ep93xx_pcm_driver);

MODULE_AUTHOR("Ryan Mallon");
MODULE_DESCRIPTION("EP93xx ALSA PCM interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ep93xx-pcm-audio");
