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

#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

#include <mach/dma.h>

#include <sound/core.h>
#include <sound/pxa2xx-lib.h>
#include <sound/dmaengine_pcm.h>

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

	ret = client->startup(substream);
	if (!ret)
		goto err2;

	return 0;

 err2:
	__pxa2xx_pcm_close(substream);
 out:
	return ret;
}

static int pxa2xx_pcm_close(struct snd_pcm_substream *substream)
{
	struct pxa2xx_pcm_client *client = substream->private_data;

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

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		goto out;

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
