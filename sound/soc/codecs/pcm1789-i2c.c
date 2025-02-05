// SPDX-License-Identifier: GPL-2.0
// Audio driver for PCM1789 I2C
// Copyright (C) 2018 Bootlin
// Mylène Josserand <mylene.josserand@bootlin.com>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include "pcm1789.h"

static int pcm1789_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &pcm1789_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	return pcm1789_common_init(&client->dev, regmap);
}

static void pcm1789_i2c_remove(struct i2c_client *client)
{
	pcm1789_common_exit(&client->dev);
}

#ifdef CONFIG_OF
static const struct of_device_id pcm1789_of_match[] = {
	{ .compatible = "ti,pcm1789", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm1789_of_match);
#endif

static const struct i2c_device_id pcm1789_i2c_ids[] = {
	{ "pcm1789" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcm1789_i2c_ids);

static struct i2c_driver pcm1789_i2c_driver = {
	.driver = {
		.name	= "pcm1789",
		.of_match_table = of_match_ptr(pcm1789_of_match),
	},
	.id_table	= pcm1789_i2c_ids,
	.probe		= pcm1789_i2c_probe,
	.remove	= pcm1789_i2c_remove,
};

module_i2c_driver(pcm1789_i2c_driver);

MODULE_DESCRIPTION("ASoC PCM1789 I2C driver");
MODULE_AUTHOR("Mylène Josserand <mylene.josserand@bootlin.com>");
MODULE_LICENSE("GPL");
