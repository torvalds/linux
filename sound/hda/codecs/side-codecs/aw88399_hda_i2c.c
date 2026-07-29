// SPDX-License-Identifier: GPL-2.0-only
//
// AW88399 HDA I2C driver
//
// Based on cs35l41_hda_i2c.c
//

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "aw88399_hda.h"

static int aw88399_hda_i2c_probe(struct i2c_client *clt)
{
	if (!strstr(dev_name(&clt->dev), "AWDZ8399"))
		return -ENODEV;

	return aw88399_hda_probe(&clt->dev,
				 devm_regmap_init_i2c(clt, &aw88399_remap_config));
}

static void aw88399_hda_i2c_remove(struct i2c_client *clt)
{
	aw88399_hda_remove(&clt->dev);
}

static const struct i2c_device_id aw88399_hda_i2c_id[] = {
	{ .name = "aw88399-hda" },
	{ }
};

static const struct acpi_device_id aw88399_acpi_hda_match[] = {
	{ "AWDZ8399", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, aw88399_acpi_hda_match);

static struct i2c_driver aw88399_hda_i2c_driver = {
	.driver = {
		.name		= "aw88399-hda",
		.acpi_match_table = aw88399_acpi_hda_match,
		.pm		= &aw88399_hda_pm_ops,
	},
	.probe		= aw88399_hda_i2c_probe,
	.remove		= aw88399_hda_i2c_remove,
	.id_table	= aw88399_hda_i2c_id,
};
module_i2c_driver(aw88399_hda_i2c_driver);

MODULE_DESCRIPTION("HDA AW88399 I2C driver");
MODULE_IMPORT_NS("SND_HDA_SCODEC_AW88399");
MODULE_AUTHOR("Yakov Till <yakov.till@gmail.com>");
MODULE_LICENSE("GPL");
