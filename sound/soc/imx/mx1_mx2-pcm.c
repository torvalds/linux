/*
 * mx1_mx2-pcm.c -- ALSA SoC interface for Freescale i.MX1x, i.MX2x CPUs
 *
 * Copyright 2009 Vista Silicon S.L.
 * Author: Javier Martin
 *         javier.martin@vista-silicon.com
 *
 * Based on mxc-pcm.c by Liam Girdwood.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
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
#include <mach/hardware.h>
#include <mach/dma-mx1-mx2.h>

#include "mx1_mx2-pcm.h"


static const struct snd_pcm_hardware mx1_mx2_pcm_hardware = {
	.info			= (SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP |
				   SNDRV_PCM_INFO_MMAP_VALID),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.buffer_bytes_max	= 32 * 1024,
	.period_bytes_min	= 64,
	.period_bytes_max	= 8 * 1024,
	.periods_min		= 2,
	.periods_max		= 255,
	.fifo_size		= 0,
};

struct mx1_mx2_runtime_data {
	int dma_ch;
	int active;
	unsigned int period;
	unsigned int periods;
	int tx_spin;
	spinlock_t dma_lock;
	struct mx1_mx2_pcm_dma_params *dma_params;
};


/**
  * This function stops the current dma transfer for playback
  * and clears the dma pointers.
  *
  * @param	substream	pointer to the structure of the current stream.
  *
  */
static int audio_stop_dma(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mx1_mx2_runtime_data *prtd = runtime->private_data;
	unsigned long flags;

	spin_lock_irqsave(&prtd->dma_lock, flags);

	pr_debug("%s\n", __func__);

	prtd->active = 0;
	prtd->period = 0;
	prtd->periods = 0;

	/* this stops the dma channel and clears the buffer ptrs */

	imx_dma_disable(prtd->dma_ch);

	spin_unlock_irqrestore(&prtd->dma_lock, flags);

	return 0;
}

/**
  * This function is called whenever a new audio block needs to be
  * transferred to the codec. The function receives the address and the size
  * of the new block and start a new DMA transfer.
  *
  * @param	substream	pointer to the structure of the current stream.
  *
  */
static int dma_new_period(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime =  substream->runtime;
	struct mx1_mx2_runtime_data *prtd = runtime->private_data;
	unsigned int dma_size;
	unsigned int offset;
	int ret = 0;
	dma_addr_t mem_addr;
	unsigned int dev_addr;

	if (prtd->active) {
		dma_size = frames_to_bytes(runtime, runtime->period_size);
		offset = dma_size * prtd->period;

		pr_debug("%s: period (%d) out of (%d)\n", __func__,
			prtd->period,
			runtime->periods);
		pr_debug("period_size %d frames\n offset %d bytes\n",
			(unsigned int)runtime->period_size,
			offset);
		pr_debug("dma_size %d bytes\n", dma_size);

		snd_BUG_ON(dma_size > mx1_mx2_pcm_hardware.period_bytes_max);

		mem_addr = (dma_addr_t)(runtime->dma_addr + offset);
		dev_addr = prtd->dma_params->per_address;
		pr_debug("%s: mem_addr is %x\n dev_addr is %x\n",
				 __func__, mem_addr, dev_addr);

		ret = imx_dma_setup_single(prtd->dma_ch, mem_addr,
					dma_size, dev_addr,
					prtd->dma_params->transfer_type);
		if (ret < 0) {
			printk(KERN_ERR "Error %d configuring DMA\n", ret);
			return ret;
		}
		imx_dma_enable(prtd->dma_ch);

		pr_debug("%s: transfer enabled\nmem_addr = %x\n",
			__func__, (unsigned int) mem_addr);
		pr_debug("dev_addr = %x\ndma_size = %d\n",
			(unsigned int) dev_addr, dma_size);

		prtd->tx_spin = 1; /* FGA little trick to retrieve DMA pos */
		prtd->period++;
		prtd->period %= runtime->periods;
    }
	return ret;
}


/**
  * This is a callback which will be called
  * when a TX transfer finishes. The call occurs
  * in interrupt context.
  *
  * @param	dat	pointer to the structure of the current stream.
  *
  */
static void audio_dma_irq(int channel, void *data)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	struct mx1_mx2_runtime_data *prtd;
	unsigned int dma_size;
	unsigned int previous_period;
	unsigned int offset;

	substream = data;
	runtime = substream->runtime;
	prtd = runtime->private_data;
	previous_period  = prtd->periods;
	dma_size = frames_to_bytes(runtime, runtime->period_size);
	offset = dma_size * previous_period;

	prtd->tx_spin = 0;
	prtd->periods++;
	prtd->periods %= runtime->periods;

	pr_debug("%s: irq per %d offset %x\n", __func__, prtd->periods, offset);

	/*
	  * If we are getting a callback for an active stream then we inform
	  * the PCM middle layer we've finished a period
	  */
	if (prtd->active)
		snd_pcm_period_elapsed(substream);

	/*
	  * Trig next DMA transfer
	  */
	dma_new_period(substream);
}

/**
  * This function configures the hardware to allow audio
  * playback operations. It is called by ALSA framework.
  *
  * @param	substream	pointer to the structure of the current stream.
  *
  * @return              0 on success, -1 otherwise.
  */
static int
snd_mx1_mx2_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime =  substream->runtime;
	struct mx1_mx2_runtime_data *prtd = runtime->private_data;

	prtd->period = 0;
	prtd->periods = 0;

	return 0;
}

static int mx1_mx2_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *hw_params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	ret = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	if (ret < 0) {
		printk(KERN_ERR "%s: Error %d failed to malloc pcm pages \n",
		__func__, ret);
		return ret;
	}

	pr_debug("%s: snd_imx1_mx2_audio_hw_params runtime->dma_addr 0x(%x)\n",
		__func__, (unsigned int)runtime->dma_addr);
	pr_debug("%s: snd_imx1_mx2_audio_hw_params runtime->dma_area 0x(%x)\n",
		__func__, (unsigned int)runtime->dma_area);
	pr_debug("%s: snd_imx1_mx2_audio_hw_params runtime->dma_bytes 0x(%x)\n",
		__func__, (unsigned int)runtime->dma_bytes);

	return ret;
}

static int mx1_mx2_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mx1_mx2_runtime_data *prtd = runtime->private_data;

	imx_dma_free(prtd->dma_ch);

	snd_pcm_lib_free_pages(substream);

	return 0;
}

static int mx1_mx2_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct mx1_mx2_runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		prtd->tx_spin = 0;
		/* requested stream startup */
		prtd->active = 1;
		pr_debug("%s: starting dma_new_period\n", __func__);
		ret = dma_new_period(substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		/* requested stream shutdown */
		pr_debug("%s: stopping dma transfer\n", __func__);
		ret = audio_stop_dma(substream);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static snd_pcm_uframes_t
mx1_mx2_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mx1_mx2_runtime_data *prtd = runtime->private_data;
	unsigned int offset = 0;

	/* tx_spin value is used here to check if a transfer is active */
	if (prtd->tx_spin) {
		offset = (runtime->period_size * (prtd->periods)) +
						(runtime->period_size >> 1);
		if (offset >= runtime->buffer_size)
			offset = runtime->period_size >> 1;
	} else {
		offset = (runtime->period_size * (prtd->periods));
		if (offset >= runtime->buffer_size)
			offset = 0;
	}
	pr_debug("%s: pointer offset %x\n", __func__, offset);

	return offset;
}

static int mx1_mx2_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mx1_mx2_runtime_data *prtd;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mx1_mx2_pcm_dma_params *dma_data = rtd->dai->cpu_dai->dma_data;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &mx1_mx2_pcm_hardware);

	ret = snd_pcm_hw_constraint_integer(runtime,
					SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	prtd = kzalloc(sizeof(struct mx1_mx2_runtime_data), GFP_KERNEL);
	if (prtd == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	runtime->private_data = prtd;

	if (!dma_data)
		return -ENODEV;

	prtd->dma_params = dma_data;

	pr_debug("%s: Requesting dma channel (%s)\n", __func__,
						prtd->dma_params->name);
	ret = imx_dma_request_by_prio(prtd->dma_params->name, DMA_PRIO_HIGH);
	if (ret < 0) {
		printk(KERN_ERR "Error %d requesting dma channel\n", ret);
		return ret;
	}
	prtd->dma_ch = ret;
	imx_dma_config_burstlen(prtd->dma_ch,
				prtd->dma_params->watermark_level);

	ret = imx_dma_config_channel(prtd->dma_ch,
			prtd->dma_params->per_config,
			prtd->dma_params->mem_config,
			prtd->dma_params->event_id, 0);

	if (ret) {
		pr_debug(KERN_ERR "Error %d configuring dma channel %d\n",
			ret, prtd->dma_ch);
		return ret;
	}

	pr_debug("%s: Setting tx dma callback function\n", __func__);
	ret = imx_dma_setup_handlers(prtd->dma_ch,
				audio_dma_irq, NULL,
				(void *)substream);
	if (ret < 0) {
		printk(KERN_ERR "Error %d setting dma callback function\n", ret);
		return ret;
	}
	return 0;

 out:
	return ret;
}

static int mx1_mx2_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mx1_mx2_runtime_data *prtd = runtime->private_data;

	kfree(prtd);

	return 0;
}

static int mx1_mx2_pcm_mmap(struct snd_pcm_substream *substream,
				struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops mx1_mx2_pcm_ops = {
	.open		= mx1_mx2_pcm_open,
	.close		= mx1_mx2_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= mx1_mx2_pcm_hw_params,
	.hw_free	= mx1_mx2_pcm_hw_free,
	.prepare	= snd_mx1_mx2_prepare,
	.trigger	= mx1_mx2_pcm_trigger,
	.pointer	= mx1_mx2_pcm_pointer,
	.mmap		= mx1_mx2_pcm_mmap,
};

static u64 mx1_mx2_pcm_dmamask = 0xffffffff;

static int mx1_mx2_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = mx1_mx2_pcm_hardware.buffer_bytes_max;
	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = pcm->card->dev;
	buf->private_data = NULL;

	/* Reserve uncached-buffered memory area for DMA */
	buf->area = dma_alloc_writecombine(pcm->card->dev, size,
					   &buf->addr, GFP_KERNEL);

	pr_debug("%s: preallocate_dma_buffer: area=%p, addr=%p, size=%d\n",
		__func__, (void *) buf->area, (void *) buf->addr, size);

	if (!buf->area)
		return -ENOMEM;

	buf->bytes = size;
	return 0;
}

static void mx1_mx2_pcm_free_dma_buffers(struct snd_pcm *pcm)
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

static int mx1_mx2_pcm_new(struct snd_card *card, struct snd_soc_dai *dai,
	struct snd_pcm *pcm)
{
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &mx1_mx2_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (dai->playback.channels_min) {
		ret = mx1_mx2_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		pr_debug("%s: preallocate playback buffer\n", __func__);
		if (ret)
			goto out;
	}

	if (dai->capture.channels_min) {
		ret = mx1_mx2_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		pr_debug("%s: preallocate capture buffer\n", __func__);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

struct snd_soc_platform mx1_mx2_soc_platform = {
	.name		= "mx1_mx2-audio",
	.pcm_ops 	= &mx1_mx2_pcm_ops,
	.pcm_new	= mx1_mx2_pcm_new,
	.pcm_free	= mx1_mx2_pcm_free_dma_buffers,
};
EXPORT_SYMBOL_GPL(mx1_mx2_soc_platform);

static int __init mx1_mx2_soc_platform_init(void)
{
	return snd_soc_register_platform(&mx1_mx2_soc_platform);
}
module_init(mx1_mx2_soc_platform_init);

static void __exit mx1_mx2_soc_platform_exit(void)
{
	snd_soc_unregister_platform(&mx1_mx2_soc_platform);
}
module_exit(mx1_mx2_soc_platform_exit);

MODULE_AUTHOR("Javier Martin, javier.martin@vista-silicon.com");
MODULE_DESCRIPTION("Freescale i.MX2x, i.MX1x PCM DMA module");
MODULE_LICENSE("GPL");
