// SPDX-License-Identifier: GPL-2.0-only
//
// CS35L56 ALSA SoC audio driver I2C binding
//
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "cs35l56.h"

static int cs35l56_i2c_probe(struct i2c_client *client)
{
	struct cs35l56_private *cs35l56;
	struct device *dev = &client->dev;
	const struct regmap_config *regmap_config = &cs35l56_regmap_i2c;
	int ret;

	cs35l56 = devm_kzalloc(dev, sizeof(struct cs35l56_private), GFP_KERNEL);
	if (!cs35l56)
		return -ENOMEM;

	cs35l56->base.dev = dev;
	cs35l56->base.can_hibernate = true;

	i2c_set_clientdata(client, cs35l56);
	cs35l56->base.regmap = devm_regmap_init_i2c(client, regmap_config);
	if (IS_ERR(cs35l56->base.regmap)) {
		ret = PTR_ERR(cs35l56->base.regmap);
		return dev_err_probe(cs35l56->base.dev, ret, "Failed to allocate register map\n");
	}

	ret = cs35l56_common_probe(cs35l56);
	if (ret != 0)
		return ret;

	ret = cs35l56_init(cs35l56);
	if (ret == 0)
		ret = cs35l56_irq_request(&cs35l56->base, client->irq);
	if (ret < 0)
		cs35l56_remove(cs35l56);

	return ret;
}

static void cs35l56_i2c_remove(struct i2c_client *client)
{
	struct cs35l56_private *cs35l56 = i2c_get_clientdata(client);

	cs35l56_remove(cs35l56);
}

static const struct i2c_device_id cs35l56_id_i2c[] = {
	{ "cs35l56" },
	{}
};
MODULE_DEVICE_TABLE(i2c, cs35l56_id_i2c);

#ifdef CONFIG_ACPI
static const struct acpi_device_id cs35l56_asoc_acpi_match[] = {
	{ "CSC355C", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, cs35l56_asoc_acpi_match);
#endif

static struct i2c_driver cs35l56_i2c_driver = {
	.driver = {
		.name		= "cs35l56",
		.pm = pm_ptr(&cs35l56_pm_ops_i2c_spi),
		.acpi_match_table = ACPI_PTR(cs35l56_asoc_acpi_match),
	},
	.id_table	= cs35l56_id_i2c,
	.probe		= cs35l56_i2c_probe,
	.remove		= cs35l56_i2c_remove,
};

module_i2c_driver(cs35l56_i2c_driver);

MODULE_DESCRIPTION("ASoC CS35L56 I2C driver");
MODULE_IMPORT_NS(SND_SOC_CS35L56_CORE);
MODULE_IMPORT_NS(SND_SOC_CS35L56_SHARED);
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
