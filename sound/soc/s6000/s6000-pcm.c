/*
 * ALSA PCM interface for the Stetch s6000 family
 *
 * Author:      Daniel Gloeckner, <dg@emlix.com>
 * Copyright:   (C) 2009 emlix GmbH <info@emlix.com>
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
#include <linux/interrupt.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/dma.h>
#include <variant/dmac.h>

#include "s6000-pcm.h"

#define S6_PCM_PREALLOCATE_SIZE (96 * 1024)
#define S6_PCM_PREALLOCATE_MAX  (2048 * 1024)

static struct snd_pcm_hardware s6000_pcm_hardware = {
	.info = (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_JOINT_DUPLEX),
	.formats = (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE),
	.rates = (SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_5512 | \
		  SNDRV_PCM_RATE_8000_192000),
	.rate_min = 0,
	.rate_max = 1562500,
	.channels_min = 2,
	.channels_max = 8,
	.buffer_bytes_max = 0x7ffffff0,
	.period_bytes_min = 16,
	.period_bytes_max = 0xfffff0,
	.periods_min = 2,
	.periods_max = 1024, /* no limit */
	.fifo_size = 0,
};

struct s6000_runtime_data {
	spinlock_t lock;
	int period;		/* current DMA period */
};

static void s6000_pcm_enqueue_dma(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct s6000_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct s6000_pcm_dma_params *par;
	int channel;
	unsigned int period_size;
	unsigned int dma_offset;
	dma_addr_t dma_pos;
	dma_addr_t src, dst;

	par = snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);

	period_size = snd_pcm_lib_period_bytes(substream);
	dma_offset = prtd->period * period_size;
	dma_pos = runtime->dma_addr + dma_offset;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		src = dma_pos;
		dst = par->sif_out;
		channel = par->dma_out;
	} else {
		src = par->sif_in;
		dst = dma_pos;
		channel = par->dma_in;
	}

	if (!s6dmac_channel_enabled(DMA_MASK_DMAC(channel),
				    DMA_INDEX_CHNL(channel)))
		return;

	if (s6dmac_fifo_full(DMA_MASK_DMAC(channel), DMA_INDEX_CHNL(channel))) {
		printk(KERN_ERR "s6000-pcm: fifo full\n");
		return;
	}

	BUG_ON(period_size & 15);
	s6dmac_put_fifo(DMA_MASK_DMAC(channel), DMA_INDEX_CHNL(channel),
			src, dst, period_size);

	prtd->period++;
	if (unlikely(prtd->period >= runtime->periods))
		prtd->period = 0;
}

static irqreturn_t s6000_pcm_irq(int irq, void *data)
{
	struct snd_pcm *pcm = data;
	struct snd_soc_pcm_runtime *runtime = pcm->private_data;
	struct s6000_runtime_data *prtd;
	unsigned int has_xrun;
	int i, ret = IRQ_NONE;

	for (i = 0; i < 2; ++i) {
		struct snd_pcm_substream *substream = pcm->streams[i].substream;
		struct s6000_pcm_dma_params *params =
					snd_soc_dai_get_dma_data(runtime->cpu_dai, substream);
		u32 channel;
		unsigned int pending;

		if (substream == SNDRV_PCM_STREAM_PLAYBACK)
			channel = params->dma_out;
		else
			channel = params->dma_in;

		has_xrun = params->check_xrun(runtime->cpu_dai);

		if (!channel)
			continue;

		if (unlikely(has_xrun & (1 << i)) &&
		    substream->runtime &&
		    snd_pcm_running(substream)) {
			dev_dbg(pcm->dev, "xrun\n");
			snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
			ret = IRQ_HANDLED;
		}

		pending = s6dmac_int_sources(DMA_MASK_DMAC(channel),
					     DMA_INDEX_CHNL(channel));

		if (pending & 1) {
			ret = IRQ_HANDLED;
			if (likely(substream->runtime &&
				   snd_pcm_running(substream))) {
				snd_pcm_period_elapsed(substream);
				dev_dbg(pcm->dev, "period elapsed %x %x\n",
				       s6dmac_cur_src(DMA_MASK_DMAC(channel),
						   DMA_INDEX_CHNL(channel)),
				       s6dmac_cur_dst(DMA_MASK_DMAC(channel),
						   DMA_INDEX_CHNL(channel)));
				prtd = substream->runtime->private_data;
				spin_lock(&prtd->lock);
				s6000_pcm_enqueue_dma(substream);
				spin_unlock(&prtd->lock);
			}
		}

		if (unlikely(pending & ~7)) {
			if (pending & (1 << 3))
				printk(KERN_WARNING
				       "s6000-pcm: DMA %x Underflow\n",
				       channel);
			if (pending & (1 << 4))
				printk(KERN_WARNING
				       "s6000-pcm: DMA %x Overflow\n",
				       channel);
			if (pending & 0x1e0)
				printk(KERN_WARNING
				       "s6000-pcm: DMA %x Master Error "
				       "(mask %x)\n",
				       channel, pending >> 5);

		}
	}

	return ret;
}

static int s6000_pcm_start(struct snd_pcm_substream *substream)
{
	struct s6000_runtime_data *prtd = substream->runtime->private_data;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct s6000_pcm_dma_params *par;
	unsigned long flags;
	int srcinc;
	u32 dma;

	par = snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);

	spin_lock_irqsave(&prtd->lock, flags);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		srcinc = 1;
		dma = par->dma_out;
	} else {
		srcinc = 0;
		dma = par->dma_in;
	}
	s6dmac_enable_chan(DMA_MASK_DMAC(dma), DMA_INDEX_CHNL(dma),
			   1 /* priority 1 (0 is max) */,
			   0 /* peripheral requests w/o xfer length mode */,
			   srcinc /* source address increment */,
			   srcinc^1 /* destination address increment */,
			   0 /* chunksize 0 (skip impossible on this dma) */,
			   0 /* source skip after chunk (impossible) */,
			   0 /* destination skip after chunk (impossible) */,
			   4 /* 16 byte burst size */,
			   -1 /* don't conserve bandwidth */,
			   0 /* low watermark irq descriptor threshold */,
			   0 /* disable hardware timestamps */,
			   1 /* enable channel */);

	s6000_pcm_enqueue_dma(substream);
	s6000_pcm_enqueue_dma(substream);

	spin_unlock_irqrestore(&prtd->lock, flags);

	return 0;
}

static int s6000_pcm_stop(struct snd_pcm_substream *substream)
{
	struct s6000_runtime_data *prtd = substream->runtime->private_data;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct s6000_pcm_dma_params *par;
	unsigned long flags;
	u32 channel;

	par = snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		channel = par->dma_out;
	else
		channel = par->dma_in;

	s6dmac_set_terminal_count(DMA_MASK_DMAC(channel),
				  DMA_INDEX_CHNL(channel), 0);

	spin_lock_irqsave(&prtd->lock, flags);

	s6dmac_disable_chan(DMA_MASK_DMAC(channel), DMA_INDEX_CHNL(channel));

	spin_unlock_irqrestore(&prtd->lock, flags);

	return 0;
}

static int s6000_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct s6000_pcm_dma_params *par;
	int ret;

	par = snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);

	ret = par->trigger(substream, cmd, 0);
	if (ret < 0)
		return ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = s6000_pcm_start(substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = s6000_pcm_stop(substream);
		break;
	default:
		ret = -EINVAL;
	}
	if (ret < 0)
		return ret;

	return par->trigger(substream, cmd, 1);
}

static int s6000_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct s6000_runtime_data *prtd = substream->runtime->private_data;

	prtd->period = 0;

	return 0;
}

static snd_pcm_uframes_t s6000_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct s6000_pcm_dma_params *par;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct s6000_runtime_data *prtd = runtime->private_data;
	unsigned long flags;
	unsigned int offset;
	dma_addr_t count;

	par = snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);

	spin_lock_irqsave(&prtd->lock, flags);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		count = s6dmac_cur_src(DMA_MASK_DMAC(par->dma_out),
				       DMA_INDEX_CHNL(par->dma_out));
	else
		count = s6dmac_cur_dst(DMA_MASK_DMAC(par->dma_in),
				       DMA_INDEX_CHNL(par->dma_in));

	count -= runtime->dma_addr;

	spin_unlock_irqrestore(&prtd->lock, flags);

	offset = bytes_to_frames(runtime, count);
	if (unlikely(offset >= runtime->buffer_size))
		offset = 0;

	return offset;
}

static int s6000_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct s6000_pcm_dma_params *par;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct s6000_runtime_data *prtd;
	int ret;

	par = snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);
	snd_soc_set_runtime_hwparams(substream, &s6000_pcm_hardware);

	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 16);
	if (ret < 0)
		return ret;
	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 16);
	if (ret < 0)
		return ret;
	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	if (par->same_rate) {
		int rate;
		spin_lock(&par->lock); /* needed? */
		rate = par->rate;
		spin_unlock(&par->lock);
		if (rate != -1) {
			ret = snd_pcm_hw_constraint_minmax(runtime,
							SNDRV_PCM_HW_PARAM_RATE,
							rate, rate);
			if (ret < 0)
				return ret;
		}
	}

	prtd = kzalloc(sizeof(struct s6000_runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);

	runtime->private_data = prtd;

	return 0;
}

static int s6000_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct s6000_runtime_data *prtd = runtime->private_data;

	kfree(prtd);

	return 0;
}

static int s6000_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *hw_params)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct s6000_pcm_dma_params *par;
	int ret;
	ret = snd_pcm_lib_malloc_pages(substream,
				       params_buffer_bytes(hw_params));
	if (ret < 0) {
		printk(KERN_WARNING "s6000-pcm: allocation of memory failed\n");
		return ret;
	}

	par = snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);

	if (par->same_rate) {
		spin_lock(&par->lock);
		if (par->rate == -1 ||
		    !(par->in_use & ~(1 << substream->stream))) {
			par->rate = params_rate(hw_params);
			par->in_use |= 1 << substream->stream;
		} else if (params_rate(hw_params) != par->rate) {
			snd_pcm_lib_free_pages(substream);
			par->in_use &= ~(1 << substream->stream);
			ret = -EBUSY;
		}
		spin_unlock(&par->lock);
	}
	return ret;
}

static int s6000_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct s6000_pcm_dma_params *par =
		snd_soc_dai_get_dma_data(soc_runtime->cpu_dai, substream);

	spin_lock(&par->lock);
	par->in_use &= ~(1 << substream->stream);
	if (!par->in_use)
		par->rate = -1;
	spin_unlock(&par->lock);

	return snd_pcm_lib_free_pages(substream);
}

static struct snd_pcm_ops s6000_pcm_ops = {
	.open = 	s6000_pcm_open,
	.close = 	s6000_pcm_close,
	.ioctl = 	snd_pcm_lib_ioctl,
	.hw_params = 	s6000_pcm_hw_params,
	.hw_free = 	s6000_pcm_hw_free,
	.trigger =	s6000_pcm_trigger,
	.prepare = 	s6000_pcm_prepare,
	.pointer = 	s6000_pcm_pointer,
};

static void s6000_pcm_free(struct snd_pcm *pcm)
{
	struct snd_soc_pcm_runtime *runtime = pcm->private_data;
	struct s6000_pcm_dma_params *params =
		snd_soc_dai_get_dma_data(runtime->cpu_dai,
			pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream);

	free_irq(params->irq, pcm);
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static u64 s6000_pcm_dmamask = DMA_BIT_MASK(32);

static int s6000_pcm_new(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_card *card = runtime->card->snd_card;
	struct snd_pcm *pcm = runtime->pcm;
	struct s6000_pcm_dma_params *params;
	int res;

	params = snd_soc_dai_get_dma_data(runtime->cpu_dai,
			pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream);

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &s6000_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	if (params->dma_in) {
		s6dmac_disable_chan(DMA_MASK_DMAC(params->dma_in),
				    DMA_INDEX_CHNL(params->dma_in));
		s6dmac_int_sources(DMA_MASK_DMAC(params->dma_in),
				   DMA_INDEX_CHNL(params->dma_in));
	}

	if (params->dma_out) {
		s6dmac_disable_chan(DMA_MASK_DMAC(params->dma_out),
				    DMA_INDEX_CHNL(params->dma_out));
		s6dmac_int_sources(DMA_MASK_DMAC(params->dma_out),
				   DMA_INDEX_CHNL(params->dma_out));
	}

	res = request_irq(params->irq, s6000_pcm_irq, IRQF_SHARED,
			  "s6000-audio", pcm);
	if (res) {
		printk(KERN_ERR "s6000-pcm couldn't get IRQ\n");
		return res;
	}

	res = snd_pcm_lib_preallocate_pages_for_all(pcm,
						    SNDRV_DMA_TYPE_DEV,
						    card->dev,
						    S6_PCM_PREALLOCATE_SIZE,
						    S6_PCM_PREALLOCATE_MAX);
	if (res)
		printk(KERN_WARNING "s6000-pcm: preallocation failed\n");

	spin_lock_init(&params->lock);
	params->in_use = 0;
	params->rate = -1;
	return 0;
}

static struct snd_soc_platform_driver s6000_soc_platform = {
	.ops =		&s6000_pcm_ops,
	.pcm_new = 	s6000_pcm_new,
	.pcm_free = 	s6000_pcm_free,
};

static int __devinit s6000_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &s6000_soc_platform);
}

static int __devexit s6000_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver s6000_pcm_driver = {
	.driver = {
			.name = "s6000-pcm-audio",
			.owner = THIS_MODULE,
	},

	.probe = s6000_soc_platform_probe,
	.remove = __devexit_p(s6000_soc_platform_remove),
};

module_platform_driver(s6000_pcm_driver);

MODULE_AUTHOR("Daniel Gloeckner");
MODULE_DESCRIPTION("Stretch s6000 family PCM DMA module");
MODULE_LICENSE("GPL");
