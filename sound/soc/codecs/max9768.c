// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAX9768 AMP driver
 *
 * Copyright (C) 2011, 2012 by Wolfram Sang, Pengutronix e.K.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/regmap.h>

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/max9768.h>

/* "Registers" */
#define MAX9768_VOL 0
#define MAX9768_CTRL 3

/* Commands */
#define MAX9768_CTRL_PWM 0x15
#define MAX9768_CTRL_FILTERLESS 0x16

struct max9768 {
	struct regmap *regmap;
	int mute_gpio;
	int shdn_gpio;
	u32 flags;
};

static const struct reg_default max9768_default_regs[] = {
	{ 0, 0 },
	{ 3,  MAX9768_CTRL_FILTERLESS},
};

static int max9768_get_gpio(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct max9768 *max9768 = snd_soc_component_get_drvdata(c);
	int val = gpio_get_value_cansleep(max9768->mute_gpio);

	ucontrol->value.integer.value[0] = !val;

	return 0;
}

static int max9768_set_gpio(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct max9768 *max9768 = snd_soc_component_get_drvdata(c);

	gpio_set_value_cansleep(max9768->mute_gpio, !ucontrol->value.integer.value[0]);

	return 0;
}

static const DECLARE_TLV_DB_RANGE(volume_tlv,
	0, 0, TLV_DB_SCALE_ITEM(-16150, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(-9280, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(-9030, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(-8680, 0, 0),
	4, 4, TLV_DB_SCALE_ITEM(-8430, 0, 0),
	5, 5, TLV_DB_SCALE_ITEM(-8080, 0, 0),
	6, 6, TLV_DB_SCALE_ITEM(-7830, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(-7470, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(-7220, 0, 0),
	9, 9, TLV_DB_SCALE_ITEM(-6870, 0, 0),
	10, 10, TLV_DB_SCALE_ITEM(-6620, 0, 0),
	11, 11, TLV_DB_SCALE_ITEM(-6270, 0, 0),
	12, 12, TLV_DB_SCALE_ITEM(-6020, 0, 0),
	13, 13, TLV_DB_SCALE_ITEM(-5670, 0, 0),
	14, 14, TLV_DB_SCALE_ITEM(-5420, 0, 0),
	15, 17, TLV_DB_SCALE_ITEM(-5060, 250, 0),
	18, 18, TLV_DB_SCALE_ITEM(-4370, 0, 0),
	19, 19, TLV_DB_SCALE_ITEM(-4210, 0, 0),
	20, 20, TLV_DB_SCALE_ITEM(-3960, 0, 0),
	21, 21, TLV_DB_SCALE_ITEM(-3760, 0, 0),
	22, 22, TLV_DB_SCALE_ITEM(-3600, 0, 0),
	23, 23, TLV_DB_SCALE_ITEM(-3340, 0, 0),
	24, 24, TLV_DB_SCALE_ITEM(-3150, 0, 0),
	25, 25, TLV_DB_SCALE_ITEM(-2980, 0, 0),
	26, 26, TLV_DB_SCALE_ITEM(-2720, 0, 0),
	27, 27, TLV_DB_SCALE_ITEM(-2520, 0, 0),
	28, 30, TLV_DB_SCALE_ITEM(-2350, 190, 0),
	31, 31, TLV_DB_SCALE_ITEM(-1750, 0, 0),
	32, 34, TLV_DB_SCALE_ITEM(-1640, 100, 0),
	35, 37, TLV_DB_SCALE_ITEM(-1310, 110, 0),
	38, 39, TLV_DB_SCALE_ITEM(-990, 100, 0),
	40, 40, TLV_DB_SCALE_ITEM(-710, 0, 0),
	41, 41, TLV_DB_SCALE_ITEM(-600, 0, 0),
	42, 42, TLV_DB_SCALE_ITEM(-500, 0, 0),
	43, 43, TLV_DB_SCALE_ITEM(-340, 0, 0),
	44, 44, TLV_DB_SCALE_ITEM(-190, 0, 0),
	45, 45, TLV_DB_SCALE_ITEM(-50, 0, 0),
	46, 46, TLV_DB_SCALE_ITEM(50, 0, 0),
	47, 50, TLV_DB_SCALE_ITEM(120, 40, 0),
	51, 57, TLV_DB_SCALE_ITEM(290, 50, 0),
	58, 58, TLV_DB_SCALE_ITEM(650, 0, 0),
	59, 62, TLV_DB_SCALE_ITEM(700, 60, 0),
	63, 63, TLV_DB_SCALE_ITEM(950, 0, 0)
);

static const struct snd_kcontrol_new max9768_volume[] = {
	SOC_SINGLE_TLV("Playback Volume", MAX9768_VOL, 0, 63, 0, volume_tlv),
};

static const struct snd_kcontrol_new max9768_mute[] = {
	SOC_SINGLE_BOOL_EXT("Playback Switch", 0, max9768_get_gpio, max9768_set_gpio),
};

static const struct snd_soc_dapm_widget max9768_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN"),

SND_SOC_DAPM_OUTPUT("OUT+"),
SND_SOC_DAPM_OUTPUT("OUT-"),
};

static const struct snd_soc_dapm_route max9768_dapm_routes[] = {
	{ "OUT+", NULL, "IN" },
	{ "OUT-", NULL, "IN" },
};

static int max9768_probe(struct snd_soc_component *component)
{
	struct max9768 *max9768 = snd_soc_component_get_drvdata(component);
	int ret;

	if (max9768->flags & MAX9768_FLAG_CLASSIC_PWM) {
		ret = regmap_write(max9768->regmap, MAX9768_CTRL,
			MAX9768_CTRL_PWM);
		if (ret)
			return ret;
	}

	if (gpio_is_valid(max9768->mute_gpio)) {
		ret = snd_soc_add_component_controls(component, max9768_mute,
				ARRAY_SIZE(max9768_mute));
		if (ret)
			return ret;
	}

	return 0;
}

static const struct snd_soc_component_driver max9768_component_driver = {
	.probe = max9768_probe,
	.controls = max9768_volume,
	.num_controls = ARRAY_SIZE(max9768_volume),
	.dapm_widgets = max9768_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(max9768_dapm_widgets),
	.dapm_routes = max9768_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(max9768_dapm_routes),
};

static const struct regmap_config max9768_i2c_regmap_config = {
	.reg_bits = 2,
	.val_bits = 6,
	.max_register = 3,
	.reg_defaults = max9768_default_regs,
	.num_reg_defaults = ARRAY_SIZE(max9768_default_regs),
	.cache_type = REGCACHE_RBTREE,
};

static int max9768_i2c_probe(struct i2c_client *client)
{
	struct max9768 *max9768;
	struct max9768_pdata *pdata = client->dev.platform_data;
	int err;

	max9768 = devm_kzalloc(&client->dev, sizeof(*max9768), GFP_KERNEL);
	if (!max9768)
		return -ENOMEM;

	if (pdata) {
		/* Mute on powerup to avoid clicks */
		err = devm_gpio_request_one(&client->dev, pdata->mute_gpio,
				GPIOF_INIT_HIGH, "MAX9768 Mute");
		max9768->mute_gpio = err ?: pdata->mute_gpio;

		/* Activate chip by releasing shutdown, enables I2C */
		err = devm_gpio_request_one(&client->dev, pdata->shdn_gpio,
				GPIOF_INIT_HIGH, "MAX9768 Shutdown");
		max9768->shdn_gpio = err ?: pdata->shdn_gpio;

		max9768->flags = pdata->flags;
	} else {
		max9768->shdn_gpio = -EINVAL;
		max9768->mute_gpio = -EINVAL;
	}

	i2c_set_clientdata(client, max9768);

	max9768->regmap = devm_regmap_init_i2c(client, &max9768_i2c_regmap_config);
	if (IS_ERR(max9768->regmap))
		return PTR_ERR(max9768->regmap);

	return devm_snd_soc_register_component(&client->dev,
		&max9768_component_driver, NULL, 0);
}

static const struct i2c_device_id max9768_i2c_id[] = {
	{ "max9768", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9768_i2c_id);

static struct i2c_driver max9768_i2c_driver = {
	.driver = {
		.name = "max9768",
	},
	.probe_new = max9768_i2c_probe,
	.id_table = max9768_i2c_id,
};
module_i2c_driver(max9768_i2c_driver);

MODULE_AUTHOR("Wolfram Sang <kernel@pengutronix.de>");
MODULE_DESCRIPTION("ASoC MAX9768 amplifier driver");
MODULE_LICENSE("GPL v2");
