/*
 * LM4857 AMP driver
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
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
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/tlv.h>

struct lm4857 {
	struct i2c_client *i2c;
	uint8_t mode;
};

static const uint8_t lm4857_default_regs[] = {
	0x00, 0x00, 0x00, 0x00,
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

static int lm4857_write(struct snd_soc_codec *codec, unsigned int reg,
		unsigned int value)
{
	uint8_t data;
	int ret;

	ret = snd_soc_cache_write(codec, reg, value);
	if (ret < 0)
		return ret;

	data = (reg << 6) | value;
	ret = i2c_master_send(codec->control_data, &data, 1);
	if (ret != 1) {
		dev_err(codec->dev, "Failed to write register: %d\n", ret);
		return ret;
	}

	return 0;
}

static unsigned int lm4857_read(struct snd_soc_codec *codec,
		unsigned int reg)
{
	unsigned int val;
	int ret;

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret)
		return -1;

	return val;
}

static int lm4857_get_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct lm4857 *lm4857 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.integer.value[0] = lm4857->mode;

	return 0;
}

static int lm4857_set_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct lm4857 *lm4857 = snd_soc_codec_get_drvdata(codec);
	uint8_t value = ucontrol->value.integer.value[0];

	lm4857->mode = value;

	if (codec->dapm.bias_level == SND_SOC_BIAS_ON)
		snd_soc_update_bits(codec, LM4857_CTRL, 0x0F, value + 6);

	return 1;
}

static int lm4857_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct lm4857 *lm4857 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_update_bits(codec, LM4857_CTRL, 0x0F, lm4857->mode + 6);
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_update_bits(codec, LM4857_CTRL, 0x0F, 0);
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

static const struct soc_enum lm4857_mode_enum =
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(lm4857_mode), lm4857_mode);

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

static int lm4857_probe(struct snd_soc_codec *codec)
{
	struct lm4857 *lm4857 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	codec->control_data = lm4857->i2c;

	ret = snd_soc_add_controls(codec, lm4857_controls,
			ARRAY_SIZE(lm4857_controls));
	if (ret)
		return ret;

	ret = snd_soc_dapm_new_controls(dapm, lm4857_dapm_widgets,
			ARRAY_SIZE(lm4857_dapm_widgets));
	if (ret)
		return ret;

	ret = snd_soc_dapm_add_routes(dapm, lm4857_routes,
			ARRAY_SIZE(lm4857_routes));
	if (ret)
		return ret;

	snd_soc_dapm_new_widgets(dapm);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_lm4857 = {
	.write = lm4857_write,
	.read = lm4857_read,
	.probe = lm4857_probe,
	.reg_cache_size = ARRAY_SIZE(lm4857_default_regs),
	.reg_word_size = sizeof(uint8_t),
	.reg_cache_default = lm4857_default_regs,
	.set_bias_level = lm4857_set_bias_level,
};

static int __devinit lm4857_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct lm4857 *lm4857;
	int ret;

	lm4857 = kzalloc(sizeof(*lm4857), GFP_KERNEL);
	if (!lm4857)
		return -ENOMEM;

	i2c_set_clientdata(i2c, lm4857);

	lm4857->i2c = i2c;

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_lm4857, NULL, 0);

	if (ret) {
		kfree(lm4857);
		return ret;
	}

	return 0;
}

static int __devexit lm4857_i2c_remove(struct i2c_client *i2c)
{
	struct lm4857 *lm4857 = i2c_get_clientdata(i2c);

	snd_soc_unregister_codec(&i2c->dev);
	kfree(lm4857);

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
	.remove = __devexit_p(lm4857_i2c_remove),
	.id_table = lm4857_i2c_id,
};

static int __init lm4857_init(void)
{
	return i2c_add_driver(&lm4857_i2c_driver);
}
module_init(lm4857_init);

static void __exit lm4857_exit(void)
{
	i2c_del_driver(&lm4857_i2c_driver);
}
module_exit(lm4857_exit);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("LM4857 amplifier driver");
MODULE_LICENSE("GPL");
