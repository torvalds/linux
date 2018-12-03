// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA SoC Audio Layer - Rockchip SPDIF_RX Controller driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>

#include "rockchip_spdifrx.h"

struct rk_spdifrx_dev {
	struct device *dev;
	struct clk *mclk;
	struct clk *hclk;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct regmap *regmap;
	struct reset_control *reset;
	int irq;
};

static int rk_spdifrx_runtime_suspend(struct device *dev)
{
	struct rk_spdifrx_dev *spdifrx = dev_get_drvdata(dev);

	clk_disable_unprepare(spdifrx->mclk);
	clk_disable_unprepare(spdifrx->hclk);

	return 0;
}

static int rk_spdifrx_runtime_resume(struct device *dev)
{
	struct rk_spdifrx_dev *spdifrx = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(spdifrx->mclk);
	if (ret) {
		dev_err(spdifrx->dev, "mclk clock enable failed %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(spdifrx->hclk);
	if (ret) {
		dev_err(spdifrx->dev, "hclk clock enable failed %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk_spdifrx_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct rk_spdifrx_dev *spdifrx = snd_soc_dai_get_drvdata(dai);

	regmap_update_bits(spdifrx->regmap, SPDIFRX_INTEN,
			   SPDIFRX_INTEN_SYNCIE_MASK |
			   SPDIFRX_INTEN_NSYNCIE_MASK,
			   SPDIFRX_INTEN_SYNCIE_EN |
			   SPDIFRX_INTEN_NSYNCIE_EN);
	regmap_update_bits(spdifrx->regmap, SPDIFRX_DMACR,
			   SPDIFRX_DMACR_RDL_MASK, SPDIFRX_DMACR_RDL(8));
	regmap_update_bits(spdifrx->regmap, SPDIFRX_CDR,
			   SPDIFRX_CDR_AVGSEL_MASK | SPDIFRX_CDR_BYPASS_MASK,
			   SPDIFRX_CDR_AVGSEL_MIN | SPDIFRX_CDR_BYPASS_DIS);
	return 0;
}

static void rk_spdifrx_reset(struct rk_spdifrx_dev *spdifrx)
{
	reset_control_assert(spdifrx->reset);
	udelay(1);
	reset_control_deassert(spdifrx->reset);
}

static int rk_spdifrx_trigger(struct snd_pcm_substream *substream,
			      int cmd, struct snd_soc_dai *dai)
{
	struct rk_spdifrx_dev *spdifrx = snd_soc_dai_get_drvdata(dai);
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		rk_spdifrx_reset(spdifrx);
		ret = regmap_update_bits(spdifrx->regmap, SPDIFRX_DMACR,
					 SPDIFRX_DMACR_RDE_MASK,
					 SPDIFRX_DMACR_RDE_ENABLE);

		if (ret != 0)
			return ret;

		ret = regmap_update_bits(spdifrx->regmap, SPDIFRX_CFGR,
					 SPDIFRX_EN_MASK,
					 SPDIFRX_EN);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_update_bits(spdifrx->regmap, SPDIFRX_DMACR,
					 SPDIFRX_DMACR_RDE_MASK,
					 SPDIFRX_DMACR_RDE_DISABLE);

		if (ret != 0)
			return ret;

		ret = regmap_update_bits(spdifrx->regmap, SPDIFRX_CFGR,
					 SPDIFRX_EN_MASK,
					 SPDIFRX_DIS);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rk_spdifrx_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_spdifrx_dev *spdifrx = snd_soc_dai_get_drvdata(dai);

	dai->capture_dma_data = &spdifrx->capture_dma_data;

	return 0;
}

static const struct snd_soc_dai_ops rk_spdifrx_dai_ops = {
	.hw_params = rk_spdifrx_hw_params,
	.trigger = rk_spdifrx_trigger,
};

static struct snd_soc_dai_driver rk_spdifrx_dai = {
	.probe = rk_spdifrx_dai_probe,
	.capture = {
		.stream_name = "Capture",
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
	.ops = &rk_spdifrx_dai_ops,
};

static const struct snd_soc_component_driver rk_spdifrx_component = {
	.name = "rockchip-spdifrx",
};

static bool rk_spdifrx_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFRX_CFGR:
	case SPDIFRX_CLR:
	case SPDIFRX_CDR:
	case SPDIFRX_CDRST:
	case SPDIFRX_DMACR:
	case SPDIFRX_FIFOCTRL:
	case SPDIFRX_INTEN:
	case SPDIFRX_INTMASK:
	case SPDIFRX_INTSR:
	case SPDIFRX_INTCLR:
	case SPDIFRX_SMPDR:
	case SPDIFRX_BURSTINFO:
		return true;
	default:
		return false;
	}
}

static bool rk_spdifrx_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFRX_CFGR:
	case SPDIFRX_CLR:
	case SPDIFRX_CDR:
	case SPDIFRX_CDRST:
	case SPDIFRX_DMACR:
	case SPDIFRX_FIFOCTRL:
	case SPDIFRX_INTEN:
	case SPDIFRX_INTMASK:
	case SPDIFRX_INTSR:
	case SPDIFRX_INTCLR:
	case SPDIFRX_SMPDR:
	case SPDIFRX_BURSTINFO:
		return true;
	default:
		return false;
	}
}

static bool rk_spdifrx_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFRX_CLR:
	case SPDIFRX_CDR:
	case SPDIFRX_CDRST:
	case SPDIFRX_FIFOCTRL:
	case SPDIFRX_INTSR:
	case SPDIFRX_INTCLR:
	case SPDIFRX_SMPDR:
	case SPDIFRX_BURSTINFO:
		return true;
	default:
		return false;
	}
}

static bool rk_spdifrx_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SPDIFRX_SMPDR:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rk_spdifrx_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = SPDIFRX_BURSTINFO,
	.writeable_reg = rk_spdifrx_wr_reg,
	.readable_reg = rk_spdifrx_rd_reg,
	.volatile_reg = rk_spdifrx_volatile_reg,
	.precious_reg = rk_spdifrx_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static irqreturn_t rk_spdifrx_isr(int irq, void *dev_id)
{
	struct rk_spdifrx_dev *spdifrx = dev_id;
	u32 intsr;

	regmap_read(spdifrx->regmap, SPDIFRX_INTSR, &intsr);

	if (intsr & BIT(7)) {
		dev_dbg(spdifrx->dev, "NSYNC\n");
		regmap_write(spdifrx->regmap, SPDIFRX_INTCLR, BIT(7));
	}

	if (intsr & BIT(9)) {
		dev_dbg(spdifrx->dev, "SYNC\n");
		regmap_write(spdifrx->regmap, SPDIFRX_INTCLR, BIT(9));
	}

	return IRQ_HANDLED;
}

static int rk_spdifrx_probe(struct platform_device *pdev)
{
	struct rk_spdifrx_dev *spdifrx;
	struct resource *res;
	void __iomem *regs;
	int ret;

	spdifrx = devm_kzalloc(&pdev->dev, sizeof(*spdifrx), GFP_KERNEL);
	if (!spdifrx)
		return -ENOMEM;

	spdifrx->reset = devm_reset_control_get(&pdev->dev, "spdifrx-m");
	if (IS_ERR(spdifrx->reset)) {
		ret = PTR_ERR(spdifrx->reset);
		if (ret != -ENOENT)
			return ret;
	}

	spdifrx->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(spdifrx->hclk))
		return PTR_ERR(spdifrx->hclk);

	spdifrx->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(spdifrx->mclk))
		return PTR_ERR(spdifrx->mclk);

	spdifrx->irq = platform_get_irq(pdev, 0);
	if (spdifrx->irq < 0)
		return spdifrx->irq;

	ret = devm_request_threaded_irq(&pdev->dev, spdifrx->irq, NULL,
					rk_spdifrx_isr,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					dev_name(&pdev->dev), spdifrx);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	spdifrx->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
						&rk_spdifrx_regmap_config);
	if (IS_ERR(spdifrx->regmap))
		return PTR_ERR(spdifrx->regmap);

	spdifrx->capture_dma_data.addr = res->start + SPDIFRX_SMPDR;
	spdifrx->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	spdifrx->capture_dma_data.maxburst = 4;

	spdifrx->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, spdifrx);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = rk_spdifrx_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_runtime;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rk_spdifrx_component,
					      &rk_spdifrx_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI\n");
		goto err_pm_suspend;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM\n");
		goto err_pm_suspend;
	}

	return 0;

err_pm_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		rk_spdifrx_runtime_suspend(&pdev->dev);
err_pm_runtime:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int rk_spdifrx_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		rk_spdifrx_runtime_suspend(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_spdifrx_suspend(struct device *dev)
{
	struct rk_spdifrx_dev *spdifrx = dev_get_drvdata(dev);

	regcache_mark_dirty(spdifrx->regmap);

	return 0;
}

static int rockchip_spdifrx_resume(struct device *dev)
{
	struct rk_spdifrx_dev *spdifrx = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0)
		return ret;
	ret = regcache_sync(spdifrx->regmap);
	pm_runtime_put(dev);

	return ret;
}
#endif

static const struct dev_pm_ops rk_spdifrx_pm_ops = {
	SET_RUNTIME_PM_OPS(rk_spdifrx_runtime_suspend, rk_spdifrx_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_spdifrx_suspend, rockchip_spdifrx_resume)
};

static const struct of_device_id rk_spdifrx_match[] = {
	{ .compatible = "rockchip,rk3308-spdifrx", },
	{},
};
MODULE_DEVICE_TABLE(of, rk_spdifrx_match);

static struct platform_driver rk_spdifrx_driver = {
	.probe = rk_spdifrx_probe,
	.remove = rk_spdifrx_remove,
	.driver = {
		.name = "rockchip-spdifrx",
		.of_match_table = of_match_ptr(rk_spdifrx_match),
		.pm = &rk_spdifrx_pm_ops,
	},
};
module_platform_driver(rk_spdifrx_driver);

MODULE_ALIAS("platform:rockchip-spdifrx");
MODULE_DESCRIPTION("ROCKCHIP SPDIFRX Controller Interface");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL v2");
