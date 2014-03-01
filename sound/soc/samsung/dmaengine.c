/*
 * dmaengine.c - Samsung dmaengine wrapper
 *
 * Author: Mark Brown <broonie@linaro.org>
 * Copyright 2013 Linaro
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/amba/pl08x.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "dma.h"

#ifdef CONFIG_ARCH_S3C64XX
#define filter_fn pl08x_filter_id
#else
#define filter_fn NULL
#endif

static const struct snd_dmaengine_pcm_config samsung_dmaengine_pcm_config = {
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.compat_filter_fn = filter_fn,
};

void samsung_asoc_init_dma_data(struct snd_soc_dai *dai,
				struct s3c_dma_params *playback,
				struct s3c_dma_params *capture)
{
	struct snd_dmaengine_dai_dma_data *playback_data = NULL;
	struct snd_dmaengine_dai_dma_data *capture_data = NULL;

	if (playback) {
		playback_data = &playback->dma_data;
		playback_data->filter_data = (void *)playback->channel;
		playback_data->chan_name = playback->ch_name;
		playback_data->addr = playback->dma_addr;
		playback_data->addr_width = playback->dma_size;
	}
	if (capture) {
		capture_data = &capture->dma_data;
		capture_data->filter_data = (void *)capture->channel;
		capture_data->chan_name = capture->ch_name;
		capture_data->addr = capture->dma_addr;
		capture_data->addr_width = capture->dma_size;
	}

	snd_soc_dai_init_dma_data(dai, playback_data, capture_data);
}
EXPORT_SYMBOL_GPL(samsung_asoc_init_dma_data);

int samsung_asoc_dma_platform_register(struct device *dev)
{
	return snd_dmaengine_pcm_register(dev, &samsung_dmaengine_pcm_config,
					  SND_DMAENGINE_PCM_FLAG_CUSTOM_CHANNEL_NAME |
					  SND_DMAENGINE_PCM_FLAG_COMPAT);
}
EXPORT_SYMBOL_GPL(samsung_asoc_dma_platform_register);

void samsung_asoc_dma_platform_unregister(struct device *dev)
{
	return snd_dmaengine_pcm_unregister(dev);
}
EXPORT_SYMBOL_GPL(samsung_asoc_dma_platform_unregister);

MODULE_AUTHOR("Mark Brown <broonie@linaro.org>");
MODULE_DESCRIPTION("Samsung dmaengine ASoC driver");
MODULE_LICENSE("GPL");
