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
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include <asm/dma.h>
#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>

#include "pxa2xx-pcm.h"


static const snd_pcm_hardware_t pxa2xx_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192 - 32,
	.periods_min		= 1,
	.periods_max		= PAGE_SIZE/sizeof(pxa_dma_desc),
	.buffer_bytes_max	= 128 * 1024,
	.fifo_size		= 32,
};

struct pxa2xx_runtime_data {
	int dma_ch;
	pxa2xx_pcm_dma_params_t *params;
	pxa_dma_desc *dma_desc_array;
	dma_addr_t dma_desc_array_phys;
};

static int pxa2xx_pcm_hw_params(snd_pcm_substream_t *substream,
				snd_pcm_hw_params_t *params)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	struct pxa2xx_runtime_data *rtd = runtime->private_data;
	size_t totsize = params_buffer_bytes(params);
	size_t period = params_period_bytes(params);
	pxa_dma_desc *dma_desc;
	dma_addr_t dma_buff_phys, next_desc_phys;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = totsize;

	dma_desc = rtd->dma_desc_array;
	next_desc_phys = rtd->dma_desc_array_phys;
	dma_buff_phys = runtime->dma_addr;
	do {
		next_desc_phys += sizeof(pxa_dma_desc);
		dma_desc->ddadr = next_desc_phys;
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			dma_desc->dsadr = dma_buff_phys;
			dma_desc->dtadr = rtd->params->dev_addr;
		} else {
			dma_desc->dsadr = rtd->params->dev_addr;
			dma_desc->dtadr = dma_buff_phys;
		}
		if (period > totsize)
			period = totsize;
		dma_desc->dcmd = rtd->params->dcmd | period | DCMD_ENDIRQEN;
		dma_desc++;
		dma_buff_phys += period;
	} while (totsize -= period);
	dma_desc[-1].ddadr = rtd->dma_desc_array_phys;

	return 0;
}

static int pxa2xx_pcm_hw_free(snd_pcm_substream_t *substream)
{
	struct pxa2xx_runtime_data *rtd = substream->runtime->private_data;

	*rtd->params->drcmr = 0;
	snd_pcm_set_runtime_buffer(substream, NULL);
	return 0;
}

static int pxa2xx_pcm_prepare(snd_pcm_substream_t *substream)
{
	pxa2xx_pcm_client_t *client = substream->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;
	struct pxa2xx_runtime_data *rtd = runtime->private_data;

	DCSR(rtd->dma_ch) &= ~DCSR_RUN;
	DCSR(rtd->dma_ch) = 0;
	DCMD(rtd->dma_ch) = 0;
	*rtd->params->drcmr = rtd->dma_ch | DRCMR_MAPVLD;

	return client->prepare(substream);
}

static int pxa2xx_pcm_trigger(snd_pcm_substream_t *substream, int cmd)
{
	struct pxa2xx_runtime_data *rtd = substream->runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		DDADR(rtd->dma_ch) = rtd->dma_desc_array_phys;
		DCSR(rtd->dma_ch) = DCSR_RUN;
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		DCSR(rtd->dma_ch) &= ~DCSR_RUN;
		break;

	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		DCSR(rtd->dma_ch) |= DCSR_RUN;
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static void pxa2xx_pcm_dma_irq(int dma_ch, void *dev_id, struct pt_regs *regs)
{
	snd_pcm_substream_t *substream = dev_id;
	struct pxa2xx_runtime_data *rtd = substream->runtime->private_data;
	int dcsr;

	dcsr = DCSR(dma_ch);
	DCSR(dma_ch) = dcsr & ~DCSR_STOPIRQEN;

	if (dcsr & DCSR_ENDINTR) {
		snd_pcm_period_elapsed(substream);
	} else {
		printk( KERN_ERR "%s: DMA error on channel %d (DCSR=%#x)\n",
			rtd->params->name, dma_ch, dcsr );
		snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
	}
}

static snd_pcm_uframes_t pxa2xx_pcm_pointer(snd_pcm_substream_t *substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	struct pxa2xx_runtime_data *rtd = runtime->private_data;
	dma_addr_t ptr = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			 DSADR(rtd->dma_ch) : DTADR(rtd->dma_ch);
	snd_pcm_uframes_t x = bytes_to_frames(runtime, ptr - runtime->dma_addr);
	if (x == runtime->buffer_size)
		x = 0;
	return x;
}

static int
pxa2xx_pcm_hw_rule_mult32(snd_pcm_hw_params_t *params, snd_pcm_hw_rule_t *rule)
{
	snd_interval_t *i = hw_param_interval(params, rule->var);
	int changed = 0;

	if (i->min & 31) {
		i->min = (i->min & ~31) + 32;
		i->openmin = 0;
		changed = 1;
	}

	if (i->max & 31) {
		i->max &= ~31;
		i->openmax = 0;
		changed = 1;
	}

	return changed;
}

static int pxa2xx_pcm_open(snd_pcm_substream_t *substream)
{
	pxa2xx_pcm_client_t *client = substream->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;
	struct pxa2xx_runtime_data *rtd;
	int ret;

	runtime->hw = pxa2xx_pcm_hardware;

	/*
	 * For mysterious reasons (and despite what the manual says)
	 * playback samples are lost if the DMA count is not a multiple
	 * of the DMA burst size.  Let's add a rule to enforce that.
	 */
	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
				  pxa2xx_pcm_hw_rule_mult32, NULL,
				  SNDRV_PCM_HW_PARAM_PERIOD_BYTES, -1);
	if (ret)
		goto out;
	ret = snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				  pxa2xx_pcm_hw_rule_mult32, NULL,
				  SNDRV_PCM_HW_PARAM_BUFFER_BYTES, -1);
	if (ret)
		goto out;

	ret = -ENOMEM;
	rtd = kmalloc(sizeof(*rtd), GFP_KERNEL);
	if (!rtd)
		goto out;
	rtd->dma_desc_array =
		dma_alloc_writecombine(substream->pcm->card->dev, PAGE_SIZE,
				       &rtd->dma_desc_array_phys, GFP_KERNEL);
	if (!rtd->dma_desc_array)
		goto err1;

	rtd->params = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		      client->playback_params : client->capture_params;
	ret = pxa_request_dma(rtd->params->name, DMA_PRIO_LOW,
			      pxa2xx_pcm_dma_irq, substream);
	if (ret < 0)
		goto err2;
	rtd->dma_ch = ret;

	runtime->private_data = rtd;
	ret = client->startup(substream);
	if (!ret)
		goto out;

	pxa_free_dma(rtd->dma_ch);
 err2:
	dma_free_writecombine(substream->pcm->card->dev, PAGE_SIZE,
			      rtd->dma_desc_array, rtd->dma_desc_array_phys);
 err1:
	kfree(rtd);
 out:
	return ret;
}

static int pxa2xx_pcm_close(snd_pcm_substream_t *substream)
{
	pxa2xx_pcm_client_t *client = substream->private_data;
	struct pxa2xx_runtime_data *rtd = substream->runtime->private_data;

	pxa_free_dma(rtd->dma_ch);
	client->shutdown(substream);
	dma_free_writecombine(substream->pcm->card->dev, PAGE_SIZE,
			      rtd->dma_desc_array, rtd->dma_desc_array_phys);
	kfree(rtd);
	return 0;
}

static int
pxa2xx_pcm_mmap(snd_pcm_substream_t *substream, struct vm_area_struct *vma)
{
	snd_pcm_runtime_t *runtime = substream->runtime;
	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static snd_pcm_ops_t pxa2xx_pcm_ops = {
	.open		= pxa2xx_pcm_open,
	.close		= pxa2xx_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= pxa2xx_pcm_hw_params,
	.hw_free	= pxa2xx_pcm_hw_free,
	.prepare	= pxa2xx_pcm_prepare,
	.trigger	= pxa2xx_pcm_trigger,
	.pointer	= pxa2xx_pcm_pointer,
	.mmap		= pxa2xx_pcm_mmap,
};

static int pxa2xx_pcm_preallocate_dma_buffer(snd_pcm_t *pcm, int stream)
{
	snd_pcm_substream_t *substream = pcm->streams[stream].substream;
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

static void pxa2xx_pcm_free_dma_buffers(snd_pcm_t *pcm)
{
	snd_pcm_substream_t *substream;
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

static u64 pxa2xx_pcm_dmamask = 0xffffffff;

int pxa2xx_pcm_new(snd_card_t *card, pxa2xx_pcm_client_t *client, snd_pcm_t **rpcm)
{
	snd_pcm_t *pcm;
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
