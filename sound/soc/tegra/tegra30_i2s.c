/*
 * tegra30_i2s.c - Tegra30 I2S driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (c) 2010-2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * Based on code copyright/by:
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 * Scott Peterson <speterson@nvidia.com>
 *
 * Copyright (C) 2010 Google, Inc.
 * Iliyan Malchev <malchev@google.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra30_ahub.h"
#include "tegra30_i2s.h"

#define DRV_NAME "tegra30-i2s"

static int tegra30_i2s_runtime_suspend(struct device *dev)
{
	struct tegra30_i2s *i2s = dev_get_drvdata(dev);

	regcache_cache_only(i2s->regmap, true);

	clk_disable_unprepare(i2s->clk_i2s);

	return 0;
}

static int tegra30_i2s_runtime_resume(struct device *dev)
{
	struct tegra30_i2s *i2s = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s->clk_i2s);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	regcache_cache_only(i2s->regmap, false);

	return 0;
}

int tegra30_i2s_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct tegra30_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = tegra30_ahub_allocate_tx_fifo(&i2s->playback_fifo_cif,
					&i2s->playback_dma_data.addr,
					&i2s->playback_dma_data.req_sel);
		i2s->playback_dma_data.wrap = 4;
		i2s->playback_dma_data.width = 32;
		tegra30_ahub_set_rx_cif_source(i2s->playback_i2s_cif,
					       i2s->playback_fifo_cif);
	} else {
		ret = tegra30_ahub_allocate_rx_fifo(&i2s->capture_fifo_cif,
					&i2s->capture_dma_data.addr,
					&i2s->capture_dma_data.req_sel);
		i2s->capture_dma_data.wrap = 4;
		i2s->capture_dma_data.width = 32;
		tegra30_ahub_set_rx_cif_source(i2s->capture_fifo_cif,
					       i2s->capture_i2s_cif);
	}

	return ret;
}

void tegra30_i2s_shutdown(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct tegra30_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		tegra30_ahub_unset_rx_cif_source(i2s->playback_i2s_cif);
		tegra30_ahub_free_tx_fifo(i2s->playback_fifo_cif);
	} else {
		tegra30_ahub_unset_rx_cif_source(i2s->capture_fifo_cif);
		tegra30_ahub_free_rx_fifo(i2s->capture_fifo_cif);
	}
}

static int tegra30_i2s_set_fmt(struct snd_soc_dai *dai,
				unsigned int fmt)
{
	struct tegra30_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int mask, val;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	mask = TEGRA30_I2S_CTRL_MASTER_ENABLE;
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		val = TEGRA30_I2S_CTRL_MASTER_ENABLE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		return -EINVAL;
	}

	mask |= TEGRA30_I2S_CTRL_FRAME_FORMAT_MASK |
		TEGRA30_I2S_CTRL_LRCK_MASK;
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		val |= TEGRA30_I2S_CTRL_FRAME_FORMAT_FSYNC;
		val |= TEGRA30_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		val |= TEGRA30_I2S_CTRL_FRAME_FORMAT_FSYNC;
		val |= TEGRA30_I2S_CTRL_LRCK_R_LOW;
		break;
	case SND_SOC_DAIFMT_I2S:
		val |= TEGRA30_I2S_CTRL_FRAME_FORMAT_LRCK;
		val |= TEGRA30_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val |= TEGRA30_I2S_CTRL_FRAME_FORMAT_LRCK;
		val |= TEGRA30_I2S_CTRL_LRCK_L_LOW;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val |= TEGRA30_I2S_CTRL_FRAME_FORMAT_LRCK;
		val |= TEGRA30_I2S_CTRL_LRCK_L_LOW;
		break;
	default:
		return -EINVAL;
	}

	pm_runtime_get_sync(dai->dev);
	regmap_update_bits(i2s->regmap, TEGRA30_I2S_CTRL, mask, val);
	pm_runtime_put(dai->dev);

	return 0;
}

static int tegra30_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra30_i2s *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int mask, val, reg;
	int ret, sample_size, srate, i2sclock, bitcnt;

	if (params_channels(params) != 2)
		return -EINVAL;

	mask = TEGRA30_I2S_CTRL_BIT_SIZE_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = TEGRA30_I2S_CTRL_BIT_SIZE_16;
		sample_size = 16;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(i2s->regmap, TEGRA30_I2S_CTRL, mask, val);

	srate = params_rate(params);

	/* Final "* 2" required by Tegra hardware */
	i2sclock = srate * params_channels(params) * sample_size * 2;

	bitcnt = (i2sclock / (2 * srate)) - 1;
	if (bitcnt < 0 || bitcnt > TEGRA30_I2S_TIMING_CHANNEL_BIT_COUNT_MASK_US)
		return -EINVAL;

	ret = clk_set_rate(i2s->clk_i2s, i2sclock);
	if (ret) {
		dev_err(dev, "Can't set I2S clock rate: %d\n", ret);
		return ret;
	}

	val = bitcnt << TEGRA30_I2S_TIMING_CHANNEL_BIT_COUNT_SHIFT;

	if (i2sclock % (2 * srate))
		val |= TEGRA30_I2S_TIMING_NON_SYM_ENABLE;

	regmap_write(i2s->regmap, TEGRA30_I2S_TIMING, val);

	val = (0 << TEGRA30_AUDIOCIF_CTRL_FIFO_THRESHOLD_SHIFT) |
	      (1 << TEGRA30_AUDIOCIF_CTRL_AUDIO_CHANNELS_SHIFT) |
	      (1 << TEGRA30_AUDIOCIF_CTRL_CLIENT_CHANNELS_SHIFT) |
	      TEGRA30_AUDIOCIF_CTRL_AUDIO_BITS_16 |
	      TEGRA30_AUDIOCIF_CTRL_CLIENT_BITS_16;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		val |= TEGRA30_AUDIOCIF_CTRL_DIRECTION_RX;
		reg = TEGRA30_I2S_CIF_RX_CTRL;
	} else {
		val |= TEGRA30_AUDIOCIF_CTRL_DIRECTION_TX;
		reg = TEGRA30_I2S_CIF_RX_CTRL;
	}

	regmap_write(i2s->regmap, reg, val);

	val = (1 << TEGRA30_I2S_OFFSET_RX_DATA_OFFSET_SHIFT) |
	      (1 << TEGRA30_I2S_OFFSET_TX_DATA_OFFSET_SHIFT);
	regmap_write(i2s->regmap, TEGRA30_I2S_OFFSET, val);

	return 0;
}

static void tegra30_i2s_start_playback(struct tegra30_i2s *i2s)
{
	tegra30_ahub_enable_tx_fifo(i2s->playback_fifo_cif);
	regmap_update_bits(i2s->regmap, TEGRA30_I2S_CTRL,
			   TEGRA30_I2S_CTRL_XFER_EN_TX,
			   TEGRA30_I2S_CTRL_XFER_EN_TX);
}

static void tegra30_i2s_stop_playback(struct tegra30_i2s *i2s)
{
	tegra30_ahub_disable_tx_fifo(i2s->playback_fifo_cif);
	regmap_update_bits(i2s->regmap, TEGRA30_I2S_CTRL,
			   TEGRA30_I2S_CTRL_XFER_EN_TX, 0);
}

static void tegra30_i2s_start_capture(struct tegra30_i2s *i2s)
{
	tegra30_ahub_enable_rx_fifo(i2s->capture_fifo_cif);
	regmap_update_bits(i2s->regmap, TEGRA30_I2S_CTRL,
			   TEGRA30_I2S_CTRL_XFER_EN_RX,
			   TEGRA30_I2S_CTRL_XFER_EN_RX);
}

static void tegra30_i2s_stop_capture(struct tegra30_i2s *i2s)
{
	tegra30_ahub_disable_rx_fifo(i2s->capture_fifo_cif);
	regmap_update_bits(i2s->regmap, TEGRA30_I2S_CTRL,
			   TEGRA30_I2S_CTRL_XFER_EN_RX, 0);
}

static int tegra30_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct tegra30_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra30_i2s_start_playback(i2s);
		else
			tegra30_i2s_start_capture(i2s);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra30_i2s_stop_playback(i2s);
		else
			tegra30_i2s_stop_capture(i2s);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra30_i2s_probe(struct snd_soc_dai *dai)
{
	struct tegra30_i2s *i2s = snd_soc_dai_get_drvdata(dai);

	dai->capture_dma_data = &i2s->capture_dma_data;
	dai->playback_dma_data = &i2s->playback_dma_data;

	return 0;
}

static struct snd_soc_dai_ops tegra30_i2s_dai_ops = {
	.startup	= tegra30_i2s_startup,
	.shutdown	= tegra30_i2s_shutdown,
	.set_fmt	= tegra30_i2s_set_fmt,
	.hw_params	= tegra30_i2s_hw_params,
	.trigger	= tegra30_i2s_trigger,
};

static const struct snd_soc_dai_driver tegra30_i2s_dai_template = {
	.probe = tegra30_i2s_probe,
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
	.ops = &tegra30_i2s_dai_ops,
	.symmetric_rates = 1,
};

static bool tegra30_i2s_wr_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA30_I2S_CTRL:
	case TEGRA30_I2S_TIMING:
	case TEGRA30_I2S_OFFSET:
	case TEGRA30_I2S_CH_CTRL:
	case TEGRA30_I2S_SLOT_CTRL:
	case TEGRA30_I2S_CIF_RX_CTRL:
	case TEGRA30_I2S_CIF_TX_CTRL:
	case TEGRA30_I2S_FLOWCTL:
	case TEGRA30_I2S_TX_STEP:
	case TEGRA30_I2S_FLOW_STATUS:
	case TEGRA30_I2S_FLOW_TOTAL:
	case TEGRA30_I2S_FLOW_OVER:
	case TEGRA30_I2S_FLOW_UNDER:
	case TEGRA30_I2S_LCOEF_1_4_0:
	case TEGRA30_I2S_LCOEF_1_4_1:
	case TEGRA30_I2S_LCOEF_1_4_2:
	case TEGRA30_I2S_LCOEF_1_4_3:
	case TEGRA30_I2S_LCOEF_1_4_4:
	case TEGRA30_I2S_LCOEF_1_4_5:
	case TEGRA30_I2S_LCOEF_2_4_0:
	case TEGRA30_I2S_LCOEF_2_4_1:
	case TEGRA30_I2S_LCOEF_2_4_2:
		return true;
	default:
		return false;
	};
}

static bool tegra30_i2s_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA30_I2S_FLOW_STATUS:
	case TEGRA30_I2S_FLOW_TOTAL:
	case TEGRA30_I2S_FLOW_OVER:
	case TEGRA30_I2S_FLOW_UNDER:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config tegra30_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = TEGRA30_I2S_LCOEF_2_4_2,
	.writeable_reg = tegra30_i2s_wr_rd_reg,
	.readable_reg = tegra30_i2s_wr_rd_reg,
	.volatile_reg = tegra30_i2s_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static __devinit int tegra30_i2s_platform_probe(struct platform_device *pdev)
{
	struct tegra30_i2s *i2s;
	u32 cif_ids[2];
	struct resource *mem, *memregion;
	void __iomem *regs;
	int ret;

	i2s = devm_kzalloc(&pdev->dev, sizeof(struct tegra30_i2s), GFP_KERNEL);
	if (!i2s) {
		dev_err(&pdev->dev, "Can't allocate tegra30_i2s\n");
		ret = -ENOMEM;
		goto err;
	}
	dev_set_drvdata(&pdev->dev, i2s);

	i2s->dai = tegra30_i2s_dai_template;
	i2s->dai.name = dev_name(&pdev->dev);

	ret = of_property_read_u32_array(pdev->dev.of_node,
					 "nvidia,ahub-cif-ids", cif_ids,
					 ARRAY_SIZE(cif_ids));
	if (ret < 0)
		goto err;

	i2s->playback_i2s_cif = cif_ids[0];
	i2s->capture_i2s_cif = cif_ids[1];

	i2s->clk_i2s = clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2s->clk_i2s)) {
		dev_err(&pdev->dev, "Can't retrieve i2s clock\n");
		ret = PTR_ERR(i2s->clk_i2s);
		goto err;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENODEV;
		goto err_clk_put;
	}

	memregion = devm_request_mem_region(&pdev->dev, mem->start,
					    resource_size(mem), DRV_NAME);
	if (!memregion) {
		dev_err(&pdev->dev, "Memory region already claimed\n");
		ret = -EBUSY;
		goto err_clk_put;
	}

	regs = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_clk_put;
	}

	i2s->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &tegra30_i2s_regmap_config);
	if (IS_ERR(i2s->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		ret = PTR_ERR(i2s->regmap);
		goto err_clk_put;
	}
	regcache_cache_only(i2s->regmap, true);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = tegra30_i2s_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = snd_soc_register_dai(&pdev->dev, &i2s->dai);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		ret = -ENOMEM;
		goto err_suspend;
	}

	ret = tegra_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		goto err_unregister_dai;
	}

	return 0;

err_unregister_dai:
	snd_soc_unregister_dai(&pdev->dev);
err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra30_i2s_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err_clk_put:
	clk_put(i2s->clk_i2s);
err:
	return ret;
}

static int __devexit tegra30_i2s_platform_remove(struct platform_device *pdev)
{
	struct tegra30_i2s *i2s = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra30_i2s_runtime_suspend(&pdev->dev);

	tegra_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_dai(&pdev->dev);

	clk_put(i2s->clk_i2s);

	return 0;
}

static const struct of_device_id tegra30_i2s_of_match[] __devinitconst = {
	{ .compatible = "nvidia,tegra30-i2s", },
	{},
};

static const struct dev_pm_ops tegra30_i2s_pm_ops __devinitconst = {
	SET_RUNTIME_PM_OPS(tegra30_i2s_runtime_suspend,
			   tegra30_i2s_runtime_resume, NULL)
};

static struct platform_driver tegra30_i2s_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = tegra30_i2s_of_match,
		.pm = &tegra30_i2s_pm_ops,
	},
	.probe = tegra30_i2s_platform_probe,
	.remove = __devexit_p(tegra30_i2s_platform_remove),
};
module_platform_driver(tegra30_i2s_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra30 I2S ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra30_i2s_of_match);
