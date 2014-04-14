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
#include <linux/platform_device.h>
#include <linux/dmaengine.h>
#include <linux/types.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "imx-pcm.h"

static bool filter(struct dma_chan *chan, void *param)
{
	if (!imx_dma_is_general_purpose(chan))
		return false;

	chan->private = param;

	return true;
}

static const struct snd_pcm_hardware imx_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_PAUSE |
		SNDRV_PCM_INFO_RESUME,
	.buffer_bytes_max = IMX_DEFAULT_DMABUF_SIZE,
	.period_bytes_min = 128,
	.period_bytes_max = 65535, /* Limited by SDMA engine */
	.periods_min = 2,
	.periods_max = 255,
	.fifo_size = 0,
};

static void imx_pcm_dma_complete(void *arg)
{
	struct snd_pcm_substream *substream = arg;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct dmaengine_pcm_runtime_data *prtd = substream->runtime->private_data;
	struct snd_dmaengine_dai_dma_data *dma_data;

	prtd->pos += snd_pcm_lib_period_bytes(substream);
	if (prtd->pos >= snd_pcm_lib_buffer_bytes(substream))
		prtd->pos = 0;

	snd_pcm_period_elapsed(substream);

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	if (dma_data->check_xrun && dma_data->check_xrun(substream))
		dma_data->device_reset(substream, 1);
}

static int imx_pcm_dma_prepare_slave_config(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct dma_slave_config *slave_config)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_dmaengine_dai_dma_data *dma_data;
	struct dmaengine_pcm_runtime_data *prtd = substream->runtime->private_data;
	int ret;

	dma_data = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	prtd->callback = imx_pcm_dma_complete;

	ret = snd_hwparams_to_dma_slave_config(substream, params, slave_config);
	if (ret)
		return ret;

	snd_dmaengine_pcm_set_config_from_dai_data(substream, dma_data,
		slave_config);

	return 0;

}

static const struct snd_dmaengine_pcm_config imx_dmaengine_pcm_config = {
	.pcm_hardware = &imx_pcm_hardware,
	.prepare_slave_config = imx_pcm_dma_prepare_slave_config,
	.compat_filter_fn = filter,
	.prealloc_buffer_size = IMX_DEFAULT_DMABUF_SIZE,
};

int imx_pcm_dma_init(struct platform_device *pdev, size_t size)
{
	struct snd_dmaengine_pcm_config *config;
	struct snd_pcm_hardware *pcm_hardware;

	config = devm_kzalloc(&pdev->dev,
			sizeof(struct snd_dmaengine_pcm_config), GFP_KERNEL);
	*config = imx_dmaengine_pcm_config;
	if (size)
		config->prealloc_buffer_size = size;

	pcm_hardware = devm_kzalloc(&pdev->dev,
			sizeof(struct snd_pcm_hardware), GFP_KERNEL);
	*pcm_hardware = imx_pcm_hardware;
	if (size)
		pcm_hardware->buffer_bytes_max = size;

	config->pcm_hardware = pcm_hardware;

	return devm_snd_dmaengine_pcm_register(&pdev->dev,
		config,
		SND_DMAENGINE_PCM_FLAG_COMPAT);
}
EXPORT_SYMBOL_GPL(imx_pcm_dma_init);

MODULE_LICENSE("GPL");
