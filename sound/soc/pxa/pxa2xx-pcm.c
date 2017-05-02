/*
 * linux/sound/arm/pxa2xx-pcm.c -- ALSA PCM interface for the Intel PXA2xx chip
 *
 * Author:	Nicolas Pitre
 * Created:	Nov 30, 2004
 * Copyright:	(C) 2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/of.h>

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pxa2xx-lib.h>
#include <sound/dmaengine_pcm.h>

#include "../../arm/pxa2xx-pcm.h"

static int pxa2xx_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_dmaengine_dai_dma_data *dma;

	dma = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!dma)
		return 0;

	return __pxa2xx_pcm_hw_params(substream, params);
}

static int pxa2xx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	__pxa2xx_pcm_hw_free(substream);

	return 0;
}

static struct snd_pcm_ops pxa2xx_pcm_ops = {
	.open		= __pxa2xx_pcm_open,
	.close		= __pxa2xx_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pxa2xx_pcm_hw_params,
	.hw_free	= pxa2xx_pcm_hw_free,
	.prepare	= __pxa2xx_pcm_prepare,
	.trigger	= pxa2xx_pcm_trigger,
	.pointer	= pxa2xx_pcm_pointer,
	.mmap		= pxa2xx_pcm_mmap,
};

static int pxa2xx_soc_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = pxa2xx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = pxa2xx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

static struct snd_soc_platform_driver pxa2xx_soc_platform = {
	.ops		= &pxa2xx_pcm_ops,
	.pcm_new	= pxa2xx_soc_pcm_new,
	.pcm_free	= pxa2xx_pcm_free_dma_buffers,
};

static int pxa2xx_soc_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_platform(&pdev->dev, &pxa2xx_soc_platform);
}

#ifdef CONFIG_OF
static const struct of_device_id snd_soc_pxa_audio_match[] = {
	{ .compatible   = "mrvl,pxa-pcm-audio" },
	{ }
};
MODULE_DEVICE_TABLE(of, snd_soc_pxa_audio_match);
#endif

static struct platform_driver pxa_pcm_driver = {
	.driver = {
		.name = "pxa-pcm-audio",
		.of_match_table = of_match_ptr(snd_soc_pxa_audio_match),
	},

	.probe = pxa2xx_soc_platform_probe,
};

module_platform_driver(pxa_pcm_driver);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("Intel PXA2xx PCM DMA module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa-pcm-audio");
