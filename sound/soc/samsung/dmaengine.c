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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/soc.h>

#include "dma.h"

int samsung_asoc_dma_platform_register(struct device *dev, dma_filter_fn filter,
				       const char *tx, const char *rx,
				       struct device *dma_dev)
{
	struct snd_dmaengine_pcm_config *pcm_conf;

	pcm_conf = devm_kzalloc(dev, sizeof(*pcm_conf), GFP_KERNEL);
	if (!pcm_conf)
		return -ENOMEM;

	pcm_conf->prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config;
	pcm_conf->compat_filter_fn = filter;
	pcm_conf->dma_dev = dma_dev;

	pcm_conf->chan_names[SNDRV_PCM_STREAM_PLAYBACK] = tx;
	pcm_conf->chan_names[SNDRV_PCM_STREAM_CAPTURE] = rx;

	return devm_snd_dmaengine_pcm_register(dev, pcm_conf,
				SND_DMAENGINE_PCM_FLAG_COMPAT);
}
EXPORT_SYMBOL_GPL(samsung_asoc_dma_platform_register);

MODULE_AUTHOR("Mark Brown <broonie@linaro.org>");
MODULE_DESCRIPTION("Samsung dmaengine ASoC driver");
MODULE_LICENSE("GPL");
