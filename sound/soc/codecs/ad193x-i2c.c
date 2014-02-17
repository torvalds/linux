/*
 * AD1936/AD1937 audio driver
 *
 * Copyright 2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#include "ad193x.h"

static const struct i2c_device_id ad193x_id[] = {
	{ "ad1936", 0 },
	{ "ad1937", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad193x_id);

static int ad193x_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct regmap_config config;

	config = ad193x_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 8;

	return ad193x_probe(&client->dev, devm_regmap_init_i2c(client, &config));
}

static int ad193x_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static struct i2c_driver ad193x_i2c_driver = {
	.driver = {
		.name = "ad193x",
	},
	.probe    = ad193x_i2c_probe,
	.remove   = ad193x_i2c_remove,
	.id_table = ad193x_id,
};
module_i2c_driver(ad193x_i2c_driver);

MODULE_DESCRIPTION("ASoC AD1936/AD1937 audio CODEC driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
