/*
 * imx-pcm-dma-mx2.c  --  ALSA Soc Audio Layer
 *
 * Copyright 2009 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This code is based on code copyrighted by Freescale,
 * Liam Girdwood, Javier Martin and probably others.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <mach/dma-mx1-mx2.h>

#include "imx-ssi.h"

struct imx_pcm_runtime_data {
	int sg_count;
	struct scatterlist *sg_list;
	int period;
	int periods;
	unsigned long dma_addr;
	int dma;
	struct snd_pcm_substream *substream;
	unsigned long offset;
	unsigned long size;
	unsigned long period_cnt;
	void *buf;
	int period_time;
};

/* Called by the DMA framework when a period has elapsed */
static void imx_ssi_dma_progression(int channel, void *data,
					struct scatterlist *sg)
{
	struct snd_pcm_substream *substream = data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	if (!sg)
		return;

	runtime = iprtd->substream->runtime;

	iprtd->offset = sg->dma_address - runtime->dma_addr;

	snd_pcm_period_elapsed(iprtd->substream);
}

static void imx_ssi_dma_callback(int channel, void *data)
{
	pr_err("%s shouldn't be called\n", __func__);
}

static void snd_imx_dma_err_callback(int channel, void *data, int err)
{
	struct snd_pcm_substream *substream = data;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_pcm_dma_params *dma_params = 
		snd_soc_dai_get_dma_data(rtd->dai->cpu_dai, substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;
	int ret;

	pr_err("DMA timeout on channel %d -%s%s%s%s\n",
		 channel,
		 err & IMX_DMA_ERR_BURST ?    " burst" : "",
		 err & IMX_DMA_ERR_REQUEST ?  " request" : "",
		 err & IMX_DMA_ERR_TRANSFER ? " transfer" : "",
		 err & IMX_DMA_ERR_BUFFER ?   " buffer" : "");

	imx_dma_disable(iprtd->dma);
	ret = imx_dma_setup_sg(iprtd->dma, iprtd->sg_list, iprtd->sg_count,
			IMX_DMA_LENGTH_LOOP, dma_params->dma_addr,
			substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			DMA_MODE_WRITE : DMA_MODE_READ);
	if (!ret)
		imx_dma_enable(iprtd->dma);
}

static int imx_ssi_dma_alloc(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_pcm_dma_params *dma_params;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;
	int ret;

	dma_params = snd_soc_dai_get_dma_data(rtd->dai->cpu_dai, substream);

	iprtd->dma = imx_dma_request_by_prio(DRV_NAME, DMA_PRIO_HIGH);
	if (iprtd->dma < 0) {
		pr_err("Failed to claim the audio DMA\n");
		return -ENODEV;
	}

	ret = imx_dma_setup_handlers(iprtd->dma,
				imx_ssi_dma_callback,
				snd_imx_dma_err_callback, substream);
	if (ret)
		goto out;

	ret = imx_dma_setup_progression_handler(iprtd->dma,
			imx_ssi_dma_progression);
	if (ret) {
		pr_err("Failed to setup the DMA handler\n");
		goto out;
	}

	ret = imx_dma_config_channel(iprtd->dma,
			IMX_DMA_MEMSIZE_16 | IMX_DMA_TYPE_FIFO,
			IMX_DMA_MEMSIZE_32 | IMX_DMA_TYPE_LINEAR,
			dma_params->dma, 1);
	if (ret < 0) {
		pr_err("Cannot configure DMA channel: %d\n", ret);
		goto out;
	}

	imx_dma_config_burstlen(iprtd->dma, dma_params->burstsize * 2);

	return 0;
out:
	imx_dma_free(iprtd->dma);
	return ret;
}

static int snd_imx_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;
	int i;
	unsigned long dma_addr;

	imx_ssi_dma_alloc(substream);

	iprtd->size = params_buffer_bytes(params);
	iprtd->periods = params_periods(params);
	iprtd->period = params_period_bytes(params);
	iprtd->offset = 0;
	iprtd->period_time = HZ / (params_rate(params) /
			params_period_size(params));

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	if (iprtd->sg_count != iprtd->periods) {
		kfree(iprtd->sg_list);

		iprtd->sg_list = kcalloc(iprtd->periods + 1,
				sizeof(struct scatterlist), GFP_KERNEL);
		if (!iprtd->sg_list)
			return -ENOMEM;
		iprtd->sg_count = iprtd->periods + 1;
	}

	sg_init_table(iprtd->sg_list, iprtd->sg_count);
	dma_addr = runtime->dma_addr;

	for (i = 0; i < iprtd->periods; i++) {
		iprtd->sg_list[i].page_link = 0;
		iprtd->sg_list[i].offset = 0;
		iprtd->sg_list[i].dma_address = dma_addr;
		iprtd->sg_list[i].length = iprtd->period;
		dma_addr += iprtd->period;
	}

	/* close the loop */
	iprtd->sg_list[iprtd->sg_count - 1].offset = 0;
	iprtd->sg_list[iprtd->sg_count - 1].length = 0;
	iprtd->sg_list[iprtd->sg_count - 1].page_link =
			((unsigned long) iprtd->sg_list | 0x01) & ~0x02;
	return 0;
}

static int snd_imx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	if (iprtd->dma >= 0) {
		imx_dma_free(iprtd->dma);
		iprtd->dma = -EINVAL;
	}

	kfree(iprtd->sg_list);
	iprtd->sg_list = NULL;

	return 0;
}

static int snd_imx_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_pcm_dma_params *dma_params;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;
	int err;

	dma_params = snd_soc_dai_get_dma_data(rtd->dai->cpu_dai, substream);

	iprtd->substream = substream;
	iprtd->buf = (unsigned int *)substream->dma_buffer.area;
	iprtd->period_cnt = 0;

	pr_debug("%s: buf: %p period: %d periods: %d\n",
			__func__, iprtd->buf, iprtd->period, iprtd->periods);

	err = imx_dma_setup_sg(iprtd->dma, iprtd->sg_list, iprtd->sg_count,
			IMX_DMA_LENGTH_LOOP, dma_params->dma_addr,
			substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			DMA_MODE_WRITE : DMA_MODE_READ);
	if (err)
		return err;

	return 0;
}

static int snd_imx_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		imx_dma_enable(iprtd->dma);

		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		imx_dma_disable(iprtd->dma);

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static snd_pcm_uframes_t snd_imx_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	return bytes_to_frames(substream->runtime, iprtd->offset);
}

static struct snd_pcm_hardware snd_imx_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rate_min = 8000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = IMX_SSI_DMABUF_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = 16 * 1024,
	.periods_min = 2,
	.periods_max = 255,
	.fifo_size = 0,
};

static int snd_imx_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd;
	int ret;

	iprtd = kzalloc(sizeof(*iprtd), GFP_KERNEL);
	runtime->private_data = iprtd;

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	snd_soc_set_runtime_hwparams(substream, &snd_imx_hardware);
	return 0;
}

static struct snd_pcm_ops imx_pcm_ops = {
	.open		= snd_imx_open,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= snd_imx_pcm_hw_params,
	.hw_free	= snd_imx_pcm_hw_free,
	.prepare	= snd_imx_pcm_prepare,
	.trigger	= snd_imx_pcm_trigger,
	.pointer	= snd_imx_pcm_pointer,
	.mmap		= snd_imx_pcm_mmap,
};

static struct snd_soc_platform imx_soc_platform_dma = {
	.name		= "imx-audio",
	.pcm_ops 	= &imx_pcm_ops,
	.pcm_new	= imx_pcm_new,
	.pcm_free	= imx_pcm_free,
};

struct snd_soc_platform *imx_ssi_dma_mx2_init(struct platform_device *pdev,
		struct imx_ssi *ssi)
{
	ssi->dma_params_tx.burstsize = DMA_TXFIFO_BURST;
	ssi->dma_params_rx.burstsize = DMA_RXFIFO_BURST;

	return &imx_soc_platform_dma;
}

