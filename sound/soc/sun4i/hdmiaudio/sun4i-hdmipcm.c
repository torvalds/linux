/*
 * sound\soc\sun4i\hdmiaudio\sun4i-hdmipcm.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * chenpailin <chenpailin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
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

#include <asm/dma.h>
#include <mach/hardware.h>
#include <plat/dma.h>

#include "sun4i-hdmiaudio.h"
#include "sun4i-hdmipcm.h"

static volatile unsigned int dmasrc = 0;
static volatile unsigned int dmadst = 0;

static const struct snd_pcm_hardware sun4i_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
				      SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
				      SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE,
	.rates			= SNDRV_PCM_RATE_8000_192000 | SNDRV_PCM_RATE_KNOT,
	.rate_min		= 8000,
	.rate_max		= 192000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= 128*1024,    /* value must be (2^n)Kbyte size */
	.period_bytes_min	= 1024*4,//1024*4,
	.period_bytes_max	= 1024*32,//1024*32,
	.periods_min		= 4,//4,
	.periods_max		= 8,//8,
	.fifo_size		= 128,//32,
};

struct sun4i_runtime_data {
	spinlock_t lock;
	int state;
	unsigned int dma_loaded;
	unsigned int dma_limit;
	unsigned int dma_period;
	dma_addr_t dma_start;
	dma_addr_t dma_pos;
	dma_addr_t dma_end;
	struct sun4i_dma_params *params;
};

static void sun4i_pcm_enqueue(struct snd_pcm_substream *substream)
{
	struct sun4i_runtime_data *prtd = substream->runtime->private_data;
	dma_addr_t pos = prtd->dma_pos;
	unsigned int limit;
	int ret;

	unsigned long len = prtd->dma_period;
  	limit = prtd->dma_limit;
  	while(prtd->dma_loaded < limit)
	{
		if((pos + len) > prtd->dma_end){
			len  = prtd->dma_end - pos;
		}

		ret = sw_dma_enqueue(prtd->params->channel, substream, __bus_to_virt(pos),  len);
		if (ret == 0) {
			prtd->dma_loaded++;
			pos += prtd->dma_period;
			if(pos >= prtd->dma_end)
				pos = prtd->dma_start;
		}else {
			break;
		}

	}
	prtd->dma_pos = pos;
}

static void sun4i_audio_buffdone(struct sw_dma_chan *channel,
		                                  void *dev_id, int size,
		                                  enum sw_dma_buffresult result)
{
	struct sun4i_runtime_data *prtd;
	struct snd_pcm_substream *substream = dev_id;

	if (result == SW_RES_ABORT || result == SW_RES_ERR)
		return;

	prtd = substream->runtime->private_data;
		if (substream) {
			snd_pcm_period_elapsed(substream);
		}

	spin_lock(&prtd->lock);
	{
		prtd->dma_loaded--;
		sun4i_pcm_enqueue(substream);
	}
	spin_unlock(&prtd->lock);
}

static int sun4i_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sun4i_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned long totbytes = params_buffer_bytes(params);
	struct sun4i_dma_params *dma =
					snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	int ret = 0;
	if (!dma)
		return 0;

	if (prtd->params == NULL) {
		prtd->params = dma;
		ret = sw_dma_request(prtd->params->channel,
					  prtd->params->client, NULL);
		if (ret < 0) {
				return ret;
		}
	}

	sw_dma_set_buffdone_fn(prtd->params->channel,
				    sun4i_audio_buffdone);

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

static int sun4i_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct sun4i_runtime_data *prtd = substream->runtime->private_data;

	/* TODO - do we need to ensure DMA flushed */
	if(prtd->params)
  	sw_dma_ctrl(prtd->params->channel, SW_DMAOP_FLUSH);

	snd_pcm_set_runtime_buffer(substream, NULL);

	if (prtd->params) {
		sw_dma_free(prtd->params->channel, prtd->params->client);
		prtd->params = NULL;
	}

	return 0;
}

static int sun4i_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct sun4i_runtime_data *prtd = substream->runtime->private_data;
	struct dma_hw_conf codec_dma_conf;
	int ret = 0;

	if (!prtd->params)
		return 0;

   	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		codec_dma_conf.drqsrc_type  = DRQ_TYPE_SDRAM;
		codec_dma_conf.drqdst_type  = DRQ_TYPE_HDMIAUDIO;
		codec_dma_conf.xfer_type    = DMAXFER_D_BWORD_S_BWORD;
		codec_dma_conf.address_type = DMAADDRT_D_IO_S_LN;
		codec_dma_conf.dir          = SW_DMA_WDEV;
		codec_dma_conf.reload       = 0;
		codec_dma_conf.hf_irq       = SW_DMA_IRQ_FULL;
		codec_dma_conf.from         = prtd->dma_start;
		codec_dma_conf.to           = prtd->params->dma_addr;
		codec_dma_conf.cmbk		 	= 0x1F071F07;
		ret = sw_dma_config(prtd->params->channel, &codec_dma_conf);
	}

	/* flush the DMA channel */
	sw_dma_ctrl(prtd->params->channel, SW_DMAOP_FLUSH);
	prtd->dma_loaded = 0;
	prtd->dma_pos = prtd->dma_start;

	/* enqueue dma buffers */
	sun4i_pcm_enqueue(substream);

	return ret;
}

static int sun4i_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct sun4i_runtime_data *prtd = substream->runtime->private_data;
	int ret ;
	spin_lock(&prtd->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		printk("[HDMI-AUDIO] PCM trigger start...\n");
		sw_dma_ctrl(prtd->params->channel, SW_DMAOP_START);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		printk("[HDMI-AUDIO] PCM trigger stop...\n");
		sw_dma_ctrl(prtd->params->channel, SW_DMAOP_STOP);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock(&prtd->lock);
	return 0;
}

static snd_pcm_uframes_t sun4i_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sun4i_runtime_data *prtd = runtime->private_data;
	unsigned long res = 0;
	snd_pcm_uframes_t offset = 0;

	spin_lock(&prtd->lock);

	sw_dma_getcurposition(DMACH_HDMIAUDIO, (dma_addr_t*)&dmasrc, (dma_addr_t*)&dmadst);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		res = dmadst - prtd->dma_start;
	else
	{
		offset = bytes_to_frames(runtime, dmasrc + prtd->dma_period - runtime->dma_addr);
	}
	spin_unlock(&prtd->lock);

	if(offset >= runtime->buffer_size)
		offset = 0;
		return offset;
}

static int sun4i_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sun4i_runtime_data *prtd;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	snd_soc_set_runtime_hwparams(substream, &sun4i_pcm_hardware);

	prtd = kzalloc(sizeof(struct sun4i_runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);

	runtime->private_data = prtd;
	return 0;
}

static int sun4i_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sun4i_runtime_data *prtd = runtime->private_data;

	kfree(prtd);

	return 0;
}

static int sun4i_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops sun4i_pcm_ops = {
	.open				= sun4i_pcm_open,
	.close			= sun4i_pcm_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params	= sun4i_pcm_hw_params,
	.hw_free		= sun4i_pcm_hw_free,
	.prepare		= sun4i_pcm_prepare,
	.trigger		= sun4i_pcm_trigger,
	.pointer		= sun4i_pcm_pointer,
	.mmap				= sun4i_pcm_mmap,
};

static int sun4i_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = sun4i_pcm_hardware.buffer_bytes_max;

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

static void sun4i_pcm_free_dma_buffers(struct snd_pcm *pcm)
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

static u64 sun4i_pcm_mask = DMA_BIT_MASK(32);

static int sun4i_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;

	if (!card->dev->dma_mask)
		card->dev->dma_mask = &sun4i_pcm_mask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = sun4i_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = sun4i_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

static struct snd_soc_platform_driver sun4i_soc_platform_hdmiaudio = {
		.ops        =        &sun4i_pcm_ops,
		.pcm_new	=		 sun4i_pcm_new,
		.pcm_free	=		 sun4i_pcm_free_dma_buffers,
};

static int __devinit sun4i_hdmiaudio_pcm_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &sun4i_soc_platform_hdmiaudio);
}

static int __devexit sun4i_hdmiaudio_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

/*data relating*/
static struct platform_device sun4i_hdmiaudio_pcm_device = {
	.name = "sun4i-hdmiaudio-pcm-audio",
};

static struct platform_driver sun4i_hdmiaudio_pcm_driver = {
	.probe = sun4i_hdmiaudio_pcm_probe,
	.remove = __devexit_p(sun4i_hdmiaudio_pcm_remove),
	.driver = {
		.name = "sun4i-hdmiaudio-pcm-audio",
		.owner = THIS_MODULE,
	},
};


static int __init sun4i_soc_platform_hdmiaudio_init(void)
{
	int err = 0;
	if((err = platform_device_register(&sun4i_hdmiaudio_pcm_device)) < 0)
		return err;

	if ((err = platform_driver_register(&sun4i_hdmiaudio_pcm_driver)) < 0)
		return err;
	return 0;
}
module_init(sun4i_soc_platform_hdmiaudio_init);

static void __exit sun4i_soc_platform_hdmiaudio_exit(void)
{
	return platform_driver_unregister(&sun4i_hdmiaudio_pcm_driver);
}
module_exit(sun4i_soc_platform_hdmiaudio_exit);

MODULE_AUTHOR("All winner");
MODULE_DESCRIPTION("SUN4I HDMIAUDIO DMA module");
MODULE_LICENSE("GPL");

