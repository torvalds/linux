/*
 * tegra20_spdif.c - Tegra20 SPDIF driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2011-2012 - NVIDIA, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tegra20_spdif.h"

#define DRV_NAME "tegra20-spdif"

static inline void tegra20_spdif_write(struct tegra20_spdif *spdif, u32 reg,
					u32 val)
{
	regmap_write(spdif->regmap, reg, val);
}

static inline u32 tegra20_spdif_read(struct tegra20_spdif *spdif, u32 reg)
{
	u32 val;
	regmap_read(spdif->regmap, reg, &val);
	return val;
}

static int tegra20_spdif_runtime_suspend(struct device *dev)
{
	struct tegra20_spdif *spdif = dev_get_drvdata(dev);

	clk_disable(spdif->clk_spdif_out);

	return 0;
}

static int tegra20_spdif_runtime_resume(struct device *dev)
{
	struct tegra20_spdif *spdif = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(spdif->clk_spdif_out);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tegra20_spdif_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct device *dev = substream->pcm->card->dev;
	struct tegra20_spdif *spdif = snd_soc_dai_get_drvdata(dai);
	int ret, spdifclock;

	spdif->reg_ctrl &= ~TEGRA20_SPDIF_CTRL_PACK;
	spdif->reg_ctrl &= ~TEGRA20_SPDIF_CTRL_BIT_MODE_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		spdif->reg_ctrl |= TEGRA20_SPDIF_CTRL_PACK;
		spdif->reg_ctrl |= TEGRA20_SPDIF_CTRL_BIT_MODE_16BIT;
		break;
	default:
		return -EINVAL;
	}

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
		dev_err(dev, "Can't set SPDIF clock rate: %d\n", ret);
		return ret;
	}

	return 0;
}

static void tegra20_spdif_start_playback(struct tegra20_spdif *spdif)
{
	spdif->reg_ctrl |= TEGRA20_SPDIF_CTRL_TX_EN;
	tegra20_spdif_write(spdif, TEGRA20_SPDIF_CTRL, spdif->reg_ctrl);
}

static void tegra20_spdif_stop_playback(struct tegra20_spdif *spdif)
{
	spdif->reg_ctrl &= ~TEGRA20_SPDIF_CTRL_TX_EN;
	tegra20_spdif_write(spdif, TEGRA20_SPDIF_CTRL, spdif->reg_ctrl);
}

static int tegra20_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct tegra20_spdif *spdif = snd_soc_dai_get_drvdata(dai);

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

static int tegra20_spdif_probe(struct snd_soc_dai *dai)
{
	struct tegra20_spdif *spdif = snd_soc_dai_get_drvdata(dai);

	dai->capture_dma_data = NULL;
	dai->playback_dma_data = &spdif->playback_dma_data;

	return 0;
}

static const struct snd_soc_dai_ops tegra20_spdif_dai_ops = {
	.hw_params	= tegra20_spdif_hw_params,
	.trigger	= tegra20_spdif_trigger,
};

static struct snd_soc_dai_driver tegra20_spdif_dai = {
	.name = DRV_NAME,
	.probe = tegra20_spdif_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tegra20_spdif_dai_ops,
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
	};
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
	};
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
	};
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
	.cache_type = REGCACHE_RBTREE,
};

static __devinit int tegra20_spdif_platform_probe(struct platform_device *pdev)
{
	struct tegra20_spdif *spdif;
	struct resource *mem, *memregion, *dmareq;
	void __iomem *regs;
	int ret;

	spdif = devm_kzalloc(&pdev->dev, sizeof(struct tegra20_spdif),
			     GFP_KERNEL);
	if (!spdif) {
		dev_err(&pdev->dev, "Can't allocate tegra20_spdif\n");
		ret = -ENOMEM;
		goto err;
	}
	dev_set_drvdata(&pdev->dev, spdif);

	spdif->clk_spdif_out = clk_get(&pdev->dev, "spdif_out");
	if (IS_ERR(spdif->clk_spdif_out)) {
		pr_err("Can't retrieve spdif clock\n");
		ret = PTR_ERR(spdif->clk_spdif_out);
		goto err;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENODEV;
		goto err_clk_put;
	}

	dmareq = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!dmareq) {
		dev_err(&pdev->dev, "No DMA resource\n");
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

	spdif->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &tegra20_spdif_regmap_config);
	if (IS_ERR(spdif->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		ret = PTR_ERR(spdif->regmap);
		goto err_clk_put;
	}

	spdif->playback_dma_data.addr = mem->start + TEGRA20_SPDIF_DATA_OUT;
	spdif->playback_dma_data.wrap = 4;
	spdif->playback_dma_data.width = 32;
	spdif->playback_dma_data.req_sel = dmareq->start;

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = tegra20_spdif_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = snd_soc_register_dai(&pdev->dev, &tegra20_spdif_dai);
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
		tegra20_spdif_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
err_clk_put:
	clk_put(spdif->clk_spdif_out);
err:
	return ret;
}

static int __devexit tegra20_spdif_platform_remove(struct platform_device *pdev)
{
	struct tegra20_spdif *spdif = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra20_spdif_runtime_suspend(&pdev->dev);

	tegra_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_dai(&pdev->dev);

	clk_put(spdif->clk_spdif_out);

	return 0;
}

static const struct dev_pm_ops tegra20_spdif_pm_ops __devinitconst = {
	SET_RUNTIME_PM_OPS(tegra20_spdif_runtime_suspend,
			   tegra20_spdif_runtime_resume, NULL)
};

static struct platform_driver tegra20_spdif_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &tegra20_spdif_pm_ops,
	},
	.probe = tegra20_spdif_platform_probe,
	.remove = __devexit_p(tegra20_spdif_platform_remove),
};

module_platform_driver(tegra20_spdif_driver);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra20 SPDIF ASoC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
