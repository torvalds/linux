// SPDX-License-Identifier: GPL-2.0
/*
 * NXP AUDMIX ALSA SoC Digital Audio Interface (DAI) driver
 *
 * Copyright 2017 NXP
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

#include "fsl_audmix.h"

#define SOC_ENUM_SINGLE_S(xreg, xshift, xtexts) \
	SOC_ENUM_SINGLE(xreg, xshift, ARRAY_SIZE(xtexts), xtexts)

static const char
	*tdm_sel[] = { "TDM1", "TDM2", },
	*mode_sel[] = { "Disabled", "TDM1", "TDM2", "Mixed", },
	*width_sel[] = { "16b", "18b", "20b", "24b", "32b", },
	*endis_sel[] = { "Disabled", "Enabled", },
	*updn_sel[] = { "Downward", "Upward", },
	*mask_sel[] = { "Unmask", "Mask", };

static const struct soc_enum fsl_audmix_enum[] = {
/* FSL_AUDMIX_CTR enums */
SOC_ENUM_SINGLE_S(FSL_AUDMIX_CTR, FSL_AUDMIX_CTR_MIXCLK_SHIFT, tdm_sel),
SOC_ENUM_SINGLE_S(FSL_AUDMIX_CTR, FSL_AUDMIX_CTR_OUTSRC_SHIFT, mode_sel),
SOC_ENUM_SINGLE_S(FSL_AUDMIX_CTR, FSL_AUDMIX_CTR_OUTWIDTH_SHIFT, width_sel),
SOC_ENUM_SINGLE_S(FSL_AUDMIX_CTR, FSL_AUDMIX_CTR_MASKRTDF_SHIFT, mask_sel),
SOC_ENUM_SINGLE_S(FSL_AUDMIX_CTR, FSL_AUDMIX_CTR_MASKCKDF_SHIFT, mask_sel),
SOC_ENUM_SINGLE_S(FSL_AUDMIX_CTR, FSL_AUDMIX_CTR_SYNCMODE_SHIFT, endis_sel),
SOC_ENUM_SINGLE_S(FSL_AUDMIX_CTR, FSL_AUDMIX_CTR_SYNCSRC_SHIFT, tdm_sel),
/* FSL_AUDMIX_ATCR0 enums */
SOC_ENUM_SINGLE_S(FSL_AUDMIX_ATCR0, 0, endis_sel),
SOC_ENUM_SINGLE_S(FSL_AUDMIX_ATCR0, 1, updn_sel),
/* FSL_AUDMIX_ATCR1 enums */
SOC_ENUM_SINGLE_S(FSL_AUDMIX_ATCR1, 0, endis_sel),
SOC_ENUM_SINGLE_S(FSL_AUDMIX_ATCR1, 1, updn_sel),
};

struct fsl_audmix_state {
	u8 tdms;
	u8 clk;
	char msg[64];
};

static const struct fsl_audmix_state prms[4][4] = {{
	/* DIS->DIS, do nothing */
	{ .tdms = 0, .clk = 0, .msg = "" },
	/* DIS->TDM1*/
	{ .tdms = 1, .clk = 1, .msg = "DIS->TDM1: TDM1 not started!\n" },
	/* DIS->TDM2*/
	{ .tdms = 2, .clk = 2, .msg = "DIS->TDM2: TDM2 not started!\n" },
	/* DIS->MIX */
	{ .tdms = 3, .clk = 0, .msg = "DIS->MIX: Please start both TDMs!\n" }
}, {	/* TDM1->DIS */
	{ .tdms = 1, .clk = 0, .msg = "TDM1->DIS: TDM1 not started!\n" },
	/* TDM1->TDM1, do nothing */
	{ .tdms = 0, .clk = 0, .msg = "" },
	/* TDM1->TDM2 */
	{ .tdms = 3, .clk = 2, .msg = "TDM1->TDM2: Please start both TDMs!\n" },
	/* TDM1->MIX */
	{ .tdms = 3, .clk = 0, .msg = "TDM1->MIX: Please start both TDMs!\n" }
}, {	/* TDM2->DIS */
	{ .tdms = 2, .clk = 0, .msg = "TDM2->DIS: TDM2 not started!\n" },
	/* TDM2->TDM1 */
	{ .tdms = 3, .clk = 1, .msg = "TDM2->TDM1: Please start both TDMs!\n" },
	/* TDM2->TDM2, do nothing */
	{ .tdms = 0, .clk = 0, .msg = "" },
	/* TDM2->MIX */
	{ .tdms = 3, .clk = 0, .msg = "TDM2->MIX: Please start both TDMs!\n" }
}, {	/* MIX->DIS */
	{ .tdms = 3, .clk = 0, .msg = "MIX->DIS: Please start both TDMs!\n" },
	/* MIX->TDM1 */
	{ .tdms = 3, .clk = 1, .msg = "MIX->TDM1: Please start both TDMs!\n" },
	/* MIX->TDM2 */
	{ .tdms = 3, .clk = 2, .msg = "MIX->TDM2: Please start both TDMs!\n" },
	/* MIX->MIX, do nothing */
	{ .tdms = 0, .clk = 0, .msg = "" }
}, };

static int fsl_audmix_state_trans(struct snd_soc_component *comp,
				  unsigned int *mask, unsigned int *ctr,
				  const struct fsl_audmix_state prm)
{
	struct fsl_audmix *priv = snd_soc_component_get_drvdata(comp);
	/* Enforce all required TDMs are started */
	if ((priv->tdms & prm.tdms) != prm.tdms) {
		dev_dbg(comp->dev, "%s", prm.msg);
		return -EINVAL;
	}

	switch (prm.clk) {
	case 1:
	case 2:
		/* Set mix clock */
		(*mask) |= FSL_AUDMIX_CTR_MIXCLK_MASK;
		(*ctr)  |= FSL_AUDMIX_CTR_MIXCLK(prm.clk - 1);
		break;
	default:
		break;
	}

	return 0;
}

static int fsl_audmix_put_mix_clk_src(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_audmix *priv = snd_soc_component_get_drvdata(comp);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int reg_val, val, mix_clk;
	int ret = 0;

	/* Get current state */
	ret = snd_soc_component_read(comp, FSL_AUDMIX_CTR, &reg_val);
	if (ret)
		return ret;

	mix_clk = ((reg_val & FSL_AUDMIX_CTR_MIXCLK_MASK)
			>> FSL_AUDMIX_CTR_MIXCLK_SHIFT);
	val = snd_soc_enum_item_to_val(e, item[0]);

	dev_dbg(comp->dev, "TDMs=x%08x, val=x%08x\n", priv->tdms, val);

	/**
	 * Ensure the current selected mixer clock is available
	 * for configuration propagation
	 */
	if (!(priv->tdms & BIT(mix_clk))) {
		dev_err(comp->dev,
			"Started TDM%d needed for config propagation!\n",
			mix_clk + 1);
		return -EINVAL;
	}

	if (!(priv->tdms & BIT(val))) {
		dev_err(comp->dev,
			"The selected clock source has no TDM%d enabled!\n",
			val + 1);
		return -EINVAL;
	}

	return snd_soc_put_enum_double(kcontrol, ucontrol);
}

static int fsl_audmix_put_out_src(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *comp = snd_kcontrol_chip(kcontrol);
	struct fsl_audmix *priv = snd_soc_component_get_drvdata(comp);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	u32 out_src, mix_clk;
	unsigned int reg_val, val, mask = 0, ctr = 0;
	int ret = 0;

	/* Get current state */
	ret = snd_soc_component_read(comp, FSL_AUDMIX_CTR, &reg_val);
	if (ret)
		return ret;

	/* "From" state */
	out_src = ((reg_val & FSL_AUDMIX_CTR_OUTSRC_MASK)
			>> FSL_AUDMIX_CTR_OUTSRC_SHIFT);
	mix_clk = ((reg_val & FSL_AUDMIX_CTR_MIXCLK_MASK)
			>> FSL_AUDMIX_CTR_MIXCLK_SHIFT);

	/* "To" state */
	val = snd_soc_enum_item_to_val(e, item[0]);

	dev_dbg(comp->dev, "TDMs=x%08x, val=x%08x\n", priv->tdms, val);

	/* Check if state is changing ... */
	if (out_src == val)
		return 0;
	/**
	 * Ensure the current selected mixer clock is available
	 * for configuration propagation
	 */
	if (!(priv->tdms & BIT(mix_clk))) {
		dev_err(comp->dev,
			"Started TDM%d needed for config propagation!\n",
			mix_clk + 1);
		return -EINVAL;
	}

	/* Check state transition constraints */
	ret = fsl_audmix_state_trans(comp, &mask, &ctr, prms[out_src][val]);
	if (ret)
		return ret;

	/* Complete transition to new state */
	mask |= FSL_AUDMIX_CTR_OUTSRC_MASK;
	ctr  |= FSL_AUDMIX_CTR_OUTSRC(val);

	return snd_soc_component_update_bits(comp, FSL_AUDMIX_CTR, mask, ctr);
}

static const struct snd_kcontrol_new fsl_audmix_snd_controls[] = {
	/* FSL_AUDMIX_CTR controls */
	SOC_ENUM_EXT("Mixing Clock Source", fsl_audmix_enum[0],
		     snd_soc_get_enum_double, fsl_audmix_put_mix_clk_src),
	SOC_ENUM_EXT("Output Source", fsl_audmix_enum[1],
		     snd_soc_get_enum_double, fsl_audmix_put_out_src),
	SOC_ENUM("Output Width", fsl_audmix_enum[2]),
	SOC_ENUM("Frame Rate Diff Error", fsl_audmix_enum[3]),
	SOC_ENUM("Clock Freq Diff Error", fsl_audmix_enum[4]),
	SOC_ENUM("Sync Mode Config", fsl_audmix_enum[5]),
	SOC_ENUM("Sync Mode Clk Source", fsl_audmix_enum[6]),
	/* TDM1 Attenuation controls */
	SOC_ENUM("TDM1 Attenuation", fsl_audmix_enum[7]),
	SOC_ENUM("TDM1 Attenuation Direction", fsl_audmix_enum[8]),
	SOC_SINGLE("TDM1 Attenuation Step Divider", FSL_AUDMIX_ATCR0,
		   2, 0x00fff, 0),
	SOC_SINGLE("TDM1 Attenuation Initial Value", FSL_AUDMIX_ATIVAL0,
		   0, 0x3ffff, 0),
	SOC_SINGLE("TDM1 Attenuation Step Up Factor", FSL_AUDMIX_ATSTPUP0,
		   0, 0x3ffff, 0),
	SOC_SINGLE("TDM1 Attenuation Step Down Factor", FSL_AUDMIX_ATSTPDN0,
		   0, 0x3ffff, 0),
	SOC_SINGLE("TDM1 Attenuation Step Target", FSL_AUDMIX_ATSTPTGT0,
		   0, 0x3ffff, 0),
	/* TDM2 Attenuation controls */
	SOC_ENUM("TDM2 Attenuation", fsl_audmix_enum[9]),
	SOC_ENUM("TDM2 Attenuation Direction", fsl_audmix_enum[10]),
	SOC_SINGLE("TDM2 Attenuation Step Divider", FSL_AUDMIX_ATCR1,
		   2, 0x00fff, 0),
	SOC_SINGLE("TDM2 Attenuation Initial Value", FSL_AUDMIX_ATIVAL1,
		   0, 0x3ffff, 0),
	SOC_SINGLE("TDM2 Attenuation Step Up Factor", FSL_AUDMIX_ATSTPUP1,
		   0, 0x3ffff, 0),
	SOC_SINGLE("TDM2 Attenuation Step Down Factor", FSL_AUDMIX_ATSTPDN1,
		   0, 0x3ffff, 0),
	SOC_SINGLE("TDM2 Attenuation Step Target", FSL_AUDMIX_ATSTPTGT1,
		   0, 0x3ffff, 0),
};

static int fsl_audmix_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *comp = dai->component;
	u32 mask = 0, ctr = 0;

	/* AUDMIX is working in DSP_A format only */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		break;
	default:
		return -EINVAL;
	}

	/* For playback the AUDMIX is slave, and for record is master */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_NF:
		/* Output data will be written on positive edge of the clock */
		ctr |= FSL_AUDMIX_CTR_OUTCKPOL(0);
		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* Output data will be written on negative edge of the clock */
		ctr |= FSL_AUDMIX_CTR_OUTCKPOL(1);
		break;
	default:
		return -EINVAL;
	}

	mask |= FSL_AUDMIX_CTR_OUTCKPOL_MASK;

	return snd_soc_component_update_bits(comp, FSL_AUDMIX_CTR, mask, ctr);
}

static int fsl_audmix_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				  struct snd_soc_dai *dai)
{
	struct fsl_audmix *priv = snd_soc_dai_get_drvdata(dai);

	/* Capture stream shall not be handled */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		priv->tdms |= BIT(dai->driver->id);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		priv->tdms &= ~BIT(dai->driver->id);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops fsl_audmix_dai_ops = {
	.set_fmt      = fsl_audmix_dai_set_fmt,
	.trigger      = fsl_audmix_dai_trigger,
};

static struct snd_soc_dai_driver fsl_audmix_dai[] = {
	{
		.id   = 0,
		.name = "audmix-0",
		.playback = {
			.stream_name = "AUDMIX-Playback-0",
			.channels_min = 8,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 96000,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = FSL_AUDMIX_FORMATS,
		},
		.capture = {
			.stream_name = "AUDMIX-Capture-0",
			.channels_min = 8,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 96000,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = FSL_AUDMIX_FORMATS,
		},
		.ops = &fsl_audmix_dai_ops,
	},
	{
		.id   = 1,
		.name = "audmix-1",
		.playback = {
			.stream_name = "AUDMIX-Playback-1",
			.channels_min = 8,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 96000,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = FSL_AUDMIX_FORMATS,
		},
		.capture = {
			.stream_name = "AUDMIX-Capture-1",
			.channels_min = 8,
			.channels_max = 8,
			.rate_min = 8000,
			.rate_max = 96000,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = FSL_AUDMIX_FORMATS,
		},
		.ops = &fsl_audmix_dai_ops,
	},
};

static const struct snd_soc_component_driver fsl_audmix_component = {
	.name		  = "fsl-audmix-dai",
	.controls	  = fsl_audmix_snd_controls,
	.num_controls	  = ARRAY_SIZE(fsl_audmix_snd_controls),
};

static bool fsl_audmix_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case FSL_AUDMIX_CTR:
	case FSL_AUDMIX_STR:
	case FSL_AUDMIX_ATCR0:
	case FSL_AUDMIX_ATIVAL0:
	case FSL_AUDMIX_ATSTPUP0:
	case FSL_AUDMIX_ATSTPDN0:
	case FSL_AUDMIX_ATSTPTGT0:
	case FSL_AUDMIX_ATTNVAL0:
	case FSL_AUDMIX_ATSTP0:
	case FSL_AUDMIX_ATCR1:
	case FSL_AUDMIX_ATIVAL1:
	case FSL_AUDMIX_ATSTPUP1:
	case FSL_AUDMIX_ATSTPDN1:
	case FSL_AUDMIX_ATSTPTGT1:
	case FSL_AUDMIX_ATTNVAL1:
	case FSL_AUDMIX_ATSTP1:
		return true;
	default:
		return false;
	}
}

static bool fsl_audmix_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case FSL_AUDMIX_CTR:
	case FSL_AUDMIX_ATCR0:
	case FSL_AUDMIX_ATIVAL0:
	case FSL_AUDMIX_ATSTPUP0:
	case FSL_AUDMIX_ATSTPDN0:
	case FSL_AUDMIX_ATSTPTGT0:
	case FSL_AUDMIX_ATCR1:
	case FSL_AUDMIX_ATIVAL1:
	case FSL_AUDMIX_ATSTPUP1:
	case FSL_AUDMIX_ATSTPDN1:
	case FSL_AUDMIX_ATSTPTGT1:
		return true;
	default:
		return false;
	}
}

static const struct reg_default fsl_audmix_reg[] = {
	{ FSL_AUDMIX_CTR,       0x00060 },
	{ FSL_AUDMIX_STR,       0x00003 },
	{ FSL_AUDMIX_ATCR0,     0x00000 },
	{ FSL_AUDMIX_ATIVAL0,   0x3FFFF },
	{ FSL_AUDMIX_ATSTPUP0,  0x2AAAA },
	{ FSL_AUDMIX_ATSTPDN0,  0x30000 },
	{ FSL_AUDMIX_ATSTPTGT0, 0x00010 },
	{ FSL_AUDMIX_ATTNVAL0,  0x00000 },
	{ FSL_AUDMIX_ATSTP0,    0x00000 },
	{ FSL_AUDMIX_ATCR1,     0x00000 },
	{ FSL_AUDMIX_ATIVAL1,   0x3FFFF },
	{ FSL_AUDMIX_ATSTPUP1,  0x2AAAA },
	{ FSL_AUDMIX_ATSTPDN1,  0x30000 },
	{ FSL_AUDMIX_ATSTPTGT1, 0x00010 },
	{ FSL_AUDMIX_ATTNVAL1,  0x00000 },
	{ FSL_AUDMIX_ATSTP1,    0x00000 },
};

static const struct regmap_config fsl_audmix_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = FSL_AUDMIX_ATSTP1,
	.reg_defaults = fsl_audmix_reg,
	.num_reg_defaults = ARRAY_SIZE(fsl_audmix_reg),
	.readable_reg = fsl_audmix_readable_reg,
	.writeable_reg = fsl_audmix_writeable_reg,
	.cache_type = REGCACHE_FLAT,
};

static const struct of_device_id fsl_audmix_ids[] = {
	{
		.compatible = "fsl,imx8qm-audmix",
		.data = "imx-audmix",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_audmix_ids);

static int fsl_audmix_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fsl_audmix *priv;
	struct resource *res;
	const char *mdrv;
	const struct of_device_id *of_id;
	void __iomem *regs;
	int ret;

	of_id = of_match_device(fsl_audmix_ids, dev);
	if (!of_id || !of_id->data)
		return -EINVAL;

	mdrv = of_id->data;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* Get the addresses */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->regmap = devm_regmap_init_mmio_clk(dev, "ipg", regs,
						 &fsl_audmix_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(dev, "failed to init regmap\n");
		return PTR_ERR(priv->regmap);
	}

	priv->ipg_clk = devm_clk_get(dev, "ipg");
	if (IS_ERR(priv->ipg_clk)) {
		dev_err(dev, "failed to get ipg clock\n");
		return PTR_ERR(priv->ipg_clk);
	}

	platform_set_drvdata(pdev, priv);
	pm_runtime_enable(dev);

	ret = devm_snd_soc_register_component(dev, &fsl_audmix_component,
					      fsl_audmix_dai,
					      ARRAY_SIZE(fsl_audmix_dai));
	if (ret) {
		dev_err(dev, "failed to register ASoC DAI\n");
		return ret;
	}

	priv->pdev = platform_device_register_data(dev, mdrv, 0, NULL, 0);
	if (IS_ERR(priv->pdev)) {
		ret = PTR_ERR(priv->pdev);
		dev_err(dev, "failed to register platform %s: %d\n", mdrv, ret);
	}

	return ret;
}

static int fsl_audmix_remove(struct platform_device *pdev)
{
	struct fsl_audmix *priv = dev_get_drvdata(&pdev->dev);

	if (priv->pdev)
		platform_device_unregister(priv->pdev);

	return 0;
}

#ifdef CONFIG_PM
static int fsl_audmix_runtime_resume(struct device *dev)
{
	struct fsl_audmix *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->ipg_clk);
	if (ret) {
		dev_err(dev, "Failed to enable IPG clock: %d\n", ret);
		return ret;
	}

	regcache_cache_only(priv->regmap, false);
	regcache_mark_dirty(priv->regmap);

	return regcache_sync(priv->regmap);
}

static int fsl_audmix_runtime_suspend(struct device *dev)
{
	struct fsl_audmix *priv = dev_get_drvdata(dev);

	regcache_cache_only(priv->regmap, true);

	clk_disable_unprepare(priv->ipg_clk);

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops fsl_audmix_pm = {
	SET_RUNTIME_PM_OPS(fsl_audmix_runtime_suspend,
			   fsl_audmix_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver fsl_audmix_driver = {
	.probe = fsl_audmix_probe,
	.remove = fsl_audmix_remove,
	.driver = {
		.name = "fsl-audmix",
		.of_match_table = fsl_audmix_ids,
		.pm = &fsl_audmix_pm,
	},
};
module_platform_driver(fsl_audmix_driver);

MODULE_DESCRIPTION("NXP AUDMIX ASoC DAI driver");
MODULE_AUTHOR("Viorel Suman <viorel.suman@nxp.com>");
MODULE_ALIAS("platform:fsl-audmix");
MODULE_LICENSE("GPL v2");
