/*
 * ADAV803 audio driver
 *
 * Copyright 2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#include "adav80x.h"

static const struct i2c_device_id adav803_id[] = {
	{ "adav803", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adav803_id);

static int adav803_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	return adav80x_bus_probe(&client->dev,
		devm_regmap_init_i2c(client, &adav80x_regmap_config));
}

static int adav803_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static struct i2c_driver adav803_driver = {
	.driver = {
		.name = "adav803",
	},
	.probe = adav803_probe,
	.remove = adav803_remove,
	.id_table = adav803_id,
};
module_i2c_driver(adav803_driver);

MODULE_DESCRIPTION("ASoC ADAV803 driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_AUTHOR("Yi Li <yi.li@analog.com>>");
MODULE_LICENSE("GPL");
