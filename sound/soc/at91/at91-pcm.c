/*
 * at91-pcm.c -- ALSA PCM interface for the Atmel AT91 SoC
 *
 * Author:	Frank Mandarino <fmandarino@endrelia.com>
 *		Endrelia Technologies Inc.
 * Created:	Mar 3, 2006
 *
 * Based on pxa2xx-pcm.c by:
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
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/atmel_pdc.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/arch/hardware.h>
#include <asm/arch/at91_ssc.h>

#include "at91-pcm.h"

#if 0
#define	DBG(x...)	printk(KERN_INFO "at91-pcm: " x)
#else
#define	DBG(x...)
#endif

static const struct snd_pcm_hardware at91_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_PAUSE,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 2,
	.periods_max		= 1024,
	.buffer_bytes_max	= 32 * 1024,
};

struct at91_runtime_data {
	struct at91_pcm_dma_params *params;
	dma_addr_t dma_buffer;			/* physical address of dma buffer */
	dma_addr_t dma_buffer_end;		/* first address beyond DMA buffer */
	size_t period_size;
	dma_addr_t period_ptr;			/* physical address of next period */
	u32 pdc_xpr_save;			/* PDC register save */
	u32 pdc_xcr_save;
	u32 pdc_xnpr_save;
	u32 pdc_xncr_save;
};

static void at91_pcm_dma_irq(u32 ssc_sr,
	struct snd_pcm_substream *substream)
{
	struct at91_runtime_data *prtd = substream->runtime->private_data;
	struct at91_pcm_dma_params *params = prtd->params;
	static int count = 0;

	count++;

	if (ssc_sr & params->mask->ssc_endbuf) {

		printk(KERN_WARNING
			"at91-pcm: buffer %s on %s (SSC_SR=%#x, count=%d)\n",
			substream->stream == SNDRV_PCM_STREAM_PLAYBACK
				? "underrun" : "overrun",
			params->name, ssc_sr, count);

		/* re-start the PDC */
		at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_disable);

		prtd->period_ptr += prtd->period_size;
		if (prtd->period_ptr >= prtd->dma_buffer_end) {
			prtd->period_ptr = prtd->dma_buffer;
		}

		at91_ssc_write(params->ssc_base + params->pdc->xpr, prtd->period_ptr);
		at91_ssc_write(params->ssc_base + params->pdc->xcr,
				prtd->period_size / params->pdc_xfer_size);

		at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_enable);
	}

	if (ssc_sr & params->mask->ssc_endx) {

		/* Load the PDC next pointer and counter registers */
		prtd->period_ptr += prtd->period_size;
		if (prtd->period_ptr >= prtd->dma_buffer_end) {
			prtd->period_ptr = prtd->dma_buffer;
		}
		at91_ssc_write(params->ssc_base + params->pdc->xnpr, prtd->period_ptr);
		at91_ssc_write(params->ssc_base + params->pdc->xncr,
				prtd->period_size / params->pdc_xfer_size);
	}

	snd_pcm_period_elapsed(substream);
}

static int at91_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct at91_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	/* this may get called several times by oss emulation
	 * with different params */

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	prtd->params = rtd->dai->cpu_dai->dma_data;
	prtd->params->dma_intr_handler = at91_pcm_dma_irq;

	prtd->dma_buffer = runtime->dma_addr;
	prtd->dma_buffer_end = runtime->dma_addr + runtime->dma_bytes;
	prtd->period_size = params_period_bytes(params);

	DBG("hw_params: DMA for %s initialized (dma_bytes=%d, period_size=%d)\n",
		prtd->params->name, runtime->dma_bytes, prtd->period_size);
	return 0;
}

static int at91_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct at91_runtime_data *prtd = substream->runtime->private_data;
	struct at91_pcm_dma_params *params = prtd->params;

	if (params != NULL) {
		at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_disable);
		prtd->params->dma_intr_handler = NULL;
	}

	return 0;
}

static int at91_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct at91_runtime_data *prtd = substream->runtime->private_data;
	struct at91_pcm_dma_params *params = prtd->params;

	at91_ssc_write(params->ssc_base + AT91_SSC_IDR,
			params->mask->ssc_endx | params->mask->ssc_endbuf);

	at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_disable);
	return 0;
}

static int at91_pcm_trigger(struct snd_pcm_substream *substream,
	int cmd)
{
	struct at91_runtime_data *prtd = substream->runtime->private_data;
	struct at91_pcm_dma_params *params = prtd->params;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		prtd->period_ptr = prtd->dma_buffer;

		at91_ssc_write(params->ssc_base + params->pdc->xpr, prtd->period_ptr);
		at91_ssc_write(params->ssc_base + params->pdc->xcr,
				prtd->period_size / params->pdc_xfer_size);

		prtd->period_ptr += prtd->period_size;
		at91_ssc_write(params->ssc_base + params->pdc->xnpr, prtd->period_ptr);
		at91_ssc_write(params->ssc_base + params->pdc->xncr,
				prtd->period_size / params->pdc_xfer_size);

		DBG("trigger: period_ptr=%lx, xpr=%lx, xcr=%ld, xnpr=%lx, xncr=%ld\n",
			(unsigned long) prtd->period_ptr,
			at91_ssc_read(params->ssc_base + params->pdc->xpr),
			at91_ssc_read(params->ssc_base + params->pdc->xcr),
			at91_ssc_read(params->ssc_base + params->pdc->xnpr),
			at91_ssc_read(params->ssc_base + params->pdc->xncr));

		at91_ssc_write(params->ssc_base + AT91_SSC_IER,
			params->mask->ssc_endx | params->mask->ssc_endbuf);

		at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_enable);

		DBG("sr=%lx imr=%lx\n", at91_ssc_read(params->ssc_base + AT91_SSC_SR),
					at91_ssc_read(params->ssc_base + AT91_SSC_IER));
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_disable);
		break;

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_enable);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static snd_pcm_uframes_t at91_pcm_pointer(
	struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct at91_runtime_data *prtd = runtime->private_data;
	struct at91_pcm_dma_params *params = prtd->params;
	dma_addr_t ptr;
	snd_pcm_uframes_t x;

	ptr = (dma_addr_t) at91_ssc_read(params->ssc_base + params->pdc->xpr);
	x = bytes_to_frames(runtime, ptr - prtd->dma_buffer);

	if (x == runtime->buffer_size)
		x = 0;
	return x;
}

static int at91_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct at91_runtime_data *prtd;
	int ret = 0;

	snd_soc_set_runtime_hwparams(substream, &at91_pcm_hardware);

	/* ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		goto out;

	prtd = kzalloc(sizeof(struct at91_runtime_data), GFP_KERNEL);
	if (prtd == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	runtime->private_data = prtd;

 out:
	return ret;
}

static int at91_pcm_close(struct snd_pcm_substream *substream)
{
	struct at91_runtime_data *prtd = substream->runtime->private_data;

	kfree(prtd);
	return 0;
}

static int at91_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

struct snd_pcm_ops at91_pcm_ops = {
	.open		= at91_pcm_open,
	.close		= at91_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= at91_pcm_hw_params,
	.hw_free	= at91_pcm_hw_free,
	.prepare	= at91_pcm_prepare,
	.trigger	= at91_pcm_trigger,
	.pointer	= at91_pcm_pointer,
	.mmap		= at91_pcm_mmap,
};

static int at91_pcm_preallocate_dma_buffer(struct snd_pcm *pcm,
	int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = at91_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);

	DBG("preallocate_dma_buffer: area=%p, addr=%p, size=%d\n",
		(void *) buf->area,
		(void *) buf->addr,
		size);

	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	return 0;
}

static u64 at91_pcm_dmamask = 0xffffffff;

static int at91_pcm_new(struct snd_card *card,
	struct snd_soc_codec_dai *dai, struct snd_pcm *pcm)
{
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &at91_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (dai->playback.channels_min) {
		ret = at91_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (dai->capture.channels_min) {
		ret = at91_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

static void at91_pcm_free_dma_buffers(struct snd_pcm *pcm)
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

#ifdef CONFIG_PM
static int at91_pcm_suspend(struct platform_device *pdev,
	struct snd_soc_cpu_dai *dai)
{
	struct snd_pcm_runtime *runtime = dai->runtime;
	struct at91_runtime_data *prtd;
	struct at91_pcm_dma_params *params;

	if (!runtime)
		return 0;

	prtd = runtime->private_data;
	params = prtd->params;

	/* disable the PDC and save the PDC registers */

	at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_disable);

	prtd->pdc_xpr_save  = at91_ssc_read(params->ssc_base + params->pdc->xpr);
	prtd->pdc_xcr_save  = at91_ssc_read(params->ssc_base + params->pdc->xcr);
	prtd->pdc_xnpr_save = at91_ssc_read(params->ssc_base + params->pdc->xnpr);
	prtd->pdc_xncr_save = at91_ssc_read(params->ssc_base + params->pdc->xncr);

	return 0;
}

static int at91_pcm_resume(struct platform_device *pdev,
	struct snd_soc_cpu_dai *dai)
{
	struct snd_pcm_runtime *runtime = dai->runtime;
	struct at91_runtime_data *prtd;
	struct at91_pcm_dma_params *params;

	if (!runtime)
		return 0;

	prtd = runtime->private_data;
	params = prtd->params;

	/* restore the PDC registers and enable the PDC */
	at91_ssc_write(params->ssc_base + params->pdc->xpr,  prtd->pdc_xpr_save);
	at91_ssc_write(params->ssc_base + params->pdc->xcr,  prtd->pdc_xcr_save);
	at91_ssc_write(params->ssc_base + params->pdc->xnpr, prtd->pdc_xnpr_save);
	at91_ssc_write(params->ssc_base + params->pdc->xncr, prtd->pdc_xncr_save);

	at91_ssc_write(params->ssc_base + ATMEL_PDC_PTCR, params->mask->pdc_enable);
	return 0;
}
#else
#define at91_pcm_suspend	NULL
#define at91_pcm_resume		NULL
#endif

struct snd_soc_platform at91_soc_platform = {
	.name		= "at91-audio",
	.pcm_ops 	= &at91_pcm_ops,
	.pcm_new	= at91_pcm_new,
	.pcm_free	= at91_pcm_free_dma_buffers,
	.suspend	= at91_pcm_suspend,
	.resume		= at91_pcm_resume,
};

EXPORT_SYMBOL_GPL(at91_soc_platform);

MODULE_AUTHOR("Frank Mandarino <fmandarino@endrelia.com>");
MODULE_DESCRIPTION("Atmel AT91 PCM module");
MODULE_LICENSE("GPL");
