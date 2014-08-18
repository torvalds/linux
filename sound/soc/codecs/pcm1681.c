/*
 * PCM1681 ASoC codec driver
 *
 * Copyright (c) StreamUnlimited GmbH 2013
 *	Marek Belisko <marek.belisko@streamunlimited.com>
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
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define PCM1681_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE  |		\
			     SNDRV_PCM_FMTBIT_S24_LE)

#define PCM1681_PCM_RATES   (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 | \
			     SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100  | \
			     SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200  | \
			     SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)

#define PCM1681_SOFT_MUTE_ALL		0xff
#define PCM1681_DEEMPH_RATE_MASK	0x18
#define PCM1681_DEEMPH_MASK		0x01

#define PCM1681_ATT_CONTROL(X)	(X <= 6 ? X : X + 9) /* Attenuation level */
#define PCM1681_SOFT_MUTE	0x07	/* Soft mute control register */
#define PCM1681_DAC_CONTROL	0x08	/* DAC operation control */
#define PCM1681_FMT_CONTROL	0x09	/* Audio interface data format */
#define PCM1681_DEEMPH_CONTROL	0x0a	/* De-emphasis control */
#define PCM1681_ZERO_DETECT_STATUS	0x0e	/* Zero detect status reg */

static const struct reg_default pcm1681_reg_defaults[] = {
	{ 0x01,	0xff },
	{ 0x02,	0xff },
	{ 0x03,	0xff },
	{ 0x04,	0xff },
	{ 0x05,	0xff },
	{ 0x06,	0xff },
	{ 0x07,	0x00 },
	{ 0x08,	0x00 },
	{ 0x09,	0x06 },
	{ 0x0A,	0x00 },
	{ 0x0B,	0xff },
	{ 0x0C,	0x0f },
	{ 0x0D,	0x00 },
	{ 0x10,	0xff },
	{ 0x11,	0xff },
	{ 0x12,	0x00 },
	{ 0x13,	0x00 },
};

static bool pcm1681_accessible_reg(struct device *dev, unsigned int reg)
{
	return !((reg == 0x00) || (reg == 0x0f));
}

static bool pcm1681_writeable_reg(struct device *dev, unsigned register reg)
{
	return pcm1681_accessible_reg(dev, reg) &&
		(reg != PCM1681_ZERO_DETECT_STATUS);
}

struct pcm1681_private {
	struct regmap *regmap;
	unsigned int format;
	/* Current deemphasis status */
	unsigned int deemph;
	/* Current rate for deemphasis control */
	unsigned int rate;
};

static const int pcm1681_deemph[] = { 44100, 48000, 32000 };

static int pcm1681_set_deemph(struct snd_soc_codec *codec)
{
	struct pcm1681_private *priv = snd_soc_codec_get_drvdata(codec);
	int i = 0, val = -1, enable = 0;

	if (priv->deemph)
		for (i = 0; i < ARRAY_SIZE(pcm1681_deemph); i++)
			if (pcm1681_deemph[i] == priv->rate)
				val = i;

	if (val != -1) {
		regmap_update_bits(priv->regmap, PCM1681_DEEMPH_CONTROL,
					PCM1681_DEEMPH_RATE_MASK, val);
		enable = 1;
	} else
		enable = 0;

	/* enable/disable deemphasis functionality */
	return regmap_update_bits(priv->regmap, PCM1681_DEEMPH_CONTROL,
					PCM1681_DEEMPH_MASK, enable);
}

static int pcm1681_get_deemph(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct pcm1681_private *priv = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = priv->deemph;

	return 0;
}

static int pcm1681_put_deemph(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct pcm1681_private *priv = snd_soc_codec_get_drvdata(codec);

	priv->deemph = ucontrol->value.enumerated.item[0];

	return pcm1681_set_deemph(codec);
}

static int pcm1681_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct pcm1681_private *priv = snd_soc_codec_get_drvdata(codec);

	/* The PCM1681 can only be slave to all clocks */
	if ((format & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS) {
		dev_err(codec->dev, "Invalid clocking mode\n");
		return -EINVAL;
	}

	priv->format = format;

	return 0;
}

static int pcm1681_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm1681_private *priv = snd_soc_codec_get_drvdata(codec);
	int val;

	if (mute)
		val = PCM1681_SOFT_MUTE_ALL;
	else
		val = 0;

	return regmap_write(priv->regmap, PCM1681_SOFT_MUTE, val);
}

static int pcm1681_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct pcm1681_private *priv = snd_soc_codec_get_drvdata(codec);
	int val = 0, ret;

	priv->rate = params_rate(params);

	switch (priv->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 24:
			val = 0;
			break;
		case 16:
			val = 3;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
		val = 0x04;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val = 0x05;
		break;
	default:
		dev_err(codec->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap, PCM1681_FMT_CONTROL, 0x0f, val);
	if (ret < 0)
		return ret;

	return pcm1681_set_deemph(codec);
}

static const struct snd_soc_dai_ops pcm1681_dai_ops = {
	.set_fmt	= pcm1681_set_dai_fmt,
	.hw_params	= pcm1681_hw_params,
	.digital_mute	= pcm1681_digital_mute,
};

static const struct snd_soc_dapm_widget pcm1681_dapm_widgets[] = {
SND_SOC_DAPM_OUTPUT("VOUT1"),
SND_SOC_DAPM_OUTPUT("VOUT2"),
SND_SOC_DAPM_OUTPUT("VOUT3"),
SND_SOC_DAPM_OUTPUT("VOUT4"),
SND_SOC_DAPM_OUTPUT("VOUT5"),
SND_SOC_DAPM_OUTPUT("VOUT6"),
SND_SOC_DAPM_OUTPUT("VOUT7"),
SND_SOC_DAPM_OUTPUT("VOUT8"),
};

static const struct snd_soc_dapm_route pcm1681_dapm_routes[] = {
	{ "VOUT1", NULL, "Playback" },
	{ "VOUT2", NULL, "Playback" },
	{ "VOUT3", NULL, "Playback" },
	{ "VOUT4", NULL, "Playback" },
	{ "VOUT5", NULL, "Playback" },
	{ "VOUT6", NULL, "Playback" },
	{ "VOUT7", NULL, "Playback" },
	{ "VOUT8", NULL, "Playback" },
};

static const DECLARE_TLV_DB_SCALE(pcm1681_dac_tlv, -6350, 50, 1);

static const struct snd_kcontrol_new pcm1681_controls[] = {
	SOC_DOUBLE_R_TLV("Channel 1/2 Playback Volume",
			PCM1681_ATT_CONTROL(1), PCM1681_ATT_CONTROL(2), 0,
			0x7f, 0, pcm1681_dac_tlv),
	SOC_DOUBLE_R_TLV("Channel 3/4 Playback Volume",
			PCM1681_ATT_CONTROL(3), PCM1681_ATT_CONTROL(4), 0,
			0x7f, 0, pcm1681_dac_tlv),
	SOC_DOUBLE_R_TLV("Channel 5/6 Playback Volume",
			PCM1681_ATT_CONTROL(5), PCM1681_ATT_CONTROL(6), 0,
			0x7f, 0, pcm1681_dac_tlv),
	SOC_DOUBLE_R_TLV("Channel 7/8 Playback Volume",
			PCM1681_ATT_CONTROL(7), PCM1681_ATT_CONTROL(8), 0,
			0x7f, 0, pcm1681_dac_tlv),
	SOC_SINGLE_BOOL_EXT("De-emphasis Switch", 0,
			    pcm1681_get_deemph, pcm1681_put_deemph),
};

static struct snd_soc_dai_driver pcm1681_dai = {
	.name = "pcm1681-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = PCM1681_PCM_RATES,
		.formats = PCM1681_PCM_FORMATS,
	},
	.ops = &pcm1681_dai_ops,
};

#ifdef CONFIG_OF
static const struct of_device_id pcm1681_dt_ids[] = {
	{ .compatible = "ti,pcm1681", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm1681_dt_ids);
#endif

static const struct regmap_config pcm1681_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 0x13,
	.reg_defaults		= pcm1681_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(pcm1681_reg_defaults),
	.writeable_reg		= pcm1681_writeable_reg,
	.readable_reg		= pcm1681_accessible_reg,
};

static struct snd_soc_codec_driver soc_codec_dev_pcm1681 = {
	.controls		= pcm1681_controls,
	.num_controls		= ARRAY_SIZE(pcm1681_controls),
	.dapm_widgets		= pcm1681_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm1681_dapm_widgets),
	.dapm_routes		= pcm1681_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm1681_dapm_routes),
};

static const struct i2c_device_id pcm1681_i2c_id[] = {
	{"pcm1681", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, pcm1681_i2c_id);

static int pcm1681_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	int ret;
	struct pcm1681_private *priv;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_i2c(client, &pcm1681_regmap);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&client->dev, "Failed to create regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, priv);

	return snd_soc_register_codec(&client->dev, &soc_codec_dev_pcm1681,
		&pcm1681_dai, 1);
}

static int pcm1681_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static struct i2c_driver pcm1681_i2c_driver = {
	.driver = {
		.name	= "pcm1681",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pcm1681_dt_ids),
	},
	.id_table	= pcm1681_i2c_id,
	.probe		= pcm1681_i2c_probe,
	.remove		= pcm1681_i2c_remove,
};

module_i2c_driver(pcm1681_i2c_driver);

MODULE_DESCRIPTION("Texas Instruments PCM1681 ALSA SoC Codec Driver");
MODULE_AUTHOR("Marek Belisko <marek.belisko@streamunlimited.com>");
MODULE_LICENSE("GPL");
