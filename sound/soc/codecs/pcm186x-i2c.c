// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments PCM186x Universal Audio ADC - I2C
 *
 * Copyright (C) 2015-2017 Texas Instruments Incorporated - https://www.ti.com
 *	Andreas Dannenberg <dannenberg@ti.com>
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>

#include "pcm186x.h"

static const struct of_device_id pcm186x_of_match[] = {
	{ .compatible = "ti,pcm1862", .data = (void *)PCM1862 },
	{ .compatible = "ti,pcm1863", .data = (void *)PCM1863 },
	{ .compatible = "ti,pcm1864", .data = (void *)PCM1864 },
	{ .compatible = "ti,pcm1865", .data = (void *)PCM1865 },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm186x_of_match);

static const struct i2c_device_id pcm186x_i2c_id[] = {
	{ "pcm1862", PCM1862 },
	{ "pcm1863", PCM1863 },
	{ "pcm1864", PCM1864 },
	{ "pcm1865", PCM1865 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcm186x_i2c_id);

static int pcm186x_i2c_probe(struct i2c_client *i2c)
{
	const enum pcm186x_type type = (uintptr_t)i2c_get_match_data(i2c);
	int irq = i2c->irq;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(i2c, &pcm186x_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return pcm186x_probe(&i2c->dev, type, irq, regmap);
}

static struct i2c_driver pcm186x_i2c_driver = {
	.probe		= pcm186x_i2c_probe,
	.id_table	= pcm186x_i2c_id,
	.driver		= {
		.name	= "pcm186x",
		.of_match_table = pcm186x_of_match,
	},
};
module_i2c_driver(pcm186x_i2c_driver);

MODULE_AUTHOR("Andreas Dannenberg <dannenberg@ti.com>");
MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("PCM186x Universal Audio ADC I2C Interface Driver");
MODULE_LICENSE("GPL v2");
