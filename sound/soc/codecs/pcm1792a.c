/*
 * PCM1792A ASoC codec driver
 *
 * Copyright (c) Amarula Solutions B.V. 2013
 *
 *     Michael Trimarchi <michael@amarulasolutions.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/spi/spi.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/of_device.h>

#include "pcm1792a.h"

#define PCM1792A_DAC_VOL_LEFT	0x10
#define PCM1792A_DAC_VOL_RIGHT	0x11
#define PCM1792A_FMT_CONTROL	0x12
#define PCM1792A_SOFT_MUTE	PCM1792A_FMT_CONTROL

#define PCM1792A_FMT_MASK	0x70
#define PCM1792A_FMT_SHIFT	4
#define PCM1792A_MUTE_MASK	0x01
#define PCM1792A_MUTE_SHIFT	0
#define PCM1792A_ATLD_ENABLE	(1 << 7)

static const struct reg_default pcm1792a_reg_defaults[] = {
	{ 0x10, 0xff },
	{ 0x11, 0xff },
	{ 0x12, 0x50 },
	{ 0x13, 0x00 },
	{ 0x14, 0x00 },
	{ 0x15, 0x01 },
	{ 0x16, 0x00 },
	{ 0x17, 0x00 },
};

static bool pcm1792a_accessible_reg(struct device *dev, unsigned int reg)
{
	return reg >= 0x10 && reg <= 0x17;
}

static bool pcm1792a_writeable_reg(struct device *dev, unsigned register reg)
{
	bool accessible;

	accessible = pcm1792a_accessible_reg(dev, reg);

	return accessible && reg != 0x16 && reg != 0x17;
}

struct pcm1792a_private {
	struct regmap *regmap;
	unsigned int format;
	unsigned int rate;
};

static int pcm1792a_set_dai_fmt(struct snd_soc_dai *codec_dai,
                             unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct pcm1792a_private *priv = snd_soc_codec_get_drvdata(codec);

	priv->format = format;

	return 0;
}

static int pcm1792a_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm1792a_private *priv = snd_soc_codec_get_drvdata(codec);
	int ret;

	ret = regmap_update_bits(priv->regmap, PCM1792A_SOFT_MUTE,
				 PCM1792A_MUTE_MASK, !!mute);
	if (ret < 0)
		return ret;

	return 0;
}

static int pcm1792a_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm1792a_private *priv = snd_soc_codec_get_drvdata(codec);
	int val = 0, ret;
	int pcm_format = params_format(params);

	priv->rate = params_rate(params);

	switch (priv->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		if (pcm_format == SNDRV_PCM_FORMAT_S24_LE ||
		    pcm_format == SNDRV_PCM_FORMAT_S32_LE)
			val = 0x02;
		else if (pcm_format == SNDRV_PCM_FORMAT_S16_LE)
			val = 0x00;
		break;
	case SND_SOC_DAIFMT_I2S:
		if (pcm_format == SNDRV_PCM_FORMAT_S24_LE ||
		    pcm_format == SNDRV_PCM_FORMAT_S32_LE)
			val = 0x05;
		else if (pcm_format == SNDRV_PCM_FORMAT_S16_LE)
			val = 0x04;
		break;
	default:
		dev_err(codec->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	val = val << PCM1792A_FMT_SHIFT | PCM1792A_ATLD_ENABLE;

	ret = regmap_update_bits(priv->regmap, PCM1792A_FMT_CONTROL,
				 PCM1792A_FMT_MASK | PCM1792A_ATLD_ENABLE, val);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_dai_ops pcm1792a_dai_ops = {
	.set_fmt	= pcm1792a_set_dai_fmt,
	.hw_params	= pcm1792a_hw_params,
	.digital_mute	= pcm1792a_digital_mute,
};

static const DECLARE_TLV_DB_SCALE(pcm1792a_dac_tlv, -12000, 50, 1);

static const struct snd_kcontrol_new pcm1792a_controls[] = {
	SOC_DOUBLE_R_RANGE_TLV("DAC Playback Volume", PCM1792A_DAC_VOL_LEFT,
			 PCM1792A_DAC_VOL_RIGHT, 0, 0xf, 0xff, 0,
			 pcm1792a_dac_tlv),
};

static const struct snd_soc_dapm_widget pcm1792a_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("IOUTL+"),
SND_SOC_DAPM_OUTPUT("IOUTL-"),
SND_SOC_DAPM_OUTPUT("IOUTR+"),
SND_SOC_DAPM_OUTPUT("IOUTR-"),
};

static const struct snd_soc_dapm_route pcm1792a_dapm_routes[] = {
	{ "IOUTL+", NULL, "Playback" },
	{ "IOUTL-", NULL, "Playback" },
	{ "IOUTR+", NULL, "Playback" },
	{ "IOUTR-", NULL, "Playback" },
};

static struct snd_soc_dai_driver pcm1792a_dai = {
	.name = "pcm1792a-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = PCM1792A_RATES,
		.formats = PCM1792A_FORMATS, },
	.ops = &pcm1792a_dai_ops,
};

static const struct of_device_id pcm1792a_of_match[] = {
	{ .compatible = "ti,pcm1792a", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm1792a_of_match);

static const struct regmap_config pcm1792a_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 24,
	.reg_defaults		= pcm1792a_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(pcm1792a_reg_defaults),
	.writeable_reg		= pcm1792a_writeable_reg,
	.readable_reg		= pcm1792a_accessible_reg,
};

static struct snd_soc_codec_driver soc_codec_dev_pcm1792a = {
	.controls		= pcm1792a_controls,
	.num_controls		= ARRAY_SIZE(pcm1792a_controls),
	.dapm_widgets		= pcm1792a_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm1792a_dapm_widgets),
	.dapm_routes		= pcm1792a_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm1792a_dapm_routes),
};

static int pcm1792a_spi_probe(struct spi_device *spi)
{
	struct pcm1792a_private *pcm1792a;
	int ret;

	pcm1792a = devm_kzalloc(&spi->dev, sizeof(struct pcm1792a_private),
				GFP_KERNEL);
	if (!pcm1792a)
		return -ENOMEM;

	spi_set_drvdata(spi, pcm1792a);

	pcm1792a->regmap = devm_regmap_init_spi(spi, &pcm1792a_regmap);
	if (IS_ERR(pcm1792a->regmap)) {
		ret = PTR_ERR(pcm1792a->regmap);
		dev_err(&spi->dev, "Failed to register regmap: %d\n", ret);
		return ret;
	}

	return snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_pcm1792a, &pcm1792a_dai, 1);
}

static int pcm1792a_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static const struct spi_device_id pcm1792a_spi_ids[] = {
	{ "pcm1792a", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, pcm1792a_spi_ids);

static struct spi_driver pcm1792a_codec_driver = {
	.driver = {
		.name = "pcm1792a",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(pcm1792a_of_match),
	},
	.id_table = pcm1792a_spi_ids,
	.probe = pcm1792a_spi_probe,
	.remove = pcm1792a_spi_remove,
};

module_spi_driver(pcm1792a_codec_driver);

MODULE_DESCRIPTION("ASoC PCM1792A driver");
MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_LICENSE("GPL");
