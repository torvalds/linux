// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver of Inno codec for rk3036 by Rockchip Inc.
 *
 * Author: Rockchip Inc.
 * Author: Zheng ShunQian<zhengsq@rock-chips.com>
 */

#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/soc-dapm.h>
#include <sound/soc-dai.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/io.h>

#include "inno_rk3036.h"

struct rk3036_codec_priv {
	void __iomem *base;
	struct clk *pclk;
	struct regmap *regmap;
	struct device *dev;
};

static const DECLARE_TLV_DB_MINMAX(rk3036_codec_hp_tlv, -39, 0);

static int rk3036_codec_antipop_info(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int rk3036_codec_antipop_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	int val, regval;

	regval = snd_soc_component_read(component, INNO_R09);
	val = ((regval >> INNO_R09_HPL_ANITPOP_SHIFT) &
	       INNO_R09_HP_ANTIPOP_MSK) == INNO_R09_HP_ANTIPOP_ON;
	ucontrol->value.integer.value[0] = val;

	val = ((regval >> INNO_R09_HPR_ANITPOP_SHIFT) &
	       INNO_R09_HP_ANTIPOP_MSK) == INNO_R09_HP_ANTIPOP_ON;
	ucontrol->value.integer.value[1] = val;

	return 0;
}

static int rk3036_codec_antipop_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	int val, ret, regmsk;

	val = (ucontrol->value.integer.value[0] ?
	       INNO_R09_HP_ANTIPOP_ON : INNO_R09_HP_ANTIPOP_OFF) <<
	      INNO_R09_HPL_ANITPOP_SHIFT;
	val |= (ucontrol->value.integer.value[1] ?
		INNO_R09_HP_ANTIPOP_ON : INNO_R09_HP_ANTIPOP_OFF) <<
	       INNO_R09_HPR_ANITPOP_SHIFT;

	regmsk = INNO_R09_HP_ANTIPOP_MSK << INNO_R09_HPL_ANITPOP_SHIFT |
		 INNO_R09_HP_ANTIPOP_MSK << INNO_R09_HPR_ANITPOP_SHIFT;

	ret = snd_soc_component_update_bits(component, INNO_R09,
					    regmsk, val);
	if (ret < 0)
		return ret;

	return 0;
}

#define SOC_RK3036_CODEC_ANTIPOP_DECL(xname) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.info = rk3036_codec_antipop_info, .get = rk3036_codec_antipop_get, \
	.put = rk3036_codec_antipop_put, }

static const struct snd_kcontrol_new rk3036_codec_dapm_controls[] = {
	SOC_DOUBLE_R_RANGE_TLV("Headphone Volume", INNO_R07, INNO_R08,
		INNO_HP_GAIN_SHIFT, INNO_HP_GAIN_N39DB,
		INNO_HP_GAIN_0DB, 0, rk3036_codec_hp_tlv),
	SOC_DOUBLE("Zero Cross Switch", INNO_R06, INNO_R06_VOUTL_CZ_SHIFT,
		INNO_R06_VOUTR_CZ_SHIFT, 1, 0),
	SOC_DOUBLE("Headphone Switch", INNO_R09, INNO_R09_HPL_MUTE_SHIFT,
		INNO_R09_HPR_MUTE_SHIFT, 1, 0),
	SOC_RK3036_CODEC_ANTIPOP_DECL("Anti-pop Switch"),
};

static const struct snd_kcontrol_new rk3036_codec_hpl_mixer_controls[] = {
	SOC_DAPM_SINGLE("DAC Left Out Switch", INNO_R09,
			INNO_R09_DACL_SWITCH_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new rk3036_codec_hpr_mixer_controls[] = {
	SOC_DAPM_SINGLE("DAC Right Out Switch", INNO_R09,
			INNO_R09_DACR_SWITCH_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new rk3036_codec_hpl_switch_controls[] = {
	SOC_DAPM_SINGLE("HP Left Out Switch", INNO_R05,
			INNO_R05_HPL_WORK_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new rk3036_codec_hpr_switch_controls[] = {
	SOC_DAPM_SINGLE("HP Right Out Switch", INNO_R05,
			INNO_R05_HPR_WORK_SHIFT, 1, 0),
};

static const struct snd_soc_dapm_widget rk3036_codec_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY_S("DAC PWR", 1, INNO_R06,
			      INNO_R06_DAC_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACL VREF", 2, INNO_R04,
			      INNO_R04_DACL_VREF_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACR VREF", 2, INNO_R04,
			      INNO_R04_DACR_VREF_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACL HiLo VREF", 3, INNO_R06,
			      INNO_R06_DACL_HILO_VREF_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACR HiLo VREF", 3, INNO_R06,
			      INNO_R06_DACR_HILO_VREF_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACR CLK", 3, INNO_R04,
			      INNO_R04_DACR_CLK_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("DACL CLK", 3, INNO_R04,
			      INNO_R04_DACL_CLK_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_DAC("DACL", "Left Playback", INNO_R04,
			 INNO_R04_DACL_SW_SHIFT, 0),
	SND_SOC_DAPM_DAC("DACR", "Right Playback", INNO_R04,
			 INNO_R04_DACR_SW_SHIFT, 0),

	SND_SOC_DAPM_MIXER("Left Headphone Mixer", SND_SOC_NOPM, 0, 0,
		rk3036_codec_hpl_mixer_controls,
		ARRAY_SIZE(rk3036_codec_hpl_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Headphone Mixer", SND_SOC_NOPM, 0, 0,
		rk3036_codec_hpr_mixer_controls,
		ARRAY_SIZE(rk3036_codec_hpr_mixer_controls)),

	SND_SOC_DAPM_PGA("HP Left Out", INNO_R05,
			 INNO_R05_HPL_EN_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HP Right Out", INNO_R05,
			 INNO_R05_HPR_EN_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("HP Left Switch",  SND_SOC_NOPM, 0, 0,
			   rk3036_codec_hpl_switch_controls,
			   ARRAY_SIZE(rk3036_codec_hpl_switch_controls)),
	SND_SOC_DAPM_MIXER("HP Right Switch",  SND_SOC_NOPM, 0, 0,
			   rk3036_codec_hpr_switch_controls,
			   ARRAY_SIZE(rk3036_codec_hpr_switch_controls)),

	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
};

static const struct snd_soc_dapm_route rk3036_codec_dapm_routes[] = {
	{"DACL VREF", NULL, "DAC PWR"},
	{"DACR VREF", NULL, "DAC PWR"},
	{"DACL HiLo VREF", NULL, "DAC PWR"},
	{"DACR HiLo VREF", NULL, "DAC PWR"},
	{"DACL CLK", NULL, "DAC PWR"},
	{"DACR CLK", NULL, "DAC PWR"},

	{"DACL", NULL, "DACL VREF"},
	{"DACL", NULL, "DACL HiLo VREF"},
	{"DACL", NULL, "DACL CLK"},
	{"DACR", NULL, "DACR VREF"},
	{"DACR", NULL, "DACR HiLo VREF"},
	{"DACR", NULL, "DACR CLK"},

	{"Left Headphone Mixer", "DAC Left Out Switch", "DACL"},
	{"Right Headphone Mixer", "DAC Right Out Switch", "DACR"},
	{"HP Left Out", NULL, "Left Headphone Mixer"},
	{"HP Right Out", NULL, "Right Headphone Mixer"},

	{"HP Left Switch", "HP Left Out Switch", "HP Left Out"},
	{"HP Right Switch", "HP Right Out Switch", "HP Right Out"},

	{"HPL", NULL, "HP Left Switch"},
	{"HPR", NULL, "HP Right Switch"},
};

static int rk3036_codec_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	unsigned int reg01_val = 0,  reg02_val = 0, reg03_val = 0;

	dev_dbg(component->dev, "rk3036_codec dai set fmt : %08x\n", fmt);

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		reg01_val |= INNO_R01_PINDIR_IN_SLAVE |
			     INNO_R01_I2SMODE_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		reg01_val |= INNO_R01_PINDIR_OUT_MASTER |
			     INNO_R01_I2SMODE_MASTER;
		break;
	default:
		dev_err(component->dev, "invalid fmt\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		reg02_val |= INNO_R02_DACM_PCM;
		break;
	case SND_SOC_DAIFMT_I2S:
		reg02_val |= INNO_R02_DACM_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		reg02_val |= INNO_R02_DACM_RJM;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg02_val |= INNO_R02_DACM_LJM;
		break;
	default:
		dev_err(component->dev, "set dai format failed\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		reg02_val |= INNO_R02_LRCP_NORMAL;
		reg03_val |= INNO_R03_BCP_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		reg02_val |= INNO_R02_LRCP_REVERSAL;
		reg03_val |= INNO_R03_BCP_REVERSAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg02_val |= INNO_R02_LRCP_REVERSAL;
		reg03_val |= INNO_R03_BCP_NORMAL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		reg02_val |= INNO_R02_LRCP_NORMAL;
		reg03_val |= INNO_R03_BCP_REVERSAL;
		break;
	default:
		dev_err(component->dev, "set dai format failed\n");
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, INNO_R01, INNO_R01_I2SMODE_MSK |
			    INNO_R01_PINDIR_MSK, reg01_val);
	snd_soc_component_update_bits(component, INNO_R02, INNO_R02_LRCP_MSK |
			    INNO_R02_DACM_MSK, reg02_val);
	snd_soc_component_update_bits(component, INNO_R03, INNO_R03_BCP_MSK, reg03_val);

	return 0;
}

static int rk3036_codec_dai_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *hw_params,
				      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	unsigned int reg02_val = 0, reg03_val = 0;

	switch (params_format(hw_params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		reg02_val |= INNO_R02_VWL_16BIT;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		reg02_val |= INNO_R02_VWL_20BIT;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		reg02_val |= INNO_R02_VWL_24BIT;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		reg02_val |= INNO_R02_VWL_32BIT;
		break;
	default:
		return -EINVAL;
	}

	reg02_val |= INNO_R02_LRCP_NORMAL;
	reg03_val |= INNO_R03_FWL_32BIT | INNO_R03_DACR_WORK;

	snd_soc_component_update_bits(component, INNO_R02, INNO_R02_LRCP_MSK |
			    INNO_R02_VWL_MSK, reg02_val);
	snd_soc_component_update_bits(component, INNO_R03, INNO_R03_DACR_MSK |
			    INNO_R03_FWL_MSK, reg03_val);
	return 0;
}

#define RK3036_CODEC_RATES (SNDRV_PCM_RATE_8000  | \
			    SNDRV_PCM_RATE_16000 | \
			    SNDRV_PCM_RATE_32000 | \
			    SNDRV_PCM_RATE_44100 | \
			    SNDRV_PCM_RATE_48000 | \
			    SNDRV_PCM_RATE_96000)

#define RK3036_CODEC_FMTS (SNDRV_PCM_FMTBIT_S16_LE  | \
			   SNDRV_PCM_FMTBIT_S20_3LE | \
			   SNDRV_PCM_FMTBIT_S24_LE  | \
			   SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops rk3036_codec_dai_ops = {
	.set_fmt	= rk3036_codec_dai_set_fmt,
	.hw_params	= rk3036_codec_dai_hw_params,
};

static struct snd_soc_dai_driver rk3036_codec_dai_driver[] = {
	{
		.name = "rk3036-codec-dai",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RK3036_CODEC_RATES,
			.formats = RK3036_CODEC_FMTS,
		},
		.ops = &rk3036_codec_dai_ops,
		.symmetric_rate = 1,
	},
};

static void rk3036_codec_reset(struct snd_soc_component *component)
{
	snd_soc_component_write(component, INNO_R00,
		      INNO_R00_CSR_RESET | INNO_R00_CDCR_RESET);
	snd_soc_component_write(component, INNO_R00,
		      INNO_R00_CSR_WORK | INNO_R00_CDCR_WORK);
}

static int rk3036_codec_probe(struct snd_soc_component *component)
{
	rk3036_codec_reset(component);
	return 0;
}

static void rk3036_codec_remove(struct snd_soc_component *component)
{
	rk3036_codec_reset(component);
}

static int rk3036_codec_set_bias_level(struct snd_soc_component *component,
				       enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		/* set a big current for capacitor charging. */
		snd_soc_component_write(component, INNO_R10, INNO_R10_MAX_CUR);
		/* start precharge */
		snd_soc_component_write(component, INNO_R06, INNO_R06_DAC_PRECHARGE);

		break;

	case SND_SOC_BIAS_OFF:
		/* set a big current for capacitor discharging. */
		snd_soc_component_write(component, INNO_R10, INNO_R10_MAX_CUR);
		/* start discharge. */
		snd_soc_component_write(component, INNO_R06, INNO_R06_DAC_DISCHARGE);

		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_component_driver rk3036_codec_driver = {
	.probe			= rk3036_codec_probe,
	.remove			= rk3036_codec_remove,
	.set_bias_level		= rk3036_codec_set_bias_level,
	.controls		= rk3036_codec_dapm_controls,
	.num_controls		= ARRAY_SIZE(rk3036_codec_dapm_controls),
	.dapm_routes		= rk3036_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(rk3036_codec_dapm_routes),
	.dapm_widgets		= rk3036_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(rk3036_codec_dapm_widgets),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config rk3036_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

#define GRF_SOC_CON0		0x00140
#define GRF_ACODEC_SEL		(BIT(10) | BIT(16 + 10))

static int rk3036_codec_platform_probe(struct platform_device *pdev)
{
	struct rk3036_codec_priv *priv;
	struct device_node *of_node = pdev->dev.of_node;
	void __iomem *base;
	struct regmap *grf;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->base = base;
	priv->regmap = devm_regmap_init_mmio(&pdev->dev, priv->base,
					     &rk3036_codec_regmap_config);
	if (IS_ERR(priv->regmap)) {
		dev_err(&pdev->dev, "init regmap failed\n");
		return PTR_ERR(priv->regmap);
	}

	grf = syscon_regmap_lookup_by_phandle(of_node, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(&pdev->dev, "needs 'rockchip,grf' property\n");
		return PTR_ERR(grf);
	}
	ret = regmap_write(grf, GRF_SOC_CON0, GRF_ACODEC_SEL);
	if (ret) {
		dev_err(&pdev->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}

	priv->pclk = devm_clk_get(&pdev->dev, "acodec_pclk");
	if (IS_ERR(priv->pclk))
		return PTR_ERR(priv->pclk);

	ret = clk_prepare_enable(priv->pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable clk\n");
		return ret;
	}

	priv->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, priv);

	ret = devm_snd_soc_register_component(&pdev->dev, &rk3036_codec_driver,
				     rk3036_codec_dai_driver,
				     ARRAY_SIZE(rk3036_codec_dai_driver));
	if (ret) {
		clk_disable_unprepare(priv->pclk);
		dev_set_drvdata(&pdev->dev, NULL);
	}

	return ret;
}

static void rk3036_codec_platform_remove(struct platform_device *pdev)
{
	struct rk3036_codec_priv *priv = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(priv->pclk);
}

static const struct of_device_id rk3036_codec_of_match[] __maybe_unused = {
	{ .compatible = "rockchip,rk3036-codec", },
	{}
};
MODULE_DEVICE_TABLE(of, rk3036_codec_of_match);

static struct platform_driver rk3036_codec_platform_driver = {
	.driver = {
		.name = "rk3036-codec-platform",
		.of_match_table = of_match_ptr(rk3036_codec_of_match),
	},
	.probe = rk3036_codec_platform_probe,
	.remove = rk3036_codec_platform_remove,
};

module_platform_driver(rk3036_codec_platform_driver);

MODULE_AUTHOR("Rockchip Inc.");
MODULE_DESCRIPTION("Rockchip rk3036 codec driver");
MODULE_LICENSE("GPL");
