// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for ADAU1361/ADAU1461/ADAU1761/ADAU1961 codec
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "adau1761.h"

static const struct i2c_device_id adau1761_i2c_ids[];

static int adau1761_i2c_probe(struct i2c_client *client)
{
	struct regmap_config config;
	const struct i2c_device_id *id = i2c_match_id(adau1761_i2c_ids, client);

	config = adau1761_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 16;

	return adau1761_probe(&client->dev,
		devm_regmap_init_i2c(client, &config),
		id->driver_data, NULL);
}

static void adau1761_i2c_remove(struct i2c_client *client)
{
	adau17x1_remove(&client->dev);
}

static const struct i2c_device_id adau1761_i2c_ids[] = {
	{ "adau1361", ADAU1361 },
	{ "adau1461", ADAU1761 },
	{ "adau1761", ADAU1761 },
	{ "adau1961", ADAU1361 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adau1761_i2c_ids);

#if defined(CONFIG_OF)
static const struct of_device_id adau1761_i2c_dt_ids[] = {
	{ .compatible = "adi,adau1361", },
	{ .compatible = "adi,adau1461", },
	{ .compatible = "adi,adau1761", },
	{ .compatible = "adi,adau1961", },
	{ },
};
MODULE_DEVICE_TABLE(of, adau1761_i2c_dt_ids);
#endif

static struct i2c_driver adau1761_i2c_driver = {
	.driver = {
		.name = "adau1761",
		.of_match_table = of_match_ptr(adau1761_i2c_dt_ids),
	},
	.probe = adau1761_i2c_probe,
	.remove = adau1761_i2c_remove,
	.id_table = adau1761_i2c_ids,
};
module_i2c_driver(adau1761_i2c_driver);

MODULE_DESCRIPTION("ASoC ADAU1361/ADAU1461/ADAU1761/ADAU1961 CODEC I2C driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
