// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PCM179X ASoC I2C driver
 *
 * Copyright (c) Teenage Engineering AB 2016
 *
 *     Jacob Siverskog <jacob@teenage.engineering>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include "pcm179x.h"

static int pcm179x_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &pcm179x_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}

	return pcm179x_common_init(&client->dev, regmap);
}

#ifdef CONFIG_OF
static const struct of_device_id pcm179x_of_match[] = {
	{ .compatible = "ti,pcm1792a", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm179x_of_match);
#endif

static const struct i2c_device_id pcm179x_i2c_ids[] = {
	{ "pcm179x", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcm179x_i2c_ids);

static struct i2c_driver pcm179x_i2c_driver = {
	.driver = {
		.name	= "pcm179x",
		.of_match_table = of_match_ptr(pcm179x_of_match),
	},
	.id_table	= pcm179x_i2c_ids,
	.probe_new	= pcm179x_i2c_probe,
};

module_i2c_driver(pcm179x_i2c_driver);

MODULE_DESCRIPTION("ASoC PCM179X I2C driver");
MODULE_AUTHOR("Jacob Siverskog <jacob@teenage.engineering>");
MODULE_LICENSE("GPL");
