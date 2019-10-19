// SPDX-License-Identifier: GPL-2.0
//
// PCM3060 I2C driver
//
// Copyright (C) 2018 Kirill Marinushkin <kmarinushkin@birdec.com>

#include <linux/i2c.h>
#include <linux/module.h>
#include <sound/soc.h>

#include "pcm3060.h"

static int pcm3060_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct pcm3060_priv *priv;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	i2c_set_clientdata(i2c, priv);

	priv->regmap = devm_regmap_init_i2c(i2c, &pcm3060_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	return pcm3060_probe(&i2c->dev);
}

static const struct i2c_device_id pcm3060_i2c_id[] = {
	{ .name = "pcm3060" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, pcm3060_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id pcm3060_of_match[] = {
	{ .compatible = "ti,pcm3060" },
	{ },
};
MODULE_DEVICE_TABLE(of, pcm3060_of_match);
#endif /* CONFIG_OF */

static struct i2c_driver pcm3060_i2c_driver = {
	.driver = {
		.name = "pcm3060",
#ifdef CONFIG_OF
		.of_match_table = pcm3060_of_match,
#endif /* CONFIG_OF */
	},
	.id_table = pcm3060_i2c_id,
	.probe = pcm3060_i2c_probe,
};

module_i2c_driver(pcm3060_i2c_driver);

MODULE_DESCRIPTION("PCM3060 I2C driver");
MODULE_AUTHOR("Kirill Marinushkin <kmarinushkin@birdec.com>");
MODULE_LICENSE("GPL v2");
