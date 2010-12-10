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
#include <linux/dmaengine.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <mach/dma.h>

#include "imx-ssi.h"

struct imx_pcm_runtime_data {
	int period_bytes;
	int periods;
	int dma;
	unsigned long offset;
	unsigned long size;
	void *buf;
	int period_time;
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *dma_chan;
	struct imx_dma_data dma_data;
};

static void audio_dma_irq(void *data)
{
	struct snd_pcm_substream *substream = (struct snd_pcm_substream *)data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	iprtd->offset += iprtd->period_bytes;
	iprtd->offset %= iprtd->period_bytes * iprtd->periods;

	snd_pcm_period_elapsed(substream);
}

static bool filter(struct dma_chan *chan, void *param)
{
	struct imx_pcm_runtime_data *iprtd = param;

	if (!imx_dma_is_general_purpose(chan))
		return false;

        chan->private = &iprtd->dma_data;

        return true;
}

static int imx_ssi_dma_alloc(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_pcm_dma_params *dma_params;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;
	struct dma_slave_config slave_config;
	dma_cap_mask_t mask;
	enum dma_slave_buswidth buswidth;
	int ret;

	dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

	iprtd->dma_data.peripheral_type = IMX_DMATYPE_SSI;
	iprtd->dma_data.priority = DMA_PRIO_HIGH;
	iprtd->dma_data.dma_request = dma_params->dma;

	/* Try to grab a DMA channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	iprtd->dma_chan = dma_request_channel(mask, filter, iprtd);
	if (!iprtd->dma_chan)
		return -EINVAL;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
	case SNDRV_PCM_FORMAT_S24_LE:
		buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;
	default:
		return 0;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slave_config.direction = DMA_TO_DEVICE;
		slave_config.dst_addr = dma_params->dma_addr;
		slave_config.dst_addr_width = buswidth;
		slave_config.dst_maxburst = dma_params->burstsize;
	} else {
		slave_config.direction = DMA_FROM_DEVICE;
		slave_config.src_addr = dma_params->dma_addr;
		slave_config.src_addr_width = buswidth;
		slave_config.src_maxburst = dma_params->burstsize;
	}

	ret = dmaengine_slave_config(iprtd->dma_chan, &slave_config);
	if (ret)
		return ret;

	return 0;
}

static int snd_imx_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;
	unsigned long dma_addr;
	struct dma_chan *chan;
	struct imx_pcm_dma_params *dma_params;
	int ret;

	dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);
	ret = imx_ssi_dma_alloc(substream, params);
	if (ret)
		return ret;
	chan = iprtd->dma_chan;

	iprtd->size = params_buffer_bytes(params);
	iprtd->periods = params_periods(params);
	iprtd->period_bytes = params_period_bytes(params);
	iprtd->offset = 0;
	iprtd->period_time = HZ / (params_rate(params) /
			params_period_size(params));

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	dma_addr = runtime->dma_addr;

	iprtd->buf = (unsigned int *)substream->dma_buffer.area;

	iprtd->desc = chan->device->device_prep_dma_cyclic(chan, dma_addr,
			iprtd->period_bytes * iprtd->periods,
			iprtd->period_bytes,
			substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (!iprtd->desc) {
		dev_err(&chan->dev->device, "cannot prepare slave dma\n");
		return -EINVAL;
	}

	iprtd->desc->callback = audio_dma_irq;
	iprtd->desc->callback_param = substream;

	return 0;
}

static int snd_imx_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	if (iprtd->dma_chan) {
		dma_release_channel(iprtd->dma_chan);
		iprtd->dma_chan = NULL;
	}

	return 0;
}

static int snd_imx_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct imx_pcm_dma_params *dma_params;

	dma_params = snd_soc_dai_get_dma_data(rtd->cpu_dai, substream);

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
		dmaengine_submit(iprtd->desc);

		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_terminate_all(iprtd->dma_chan);

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

	pr_debug("%s: %ld %ld\n", __func__, iprtd->offset,
			bytes_to_frames(substream->runtime, iprtd->offset));

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
	.period_bytes_max = 65535, /* Limited by SDMA engine */
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
	if (iprtd == NULL)
		return -ENOMEM;
	runtime->private_data = iprtd;

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		kfree(iprtd);
		return ret;
	}

	snd_soc_set_runtime_hwparams(substream, &snd_imx_hardware);

	return 0;
}

static int snd_imx_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	kfree(iprtd);

	return 0;
}

static struct snd_pcm_ops imx_pcm_ops = {
	.open		= snd_imx_open,
	.close		= snd_imx_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= snd_imx_pcm_hw_params,
	.hw_free	= snd_imx_pcm_hw_free,
	.prepare	= snd_imx_pcm_prepare,
	.trigger	= snd_imx_pcm_trigger,
	.pointer	= snd_imx_pcm_pointer,
	.mmap		= snd_imx_pcm_mmap,
};

static struct snd_soc_platform_driver imx_soc_platform_mx2 = {
	.ops		= &imx_pcm_ops,
	.pcm_new	= imx_pcm_new,
	.pcm_free	= imx_pcm_free,
};

static int __devinit imx_soc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_platform(&pdev->dev, &imx_soc_platform_mx2);
}

static int __devexit imx_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static struct platform_driver imx_pcm_driver = {
	.driver = {
			.name = "imx-pcm-audio",
			.owner = THIS_MODULE,
	},
	.probe = imx_soc_platform_probe,
	.remove = __devexit_p(imx_soc_platform_remove),
};

static int __init snd_imx_pcm_init(void)
{
	return platform_driver_register(&imx_pcm_driver);
}
module_init(snd_imx_pcm_init);

static void __exit snd_imx_pcm_exit(void)
{
	platform_driver_unregister(&imx_pcm_driver);
}
module_exit(snd_imx_pcm_exit);
