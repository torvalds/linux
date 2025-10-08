// SPDX-License-Identifier: GPL-2.0
//
// Common functions for loongson I2S controller driver
//
// Copyright (C) 2023 Loongson Technology Corporation Limited.
// Author: Yingkun Meng <mengyingkun@loongson.cn>
//

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/pm_runtime.h>
#include <linux/dma-mapping.h>
#include <sound/soc.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "loongson_i2s.h"

#define LOONGSON_I2S_FORMATS (SNDRV_PCM_FMTBIT_S8 | \
			SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

#define LOONGSON_I2S_TX_ENABLE	(I2S_CTRL_TX_EN | I2S_CTRL_TX_DMA_EN)
#define LOONGSON_I2S_RX_ENABLE	(I2S_CTRL_RX_EN | I2S_CTRL_RX_DMA_EN)

#define LOONGSON_I2S_DEF_DELAY		10
#define LOONGSON_I2S_DEF_TIMEOUT	500000

static int loongson_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct loongson_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int mask;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mask = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		       LOONGSON_I2S_TX_ENABLE : LOONGSON_I2S_RX_ENABLE;
		regmap_update_bits(i2s->regmap, LS_I2S_CTRL, mask, mask);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		mask = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		       LOONGSON_I2S_TX_ENABLE : LOONGSON_I2S_RX_ENABLE;
		regmap_update_bits(i2s->regmap, LS_I2S_CTRL, mask, 0);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int loongson_i2s_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct loongson_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	u32 clk_rate = i2s->clk_rate;
	u32 sysclk = i2s->sysclk;
	u32 bits = params_width(params);
	u32 chans = params_channels(params);
	u32 fs = params_rate(params);
	u32 bclk_ratio, mclk_ratio;
	u32 mclk_ratio_frac;
	u32 val = 0;

	switch (i2s->rev_id) {
	case 0:
		bclk_ratio = DIV_ROUND_CLOSEST(clk_rate,
					       (bits * chans * fs * 2)) - 1;
		mclk_ratio = DIV_ROUND_CLOSEST(clk_rate, (sysclk * 2)) - 1;

		/* According to 2k1000LA user manual, set bits == depth */
		val |= (bits << 24);
		val |= (bits << 16);
		val |= (bclk_ratio << 8);
		val |= mclk_ratio;
		regmap_write(i2s->regmap, LS_I2S_CFG, val);

		break;
	case 1:
		bclk_ratio = DIV_ROUND_CLOSEST(sysclk,
					       (bits * chans * fs * 2)) - 1;
		mclk_ratio = clk_rate / sysclk;
		mclk_ratio_frac = DIV_ROUND_CLOSEST_ULL(((u64)clk_rate << 16),
						    sysclk) - (mclk_ratio << 16);

		regmap_read(i2s->regmap, LS_I2S_CFG, &val);
		val |= (bits << 24);
		val |= (bclk_ratio << 8);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			val |= (bits << 16);
		else
			val |= bits;
		regmap_write(i2s->regmap, LS_I2S_CFG, val);

		val = (mclk_ratio_frac << 16) | mclk_ratio;
		regmap_write(i2s->regmap, LS_I2S_CFG1, val);

		break;
	default:
		dev_err(i2s->dev, "I2S revision invalid\n");
		return -EINVAL;
	}

	return 0;
}

static int loongson_i2s_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				       unsigned int freq, int dir)
{
	struct loongson_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	i2s->sysclk = freq;

	return 0;
}

static int loongson_i2s_enable_mclk(struct loongson_i2s *i2s)
{
	u32 val;

	if (i2s->rev_id == 0)
		return 0;

	regmap_update_bits(i2s->regmap, LS_I2S_CTRL,
			   I2S_CTRL_MCLK_EN, I2S_CTRL_MCLK_EN);

	return regmap_read_poll_timeout_atomic(i2s->regmap,
					       LS_I2S_CTRL, val,
					       val & I2S_CTRL_MCLK_READY,
					       LOONGSON_I2S_DEF_DELAY,
					       LOONGSON_I2S_DEF_TIMEOUT);
}

static int loongson_i2s_enable_bclk(struct loongson_i2s *i2s)
{
	u32 val;

	if (i2s->rev_id == 0)
		return 0;

	return regmap_read_poll_timeout_atomic(i2s->regmap,
					       LS_I2S_CTRL, val,
					       val & I2S_CTRL_CLK_READY,
					       LOONGSON_I2S_DEF_DELAY,
					       LOONGSON_I2S_DEF_TIMEOUT);
}

static int loongson_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct loongson_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	int ret;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		regmap_update_bits(i2s->regmap, LS_I2S_CTRL, I2S_CTRL_MSB,
				   I2S_CTRL_MSB);
		break;
	default:
		return -EINVAL;
	}


	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		break;
	case SND_SOC_DAIFMT_BP_FC:
		/* Enable master mode */
		regmap_update_bits(i2s->regmap, LS_I2S_CTRL, I2S_CTRL_MASTER,
				   I2S_CTRL_MASTER);
		ret = loongson_i2s_enable_bclk(i2s);
		if (ret < 0)
			dev_warn(dai->dev, "wait BCLK ready timeout\n");
		break;
	case SND_SOC_DAIFMT_BC_FP:
		/* Enable MCLK */
		ret = loongson_i2s_enable_mclk(i2s);
		if (ret < 0)
			dev_warn(dai->dev, "wait MCLK ready timeout\n");
		break;
	case SND_SOC_DAIFMT_BP_FP:
		/* Enable MCLK */
		ret = loongson_i2s_enable_mclk(i2s);
		if (ret < 0)
			dev_warn(dai->dev, "wait MCLK ready timeout\n");

		/* Enable master mode */
		regmap_update_bits(i2s->regmap, LS_I2S_CTRL, I2S_CTRL_MASTER,
				   I2S_CTRL_MASTER);

		ret = loongson_i2s_enable_bclk(i2s);
		if (ret < 0)
			dev_warn(dai->dev, "wait BCLK ready timeout\n");
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int loongson_i2s_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct loongson_i2s *i2s = dev_get_drvdata(cpu_dai->dev);

	snd_soc_dai_init_dma_data(cpu_dai, &i2s->playback_dma_data,
				  &i2s->capture_dma_data);
	snd_soc_dai_set_drvdata(cpu_dai, i2s);

	return 0;
}

static const struct snd_soc_dai_ops loongson_i2s_dai_ops = {
	.probe		= loongson_i2s_dai_probe,
	.trigger	= loongson_i2s_trigger,
	.hw_params	= loongson_i2s_hw_params,
	.set_sysclk	= loongson_i2s_set_dai_sysclk,
	.set_fmt	= loongson_i2s_set_fmt,
};

struct snd_soc_dai_driver loongson_i2s_dai = {
	.name = "loongson-i2s",
	.playback = {
		.stream_name = "CPU-Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = LOONGSON_I2S_FORMATS,
	},
	.capture = {
		.stream_name = "CPU-Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = LOONGSON_I2S_FORMATS,
	},
	.ops = &loongson_i2s_dai_ops,
	.symmetric_rate = 1,
};
EXPORT_SYMBOL_GPL(loongson_i2s_dai);

static int i2s_suspend(struct device *dev)
{
	struct loongson_i2s *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, true);

	return 0;
}

static int i2s_resume(struct device *dev)
{
	struct loongson_i2s *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, false);
	regcache_mark_dirty(i2s->regmap);
	return regcache_sync(i2s->regmap);
}

const struct dev_pm_ops loongson_i2s_pm = {
	SYSTEM_SLEEP_PM_OPS(i2s_suspend, i2s_resume)
};
EXPORT_SYMBOL_GPL(loongson_i2s_pm);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Common functions for loongson I2S controller driver");
