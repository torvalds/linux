/*
 * Copyright 2009 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This code is based on code copyrighted by Freescale,
 * Liam Girdwood, Javier Martin and probably others.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include "imx-pcm.h"

int snd_imx_pcm_mmap(struct snd_pcm_substream *substream,
		struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	ret = dma_mmap_writecombine(substream->pcm->card->dev, vma,
		runtime->dma_area, runtime->dma_addr, runtime->dma_bytes);

	pr_debug("%s: ret: %d %p 0x%08x 0x%08x\n", __func__, ret,
			runtime->dma_area,
			runtime->dma_addr,
			runtime->dma_bytes);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_imx_pcm_mmap);

static int imx_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = IMX_SSI_DMABUF_SIZE;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;
	buf->bytes = size;

	return 0;
}

static u64 imx_pcm_dmamask = DMA_BIT_MASK(32);

int imx_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &imx_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);
	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = imx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = imx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}

out:
	return ret;
}
EXPORT_SYMBOL_GPL(imx_pcm_new);

void imx_pcm_free(struct snd_pcm *pcm)
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

		dma_free_writecombine(pcm->card->dev, buf->bytes,
				      buf->area, buf->addr);
		buf->area = NULL;
	}
}
EXPORT_SYMBOL_GPL(imx_pcm_free);

static int imx_pcm_probe(struct platform_device *pdev)
{
	if (strcmp(pdev->id_entry->name, "imx-fiq-pcm-audio") == 0)
		return imx_pcm_fiq_init(pdev);

	return imx_pcm_dma_init(pdev);
}

static int imx_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_device_id imx_pcm_devtype[] = {
	{ .name = "imx-pcm-audio", },
	{ .name = "imx-fiq-pcm-audio", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, imx_pcm_devtype);

static struct platform_driver imx_pcm_driver = {
	.driver = {
			.name = "imx-pcm",
			.owner = THIS_MODULE,
	},
	.id_table = imx_pcm_devtype,
	.probe = imx_pcm_probe,
	.remove = imx_pcm_remove,
};
module_platform_driver(imx_pcm_driver);

MODULE_DESCRIPTION("Freescale i.MX PCM driver");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
MODULE_LICENSE("GPL");
