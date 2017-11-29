/*
 * Copyright (c) 2010 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <mach/hardware.h>

#include "nuc900-audio.h"

static const struct snd_pcm_hardware nuc900_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_BLOCK_TRANSFER |
					SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID |
					SNDRV_PCM_INFO_PAUSE |
					SNDRV_PCM_INFO_RESUME,
	.buffer_bytes_max	= 4*1024,
	.period_bytes_min	= 1*1024,
	.period_bytes_max	= 4*1024,
	.periods_min		= 1,
	.periods_max		= 1024,
};

static int nuc900_dma_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static void nuc900_update_dma_register(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc900_audio *nuc900_audio = runtime->private_data;
	void __iomem *mmio_addr, *mmio_len;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		mmio_addr = nuc900_audio->mmio + ACTL_PDSTB;
		mmio_len = nuc900_audio->mmio + ACTL_PDST_LENGTH;
	} else {
		mmio_addr = nuc900_audio->mmio + ACTL_RDSTB;
		mmio_len = nuc900_audio->mmio + ACTL_RDST_LENGTH;
	}

	AUDIO_WRITE(mmio_addr, runtime->dma_addr);
	AUDIO_WRITE(mmio_len, runtime->dma_bytes);
}

static void nuc900_dma_start(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc900_audio *nuc900_audio = runtime->private_data;
	unsigned long val;

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_CON);
	val |= (T_DMA_IRQ | R_DMA_IRQ);
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_CON, val);
}

static void nuc900_dma_stop(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc900_audio *nuc900_audio = runtime->private_data;
	unsigned long val;

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_CON);
	val &= ~(T_DMA_IRQ | R_DMA_IRQ);
	AUDIO_WRITE(nuc900_audio->mmio + ACTL_CON, val);
}

static irqreturn_t nuc900_dma_interrupt(int irq, void *dev_id)
{
	struct snd_pcm_substream *substream = dev_id;
	struct nuc900_audio *nuc900_audio = substream->runtime->private_data;
	unsigned long val;

	spin_lock(&nuc900_audio->lock);

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_CON);

	if (val & R_DMA_IRQ) {
		AUDIO_WRITE(nuc900_audio->mmio + ACTL_CON, val | R_DMA_IRQ);

		val = AUDIO_READ(nuc900_audio->mmio + ACTL_RSR);

		if (val & R_DMA_MIDDLE_IRQ) {
			val |= R_DMA_MIDDLE_IRQ;
			AUDIO_WRITE(nuc900_audio->mmio + ACTL_RSR, val);
		}

		if (val & R_DMA_END_IRQ) {
			val |= R_DMA_END_IRQ;
			AUDIO_WRITE(nuc900_audio->mmio + ACTL_RSR, val);
		}
	} else if (val & T_DMA_IRQ) {
		AUDIO_WRITE(nuc900_audio->mmio + ACTL_CON, val | T_DMA_IRQ);

		val = AUDIO_READ(nuc900_audio->mmio + ACTL_PSR);

		if (val & P_DMA_MIDDLE_IRQ) {
			val |= P_DMA_MIDDLE_IRQ;
			AUDIO_WRITE(nuc900_audio->mmio + ACTL_PSR, val);
		}

		if (val & P_DMA_END_IRQ) {
			val |= P_DMA_END_IRQ;
			AUDIO_WRITE(nuc900_audio->mmio + ACTL_PSR, val);
		}
	} else {
		dev_err(nuc900_audio->dev, "Wrong DMA interrupt status!\n");
		spin_unlock(&nuc900_audio->lock);
		return IRQ_HANDLED;
	}

	spin_unlock(&nuc900_audio->lock);

	snd_pcm_period_elapsed(substream);

	return IRQ_HANDLED;
}

static int nuc900_dma_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int nuc900_dma_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc900_audio *nuc900_audio = runtime->private_data;
	unsigned long flags, val;
	int ret = 0;

	spin_lock_irqsave(&nuc900_audio->lock, flags);

	nuc900_update_dma_register(substream);

	val = AUDIO_READ(nuc900_audio->mmio + ACTL_RESET);

	switch (runtime->channels) {
	case 1:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			val &= ~(PLAY_LEFT_CHNNEL | PLAY_RIGHT_CHNNEL);
			val |= PLAY_RIGHT_CHNNEL;
		} else {
			val &= ~(RECORD_LEFT_CHNNEL | RECORD_RIGHT_CHNNEL);
			val |= RECORD_RIGHT_CHNNEL;
		}
		AUDIO_WRITE(nuc900_audio->mmio + ACTL_RESET, val);
		break;
	case 2:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			val |= (PLAY_LEFT_CHNNEL | PLAY_RIGHT_CHNNEL);
		else
			val |= (RECORD_LEFT_CHNNEL | RECORD_RIGHT_CHNNEL);
		AUDIO_WRITE(nuc900_audio->mmio + ACTL_RESET, val);
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&nuc900_audio->lock, flags);
	return ret;
}

static int nuc900_dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		nuc900_dma_start(substream);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		nuc900_dma_stop(substream);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int nuc900_dma_getposition(struct snd_pcm_substream *substream,
					dma_addr_t *src, dma_addr_t *dst)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc900_audio *nuc900_audio = runtime->private_data;

	if (src != NULL)
		*src = AUDIO_READ(nuc900_audio->mmio + ACTL_PDSTC);

	if (dst != NULL)
		*dst = AUDIO_READ(nuc900_audio->mmio + ACTL_RDSTC);

	return 0;
}

static snd_pcm_uframes_t nuc900_dma_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	dma_addr_t src, dst;
	unsigned long res;

	nuc900_dma_getposition(substream, &src, &dst);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		res = dst - runtime->dma_addr;
	else
		res = src - runtime->dma_addr;

	return bytes_to_frames(substream->runtime, res);
}

static int nuc900_dma_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc900_audio *nuc900_audio;

	snd_soc_set_runtime_hwparams(substream, &nuc900_pcm_hardware);

	nuc900_audio = nuc900_ac97_data;

	if (request_irq(nuc900_audio->irq_num, nuc900_dma_interrupt,
			0, "nuc900-dma", substream))
		return -EBUSY;

	runtime->private_data = nuc900_audio;

	return 0;
}

static int nuc900_dma_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nuc900_audio *nuc900_audio = runtime->private_data;

	free_irq(nuc900_audio->irq_num, substream);

	return 0;
}

static int nuc900_dma_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_wc(substream->pcm->card->dev, vma, runtime->dma_area,
			   runtime->dma_addr, runtime->dma_bytes);
}

static const struct snd_pcm_ops nuc900_dma_ops = {
	.open		= nuc900_dma_open,
	.close		= nuc900_dma_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= nuc900_dma_hw_params,
	.hw_free	= nuc900_dma_hw_free,
	.prepare	= nuc900_dma_prepare,
	.trigger	= nuc900_dma_trigger,
	.pointer	= nuc900_dma_pointer,
	.mmap		= nuc900_dma_mmap,
};

static int nuc900_dma_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret;

	ret = dma_coerce_mask_and_coherent(card->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
		card->dev, 4 * 1024, (4 * 1024) - 1);

	return 0;
}

static const struct snd_soc_platform_driver nuc900_soc_platform = {
	.ops		= &nuc900_dma_ops,
	.pcm_new	= nuc900_dma_new,
};

static int nuc900_soc_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_platform(&pdev->dev, &nuc900_soc_platform);
}

static struct platform_driver nuc900_pcm_driver = {
	.driver = {
			.name = "nuc900-pcm-audio",
	},

	.probe = nuc900_soc_platform_probe,
};

module_platform_driver(nuc900_pcm_driver);

MODULE_AUTHOR("Wan ZongShun, <mcuos.com@gmail.com>");
MODULE_DESCRIPTION("nuc900 Audio DMA module");
MODULE_LICENSE("GPL");
