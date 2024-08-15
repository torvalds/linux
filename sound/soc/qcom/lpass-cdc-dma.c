// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 *
 * lpass-cdc-dma.c -- ALSA SoC CDC DMA CPU DAI driver for QTi LPASS
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/export.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "lpass-lpaif-reg.h"
#include "lpass.h"

#define CODEC_MEM_HZ_NORMAL 153600000

enum codec_dma_interfaces {
	LPASS_CDC_DMA_INTERFACE1 = 1,
	LPASS_CDC_DMA_INTERFACE2,
	LPASS_CDC_DMA_INTERFACE3,
	LPASS_CDC_DMA_INTERFACE4,
	LPASS_CDC_DMA_INTERFACE5,
	LPASS_CDC_DMA_INTERFACE6,
	LPASS_CDC_DMA_INTERFACE7,
	LPASS_CDC_DMA_INTERFACE8,
	LPASS_CDC_DMA_INTERFACE9,
	LPASS_CDC_DMA_INTERFACE10,
};

static void __lpass_get_dmactl_handle(struct snd_pcm_substream *substream, struct snd_soc_dai *dai,
				      struct lpaif_dmactl **dmactl, int *id)
{
	struct snd_soc_pcm_runtime *soc_runtime = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(soc_runtime, 0);
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *rt = substream->runtime;
	struct lpass_pcm_data *pcm_data = rt->private_data;
	struct lpass_variant *v = drvdata->variant;
	unsigned int dai_id = cpu_dai->driver->id;

	switch (dai_id) {
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
		*dmactl = drvdata->rxtx_rd_dmactl;
		*id = pcm_data->dma_ch;
		break;
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		*dmactl = drvdata->rxtx_wr_dmactl;
		*id = pcm_data->dma_ch - v->rxtx_wrdma_channel_start;
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX8:
		*dmactl = drvdata->va_wr_dmactl;
		*id = pcm_data->dma_ch - v->va_wrdma_channel_start;
		break;
	default:
		dev_err(soc_runtime->dev, "invalid dai id for dma ctl: %d\n", dai_id);
		break;
	}
}

static int __lpass_get_codec_dma_intf_type(int dai_id)
{
	int ret;

	switch (dai_id) {
	case LPASS_CDC_DMA_RX0:
	case LPASS_CDC_DMA_TX0:
	case LPASS_CDC_DMA_VA_TX0:
		ret = LPASS_CDC_DMA_INTERFACE1;
		break;
	case LPASS_CDC_DMA_RX1:
	case LPASS_CDC_DMA_TX1:
	case LPASS_CDC_DMA_VA_TX1:
		ret = LPASS_CDC_DMA_INTERFACE2;
		break;
	case LPASS_CDC_DMA_RX2:
	case LPASS_CDC_DMA_TX2:
	case LPASS_CDC_DMA_VA_TX2:
		ret = LPASS_CDC_DMA_INTERFACE3;
		break;
	case LPASS_CDC_DMA_RX3:
	case LPASS_CDC_DMA_TX3:
	case LPASS_CDC_DMA_VA_TX3:
		ret = LPASS_CDC_DMA_INTERFACE4;
		break;
	case LPASS_CDC_DMA_RX4:
	case LPASS_CDC_DMA_TX4:
	case LPASS_CDC_DMA_VA_TX4:
		ret = LPASS_CDC_DMA_INTERFACE5;
		break;
	case LPASS_CDC_DMA_RX5:
	case LPASS_CDC_DMA_TX5:
	case LPASS_CDC_DMA_VA_TX5:
		ret = LPASS_CDC_DMA_INTERFACE6;
		break;
	case LPASS_CDC_DMA_RX6:
	case LPASS_CDC_DMA_TX6:
	case LPASS_CDC_DMA_VA_TX6:
		ret = LPASS_CDC_DMA_INTERFACE7;
		break;
	case LPASS_CDC_DMA_RX7:
	case LPASS_CDC_DMA_TX7:
	case LPASS_CDC_DMA_VA_TX7:
		ret = LPASS_CDC_DMA_INTERFACE8;
		break;
	case LPASS_CDC_DMA_RX8:
	case LPASS_CDC_DMA_TX8:
	case LPASS_CDC_DMA_VA_TX8:
		ret = LPASS_CDC_DMA_INTERFACE9;
		break;
	case LPASS_CDC_DMA_RX9:
		ret  = LPASS_CDC_DMA_INTERFACE10;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int __lpass_platform_codec_intf_init(struct snd_soc_dai *dai,
					    struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *soc_runtime = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(soc_runtime, 0);
	struct lpaif_dmactl *dmactl = NULL;
	struct device *dev = soc_runtime->dev;
	int ret, id, codec_intf;
	unsigned int dai_id = cpu_dai->driver->id;

	codec_intf = __lpass_get_codec_dma_intf_type(dai_id);
	if (codec_intf < 0) {
		dev_err(dev, "failed to get codec_intf: %d\n", codec_intf);
		return codec_intf;
	}

	__lpass_get_dmactl_handle(substream, dai, &dmactl, &id);
	if (!dmactl)
		return -EINVAL;

	ret = regmap_fields_write(dmactl->codec_intf, id, codec_intf);
	if (ret) {
		dev_err(dev, "error writing to dmactl codec_intf reg field: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(dmactl->codec_fs_sel, id, 0x0);
	if (ret) {
		dev_err(dev, "error writing to dmactl codec_fs_sel reg field: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(dmactl->codec_fs_delay, id, 0x0);
	if (ret) {
		dev_err(dev, "error writing to dmactl codec_fs_delay reg field: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(dmactl->codec_pack, id, 0x1);
	if (ret) {
		dev_err(dev, "error writing to dmactl codec_pack reg field: %d\n", ret);
		return ret;
	}
	ret = regmap_fields_write(dmactl->codec_enable, id, LPAIF_DMACTL_ENABLE_ON);
	if (ret) {
		dev_err(dev, "error writing to dmactl codec_enable reg field: %d\n", ret);
		return ret;
	}
	return 0;
}

static int lpass_cdc_dma_daiops_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *soc_runtime = asoc_substream_to_rtd(substream);

	switch (dai->id) {
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		clk_set_rate(drvdata->codec_mem0, CODEC_MEM_HZ_NORMAL);
		clk_prepare_enable(drvdata->codec_mem0);
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX0:
		clk_set_rate(drvdata->va_mem0, CODEC_MEM_HZ_NORMAL);
		clk_prepare_enable(drvdata->va_mem0);
		break;
	default:
		dev_err(soc_runtime->dev, "%s: invalid  interface: %d\n", __func__, dai->id);
		break;
	}
	return 0;
}

static void lpass_cdc_dma_daiops_shutdown(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_pcm_runtime *soc_runtime = asoc_substream_to_rtd(substream);

	switch (dai->id) {
	case LPASS_CDC_DMA_RX0 ... LPASS_CDC_DMA_RX9:
	case LPASS_CDC_DMA_TX0 ... LPASS_CDC_DMA_TX8:
		clk_disable_unprepare(drvdata->codec_mem0);
		break;
	case LPASS_CDC_DMA_VA_TX0 ... LPASS_CDC_DMA_VA_TX0:
		clk_disable_unprepare(drvdata->va_mem0);
		break;
	default:
		dev_err(soc_runtime->dev, "%s: invalid  interface: %d\n", __func__, dai->id);
		break;
	}
}

static int lpass_cdc_dma_daiops_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *soc_runtime = asoc_substream_to_rtd(substream);
	struct lpaif_dmactl *dmactl = NULL;
	unsigned int ret, regval;
	unsigned int channels = params_channels(params);
	int id;

	switch (channels) {
	case 1:
		regval = LPASS_CDC_DMA_INTF_ONE_CHANNEL;
		break;
	case 2:
		regval = LPASS_CDC_DMA_INTF_TWO_CHANNEL;
		break;
	case 4:
		regval = LPASS_CDC_DMA_INTF_FOUR_CHANNEL;
		break;
	case 6:
		regval = LPASS_CDC_DMA_INTF_SIX_CHANNEL;
		break;
	case 8:
		regval = LPASS_CDC_DMA_INTF_EIGHT_CHANNEL;
		break;
	default:
		dev_err(soc_runtime->dev, "invalid PCM config\n");
		return -EINVAL;
	}

	__lpass_get_dmactl_handle(substream, dai, &dmactl, &id);
	if (!dmactl)
		return -EINVAL;

	ret = regmap_fields_write(dmactl->codec_channel, id, regval);
	if (ret) {
		dev_err(soc_runtime->dev,
			"error writing to dmactl codec_channel reg field: %d\n", ret);
		return ret;
	}
	return 0;
}

static int lpass_cdc_dma_daiops_trigger(struct snd_pcm_substream *substream,
				    int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *soc_runtime = asoc_substream_to_rtd(substream);
	struct lpaif_dmactl *dmactl;
	int ret = 0, id;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		__lpass_platform_codec_intf_init(dai, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		__lpass_get_dmactl_handle(substream, dai, &dmactl, &id);
		if (!dmactl)
			return -EINVAL;

		ret = regmap_fields_write(dmactl->codec_enable, id, LPAIF_DMACTL_ENABLE_OFF);
		if (ret) {
			dev_err(soc_runtime->dev,
				"error writing to dmactl codec_enable reg: %d\n", ret);
			return ret;
		}
		break;
	default:
		ret = -EINVAL;
		dev_err(soc_runtime->dev, "%s: invalid %d interface\n", __func__, cmd);
		break;
	}
	return ret;
}

const struct snd_soc_dai_ops asoc_qcom_lpass_cdc_dma_dai_ops = {
	.startup	= lpass_cdc_dma_daiops_startup,
	.shutdown	= lpass_cdc_dma_daiops_shutdown,
	.hw_params	= lpass_cdc_dma_daiops_hw_params,
	.trigger	= lpass_cdc_dma_daiops_trigger,
};
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_cdc_dma_dai_ops);

MODULE_DESCRIPTION("QTi LPASS CDC DMA Driver");
MODULE_LICENSE("GPL");
