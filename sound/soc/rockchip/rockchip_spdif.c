// SPDX-License-Identifier: GPL-2.0-only
/* sound/soc/rockchip/rk_spdif.c
 *
 * ALSA SoC Audio Layer - Rockchip I2S Controller driver
 *
 * Copyright (c) 2014 Rockchip Electronics Co. Ltd.
 * Author: Jianqun <jay.xu@rock-chips.com>
 * Copyright (c) 2015-2026 Collabora Ltd.
 * Author: Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/pcm_iec958.h>
#include <sound/dmaengine_pcm.h>

#include "rockchip_spdif.h"

enum rk_spdif_type {
	RK_SPDIF_RK3066,
	RK_SPDIF_RK3188,
	RK_SPDIF_RK3288,
	RK_SPDIF_RK3366,
};

/*
 *      |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0  |
 * CS0: |   Mode    |        d        |  c  |  b  |  a  |
 * CS1: |               Category Code                   |
 * CS2: |    Channel Number     |     Source Number     |
 * CS3: |    Clock Accuracy     |     Sample Freq       |
 * CS4: |    Ori Sample Freq    |     Word Length       |
 * CS5: |                                   |   CGMS-A  |
 * CS6~CS23: Reserved
 *
 * a: use of channel status block
 * b: linear PCM identification: 0 for lpcm, 1 for nlpcm
 * c: copyright information
 * d: additional format information
 */
#define CS_BYTE			6
#define CS_FRAME(c)		((c) << 16 | (c))

#define RK3288_GRF_SOC_CON2	0x24c

struct rk_spdif_dev {
	struct device *dev;

	struct clk *mclk;
	struct clk *hclk;

	struct snd_dmaengine_dai_dma_data playback_dma_data;

	struct regmap *regmap;
};

static int rk_spdif_runtime_suspend(struct device *dev)
{
	struct rk_spdif_dev *spdif = dev_get_drvdata(dev);

	regcache_cache_only(spdif->regmap, true);
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
		clk_disable_unprepare(spdif->mclk);
		dev_err(spdif->dev, "hclk clock enable failed %d\n", ret);
		return ret;
	}

	regcache_cache_only(spdif->regmap, false);
	regcache_mark_dirty(spdif->regmap);

	ret = regcache_sync(spdif->regmap);
	if (ret) {
		clk_disable_unprepare(spdif->mclk);
		clk_disable_unprepare(spdif->hclk);
	}

	return ret;
}

static int rk_spdif_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct rk_spdif_dev *spdif = snd_soc_dai_get_drvdata(dai);
	unsigned int mclk_rate = clk_get_rate(spdif->mclk);
	unsigned int val = SPDIF_CFGR_HALFWORD_ENABLE;
	int bmc, div, ret, i;
	u16 *fc;
	u8 cs[CS_BYTE];

	ret = snd_pcm_create_iec958_consumer_hw_params(params, cs, sizeof(cs));
	if (ret < 0)
		return ret;

	fc = (u16 *)cs;
	for (i = 0; i < CS_BYTE / 2; i++)
		regmap_write(spdif->regmap, SPDIF_CHNSRn(i), CS_FRAME(fc[i]));

	regmap_update_bits(spdif->regmap, SPDIF_CFGR, SPDIF_CFGR_CSE_MASK,
			   SPDIF_CFGR_CSE_EN);

	/* bmc = 128fs */
	bmc = 128 * params_rate(params);
	div = DIV_ROUND_CLOSEST(mclk_rate, bmc);
	val |= SPDIF_CFGR_CLK_DIV(div);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= SPDIF_CFGR_VDW_16;
		val |= SPDIF_CFGR_ADJ_RIGHT_J;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val |= SPDIF_CFGR_VDW_20;
		val |= SPDIF_CFGR_ADJ_RIGHT_J;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val |= SPDIF_CFGR_VDW_24;
		val |= SPDIF_CFGR_ADJ_RIGHT_J;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val |= SPDIF_CFGR_VDW_24;
		val |= SPDIF_CFGR_ADJ_LEFT_J;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * clear MCLK domain logic before setting Fmclk and Fsdo to ensure
	 * that switching between S16_LE and S32_LE audio does not result
	 * in accidential channels swap.
	 */
	regmap_update_bits(spdif->regmap, SPDIF_CFGR, SPDIF_CFGR_CLR_MASK,
			   SPDIF_CFGR_CLR_EN);
	udelay(1);

	ret = regmap_update_bits(spdif->regmap, SPDIF_CFGR,
				 SPDIF_CFGR_CLK_DIV_MASK |
				 SPDIF_CFGR_HALFWORD_MASK |
				 SDPIF_CFGR_VDW_MASK |
				 SPDIF_CFGR_ADJ_MASK, val);

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
					 SPDIF_DMACR_TDE_MASK |
					 SPDIF_DMACR_TDL_MASK,
					 SPDIF_DMACR_TDE_ENABLE |
					 SPDIF_DMACR_TDL(16));

		if (ret != 0)
			return ret;

		ret = regmap_update_bits(spdif->regmap, SPDIF_XFER,
					 SPDIF_XFER_TXS_MASK,
					 SPDIF_XFER_TXS_START);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_update_bits(spdif->regmap, SPDIF_DMACR,
					 SPDIF_DMACR_TDE_MASK,
					 SPDIF_DMACR_TDE_DISABLE);

		if (ret != 0)
			return ret;

		ret = regmap_update_bits(spdif->regmap, SPDIF_XFER,
					 SPDIF_XFER_TXS_MASK,
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

	snd_soc_dai_dma_data_set_playback(dai, &spdif->playback_dma_data);

	return 0;
}

static int rk_spdif_set_sysclk(struct snd_soc_dai *dai,
			       int clk_id, unsigned int freq, int dir)
{
	struct rk_spdif_dev *spdif = snd_soc_dai_get_drvdata(dai);
	int ret;

	if (!freq)
		return 0;

	ret = clk_set_rate(spdif->mclk, freq);
	if (ret)
		dev_err(spdif->dev, "Failed to set mclk: %d\n", ret);

	return ret;
}

static const struct snd_soc_dai_ops rk_spdif_dai_ops = {
	.set_sysclk = rk_spdif_set_sysclk,
	.probe = rk_spdif_dai_probe,
	.hw_params = rk_spdif_hw_params,
	.trigger = rk_spdif_trigger,
};

static struct snd_soc_dai_driver rk_spdif_dai = {
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = (SNDRV_PCM_FMTBIT_S16_LE |
			    SNDRV_PCM_FMTBIT_S20_3LE |
			    SNDRV_PCM_FMTBIT_S24_LE |
			    SNDRV_PCM_FMTBIT_S32_LE),
	},
	.ops = &rk_spdif_dai_ops,
};

static const struct snd_soc_component_driver rk_spdif_component = {
	.name = "rockchip-spdif",
	.legacy_dai_naming = 1,
};

static bool rk_spdif_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIF_CFGR:
	case SPDIF_DMACR:
	case SPDIF_INTCR:
	case SPDIF_XFER:
	case SPDIF_SMPDR:
	case SPDIF_VLDFRn(0) ... SPDIF_VLDFRn(11):
	case SPDIF_USRDRn(0) ... SPDIF_USRDRn(11):
	case SPDIF_CHNSRn(0) ... SPDIF_CHNSRn(11):
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
	case SPDIF_SMPDR:
	case SPDIF_VLDFRn(0) ... SPDIF_VLDFRn(11):
	case SPDIF_USRDRn(0) ... SPDIF_USRDRn(11):
	case SPDIF_CHNSRn(0) ... SPDIF_CHNSRn(11):
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
	case SPDIF_SMPDR:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rk_spdif_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SPDIF_VERSION,
	.writeable_reg = rk_spdif_wr_reg,
	.readable_reg = rk_spdif_rd_reg,
	.volatile_reg = rk_spdif_volatile_reg,
	.cache_type = REGCACHE_FLAT,
};

static void rk_spdif_suspend(void *data)
{
	struct device *dev = data;

	if (!pm_runtime_status_suspended(dev))
		rk_spdif_runtime_suspend(dev);
}

static int rk_spdif_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	enum rk_spdif_type spdif_type;
	struct rk_spdif_dev *spdif;
	struct resource *res;
	void __iomem *regs;
	int ret;

	spdif_type = (uintptr_t) device_get_match_data(&pdev->dev);
	if (spdif_type == RK_SPDIF_RK3288) {
		struct regmap *grf;

		grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
		if (IS_ERR(grf))
			return dev_err_probe(&pdev->dev, PTR_ERR(grf),
				"rockchip_spdif missing 'rockchip,grf'\n");

		/* Select the 8 channel SPDIF solution on RK3288 as
		 * the 2 channel one does not appear to work
		 */
		regmap_write(grf, RK3288_GRF_SOC_CON2, BIT(1) << 16);
	}

	spdif = devm_kzalloc(&pdev->dev, sizeof(*spdif), GFP_KERNEL);
	if (!spdif)
		return -ENOMEM;

	spdif->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(spdif->hclk))
		return PTR_ERR(spdif->hclk);

	spdif->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(spdif->mclk))
		return PTR_ERR(spdif->mclk);

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	spdif->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "hclk", regs,
						  &rk_spdif_regmap_config);
	if (IS_ERR(spdif->regmap))
		return PTR_ERR(spdif->regmap);

	spdif->playback_dma_data.addr = res->start + SPDIF_SMPDR;
	spdif->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdif->playback_dma_data.maxburst = 4;

	spdif->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, spdif);

	ret = devm_add_action_or_reset(&pdev->dev, rk_spdif_suspend, &pdev->dev);
	if (ret)
		return ret;

	devm_pm_runtime_enable(&pdev->dev);

	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = rk_spdif_runtime_resume(&pdev->dev);
		if (ret)
			return ret;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Could not register PCM\n");

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rk_spdif_component,
					      &rk_spdif_dai, 1);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Could not register DAI\n");

	return 0;
}

static const struct dev_pm_ops rk_spdif_pm_ops = {
	RUNTIME_PM_OPS(rk_spdif_runtime_suspend, rk_spdif_runtime_resume, NULL)
};

static const struct of_device_id rk_spdif_match[] = {
	{ .compatible = "rockchip,rk3066-spdif",
	  .data = (void *)RK_SPDIF_RK3066 },
	{ .compatible = "rockchip,rk3188-spdif",
	  .data = (void *)RK_SPDIF_RK3188 },
	{ .compatible = "rockchip,rk3228-spdif",
	  .data = (void *)RK_SPDIF_RK3366 },
	{ .compatible = "rockchip,rk3288-spdif",
	  .data = (void *)RK_SPDIF_RK3288 },
	{ .compatible = "rockchip,rk3328-spdif",
	  .data = (void *)RK_SPDIF_RK3366 },
	{ .compatible = "rockchip,rk3366-spdif",
	  .data = (void *)RK_SPDIF_RK3366 },
	{ .compatible = "rockchip,rk3368-spdif",
	  .data = (void *)RK_SPDIF_RK3366 },
	{ .compatible = "rockchip,rk3399-spdif",
	  .data = (void *)RK_SPDIF_RK3366 },
	{ .compatible = "rockchip,rk3568-spdif",
	  .data = (void *)RK_SPDIF_RK3366 },
	{},
};
MODULE_DEVICE_TABLE(of, rk_spdif_match);

static struct platform_driver rk_spdif_driver = {
	.probe = rk_spdif_probe,
	.driver = {
		.name = "rockchip-spdif",
		.of_match_table = rk_spdif_match,
		.pm = pm_ptr(&rk_spdif_pm_ops),
	},
};
module_platform_driver(rk_spdif_driver);

MODULE_ALIAS("platform:rockchip-spdif");
MODULE_DESCRIPTION("ROCKCHIP SPDIF transceiver Interface");
MODULE_AUTHOR("Sjoerd Simons <sjoerd.simons@collabora.co.uk>");
MODULE_LICENSE("GPL v2");
