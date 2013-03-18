/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * Based on sound/soc/imx/imx-pcm-dma-mx2.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dmaengine.h>
#include <linux/fsl/mxs-dma.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "mxs-pcm.h"

struct mxs_pcm_dma_data {
	struct mxs_dma_data dma_data;
	struct mxs_pcm_dma_params *dma_params;
};

static struct snd_pcm_hardware snd_mxs_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_INTERLEAVED,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S20_3LE |
				  SNDRV_PCM_FMTBIT_S24_LE,
	.channels_min		= 2,
	.channels_max		= 2,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 1,
	.periods_max		= 52,
	.buffer_bytes_max	= 64 * 1024,
	.fifo_size		= 32,

};

static bool filter(struct dma_chan *chan, void *param)
{
	struct mxs_pcm_dma_data *pcm_dma_data = param;
	struct mxs_pcm_dma_params *dma_params = pcm_dma_data->dma_params;

	if (!mxs_dma_is_apbx(chan))
		return false;

	if (chan->chan_id != dma_params->chan_num)
		return false;

	chan->private = &pcm_dma_data->dma_data;

	return true;
}

static int snd_mxs_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int snd_mxs_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mxs_pcm_dma_data *pcm_dma_data;
	int ret;

	pcm_dma_data = kzalloc(sizeof(*pcm_dma_data), GFP_KERNEL);
	if (pcm_dma_data == NULL)
		return -ENOMEM;

	pcm_dma_data->dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	pcm_dma_data->dma_data.chan_irq = pcm_dma_data->dma_params->chan_irq;

	ret = snd_dmaengine_pcm_open(substream, filter, pcm_dma_data);
	if (ret) {
		kfree(pcm_dma_data);
		return ret;
	}

	snd_soc_set_runtime_hwparams(substream, &snd_mxs_hardware);

	snd_dmaengine_pcm_set_data(substream, pcm_dma_data);

	return 0;
}

static int snd_mxs_close(struct snd_pcm_substream *substream)
{
	struct mxs_pcm_dma_data *pcm_dma_data = snd_dmaengine_pcm_get_data(substream);

	snd_dmaengine_pcm_close(substream);
	kfree(pcm_dma_data);

	return 0;
}

static int snd_mxs_pcm_mmap(struct snd_pcm_substream *substream,
		struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
					runtime->dma_area,
					runtime->dma_addr,
					runtime->dma_bytes);
}

static struct snd_pcm_ops mxs_pcm_ops = {
	.open		= snd_mxs_open,
	.close		= snd_mxs_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= snd_mxs_pcm_hw_params,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= snd_dmaengine_pcm_pointer_no_residue,
	.mmap		= snd_mxs_pcm_mmap,
};

static int mxs_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = snd_mxs_hardware.buffer_bytes_max;

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

static u64 mxs_pcm_dmamask = DMA_BIT_MASK(32);
static int mxs_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &mxs_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = mxs_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = mxs_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}

out:
	return ret;
}

static void mxs_pcm_free(struct snd_pcm *pcm)
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

static struct snd_soc_platform_driver mxs_soc_platform = {
	.ops		= &mxs_pcm_ops,
	.pcm_new	= mxs_pcm_new,
	.pcm_free	= mxs_pcm_free,
};

int mxs_pcm_platform_register(struct device *dev)
{
	return snd_soc_register_platform(dev, &mxs_soc_platform);
}
EXPORT_SYMBOL_GPL(mxs_pcm_platform_register);

void mxs_pcm_platform_unregister(struct device *dev)
{
	snd_soc_unregister_platform(dev);
}
EXPORT_SYMBOL_GPL(mxs_pcm_platform_unregister);

MODULE_LICENSE("GPL");
