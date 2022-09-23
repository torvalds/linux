// SPDX-License-Identifier: GPL-2.0
/*
 * PDM driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/reset.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "starfive_pdm.h"

struct sf_pdm {
	struct regmap *pdm_map;
	struct device *dev;
	struct clk *clk_pdm_apb;
	struct clk *clk_pdm_mclk;
	struct clk *clk_apb0;
	struct clk *clk_mclk;
	struct clk *clk_mclk_ext;
	struct reset_control *rst_pdm_dmic;
	struct reset_control *rst_pdm_apb;
	unsigned char flag_first;
};

static const DECLARE_TLV_DB_SCALE(volume_tlv, -9450, 150, 0);

static const struct snd_kcontrol_new sf_pdm_snd_controls[] = {
	SOC_SINGLE("DC compensation Control", PDM_DMIC_CTRL0, 30, 1, 0),
	SOC_SINGLE("High Pass Filter Control", PDM_DMIC_CTRL0, 28, 1, 0),
	SOC_SINGLE("Left Channel Volume Control", PDM_DMIC_CTRL0, 23, 1, 0),
	SOC_SINGLE("Right Channel Volume Control", PDM_DMIC_CTRL0, 22, 1, 0),
	SOC_SINGLE_TLV("Volume", PDM_DMIC_CTRL0, 16, 0x3F, 1, volume_tlv),
	SOC_SINGLE("Data MSB Shift", PDM_DMIC_CTRL0, 1, 7, 0),
	SOC_SINGLE("SCALE", PDM_DC_SCALE0, 0, 0x3F, 0),
	SOC_SINGLE("DC offset", PDM_DC_SCALE0, 8, 0xFFFFF, 0),
};

static void sf_pdm_enable(struct regmap *map)
{
	/* Left and Right Channel Volume Control Enable */
	regmap_update_bits(map, PDM_DMIC_CTRL0, PDM_DMIC_RVOL_MASK, 0);
	regmap_update_bits(map, PDM_DMIC_CTRL0, PDM_DMIC_LVOL_MASK, 0);
}

static void sf_pdm_disable(struct regmap *map)
{
	/* Left and Right Channel Volume Control Disable */
	regmap_update_bits(map, PDM_DMIC_CTRL0,
			PDM_DMIC_RVOL_MASK, PDM_DMIC_RVOL_MASK);
	regmap_update_bits(map, PDM_DMIC_CTRL0,
			PDM_DMIC_LVOL_MASK, PDM_DMIC_LVOL_MASK);
}

static int sf_pdm_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct sf_pdm *priv = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (priv->flag_first) {
			priv->flag_first = 0;
			mdelay(200);
		}
		sf_pdm_enable(priv->pdm_map);
		return 0;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		sf_pdm_disable(priv->pdm_map);
		return 0;

	default:
		return -EINVAL;
	}
}

static int sf_pdm_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct sf_pdm *priv = snd_soc_dai_get_drvdata(dai);
	unsigned int sample_rate;
	unsigned int data_width;
	int ret;
	const int pdm_mul = 128;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	sample_rate = params_rate(params);
	switch (sample_rate) {
	case 8000:
	case 11025:
	case 16000:
		break;
	default:
		pr_err("PDM: not support sample rate:%d\n", sample_rate);
		return -EINVAL;
	}

	data_width = params_width(params);
	switch (data_width) {
	case 16:
	case 32:
		break;
	default:
		pr_err("PDM: not support bit width %d\n", data_width);
		return -EINVAL;
	}

	/* set pdm_mclk,  PDM MCLK = 128 * LRCLK */
	ret = clk_set_rate(priv->clk_pdm_mclk, pdm_mul * sample_rate);
	if (ret) {
		dev_info(priv->dev, "Can't set pdm_mclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_dai_ops sf_pdm_dai_ops = {
	.trigger	= sf_pdm_trigger,
	.hw_params	= sf_pdm_hw_params,
};

static int sf_pdm_dai_probe(struct snd_soc_dai *dai)
{
	struct sf_pdm *priv = snd_soc_dai_get_drvdata(dai);

	/* Reset */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_SW_RST_MASK, 0x00);
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_SW_RST_MASK, PDM_SW_RST_RELEASE);

	/* Make sure the device is initially disabled */
	sf_pdm_disable(priv->pdm_map);

	/* MUTE */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_VOL_MASK, PDM_VOL_DB_MUTE);

	/* UNMUTE */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_VOL_MASK, PDM_VOL_DB_MAX);

	/* enable high pass filter */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_HPF_EN, PDM_DMIC_HPF_EN);

	/* i2s slave mode */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_I2S_SLAVE, PDM_DMIC_I2S_SLAVE);

	/* disable fast mode */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_FASTMODE_MASK, 0);

	/* dmic msb shift 0 */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_MSB_MASK, 0);

	/* scale: 0x8 */
	regmap_update_bits(priv->pdm_map, PDM_DC_SCALE0,
			DMIC_SCALE_MASK, DMIC_SCALE_DEF_VAL);

	regmap_update_bits(priv->pdm_map, PDM_DC_SCALE0,
			DMIC_DCOFF1_MASK, DMIC_DCOFF1_VAL);

	regmap_update_bits(priv->pdm_map, PDM_DC_SCALE0,
			DMIC_DCOFF3_MASK, DMIC_DCOFF3_VAL);

	/* scale: 0x3f */
	regmap_update_bits(priv->pdm_map, PDM_DC_SCALE0,
			DMIC_SCALE_MASK, DMIC_SCALE_MASK);

	/* dmic msb shift 2 */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_MSB_MASK, PDM_MSB_SHIFT_4);

	return 0;
}

static int sf_pdm_dai_remove(struct snd_soc_dai *dai)
{
	struct sf_pdm *priv = snd_soc_dai_get_drvdata(dai);

	/* MUTE */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_VOL_MASK, PDM_DMIC_VOL_MASK);

	return 0;
}

#define SF_PDM_RATES	(SNDRV_PCM_RATE_8000 | \
			SNDRV_PCM_RATE_11025 | \
			SNDRV_PCM_RATE_16000)

#define SF_PDM_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver sf_pdm_dai_drv = {
	.name = "PDM",
	.id = 0,
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SF_PDM_RATES,
		.formats	= SF_PDM_FORMATS,
	},
	.ops = &sf_pdm_dai_ops,
	.probe = sf_pdm_dai_probe,
	.remove	= sf_pdm_dai_remove,
	.symmetric_rate = 1,
};

static int pdm_probe(struct snd_soc_component *component)
{
	struct sf_pdm *priv = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, priv->pdm_map);
	snd_soc_add_component_controls(component, sf_pdm_snd_controls,
				     ARRAY_SIZE(sf_pdm_snd_controls));

	return 0;
}

static const struct snd_soc_component_driver sf_pdm_component_drv = {
	.name = "jh7110-pdm",
	.probe = pdm_probe,
};

static const struct regmap_config sf_pdm_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x20,
};

static int sf_pdm_clock_init(struct platform_device *pdev, struct sf_pdm *priv)
{
	int ret;

	static struct clk_bulk_data clks[] = {
		{ .id = "pdm_mclk" },
		{ .id = "clk_apb0" },
		{ .id = "pdm_apb" },
		{ .id = "clk_mclk" },
		{ .id = "mclk_ext" },
	};

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(clks), clks);
	if (ret) {
		dev_err(&pdev->dev, "failed to get pdm clocks\n");
		goto exit;
	}

	priv->clk_pdm_mclk = clks[0].clk;
	priv->clk_apb0 = clks[1].clk;
	priv->clk_pdm_apb = clks[2].clk;
	priv->clk_mclk = clks[3].clk;
	priv->clk_mclk_ext = clks[4].clk;

	priv->rst_pdm_dmic = devm_reset_control_get_exclusive(&pdev->dev, "pdm_dmic");
	if (IS_ERR(priv->rst_pdm_dmic)) {
		dev_err(&pdev->dev, "failed to get pdm_dmic reset control\n");
		ret = PTR_ERR(priv->rst_pdm_dmic);
		goto exit;
	}

	priv->rst_pdm_apb = devm_reset_control_get_exclusive(&pdev->dev, "pdm_apb");
	if (IS_ERR(priv->rst_pdm_apb)) {
		dev_err(&pdev->dev, "failed to get pdm_apb reset control\n");
		ret = PTR_ERR(priv->rst_pdm_apb);
		goto exit;
	}

	/*  Enable PDM Clock  */
	ret = reset_control_assert(priv->rst_pdm_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to assert rst_pdm_apb\n");
		goto exit;
	}

	ret = clk_set_parent(priv->clk_mclk, priv->clk_mclk_ext);
	if (ret) {
		dev_err(&pdev->dev, "failed to set parent clk_mclk ret=%d\n", ret);
		goto exit;
	}

	ret = clk_prepare_enable(priv->clk_mclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_mclk\n");
		goto err_dis_mclk;
	}

	ret = clk_prepare_enable(priv->clk_pdm_mclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_pdm_mclk\n");
		goto err_dis_pdm_mclk;
	}

	ret = clk_prepare_enable(priv->clk_apb0);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_apb0\n");
		goto err_dis_apb0;
	}

	ret = clk_prepare_enable(priv->clk_pdm_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_pdm_apb\n");
		goto err_dis_pdm_apb;
	}

	ret = reset_control_deassert(priv->rst_pdm_dmic);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert pdm_dmic\n");
		goto err_clk_disable;
	}

	ret = reset_control_deassert(priv->rst_pdm_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to deassert pdm_apb\n");
		goto err_clk_disable;
	}

	return 0;

err_clk_disable:
	clk_disable_unprepare(priv->clk_pdm_apb);
err_dis_pdm_apb:
	clk_disable_unprepare(priv->clk_apb0);
err_dis_apb0:
	clk_disable_unprepare(priv->clk_pdm_mclk);
err_dis_pdm_mclk:
	clk_disable_unprepare(priv->clk_mclk);
err_dis_mclk:
exit:
	return ret;
}

static int sf_pdm_probe(struct platform_device *pdev)
{
	struct sf_pdm *priv;
	struct resource *res;
	void __iomem *regs;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pdm");
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->pdm_map = devm_regmap_init_mmio(&pdev->dev, regs, &sf_pdm_regmap_cfg);
	if (IS_ERR(priv->pdm_map)) {
		dev_err(&pdev->dev, "failed to init regmap: %ld\n",
				PTR_ERR(priv->pdm_map));
		return PTR_ERR(priv->pdm_map);
	}

	priv->dev = &pdev->dev;
	priv->flag_first = 1;

	ret = sf_pdm_clock_init(pdev, priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable audio-pdm clock\n");
		return ret;
	}

	return devm_snd_soc_register_component(&pdev->dev, &sf_pdm_component_drv,
					       &sf_pdm_dai_drv, 1);
}

static int sf_pdm_dev_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id sf_pdm_of_match[] = {
	{.compatible = "starfive,jh7110-pdm",},
	{}
};
MODULE_DEVICE_TABLE(of, sf_pdm_of_match);

static struct platform_driver sf_pdm_driver = {
	.driver = {
		.name = "jh7110-pdm",
		.of_match_table = sf_pdm_of_match,
	},
	.probe = sf_pdm_probe,
	.remove = sf_pdm_dev_remove,
};
module_platform_driver(sf_pdm_driver);

MODULE_AUTHOR("Walker Chen <walker.chen@starfivetech.com>");
MODULE_DESCRIPTION("Starfive PDM Controller Driver");
MODULE_LICENSE("GPL v2");
