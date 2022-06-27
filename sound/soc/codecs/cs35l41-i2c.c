// SPDX-License-Identifier: GPL-2.0
//
// cs35l41-i2c.c -- CS35l41 I2C driver
//
// Copyright 2017-2021 Cirrus Logic, Inc.
//
// Author: David Rhodes <david.rhodes@cirrus.com>

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "cs35l41.h"

static const struct i2c_device_id cs35l41_id_i2c[] = {
	{ "cs35l40", 0 },
	{ "cs35l41", 0 },
	{ "cs35l51", 0 },
	{ "cs35l53", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, cs35l41_id_i2c);

static int cs35l41_i2c_probe(struct i2c_client *client)
{
	struct cs35l41_private *cs35l41;
	struct device *dev = &client->dev;
	struct cs35l41_hw_cfg *hw_cfg = dev_get_platdata(dev);
	const struct regmap_config *regmap_config = &cs35l41_regmap_i2c;
	int ret;

	cs35l41 = devm_kzalloc(dev, sizeof(struct cs35l41_private), GFP_KERNEL);

	if (!cs35l41)
		return -ENOMEM;

	cs35l41->dev = dev;
	cs35l41->irq = client->irq;

	i2c_set_clientdata(client, cs35l41);
	cs35l41->regmap = devm_regmap_init_i2c(client, regmap_config);
	if (IS_ERR(cs35l41->regmap)) {
		ret = PTR_ERR(cs35l41->regmap);
		dev_err(cs35l41->dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	return cs35l41_probe(cs35l41, hw_cfg);
}

static int cs35l41_i2c_remove(struct i2c_client *client)
{
	struct cs35l41_private *cs35l41 = i2c_get_clientdata(client);

	cs35l41_remove(cs35l41);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id cs35l41_of_match[] = {
	{ .compatible = "cirrus,cs35l40" },
	{ .compatible = "cirrus,cs35l41" },
	{},
};
MODULE_DEVICE_TABLE(of, cs35l41_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id cs35l41_acpi_match[] = {
	{ "CSC3541", 0 }, /* Cirrus Logic PnP ID + part ID */
	{},
};
MODULE_DEVICE_TABLE(acpi, cs35l41_acpi_match);
#endif

static struct i2c_driver cs35l41_i2c_driver = {
	.driver = {
		.name		= "cs35l41",
		.pm		= &cs35l41_pm_ops,
		.of_match_table = of_match_ptr(cs35l41_of_match),
		.acpi_match_table = ACPI_PTR(cs35l41_acpi_match),
	},
	.id_table	= cs35l41_id_i2c,
	.probe_new	= cs35l41_i2c_probe,
	.remove		= cs35l41_i2c_remove,
};

module_i2c_driver(cs35l41_i2c_driver);

MODULE_DESCRIPTION("I2C CS35L41 driver");
MODULE_AUTHOR("David Rhodes, Cirrus Logic Inc, <david.rhodes@cirrus.com>");
MODULE_LICENSE("GPL");
