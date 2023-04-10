// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * CS4271 I2C audio driver
 *
 * Copyright (c) 2010 Alexander Sverdlin <subaparts@yandex.ru>
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "cs4271.h"

static int cs4271_i2c_probe(struct i2c_client *client)
{
	struct regmap_config config;

	config = cs4271_regmap_config;
	config.reg_bits = 8;

	return cs4271_probe(&client->dev,
			    devm_regmap_init_i2c(client, &config));
}

static const struct i2c_device_id cs4271_i2c_id[] = {
	{ "cs4271", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs4271_i2c_id);

static struct i2c_driver cs4271_i2c_driver = {
	.driver = {
		.name = "cs4271",
		.of_match_table = of_match_ptr(cs4271_dt_ids),
	},
	.probe_new = cs4271_i2c_probe,
	.id_table = cs4271_i2c_id,
};
module_i2c_driver(cs4271_i2c_driver);

MODULE_DESCRIPTION("ASoC CS4271 I2C Driver");
MODULE_AUTHOR("Alexander Sverdlin <subaparts@yandex.ru>");
MODULE_LICENSE("GPL");
