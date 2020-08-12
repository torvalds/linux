// SPDX-License-Identifier: GPL-2.0
//
// ALSA SoC IMX MQS driver
//
// Copyright (C) 2014-2015 Freescale Semiconductor, Inc.
// Copyright 2019 NXP

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#define REG_MQS_CTRL		0x00

#define MQS_EN_MASK			(0x1 << 28)
#define MQS_EN_SHIFT			(28)
#define MQS_SW_RST_MASK			(0x1 << 24)
#define MQS_SW_RST_SHIFT		(24)
#define MQS_OVERSAMPLE_MASK		(0x1 << 20)
#define MQS_OVERSAMPLE_SHIFT		(20)
#define MQS_CLK_DIV_MASK		(0xFF << 0)
#define MQS_CLK_DIV_SHIFT		(0)

/* codec private data */
struct fsl_mqs {
	struct regmap *regmap;
	struct clk *mclk;
	struct clk *ipg;

	unsigned int reg_iomuxc_gpr2;
	unsigned int reg_mqs_ctrl;
	bool use_gpr;
};

#define FSL_MQS_RATES	(SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define FSL_MQS_FORMATS	SNDRV_PCM_FMTBIT_S16_LE

static int fsl_mqs_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct fsl_mqs *mqs_priv = snd_soc_component_get_drvdata(component);
	unsigned long mclk_rate;
	int div, res;
	int lrclk;

	mclk_rate = clk_get_rate(mqs_priv->mclk);
	lrclk = params_rate(params);

	/*
	 * mclk_rate / (oversample(32,64) * FS * 2 * divider ) = repeat_rate;
	 * if repeat_rate is 8, mqs can achieve better quality.
	 * oversample rate is fix to 32 currently.
	 */
	div = mclk_rate / (32 * lrclk * 2 * 8);
	res = mclk_rate % (32 * lrclk * 2 * 8);

	if (res == 0 && div > 0 && div <= 256) {
		if (mqs_priv->use_gpr) {
			regmap_update_bits(mqs_priv->regmap, IOMUXC_GPR2,
					   IMX6SX_GPR2_MQS_CLK_DIV_MASK,
					   (div - 1) << IMX6SX_GPR2_MQS_CLK_DIV_SHIFT);
			regmap_update_bits(mqs_priv->regmap, IOMUXC_GPR2,
					   IMX6SX_GPR2_MQS_OVERSAMPLE_MASK, 0);
		} else {
			regmap_update_bits(mqs_priv->regmap, REG_MQS_CTRL,
					   MQS_CLK_DIV_MASK,
					   (div - 1) << MQS_CLK_DIV_SHIFT);
			regmap_update_bits(mqs_priv->regmap, REG_MQS_CTRL,
					   MQS_OVERSAMPLE_MASK, 0);
		}
	} else {
		dev_err(component->dev, "can't get proper divider\n");
	}

	return 0;
}

static int fsl_mqs_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	/* Only LEFT_J & SLAVE mode is supported. */
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

static int fsl_mqs_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct fsl_mqs *mqs_priv = snd_soc_component_get_drvdata(component);

	if (mqs_priv->use_gpr)
		regmap_update_bits(mqs_priv->regmap, IOMUXC_GPR2,
				   IMX6SX_GPR2_MQS_EN_MASK,
				   1 << IMX6SX_GPR2_MQS_EN_SHIFT);
	else
		regmap_update_bits(mqs_priv->regmap, REG_MQS_CTRL,
				   MQS_EN_MASK,
				   1 << MQS_EN_SHIFT);
	return 0;
}

static void fsl_mqs_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct fsl_mqs *mqs_priv = snd_soc_component_get_drvdata(component);

	if (mqs_priv->use_gpr)
		regmap_update_bits(mqs_priv->regmap, IOMUXC_GPR2,
				   IMX6SX_GPR2_MQS_EN_MASK, 0);
	else
		regmap_update_bits(mqs_priv->regmap, REG_MQS_CTRL,
				   MQS_EN_MASK, 0);
}

static const struct snd_soc_component_driver soc_codec_fsl_mqs = {
	.idle_bias_on = 1,
	.non_legacy_dai_naming	= 1,
};

static const struct snd_soc_dai_ops fsl_mqs_dai_ops = {
	.startup = fsl_mqs_startup,
	.shutdown = fsl_mqs_shutdown,
	.hw_params = fsl_mqs_hw_params,
	.set_fmt = fsl_mqs_set_dai_fmt,
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

static const struct regmap_config fsl_mqs_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = REG_MQS_CTRL,
	.cache_type = REGCACHE_NONE,
};

static int fsl_mqs_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *gpr_np = NULL;
	struct fsl_mqs *mqs_priv;
	void __iomem *regs;
	int ret;

	mqs_priv = devm_kzalloc(&pdev->dev, sizeof(*mqs_priv), GFP_KERNEL);
	if (!mqs_priv)
		return -ENOMEM;

	/* On i.MX6sx the MQS control register is in GPR domain
	 * But in i.MX8QM/i.MX8QXP the control register is moved
	 * to its own domain.
	 */
	if (of_device_is_compatible(np, "fsl,imx8qm-mqs"))
		mqs_priv->use_gpr = false;
	else
		mqs_priv->use_gpr = true;

	if (mqs_priv->use_gpr) {
		gpr_np = of_parse_phandle(np, "gpr", 0);
		if (!gpr_np) {
			dev_err(&pdev->dev, "failed to get gpr node by phandle\n");
			return -EINVAL;
		}

		mqs_priv->regmap = syscon_node_to_regmap(gpr_np);
		if (IS_ERR(mqs_priv->regmap)) {
			dev_err(&pdev->dev, "failed to get gpr regmap\n");
			ret = PTR_ERR(mqs_priv->regmap);
			goto err_free_gpr_np;
		}
	} else {
		regs = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(regs))
			return PTR_ERR(regs);

		mqs_priv->regmap = devm_regmap_init_mmio_clk(&pdev->dev,
							     "core",
							     regs,
							     &fsl_mqs_regmap_config);
		if (IS_ERR(mqs_priv->regmap)) {
			dev_err(&pdev->dev, "failed to init regmap: %ld\n",
				PTR_ERR(mqs_priv->regmap));
			return PTR_ERR(mqs_priv->regmap);
		}

		mqs_priv->ipg = devm_clk_get(&pdev->dev, "core");
		if (IS_ERR(mqs_priv->ipg)) {
			dev_err(&pdev->dev, "failed to get the clock: %ld\n",
				PTR_ERR(mqs_priv->ipg));
			return PTR_ERR(mqs_priv->ipg);
		}
	}

	mqs_priv->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(mqs_priv->mclk)) {
		dev_err(&pdev->dev, "failed to get the clock: %ld\n",
			PTR_ERR(mqs_priv->mclk));
		ret = PTR_ERR(mqs_priv->mclk);
		goto err_free_gpr_np;
	}

	dev_set_drvdata(&pdev->dev, mqs_priv);
	pm_runtime_enable(&pdev->dev);

	ret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_fsl_mqs,
			&fsl_mqs_dai, 1);
	if (ret)
		goto err_free_gpr_np;
	return 0;

err_free_gpr_np:
	of_node_put(gpr_np);

	return ret;
}

static int fsl_mqs_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	return 0;
}

#ifdef CONFIG_PM
static int fsl_mqs_runtime_resume(struct device *dev)
{
	struct fsl_mqs *mqs_priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(mqs_priv->ipg);
	if (ret) {
		dev_err(dev, "failed to enable ipg clock\n");
		return ret;
	}

	ret = clk_prepare_enable(mqs_priv->mclk);
	if (ret) {
		dev_err(dev, "failed to enable mclk clock\n");
		clk_disable_unprepare(mqs_priv->ipg);
		return ret;
	}

	if (mqs_priv->use_gpr)
		regmap_write(mqs_priv->regmap, IOMUXC_GPR2,
			     mqs_priv->reg_iomuxc_gpr2);
	else
		regmap_write(mqs_priv->regmap, REG_MQS_CTRL,
			     mqs_priv->reg_mqs_ctrl);
	return 0;
}

static int fsl_mqs_runtime_suspend(struct device *dev)
{
	struct fsl_mqs *mqs_priv = dev_get_drvdata(dev);

	if (mqs_priv->use_gpr)
		regmap_read(mqs_priv->regmap, IOMUXC_GPR2,
			    &mqs_priv->reg_iomuxc_gpr2);
	else
		regmap_read(mqs_priv->regmap, REG_MQS_CTRL,
			    &mqs_priv->reg_mqs_ctrl);

	clk_disable_unprepare(mqs_priv->mclk);
	clk_disable_unprepare(mqs_priv->ipg);

	return 0;
}
#endif

static const struct dev_pm_ops fsl_mqs_pm_ops = {
	SET_RUNTIME_PM_OPS(fsl_mqs_runtime_suspend,
			   fsl_mqs_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id fsl_mqs_dt_ids[] = {
	{ .compatible = "fsl,imx8qm-mqs", },
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
		.pm = &fsl_mqs_pm_ops,
	},
};

module_platform_driver(fsl_mqs_driver);

MODULE_AUTHOR("Shengjiu Wang <Shengjiu.Wang@nxp.com>");
MODULE_DESCRIPTION("MQS codec driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform: fsl-mqs");
