/*
 * ALSA SoC IMX MQS driver
 *
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>


/* codec private data */
struct fsl_mqs {
	struct platform_device *pdev;
	struct regmap *gpr;
	struct clk *mclk;

	unsigned long mclk_rate;

	int sysclk_rate;
	int bclk;
	int lrclk;
	char name[32];
};

#define FSL_MQS_RATES	SNDRV_PCM_RATE_8000_192000
#define FSL_MQS_FORMATS	SNDRV_PCM_FMTBIT_S16_LE

static int fsl_mqs_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct fsl_mqs *mqs_priv = snd_soc_codec_get_drvdata(codec);
	int div, res;

	mqs_priv->mclk_rate = clk_get_rate(mqs_priv->mclk);

	mqs_priv->bclk = snd_soc_params_to_bclk(params);
	mqs_priv->lrclk = params_rate(params);

	/*
	 * mclk_rate / (oversample(32,64) * FS * 2 * divider ) = repeat_rate;
	 * if repeat_rate is 8, mqs can achieve better quality.
	 * oversample rate is fix to 32 currently.
	 */
	div = mqs_priv->mclk_rate / (32 * 2 * mqs_priv->lrclk * 8);
	res = mqs_priv->mclk_rate % (32 * 2 * mqs_priv->lrclk * 8);

	if (res == 0 && div > 0 && div <= 256) {
		regmap_update_bits(mqs_priv->gpr, IOMUXC_GPR2,
			IMX6SX_GPR2_MQS_CLK_DIV_MASK, (div-1) << IMX6SX_GPR2_MQS_CLK_DIV_SHIFT);
		regmap_update_bits(mqs_priv->gpr, IOMUXC_GPR2,
			IMX6SX_GPR2_MQS_OVERSAMPLE_MASK, 0 << IMX6SX_GPR2_MQS_OVERSAMPLE_SHIFT);
	} else
		dev_err(&mqs_priv->pdev->dev, "can't get proper divider\n");

	return 0;
}

static int fsl_mqs_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fsl_mqs_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct fsl_mqs *mqs_priv = snd_soc_codec_get_drvdata(codec);

	mqs_priv->sysclk_rate = freq;

	return 0;
}

static int fsl_mqs_startup(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct fsl_mqs *mqs_priv = snd_soc_codec_get_drvdata(codec);

	regmap_update_bits(mqs_priv->gpr, IOMUXC_GPR2, IMX6SX_GPR2_MQS_EN_MASK,
					1 << IMX6SX_GPR2_MQS_EN_SHIFT);
	return 0;
}

static void fsl_mqs_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct fsl_mqs *mqs_priv = snd_soc_codec_get_drvdata(codec);

	regmap_update_bits(mqs_priv->gpr, IOMUXC_GPR2,
					IMX6SX_GPR2_MQS_EN_MASK, 0);

}


static struct snd_soc_codec_driver soc_codec_fsl_mqs;

static const struct snd_soc_dai_ops fsl_mqs_dai_ops = {
	.startup = fsl_mqs_startup,
	.shutdown = fsl_mqs_shutdown,
	.hw_params = fsl_mqs_hw_params,
	.set_fmt = fsl_mqs_set_dai_fmt,
	.set_sysclk = fsl_mqs_set_dai_sysclk,
};

static struct snd_soc_dai_driver fsl_mqs_dai = {
	.name		= "fsl-mqs-dai",
	.playback	= {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= FSL_MQS_RATES,
		.formats	= FSL_MQS_FORMATS,
	},
	.ops = &fsl_mqs_dai_ops,
};

static int fsl_mqs_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *gpr_np;
	struct fsl_mqs *mqs_priv;
	int ret = 0;

	mqs_priv = devm_kzalloc(&pdev->dev, sizeof(*mqs_priv), GFP_KERNEL);
	if (!mqs_priv)
		return -ENOMEM;

	mqs_priv->pdev = pdev;
	strncpy(mqs_priv->name, np->name, sizeof(mqs_priv->name) - 1);

	gpr_np = of_parse_phandle(np, "gpr", 0);
	if (IS_ERR(gpr_np)) {
		dev_err(&pdev->dev, "failed to get gpr node by phandle\n");
		ret = PTR_ERR(gpr_np);
		goto out;
	}

	mqs_priv->gpr = syscon_node_to_regmap(gpr_np);
	if (IS_ERR(mqs_priv->gpr)) {
		dev_err(&pdev->dev, "failed to get gpr regmap\n");
		ret = PTR_ERR(mqs_priv->gpr);
		goto out;
	}

	mqs_priv->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(mqs_priv->mclk)) {
		dev_err(&pdev->dev, "failed to get the clock: %ld\n",
				PTR_ERR(mqs_priv->mclk));
		goto out;
	}

	dev_set_drvdata(&pdev->dev, mqs_priv);

	return snd_soc_register_codec(&pdev->dev, &soc_codec_fsl_mqs,
			&fsl_mqs_dai, 1);

out:
	if (!IS_ERR(gpr_np))
		of_node_put(gpr_np);

	return ret;
}

static int fsl_mqs_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static const struct of_device_id fsl_mqs_dt_ids[] = {
	{ .compatible = "fsl,imx6sx-mqs", },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_mqs_dt_ids);


static struct platform_driver fsl_mqs_driver = {
	.probe		= fsl_mqs_probe,
	.remove		= fsl_mqs_remove,
	.driver		= {
		.name	= "fsl-mqs",
		.of_match_table = fsl_mqs_dt_ids,
	},
};

module_platform_driver(fsl_mqs_driver);

MODULE_AUTHOR("shengjiu wang <shengjiu.wang@freescale.com>");
MODULE_DESCRIPTION("MQS dummy codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform: fsl-mqs");
