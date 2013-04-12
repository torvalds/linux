/*
 * atmel-pcm-dma.c  --  ALSA PCM DMA support for the Atmel SoC.
 *
 *  Copyright (C) 2012 Atmel
 *
 * Author: Bo Shen <voice.shen@atmel.com>
 *
 * Based on atmel-pcm by:
 * Sedji Gaouaou <sedji.gaouaou@atmel.com>
 * Copyright 2008 Atmel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/atmel-ssc.h>
#include <linux/platform_data/dma-atmel.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "atmel-pcm.h"

/*--------------------------------------------------------------------------*\
 * Hardware definition
\*--------------------------------------------------------------------------*/
static const struct snd_pcm_hardware atmel_pcm_dma_hardware = {
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_PAUSE,
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.period_bytes_min	= 256,		/* lighting DMA overhead */
	.period_bytes_max	= 2 * 0xffff,	/* if 2 bytes format */
	.periods_min		= 8,
	.periods_max		= 1024,		/* no limit */
	.buffer_bytes_max	= ATMEL_SSC_DMABUF_SIZE,
};

/**
 * atmel_pcm_dma_irq: SSC interrupt handler for DMAENGINE enabled SSC
 *
 * We use DMAENGINE to send/receive data to/from SSC so this ISR is only to
 * check if any overrun occured.
 */
static void atmel_pcm_dma_irq(u32 ssc_sr,
	struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atmel_pcm_dma_params *prtd;

	prtd = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	if (ssc_sr & prtd->mask->ssc_error) {
		if (snd_pcm_running(substream))
			pr_warn("atmel-pcm: buffer %s on %s (SSC_SR=%#x)\n",
				substream->stream == SNDRV_PCM_STREAM_PLAYBACK
				? "underrun" : "overrun", prtd->name,
				ssc_sr);

		/* stop RX and capture: will be enabled again at restart */
		ssc_writex(prtd->ssc->regs, SSC_CR, prtd->mask->ssc_disable);
		snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);

		/* now drain RHR and read status to remove xrun condition */
		ssc_readx(prtd->ssc->regs, SSC_RHR);
		ssc_readx(prtd->ssc->regs, SSC_SR);
	}
}

/*--------------------------------------------------------------------------*\
 * DMAENGINE operations
\*--------------------------------------------------------------------------*/
static bool filter(struct dma_chan *chan, void *slave)
{
	struct at_dma_slave *sl = slave;

	if (sl->dma_dev == chan->device->dev) {
		chan->private = sl;
		return true;
	} else {
		return false;
	}
}

static int atmel_pcm_configure_dma(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct atmel_pcm_dma_params *prtd)
{
	struct ssc_device *ssc;
	struct dma_chan *dma_chan;
	struct dma_slave_config slave_config;
	int ret;

	ssc = prtd->ssc;

	ret = snd_hwparams_to_dma_slave_config(substream, params,
			&slave_config);
	if (ret) {
		pr_err("atmel-pcm: hwparams to dma slave configure failed\n");
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slave_config.dst_addr = (dma_addr_t)ssc->phybase + SSC_THR;
		slave_config.dst_maxburst = 1;
	} else {
		slave_config.src_addr = (dma_addr_t)ssc->phybase + SSC_RHR;
		slave_config.src_maxburst = 1;
	}

	dma_chan = snd_dmaengine_pcm_get_chan(substream);
	if (dmaengine_slave_config(dma_chan, &slave_config)) {
		pr_err("atmel-pcm: failed to configure dma channel\n");
		ret = -EBUSY;
		return ret;
	}

	return 0;
}

static int atmel_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atmel_pcm_dma_params *prtd;
	struct ssc_device *ssc;
	struct at_dma_slave *sdata = NULL;
	int ret;

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	prtd = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	ssc = prtd->ssc;
	if (ssc->pdev)
		sdata = ssc->pdev->dev.platform_data;

	ret = snd_dmaengine_pcm_open(substream, filter, sdata);
	if (ret) {
		pr_err("atmel-pcm: dmaengine pcm open failed\n");
		return -EINVAL;
	}

	ret = atmel_pcm_configure_dma(substream, params, prtd);
	if (ret) {
		pr_err("atmel-pcm: failed to configure dmai\n");
		goto err;
	}

	prtd->dma_intr_handler = atmel_pcm_dma_irq;

	return 0;
err:
	snd_dmaengine_pcm_close(substream);
	return ret;
}

static int atmel_pcm_dma_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct atmel_pcm_dma_params *prtd;

	prtd = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	ssc_writex(prtd->ssc->regs, SSC_IER, prtd->mask->ssc_error);
	ssc_writex(prtd->ssc->regs, SSC_CR, prtd->mask->ssc_enable);

	return 0;
}

static int atmel_pcm_open(struct snd_pcm_substream *substream)
{
	snd_soc_set_runtime_hwparams(substream, &atmel_pcm_dma_hardware);

	return 0;
}

static struct snd_pcm_ops atmel_pcm_ops = {
	.open		= atmel_pcm_open,
	.close		= snd_dmaengine_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= atmel_pcm_hw_params,
	.prepare	= atmel_pcm_dma_prepare,
	.trigger	= snd_dmaengine_pcm_trigger,
	.pointer	= snd_dmaengine_pcm_pointer_no_residue,
	.mmap		= atmel_pcm_mmap,
};

static struct snd_soc_platform_driver atmel_soc_platform = {
	.ops		= &atmel_pcm_ops,
	.pcm_new	= atmel_pcm_new,
	.pcm_free	= atmel_pcm_free,
};

int atmel_pcm_dma_platform_register(struct device *dev)
{
	return snd_soc_register_platform(dev, &atmel_soc_platform);
}
EXPORT_SYMBOL(atmel_pcm_dma_platform_register);

void atmel_pcm_dma_platform_unregister(struct device *dev)
{
	snd_soc_unregister_platform(dev);
}
EXPORT_SYMBOL(atmel_pcm_dma_platform_unregister);

MODULE_AUTHOR("Bo Shen <voice.shen@atmel.com>");
MODULE_DESCRIPTION("Atmel DMA based PCM module");
MODULE_LICENSE("GPL");
