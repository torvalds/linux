/*
 * ALSA PCM interface for ST SPEAr Processors
 *
 * sound/soc/spear/spear_pcm.c
 *
 * Copyright (C) 2012 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/spear_dma.h>

static struct snd_pcm_hardware spear_pcm_hardware = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.buffer_bytes_max = 16 * 1024, /* max buffer size */
	.period_bytes_min = 2 * 1024, /* 1 msec data minimum period size */
	.period_bytes_max = 2 * 1024, /* maximum period size */
	.periods_min = 1, /* min # periods */
	.periods_max = 8, /* max # of periods */
	.fifo_size = 0, /* fifo size in bytes */
};

static int spear_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int spear_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);

	return 0;
}

static int spear_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	struct spear_dma_data *dma_data = (struct spear_dma_data *)
		snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	int ret;

	ret = snd_soc_set_runtime_hwparams(substream, &spear_pcm_hardware);
	if (ret)
		return ret;

	return snd_dmaengine_pcm_open_request_chan(substream, dma_data->filter,
				dma_data);
}

static int spear_pcm_mmap(struct snd_pcm_substream *substream,
		struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
			runtime->dma_area, runtime->dma_addr,
			runtime->dma_bytes);
}

static struct snd_pcm_ops spear_pcm_ops = {
	.open		= spear_pcm_open,
	.close		= snd_dmaengine_pcm_close_release_chan,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= spear_pcm_hw_params,
	.hw_free	= spear_pcm_hw_free,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= snd_dmaengine_pcm_pointer,
	.mmap		= spear_pcm_mmap,
};

static int
spear_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream,
		size_t size)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;

	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
			&buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;

	dev_info(buf->dev.dev,
			" preallocate_dma_buffer: area=%p, addr=%p, size=%d\n",
			(void *)buf->area, (void *)buf->addr, size);

	buf->bytes = size;
	return 0;
}

static void spear_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf || !buf->area)
			continue;

		dma_free_writecombine(pcm->card->dev, buf->bytes,
				buf->area, buf->addr);
		buf->area = NULL;
	}
}

static u64 spear_pcm_dmamask = DMA_BIT_MASK(32);

static int spear_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	int ret;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &spear_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (rtd->cpu_dai->driver->playback.channels_min) {
		ret = spear_pcm_preallocate_dma_buffer(rtd->pcm,
				SNDRV_PCM_STREAM_PLAYBACK,
				spear_pcm_hardware.buffer_bytes_max);
		if (ret)
			return ret;
	}

	if (rtd->cpu_dai->driver->capture.channels_min) {
		ret = spear_pcm_preallocate_dma_buffer(rtd->pcm,
				SNDRV_PCM_STREAM_CAPTURE,
				spear_pcm_hardware.buffer_bytes_max);
		if (ret)
			return ret;
	}

	return 0;
}

static struct snd_soc_platform_driver spear_soc_platform = {
	.ops		=	&spear_pcm_ops,
	.pcm_new	=	spear_pcm_new,
	.pcm_free	=	spear_pcm_free,
};

static int spear_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &spear_soc_platform);
}

static int spear_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);

	return 0;
}

static struct platform_driver spear_pcm_driver = {
	.driver = {
		.name = "spear-pcm-audio",
		.owner = THIS_MODULE,
	},

	.probe = spear_soc_platform_probe,
	.remove = spear_soc_platform_remove,
};

module_platform_driver(spear_pcm_driver);

MODULE_AUTHOR("Rajeev Kumar <rajeev-dlh.kumar@st.com>");
MODULE_DESCRIPTION("SPEAr PCM DMA module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spear-pcm-audio");
