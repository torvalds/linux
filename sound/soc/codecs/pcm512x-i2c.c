// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the PCM512x CODECs
 *
 * Author:	Mark Brown <broonie@kernel.org>
 *		Copyright 2014 Linaro Ltd
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/acpi.h>

#include "pcm512x.h"

static int pcm512x_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct regmap *regmap;
	struct regmap_config config = pcm512x_regmap;

	/* msb needs to be set to enable auto-increment of addresses */
	config.read_flag_mask = 0x80;
	config.write_flag_mask = 0x80;

	regmap = devm_regmap_init_i2c(i2c, &config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return pcm512x_probe(&i2c->dev, regmap);
}

static int pcm512x_i2c_remove(struct i2c_client *i2c)
{
	pcm512x_remove(&i2c->dev);
	return 0;
}

static const struct i2c_device_id pcm512x_i2c_id[] = {
	{ "pcm5121", },
	{ "pcm5122", },
	{ "pcm5141", },
	{ "pcm5142", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcm512x_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id pcm512x_of_match[] = {
	{ .compatible = "ti,pcm5121", },
	{ .compatible = "ti,pcm5122", },
	{ .compatible = "ti,pcm5141", },
	{ .compatible = "ti,pcm5142", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm512x_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id pcm512x_acpi_match[] = {
	{ "104C5121", 0 },
	{ "104C5122", 0 },
	{ "104C5141", 0 },
	{ "104C5142", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, pcm512x_acpi_match);
#endif

static struct i2c_driver pcm512x_i2c_driver = {
	.probe 		= pcm512x_i2c_probe,
	.remove 	= pcm512x_i2c_remove,
	.id_table	= pcm512x_i2c_id,
	.driver		= {
		.name	= "pcm512x",
		.of_match_table = of_match_ptr(pcm512x_of_match),
		.acpi_match_table = ACPI_PTR(pcm512x_acpi_match),
		.pm     = &pcm512x_pm_ops,
	},
};

module_i2c_driver(pcm512x_i2c_driver);

MODULE_DESCRIPTION("ASoC PCM512x codec driver - I2C");
MODULE_AUTHOR("Mark Brown <broonie@kernel.org>");
MODULE_LICENSE("GPL v2");
