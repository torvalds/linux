/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dma/pxa-dma.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/pxa2xx-lib.h>
#include <sound/dmaengine_pcm.h>

#include "pxa2xx-pcm.h"

static const struct snd_pcm_hardware pxa2xx_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE |
				  SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
					SNDRV_PCM_FMTBIT_S24_LE |
					SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192 - 32,
	.periods_min		= 1,
	.periods_max		= 256,
	.buffer_bytes_max	= 128 * 1024,
	.fifo_size		= 32,
};

int __pxa2xx_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct dma_chan *chan = snd_dmaengine_pcm_get_chan(substream);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_dmaengine_dai_dma_data *dma_params;
	struct dma_slave_config config;
	int ret;

	dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	if (!dma_params)
		return 0;

	ret = snd_hwparams_to_dma_slave_config(substream, params, &config);
	if (ret)
		return ret;

	snd_dmaengine_pcm_set_config_from_dai_data(substream,
			snd_soc_dai_get_dma_data(rtd->cpu_dai, substream),
			&config);

	ret = dmaengine_slave_config(chan, &config);
	if (ret)
		return ret;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}
EXPORT_SYMBOL(__pxa2xx_pcm_hw_params);

int __pxa2xx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}
EXPORT_SYMBOL(__pxa2xx_pcm_hw_free);

int pxa2xx_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	return snd_dmaengine_pcm_trigger(substream, cmd);
}
EXPORT_SYMBOL(pxa2xx_pcm_trigger);

snd_pcm_uframes_t
pxa2xx_pcm_pointer(struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_pointer(substream);
}
EXPORT_SYMBOL(pxa2xx_pcm_pointer);

int __pxa2xx_pcm_prepare(struct snd_pcm_substream *substream)
{
	return 0;
}
EXPORT_SYMBOL(__pxa2xx_pcm_prepare);

int __pxa2xx_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_dmaengine_dai_dma_data *dma_params;
	int ret;

	runtime->hw = pxa2xx_pcm_hardware;

	dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	if (!dma_params)
		return 0;

	/*
	 * For mysterious reasons (and despite what the manual says)
	 * playback samples are lost if the DMA count is not a multiple
	 * of the DMA burst size.  Let's add a rule to enforce that.
	 */
	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (ret)
		return ret;

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	return snd_dmaengine_pcm_open_request_chan(substream,
					pxad_filter_fn,
					dma_params->filter_data);
}
EXPORT_SYMBOL(__pxa2xx_pcm_open);

int __pxa2xx_pcm_close(struct snd_pcm_substream *substream)
{
	return snd_dmaengine_pcm_close_release_chan(substream);
}
EXPORT_SYMBOL(__pxa2xx_pcm_close);

int pxa2xx_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}
EXPORT_SYMBOL(pxa2xx_pcm_mmap);

int pxa2xx_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = pxa2xx_pcm_hardware.buffer_bytes_max;
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
EXPORT_SYMBOL(pxa2xx_pcm_preallocate_dma_buffer);

void pxa2xx_pcm_free_dma_buffers(struct snd_pcm *pcm)
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
EXPORT_SYMBOL(pxa2xx_pcm_free_dma_buffers);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_DESCRIPTION("Intel PXA2xx sound library");
MODULE_LICENSE("GPL");
