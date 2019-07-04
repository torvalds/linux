// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8804-i2c.c  --  WM8804 S/PDIF transceiver driver - I2C
 *
 * Copyright 2015 Cirrus Logic Inc
 *
 * Author: Charles Keepax <ckeepax@opensource.wolfsonmicro.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

#include "wm8804.h"

static int wm8804_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(i2c, &wm8804_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return wm8804_probe(&i2c->dev, regmap);
}

static int wm8804_i2c_remove(struct i2c_client *i2c)
{
	wm8804_remove(&i2c->dev);
	return 0;
}

static const struct i2c_device_id wm8804_i2c_id[] = {
	{ "wm8804", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8804_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id wm8804_of_match[] = {
	{ .compatible = "wlf,wm8804", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8804_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id wm8804_acpi_match[] = {
	{ "1AEC8804", 0 }, /* Wolfson PCI ID + part ID */
	{ "10138804", 0 }, /* Cirrus Logic PCI ID + part ID */
	{ },
};
MODULE_DEVICE_TABLE(acpi, wm8804_acpi_match);
#endif

static struct i2c_driver wm8804_i2c_driver = {
	.driver = {
		.name = "wm8804",
		.pm = &wm8804_pm,
		.of_match_table = of_match_ptr(wm8804_of_match),
		.acpi_match_table = ACPI_PTR(wm8804_acpi_match),
	},
	.probe = wm8804_i2c_probe,
	.remove = wm8804_i2c_remove,
	.id_table = wm8804_i2c_id
};

module_i2c_driver(wm8804_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8804 driver - I2C");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
