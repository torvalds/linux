// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs42l42-i2c.c -- CS42L42 ALSA SoC audio driver for I2C
 *
 * Copyright 2016, 2022 Cirrus Logic, Inc.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "cs42l42.h"

static int cs42l42_i2c_probe(struct i2c_client *i2c_client)
{
	struct device *dev = &i2c_client->dev;
	struct cs42l42_private *cs42l42;
	struct regmap *regmap;
	int ret;

	cs42l42 = devm_kzalloc(dev, sizeof(*cs42l42), GFP_KERNEL);
	if (!cs42l42)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(i2c_client, &cs42l42_regmap);
	if (IS_ERR(regmap))
		return dev_err_probe(&i2c_client->dev, PTR_ERR(regmap),
				     "regmap_init() failed\n");

	cs42l42->devid = CS42L42_CHIP_ID;
	cs42l42->dev = dev;
	cs42l42->regmap = regmap;
	cs42l42->irq = i2c_client->irq;

	ret = cs42l42_common_probe(cs42l42, &cs42l42_soc_component, &cs42l42_dai);
	if (ret)
		return ret;

	return cs42l42_init(cs42l42);
}

static void cs42l42_i2c_remove(struct i2c_client *i2c_client)
{
	struct cs42l42_private *cs42l42 = dev_get_drvdata(&i2c_client->dev);

	cs42l42_common_remove(cs42l42);
}

static int __maybe_unused cs42l42_i2c_resume(struct device *dev)
{
	int ret;

	ret = cs42l42_resume(dev);
	if (ret)
		return ret;

	cs42l42_resume_restore(dev);

	return 0;
}

static const struct dev_pm_ops cs42l42_i2c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cs42l42_suspend, cs42l42_i2c_resume)
};

static const struct of_device_id __maybe_unused cs42l42_of_match[] = {
	{ .compatible = "cirrus,cs42l42", },
	{}
};
MODULE_DEVICE_TABLE(of, cs42l42_of_match);

static const struct acpi_device_id __maybe_unused cs42l42_acpi_match[] = {
	{"10134242", 0,},
	{}
};
MODULE_DEVICE_TABLE(acpi, cs42l42_acpi_match);

static const struct i2c_device_id cs42l42_id[] = {
	{"cs42l42", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, cs42l42_id);

static struct i2c_driver cs42l42_i2c_driver = {
	.driver = {
		.name = "cs42l42",
		.pm = &cs42l42_i2c_pm_ops,
		.of_match_table = of_match_ptr(cs42l42_of_match),
		.acpi_match_table = ACPI_PTR(cs42l42_acpi_match),
		},
	.id_table = cs42l42_id,
	.probe = cs42l42_i2c_probe,
	.remove = cs42l42_i2c_remove,
};

module_i2c_driver(cs42l42_i2c_driver);

MODULE_DESCRIPTION("ASoC CS42L42 I2C driver");
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(SND_SOC_CS42L42_CORE);
