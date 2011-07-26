/*
 * dma.c  --  ALSA Soc Audio Layer
 *
 * (c) 2006 Wolfson Microelectronics PLC.
 * Graeme Gregory graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 * Copyright 2004-2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <sound/soc.h>
#include <sound/pcm_params.h>

#include <asm/dma.h>
#include <mach/hardware.h>
#include <mach/dma.h>

#include "dma.h"

#define ST_RUNNING		(1<<0)
#define ST_OPENED		(1<<1)

static const struct snd_pcm_hardware dma_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED |
				    SNDRV_PCM_INFO_BLOCK_TRANSFER |
				    SNDRV_PCM_INFO_MMAP |
				    SNDRV_PCM_INFO_MMAP_VALID |
				    SNDRV_PCM_INFO_PAUSE |
				    SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_U16_LE |
				    SNDRV_PCM_FMTBIT_U8 |
				    SNDRV_PCM_FMTBIT_S8,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= PAGE_SIZE,
	.period_bytes_max	= PAGE_SIZE*2,
	.periods_min		= 2,
	.periods_max		= 128,
	.fifo_size		= 32,
};

struct runtime_data {
	spinlock_t lock;
	int state;
	unsigned int dma_loaded;
	unsigned int dma_limit;
	unsigned int dma_period;
	dma_addr_t dma_start;
	dma_addr_t dma_pos;
	dma_addr_t dma_end;
	struct s3c_dma_params *params;
};

/* dma_enqueue
 *
 * place a dma buffer onto the queue for the dma system
 * to handle.
*/
static void dma_enqueue(struct snd_pcm_substream *substream)
{
	struct runtime_data *prtd = substream->runtime->private_data;
	dma_addr_t pos = prtd->dma_pos;
	unsigned int limit;
	int ret;

	pr_debug("Entered %s\n", __func__);

	if (s3c_dma_has_circular())
		limit = (prtd->dma_end - prtd->dma_start) / prtd->dma_period;
	else
		limit = prtd->dma_limit;

	pr_debug("%s: loaded %d, limit %d\n",
				__func__, prtd->dma_loaded, limit);

	while (prtd->dma_loaded < limit) {
		unsigned long len = prtd->dma_period;

		pr_debug("dma_loaded: %d\n", prtd->dma_loaded);

		if ((pos + len) > prtd->dma_end) {
			len  = prtd->dma_end - pos;
			pr_debug("%s: corrected dma len %ld\n", __func__, len);
		}

		ret = s3c2410_dma_enqueue(prtd->params->channel,
			substream, pos, len);

		if (ret == 0) {
			prtd->dma_loaded++;
			pos += prtd->dma_period;
			if (pos >= prtd->dma_end)
				pos = prtd->dma_start;
		} else
			break;
	}

	prtd->dma_pos = pos;
}

static void audio_buffdone(struct s3c2410_dma_chan *channel,
				void *dev_id, int size,
				enum s3c2410_dma_buffresult result)
{
	struct snd_pcm_substream *substream = dev_id;
	struct runtime_data *prtd;

	pr_debug("Entered %s\n", __func__);

	if (result == S3C2410_RES_ABORT || result == S3C2410_RES_ERR)
		return;

	prtd = substream->runtime->private_data;

	if (substream)
		snd_pcm_period_elapsed(substream);

	spin_lock(&prtd->lock);
	if (prtd->state & ST_RUNNING && !s3c_dma_has_circular()) {
		prtd->dma_loaded--;
		dma_enqueue(substream);
	}

	spin_unlock(&prtd->lock);
}

static int dma_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned long totbytes = params_buffer_bytes(params);
	struct s3c_dma_params *dma =
		snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	int ret = 0;


	pr_debug("Entered %s\n", __func__);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!dma)
		return 0;

	/* this may get called several times by oss emulation
	 * with different params -HW */
	if (prtd->params == NULL) {
		/* prepare DMA */
		prtd->params = dma;

		pr_debug("params %p, client %p, channel %d\n", prtd->params,
			prtd->params->client, prtd->params->channel);

		ret = s3c2410_dma_request(prtd->params->channel,
					  prtd->params->client, NULL);

		if (ret < 0) {
			printk(KERN_ERR "failed to get dma channel\n");
			return ret;
		}

		/* use the circular buffering if we have it available. */
		if (s3c_dma_has_circular())
			s3c2410_dma_setflags(prtd->params->channel,
					     S3C2410_DMAF_CIRCULAR);
	}

	s3c2410_dma_set_buffdone_fn(prtd->params->channel,
				    audio_buffdone);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	runtime->dma_bytes = totbytes;

	spin_lock_irq(&prtd->lock);
	prtd->dma_loaded = 0;
	prtd->dma_limit = runtime->hw.periods_min;
	prtd->dma_period = params_period_bytes(params);
	prtd->dma_start = runtime->dma_addr;
	prtd->dma_pos = prtd->dma_start;
	prtd->dma_end = prtd->dma_start + totbytes;
	spin_unlock_irq(&prtd->lock);

	return 0;
}

static int dma_hw_free(struct snd_pcm_substream *substream)
{
	struct runtime_data *prtd = substream->runtime->private_data;

	pr_debug("Entered %s\n", __func__);

	/* TODO - do we need to ensure DMA flushed */
	snd_pcm_set_runtime_buffer(substream, NULL);

	if (prtd->params) {
		s3c2410_dma_free(prtd->params->channel, prtd->params->client);
		prtd->params = NULL;
	}

	return 0;
}

static int dma_prepare(struct snd_pcm_substream *substream)
{
	struct runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	pr_debug("Entered %s\n", __func__);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!prtd->params)
		return 0;

	/* channel needs configuring for mem=>device, increment memory addr,
	 * sync to pclk, half-word transfers to the IIS-FIFO. */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		s3c2410_dma_devconfig(prtd->params->channel,
				      S3C2410_DMASRC_MEM,
				      prtd->params->dma_addr);
	} else {
		s3c2410_dma_devconfig(prtd->params->channel,
				      S3C2410_DMASRC_HW,
				      prtd->params->dma_addr);
	}

	s3c2410_dma_config(prtd->params->channel,
			   prtd->params->dma_size);

	/* flush the DMA channel */
	s3c2410_dma_ctrl(prtd->params->channel, S3C2410_DMAOP_FLUSH);
	prtd->dma_loaded = 0;
	prtd->dma_pos = prtd->dma_start;

	/* enqueue dma buffers */
	dma_enqueue(substream);

	return ret;
}

static int dma_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	pr_debug("Entered %s\n", __func__);

	spin_lock(&prtd->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		prtd->state |= ST_RUNNING;
		s3c2410_dma_ctrl(prtd->params->channel, S3C2410_DMAOP_START);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		prtd->state &= ~ST_RUNNING;
		s3c2410_dma_ctrl(prtd->params->channel, S3C2410_DMAOP_STOP);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock(&prtd->lock);

	return ret;
}

static snd_pcm_uframes_t
dma_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct runtime_data *prtd = runtime->private_data;
	unsigned long res;
	dma_addr_t src, dst;

	pr_debug("Entered %s\n", __func__);

	spin_lock(&prtd->lock);
	s3c2410_dma_getposition(prtd->params->channel, &src, &dst);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		res = dst - prtd->dma_start;
	else
		res = src - prtd->dma_start;

	spin_unlock(&prtd->lock);

	pr_debug("Pointer %x %x\n", src, dst);

	/* we seem to be getting the odd error from the pcm library due
	 * to out-of-bounds pointers. this is maybe due to the dma engine
	 * not having loaded the new values for the channel before being
	 * called... (todo - fix )
	 */

	if (res >= snd_pcm_lib_buffer_bytes(substream)) {
		if (res == snd_pcm_lib_buffer_bytes(substream))
			res = 0;
	}

	return bytes_to_frames(substream->runtime, res);
}

static int dma_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct runtime_data *prtd;

	pr_debug("Entered %s\n", __func__);

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	snd_soc_set_runtime_hwparams(substream, &dma_hardware);

	prtd = kzalloc(sizeof(struct runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);

	runtime->private_data = prtd;
	return 0;
}

static int dma_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct runtime_data *prtd = runtime->private_data;

	pr_debug("Entered %s\n", __func__);

	if (!prtd)
		pr_debug("dma_close called with prtd == NULL\n");

	kfree(prtd);

	return 0;
}

static int dma_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	pr_debug("Entered %s\n", __func__);

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops dma_ops = {
	.open		= dma_open,
	.close		= dma_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= dma_hw_params,
	.hw_free	= dma_hw_free,
	.prepare	= dma_prepare,
	.trigger	= dma_trigger,
	.pointer	= dma_pointer,
	.mmap		= dma_mmap,
};

static int preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = dma_hardware.buffer_bytes_max;

	pr_debug("Entered %s\n", __func__);

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

static void dma_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	pr_debug("Entered %s\n", __func__);

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

static u64 dma_mask = DMA_BIT_MASK(32);

static int dma_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_soc_dai *dai = rtd->cpu_dai;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	pr_debug("Entered %s\n", __func__);

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &dma_mask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (dai->driver->playback.channels_min) {
		ret = preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (dai->driver->capture.channels_min) {
		ret = preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
out:
	return ret;
}

static struct snd_soc_platform_driver samsung_asoc_platform = {
	.ops		= &dma_ops,
	.pcm_new	= dma_new,
	.pcm_free	= dma_free_dma_buffers,
};

static int __devinit samsung_asoc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &samsung_asoc_platform);
}

static int __devexit samsung_asoc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver asoc_dma_driver = {
	.driver = {
		.name = "samsung-audio",
		.owner = THIS_MODULE,
	},

	.probe = samsung_asoc_platform_probe,
	.remove = __devexit_p(samsung_asoc_platform_remove),
};

static int __init samsung_asoc_init(void)
{
	return platform_driver_register(&asoc_dma_driver);
}
module_init(samsung_asoc_init);

static void __exit samsung_asoc_exit(void)
{
	platform_driver_unregister(&asoc_dma_driver);
}
module_exit(samsung_asoc_exit);

MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("Samsung ASoC DMA Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:samsung-audio");
