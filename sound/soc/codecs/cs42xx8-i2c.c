/*
 * Cirrus Logic CS42448/CS42888 Audio CODEC DAI I2C driver
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Author: Nicolin Chen <Guangyu.Chen@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>

#include "cs42xx8.h"

static const struct of_device_id cs42xx8_of_match[];

static int cs42xx8_i2c_probe(struct i2c_client *i2c)
{
	int ret;
	struct cs42xx8_driver_data *drvdata;
	const struct of_device_id *of_id;

	of_id = of_match_device(cs42xx8_of_match, &i2c->dev);
	if (!of_id) {
		dev_err(&i2c->dev, "failed to find driver data\n");
		return -EINVAL;
	}

	drvdata = (struct cs42xx8_driver_data *)of_id->data;

	ret = cs42xx8_probe(&i2c->dev,
		devm_regmap_init_i2c(i2c, &cs42xx8_regmap_config), drvdata);
	if (ret)
		return ret;

	pm_runtime_enable(&i2c->dev);
	pm_request_idle(&i2c->dev);

	return 0;
}

static void cs42xx8_i2c_remove(struct i2c_client *i2c)
{
	pm_runtime_disable(&i2c->dev);
}

static const struct of_device_id cs42xx8_of_match[] = {
	{ .compatible = "cirrus,cs42448", .data = &cs42448_data, },
	{ .compatible = "cirrus,cs42888", .data = &cs42888_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cs42xx8_of_match);

static const struct i2c_device_id cs42xx8_i2c_id[] = {
	{"cs42448", (kernel_ulong_t)&cs42448_data},
	{"cs42888", (kernel_ulong_t)&cs42888_data},
	{}
};
MODULE_DEVICE_TABLE(i2c, cs42xx8_i2c_id);

static struct i2c_driver cs42xx8_i2c_driver = {
	.driver = {
		.name = "cs42xx8",
		.pm = &cs42xx8_pm,
		.of_match_table = cs42xx8_of_match,
	},
	.probe_new = cs42xx8_i2c_probe,
	.remove = cs42xx8_i2c_remove,
	.id_table = cs42xx8_i2c_id,
};

module_i2c_driver(cs42xx8_i2c_driver);

MODULE_DESCRIPTION("Cirrus Logic CS42448/CS42888 ALSA SoC Codec I2C Driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
