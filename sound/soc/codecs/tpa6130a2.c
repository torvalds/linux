// SPDX-License-Identifier: GPL-2.0-only
/*
 * ALSA SoC Texas Instruments TPA6130A2 headset stereo amplifier driver
 *
 * Copyright (C) Nokia Corporation
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "tpa6130a2.h"

enum tpa_model {
	TPA6130A2,
	TPA6140A2,
};

/* This struct is used to save the context */
struct tpa6130a2_data {
	struct device *dev;
	struct regmap *regmap;
	struct regulator *supply;
	struct gpio_desc *power_gpio;
	enum tpa_model id;
};

static int tpa6130a2_power(struct tpa6130a2_data *data, bool enable)
{
	int ret = 0, ret2;

	if (enable) {
		ret = regulator_enable(data->supply);
		if (ret != 0) {
			dev_err(data->dev,
				"Failed to enable supply: %d\n", ret);
			return ret;
		}
		/* Power on */
		gpiod_set_value(data->power_gpio, 1);

		/* Sync registers */
		regcache_cache_only(data->regmap, false);
		ret = regcache_sync(data->regmap);
		if (ret != 0) {
			dev_err(data->dev,
				"Failed to sync registers: %d\n", ret);
			regcache_cache_only(data->regmap, true);
			gpiod_set_value(data->power_gpio, 0);
			ret2 = regulator_disable(data->supply);
			if (ret2 != 0)
				dev_err(data->dev,
					"Failed to disable supply: %d\n", ret2);
			return ret;
		}
	} else {
		/* Powered off device does not retain registers. While device
		 * is off, any register updates (i.e. volume changes) should
		 * happen in cache only.
		 */
		regcache_mark_dirty(data->regmap);
		regcache_cache_only(data->regmap, true);

		/* Power off */
		gpiod_set_value(data->power_gpio, 0);

		ret = regulator_disable(data->supply);
		if (ret != 0) {
			dev_err(data->dev,
				"Failed to disable supply: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int tpa6130a2_power_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kctrl, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct tpa6130a2_data *data = snd_soc_component_get_drvdata(c);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		/* Before widget power up: turn chip on, sync registers */
		return tpa6130a2_power(data, true);
	} else {
		/* After widget power down: turn chip off */
		return tpa6130a2_power(data, false);
	}
}

/*
 * TPA6130 volume. From -59.5 to 4 dB with increasing step size when going
 * down in gain.
 */
static const DECLARE_TLV_DB_RANGE(tpa6130_tlv,
	0, 1, TLV_DB_SCALE_ITEM(-5950, 600, 0),
	2, 3, TLV_DB_SCALE_ITEM(-5000, 250, 0),
	4, 5, TLV_DB_SCALE_ITEM(-4550, 160, 0),
	6, 7, TLV_DB_SCALE_ITEM(-4140, 190, 0),
	8, 9, TLV_DB_SCALE_ITEM(-3650, 120, 0),
	10, 11, TLV_DB_SCALE_ITEM(-3330, 160, 0),
	12, 13, TLV_DB_SCALE_ITEM(-3040, 180, 0),
	14, 20, TLV_DB_SCALE_ITEM(-2710, 110, 0),
	21, 37, TLV_DB_SCALE_ITEM(-1960, 74, 0),
	38, 63, TLV_DB_SCALE_ITEM(-720, 45, 0)
);

static const struct snd_kcontrol_new tpa6130a2_controls[] = {
	SOC_SINGLE_TLV("Headphone Playback Volume",
		       TPA6130A2_REG_VOL_MUTE, 0, 0x3f, 0,
		       tpa6130_tlv),
};

static const DECLARE_TLV_DB_RANGE(tpa6140_tlv,
	0, 8, TLV_DB_SCALE_ITEM(-5900, 400, 0),
	9, 16, TLV_DB_SCALE_ITEM(-2500, 200, 0),
	17, 31, TLV_DB_SCALE_ITEM(-1000, 100, 0)
);

static const struct snd_kcontrol_new tpa6140a2_controls[] = {
	SOC_SINGLE_TLV("Headphone Playback Volume",
		       TPA6130A2_REG_VOL_MUTE, 1, 0x1f, 0,
		       tpa6140_tlv),
};

static int tpa6130a2_component_probe(struct snd_soc_component *component)
{
	struct tpa6130a2_data *data = snd_soc_component_get_drvdata(component);

	if (data->id == TPA6140A2)
		return snd_soc_add_component_controls(component,
			tpa6140a2_controls, ARRAY_SIZE(tpa6140a2_controls));
	else
		return snd_soc_add_component_controls(component,
			tpa6130a2_controls, ARRAY_SIZE(tpa6130a2_controls));
}

static const struct snd_soc_dapm_widget tpa6130a2_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("LEFTIN"),
	SND_SOC_DAPM_INPUT("RIGHTIN"),
	SND_SOC_DAPM_OUTPUT("HPLEFT"),
	SND_SOC_DAPM_OUTPUT("HPRIGHT"),

	SND_SOC_DAPM_PGA("Left Mute", TPA6130A2_REG_VOL_MUTE,
			 TPA6130A2_HP_EN_L_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right Mute", TPA6130A2_REG_VOL_MUTE,
			 TPA6130A2_HP_EN_R_SHIFT, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Left PGA", TPA6130A2_REG_CONTROL,
			 TPA6130A2_HP_EN_L_SHIFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right PGA", TPA6130A2_REG_CONTROL,
			 TPA6130A2_HP_EN_R_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Power", TPA6130A2_REG_CONTROL,
			    TPA6130A2_SWS_SHIFT, 1, tpa6130a2_power_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route tpa6130a2_dapm_routes[] = {
	{ "Left PGA", NULL, "LEFTIN" },
	{ "Right PGA", NULL, "RIGHTIN" },

	{ "Left Mute", NULL, "Left PGA" },
	{ "Right Mute", NULL, "Right PGA" },

	{ "HPLEFT", NULL, "Left Mute" },
	{ "HPRIGHT", NULL, "Right Mute" },

	{ "Left PGA", NULL, "Power" },
	{ "Right PGA", NULL, "Power" },
};

static const struct snd_soc_component_driver tpa6130a2_component_driver = {
	.name = "tpa6130a2",
	.probe = tpa6130a2_component_probe,
	.dapm_widgets = tpa6130a2_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tpa6130a2_dapm_widgets),
	.dapm_routes = tpa6130a2_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(tpa6130a2_dapm_routes),
};

static const struct reg_default tpa6130a2_reg_defaults[] = {
	{ TPA6130A2_REG_CONTROL, TPA6130A2_SWS },
	{ TPA6130A2_REG_VOL_MUTE, TPA6130A2_MUTE_R | TPA6130A2_MUTE_L },
};

static const struct regmap_config tpa6130a2_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TPA6130A2_REG_VERSION,
	.reg_defaults = tpa6130a2_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tpa6130a2_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static int tpa6130a2_probe(struct i2c_client *client)
{
	struct device *dev;
	struct tpa6130a2_data *data;
	struct device_node *np = client->dev.of_node;
	const char *regulator;
	unsigned int version;
	int ret;

	dev = &client->dev;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	data->regmap = devm_regmap_init_i2c(client, &tpa6130a2_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	if (np) {
		data->power_gpio = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_LOW);
		if (IS_ERR(data->power_gpio)) {
			return dev_err_probe(dev, PTR_ERR(data->power_gpio),
					     "Failed to request power GPIO\n");
		}
		gpiod_set_consumer_name(data->power_gpio, "tpa6130a2 enable");
	} else {
		dev_err(dev, "Platform data not set\n");
		dump_stack();
		return -ENODEV;
	}

	i2c_set_clientdata(client, data);

	data->id = (uintptr_t)i2c_get_match_data(client);

	switch (data->id) {
	default:
		dev_warn(dev, "Unknown TPA model (%d). Assuming 6130A2\n",
			 data->id);
		fallthrough;
	case TPA6130A2:
		regulator = "Vdd";
		break;
	case TPA6140A2:
		regulator = "AVdd";
		break;
	}

	data->supply = devm_regulator_get(dev, regulator);
	if (IS_ERR(data->supply)) {
		ret = PTR_ERR(data->supply);
		dev_err(dev, "Failed to request supply: %d\n", ret);
		return ret;
	}

	ret = tpa6130a2_power(data, true);
	if (ret != 0)
		return ret;


	/* Read version */
	regmap_read(data->regmap, TPA6130A2_REG_VERSION, &version);
	version &= TPA6130A2_VERSION_MASK;
	if ((version != 1) && (version != 2))
		dev_warn(dev, "UNTESTED version detected (%d)\n", version);

	/* Disable the chip */
	ret = tpa6130a2_power(data, false);
	if (ret != 0)
		return ret;

	return devm_snd_soc_register_component(&client->dev,
			&tpa6130a2_component_driver, NULL, 0);
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tpa6130a2_of_match[] = {
	{ .compatible = "ti,tpa6130a2", },
	{ .compatible = "ti,tpa6140a2" },
	{},
};
MODULE_DEVICE_TABLE(of, tpa6130a2_of_match);
#endif

static struct i2c_driver tpa6130a2_i2c_driver = {
	.driver = {
		.name = "tpa6130a2",
		.of_match_table = of_match_ptr(tpa6130a2_of_match),
	},
	.probe = tpa6130a2_probe,
};

module_i2c_driver(tpa6130a2_i2c_driver);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("TPA6130A2 Headphone amplifier driver");
MODULE_LICENSE("GPL");
