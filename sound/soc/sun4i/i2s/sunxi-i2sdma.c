/*
 * sound\soc\sunxi\i2s\sunxi-i2sdma.c
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
#include <plat/dma_compat.h>

#include "sunxi-i2s.h"
#include "sunxi-i2sdma.h"

static volatile unsigned int dmasrc = 0;
static volatile unsigned int dmadst = 0;

static const struct snd_pcm_hardware sunxi_pcm_hardware = {
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

struct sunxi_runtime_data {
	spinlock_t lock;
	int state;
	unsigned int dma_loaded;
	unsigned int dma_limit;
	unsigned int dma_period;
	dma_addr_t dma_start;
	dma_addr_t dma_pos;
	dma_addr_t dma_end;
	struct sunxi_dma_params *params;
};

static void sunxi_pcm_enqueue(struct snd_pcm_substream *substream)
{
	struct sunxi_runtime_data *prtd = substream->runtime->private_data;
	dma_addr_t pos = prtd->dma_pos;
	unsigned int limit;
	int ret;

	unsigned long len = prtd->dma_period;
  	limit = prtd->dma_limit;
  	while(prtd->dma_loaded < limit) {
		if((pos + len) > prtd->dma_end) {
			len  = prtd->dma_end - pos;
		}

		ret = sunxi_dma_enqueue(prtd->params, pos,  len, 0);
		if(ret == 0) {
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

static void sunxi_audio_buffdone(struct sunxi_dma_params *dma, void *dev_id)
{
	struct sunxi_runtime_data *prtd;
	struct snd_pcm_substream *substream = dev_id;

	prtd = substream->runtime->private_data;
	if (substream) {
		snd_pcm_period_elapsed(substream);
	}

	spin_lock(&prtd->lock);
	{
		prtd->dma_loaded--;
		sunxi_pcm_enqueue(substream);
	}
	spin_unlock(&prtd->lock);
}

static int sunxi_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sunxi_runtime_data *prtd = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned long totbytes = params_buffer_bytes(params);
	struct sunxi_dma_params *dma =
			snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	int ret = 0;
	if (!dma)
		return 0;

	if (prtd->params == NULL) {
		prtd->params = dma;
		ret = sunxi_dma_request(prtd->params, 0);
		if (ret < 0) {
				return ret;
		}
	}

	if (sunxi_dma_set_callback(prtd->params, sunxi_audio_buffdone,
							    substream) != 0) {
		sunxi_dma_release(prtd->params);
		prtd->params = NULL;
		return -EINVAL;
	}

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

static int sunxi_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct sunxi_runtime_data *prtd = substream->runtime->private_data;

	if (prtd->params)
		sunxi_dma_flush(prtd->params);

	snd_pcm_set_runtime_buffer(substream, NULL);

	if (prtd->params) {
		sunxi_dma_stop(prtd->params);
		sunxi_dma_release(prtd->params);
		prtd->params = NULL;
	}

	return 0;
}

static int sunxi_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct sunxi_runtime_data *prtd = substream->runtime->private_data;
	int ret = 0;

	if (!prtd->params)
		return 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
		struct dma_hw_conf codec_dma_conf;
		codec_dma_conf.drqsrc_type  = DRQ_TYPE_SDRAM;
		codec_dma_conf.drqdst_type  = DRQ_TYPE_IIS;
		codec_dma_conf.xfer_type    = DMAXFER_D_BHALF_S_BHALF;
		codec_dma_conf.address_type = DMAADDRT_D_FIX_S_INC;
		codec_dma_conf.dir          = SW_DMA_WDEV;
		codec_dma_conf.reload       = 0;
		codec_dma_conf.hf_irq       = SW_DMA_IRQ_FULL;
		codec_dma_conf.from         = prtd->dma_start;
		codec_dma_conf.to           = prtd->params->dma_addr;
#else
		dma_config_t codec_dma_conf;
		memset(&codec_dma_conf, 0, sizeof(codec_dma_conf));
		codec_dma_conf.xfer_type.src_data_width	= DATA_WIDTH_16BIT;
		codec_dma_conf.xfer_type.src_bst_len	= DATA_BRST_1;	
		codec_dma_conf.xfer_type.dst_data_width	= DATA_WIDTH_16BIT;
		codec_dma_conf.xfer_type.dst_bst_len	= DATA_BRST_1;
		codec_dma_conf.address_type.src_addr_mode = NDMA_ADDR_INCREMENT;
		codec_dma_conf.address_type.dst_addr_mode = NDMA_ADDR_NOCHANGE;
		codec_dma_conf.src_drq_type		= N_SRC_SDRAM;
		codec_dma_conf.dst_drq_type		= N_DST_IIS0_TX;
		codec_dma_conf.bconti_mode		= false;
		codec_dma_conf.irq_spt			= CHAN_IRQ_FD;
#endif
		ret = sunxi_dma_config(prtd->params, &codec_dma_conf, 0);
	}

	/* flush the DMA channel */
	prtd->dma_loaded = 0;
	if (sunxi_dma_flush(prtd->params) == 0)
		prtd->dma_pos = prtd->dma_start;

	/* enqueue dma buffers */
	sunxi_pcm_enqueue(substream);

	return ret;
}

static int sunxi_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct sunxi_runtime_data *prtd = substream->runtime->private_data;
	int ret ;
	spin_lock(&prtd->lock);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		printk("[IIS] dma trigger start\n");
		printk("[IIS] 0x01c22400+0x24 = %#x, line= %d\n", readl(0xf1c22400+0x24), __LINE__);
		sunxi_dma_start(prtd->params);
		break;

	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        printk("[IIS] dma trigger stop\n");
		sunxi_dma_stop(prtd->params);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	spin_unlock(&prtd->lock);
	return 0;
}

static snd_pcm_uframes_t sunxi_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sunxi_runtime_data *prtd = runtime->private_data;
	unsigned long res = 0;
	snd_pcm_uframes_t offset = 0;

	spin_lock(&prtd->lock);
	sunxi_dma_getcurposition(prtd->params,
				 (dma_addr_t*)&dmasrc, (dma_addr_t*)&dmadst);

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

static int sunxi_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sunxi_runtime_data *prtd;

	snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	snd_soc_set_runtime_hwparams(substream, &sunxi_pcm_hardware);

	prtd = kzalloc(sizeof(struct sunxi_runtime_data), GFP_KERNEL);
	if (prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&prtd->lock);

	runtime->private_data = prtd;
	return 0;
}

static int sunxi_pcm_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct sunxi_runtime_data *prtd = runtime->private_data;

	kfree(prtd);

	return 0;
}

static int sunxi_pcm_mmap(struct snd_pcm_substream *substream,
	struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_writecombine(substream->pcm->card->dev, vma,
				     runtime->dma_area,
				     runtime->dma_addr,
				     runtime->dma_bytes);
}

static struct snd_pcm_ops sunxi_pcm_ops = {
	.open			= sunxi_pcm_open,
	.close			= sunxi_pcm_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params		= sunxi_pcm_hw_params,
	.hw_free		= sunxi_pcm_hw_free,
	.prepare		= sunxi_pcm_prepare,
	.trigger		= sunxi_pcm_trigger,
	.pointer		= sunxi_pcm_pointer,
	.mmap			= sunxi_pcm_mmap,
};

static int sunxi_pcm_preallocate_dma_buffer(struct snd_pcm *pcm, int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = sunxi_pcm_hardware.buffer_bytes_max;

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

static void sunxi_pcm_free_dma_buffers(struct snd_pcm *pcm)
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

static u64 sunxi_pcm_mask = DMA_BIT_MASK(32);

static int sunxi_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;
	int ret = 0;
	if (!card->dev->dma_mask)
		card->dev->dma_mask = &sunxi_pcm_mask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = sunxi_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		ret = sunxi_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}
 out:
	return ret;
}

static struct snd_soc_platform_driver sunxi_soc_platform = {
	.ops		= &sunxi_pcm_ops,
	.pcm_new	= sunxi_pcm_new,
	.pcm_free	= sunxi_pcm_free_dma_buffers,
};

static int __devinit sunxi_i2s_pcm_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &sunxi_soc_platform);
}

static int __devexit sunxi_i2s_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

/*data relating*/
static struct platform_device sunxi_i2s_pcm_device = {
	.name = "sunxi-i2s-pcm-audio",
};

/*method relating*/
static struct platform_driver sunxi_i2s_pcm_driver = {
	.probe = sunxi_i2s_pcm_probe,
	.remove = __devexit_p(sunxi_i2s_pcm_remove),
	.driver = {
		.name = "sunxi-i2s-pcm-audio",
		.owner = THIS_MODULE,
	},
};

static int __init sunxi_soc_platform_i2s_init(void)
{
	int err = 0;
	if((err = platform_device_register(&sunxi_i2s_pcm_device)) < 0)
		return err;

	if ((err = platform_driver_register(&sunxi_i2s_pcm_driver)) < 0)
		return err;
	return 0;
}
module_init(sunxi_soc_platform_i2s_init);

static void __exit sunxi_soc_platform_i2s_exit(void)
{
	return platform_driver_unregister(&sunxi_i2s_pcm_driver);
}
module_exit(sunxi_soc_platform_i2s_exit);

MODULE_AUTHOR("All winner");
MODULE_DESCRIPTION("SUNXI PCM DMA module");
MODULE_LICENSE("GPL");

