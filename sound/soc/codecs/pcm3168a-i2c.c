// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCM3168A codec i2c driver
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>

#include <sound/soc.h>

#include "pcm3168a.h"

static int pcm3168a_i2c_probe(struct i2c_client *i2c)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(i2c, &pcm3168a_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return pcm3168a_probe(&i2c->dev, regmap);
}

static int pcm3168a_i2c_remove(struct i2c_client *i2c)
{
	pcm3168a_remove(&i2c->dev);

	return 0;
}

static const struct i2c_device_id pcm3168a_i2c_id[] = {
	{ "pcm3168a", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcm3168a_i2c_id);

static const struct of_device_id pcm3168a_of_match[] = {
	{ .compatible = "ti,pcm3168a", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm3168a_of_match);

static struct i2c_driver pcm3168a_i2c_driver = {
	.probe_new	= pcm3168a_i2c_probe,
	.remove		= pcm3168a_i2c_remove,
	.id_table	= pcm3168a_i2c_id,
	.driver		= {
		.name	= "pcm3168a",
		.of_match_table = pcm3168a_of_match,
		.pm		= &pcm3168a_pm_ops,
	},
};
module_i2c_driver(pcm3168a_i2c_driver);

MODULE_DESCRIPTION("PCM3168A I2C codec driver");
MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_LICENSE("GPL v2");
