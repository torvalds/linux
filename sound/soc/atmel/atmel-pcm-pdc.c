// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * atmel-pcm.c  --  ALSA PCM interface for the Atmel atmel SoC.
 *
 *  Copyright (C) 2005 SAN People
 *  Copyright (C) 2008 Atmel
 *
 * Authors: Sedji Gaouaou <sedji.gaouaou@atmel.com>
 *
 * Based on at91-pcm. by:
 * Frank Mandarino <fmandarino@endrelia.com>
 * Copyright 2006 Endrelia Technologies Inc.
 *
 * Based on pxa2xx-pcm.c by:
 *
 * Author:	Nicolas Pitre
 * Created:	Nov 30, 2004
 * Copyright:	(C) 2004 MontaVista Software, Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/atmel_pdc.h>
#include <linux/atmel-ssc.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "atmel-pcm.h"


static int atmel_pcm_preallocate_dma_buffer(struct snd_pcm *pcm,
	int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = ATMEL_SSC_DMABUF_SIZE;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_coherent(pcm->card->dev, size,
			&buf->addr, GFP_KERNEL);
	pr_debug("atmel-pcm: alloc dma buffer: area=%p, addr=%p, size=%zu\n",
			(void *)buf->area, (void *)(long)buf->addr, size);

	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	return 0;
}

static int atmel_pcm_mmap(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream,
			  struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start,
		       substream->dma_buffer.addr >> PAGE_SHIFT,
		       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static int atmel_pcm_new(struct snd_soc_component *component,
			 struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		pr_debug("atmel-pcm: allocating PCM playback DMA buffer\n");
		ret = atmel_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		pr_debug("atmel-pcm: allocating PCM capture DMA buffer\n");
		ret = atmel_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

static void atmel_pcm_free(struct snd_soc_component *component,
			   struct snd_pcm *pcm)
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
		dma_free_coherent(pcm->card->dev, buf->bytes,
				  buf->area, buf->addr);
		buf->area = NULL;
	}
}

/*--------------------------------------------------------------------------*\
 * Hardware definition
\*--------------------------------------------------------------------------*/
/* TODO: These values were taken from the AT91 platform driver, check
 *	 them against real values for AT32
 */
static const struct snd_pcm_hardware atmel_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 2,
	.periods_max		= 1024,
	.buffer_bytes_max	= ATMEL_SSC_DMABUF_SIZE,
};


/*--------------------------------------------------------------------------*\
 * Data types
\*--------------------------------------------------------------------------*/
struct atmel_runtime_data {
	struct atmel_pcm_dma_params *params;
	dma_addr_t dma_buffer;		/* physical address of dma buffer */
	dma_addr_t dma_buffer_end;	/* first address beyond DMA buffer */
	size_t period_size;

	dma_addr_t period_ptr;		/* physical address of next period */
};

/*--------------------------------------------------------------------------*\
 * ISR
\*--------------------------------------------------------------------------*/
static void atmel_pcm_dma_irq(u32 ssc_sr,
	struct snd_pcm_substream *substream)
{
	struct atmel_runtime_data *prtd = substream->runtime->private_data;
	struct atmel_pcm_dma_params *params = prtd->params;
	static int count;

	count++;

	if (ssc_sr & params->mask->ssc_endbuf) {
		pr_warn("atmel-pcm: buffer %s on %s (SSC_SR=%#x, count=%d)\n",
				substream->stream == SNDRV_PCM_STREAM_PLAYBACK
				? "underrun" : "overrun",
				params->name, ssc_sr, count);

		/* re-start the PDC */
		ssc_writex(params->ssc->regs, ATMEL_PDC_PTCR,
			   params->mask->pdc_disable);
		prtd->period_ptr += prtd->period_size;
		if (prtd->period_ptr >= prtd->dma_buffer_end)
			prtd->period_ptr = prtd->dma_buffer;

		ssc_writex(params->ssc->regs, params->pdc->xpr,
			   prtd->period_ptr);
		ssc_writex(params->ssc->regs, params->pdc->xcr,
			   prtd->period_size / params->pdc_xfer_size);
		ssc_writex(params->ssc->regs, ATMEL_PDC_PTCR,
			   params->mask->pdc_enable);
	}

	if (ssc_sr & params->mask->ssc_endx) {
		/* Load the PDC next pointer and counter registers */
		prtd->period_ptr += prtd->period_size;
		if (prtd->period_ptr >= prtd->dma_buffer_end)
			prtd->period_ptr = prtd->dma_buffer;

		ssc_writex(params->ssc->regs, params->pdc->xnpr,
			   prtd->period_ptr);
		ssc_writex(params->ssc->regs, params->pdc->xncr,
			   prtd->period_size / params->pdc_xfer_size);
	}

	snd_pcm_period_elapsed(substream);
}


/*--------------------------------------------------------------------------*\
 * PCM operations
\*--------------------------------------------------------------------------*/
static int atmel_pcm_hw_params(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct atmel_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	/* this may get called several times by oss emulation
	 * with different params */

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	prtd->params = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);
	prtd->params->dma_intr_handler = atmel_pcm_dma_irq;

	prtd->dma_buffer = runtime->dma_addr;
	prtd->dma_buffer_end = runtime->dma_addr + runtime->dma_bytes;
	prtd->period_size = params_period_bytes(params);

	pr_debug("atmel-pcm: "
		"hw_params: DMA for %s initialized "
		"(dma_bytes=%zu, period_size=%zu)\n",
		prtd->params->name,
		runtime->dma_bytes,
		prtd->period_size);
	return 0;
}

static int atmel_pcm_hw_free(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct atmel_runtime_data *prtd = substream->runtime->private_data;
	struct atmel_pcm_dma_params *params = prtd->params;

	if (params != NULL) {
		ssc_writex(params->ssc->regs, SSC_PDC_PTCR,
			   params->mask->pdc_disable);
		prtd->params->dma_intr_handler = NULL;
	}

	return 0;
}

static int atmel_pcm_prepare(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream)
{
	struct atmel_runtime_data *prtd = substream->runtime->private_data;
	struct atmel_pcm_dma_params *params = prtd->params;

	ssc_writex(params->ssc->regs, SSC_IDR,
		   params->mask->ssc_endx | params->mask->ssc_endbuf);
	ssc_writex(params->ssc->regs, ATMEL_PDC_PTCR,
		   params->mask->pdc_disable);
	return 0;
}

static int atmel_pcm_trigger(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *rtd = substream->runtime;
	struct atmel_runtime_data *prtd = rtd->private_data;
	struct atmel_pcm_dma_params *params = prtd->params;
	int ret = 0;

	pr_debug("atmel-pcm:buffer_size = %ld,"
		"dma_area = %p, dma_bytes = %zu\n",
		rtd->buffer_size, rtd->dma_area, rtd->dma_bytes);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		prtd->period_ptr = prtd->dma_buffer;

		ssc_writex(params->ssc->regs, params->pdc->xpr,
			   prtd->period_ptr);
		ssc_writex(params->ssc->regs, params->pdc->xcr,
			   prtd->period_size / params->pdc_xfer_size);

		prtd->period_ptr += prtd->period_size;
		ssc_writex(params->ssc->regs, params->pdc->xnpr,
			   prtd->period_ptr);
		ssc_writex(params->ssc->regs, params->pdc->xncr,
			   prtd->period_size / params->pdc_xfer_size);

		pr_debug("atmel-pcm: trigger: "
			"period_ptr=%lx, xpr=%u, "
			"xcr=%u, xnpr=%u, xncr=%u\n",
			(unsigned long)prtd->period_ptr,
			ssc_readx(params->ssc->regs, params->pdc->xpr),
			ssc_readx(params->ssc->regs, params->pdc->xcr),
			ssc_readx(params->ssc->regs, params->pdc->xnpr),
			ssc_readx(params->ssc->regs, params->pdc->xncr));

		ssc_writex(params->ssc->regs, SSC_IER,
			   params->mask->ssc_endx | params->mask->ssc_endbuf);
		ssc_writex(params->ssc->regs, SSC_PDC_PTCR,
			   params->mask->pdc_enable);

		pr_debug("sr=%u imr=%u\n",
			ssc_readx(params->ssc->regs, SSC_SR),
			ssc_readx(params->ssc->regs, SSC_IER));
		break;		/* SNDRV_PCM_TRIGGER_START */

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ssc_writex(params->ssc->regs, ATMEL_PDC_PTCR,
			   params->mask->pdc_disable);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ssc_writex(params->ssc->regs, ATMEL_PDC_PTCR,
			   params->mask->pdc_enable);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static snd_pcm_uframes_t atmel_pcm_pointer(struct snd_soc_component *component,
					   struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct atmel_runtime_data *prtd = runtime->private_data;
	struct atmel_pcm_dma_params *params = prtd->params;
	dma_addr_t ptr;
	snd_pcm_uframes_t x;

	ptr = (dma_addr_t) ssc_readx(params->ssc->regs, params->pdc->xpr);
	x = bytes_to_frames(runtime, ptr - prtd->dma_buffer);

	if (x == runtime->buffer_size)
		x = 0;

	return x;
}

static int atmel_pcm_open(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct atmel_runtime_data *prtd;
	int ret = 0;

	snd_soc_set_runtime_hwparams(substream, &atmel_pcm_hardware);

	/* ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		goto out;

	prtd = kzalloc(sizeof(struct atmel_runtime_data), GFP_KERNEL);
	if (prtd == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	runtime->private_data = prtd;

 out:
	return ret;
}

static int atmel_pcm_close(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream)
{
	struct atmel_runtime_data *prtd = substream->runtime->private_data;

	kfree(prtd);
	return 0;
}

static const struct snd_soc_component_driver atmel_soc_platform = {
	.open		= atmel_pcm_open,
	.close		= atmel_pcm_close,
	.hw_params	= atmel_pcm_hw_params,
	.hw_free	= atmel_pcm_hw_free,
	.prepare	= atmel_pcm_prepare,
	.trigger	= atmel_pcm_trigger,
	.pointer	= atmel_pcm_pointer,
	.mmap		= atmel_pcm_mmap,
	.pcm_construct	= atmel_pcm_new,
	.pcm_destruct	= atmel_pcm_free,
};

int atmel_pcm_pdc_platform_register(struct device *dev)
{
	return devm_snd_soc_register_component(dev, &atmel_soc_platform,
					       NULL, 0);
}
EXPORT_SYMBOL(atmel_pcm_pdc_platform_register);

MODULE_AUTHOR("Sedji Gaouaou <sedji.gaouaou@atmel.com>");
MODULE_DESCRIPTION("Atmel PCM module");
MODULE_LICENSE("GPL");
