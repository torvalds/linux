/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/mach-jz4740/dma.h>
#include "jz4740-pcm.h"

struct jz4740_runtime_data {
	unsigned long dma_period;
	dma_addr_t dma_start;
	dma_addr_t dma_pos;
	dma_addr_t dma_end;

	struct jz4740_dma_chan *dma;

	dma_addr_t fifo_addr;
};

/* identify hardware playback capabilities */
static const struct snd_pcm_hardware jz4740_pcm_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8,

	.rates			= SNDRV_PCM_RATE_8000_48000,
	.channels_min		= 1,
	.channels_max		= 2,
	.period_bytes_min	= 16,
	.period_bytes_max	= 2 * PAGE_SIZE,
	.periods_min		= 2,
	.periods_max		= 128,
	.buffer_bytes_max	= 128 * 2 * PAGE_SIZE,
	.fifo_size		= 32,
};

static void jz4740_pcm_start_transfer(struct jz4740_runtime_data *prtd,
	struct snd_pcm_substream *substream)
{
	unsigned long count;

	if (prtd->dma_pos == prtd->dma_end)
		prtd->dma_pos = prtd->dma_start;

	if (prtd->dma_pos + prtd->dma_period > prtd->dma_end)
		count = prtd->dma_end - prtd->dma_pos;
	else
		count = prtd->dma_period;

	jz4740_dma_disable(prtd->dma);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		jz4740_dma_set_src_addr(prtd->dma, prtd->dma_pos);
		jz4740_dma_set_dst_addr(prtd->dma, prtd->fifo_addr);
	} else {
		jz4740_dma_set_src_addr(prtd->dma, prtd->fifo_addr);
		jz4740_dma_set_dst_addr(prtd->dma, prtd->dma_pos);
	}

	jz4740_dma_set_transfer_count(prtd->dma, count);

	prtd->dma_pos += count;

	jz4740_dma_enable(prtd->dma);
}

static void jz4740_pcm_dma_transfer_done(struct jz4740_dma_chan *dma, int err,
	void *dev_id)
{
	struct snd_pcm_substream *substream = dev_id;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jz4740_runtime_data *prtd = runtime->private_data;

	snd_pcm_period_elapsed(substream);

	jz4740_pcm_start_transfer(prtd, substream);
}

static int jz4740_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jz4740_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct jz4740_pcm_config *config;

	config = snd_soc_dai_get_dma_data(rtd->dai->cpu_dai, substream);

	if (!config)
		return 0;

	if (!prtd->dma) {
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			prtd->dma = jz4740_dma_request(substream, "PCM Capture");
		else
			prtd->dma = jz4740_dma_request(substream, "PCM Playback");
	}

	if (!prtd->dma)
		return -EBUSY;

	jz4740_dma_configure(prtd->dma, &config->dma_config);
	prtd->fifo_addr = config->fifo_addr;

	jz4740_dma_set_complete_cb(prtd->dma, jz4740_pcm_dma_transfer_done);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	prtd->dma_period = params_period_bytes(params);
	prtd->dma_start = runtime->dma_addr;
	prtd->dma_pos = prtd->dma_start;
	prtd->dma_end = prtd->dma_start + runtime->dma_bytes;

	return 0;
}

static int jz4740_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct jz4740_runtime_data *prtd = substream->runtime->private_data;

	snd_pcm_set_runtime_buffer(substream, NULL);
	if (prtd->dma) {
		jz4740_dma_free(prtd->dma);
		prtd->dma = NULL;
	}

	return 0;
}

static int jz4740_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct jz4740_runtime_data *prtd = substream->runtime->private_data;

	if (!prtd->dma)
		return -EBUSY;

	prtd->dma_pos = prtd->dma_start;

	return 0;
}

static int jz4740_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jz4740_runtime_data *prtd = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		jz4740_pcm_start_transfer(prtd, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		jz4740_dma_disable(prtd->dma);
		break;
	default:
		break;
	}

	return 0;
}

static snd_pcm_uframes_t jz4740_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jz4740_runtime_data *prtd = runtime->private_data;
	unsigned long byte_offset;
	snd_pcm_uframes_t offset;
	struct jz4740_dma_chan *dma = prtd->dma;

	/* prtd->dma_pos points to the end of the current transfer. So by
	 * subtracting prdt->dma_start we get the offset to the end of the
	 * current period in bytes. By subtracting the residue of the transfer
	 * we get the current offset in bytes. */
	byte_offset = prtd->dma_pos - prtd->dma_start;
	byte_offset -= jz4740_dma_get_residue(dma);

	offset = bytes_to_frames(runtime, byte_offset);
	if (offset >= runtime->buffer_size)
		offset = 0;

	return offset;
}

static int jz4740_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jz4740_runtime_data *prtd;

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	snd_soc_set_runtime_hwparams(substream, &jz4740_pcm_hardware);

	runtime->private_data = prtd;

	return 0;
}

static int jz4740_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct jz4740_runtime_data *prtd = runtime->private_data;

	kfree(prtd);

	return 0;
}

static int jz4740_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start,
			substream->dma_buffer.addr >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static struct snd_pcm_ops jz4740_pcm_ops = {
	.open		= jz4740_pcm_open,
	.close		= jz4740_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= jz4740_pcm_hw_params,
	.hw_free	= jz4740_pcm_hw_free,
	.prepare	= jz4740_pcm_prepare,
	.trigger	= jz4740_pcm_trigger,
	.pointer	= jz4740_pcm_pointer,
	.mmap		= jz4740_pcm_mmap,
};

static int jz4740_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = jz4740_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;

	buf->area = dma_alloc_noncoherent(pcm->card->dev, size,
					  &buf->addr, GFP_KERNEL);
	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;

	return 0;
}

static void jz4740_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < SNDRV_PCM_STREAM_LAST; ++stream) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;

		dma_free_noncoherent(pcm->card->dev, buf->bytes, buf->area,
				buf->addr);
		buf->area = NULL;
	}
}

static u64 jz4740_pcm_dmamask = DMA_BIT_MASK(32);

int jz4740_pcm_new(struct snd_card *card, struct snd_soc_dai *dai,
	struct snd_pcm *pcm)
{
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &jz4740_pcm_dmamask;

	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (dai->playback.channels_min) {
		ret = jz4740_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto err;
	}

	if (dai->capture.channels_min) {
		ret = jz4740_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto err;
	}

err:
	return ret;
}

struct snd_soc_platform jz4740_soc_platform = {
		.name		= "jz4740-pcm",
		.pcm_ops	= &jz4740_pcm_ops,
		.pcm_new	= jz4740_pcm_new,
		.pcm_free	= jz4740_pcm_free,
};
EXPORT_SYMBOL_GPL(jz4740_soc_platform);

static int __devinit jz4740_pcm_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&jz4740_soc_platform);
}

static int __devexit jz4740_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&jz4740_soc_platform);
	return 0;
}

static struct platform_driver jz4740_pcm_driver = {
	.probe = jz4740_pcm_probe,
	.remove = __devexit_p(jz4740_pcm_remove),
	.driver = {
		.name = "jz4740-pcm",
		.owner = THIS_MODULE,
	},
};

static int __init jz4740_soc_platform_init(void)
{
	return platform_driver_register(&jz4740_pcm_driver);
}
module_init(jz4740_soc_platform_init);

static void __exit jz4740_soc_platform_exit(void)
{
	return platform_driver_unregister(&jz4740_pcm_driver);
}
module_exit(jz4740_soc_platform_exit);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Ingenic SoC JZ4740 PCM driver");
MODULE_LICENSE("GPL");
