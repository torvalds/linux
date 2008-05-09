/*
 * s3c24xx-pcm.c  --  ALSA Soc Audio Layer
 *
 * (c) 2006 Wolfson Microelectronics PLC.
 * Graeme Gregory graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 * (c) 2004-2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Revision history
 *    11th Dec 2006   Merged with Simtec driver
 *    10th Nov 2006   Initial version.
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

#include <asm/dma.h>
#include <asm/hardware.h>
#include <asm/arch/dma.h>
#include <asm/arch/audio.h>

#include "s3c24xx-pcm.h"

#define S3C24XX_PCM_DEBUG 0
#if S3C24XX_PCM_DEBUG
#define DBG(x...) printk(KERN_DEBUG "s3c24xx-pcm: " x)
#else
#define DBG(x...)
#endif

static const struct snd_pcm_hardware s3c24xx_pcm_hardware = {
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

struct s3c24xx_runtime_data {
	spinlock_t lock;
	int state;
	unsigned int dma_loaded;
	unsigned int dma_limit;
	unsigned int dma_period;
	dma_addr_t dma_start;
	dma_addr_t dma_pos;
	dma_addr_t dma_end;
	struct s3c24xx_pcm_dma_params *params;
};

/* s3c24xx_pcm_enqueue
 *
 * place a dma buffer onto the queue for the dma system
 * to handle.
*/
static void s3c24xx_pcm_enqueue(struct snd_pcm_substream *substream)
{
	struct s3c24xx_runtime_data *prtd = substream->runtime->private_data;
	dma_addr_t pos = prtd->dma_pos;
	int ret;

	DBG("Entered %s\n", __func__);

	while (prtd->dma_loaded < prtd->dma_limit) {
		unsigned long len = prtd->dma_period;

		DBG("dma_loaded: %d\n", prtd->dma_loaded);

		if ((pos + len) > prtd->dma_end) {
			len  = prtd->dma_end - pos;
			DBG(KERN_DEBUG "%s: corrected dma len %ld\n",
			       __func__, len);
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

static void s3c24xx_audio_buffdone(struct s3c2410_dma_chan *channel,
				void *dev_id, int size,
				enum s3c2410_dma_buffresult result)
{
	struct snd_pcm_substream *substream = dev_id;
	struct s3c24xx_runtime_data *prtd;

	DBG("Entered %s\n", __func__);

	if (result == S3C2410_RES_ABORT || result == S3C2410_RES_ERR)
		return;

	prtd = substream->runtime->private_data;

	if (substream)
		snd_pcm_period_elapsed(substream);

	spin_lock(&prtd->lock);
	if (prtd->state & ST_RUNNING) {
		prtd->dma_loaded--;
		s3c24xx_pcm_enqueue(substream);
	}

	spin_unlock(&prtd->lock);
}

static int s3c24xx_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct s3c24xx_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct s3c24xx_pcm_dma_params *dma = rtd->dai->cpu_dai->dma_data;
	unsigned long totbytes = params_buffer_bytes(params);
	int ret = 0;

	DBG("Entered %s\n", __func__);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!dma)
		return 0;

	/* this may get called several times by oss emulation
	 * with different params -HW */
	if (prtd->params == NULL) {
		/* prepare DMA */
		prtd->params = dma;

		DBG("params %p, client %p, channel %d\n", prtd->params,
			prtd->params->client, prtd->params->channel);

		ret = s3c2410_dma_request(prtd->params->channel,
					  prtd->params->client, NULL);

		if (ret) {
			DBG(KERN_ERR "failed to get dma channel\n");
			return ret;
		}
	}

	s3c2410_dma_set_buffdone_fn(prtd->params->channel,
				    s3c24xx_audio_buffdone);

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

static int s3c24xx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct s3c24xx_runtime_data *prtd = substream->runtime->private_data;

	DBG("Entered %s\n", __func__);

	/* TODO - do we need to ensure DMA flushed */
	snd_pcm_set_runtime_buffer(substream, NULL);

	if (prtd->params) {
		s3c2410_dma_free(prtd->params->channel, prtd->params->client);
		prtd->params = NULL;
	}

	return 0;
}

static int s3c24xx_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct s3c24xx_runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	DBG("Entered %s\n", __func__);

	/* return if this is a bufferless transfer e.g.
	 * codec <--> BT codec or GSM modem -- lg FIXME */
	if (!prtd->params)
		return 0;

	/* channel needs configuring for mem=>device, increment memory addr,
	 * sync to pclk, half-word transfers to the IIS-FIFO. */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		s3c2410_dma_devconfig(prtd->params->channel,
				S3C2410_DMASRC_MEM, S3C2410_DISRCC_INC |
				S3C2410_DISRCC_APB, prtd->params->dma_addr);

		s3c2410_dma_config(prtd->params->channel,
				prtd->params->dma_size,
				S3C2410_DCON_SYNC_PCLK |
				S3C2410_DCON_HANDSHAKE);
	} else {
		s3c2410_dma_config(prtd->params->channel,
				prtd->params->dma_size,
				S3C2410_DCON_HANDSHAKE |
				S3C2410_DCON_SYNC_PCLK);

		s3c2410_dma_devconfig(prtd->params->channel,
					S3C2410_DMASRC_HW, 0x3,
					prtd->params->dma_addr);
	}

	/* flush the DMA channel */
	s3c2410_dma_ctrl(prtd->params->channel, S3C2410_DMAOP_FLUSH);
	prtd->dma_loaded = 0;
	prtd->dma_pos = prtd->dma_start;

	/* enqueue dma buffers */
	s3c24xx_pcm_enqueue(substream);

	return ret;
}

static int s3c24xx_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct s3c24xx_runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	DBG("Entered %s\n", __func__);

	spin_lock(&prtd->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		prtd->state |= ST_RUNNING;
		s3c2410_dma_ctrl(prtd->params->channel, S3C2410_DMAOP_START);
		s3c2410_dma_ctrl(prtd->params->channel, S3C2410_DMAOP_STARTED);
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
s3c24xx_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct s3c24xx_runtime_data *prtd = runtime->private_data;
	unsigned long res;
	dma_addr_t src, dst;

	DBG("Entered %s\n", __func__);

	spin_lock(&prtd->lock);
	s3c2410_dma_getposition(prtd->params->channel, &src, &dst);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		res = dst - prtd->dma_start;
	else
		res = src - prtd->dma_start;

	spin_unlock(&prtd->lock);

	DBG("Pointer %x %x\n", src, dst);

	/* we seem to be getting the odd error from the pcm library due
	 * to out-of-bounds pointers. this is maybe due to the dma engine
	 * not having loaded the new values for the channel before being
	 * callled... (todo - fix )
	 */

	if (res >= snd_pcm_lib_buffer_bytes(substream)) {
		if (res == snd_pcm_lib_buffer_bytes(substream))
			res = 0;
	}

	return bytes_to_frames(substream->runtime, res);
}

static int s3c24xx_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct s3c24xx_runtime_data *prtd;

	DBG("Entered %s\n", __func__);

	snd_soc_set_runtime_hwparams(substream, &s3c24xx_pcm_hardware);

	prtd = kzalloc(sizeof(struct s3c24xx_runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);

	runtime->private_data = prtd;
	return 0;
}

static int s3c24xx_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct s3c24xx_runtime_data *prtd = runtime->private_data;

	DBG("Entered %s\n", __func__);

	if (!prtd)
		DBG("s3c24xx_pcm_close called with prtd == NULL\n");

	kfree(prtd);

	return 0;
}

static int s3c24xx_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	DBG("Entered %s\n", __func__);

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops s3c24xx_pcm_ops = {
	.open		= s3c24xx_pcm_open,
	.close		= s3c24xx_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= s3c24xx_pcm_hw_params,
	.hw_free	= s3c24xx_pcm_hw_free,
	.prepare	= s3c24xx_pcm_prepare,
	.trigger	= s3c24xx_pcm_trigger,
	.pointer	= s3c24xx_pcm_pointer,
	.mmap		= s3c24xx_pcm_mmap,
};

static int s3c24xx_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = s3c24xx_pcm_hardware.buffer_bytes_max;

	DBG("Entered %s\n", __func__);

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

static void s3c24xx_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;

	DBG("Entered %s\n", __func__);

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

static u64 s3c24xx_pcm_dmamask = DMA_32BIT_MASK;

static int s3c24xx_pcm_new(struct snd_card *card,
	struct snd_soc_codec_dai *dai, struct snd_pcm *pcm)
{
	int ret = 0;

	DBG("Entered %s\n", __func__);

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &s3c24xx_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (dai->playback.channels_min) {
		ret = s3c24xx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (dai->capture.channels_min) {
		ret = s3c24xx_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

struct snd_soc_platform s3c24xx_soc_platform = {
	.name		= "s3c24xx-audio",
	.pcm_ops 	= &s3c24xx_pcm_ops,
	.pcm_new	= s3c24xx_pcm_new,
	.pcm_free	= s3c24xx_pcm_free_dma_buffers,
};
EXPORT_SYMBOL_GPL(s3c24xx_soc_platform);

MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_DESCRIPTION("Samsung S3C24XX PCM DMA module");
MODULE_LICENSE("GPL");
