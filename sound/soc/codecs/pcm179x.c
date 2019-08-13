// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PCM179X ASoC codec driver
 *
 * Copyright (c) Amarula Solutions B.V. 2013
 *
 *     Michael Trimarchi <michael@amarulasolutions.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/of.h>

#include "pcm179x.h"

#define PCM179X_DAC_VOL_LEFT	0x10
#define PCM179X_DAC_VOL_RIGHT	0x11
#define PCM179X_FMT_CONTROL	0x12
#define PCM179X_MODE_CONTROL	0x13
#define PCM179X_SOFT_MUTE	PCM179X_FMT_CONTROL

#define PCM179X_FMT_MASK	0x70
#define PCM179X_FMT_SHIFT	4
#define PCM179X_MUTE_MASK	0x01
#define PCM179X_MUTE_SHIFT	0
#define PCM179X_ATLD_ENABLE	(1 << 7)

static const struct reg_default pcm179x_reg_defaults[] = {
	{ 0x10, 0xff },
	{ 0x11, 0xff },
	{ 0x12, 0x50 },
	{ 0x13, 0x00 },
	{ 0x14, 0x00 },
	{ 0x15, 0x01 },
	{ 0x16, 0x00 },
	{ 0x17, 0x00 },
};

static bool pcm179x_accessible_reg(struct device *dev, unsigned int reg)
{
	return reg >= 0x10 && reg <= 0x17;
}

static bool pcm179x_writeable_reg(struct device *dev, unsigned int reg)
{
	bool accessible;

	accessible = pcm179x_accessible_reg(dev, reg);

	return accessible && reg != 0x16 && reg != 0x17;
}

struct pcm179x_private {
	struct regmap *regmap;
	unsigned int format;
	unsigned int rate;
};

static int pcm179x_set_dai_fmt(struct snd_soc_dai *codec_dai,
                             unsigned int format)
{
	struct snd_soc_component *component = codec_dai->component;
	struct pcm179x_private *priv = snd_soc_component_get_drvdata(component);

	priv->format = format;

	return 0;
}

static int pcm179x_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;
	struct pcm179x_private *priv = snd_soc_component_get_drvdata(component);
	int ret;

	ret = regmap_update_bits(priv->regmap, PCM179X_SOFT_MUTE,
				 PCM179X_MUTE_MASK, !!mute);
	if (ret < 0)
		return ret;

	return 0;
}

static int pcm179x_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm179x_private *priv = snd_soc_component_get_drvdata(component);
	int val = 0, ret;

	priv->rate = params_rate(params);

	switch (priv->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 24:
		case 32:
			val = 2;
			break;
		case 16:
			val = 0;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
		switch (params_width(params)) {
		case 24:
		case 32:
			val = 5;
			break;
		case 16:
			val = 4;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		dev_err(component->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	val = val << PCM179X_FMT_SHIFT | PCM179X_ATLD_ENABLE;

	ret = regmap_update_bits(priv->regmap, PCM179X_FMT_CONTROL,
				 PCM179X_FMT_MASK | PCM179X_ATLD_ENABLE, val);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_dai_ops pcm179x_dai_ops = {
	.set_fmt	= pcm179x_set_dai_fmt,
	.hw_params	= pcm179x_hw_params,
	.digital_mute	= pcm179x_digital_mute,
};

static const DECLARE_TLV_DB_SCALE(pcm179x_dac_tlv, -12000, 50, 1);

static const struct snd_kcontrol_new pcm179x_controls[] = {
	SOC_DOUBLE_R_RANGE_TLV("DAC Playback Volume", PCM179X_DAC_VOL_LEFT,
			 PCM179X_DAC_VOL_RIGHT, 0, 0xf, 0xff, 0,
			 pcm179x_dac_tlv),
	SOC_SINGLE("DAC Invert Output Switch", PCM179X_MODE_CONTROL, 7, 1, 0),
	SOC_SINGLE("DAC Rolloff Filter Switch", PCM179X_MODE_CONTROL, 1, 1, 0),
};

static const struct snd_soc_dapm_widget pcm179x_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("IOUTL+"),
SND_SOC_DAPM_OUTPUT("IOUTL-"),
SND_SOC_DAPM_OUTPUT("IOUTR+"),
SND_SOC_DAPM_OUTPUT("IOUTR-"),
};

static const struct snd_soc_dapm_route pcm179x_dapm_routes[] = {
	{ "IOUTL+", NULL, "Playback" },
	{ "IOUTL-", NULL, "Playback" },
	{ "IOUTR+", NULL, "Playback" },
	{ "IOUTR-", NULL, "Playback" },
};

static struct snd_soc_dai_driver pcm179x_dai = {
	.name = "pcm179x-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 10000,
		.rate_max = 200000,
		.formats = PCM1792A_FORMATS, },
	.ops = &pcm179x_dai_ops,
};

const struct regmap_config pcm179x_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 23,
	.reg_defaults		= pcm179x_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(pcm179x_reg_defaults),
	.writeable_reg		= pcm179x_writeable_reg,
	.readable_reg		= pcm179x_accessible_reg,
};
EXPORT_SYMBOL_GPL(pcm179x_regmap_config);

static const struct snd_soc_component_driver soc_component_dev_pcm179x = {
	.controls		= pcm179x_controls,
	.num_controls		= ARRAY_SIZE(pcm179x_controls),
	.dapm_widgets		= pcm179x_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm179x_dapm_widgets),
	.dapm_routes		= pcm179x_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm179x_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

int pcm179x_common_init(struct device *dev, struct regmap *regmap)
{
	struct pcm179x_private *pcm179x;

	pcm179x = devm_kzalloc(dev, sizeof(struct pcm179x_private),
				GFP_KERNEL);
	if (!pcm179x)
		return -ENOMEM;

	pcm179x->regmap = regmap;
	dev_set_drvdata(dev, pcm179x);

	return devm_snd_soc_register_component(dev,
			&soc_component_dev_pcm179x, &pcm179x_dai, 1);
}
EXPORT_SYMBOL_GPL(pcm179x_common_init);

MODULE_DESCRIPTION("ASoC PCM179X driver");
MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_LICENSE("GPL");
