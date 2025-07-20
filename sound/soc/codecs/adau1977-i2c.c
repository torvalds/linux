// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADAU1977/ADAU1978/ADAU1979 driver
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "adau1977.h"

static int adau1977_i2c_probe(struct i2c_client *client)
{
	struct regmap_config config;

	config = adau1977_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 8;

	return adau1977_probe(&client->dev,
		devm_regmap_init_i2c(client, &config),
		(uintptr_t)i2c_get_match_data(client), NULL);
}

static const struct i2c_device_id adau1977_i2c_ids[] = {
	{ "adau1977", ADAU1977 },
	{ "adau1978", ADAU1978 },
	{ "adau1979", ADAU1978 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adau1977_i2c_ids);

static struct i2c_driver adau1977_i2c_driver = {
	.driver = {
		.name = "adau1977",
	},
	.probe = adau1977_i2c_probe,
	.id_table = adau1977_i2c_ids,
};
module_i2c_driver(adau1977_i2c_driver);

MODULE_DESCRIPTION("ASoC ADAU1977/ADAU1978/ADAU1979 driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
