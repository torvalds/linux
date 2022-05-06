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

#define AUDIOC_CLK	12288000
#define PDM_MUL		128

struct sf_pdm {
	struct regmap *pdm_map;
	struct clk *clk_pdm_apb;
	struct clk *clk_pdm_dmic;
	struct clk *clk_dmic0_bclk;
	struct clk *clk_dmic0_lrck;
	struct clk *clk_dmic1_bclk;
	struct clk *clk_dmic1_lrck;
	struct clk *clk_apb0;
	struct clk *clk_i2srx_3ch_bclk;
	struct reset_control *rst_pdm_dmic;
	struct reset_control *rst_pdm_apb;
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

static int sf_pdm_set_mclk(struct sf_pdm *priv, unsigned int clk,
				unsigned int width)
{
	/*
	audio source clk:12288000, mclk_div:4, mclk:3M
	support 8K/16K/32K/48K sample rate
	support 16/24/32 bit width
	bit width 32
	mclk bclk  lrclk
	3M   1.5M  48K
	3M   1M    32K
	3M   0.5M  16K
	3M   0.25M  8K

	bit width 24, set lrclk_div as 32
	mclk bclk  lrclk
	3M   1.5M  48K
	3M   1M    32K
	3M   0.5M  16K
	3M   0.25M  8K

	bit width 16
	mclk bclk   lrclk
	3M   0.75M  48K
	3M   0.5M   32K
	3M   0.25M  16K
	3M   0.125M 8K
	*/

	switch (clk) {
	case 8000:
	case 16000:
	case 32000:
	case 48000:
		break;
	default:
		pr_err("PDM: not support sample rate:%d\n", clk);
		return -EINVAL;
	}

	switch (width) {
	case 16:
	case 24:
	case 32:
		break;
	default:
		pr_err("PDM: not support bit width %d\n", width);
		return -EINVAL;
	}

	if (width == 24)
		width = 32;

	/* PDM MCLK = 128 * LRCLK */
	clk_set_rate(priv->clk_dmic0_bclk, clk*width);
	clk_set_rate(priv->clk_dmic0_lrck, clk);
	/* MCLK */
	clk_set_rate(priv->clk_pdm_dmic, PDM_MUL * clk);

	return 0;
}

static void sf_pdm_enable(struct regmap *map)
{
	/* Enable PDM */
	regmap_update_bits(map, PDM_DMIC_CTRL0, PDM_DMIC_RVOL_MASK, 0);
	regmap_update_bits(map, PDM_DMIC_CTRL0, PDM_DMIC_LVOL_MASK, 0);
}

static void sf_pdm_disable(struct regmap *map)
{
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
	unsigned int rate = params_rate(params);
	unsigned int width;
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	width = params_width(params);
	switch (width) {
	case 16:
	case 24:
	case 32:
		break;
	default:
		dev_err(dai->dev, "unsupported sample width\n");
		return -EINVAL;
	}

	ret = sf_pdm_set_mclk(priv, rate, width);
	if (ret < 0) {
		dev_err(dai->dev, "unsupported sample rate\n");
		return -EINVAL;
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
			PDM_DMIC_SW_RSTN_MASK, 0x00);
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_SW_RSTN_MASK, PDM_DMIC_SW_RSTN_MASK);

	/* Make sure the device is initially disabled */
	sf_pdm_disable(priv->pdm_map);

	/* MUTE */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_VOL_MASK, PDM_DMIC_VOL_MASK);

	/* UNMUTE */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_VOL_MASK, 0);

	/* enable high pass filter */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_HPF_EN, PDM_DMIC_HPF_EN);

	/* i2s slave mode */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_I2S_SLAVE, PDM_DMIC_I2S_SLAVE);

	/* disable fast mode */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_FASTMODE_MASK, 0);

	/* enable dc bypass mode */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_DC_BYPASS_MASK, 0);

	/* dmic msb shift 0 */
	regmap_update_bits(priv->pdm_map, PDM_DMIC_CTRL0,
			PDM_DMIC_MSB_MASK, 0);

	/* scale:0 */
	regmap_update_bits(priv->pdm_map, PDM_DC_SCALE0,
			PDM_DC_SCALE0_MASK, 0x08);

	/* DC offset:0 */
	regmap_update_bits(priv->pdm_map, PDM_DC_SCALE0,
			PDM_DMIC_DCOFF1_MASK, PDM_DMIC_DCOFF1_EN);

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

#define SF_PCM_RATE (SNDRV_PCM_RATE_8000 | \
			SNDRV_PCM_RATE_16000 | \
			SNDRV_PCM_RATE_32000 | \
			SNDRV_PCM_RATE_48000)

static struct snd_soc_dai_driver sf_pdm_dai_drv = {
	.name = "PDM",
	.id = 0,
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SF_PCM_RATE,
		.formats	= SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S32_LE,
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
	.name = "sf-pdm",
	.probe = pdm_probe,
};

static const struct regmap_config sf_pdm_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x20,
};

static const struct regmap_config sf_audio_clk_regmap_cfg = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x100,
};

static int sf_pdm_clock_init(struct platform_device *pdev, struct sf_pdm *priv)
{
	int ret;

	priv->rst_pdm_dmic = devm_reset_control_get_exclusive(&pdev->dev, "pdm_dmic");
	if (IS_ERR(priv->rst_pdm_dmic)) {
		dev_err(&pdev->dev, "failed to get pdm_dmic reset control\n");
		return PTR_ERR(priv->rst_pdm_dmic);
	}

	priv->rst_pdm_apb = devm_reset_control_get_exclusive(&pdev->dev, "pdm_apb");
	if (IS_ERR(priv->rst_pdm_apb)) {
		dev_err(&pdev->dev, "failed to get pdm_apb reset control\n");
		return PTR_ERR(priv->rst_pdm_apb);
	}

	priv->clk_apb0 = devm_clk_get(&pdev->dev, "clk_apb0");
	if (IS_ERR(priv->clk_apb0)) {
		dev_err(&pdev->dev, "failed to get clk_apb0\n");
		return PTR_ERR(priv->clk_apb0);
	}

	priv->clk_pdm_apb = devm_clk_get(&pdev->dev, "pdm_apb");
	if (IS_ERR(priv->clk_pdm_apb)) {
		dev_err(&pdev->dev, "failed to get clk_pdm_apb\n");
		return PTR_ERR(priv->clk_pdm_apb);
	}

	priv->clk_pdm_dmic = devm_clk_get(&pdev->dev, "pdm_dmic");
	if (IS_ERR(priv->clk_pdm_dmic)) {
		dev_err(&pdev->dev, "failed to get clk_pdm_dmic\n");
		return PTR_ERR(priv->clk_pdm_dmic);
	}

	priv->clk_dmic0_bclk = devm_clk_get(&pdev->dev, "pdm_dmic0_bclk");
	if (IS_ERR(priv->clk_dmic0_bclk)) {
		dev_err(&pdev->dev, "failed to get clk_dmic0_bclk\n");
		return PTR_ERR(priv->clk_dmic0_bclk);
	}

	priv->clk_dmic0_lrck = devm_clk_get(&pdev->dev, "pdm_dmic0_lrck");
	if (IS_ERR(priv->clk_dmic0_lrck)) {
		dev_err(&pdev->dev, "failed to get clk_dmic0_bclk\n");
		return PTR_ERR(priv->clk_dmic0_lrck);
	}

	priv->clk_dmic1_bclk = devm_clk_get(&pdev->dev, "pdm_dmic1_bclk");
	if (IS_ERR(priv->clk_dmic1_bclk)) {
		dev_err(&pdev->dev, "failed to get clk_dmic1_bclk\n");
		return PTR_ERR(priv->clk_dmic1_bclk);
	}

	priv->clk_dmic1_lrck = devm_clk_get(&pdev->dev, "pdm_dmic1_lrck");
	if (IS_ERR(priv->clk_dmic1_lrck)) {
		dev_err(&pdev->dev, "failed to get clk_dmic1_bclk\n");
		return PTR_ERR(priv->clk_dmic1_lrck);
	}

	priv->clk_i2srx_3ch_bclk = devm_clk_get(&pdev->dev, "u0_i2srx_3ch_bclk");
	if (IS_ERR(priv->clk_i2srx_3ch_bclk)) {
		dev_err(&pdev->dev, "failed to get clk_i2srx_3ch_bclk\n");
		return PTR_ERR(priv->clk_i2srx_3ch_bclk);
	}

	ret = clk_prepare_enable(priv->clk_pdm_dmic);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_pdm_dmic\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(priv->clk_apb0);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_apb0\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(priv->clk_pdm_apb);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_pdm_apb\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(priv->clk_i2srx_3ch_bclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_i2srx_3ch_bclk\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(priv->clk_dmic0_bclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_dmic0_bclk\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(priv->clk_dmic0_lrck);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_dmic0_lrck\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(priv->clk_dmic1_bclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_dmic1_bclk\n");
		goto err_clk_disable;
	}

	ret = clk_prepare_enable(priv->clk_dmic1_lrck);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare enable clk_dmic1_lrck\n");
		goto err_clk_disable;
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
	return ret;
}

static int sf_pdm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sf_pdm *priv;
	struct resource *res;
	void __iomem *regs;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pdm");
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->pdm_map = devm_regmap_init_mmio(dev, regs, &sf_pdm_regmap_cfg);
	if (IS_ERR(priv->pdm_map)) {
		dev_err(dev, "failed to init regmap: %ld\n",
			PTR_ERR(priv->pdm_map));
		return PTR_ERR(priv->pdm_map);
	}

	ret = sf_pdm_clock_init(pdev, priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable audio-pdm clock\n");
		return ret;
	}

	return devm_snd_soc_register_component(dev, &sf_pdm_component_drv,
					       &sf_pdm_dai_drv, 1);
}

static int sf_pdm_dev_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id sf_pdm_of_match[] = {
	{.compatible = "starfive,sf-pdm",},
	{}
};
MODULE_DEVICE_TABLE(of, sf_pdm_of_match);

static struct platform_driver sf_pdm_driver = {

	.driver = {
		.name = "sf-pdm",
		.of_match_table = sf_pdm_of_match,
	},
	.probe = sf_pdm_probe,
	.remove = sf_pdm_dev_remove,
};
module_platform_driver(sf_pdm_driver);

MODULE_AUTHOR("Walker Chen <walker.chen@starfivetech.com>");
MODULE_DESCRIPTION("Starfive PDM Controller Driver");
MODULE_LICENSE("GPL v2");
