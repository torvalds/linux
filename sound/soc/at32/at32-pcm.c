/* sound/soc/at32/at32-pcm.c
 * ASoC PCM interface for Atmel AT32 SoC
 *
 * Copyright (C) 2008 Long Range Systems
 *    Geoffrey Wossum <gwossum@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Note that this is basically a port of the sound/soc/at91-pcm.c to
 * the AVR32 kernel.  Thanks to Frank Mandarino for that code.
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

#include "at32-pcm.h"



/*--------------------------------------------------------------------------*\
 * Hardware definition
\*--------------------------------------------------------------------------*/
/* TODO: These values were taken from the AT91 platform driver, check
 *	 them against real values for AT32
 */
static const struct snd_pcm_hardware at32_pcm_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_PAUSE),

	.formats = SNDRV_PCM_FMTBIT_S16,
	.period_bytes_min = 32,
	.period_bytes_max = 8192,	/* 512 frames * 16 bytes / frame */
	.periods_min = 2,
	.periods_max = 1024,
	.buffer_bytes_max = 32 * 1024,
};



/*--------------------------------------------------------------------------*\
 * Data types
\*--------------------------------------------------------------------------*/
struct at32_runtime_data {
	struct at32_pcm_dma_params *params;
	dma_addr_t dma_buffer;	/* physical address of DMA buffer */
	dma_addr_t dma_buffer_end; /* first address beyond DMA buffer */
	size_t period_size;

	dma_addr_t period_ptr;	/* physical address of next period */
	int periods;		/* period index of period_ptr */

	/* Save PDC registers (for power management) */
	u32 pdc_xpr_save;
	u32 pdc_xcr_save;
	u32 pdc_xnpr_save;
	u32 pdc_xncr_save;
};



/*--------------------------------------------------------------------------*\
 * Helper functions
\*--------------------------------------------------------------------------*/
static int at32_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *dmabuf = &substream->dma_buffer;
	size_t size = at32_pcm_hardware.buffer_bytes_max;

	dmabuf->dev.type = SNDRV_DMA_TYPE_DEV;
	dmabuf->dev.dev = pcm->card->dev;
	dmabuf->private_data = NULL;
	dmabuf->area = dma_alloc_coherent(pcm->card->dev, size,
					  &dmabuf->addr, GFP_KERNEL);
	pr_debug("at32_pcm: preallocate_dma_buffer: "
		 "area=%p, addr=%p, size=%ld\n",
		 (void *)dmabuf->area, (void *)dmabuf->addr, size);

	if (!dmabuf->area)
		return -ENOMEM;

	dmabuf->bytes = size;
	return 0;
}



/*--------------------------------------------------------------------------*\
 * ISR
\*--------------------------------------------------------------------------*/
static void at32_pcm_dma_irq(u32 ssc_sr, struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *rtd = substream->runtime;
	struct at32_runtime_data *prtd = rtd->private_data;
	struct at32_pcm_dma_params *params = prtd->params;
	static int count;

	count++;
	if (ssc_sr & params->mask->ssc_endbuf) {
		pr_warning("at32-pcm: buffer %s on %s (SSC_SR=%#x, count=%d)\n",
			   substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			   "underrun" : "overrun", params->name, ssc_sr, count);

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
static int at32_pcm_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct at32_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	/* this may get called several times by oss emulation
	 * with different params
	 */
	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);

	prtd->params = rtd->dai->cpu_dai->dma_data;
	prtd->params->dma_intr_handler = at32_pcm_dma_irq;

	prtd->dma_buffer = runtime->dma_addr;
	prtd->dma_buffer_end = runtime->dma_addr + runtime->dma_bytes;
	prtd->period_size = params_period_bytes(params);

	pr_debug("hw_params: DMA for %s initialized "
		 "(dma_bytes=%ld, period_size=%ld)\n",
		 prtd->params->name, runtime->dma_bytes, prtd->period_size);

	return 0;
}



static int at32_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct at32_runtime_data *prtd = substream->runtime->private_data;
	struct at32_pcm_dma_params *params = prtd->params;

	if (params != NULL) {
		ssc_writex(params->ssc->regs, SSC_PDC_PTCR,
			   params->mask->pdc_disable);
		prtd->params->dma_intr_handler = NULL;
	}

	return 0;
}



static int at32_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct at32_runtime_data *prtd = substream->runtime->private_data;
	struct at32_pcm_dma_params *params = prtd->params;

	ssc_writex(params->ssc->regs, SSC_IDR,
		   params->mask->ssc_endx | params->mask->ssc_endbuf);
	ssc_writex(params->ssc->regs, ATMEL_PDC_PTCR,
		   params->mask->pdc_disable);

	return 0;
}


static int at32_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *rtd = substream->runtime;
	struct at32_runtime_data *prtd = rtd->private_data;
	struct at32_pcm_dma_params *params = prtd->params;
	int ret = 0;

	pr_debug("at32_pcm_trigger: buffer_size = %ld, "
		 "dma_area = %p, dma_bytes = %ld\n",
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

		pr_debug("trigger: period_ptr=%lx, xpr=%x, "
			 "xcr=%d, xnpr=%x, xncr=%d\n",
			 (unsigned long)prtd->period_ptr,
			 ssc_readx(params->ssc->regs, params->pdc->xpr),
			 ssc_readx(params->ssc->regs, params->pdc->xcr),
			 ssc_readx(params->ssc->regs, params->pdc->xnpr),
			 ssc_readx(params->ssc->regs, params->pdc->xncr));

		ssc_writex(params->ssc->regs, SSC_IER,
			   params->mask->ssc_endx | params->mask->ssc_endbuf);
		ssc_writex(params->ssc->regs, SSC_PDC_PTCR,
			   params->mask->pdc_enable);

		pr_debug("sr=%x, imr=%x\n",
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



static snd_pcm_uframes_t at32_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct at32_runtime_data *prtd = runtime->private_data;
	struct at32_pcm_dma_params *params = prtd->params;
	dma_addr_t ptr;
	snd_pcm_uframes_t x;

	ptr = (dma_addr_t) ssc_readx(params->ssc->regs, params->pdc->xpr);
	x = bytes_to_frames(runtime, ptr - prtd->dma_buffer);

	if (x == runtime->buffer_size)
		x = 0;

	return x;
}



static int at32_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct at32_runtime_data *prtd;
	int ret = 0;

	snd_soc_set_runtime_hwparams(substream, &at32_pcm_hardware);

	/* ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		goto out;

	prtd = kzalloc(sizeof(*prtd), GFP_KERNEL);
	if (prtd == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	runtime->private_data = prtd;


out:
	return ret;
}



static int at32_pcm_close(struct snd_pcm_substream *substream)
{
	struct at32_runtime_data *prtd = substream->runtime->private_data;

	kfree(prtd);
	return 0;
}


static int at32_pcm_mmap(struct snd_pcm_substream *substream,
			 struct vm_area_struct *vma)
{
	return remap_pfn_range(vma, vma->vm_start,
			       substream->dma_buffer.addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}



static struct snd_pcm_ops at32_pcm_ops = {
	.open = at32_pcm_open,
	.close = at32_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = at32_pcm_hw_params,
	.hw_free = at32_pcm_hw_free,
	.prepare = at32_pcm_prepare,
	.trigger = at32_pcm_trigger,
	.pointer = at32_pcm_pointer,
	.mmap = at32_pcm_mmap,
};



/*--------------------------------------------------------------------------*\
 * ASoC platform driver
\*--------------------------------------------------------------------------*/
static u64 at32_pcm_dmamask = 0xffffffff;

static int at32_pcm_new(struct snd_card *card,
			struct snd_soc_dai *dai,
			struct snd_pcm *pcm)
{
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &at32_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (dai->playback.channels_min) {
		ret = at32_pcm_preallocate_dma_buffer(
			  pcm, SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (dai->capture.channels_min) {
		pr_debug("at32-pcm: Allocating PCM capture DMA buffer\n");
		ret = at32_pcm_preallocate_dma_buffer(
			  pcm, SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}


out:
	return ret;
}



static void at32_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (substream == NULL)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;
		dma_free_coherent(pcm->card->dev, buf->bytes,
				  buf->area, buf->addr);
		buf->area = NULL;
	}
}



#ifdef CONFIG_PM
static int at32_pcm_suspend(struct platform_device *pdev,
			    struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = dai->runtime;
	struct at32_runtime_data *prtd;
	struct at32_pcm_dma_params *params;

	if (runtime == NULL)
		return 0;
	prtd = runtime->private_data;
	params = prtd->params;

	/* Disable the PDC and save the PDC registers */
	ssc_writex(params->ssc->regs, ATMEL_PDC_PTCR,
		   params->mask->pdc_disable);

	prtd->pdc_xpr_save = ssc_readx(params->ssc->regs, params->pdc->xpr);
	prtd->pdc_xcr_save = ssc_readx(params->ssc->regs, params->pdc->xcr);
	prtd->pdc_xnpr_save = ssc_readx(params->ssc->regs, params->pdc->xnpr);
	prtd->pdc_xncr_save = ssc_readx(params->ssc->regs, params->pdc->xncr);

	return 0;
}



static int at32_pcm_resume(struct platform_device *pdev,
			   struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = dai->runtime;
	struct at32_runtime_data *prtd;
	struct at32_pcm_dma_params *params;

	if (runtime == NULL)
		return 0;
	prtd = runtime->private_data;
	params = prtd->params;

	/* Restore the PDC registers and enable the PDC */
	ssc_writex(params->ssc->regs, params->pdc->xpr, prtd->pdc_xpr_save);
	ssc_writex(params->ssc->regs, params->pdc->xcr, prtd->pdc_xcr_save);
	ssc_writex(params->ssc->regs, params->pdc->xnpr, prtd->pdc_xnpr_save);
	ssc_writex(params->ssc->regs, params->pdc->xncr, prtd->pdc_xncr_save);

	ssc_writex(params->ssc->regs, ATMEL_PDC_PTCR, params->mask->pdc_enable);
	return 0;
}
#else /* CONFIG_PM */
#  define at32_pcm_suspend	NULL
#  define at32_pcm_resume	NULL
#endif /* CONFIG_PM */



struct snd_soc_platform at32_soc_platform = {
	.name = "at32-audio",
	.pcm_ops = &at32_pcm_ops,
	.pcm_new = at32_pcm_new,
	.pcm_free = at32_pcm_free_dma_buffers,
	.suspend = at32_pcm_suspend,
	.resume = at32_pcm_resume,
};
EXPORT_SYMBOL_GPL(at32_soc_platform);



MODULE_AUTHOR("Geoffrey Wossum <gwossum@acm.org>");
MODULE_DESCRIPTION("Atmel AT32 PCM module");
MODULE_LICENSE("GPL");
