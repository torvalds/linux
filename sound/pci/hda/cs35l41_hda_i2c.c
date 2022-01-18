// SPDX-License-Identifier: GPL-2.0
//
// cs35l41.c -- CS35l41 HDA I2C driver
//
// Copyright 2021 Cirrus Logic, Inc.
//
// Author: Lucas Tanure <tanureal@opensource.cirrus.com>

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

#include "cs35l41_hda.h"

static int cs35l41_hda_i2c_probe(struct i2c_client *clt, const struct i2c_device_id *id)
{
	const char *device_name;

	/* Compare against the device name so it works for I2C, normal ACPI
	 * and for ACPI by i2c-multi-instantiate matching cases
	 */
	if (strstr(dev_name(&clt->dev), "CLSA0100"))
		device_name = "CLSA0100";
	else if (strstr(dev_name(&clt->dev), "CSC3551"))
		device_name = "CSC3551";
	else
		return -ENODEV;

	return cs35l41_hda_probe(&clt->dev, device_name, clt->addr, clt->irq,
				 devm_regmap_init_i2c(clt, &cs35l41_regmap_i2c));
}

static int cs35l41_hda_i2c_remove(struct i2c_client *clt)
{
	cs35l41_hda_remove(&clt->dev);

	return 0;
}

static const struct i2c_device_id cs35l41_hda_i2c_id[] = {
	{ "cs35l41-hda", 0 },
	{}
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id cs35l41_acpi_hda_match[] = {
	{"CLSA0100", 0 },
	{"CSC3551", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, cs35l41_acpi_hda_match);
#endif

static struct i2c_driver cs35l41_i2c_driver = {
	.driver = {
		.name		= "cs35l41-hda",
		.acpi_match_table = ACPI_PTR(cs35l41_acpi_hda_match),
	},
	.id_table	= cs35l41_hda_i2c_id,
	.probe		= cs35l41_hda_i2c_probe,
	.remove		= cs35l41_hda_i2c_remove,
};
module_i2c_driver(cs35l41_i2c_driver);

MODULE_DESCRIPTION("HDA CS35L41 driver");
MODULE_IMPORT_NS(SND_HDA_SCODEC_CS35L41);
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
