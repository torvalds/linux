// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for ADAU1372 codec
 *
 * Copyright 2016 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "adau1372.h"

static int adau1372_i2c_probe(struct i2c_client *client)
{
	return adau1372_probe(&client->dev,
		devm_regmap_init_i2c(client, &adau1372_regmap_config), NULL);
}

static const struct i2c_device_id adau1372_i2c_ids[] = {
	{ "adau1372", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adau1372_i2c_ids);

static struct i2c_driver adau1372_i2c_driver = {
	.driver = {
		.name = "adau1372",
	},
	.probe = adau1372_i2c_probe,
	.id_table = adau1372_i2c_ids,
};
module_i2c_driver(adau1372_i2c_driver);

MODULE_DESCRIPTION("ASoC ADAU1372 CODEC I2C driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL v2");
