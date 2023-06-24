// SPDX-License-Identifier: GPL-2.0-only
/*
 * AD1936/AD1937 audio driver
 *
 * Copyright 2014 Analog Devices Inc.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#include "ad193x.h"

static const struct i2c_device_id ad193x_id[] = {
	{ "ad1936", AD193X },
	{ "ad1937", AD193X },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad193x_id);

static int ad193x_i2c_probe(struct i2c_client *client)
{
	struct regmap_config config;
	const struct i2c_device_id *id = i2c_match_id(ad193x_id, client);

	config = ad193x_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 8;

	return ad193x_probe(&client->dev,
			    devm_regmap_init_i2c(client, &config),
			    (enum ad193x_type)id->driver_data);
}

static struct i2c_driver ad193x_i2c_driver = {
	.driver = {
		.name = "ad193x",
	},
	.probe_new = ad193x_i2c_probe,
	.id_table = ad193x_id,
};
module_i2c_driver(ad193x_i2c_driver);

MODULE_DESCRIPTION("ASoC AD1936/AD1937 audio CODEC driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
