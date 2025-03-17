// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra20_i2s.c - Tegra20 I2S driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010,2012 - NVIDIA, Inc.
 *
 * Based on code copyright/by:
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 * Scott Peterson <speterson@nvidia.com>
 *
 * Copyright (C) 2010 Google, Inc.
 * Iliyan Malchev <malchev@google.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "tegra20_i2s.h"

#define DRV_NAME "tegra20-i2s"

static int tegra20_i2s_runtime_suspend(struct device *dev)
{
	struct tegra20_i2s *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, true);

	clk_disable_unprepare(i2s->clk_i2s);

	return 0;
}

static int tegra20_i2s_runtime_resume(struct device *dev)
{
	struct tegra20_i2s *i2s = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_assert(i2s->reset);
	if (ret)
		return ret;

	ret = clk_prepare_enable(i2s->clk_i2s);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	usleep_range(10, 100);

	ret = reset_control_deassert(i2s->reset);
	if (ret)
		goto disable_clocks;

	regcache_cache_only(i2s->regmap, false);
	regcache_mark_dirty(i2s->regmap);

	ret = regcache_sync(i2s->regmap);
	if (ret)
		goto disable_clocks;

	return 0;

disable_clocks:
	clk_disable_unprepare(i2s->clk_i2s);

	return ret;
}

static int tegra20_i2s_set_fmt(struct snd_soc_dai *dai,
				unsigned int fmt)
{
	struct tegra20_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int mask = 0, val = 0;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	mask |= TEGRA20_I2S_CTRL_MASTER_ENABLE;
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		val |= TEGRA20_I2S_CTRL_MASTER_ENABLE;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		break;
	default:
		return -EINVAL;
	}

	mask |= TEGRA20_I2S_CTRL_BIT_FORMAT_MASK |
		TEGRA20_I2S_CTRL_LRCK_MASK;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		val |= TEGRA20_I2S_CTRL_BIT_FORMAT_DSP;
		val |= TEGRA20_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		val |= TEGRA20_I2S_CTRL_BIT_FORMAT_DSP;
		val |= TEGRA20_I2S_CTRL_LRCK_R_LOW;
		break;
	case SND_SOC_DAIFMT_I2S:
		val |= TEGRA20_I2S_CTRL_BIT_FORMAT_I2S;
		val |= TEGRA20_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val |= TEGRA20_I2S_CTRL_BIT_FORMAT_RJM;
		val |= TEGRA20_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val |= TEGRA20_I2S_CTRL_BIT_FORMAT_LJM;
		val |= TEGRA20_I2S_CTRL_LRCK_L_LOW;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, TEGRA20_I2S_CTRL, mask, val);

	return 0;
}

static int tegra20_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra20_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int mask, val;
	int ret, sample_size, srate, i2sclock, bitcnt;

	mask = TEGRA20_I2S_CTRL_BIT_SIZE_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = TEGRA20_I2S_CTRL_BIT_SIZE_16;
		sample_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = TEGRA20_I2S_CTRL_BIT_SIZE_24;
		sample_size = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val = TEGRA20_I2S_CTRL_BIT_SIZE_32;
		sample_size = 32;
		break;
	default:
		return -EINVAL;
	}

	mask |= TEGRA20_I2S_CTRL_FIFO_FORMAT_MASK;
	val |= TEGRA20_I2S_CTRL_FIFO_FORMAT_PACKED;

	regmap_update_bits(i2s->regmap, TEGRA20_I2S_CTRL, mask, val);

	srate = params_rate(params);

	/* Final "* 2" required by Tegra hardware */
	i2sclock = srate * params_channels(params) * sample_size * 2;

	ret = clk_set_rate(i2s->clk_i2s, i2sclock);
	if (ret) {
		dev_err(dev, "Can't set I2S clock rate: %d\n", ret);
		return ret;
	}

	bitcnt = (i2sclock / (2 * srate)) - 1;
	if (bitcnt < 0 || bitcnt > TEGRA20_I2S_TIMING_CHANNEL_BIT_COUNT_MASK_US)
		return -EINVAL;
	val = bitcnt << TEGRA20_I2S_TIMING_CHANNEL_BIT_COUNT_SHIFT;

	if (i2sclock % (2 * srate))
		val |= TEGRA20_I2S_TIMING_NON_SYM_ENABLE;

	regmap_write(i2s->regmap, TEGRA20_I2S_TIMING, val);

	regmap_write(i2s->regmap, TEGRA20_I2S_FIFO_SCR,
		     TEGRA20_I2S_FIFO_SCR_FIFO2_ATN_LVL_FOUR_SLOTS |
		     TEGRA20_I2S_FIFO_SCR_FIFO1_ATN_LVL_FOUR_SLOTS);

	return 0;
}

static void tegra20_i2s_start_playback(struct tegra20_i2s *i2s)
{
	regmap_update_bits(i2s->regmap, TEGRA20_I2S_CTRL,
			   TEGRA20_I2S_CTRL_FIFO1_ENABLE,
			   TEGRA20_I2S_CTRL_FIFO1_ENABLE);
}

static void tegra20_i2s_stop_playback(struct tegra20_i2s *i2s)
{
	regmap_update_bits(i2s->regmap, TEGRA20_I2S_CTRL,
			   TEGRA20_I2S_CTRL_FIFO1_ENABLE, 0);
}

static void tegra20_i2s_start_capture(struct tegra20_i2s *i2s)
{
	regmap_update_bits(i2s->regmap, TEGRA20_I2S_CTRL,
			   TEGRA20_I2S_CTRL_FIFO2_ENABLE,
			   TEGRA20_I2S_CTRL_FIFO2_ENABLE);
}

static void tegra20_i2s_stop_capture(struct tegra20_i2s *i2s)
{
	regmap_update_bits(i2s->regmap, TEGRA20_I2S_CTRL,
			   TEGRA20_I2S_CTRL_FIFO2_ENABLE, 0);
}

static int tegra20_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	struct tegra20_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra20_i2s_start_playback(i2s);
		else
			tegra20_i2s_start_capture(i2s);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra20_i2s_stop_playback(i2s);
		else
			tegra20_i2s_stop_capture(i2s);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra20_i2s_probe(struct snd_soc_dai *dai)
{
	struct tegra20_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai,	&i2s->playback_dma_data,
					&i2s->capture_dma_data);

	return 0;
}

static const unsigned int tegra20_i2s_rates[] = {
	8000, 11025, 16000, 22050, 32000, 44100, 48000, 64000, 88200, 96000
};

static int tegra20_i2s_filter_rates(struct snd_pcm_hw_params *params,
				    struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *r = hw_param_interval(params, rule->var);
	struct snd_soc_dai *dai = rule->private;
	struct tegra20_i2s *i2s = dev_get_drvdata(dai->dev);
	struct clk *parent = clk_get_parent(i2s->clk_i2s);
	unsigned long i, parent_rate, valid_rates = 0;

	parent_rate = clk_get_rate(parent);
	if (!parent_rate) {
		dev_err(dai->dev, "Can't get parent clock rate\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(tegra20_i2s_rates); i++) {
		if (parent_rate % (tegra20_i2s_rates[i] * 128) == 0)
			valid_rates |= BIT(i);
	}

	/*
	 * At least one rate must be valid, otherwise the parent clock isn't
	 * audio PLL. Nothing should be filtered in this case.
	 */
	if (!valid_rates)
		valid_rates = BIT(ARRAY_SIZE(tegra20_i2s_rates)) - 1;

	return snd_interval_list(r, ARRAY_SIZE(tegra20_i2s_rates),
				 tegra20_i2s_rates, valid_rates);
}

static int tegra20_i2s_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	if (!device_property_read_bool(dai->dev, "nvidia,fixed-parent-rate"))
		return 0;

	return snd_pcm_hw_rule_add(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   tegra20_i2s_filter_rates, dai,
				   SNDRV_PCM_HW_PARAM_RATE, -1);
}

static const struct snd_soc_dai_ops tegra20_i2s_dai_ops = {
	.probe		= tegra20_i2s_probe,
	.set_fmt	= tegra20_i2s_set_fmt,
	.hw_params	= tegra20_i2s_hw_params,
	.trigger	= tegra20_i2s_trigger,
	.startup	= tegra20_i2s_startup,
};

static const struct snd_soc_dai_driver tegra20_i2s_dai_template = {
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tegra20_i2s_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_component_driver tegra20_i2s_component = {
	.name			= DRV_NAME,
	.legacy_dai_naming	= 1,
};

static bool tegra20_i2s_wr_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_I2S_CTRL:
	case TEGRA20_I2S_STATUS:
	case TEGRA20_I2S_TIMING:
	case TEGRA20_I2S_FIFO_SCR:
	case TEGRA20_I2S_PCM_CTRL:
	case TEGRA20_I2S_NW_CTRL:
	case TEGRA20_I2S_TDM_CTRL:
	case TEGRA20_I2S_TDM_TX_RX_CTRL:
	case TEGRA20_I2S_FIFO1:
	case TEGRA20_I2S_FIFO2:
		return true;
	default:
		return false;
	}
}

static bool tegra20_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_I2S_STATUS:
	case TEGRA20_I2S_FIFO_SCR:
	case TEGRA20_I2S_FIFO1:
	case TEGRA20_I2S_FIFO2:
		return true;
	default:
		return false;
	}
}

static bool tegra20_i2s_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_I2S_FIFO1:
	case TEGRA20_I2S_FIFO2:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra20_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = TEGRA20_I2S_FIFO2,
	.writeable_reg = tegra20_i2s_wr_rd_reg,
	.readable_reg = tegra20_i2s_wr_rd_reg,
	.volatile_reg = tegra20_i2s_volatile_reg,
	.precious_reg = tegra20_i2s_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static int tegra20_i2s_platform_probe(struct platform_device *pdev)
{
	struct tegra20_i2s *i2s;
	struct resource *mem;
	void __iomem *regs;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct tegra20_i2s), GFP_KERNEL);
	if (!i2s) {
		ret = -ENOMEM;
		goto err;
	}
	dev_set_drvdata(&pdev->dev, i2s);

	i2s->dai = tegra20_i2s_dai_template;
	i2s->dai.name = dev_name(&pdev->dev);

	i2s->reset = devm_reset_control_get_exclusive(&pdev->dev, "i2s");
	if (IS_ERR(i2s->reset)) {
		dev_err(&pdev->dev, "Can't retrieve i2s reset\n");
		return PTR_ERR(i2s->reset);
	}

	i2s->clk_i2s = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2s->clk_i2s)) {
		dev_err(&pdev->dev, "Can't retrieve i2s clock\n");
		ret = PTR_ERR(i2s->clk_i2s);
		goto err;
	}

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto err;
	}

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &tegra20_i2s_regmap_config);
	if (IS_ERR(i2s->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		ret = PTR_ERR(i2s->regmap);
		goto err;
	}

	i2s->capture_dma_data.addr = mem->start + TEGRA20_I2S_FIFO2;
	i2s->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->capture_dma_data.maxburst = 4;

	i2s->playback_dma_data.addr = mem->start + TEGRA20_I2S_FIFO1;
	i2s->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s->playback_dma_data.maxburst = 4;

	pm_runtime_enable(&pdev->dev);

	ret = snd_soc_register_component(&pdev->dev, &tegra20_i2s_component,
					 &i2s->dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		ret = -ENOMEM;
		goto err_pm_disable;
	}

	ret = tegra_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		goto err_unregister_component;
	}

	return 0;

err_unregister_component:
	snd_soc_unregister_component(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err:
	return ret;
}

static void tegra20_i2s_platform_remove(struct platform_device *pdev)
{
	tegra_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id tegra20_i2s_of_match[] = {
	{ .compatible = "nvidia,tegra20-i2s", },
	{},
};

static const struct dev_pm_ops tegra20_i2s_pm_ops = {
	RUNTIME_PM_OPS(tegra20_i2s_runtime_suspend,
		       tegra20_i2s_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
};

static struct platform_driver tegra20_i2s_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = tegra20_i2s_of_match,
		.pm = pm_ptr(&tegra20_i2s_pm_ops),
	},
	.probe = tegra20_i2s_platform_probe,
	.remove = tegra20_i2s_platform_remove,
};
module_platform_driver(tegra20_i2s_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra20 I2S ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra20_i2s_of_match);
