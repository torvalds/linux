/*
 * imx-pcm-fiq.c  --  ALSA Soc Audio Layer
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

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <asm/fiq.h>

#include <mach/ssi.h>

#include "imx-ssi.h"

struct imx_pcm_runtime_data {
	int period;
	int periods;
	unsigned long offset;
	unsigned long size;
	struct timer_list timer;
	int period_time;
};

static void imx_ssi_timer_callback(unsigned long data)
{
	struct snd_pcm_substream *substream = (void *)data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;
	struct pt_regs regs;

	get_fiq_regs(&regs);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		iprtd->offset = regs.ARM_r8 & 0xffff;
	else
		iprtd->offset = regs.ARM_r9 & 0xffff;

	iprtd->timer.expires = jiffies + iprtd->period_time;
	add_timer(&iprtd->timer);
	snd_pcm_period_elapsed(substream);
}

static struct fiq_handler fh = {
	.name		= DRV_NAME,
};

static int snd_imx_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	iprtd->size = params_buffer_bytes(params);
	iprtd->periods = params_periods(params);
	iprtd->period = params_period_bytes(params);
	iprtd->offset = 0;
	iprtd->period_time = HZ / (params_rate(params) / params_period_size(params));

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int snd_imx_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;
	struct pt_regs regs;

	get_fiq_regs(&regs);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		regs.ARM_r8 = (iprtd->period * iprtd->periods - 1) << 16;
	else
		regs.ARM_r9 = (iprtd->period * iprtd->periods - 1) << 16;

	set_fiq_regs(&regs);

	return 0;
}

static int fiq_enable;
static int imx_pcm_fiq;

static int snd_imx_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		iprtd->timer.expires = jiffies + iprtd->period_time;
		add_timer(&iprtd->timer);
		if (++fiq_enable == 1)
			enable_fiq(imx_pcm_fiq);

		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		del_timer(&iprtd->timer);
		if (--fiq_enable == 0)
			disable_fiq(imx_pcm_fiq);


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

	init_timer(&iprtd->timer);
	iprtd->timer.data = (unsigned long)substream;
	iprtd->timer.function = imx_ssi_timer_callback;

	ret = snd_pcm_hw_constraint_integer(substream->runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		return ret;

	snd_soc_set_runtime_hwparams(substream, &snd_imx_hardware);
	return 0;
}

static int snd_imx_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct imx_pcm_runtime_data *iprtd = runtime->private_data;

	del_timer_sync(&iprtd->timer);
	kfree(iprtd);

	return 0;
}

static struct snd_pcm_ops imx_pcm_ops = {
	.open		= snd_imx_open,
	.close		= snd_imx_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= snd_imx_pcm_hw_params,
	.prepare	= snd_imx_pcm_prepare,
	.trigger	= snd_imx_pcm_trigger,
	.pointer	= snd_imx_pcm_pointer,
	.mmap		= snd_imx_pcm_mmap,
};

static int imx_pcm_fiq_new(struct snd_card *card, struct snd_soc_dai *dai,
	struct snd_pcm *pcm)
{
	int ret;

	ret = imx_pcm_new(card, dai, pcm);
	if (ret)
		return ret;

	if (dai->playback.channels_min) {
		struct snd_pcm_substream *substream =
			pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
		struct snd_dma_buffer *buf = &substream->dma_buffer;

		imx_ssi_fiq_tx_buffer = (unsigned long)buf->area;
	}

	if (dai->capture.channels_min) {
		struct snd_pcm_substream *substream =
			pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream;
		struct snd_dma_buffer *buf = &substream->dma_buffer;

		imx_ssi_fiq_rx_buffer = (unsigned long)buf->area;
	}

	set_fiq_handler(&imx_ssi_fiq_start,
		&imx_ssi_fiq_end - &imx_ssi_fiq_start);

	return 0;
}

static struct snd_soc_platform imx_soc_platform_fiq = {
	.pcm_ops 	= &imx_pcm_ops,
	.pcm_new	= imx_pcm_fiq_new,
	.pcm_free	= imx_pcm_free,
};

struct snd_soc_platform *imx_ssi_fiq_init(struct platform_device *pdev,
		struct imx_ssi *ssi)
{
	int ret = 0;

	ret = claim_fiq(&fh);
	if (ret) {
		dev_err(&pdev->dev, "failed to claim fiq: %d", ret);
		return ERR_PTR(ret);
	}

	mxc_set_irq_fiq(ssi->irq, 1);

	imx_pcm_fiq = ssi->irq;

	imx_ssi_fiq_base = (unsigned long)ssi->base;

	ssi->dma_params_tx.burstsize = 4;
	ssi->dma_params_rx.burstsize = 6;

	return &imx_soc_platform_fiq;
}

void imx_ssi_fiq_exit(struct platform_device *pdev,
		struct imx_ssi *ssi)
{
	mxc_set_irq_fiq(ssi->irq, 0);
	release_fiq(&fh);
}

