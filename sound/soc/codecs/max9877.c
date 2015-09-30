/*
 * max9877.c  --  amp driver for max9877
 *
 * Copyright (C) 2009 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "max9877.h"

static const struct reg_default max9877_regs[] = {
	{ 0, 0x40 },
	{ 1, 0x00 },
	{ 2, 0x00 },
	{ 3, 0x00 },
	{ 4, 0x49 },
};

static const DECLARE_TLV_DB_RANGE(max9877_pgain_tlv,
	0, 1, TLV_DB_SCALE_ITEM(0, 900, 0),
	2, 2, TLV_DB_SCALE_ITEM(2000, 0, 0)
);

static const DECLARE_TLV_DB_RANGE(max9877_output_tlv,
	0, 7, TLV_DB_SCALE_ITEM(-7900, 400, 1),
	8, 15, TLV_DB_SCALE_ITEM(-4700, 300, 0),
	16, 23, TLV_DB_SCALE_ITEM(-2300, 200, 0),
	24, 31, TLV_DB_SCALE_ITEM(-700, 100, 0)
);

static const char *max9877_out_mode[] = {
	"INA -> SPK",
	"INA -> HP",
	"INA -> SPK and HP",
	"INB -> SPK",
	"INB -> HP",
	"INB -> SPK and HP",
	"INA + INB -> SPK",
	"INA + INB -> HP",
	"INA + INB -> SPK and HP",
};

static const char *max9877_osc_mode[] = {
	"1176KHz",
	"1100KHz",
	"700KHz",
};

static const struct soc_enum max9877_enum[] = {
	SOC_ENUM_SINGLE(MAX9877_OUTPUT_MODE, 0, ARRAY_SIZE(max9877_out_mode),
			max9877_out_mode),
	SOC_ENUM_SINGLE(MAX9877_OUTPUT_MODE, MAX9877_OSC_OFFSET,
			ARRAY_SIZE(max9877_osc_mode), max9877_osc_mode),
};

static const struct snd_kcontrol_new max9877_controls[] = {
	SOC_SINGLE_TLV("MAX9877 PGAINA Playback Volume",
		       MAX9877_INPUT_MODE, 0, 2, 0, max9877_pgain_tlv),
	SOC_SINGLE_TLV("MAX9877 PGAINB Playback Volume",
		       MAX9877_INPUT_MODE, 2, 2, 0, max9877_pgain_tlv),
	SOC_SINGLE_TLV("MAX9877 Amp Speaker Playback Volume",
		       MAX9877_SPK_VOLUME, 0, 31, 0, max9877_output_tlv),
	SOC_DOUBLE_R_TLV("MAX9877 Amp HP Playback Volume",
			 MAX9877_HPL_VOLUME, MAX9877_HPR_VOLUME, 0, 31, 0,
			 max9877_output_tlv),
	SOC_SINGLE("MAX9877 INB Stereo Switch",
		   MAX9877_INPUT_MODE, 4, 1, 1),
	SOC_SINGLE("MAX9877 INA Stereo Switch",
		   MAX9877_INPUT_MODE, 5, 1, 1),
	SOC_SINGLE("MAX9877 Zero-crossing detection Switch",
		   MAX9877_INPUT_MODE, 6, 1, 0),
	SOC_SINGLE("MAX9877 Bypass Mode Switch",
		   MAX9877_OUTPUT_MODE, 6, 1, 0),
	SOC_ENUM("MAX9877 Output Mode", max9877_enum[0]),
	SOC_ENUM("MAX9877 Oscillator Mode", max9877_enum[1]),
};

static const struct snd_soc_dapm_widget max9877_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("INA1"),
SND_SOC_DAPM_INPUT("INA2"),
SND_SOC_DAPM_INPUT("INB1"),
SND_SOC_DAPM_INPUT("INB2"),
SND_SOC_DAPM_INPUT("RXIN+"),
SND_SOC_DAPM_INPUT("RXIN-"),

SND_SOC_DAPM_PGA("SHDN", MAX9877_OUTPUT_MODE, 7, 1, NULL, 0),

SND_SOC_DAPM_OUTPUT("OUT+"),
SND_SOC_DAPM_OUTPUT("OUT-"),
SND_SOC_DAPM_OUTPUT("HPL"),
SND_SOC_DAPM_OUTPUT("HPR"),
};

static const struct snd_soc_dapm_route max9877_dapm_routes[] = {
	{ "SHDN", NULL, "INA1" },
	{ "SHDN", NULL, "INA2" },
	{ "SHDN", NULL, "INB1" },
	{ "SHDN", NULL, "INB2" },

	{ "OUT+", NULL, "RXIN+" },
	{ "OUT+", NULL, "SHDN" },

	{ "OUT-", NULL, "SHDN" },
	{ "OUT-", NULL, "RXIN-" },

	{ "HPL", NULL, "SHDN" },
	{ "HPR", NULL, "SHDN" },
};

static const struct snd_soc_component_driver max9877_component_driver = {
	.controls = max9877_controls,
	.num_controls = ARRAY_SIZE(max9877_controls),

	.dapm_widgets = max9877_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(max9877_dapm_widgets),
	.dapm_routes = max9877_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(max9877_dapm_routes),
};

static const struct regmap_config max9877_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.reg_defaults = max9877_regs,
	.num_reg_defaults = ARRAY_SIZE(max9877_regs),
	.cache_type = REGCACHE_RBTREE,
};

static int max9877_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct regmap *regmap;
	int i;

	regmap = devm_regmap_init_i2c(client, &max9877_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Ensure the device is in reset state */
	for (i = 0; i < ARRAY_SIZE(max9877_regs); i++)
		regmap_write(regmap, max9877_regs[i].reg, max9877_regs[i].def);

	return devm_snd_soc_register_component(&client->dev,
			&max9877_component_driver, NULL, 0);
}

static const struct i2c_device_id max9877_i2c_id[] = {
	{ "max9877", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9877_i2c_id);

static struct i2c_driver max9877_i2c_driver = {
	.driver = {
		.name = "max9877",
	},
	.probe = max9877_i2c_probe,
	.id_table = max9877_i2c_id,
};

module_i2c_driver(max9877_i2c_driver);

MODULE_DESCRIPTION("ASoC MAX9877 amp driver");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_LICENSE("GPL");
