/*
 * LM4857 AMP driver
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com
 * Copyright 2011 Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/tlv.h>

struct lm4857 {
	struct regmap *regmap;
	uint8_t mode;
};

static const struct reg_default lm4857_default_regs[] = {
	{ 0x0, 0x00 },
	{ 0x1, 0x00 },
	{ 0x2, 0x00 },
	{ 0x3, 0x00 },
};

/* The register offsets in the cache array */
#define LM4857_MVOL 0
#define LM4857_LVOL 1
#define LM4857_RVOL 2
#define LM4857_CTRL 3

/* the shifts required to set these bits */
#define LM4857_3D 5
#define LM4857_WAKEUP 5
#define LM4857_EPGAIN 4

static int lm4857_get_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct lm4857 *lm4857 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = lm4857->mode;

	return 0;
}

static int lm4857_set_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct lm4857 *lm4857 = snd_soc_codec_get_drvdata(codec);
	uint8_t value = ucontrol->value.integer.value[0];

	lm4857->mode = value;

	if (codec->dapm.bias_level == SND_SOC_BIAS_ON)
		regmap_update_bits(lm4857->regmap, LM4857_CTRL, 0x0F, value + 6);

	return 1;
}

static int lm4857_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct lm4857 *lm4857 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		regmap_update_bits(lm4857->regmap, LM4857_CTRL, 0x0F,
			lm4857->mode + 6);
		break;
	case SND_SOC_BIAS_STANDBY:
		regmap_update_bits(lm4857->regmap, LM4857_CTRL, 0x0F, 0);
		break;
	default:
		break;
	}

	codec->dapm.bias_level = level;

	return 0;
}

static const char *lm4857_mode[] = {
	"Earpiece",
	"Loudspeaker",
	"Loudspeaker + Headphone",
	"Headphone",
};

static SOC_ENUM_SINGLE_EXT_DECL(lm4857_mode_enum, lm4857_mode);

static const struct snd_soc_dapm_widget lm4857_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN"),

	SND_SOC_DAPM_OUTPUT("LS"),
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_OUTPUT("EP"),
};

static const DECLARE_TLV_DB_SCALE(stereo_tlv, -4050, 150, 0);
static const DECLARE_TLV_DB_SCALE(mono_tlv, -3450, 150, 0);

static const struct snd_kcontrol_new lm4857_controls[] = {
	SOC_SINGLE_TLV("Left Playback Volume", LM4857_LVOL, 0, 31, 0,
		stereo_tlv),
	SOC_SINGLE_TLV("Right Playback Volume", LM4857_RVOL, 0, 31, 0,
		stereo_tlv),
	SOC_SINGLE_TLV("Mono Playback Volume", LM4857_MVOL, 0, 31, 0,
		mono_tlv),
	SOC_SINGLE("Spk 3D Playback Switch", LM4857_LVOL, LM4857_3D, 1, 0),
	SOC_SINGLE("HP 3D Playback Switch", LM4857_RVOL, LM4857_3D, 1, 0),
	SOC_SINGLE("Fast Wakeup Playback Switch", LM4857_CTRL,
		LM4857_WAKEUP, 1, 0),
	SOC_SINGLE("Earpiece 6dB Playback Switch", LM4857_CTRL,
		LM4857_EPGAIN, 1, 0),

	SOC_ENUM_EXT("Mode", lm4857_mode_enum,
		lm4857_get_mode, lm4857_set_mode),
};

/* There is a demux between the input signal and the output signals.
 * Currently there is no easy way to model it in ASoC and since it does not make
 * much of a difference in practice simply connect the input direclty to the
 * outputs. */
static const struct snd_soc_dapm_route lm4857_routes[] = {
	{"LS", NULL, "IN"},
	{"HP", NULL, "IN"},
	{"EP", NULL, "IN"},
};

static struct snd_soc_codec_driver soc_codec_dev_lm4857 = {
	.set_bias_level = lm4857_set_bias_level,

	.controls = lm4857_controls,
	.num_controls = ARRAY_SIZE(lm4857_controls),
	.dapm_widgets = lm4857_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(lm4857_dapm_widgets),
	.dapm_routes = lm4857_routes,
	.num_dapm_routes = ARRAY_SIZE(lm4857_routes),
};

static const struct regmap_config lm4857_regmap_config = {
	.val_bits = 6,
	.reg_bits = 2,

	.max_register = LM4857_CTRL,

	.cache_type = REGCACHE_FLAT,
	.reg_defaults = lm4857_default_regs,
	.num_reg_defaults = ARRAY_SIZE(lm4857_default_regs),
};

static int lm4857_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct lm4857 *lm4857;

	lm4857 = devm_kzalloc(&i2c->dev, sizeof(*lm4857), GFP_KERNEL);
	if (!lm4857)
		return -ENOMEM;

	i2c_set_clientdata(i2c, lm4857);

	lm4857->regmap = devm_regmap_init_i2c(i2c, &lm4857_regmap_config);
	if (IS_ERR(lm4857->regmap))
		return PTR_ERR(lm4857->regmap);

	return snd_soc_register_codec(&i2c->dev, &soc_codec_dev_lm4857, NULL, 0);
}

static int lm4857_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);
	return 0;
}

static const struct i2c_device_id lm4857_i2c_id[] = {
	{ "lm4857", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm4857_i2c_id);

static struct i2c_driver lm4857_i2c_driver = {
	.driver = {
		.name = "lm4857",
		.owner = THIS_MODULE,
	},
	.probe = lm4857_i2c_probe,
	.remove = lm4857_i2c_remove,
	.id_table = lm4857_i2c_id,
};

module_i2c_driver(lm4857_i2c_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("LM4857 amplifier driver");
MODULE_LICENSE("GPL");
