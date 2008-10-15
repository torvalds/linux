/*
 * ALSA PCM interface for the TI DAVINCI processor
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/dma.h>

#include "davinci-pcm.h"

#define DAVINCI_PCM_DEBUG 0
#if DAVINCI_PCM_DEBUG
#define DPRINTK(x...) printk(KERN_DEBUG x)
#else
#define DPRINTK(x...)
#endif

static struct snd_pcm_hardware davinci_pcm_hardware = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE),
	.formats = (SNDRV_PCM_FMTBIT_S16_LE),
	.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
		  SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 |
		  SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
		  SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
		  SNDRV_PCM_RATE_KNOT),
	.rate_min = 8000,
	.rate_max = 96000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 128 * 1024,
	.period_bytes_min = 32,
	.period_bytes_max = 8 * 1024,
	.periods_min = 16,
	.periods_max = 255,
	.fifo_size = 0,
};

struct davinci_runtime_data {
	spinlock_t lock;
	int period;		/* current DMA period */
	int master_lch;		/* Master DMA channel */
	int slave_lch;		/* Slave DMA channel */
	struct davinci_pcm_dma_params *params;	/* DMA params */
};

static void davinci_pcm_enqueue_dma(struct snd_pcm_substream *substream)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int lch = prtd->slave_lch;
	unsigned int period_size;
	unsigned int dma_offset;
	dma_addr_t dma_pos;
	dma_addr_t src, dst;
	unsigned short src_bidx, dst_bidx;
	unsigned int data_type;
	unsigned int count;

	period_size = snd_pcm_lib_period_bytes(substream);
	dma_offset = prtd->period * period_size;
	dma_pos = runtime->dma_addr + dma_offset;

	DPRINTK("audio_set_dma_params_play channel = %d dma_ptr = %x "
		"period_size=%x\n", lch, dma_pos, period_size);

	data_type = prtd->params->data_type;
	count = period_size / data_type;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		src = dma_pos;
		dst = prtd->params->dma_addr;
		src_bidx = data_type;
		dst_bidx = 0;
	} else {
		src = prtd->params->dma_addr;
		dst = dma_pos;
		src_bidx = 0;
		dst_bidx = data_type;
	}

	davinci_set_dma_src_params(lch, src, INCR, W8BIT);
	davinci_set_dma_dest_params(lch, dst, INCR, W8BIT);
	davinci_set_dma_src_index(lch, src_bidx, 0);
	davinci_set_dma_dest_index(lch, dst_bidx, 0);
	davinci_set_dma_transfer_params(lch, data_type, count, 1, 0, ASYNC);

	prtd->period++;
	if (unlikely(prtd->period >= runtime->periods))
		prtd->period = 0;
}

static void davinci_pcm_dma_irq(int lch, u16 ch_status, void *data)
{
	struct snd_pcm_substream *substream = data;
	struct davinci_runtime_data *prtd = substream->runtime->private_data;

	DPRINTK("lch=%d, status=0x%x\n", lch, ch_status);

	if (unlikely(ch_status != DMA_COMPLETE))
		return;

	if (snd_pcm_running(substream)) {
		snd_pcm_period_elapsed(substream);

		spin_lock(&prtd->lock);
		davinci_pcm_enqueue_dma(substream);
		spin_unlock(&prtd->lock);
	}
}

static int davinci_pcm_dma_request(struct snd_pcm_substream *substream)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct davinci_pcm_dma_params *dma_data = rtd->dai->cpu_dai->dma_data;
	int tcc = TCC_ANY;
	int ret;

	if (!dma_data)
		return -ENODEV;

	prtd->params = dma_data;

	/* Request master DMA channel */
	ret = davinci_request_dma(prtd->params->channel, prtd->params->name,
				  davinci_pcm_dma_irq, substream,
				  &prtd->master_lch, &tcc, EVENTQ_0);
	if (ret)
		return ret;

	/* Request slave DMA channel */
	ret = davinci_request_dma(PARAM_ANY, "Link",
				  NULL, NULL, &prtd->slave_lch, &tcc, EVENTQ_0);
	if (ret) {
		davinci_free_dma(prtd->master_lch);
		return ret;
	}

	/* Link slave DMA channel in loopback */
	davinci_dma_link_lch(prtd->slave_lch, prtd->slave_lch);

	return 0;
}

static int davinci_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	spin_lock(&prtd->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		davinci_start_dma(prtd->master_lch);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		davinci_stop_dma(prtd->master_lch);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock(&prtd->lock);

	return ret;
}

static int davinci_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct davinci_runtime_data *prtd = substream->runtime->private_data;
	struct paramentry_descriptor temp;

	prtd->period = 0;
	davinci_pcm_enqueue_dma(substream);

	/* Get slave channel dma params for master channel startup */
	davinci_get_dma_params(prtd->slave_lch, &temp);
	davinci_set_dma_params(prtd->master_lch, &temp);

	return 0;
}

static snd_pcm_uframes_t
davinci_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct davinci_runtime_data *prtd = runtime->private_data;
	unsigned int offset;
	dma_addr_t count;
	dma_addr_t src, dst;

	spin_lock(&prtd->lock);

	davinci_dma_getposition(prtd->master_lch, &src, &dst);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		count = src - runtime->dma_addr;
	else
		count = dst - runtime->dma_addr;;

	spin_unlock(&prtd->lock);

	offset = bytes_to_frames(runtime, count);
	if (offset >= runtime->buffer_size)
		offset = 0;

	return offset;
}

static int davinci_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct davinci_runtime_data *prtd;
	int ret = 0;

	snd_soc_set_runtime_hwparams(substream, &davinci_pcm_hardware);

	prtd = kzalloc(sizeof(struct davinci_runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);

	runtime->private_data = prtd;

	ret = davinci_pcm_dma_request(substream);
	if (ret) {
		printk(KERN_ERR "davinci_pcm: Failed to get dma channels\n");
		kfree(prtd);
	}

	return ret;
}

static int davinci_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct davinci_runtime_data *prtd = runtime->private_data;

	davinci_dma_unlink_lch(prtd->slave_lch, prtd->slave_lch);

	davinci_free_dma(prtd->slave_lch);
	davinci_free_dma(prtd->master_lch);

	kfree(prtd);

	return 0;
}

static int davinci_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int davinci_pcm_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int davinci_pcm_mmap(struct snd_pcm_substream *substream,
			    struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

struct snd_pcm_ops davinci_pcm_ops = {
	.open = 	davinci_pcm_open,
	.close = 	davinci_pcm_close,
	.ioctl = 	snd_pcm_lib_ioctl,
	.hw_params = 	davinci_pcm_hw_params,
	.hw_free = 	davinci_pcm_hw_free,
	.prepare = 	davinci_pcm_prepare,
	.trigger = 	davinci_pcm_trigger,
	.pointer = 	davinci_pcm_pointer,
	.mmap = 	davinci_pcm_mmap,
};

static int davinci_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = davinci_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);

	DPRINTK("preallocate_dma_buffer: area=%p, addr=%p, size=%d\n",
		(void *) buf->area, (void *) buf->addr, size);

	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	return 0;
}

static void davinci_pcm_free(struct snd_pcm *pcm)
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

static u64 davinci_pcm_dmamask = 0xffffffff;

static int davinci_pcm_new(struct snd_card *card,
			   struct snd_soc_dai *dai, struct snd_pcm *pcm)
{
	int ret;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &davinci_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (dai->playback.channels_min) {
		ret = davinci_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			return ret;
	}

	if (dai->capture.channels_min) {
		ret = davinci_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			return ret;
	}

	return 0;
}

struct snd_soc_platform davinci_soc_platform = {
	.name = 	"davinci-audio",
	.pcm_ops = 	&davinci_pcm_ops,
	.pcm_new = 	davinci_pcm_new,
	.pcm_free = 	davinci_pcm_free,
};
EXPORT_SYMBOL_GPL(davinci_soc_platform);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI PCM DMA module");
MODULE_LICENSE("GPL");
