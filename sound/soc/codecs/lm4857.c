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

static const unsigned int lm4857_mode_values[] = {
	0,
	6,
	7,
	8,
	9,
};

static const char * const lm4857_mode_texts[] = {
	"Off",
	"Earpiece",
	"Loudspeaker",
	"Loudspeaker + Headphone",
	"Headphone",
};

static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(lm4857_mode_enum,
	LM4857_CTRL, 0, 0xf, lm4857_mode_texts, lm4857_mode_values);

static const struct snd_kcontrol_new lm4857_mode_ctrl =
	SOC_DAPM_ENUM("Mode", lm4857_mode_enum);

static const struct snd_soc_dapm_widget lm4857_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN"),

	SND_SOC_DAPM_DEMUX("Mode", SND_SOC_NOPM, 0, 0, &lm4857_mode_ctrl),

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
};

static const struct snd_soc_dapm_route lm4857_routes[] = {
	{ "Mode", NULL, "IN" },
	{ "LS", "Loudspeaker", "Mode" },
	{ "LS", "Loudspeaker + Headphone", "Mode" },
	{ "HP", "Headphone", "Mode" },
	{ "HP", "Loudspeaker + Headphone", "Mode" },
	{ "EP", "Earpiece", "Mode" },
};

static const struct snd_soc_component_driver lm4857_component_driver = {
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
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(i2c, &lm4857_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return devm_snd_soc_register_component(&i2c->dev,
		&lm4857_component_driver, NULL, 0);
}

static const struct i2c_device_id lm4857_i2c_id[] = {
	{ "lm4857", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm4857_i2c_id);

static struct i2c_driver lm4857_i2c_driver = {
	.driver = {
		.name = "lm4857",
	},
	.probe = lm4857_i2c_probe,
	.id_table = lm4857_i2c_id,
};

module_i2c_driver(lm4857_i2c_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("LM4857 amplifier driver");
MODULE_LICENSE("GPL");
