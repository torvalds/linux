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
#include <linux/platform_data/dma-s3c24xx.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "dma.h"

void samsung_asoc_init_dma_data(struct snd_soc_dai *dai,
				struct s3c_dma_params *playback,
				struct s3c_dma_params *capture)
{
	struct snd_dmaengine_dai_dma_data *playback_data = NULL;
	struct snd_dmaengine_dai_dma_data *capture_data = NULL;

	if (playback) {
		playback_data = &playback->dma_data;
		playback_data->filter_data = playback->slave;
		playback_data->chan_name = playback->ch_name;
		playback_data->addr = playback->dma_addr;
		playback_data->addr_width = playback->dma_size;
	}
	if (capture) {
		capture_data = &capture->dma_data;
		capture_data->filter_data = capture->slave;
		capture_data->chan_name = capture->ch_name;
		capture_data->addr = capture->dma_addr;
		capture_data->addr_width = capture->dma_size;
	}

	snd_soc_dai_init_dma_data(dai, playback_data, capture_data);
}
EXPORT_SYMBOL_GPL(samsung_asoc_init_dma_data);

int samsung_asoc_dma_platform_register(struct device *dev, dma_filter_fn filter,
				       const char *tx, const char *rx)
{
	unsigned int flags = SND_DMAENGINE_PCM_FLAG_COMPAT;

	struct snd_dmaengine_pcm_config *pcm_conf;

	pcm_conf = devm_kzalloc(dev, sizeof(*pcm_conf), GFP_KERNEL);
	if (!pcm_conf)
		return -ENOMEM;

	pcm_conf->prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config;
	pcm_conf->compat_filter_fn = filter;

	if (dev->of_node) {
		pcm_conf->chan_names[SNDRV_PCM_STREAM_PLAYBACK] = tx;
		pcm_conf->chan_names[SNDRV_PCM_STREAM_CAPTURE] = rx;
	} else {
		flags |= SND_DMAENGINE_PCM_FLAG_CUSTOM_CHANNEL_NAME;
	}

	return devm_snd_dmaengine_pcm_register(dev, pcm_conf, flags);
}
EXPORT_SYMBOL_GPL(samsung_asoc_dma_platform_register);

MODULE_AUTHOR("Mark Brown <broonie@linaro.org>");
MODULE_DESCRIPTION("Samsung dmaengine ASoC driver");
MODULE_LICENSE("GPL");
