// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2013, Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/regmap.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>

#define AXI_SPDIF_REG_CTRL	0x0
#define AXI_SPDIF_REG_STAT	0x4
#define AXI_SPDIF_REG_TX_FIFO	0xc

#define AXI_SPDIF_CTRL_TXDATA BIT(1)
#define AXI_SPDIF_CTRL_TXEN BIT(0)
#define AXI_SPDIF_CTRL_CLKDIV_OFFSET 8
#define AXI_SPDIF_CTRL_CLKDIV_MASK (0xff << 8)

#define AXI_SPDIF_FREQ_44100	(0x0 << 6)
#define AXI_SPDIF_FREQ_48000	(0x1 << 6)
#define AXI_SPDIF_FREQ_32000	(0x2 << 6)
#define AXI_SPDIF_FREQ_NA	(0x3 << 6)

struct axi_spdif {
	struct regmap *regmap;
	struct clk *clk;
	struct clk *clk_ref;

	struct snd_dmaengine_dai_dma_data dma_data;

	struct snd_ratnum ratnum;
	struct snd_pcm_hw_constraint_ratnums rate_constraints;
};

static int axi_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct axi_spdif *spdif = snd_soc_dai_get_drvdata(dai);
	unsigned int val;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = AXI_SPDIF_CTRL_TXDATA;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(spdif->regmap, AXI_SPDIF_REG_CTRL,
		AXI_SPDIF_CTRL_TXDATA, val);

	return 0;
}

static int axi_spdif_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct axi_spdif *spdif = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);
	unsigned int clkdiv, stat;

	switch (params_rate(params)) {
	case 32000:
		stat = AXI_SPDIF_FREQ_32000;
		break;
	case 44100:
		stat = AXI_SPDIF_FREQ_44100;
		break;
	case 48000:
		stat = AXI_SPDIF_FREQ_48000;
		break;
	default:
		stat = AXI_SPDIF_FREQ_NA;
		break;
	}

	clkdiv = DIV_ROUND_CLOSEST(clk_get_rate(spdif->clk_ref),
			rate * 64 * 2) - 1;
	clkdiv <<= AXI_SPDIF_CTRL_CLKDIV_OFFSET;

	regmap_write(spdif->regmap, AXI_SPDIF_REG_STAT, stat);
	regmap_update_bits(spdif->regmap, AXI_SPDIF_REG_CTRL,
		AXI_SPDIF_CTRL_CLKDIV_MASK, clkdiv);

	return 0;
}

static int axi_spdif_dai_probe(struct snd_soc_dai *dai)
{
	struct axi_spdif *spdif = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &spdif->dma_data, NULL);

	return 0;
}

static int axi_spdif_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct axi_spdif *spdif = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = snd_pcm_hw_constraint_ratnums(substream->runtime, 0,
			   SNDRV_PCM_HW_PARAM_RATE,
			   &spdif->rate_constraints);
	if (ret)
		return ret;

	ret = clk_prepare_enable(spdif->clk_ref);
	if (ret)
		return ret;

	regmap_update_bits(spdif->regmap, AXI_SPDIF_REG_CTRL,
		AXI_SPDIF_CTRL_TXEN, AXI_SPDIF_CTRL_TXEN);

	return 0;
}

static void axi_spdif_shutdown(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct axi_spdif *spdif = snd_soc_dai_get_drvdata(dai);

	regmap_update_bits(spdif->regmap, AXI_SPDIF_REG_CTRL,
		AXI_SPDIF_CTRL_TXEN, 0);

	clk_disable_unprepare(spdif->clk_ref);
}

static const struct snd_soc_dai_ops axi_spdif_dai_ops = {
	.probe = axi_spdif_dai_probe,
	.startup = axi_spdif_startup,
	.shutdown = axi_spdif_shutdown,
	.trigger = axi_spdif_trigger,
	.hw_params = axi_spdif_hw_params,
};

static struct snd_soc_dai_driver axi_spdif_dai = {
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &axi_spdif_dai_ops,
};

static const struct snd_soc_component_driver axi_spdif_component = {
	.name = "axi-spdif",
	.legacy_dai_naming = 1,
};

static const struct regmap_config axi_spdif_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AXI_SPDIF_REG_STAT,
};

static int axi_spdif_probe(struct platform_device *pdev)
{
	struct axi_spdif *spdif;
	struct resource *res;
	void __iomem *base;
	int ret;

	spdif = devm_kzalloc(&pdev->dev, sizeof(*spdif), GFP_KERNEL);
	if (!spdif)
		return -ENOMEM;

	platform_set_drvdata(pdev, spdif);

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	spdif->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &axi_spdif_regmap_config);
	if (IS_ERR(spdif->regmap))
		return PTR_ERR(spdif->regmap);

	spdif->clk = devm_clk_get(&pdev->dev, "axi");
	if (IS_ERR(spdif->clk))
		return PTR_ERR(spdif->clk);

	spdif->clk_ref = devm_clk_get(&pdev->dev, "ref");
	if (IS_ERR(spdif->clk_ref))
		return PTR_ERR(spdif->clk_ref);

	ret = clk_prepare_enable(spdif->clk);
	if (ret)
		return ret;

	spdif->dma_data.addr = res->start + AXI_SPDIF_REG_TX_FIFO;
	spdif->dma_data.addr_width = 4;
	spdif->dma_data.maxburst = 1;

	spdif->ratnum.num = clk_get_rate(spdif->clk_ref) / 128;
	spdif->ratnum.den_step = 1;
	spdif->ratnum.den_min = 1;
	spdif->ratnum.den_max = 64;

	spdif->rate_constraints.rats = &spdif->ratnum;
	spdif->rate_constraints.nrats = 1;

	ret = devm_snd_soc_register_component(&pdev->dev, &axi_spdif_component,
					 &axi_spdif_dai, 1);
	if (ret)
		goto err_clk_disable;

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret)
		goto err_clk_disable;

	return 0;

err_clk_disable:
	clk_disable_unprepare(spdif->clk);
	return ret;
}

static void axi_spdif_dev_remove(struct platform_device *pdev)
{
	struct axi_spdif *spdif = platform_get_drvdata(pdev);

	clk_disable_unprepare(spdif->clk);
}

static const struct of_device_id axi_spdif_of_match[] = {
	{ .compatible = "adi,axi-spdif-tx-1.00.a", },
	{},
};
MODULE_DEVICE_TABLE(of, axi_spdif_of_match);

static struct platform_driver axi_spdif_driver = {
	.driver = {
		.name = "axi-spdif",
		.of_match_table = axi_spdif_of_match,
	},
	.probe = axi_spdif_probe,
	.remove = axi_spdif_dev_remove,
};
module_platform_driver(axi_spdif_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("AXI SPDIF driver");
MODULE_LICENSE("GPL");
