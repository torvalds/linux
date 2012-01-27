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

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <mach/dma.h>
#include "mxs-pcm.h"

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

static void audio_dma_irq(void *data)
{
	struct snd_pcm_substream *substream = (struct snd_pcm_substream *)data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mxs_pcm_runtime_data *iprtd = runtime->private_data;

	iprtd->offset += iprtd->period_bytes;
	iprtd->offset %= iprtd->period_bytes * iprtd->periods;
	snd_pcm_period_elapsed(substream);
}

static bool filter(struct dma_chan *chan, void *param)
{
	struct mxs_pcm_runtime_data *iprtd = param;
	struct mxs_pcm_dma_params *dma_params = iprtd->dma_params;

	if (!mxs_dma_is_apbx(chan))
		return false;

	if (chan->chan_id != dma_params->chan_num)
		return false;

	chan->private = &iprtd->dma_data;

	return true;
}

static int mxs_dma_alloc(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mxs_pcm_runtime_data *iprtd = runtime->private_data;
	dma_cap_mask_t mask;

	iprtd->dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	iprtd->dma_data.chan_irq = iprtd->dma_params->chan_irq;
	iprtd->dma_chan = dma_request_channel(mask, filter, iprtd);
	if (!iprtd->dma_chan)
		return -EINVAL;

	return 0;
}

static int snd_mxs_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mxs_pcm_runtime_data *iprtd = runtime->private_data;
	unsigned long dma_addr;
	struct dma_chan *chan;
	int ret;

	ret = mxs_dma_alloc(substream, params);
	if (ret)
		return ret;
	chan = iprtd->dma_chan;

	iprtd->size = params_buffer_bytes(params);
	iprtd->periods = params_periods(params);
	iprtd->period_bytes = params_period_bytes(params);
	iprtd->offset = 0;
	iprtd->period_time = HZ / (params_rate(params) /
			params_period_size(params));

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	dma_addr = runtime->dma_addr;

	iprtd->buf = substream->dma_buffer.area;

	iprtd->desc = chan->device->device_prep_dma_cyclic(chan, dma_addr,
			iprtd->period_bytes * iprtd->periods,
			iprtd->period_bytes,
			substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			DMA_MEM_TO_DEV : DMA_DEV_TO_MEM);
	if (!iprtd->desc) {
		dev_err(&chan->dev->device, "cannot prepare slave dma\n");
		return -EINVAL;
	}

	iprtd->desc->callback = audio_dma_irq;
	iprtd->desc->callback_param = substream;

	return 0;
}

static int snd_mxs_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mxs_pcm_runtime_data *iprtd = runtime->private_data;

	if (iprtd->dma_chan) {
		dma_release_channel(iprtd->dma_chan);
		iprtd->dma_chan = NULL;
	}

	return 0;
}

static int snd_mxs_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mxs_pcm_runtime_data *iprtd = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dmaengine_submit(iprtd->desc);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_terminate_all(iprtd->dma_chan);

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t snd_mxs_pcm_pointer(
		struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mxs_pcm_runtime_data *iprtd = runtime->private_data;

	return bytes_to_frames(substream->runtime, iprtd->offset);
}

static int snd_mxs_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mxs_pcm_runtime_data *iprtd;
	int ret;

	iprtd = kzalloc(sizeof(*iprtd), GFP_KERNEL);
	if (iprtd == NULL)
		return -ENOMEM;
	runtime->private_data = iprtd;

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		kfree(iprtd);
		return ret;
	}

	snd_soc_set_runtime_hwparams(substream, &snd_mxs_hardware);

	return 0;
}

static int snd_mxs_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mxs_pcm_runtime_data *iprtd = runtime->private_data;

	kfree(iprtd);

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
	.hw_free	= snd_mxs_pcm_hw_free,
	.trigger	= snd_mxs_pcm_trigger,
	.pointer	= snd_mxs_pcm_pointer,
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

static int __devinit mxs_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &mxs_soc_platform);
}

static int __devexit mxs_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);

	return 0;
}

static struct platform_driver mxs_pcm_driver = {
	.driver = {
		.name = "mxs-pcm-audio",
		.owner = THIS_MODULE,
	},
	.probe = mxs_soc_platform_probe,
	.remove = __devexit_p(mxs_soc_platform_remove),
};

module_platform_driver(mxs_pcm_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mxs-pcm-audio");
