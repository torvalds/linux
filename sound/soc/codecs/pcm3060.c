// SPDX-License-Identifier: GPL-2.0
//
// PCM3060 codec driver
//
// Copyright (C) 2018 Kirill Marinushkin <kmarinushkin@birdec.tech>

#include <linux/module.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "pcm3060.h"

/* dai */

static int pcm3060_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			      unsigned int freq, int dir)
{
	struct snd_soc_component *comp = dai->component;
	struct pcm3060_priv *priv = snd_soc_component_get_drvdata(comp);
	unsigned int reg;
	unsigned int val;

	if (dir != SND_SOC_CLOCK_IN) {
		dev_err(comp->dev, "unsupported sysclock dir: %d\n", dir);
		return -EINVAL;
	}

	switch (clk_id) {
	case PCM3060_CLK_DEF:
		val = 0;
		break;

	case PCM3060_CLK1:
		val = (dai->id == PCM3060_DAI_ID_DAC ? PCM3060_REG_CSEL : 0);
		break;

	case PCM3060_CLK2:
		val = (dai->id == PCM3060_DAI_ID_DAC ? 0 : PCM3060_REG_CSEL);
		break;

	default:
		dev_err(comp->dev, "unsupported sysclock id: %d\n", clk_id);
		return -EINVAL;
	}

	if (dai->id == PCM3060_DAI_ID_DAC)
		reg = PCM3060_REG67;
	else
		reg = PCM3060_REG72;

	regmap_update_bits(priv->regmap, reg, PCM3060_REG_CSEL, val);

	priv->dai[dai->id].sclk_freq = freq;

	return 0;
}

static int pcm3060_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *comp = dai->component;
	struct pcm3060_priv *priv = snd_soc_component_get_drvdata(comp);
	unsigned int reg;
	unsigned int val;

	if ((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF) {
		dev_err(comp->dev, "unsupported DAI polarity: 0x%x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		priv->dai[dai->id].is_master = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		priv->dai[dai->id].is_master = false;
		break;
	default:
		dev_err(comp->dev, "unsupported DAI master mode: 0x%x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		val = PCM3060_REG_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val = PCM3060_REG_FMT_RJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val = PCM3060_REG_FMT_LJ;
		break;
	default:
		dev_err(comp->dev, "unsupported DAI format: 0x%x\n", fmt);
		return -EINVAL;
	}

	if (dai->id == PCM3060_DAI_ID_DAC)
		reg = PCM3060_REG67;
	else
		reg = PCM3060_REG72;

	regmap_update_bits(priv->regmap, reg, PCM3060_REG_MASK_FMT, val);

	return 0;
}

static int pcm3060_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *comp = dai->component;
	struct pcm3060_priv *priv = snd_soc_component_get_drvdata(comp);
	unsigned int rate;
	unsigned int ratio;
	unsigned int reg;
	unsigned int val;

	if (!priv->dai[dai->id].is_master) {
		val = PCM3060_REG_MS_S;
		goto val_ready;
	}

	rate = params_rate(params);
	if (!rate) {
		dev_err(comp->dev, "rate is not configured\n");
		return -EINVAL;
	}

	ratio = priv->dai[dai->id].sclk_freq / rate;

	switch (ratio) {
	case 768:
		val = PCM3060_REG_MS_M768;
		break;
	case 512:
		val = PCM3060_REG_MS_M512;
		break;
	case 384:
		val = PCM3060_REG_MS_M384;
		break;
	case 256:
		val = PCM3060_REG_MS_M256;
		break;
	case 192:
		val = PCM3060_REG_MS_M192;
		break;
	case 128:
		val = PCM3060_REG_MS_M128;
		break;
	default:
		dev_err(comp->dev, "unsupported ratio: %d\n", ratio);
		return -EINVAL;
	}

val_ready:
	if (dai->id == PCM3060_DAI_ID_DAC)
		reg = PCM3060_REG67;
	else
		reg = PCM3060_REG72;

	regmap_update_bits(priv->regmap, reg, PCM3060_REG_MASK_MS, val);

	return 0;
}

static const struct snd_soc_dai_ops pcm3060_dai_ops = {
	.set_sysclk = pcm3060_set_sysclk,
	.set_fmt = pcm3060_set_fmt,
	.hw_params = pcm3060_hw_params,
};

#define PCM3060_DAI_RATES_ADC	(SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_32000 | \
				 SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
				 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define PCM3060_DAI_RATES_DAC	(PCM3060_DAI_RATES_ADC | \
				 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

static struct snd_soc_dai_driver pcm3060_dai[] = {
	{
		.name = "pcm3060-dac",
		.id = PCM3060_DAI_ID_DAC,
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = PCM3060_DAI_RATES_DAC,
			.formats = SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &pcm3060_dai_ops,
	},
	{
		.name = "pcm3060-adc",
		.id = PCM3060_DAI_ID_ADC,
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = PCM3060_DAI_RATES_ADC,
			.formats = SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &pcm3060_dai_ops,
	},
};

/* dapm */

static DECLARE_TLV_DB_SCALE(pcm3060_dapm_tlv, -10050, 50, 1);

static const struct snd_kcontrol_new pcm3060_dapm_controls[] = {
	SOC_DOUBLE_R_RANGE_TLV("Master Playback Volume",
			       PCM3060_REG65, PCM3060_REG66, 0,
			       PCM3060_REG_AT2_MIN, PCM3060_REG_AT2_MAX,
			       0, pcm3060_dapm_tlv),
	SOC_DOUBLE("Master Playback Switch", PCM3060_REG68,
		   PCM3060_REG_SHIFT_MUT21, PCM3060_REG_SHIFT_MUT22, 1, 1),

	SOC_DOUBLE_R_RANGE_TLV("Master Capture Volume",
			       PCM3060_REG70, PCM3060_REG71, 0,
			       PCM3060_REG_AT1_MIN, PCM3060_REG_AT1_MAX,
			       0, pcm3060_dapm_tlv),
	SOC_DOUBLE("Master Capture Switch", PCM3060_REG73,
		   PCM3060_REG_SHIFT_MUT11, PCM3060_REG_SHIFT_MUT12, 1, 1),
};

static const struct snd_soc_dapm_widget pcm3060_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", PCM3060_REG64,
			 PCM3060_REG_SHIFT_DAPSV, 1),

	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_OUTPUT("OUTR"),

	SND_SOC_DAPM_INPUT("INL"),
	SND_SOC_DAPM_INPUT("INR"),

	SND_SOC_DAPM_ADC("ADC", "Capture", PCM3060_REG64,
			 PCM3060_REG_SHIFT_ADPSV, 1),
};

static const struct snd_soc_dapm_route pcm3060_dapm_map[] = {
	{ "OUTL", NULL, "DAC" },
	{ "OUTR", NULL, "DAC" },

	{ "ADC", NULL, "INL" },
	{ "ADC", NULL, "INR" },
};

/* soc component */

static const struct snd_soc_component_driver pcm3060_soc_comp_driver = {
	.controls = pcm3060_dapm_controls,
	.num_controls = ARRAY_SIZE(pcm3060_dapm_controls),
	.dapm_widgets = pcm3060_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(pcm3060_dapm_widgets),
	.dapm_routes = pcm3060_dapm_map,
	.num_dapm_routes = ARRAY_SIZE(pcm3060_dapm_map),
};

/* regmap */

static bool pcm3060_reg_writeable(struct device *dev, unsigned int reg)
{
	return (reg >= PCM3060_REG64);
}

static bool pcm3060_reg_readable(struct device *dev, unsigned int reg)
{
	return (reg >= PCM3060_REG64);
}

static bool pcm3060_reg_volatile(struct device *dev, unsigned int reg)
{
	/* PCM3060_REG64 is volatile */
	return (reg == PCM3060_REG64);
}

static const struct reg_default pcm3060_reg_defaults[] = {
	{ PCM3060_REG64,  0xF0 },
	{ PCM3060_REG65,  0xFF },
	{ PCM3060_REG66,  0xFF },
	{ PCM3060_REG67,  0x00 },
	{ PCM3060_REG68,  0x00 },
	{ PCM3060_REG69,  0x00 },
	{ PCM3060_REG70,  0xD7 },
	{ PCM3060_REG71,  0xD7 },
	{ PCM3060_REG72,  0x00 },
	{ PCM3060_REG73,  0x00 },
};

const struct regmap_config pcm3060_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.writeable_reg = pcm3060_reg_writeable,
	.readable_reg = pcm3060_reg_readable,
	.volatile_reg = pcm3060_reg_volatile,
	.max_register = PCM3060_REG73,
	.reg_defaults = pcm3060_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(pcm3060_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL(pcm3060_regmap);

/* device */

static void pcm3060_parse_dt(const struct device_node *np,
			     struct pcm3060_priv *priv)
{
	priv->out_se = of_property_read_bool(np, "ti,out-single-ended");
}

int pcm3060_probe(struct device *dev)
{
	int rc;
	struct pcm3060_priv *priv = dev_get_drvdata(dev);

	/* soft reset */
	rc = regmap_update_bits(priv->regmap, PCM3060_REG64,
				PCM3060_REG_MRST, 0);
	if (rc) {
		dev_err(dev, "failed to reset component, rc=%d\n", rc);
		return rc;
	}

	if (dev->of_node)
		pcm3060_parse_dt(dev->of_node, priv);

	if (priv->out_se)
		regmap_update_bits(priv->regmap, PCM3060_REG64,
				   PCM3060_REG_SE, PCM3060_REG_SE);

	rc = devm_snd_soc_register_component(dev, &pcm3060_soc_comp_driver,
					     pcm3060_dai,
					     ARRAY_SIZE(pcm3060_dai));
	if (rc) {
		dev_err(dev, "failed to register component, rc=%d\n", rc);
		return rc;
	}

	return 0;
}
EXPORT_SYMBOL(pcm3060_probe);

MODULE_DESCRIPTION("PCM3060 codec driver");
MODULE_AUTHOR("Kirill Marinushkin <kmarinushkin@birdec.tech>");
MODULE_LICENSE("GPL v2");
