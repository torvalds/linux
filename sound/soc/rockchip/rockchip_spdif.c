/* sound/soc/rockchip/rk_spdif.c
 *
 * ALSA SoC Audio Layer - Rockchip I2S Controller driver
 *
 * Copyright (c) 2014 Rockchip Electronics Co. Ltd.
 * Author: Jianqun <jay.xu@rock-chips.com>
 * Copyright (c) 2015 Collabora Ltd.
 * Author: Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "rockchip_spdif.h"

enum rk_spdif_type {
	RK_SPDIF_RK3066,
	RK_SPDIF_RK3188,
	RK_SPDIF_RK3288,
};

#define RK3288_GRF_SOC_CON2 0x24c

struct rk_spdif_dev {
	struct device *dev;

	struct clk *mclk;
	struct clk *hclk;

	struct snd_dmaengine_dai_dma_data playback_dma_data;

	struct regmap *regmap;
};

static const struct of_device_id rk_spdif_match[] = {
	{ .compatible = "rockchip,rk3066-spdif",
	  .data = (void *) RK_SPDIF_RK3066 },
	{ .compatible = "rockchip,rk3188-spdif",
	  .data = (void *) RK_SPDIF_RK3188 },
	{ .compatible = "rockchip,rk3288-spdif",
	  .data = (void *) RK_SPDIF_RK3288 },
	{},
};
MODULE_DEVICE_TABLE(of, rk_spdif_match);

static int rk_spdif_runtime_suspend(struct device *dev)
{
	struct rk_spdif_dev *spdif = dev_get_drvdata(dev);

	clk_disable_unprepare(spdif->mclk);
	clk_disable_unprepare(spdif->hclk);

	return 0;
}

static int rk_spdif_runtime_resume(struct device *dev)
{
	struct rk_spdif_dev *spdif = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(spdif->mclk);
	if (ret) {
		dev_err(spdif->dev, "mclk clock enable failed %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(spdif->hclk);
	if (ret) {
		dev_err(spdif->dev, "hclk clock enable failed %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk_spdif_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct rk_spdif_dev *spdif = snd_soc_dai_get_drvdata(dai);
	unsigned int val = SPDIF_CFGR_HALFWORD_ENABLE;
	int srate, mclk;
	int ret;

	srate = params_rate(params);
	switch (srate) {
	case 32000:
	case 48000:
	case 96000:
		mclk = 96000 * 128; /* 12288000 hz */
		break;
	case 44100:
		mclk = 44100 * 256; /* 11289600 hz */
		break;
	case 192000:
		mclk = 192000 * 128; /* 24576000 hz */
		break;
	default:
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= SPDIF_CFGR_VDW_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val |= SPDIF_CFGR_VDW_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val |= SPDIF_CFGR_VDW_24;
		break;
	default:
		return -EINVAL;
	}

	/* Set clock and calculate divider */
	ret = clk_set_rate(spdif->mclk, mclk);
	if (ret != 0) {
		dev_err(spdif->dev, "Failed to set module clock rate: %d\n",
			ret);
		return ret;
	}

	val |= SPDIF_CFGR_CLK_DIV(mclk/(srate * 256));
	ret = regmap_update_bits(spdif->regmap, SPDIF_CFGR,
		SPDIF_CFGR_CLK_DIV_MASK | SPDIF_CFGR_HALFWORD_ENABLE |
		SDPIF_CFGR_VDW_MASK,
		val);

	return ret;
}

static int rk_spdif_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	struct rk_spdif_dev *spdif = snd_soc_dai_get_drvdata(dai);
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = regmap_update_bits(spdif->regmap, SPDIF_DMACR,
				   SPDIF_DMACR_TDE_ENABLE |
				   SPDIF_DMACR_TDL_MASK,
				   SPDIF_DMACR_TDE_ENABLE |
				   SPDIF_DMACR_TDL(16));

		if (ret != 0)
			return ret;

		ret = regmap_update_bits(spdif->regmap, SPDIF_XFER,
				   SPDIF_XFER_TXS_START,
				   SPDIF_XFER_TXS_START);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_update_bits(spdif->regmap, SPDIF_DMACR,
				   SPDIF_DMACR_TDE_ENABLE,
				   SPDIF_DMACR_TDE_DISABLE);

		if (ret != 0)
			return ret;

		ret = regmap_update_bits(spdif->regmap, SPDIF_XFER,
				   SPDIF_XFER_TXS_START,
				   SPDIF_XFER_TXS_STOP);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rk_spdif_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_spdif_dev *spdif = snd_soc_dai_get_drvdata(dai);

	dai->playback_dma_data = &spdif->playback_dma_data;

	return 0;
}

static const struct snd_soc_dai_ops rk_spdif_dai_ops = {
	.hw_params = rk_spdif_hw_params,
	.trigger = rk_spdif_trigger,
};

static struct snd_soc_dai_driver rk_spdif_dai = {
	.probe = rk_spdif_dai_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = (SNDRV_PCM_RATE_32000 |
			  SNDRV_PCM_RATE_44100 |
			  SNDRV_PCM_RATE_48000 |
			  SNDRV_PCM_RATE_96000 |
			  SNDRV_PCM_RATE_192000),
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE),
	},
	.ops = &rk_spdif_dai_ops,
};

static const struct snd_soc_component_driver rk_spdif_component = {
	.name = "rockchip-spdif",
};

static bool rk_spdif_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIF_CFGR:
	case SPDIF_DMACR:
	case SPDIF_INTCR:
	case SPDIF_XFER:
	case SPDIF_SMPDR:
		return true;
	default:
		return false;
	}
}

static bool rk_spdif_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIF_CFGR:
	case SPDIF_SDBLR:
	case SPDIF_INTCR:
	case SPDIF_INTSR:
	case SPDIF_XFER:
		return true;
	default:
		return false;
	}
}

static bool rk_spdif_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIF_INTSR:
	case SPDIF_SDBLR:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rk_spdif_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SPDIF_SMPDR,
	.writeable_reg = rk_spdif_wr_reg,
	.readable_reg = rk_spdif_rd_reg,
	.volatile_reg = rk_spdif_volatile_reg,
	.cache_type = REGCACHE_FLAT,
};

static int rk_spdif_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk_spdif_dev *spdif;
	const struct of_device_id *match;
	struct resource *res;
	void __iomem *regs;
	int ret;

	match = of_match_node(rk_spdif_match, np);
	if (match->data == (void *)RK_SPDIF_RK3288) {
		struct regmap *grf;

		grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
		if (IS_ERR(grf)) {
			dev_err(&pdev->dev,
				"rockchip_spdif missing 'rockchip,grf' \n");
			return PTR_ERR(grf);
		}

		/* Select the 8 channel SPDIF solution on RK3288 as
		 * the 2 channel one does not appear to work
		 */
		regmap_write(grf, RK3288_GRF_SOC_CON2, BIT(1) << 16);
	}

	spdif = devm_kzalloc(&pdev->dev, sizeof(*spdif), GFP_KERNEL);
	if (!spdif)
		return -ENOMEM;

	spdif->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(spdif->hclk)) {
		dev_err(&pdev->dev, "Can't retrieve rk_spdif bus clock\n");
		return PTR_ERR(spdif->hclk);
	}
	ret = clk_prepare_enable(spdif->hclk);
	if (ret) {
		dev_err(spdif->dev, "hclock enable failed %d\n", ret);
		return ret;
	}

	spdif->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(spdif->mclk)) {
		dev_err(&pdev->dev, "Can't retrieve rk_spdif master clock\n");
		return PTR_ERR(spdif->mclk);
	}

	ret = clk_prepare_enable(spdif->mclk);
	if (ret) {
		dev_err(spdif->dev, "clock enable failed %d\n", ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	spdif->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "hclk", regs,
						  &rk_spdif_regmap_config);
	if (IS_ERR(spdif->regmap)) {
		dev_err(&pdev->dev,
			"Failed to initialise managed register map\n");
		return PTR_ERR(spdif->regmap);
	}

	spdif->playback_dma_data.addr = res->start + SPDIF_SMPDR;
	spdif->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdif->playback_dma_data.maxburst = 4;

	spdif->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, spdif);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_request_idle(&pdev->dev);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rk_spdif_component,
					      &rk_spdif_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI\n");
		goto err_pm_runtime;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		goto err_pm_runtime;
	}

	return 0;

err_pm_runtime:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int rk_spdif_remove(struct platform_device *pdev)
{
	struct rk_spdif_dev *spdif = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		rk_spdif_runtime_suspend(&pdev->dev);

	clk_disable_unprepare(spdif->mclk);
	clk_disable_unprepare(spdif->hclk);

	return 0;
}

static const struct dev_pm_ops rk_spdif_pm_ops = {
	SET_RUNTIME_PM_OPS(rk_spdif_runtime_suspend, rk_spdif_runtime_resume,
			   NULL)
};

static struct platform_driver rk_spdif_driver = {
	.probe = rk_spdif_probe,
	.remove = rk_spdif_remove,
	.driver = {
		.name = "rockchip-spdif",
		.of_match_table = of_match_ptr(rk_spdif_match),
		.pm = &rk_spdif_pm_ops,
	},
};
module_platform_driver(rk_spdif_driver);

MODULE_ALIAS("platform:rockchip-spdif");
MODULE_DESCRIPTION("ROCKCHIP SPDIF transceiver Interface");
MODULE_AUTHOR("Sjoerd Simons <sjoerd.simons@collabora.co.uk>");
MODULE_LICENSE("GPL v2");
