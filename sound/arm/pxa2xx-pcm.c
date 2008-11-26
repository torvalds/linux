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

#include <sound/core.h>
#include <sound/pxa2xx-lib.h>

#include "pxa2xx-pcm.h"

static int pxa2xx_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct pxa2xx_pcm_client *client = substream->private_data;

	__pxa2xx_pcm_prepare(substream);

	return client->prepare(substream);
}

static int pxa2xx_pcm_open(struct snd_pcm_substream *substream)
{
	struct pxa2xx_pcm_client *client = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct pxa2xx_runtime_data *rtd;
	int ret;

	ret = __pxa2xx_pcm_open(substream);
	if (ret)
		goto out;

	rtd = runtime->private_data;

	rtd->params = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		      client->playback_params : client->capture_params;
	ret = pxa_request_dma(rtd->params->name, DMA_PRIO_LOW,
			      pxa2xx_pcm_dma_irq, substream);
	if (ret < 0)
		goto err2;
	rtd->dma_ch = ret;

	ret = client->startup(substream);
	if (!ret)
		goto out;

	pxa_free_dma(rtd->dma_ch);
 err2:
	__pxa2xx_pcm_close(substream);
 out:
	return ret;
}

static int pxa2xx_pcm_close(struct snd_pcm_substream *substream)
{
	struct pxa2xx_pcm_client *client = substream->private_data;
	struct pxa2xx_runtime_data *rtd = substream->runtime->private_data;

	pxa_free_dma(rtd->dma_ch);
	client->shutdown(substream);

	return __pxa2xx_pcm_close(substream);
}

static struct snd_pcm_ops pxa2xx_pcm_ops = {
	.open		= pxa2xx_pcm_open,
	.close		= pxa2xx_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= __pxa2xx_pcm_hw_params,
	.hw_free	= __pxa2xx_pcm_hw_free,
	.prepare	= pxa2xx_pcm_prepare,
	.trigger	= pxa2xx_pcm_trigger,
	.pointer	= pxa2xx_pcm_pointer,
	.mmap		= pxa2xx_pcm_mmap,
};

static u64 pxa2xx_pcm_dmamask = 0xffffffff;

int pxa2xx_pcm_new(struct snd_card *card, struct pxa2xx_pcm_client *client,
		   struct snd_pcm **rpcm)
{
	struct snd_pcm *pcm;
	int play = client->playback_params ? 1 : 0;
	int capt = client->capture_params ? 1 : 0;
	int ret;

	ret = snd_pcm_new(card, "PXA2xx-PCM", 0, play, capt, &pcm);
	if (ret)
		goto out;

	pcm->private_data = client;
	pcm->private_free = pxa2xx_pcm_free_dma_buffers;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &pxa2xx_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (play) {
		int stream = SNDRV_PCM_STREAM_PLAYBACK;
		snd_pcm_set_ops(pcm, stream, &pxa2xx_pcm_ops);
		ret = pxa2xx_pcm_preallocate_dma_buffer(pcm, stream);
		if (ret)
			goto out;
	}
	if (capt) {
		int stream = SNDRV_PCM_STREAM_CAPTURE;
		snd_pcm_set_ops(pcm, stream, &pxa2xx_pcm_ops);
		ret = pxa2xx_pcm_preallocate_dma_buffer(pcm, stream);
		if (ret)
			goto out;
	}

	if (rpcm)
		*rpcm = pcm;
	ret = 0;

 out:
	return ret;
}

EXPORT_SYMBOL(pxa2xx_pcm_new);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("Intel PXA2xx PCM DMA module");
MODULE_LICENSE("GPL");
