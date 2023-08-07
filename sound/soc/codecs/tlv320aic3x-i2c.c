/* SPDX-License-Identifier: GPL-2.0-only
 *
 * ALSA SoC TLV320AIC3x codec driver I2C interface
 *
 * Author:      Arun KS, <arunks@mistralsolutions.com>
 * Copyright:   (C) 2008 Mistral Solutions Pvt Ltd.,
 *
 * Based on sound/soc/codecs/wm8731.c by Richard Purdie
 *
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "tlv320aic3x.h"

static const struct i2c_device_id aic3x_i2c_id[] = {
	{ "tlv320aic3x", AIC3X_MODEL_3X },
	{ "tlv320aic33", AIC3X_MODEL_33 },
	{ "tlv320aic3007", AIC3X_MODEL_3007 },
	{ "tlv320aic3104", AIC3X_MODEL_3104 },
	{ "tlv320aic3106", AIC3X_MODEL_3106 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aic3x_i2c_id);

static int aic3x_i2c_probe(struct i2c_client *i2c)
{
	struct regmap *regmap;
	struct regmap_config config;
	const struct i2c_device_id *id = i2c_match_id(aic3x_i2c_id, i2c);

	config = aic3x_regmap;
	config.reg_bits = 8;
	config.val_bits = 8;

	regmap = devm_regmap_init_i2c(i2c, &config);
	return aic3x_probe(&i2c->dev, regmap, id->driver_data);
}

static void aic3x_i2c_remove(struct i2c_client *i2c)
{
	aic3x_remove(&i2c->dev);
}

static const struct of_device_id aic3x_of_id[] = {
	{ .compatible = "ti,tlv320aic3x", },
	{ .compatible = "ti,tlv320aic33" },
	{ .compatible = "ti,tlv320aic3007" },
	{ .compatible = "ti,tlv320aic3104" },
	{ .compatible = "ti,tlv320aic3106" },
	{},
};
MODULE_DEVICE_TABLE(of, aic3x_of_id);

static struct i2c_driver aic3x_i2c_driver = {
	.driver = {
		.name = "tlv320aic3x",
		.of_match_table = aic3x_of_id,
	},
	.probe = aic3x_i2c_probe,
	.remove = aic3x_i2c_remove,
	.id_table = aic3x_i2c_id,
};

module_i2c_driver(aic3x_i2c_driver);

MODULE_DESCRIPTION("ASoC TLV320AIC3x codec driver I2C");
MODULE_AUTHOR("Arun KS <arunks@mistralsolutions.com>");
MODULE_LICENSE("GPL");
