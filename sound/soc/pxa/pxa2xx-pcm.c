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

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pxa2xx-lib.h>

#include "pxa2xx-pcm.h"
#include "../../arm/pxa2xx-pcm.h"

static int pxa2xx_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pxa2xx_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct pxa2xx_pcm_dma_params *dma = rtd->dai->cpu_dai->dma_data;
	int ret;

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!dma)
		return 0;

	/* this may get called several times by oss emulation
	 * with different params */
	if (prtd->params == NULL) {
		prtd->params = dma;
		ret = pxa_request_dma(prtd->params->name, DMA_PRIO_LOW,
			      pxa2xx_pcm_dma_irq, substream);
		if (ret < 0)
			return ret;
		prtd->dma_ch = ret;
	} else if (prtd->params != dma) {
		pxa_free_dma(prtd->dma_ch);
		prtd->params = dma;
		ret = pxa_request_dma(prtd->params->name, DMA_PRIO_LOW,
			      pxa2xx_pcm_dma_irq, substream);
		if (ret < 0)
			return ret;
		prtd->dma_ch = ret;
	}

	return __pxa2xx_pcm_hw_params(substream, params);
}

static int pxa2xx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct pxa2xx_runtime_data *prtd = substream->runtime->private_data;

	__pxa2xx_pcm_hw_free(substream);

	if (prtd->dma_ch >= 0) {
		pxa_free_dma(prtd->dma_ch);
		prtd->dma_ch = -1;
	}

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

static u64 pxa2xx_pcm_dmamask = DMA_32BIT_MASK;

static int pxa2xx_soc_pcm_new(struct snd_card *card, struct snd_soc_dai *dai,
	struct snd_pcm *pcm)
{
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &pxa2xx_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_32BIT_MASK;

	if (dai->playback.channels_min) {
		ret = pxa2xx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (dai->capture.channels_min) {
		ret = pxa2xx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

struct snd_soc_platform pxa2xx_soc_platform = {
	.name		= "pxa2xx-audio",
	.pcm_ops 	= &pxa2xx_pcm_ops,
	.pcm_new	= pxa2xx_soc_pcm_new,
	.pcm_free	= pxa2xx_pcm_free_dma_buffers,
};
EXPORT_SYMBOL_GPL(pxa2xx_soc_platform);

static int __init pxa2xx_soc_platform_init(void)
{
	return snd_soc_register_platform(&pxa2xx_soc_platform);
}
module_init(pxa2xx_soc_platform_init);

static void __exit pxa2xx_soc_platform_exit(void)
{
	snd_soc_unregister_platform(&pxa2xx_soc_platform);
}
module_exit(pxa2xx_soc_platform_exit);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("Intel PXA2xx PCM DMA module");
MODULE_LICENSE("GPL");
