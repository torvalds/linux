// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra20_spdif.c - Tegra20 SPDIF driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2011-2012 - NVIDIA, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
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

#include "tegra20_spdif.h"

static __maybe_unused int tegra20_spdif_runtime_suspend(struct device *dev)
{
	struct tegra20_spdif *spdif = dev_get_drvdata(dev);

	regcache_cache_only(spdif->regmap, true);

	clk_disable_unprepare(spdif->clk_spdif_out);

	return 0;
}

static __maybe_unused int tegra20_spdif_runtime_resume(struct device *dev)
{
	struct tegra20_spdif *spdif = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_assert(spdif->reset);
	if (ret)
		return ret;

	ret = clk_prepare_enable(spdif->clk_spdif_out);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	usleep_range(10, 100);

	ret = reset_control_deassert(spdif->reset);
	if (ret)
		goto disable_clocks;

	regcache_cache_only(spdif->regmap, false);
	regcache_mark_dirty(spdif->regmap);

	ret = regcache_sync(spdif->regmap);
	if (ret)
		goto disable_clocks;

	return 0;

disable_clocks:
	clk_disable_unprepare(spdif->clk_spdif_out);

	return ret;
}

static int tegra20_spdif_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct tegra20_spdif *spdif = dev_get_drvdata(dai->dev);
	unsigned int mask = 0, val = 0;
	int ret, spdifclock;
	long rate;

	mask |= TEGRA20_SPDIF_CTRL_PACK |
		TEGRA20_SPDIF_CTRL_BIT_MODE_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= TEGRA20_SPDIF_CTRL_PACK |
		       TEGRA20_SPDIF_CTRL_BIT_MODE_16BIT;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(spdif->regmap, TEGRA20_SPDIF_CTRL, mask, val);

	/*
	 * FIFO trigger level must be bigger than DMA burst or equal to it,
	 * otherwise data is discarded on overflow.
	 */
	regmap_update_bits(spdif->regmap, TEGRA20_SPDIF_DATA_FIFO_CSR,
			   TEGRA20_SPDIF_DATA_FIFO_CSR_TX_ATN_LVL_MASK,
			   TEGRA20_SPDIF_DATA_FIFO_CSR_TX_ATN_LVL_TU4_WORD_FULL);

	switch (params_rate(params)) {
	case 32000:
		spdifclock = 4096000;
		break;
	case 44100:
		spdifclock = 5644800;
		break;
	case 48000:
		spdifclock = 6144000;
		break;
	case 88200:
		spdifclock = 11289600;
		break;
	case 96000:
		spdifclock = 12288000;
		break;
	case 176400:
		spdifclock = 22579200;
		break;
	case 192000:
		spdifclock = 24576000;
		break;
	default:
		return -EINVAL;
	}

	ret = clk_set_rate(spdif->clk_spdif_out, spdifclock);
	if (ret) {
		dev_err(dai->dev, "Can't set SPDIF clock rate: %d\n", ret);
		return ret;
	}

	rate = clk_get_rate(spdif->clk_spdif_out);
	if (rate != spdifclock)
		dev_warn_once(dai->dev,
			      "SPDIF clock rate %d doesn't match requested rate %lu\n",
			      spdifclock, rate);

	return 0;
}

static void tegra20_spdif_start_playback(struct tegra20_spdif *spdif)
{
	regmap_update_bits(spdif->regmap, TEGRA20_SPDIF_CTRL,
			   TEGRA20_SPDIF_CTRL_TX_EN,
			   TEGRA20_SPDIF_CTRL_TX_EN);
}

static void tegra20_spdif_stop_playback(struct tegra20_spdif *spdif)
{
	regmap_update_bits(spdif->regmap, TEGRA20_SPDIF_CTRL,
			   TEGRA20_SPDIF_CTRL_TX_EN, 0);
}

static int tegra20_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	struct tegra20_spdif *spdif = dev_get_drvdata(dai->dev);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		tegra20_spdif_start_playback(spdif);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		tegra20_spdif_stop_playback(spdif);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra20_spdif_filter_rates(struct snd_pcm_hw_params *params,
				      struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *r = hw_param_interval(params, rule->var);
	struct snd_soc_dai *dai = rule->private;
	struct tegra20_spdif *spdif = dev_get_drvdata(dai->dev);
	struct clk *parent = clk_get_parent(spdif->clk_spdif_out);
	static const unsigned int rates[] = { 32000, 44100, 48000 };
	long i, parent_rate, valid_rates = 0;

	parent_rate = clk_get_rate(parent);
	if (parent_rate <= 0) {
		dev_err(dai->dev, "Can't get parent clock rate: %ld\n",
			parent_rate);
		return parent_rate ?: -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rates); i++) {
		if (parent_rate % (rates[i] * 128) == 0)
			valid_rates |= BIT(i);
	}

	/*
	 * At least one rate must be valid, otherwise the parent clock isn't
	 * audio PLL. Nothing should be filtered in this case.
	 */
	if (!valid_rates)
		valid_rates = BIT(ARRAY_SIZE(rates)) - 1;

	return snd_interval_list(r, ARRAY_SIZE(rates), rates, valid_rates);
}

static int tegra20_spdif_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	if (!device_property_read_bool(dai->dev, "nvidia,fixed-parent-rate"))
		return 0;

	/*
	 * SPDIF and I2S share audio PLL. HDMI takes audio packets from SPDIF
	 * and audio may not work on some TVs if clock rate isn't precise.
	 *
	 * PLL rate is controlled by I2S side. Filter out audio rates that
	 * don't match PLL rate at the start of stream to allow both SPDIF
	 * and I2S work simultaneously, assuming that PLL rate won't be
	 * changed later on.
	 */
	return snd_pcm_hw_rule_add(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   tegra20_spdif_filter_rates, dai,
				   SNDRV_PCM_HW_PARAM_RATE, -1);
}

static int tegra20_spdif_probe(struct snd_soc_dai *dai)
{
	struct tegra20_spdif *spdif = dev_get_drvdata(dai->dev);

	dai->capture_dma_data = NULL;
	dai->playback_dma_data = &spdif->playback_dma_data;

	return 0;
}

static const struct snd_soc_dai_ops tegra20_spdif_dai_ops = {
	.hw_params = tegra20_spdif_hw_params,
	.trigger = tegra20_spdif_trigger,
	.startup = tegra20_spdif_startup,
};

static struct snd_soc_dai_driver tegra20_spdif_dai = {
	.name = "tegra20-spdif",
	.probe = tegra20_spdif_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tegra20_spdif_dai_ops,
};

static const struct snd_soc_component_driver tegra20_spdif_component = {
	.name = "tegra20-spdif",
	.legacy_dai_naming = 1,
};

static bool tegra20_spdif_wr_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_SPDIF_CTRL:
	case TEGRA20_SPDIF_STATUS:
	case TEGRA20_SPDIF_STROBE_CTRL:
	case TEGRA20_SPDIF_DATA_FIFO_CSR:
	case TEGRA20_SPDIF_DATA_OUT:
	case TEGRA20_SPDIF_DATA_IN:
	case TEGRA20_SPDIF_CH_STA_RX_A:
	case TEGRA20_SPDIF_CH_STA_RX_B:
	case TEGRA20_SPDIF_CH_STA_RX_C:
	case TEGRA20_SPDIF_CH_STA_RX_D:
	case TEGRA20_SPDIF_CH_STA_RX_E:
	case TEGRA20_SPDIF_CH_STA_RX_F:
	case TEGRA20_SPDIF_CH_STA_TX_A:
	case TEGRA20_SPDIF_CH_STA_TX_B:
	case TEGRA20_SPDIF_CH_STA_TX_C:
	case TEGRA20_SPDIF_CH_STA_TX_D:
	case TEGRA20_SPDIF_CH_STA_TX_E:
	case TEGRA20_SPDIF_CH_STA_TX_F:
	case TEGRA20_SPDIF_USR_STA_RX_A:
	case TEGRA20_SPDIF_USR_DAT_TX_A:
		return true;
	default:
		return false;
	}
}

static bool tegra20_spdif_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_SPDIF_STATUS:
	case TEGRA20_SPDIF_DATA_FIFO_CSR:
	case TEGRA20_SPDIF_DATA_OUT:
	case TEGRA20_SPDIF_DATA_IN:
	case TEGRA20_SPDIF_CH_STA_RX_A:
	case TEGRA20_SPDIF_CH_STA_RX_B:
	case TEGRA20_SPDIF_CH_STA_RX_C:
	case TEGRA20_SPDIF_CH_STA_RX_D:
	case TEGRA20_SPDIF_CH_STA_RX_E:
	case TEGRA20_SPDIF_CH_STA_RX_F:
	case TEGRA20_SPDIF_USR_STA_RX_A:
	case TEGRA20_SPDIF_USR_DAT_TX_A:
		return true;
	default:
		return false;
	}
}

static bool tegra20_spdif_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_SPDIF_DATA_OUT:
	case TEGRA20_SPDIF_DATA_IN:
	case TEGRA20_SPDIF_USR_STA_RX_A:
	case TEGRA20_SPDIF_USR_DAT_TX_A:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tegra20_spdif_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = TEGRA20_SPDIF_USR_DAT_TX_A,
	.writeable_reg = tegra20_spdif_wr_rd_reg,
	.readable_reg = tegra20_spdif_wr_rd_reg,
	.volatile_reg = tegra20_spdif_volatile_reg,
	.precious_reg = tegra20_spdif_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static int tegra20_spdif_platform_probe(struct platform_device *pdev)
{
	struct tegra20_spdif *spdif;
	struct resource *mem;
	void __iomem *regs;
	int ret;

	spdif = devm_kzalloc(&pdev->dev, sizeof(struct tegra20_spdif),
			     GFP_KERNEL);
	if (!spdif)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, spdif);

	spdif->reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(spdif->reset)) {
		dev_err(&pdev->dev, "Can't retrieve spdif reset\n");
		return PTR_ERR(spdif->reset);
	}

	spdif->clk_spdif_out = devm_clk_get(&pdev->dev, "out");
	if (IS_ERR(spdif->clk_spdif_out)) {
		dev_err(&pdev->dev, "Could not retrieve spdif clock\n");
		return PTR_ERR(spdif->clk_spdif_out);
	}

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	spdif->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					      &tegra20_spdif_regmap_config);
	if (IS_ERR(spdif->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(spdif->regmap);
	}

	spdif->playback_dma_data.addr = mem->start + TEGRA20_SPDIF_DATA_OUT;
	spdif->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdif->playback_dma_data.maxburst = 4;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &tegra20_spdif_component,
					      &tegra20_spdif_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		return ret;
	}

	ret = devm_tegra_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops tegra20_spdif_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra20_spdif_runtime_suspend,
			   tegra20_spdif_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id tegra20_spdif_of_match[] = {
	{ .compatible = "nvidia,tegra20-spdif", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra20_spdif_of_match);

static struct platform_driver tegra20_spdif_driver = {
	.driver = {
		.name = "tegra20-spdif",
		.pm = &tegra20_spdif_pm_ops,
		.of_match_table = tegra20_spdif_of_match,
	},
	.probe = tegra20_spdif_platform_probe,
};
module_platform_driver(tegra20_spdif_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra20 SPDIF ASoC driver");
MODULE_LICENSE("GPL");
