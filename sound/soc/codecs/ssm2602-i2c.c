// SPDX-License-Identifier: GPL-2.0-only
/*
 * SSM2602/SSM2603/SSM2604 I2C audio driver
 *
 * Copyright 2014 Analog Devices Inc.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#include "ssm2602.h"

static const struct i2c_device_id ssm2602_i2c_id[];

/*
 * ssm2602 2 wire address is determined by GPIO5
 * state during powerup.
 *    low  = 0x1a
 *    high = 0x1b
 */
static int ssm2602_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_match_id(ssm2602_i2c_id, client);
	return ssm2602_probe(&client->dev, id->driver_data,
		devm_regmap_init_i2c(client, &ssm2602_regmap_config));
}

static const struct i2c_device_id ssm2602_i2c_id[] = {
	{ "ssm2602", SSM2602 },
	{ "ssm2603", SSM2602 },
	{ "ssm2604", SSM2604 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssm2602_i2c_id);

static const struct of_device_id ssm2602_of_match[] = {
	{ .compatible = "adi,ssm2602", },
	{ .compatible = "adi,ssm2603", },
	{ .compatible = "adi,ssm2604", },
	{ }
};
MODULE_DEVICE_TABLE(of, ssm2602_of_match);

static struct i2c_driver ssm2602_i2c_driver = {
	.driver = {
		.name = "ssm2602",
		.of_match_table = ssm2602_of_match,
	},
	.probe_new = ssm2602_i2c_probe,
	.id_table = ssm2602_i2c_id,
};
module_i2c_driver(ssm2602_i2c_driver);

MODULE_DESCRIPTION("ASoC SSM2602/SSM2603/SSM2604 I2C driver");
MODULE_AUTHOR("Cliff Cai");
MODULE_LICENSE("GPL");
