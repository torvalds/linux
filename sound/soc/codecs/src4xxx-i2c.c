// SPDX-License-Identifier: GPL-2.0
//
// Driver for SRC4XXX codecs
//
// Copyright 2021-2022 Deqx Pty Ltd
// Author: Matt Flax <flatmax@flatmax.com>

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "src4xxx.h"

static int src4xxx_i2c_probe(struct i2c_client *i2c,
			const struct i2c_device_id *id)
{
	return src4xxx_probe(&i2c->dev,
		devm_regmap_init_i2c(i2c, &src4xxx_regmap_config), NULL);
}

static const struct i2c_device_id src4xxx_i2c_ids[] = {
	{ "src4392", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, src4xxx_i2c_ids);

static const struct of_device_id src4xxx_of_match[] = {
	{ .compatible = "ti,src4392", },
	{ }
};
MODULE_DEVICE_TABLE(of, src4xxx_of_match);


static struct i2c_driver src4xxx_i2c_driver = {
	.driver = {
		.name = "src4xxx",
		.of_match_table = of_match_ptr(src4xxx_of_match),
	},
	.probe = src4xxx_i2c_probe,
	.id_table = src4xxx_i2c_ids,
};
module_i2c_driver(src4xxx_i2c_driver);

MODULE_DESCRIPTION("ASoC SRC4392 CODEC I2C driver");
MODULE_AUTHOR("Matt Flax <flatmax@flatmax.com>");
MODULE_LICENSE("GPL");
