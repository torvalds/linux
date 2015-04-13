/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * lpass-platform.c -- ALSA SoC platform driver for QTi LPASS
 */

#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <sound/memalloc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "lpass-lpaif-ipq806x.h"
#include "lpass.h"

#define LPASS_PLATFORM_BUFFER_SIZE	(16 * 1024)
#define LPASS_PLATFORM_PERIODS		2

static struct snd_pcm_hardware lpass_platform_pcm_hardware = {
	.info			=	SNDRV_PCM_INFO_MMAP |
					SNDRV_PCM_INFO_MMAP_VALID |
					SNDRV_PCM_INFO_INTERLEAVED |
					SNDRV_PCM_INFO_PAUSE |
					SNDRV_PCM_INFO_RESUME,
	.formats		=	SNDRV_PCM_FMTBIT_S16 |
					SNDRV_PCM_FMTBIT_S24 |
					SNDRV_PCM_FMTBIT_S32,
	.rates			=	SNDRV_PCM_RATE_8000_192000,
	.rate_min		=	8000,
	.rate_max		=	192000,
	.channels_min		=	1,
	.channels_max		=	8,
	.buffer_bytes_max	=	LPASS_PLATFORM_BUFFER_SIZE,
	.period_bytes_max	=	LPASS_PLATFORM_BUFFER_SIZE /
						LPASS_PLATFORM_PERIODS,
	.period_bytes_min	=	LPASS_PLATFORM_BUFFER_SIZE /
						LPASS_PLATFORM_PERIODS,
	.periods_min		=	LPASS_PLATFORM_PERIODS,
	.periods_max		=	LPASS_PLATFORM_PERIODS,
	.fifo_size		=	0,
};

static int lpass_platform_pcmops_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &lpass_platform_pcm_hardware);

	runtime->dma_bytes = lpass_platform_pcm_hardware.buffer_bytes_max;

	ret = snd_pcm_hw_constraint_integer(runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0) {
		dev_err(soc_runtime->dev, "%s() setting constraints failed: %d\n",
				__func__, ret);
		return -EINVAL;
	}

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int lpass_platform_pcmops_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata =
		snd_soc_platform_get_drvdata(soc_runtime->platform);
	snd_pcm_format_t format = params_format(params);
	unsigned int channels = params_channels(params);
	unsigned int regval;
	int bitwidth;
	int ret;

	bitwidth = snd_pcm_format_width(format);
	if (bitwidth < 0) {
		dev_err(soc_runtime->dev, "%s() invalid bit width given: %d\n",
				__func__, bitwidth);
		return bitwidth;
	}

	regval = LPAIF_RDMACTL_BURSTEN_INCR4 |
			LPAIF_RDMACTL_AUDINTF_MI2S |
			LPAIF_RDMACTL_FIFOWM_8;

	switch (bitwidth) {
	case 16:
		switch (channels) {
		case 1:
		case 2:
			regval |= LPAIF_RDMACTL_WPSCNT_ONE;
			break;
		case 4:
			regval |= LPAIF_RDMACTL_WPSCNT_TWO;
			break;
		case 6:
			regval |= LPAIF_RDMACTL_WPSCNT_THREE;
			break;
		case 8:
			regval |= LPAIF_RDMACTL_WPSCNT_FOUR;
			break;
		default:
			dev_err(soc_runtime->dev, "%s() invalid PCM config given: bw=%d, ch=%u\n",
					__func__, bitwidth, channels);
			return -EINVAL;
		}
		break;
	case 24:
	case 32:
		switch (channels) {
		case 1:
			regval |= LPAIF_RDMACTL_WPSCNT_ONE;
			break;
		case 2:
			regval |= LPAIF_RDMACTL_WPSCNT_TWO;
			break;
		case 4:
			regval |= LPAIF_RDMACTL_WPSCNT_FOUR;
			break;
		case 6:
			regval |= LPAIF_RDMACTL_WPSCNT_SIX;
			break;
		case 8:
			regval |= LPAIF_RDMACTL_WPSCNT_EIGHT;
			break;
		default:
			dev_err(soc_runtime->dev, "%s() invalid PCM config given: bw=%d, ch=%u\n",
					__func__, bitwidth, channels);
			return -EINVAL;
		}
		break;
	default:
		dev_err(soc_runtime->dev, "%s() invalid PCM config given: bw=%d, ch=%u\n",
				__func__, bitwidth, channels);
		return -EINVAL;
	}

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_RDMACTL_REG(LPAIF_RDMA_CHAN_MI2S), regval);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error writing to rdmactl reg: %d\n",
				__func__, ret);
		return ret;
	}

	return 0;
}

static int lpass_platform_pcmops_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata =
		snd_soc_platform_get_drvdata(soc_runtime->platform);
	int ret;

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_RDMACTL_REG(LPAIF_RDMA_CHAN_MI2S), 0);
	if (ret)
		dev_err(soc_runtime->dev, "%s() error writing to rdmactl reg: %d\n",
				__func__, ret);

	return ret;
}

static int lpass_platform_pcmops_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata =
		snd_soc_platform_get_drvdata(soc_runtime->platform);
	int ret;

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_RDMABASE_REG(LPAIF_RDMA_CHAN_MI2S),
			runtime->dma_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error writing to rdmabase reg: %d\n",
				__func__, ret);
		return ret;
	}

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_RDMABUFF_REG(LPAIF_RDMA_CHAN_MI2S),
			(snd_pcm_lib_buffer_bytes(substream) >> 2) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error writing to rdmabuff reg: %d\n",
				__func__, ret);
		return ret;
	}

	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_RDMAPER_REG(LPAIF_RDMA_CHAN_MI2S),
			(snd_pcm_lib_period_bytes(substream) >> 2) - 1);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error writing to rdmaper reg: %d\n",
				__func__, ret);
		return ret;
	}

	ret = regmap_update_bits(drvdata->lpaif_map,
			LPAIF_RDMACTL_REG(LPAIF_RDMA_CHAN_MI2S),
			LPAIF_RDMACTL_ENABLE_MASK, LPAIF_RDMACTL_ENABLE_ON);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error writing to rdmactl reg: %d\n",
				__func__, ret);
		return ret;
	}

	return 0;
}

static int lpass_platform_pcmops_trigger(struct snd_pcm_substream *substream,
		int cmd)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata =
		snd_soc_platform_get_drvdata(soc_runtime->platform);
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		/* clear status before enabling interrupts */
		ret = regmap_write(drvdata->lpaif_map,
				LPAIF_IRQCLEAR_REG(LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_ALL(LPAIF_RDMA_CHAN_MI2S));
		if (ret) {
			dev_err(soc_runtime->dev, "%s() error writing to irqclear reg: %d\n",
					__func__, ret);
			return ret;
		}

		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_IRQEN_REG(LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_ALL(LPAIF_RDMA_CHAN_MI2S),
				LPAIF_IRQ_ALL(LPAIF_RDMA_CHAN_MI2S));
		if (ret) {
			dev_err(soc_runtime->dev, "%s() error writing to irqen reg: %d\n",
					__func__, ret);
			return ret;
		}

		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_RDMACTL_REG(LPAIF_RDMA_CHAN_MI2S),
				LPAIF_RDMACTL_ENABLE_MASK,
				LPAIF_RDMACTL_ENABLE_ON);
		if (ret) {
			dev_err(soc_runtime->dev, "%s() error writing to rdmactl reg: %d\n",
					__func__, ret);
			return ret;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_RDMACTL_REG(LPAIF_RDMA_CHAN_MI2S),
				LPAIF_RDMACTL_ENABLE_MASK,
				LPAIF_RDMACTL_ENABLE_OFF);
		if (ret) {
			dev_err(soc_runtime->dev, "%s() error writing to rdmactl reg: %d\n",
					__func__, ret);
			return ret;
		}

		ret = regmap_update_bits(drvdata->lpaif_map,
				LPAIF_IRQEN_REG(LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_ALL(LPAIF_RDMA_CHAN_MI2S), 0);
		if (ret) {
			dev_err(soc_runtime->dev, "%s() error writing to irqen reg: %d\n",
					__func__, ret);
			return ret;
		}
		break;
	}

	return 0;
}

static snd_pcm_uframes_t lpass_platform_pcmops_pointer(
		struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata =
			snd_soc_platform_get_drvdata(soc_runtime->platform);
	unsigned int base_addr, curr_addr;
	int ret;

	ret = regmap_read(drvdata->lpaif_map,
			LPAIF_RDMABASE_REG(LPAIF_RDMA_CHAN_MI2S), &base_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error reading from rdmabase reg: %d\n",
				__func__, ret);
		return ret;
	}

	ret = regmap_read(drvdata->lpaif_map,
			LPAIF_RDMACURR_REG(LPAIF_RDMA_CHAN_MI2S), &curr_addr);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error reading from rdmacurr reg: %d\n",
				__func__, ret);
		return ret;
	}

	return bytes_to_frames(substream->runtime, curr_addr - base_addr);
}

static int lpass_platform_pcmops_mmap(struct snd_pcm_substream *substream,
		struct vm_area_struct *vma)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	return dma_mmap_coherent(substream->pcm->card->dev, vma,
			runtime->dma_area, runtime->dma_addr,
			runtime->dma_bytes);
}

static struct snd_pcm_ops lpass_platform_pcm_ops = {
	.open		= lpass_platform_pcmops_open,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= lpass_platform_pcmops_hw_params,
	.hw_free	= lpass_platform_pcmops_hw_free,
	.prepare	= lpass_platform_pcmops_prepare,
	.trigger	= lpass_platform_pcmops_trigger,
	.pointer	= lpass_platform_pcmops_pointer,
	.mmap		= lpass_platform_pcmops_mmap,
};

static irqreturn_t lpass_platform_lpaif_irq(int irq, void *data)
{
	struct snd_pcm_substream *substream = data;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;
	struct lpass_data *drvdata =
		snd_soc_platform_get_drvdata(soc_runtime->platform);
	unsigned int interrupts;
	irqreturn_t ret = IRQ_NONE;
	int rv;

	rv = regmap_read(drvdata->lpaif_map,
			LPAIF_IRQSTAT_REG(LPAIF_IRQ_PORT_HOST), &interrupts);
	if (rv) {
		dev_err(soc_runtime->dev, "%s() error reading from irqstat reg: %d\n",
				__func__, rv);
		return IRQ_NONE;
	}
	interrupts &= LPAIF_IRQ_ALL(LPAIF_RDMA_CHAN_MI2S);

	if (interrupts & LPAIF_IRQ_PER(LPAIF_RDMA_CHAN_MI2S)) {
		rv = regmap_write(drvdata->lpaif_map,
				LPAIF_IRQCLEAR_REG(LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_PER(LPAIF_RDMA_CHAN_MI2S));
		if (rv) {
			dev_err(soc_runtime->dev, "%s() error writing to irqclear reg: %d\n",
					__func__, rv);
			return IRQ_NONE;
		}
		snd_pcm_period_elapsed(substream);
		ret = IRQ_HANDLED;
	}

	if (interrupts & LPAIF_IRQ_XRUN(LPAIF_RDMA_CHAN_MI2S)) {
		rv = regmap_write(drvdata->lpaif_map,
				LPAIF_IRQCLEAR_REG(LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_XRUN(LPAIF_RDMA_CHAN_MI2S));
		if (rv) {
			dev_err(soc_runtime->dev, "%s() error writing to irqclear reg: %d\n",
					__func__, rv);
			return IRQ_NONE;
		}
		dev_warn(soc_runtime->dev, "%s() xrun warning\n", __func__);
		snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
		ret = IRQ_HANDLED;
	}

	if (interrupts & LPAIF_IRQ_ERR(LPAIF_RDMA_CHAN_MI2S)) {
		rv = regmap_write(drvdata->lpaif_map,
				LPAIF_IRQCLEAR_REG(LPAIF_IRQ_PORT_HOST),
				LPAIF_IRQ_ERR(LPAIF_RDMA_CHAN_MI2S));
		if (rv) {
			dev_err(soc_runtime->dev, "%s() error writing to irqclear reg: %d\n",
					__func__, rv);
			return IRQ_NONE;
		}
		dev_err(soc_runtime->dev, "%s() bus access error\n", __func__);
		snd_pcm_stop(substream, SNDRV_PCM_STATE_DISCONNECTED);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int lpass_platform_alloc_buffer(struct snd_pcm_substream *substream,
		struct snd_soc_pcm_runtime *soc_runtime)
{
	struct snd_dma_buffer *buf = &substream->dma_buffer;
	size_t size = lpass_platform_pcm_hardware.buffer_bytes_max;

	buf->dev.type = SNDRV_DMA_TYPE_DEV;
	buf->dev.dev = soc_runtime->dev;
	buf->private_data = NULL;
	buf->area = dma_alloc_coherent(soc_runtime->dev, size, &buf->addr,
			GFP_KERNEL);
	if (!buf->area) {
		dev_err(soc_runtime->dev, "%s: Could not allocate DMA buffer\n",
				__func__);
		return -ENOMEM;
	}
	buf->bytes = size;

	return 0;
}

static void lpass_platform_free_buffer(struct snd_pcm_substream *substream,
		struct snd_soc_pcm_runtime *soc_runtime)
{
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	if (buf->area) {
		dma_free_coherent(soc_runtime->dev, buf->bytes, buf->area,
				buf->addr);
	}
	buf->area = NULL;
}

static int lpass_platform_pcm_new(struct snd_soc_pcm_runtime *soc_runtime)
{
	struct snd_pcm *pcm = soc_runtime->pcm;
	struct snd_pcm_substream *substream =
		pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	struct lpass_data *drvdata =
		snd_soc_platform_get_drvdata(soc_runtime->platform);
	int ret;

	soc_runtime->dev->coherent_dma_mask = DMA_BIT_MASK(32);
	soc_runtime->dev->dma_mask = &soc_runtime->dev->coherent_dma_mask;

	ret = lpass_platform_alloc_buffer(substream, soc_runtime);
	if (ret)
		return ret;

	ret = devm_request_irq(soc_runtime->dev, drvdata->lpaif_irq,
			lpass_platform_lpaif_irq, IRQF_TRIGGER_RISING,
			"lpass-irq-lpaif", substream);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() irq request failed: %d\n",
				__func__, ret);
		goto err_buf;
	}

	/* ensure audio hardware is disabled */
	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_IRQEN_REG(LPAIF_IRQ_PORT_HOST), 0);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error writing to irqen reg: %d\n",
				__func__, ret);
		return ret;
	}
	ret = regmap_write(drvdata->lpaif_map,
			LPAIF_RDMACTL_REG(LPAIF_RDMA_CHAN_MI2S), 0);
	if (ret) {
		dev_err(soc_runtime->dev, "%s() error writing to rdmactl reg: %d\n",
				__func__, ret);
		return ret;
	}

	return 0;

err_buf:
	lpass_platform_free_buffer(substream, soc_runtime);
	return ret;
}

static void lpass_platform_pcm_free(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream =
		pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream;
	struct snd_soc_pcm_runtime *soc_runtime = substream->private_data;

	lpass_platform_free_buffer(substream, soc_runtime);
}

static struct snd_soc_platform_driver lpass_platform_driver = {
	.pcm_new	= lpass_platform_pcm_new,
	.pcm_free	= lpass_platform_pcm_free,
	.ops		= &lpass_platform_pcm_ops,
};

int asoc_qcom_lpass_platform_register(struct platform_device *pdev)
{
	struct lpass_data *drvdata = platform_get_drvdata(pdev);

	drvdata->lpaif_irq = platform_get_irq_byname(pdev, "lpass-irq-lpaif");
	if (drvdata->lpaif_irq < 0) {
		dev_err(&pdev->dev, "%s() error getting irq handle: %d\n",
				__func__, drvdata->lpaif_irq);
		return -ENODEV;
	}

	return devm_snd_soc_register_platform(&pdev->dev,
			&lpass_platform_driver);
}
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_platform_register);

MODULE_DESCRIPTION("QTi LPASS Platform Driver");
MODULE_LICENSE("GPL v2");
