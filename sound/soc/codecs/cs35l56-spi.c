// SPDX-License-Identifier: GPL-2.0-only
//
// CS35L56 ALSA SoC audio driver SPI binding
//
// Copyright (C) 2023 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#include "cs35l56.h"

static int cs35l56_spi_probe(struct spi_device *spi)
{
	const struct regmap_config *regmap_config = &cs35l56_regmap_spi;
	struct cs35l56_private *cs35l56;
	int ret;

	cs35l56 = devm_kzalloc(&spi->dev, sizeof(struct cs35l56_private), GFP_KERNEL);
	if (!cs35l56)
		return -ENOMEM;

	spi_set_drvdata(spi, cs35l56);
	cs35l56->regmap = devm_regmap_init_spi(spi, regmap_config);
	if (IS_ERR(cs35l56->regmap)) {
		ret = PTR_ERR(cs35l56->regmap);
		return dev_err_probe(&spi->dev, ret, "Failed to allocate register map\n");
	}

	cs35l56->dev = &spi->dev;

	ret = cs35l56_common_probe(cs35l56);
	if (ret != 0)
		return ret;

	ret = cs35l56_init(cs35l56);
	if (ret == 0)
		ret = cs35l56_irq_request(cs35l56, spi->irq);
	if (ret < 0)
		cs35l56_remove(cs35l56);

	return ret;
}

static void cs35l56_spi_remove(struct spi_device *spi)
{
	struct cs35l56_private *cs35l56 = spi_get_drvdata(spi);

	cs35l56_remove(cs35l56);
}

static const struct spi_device_id cs35l56_id_spi[] = {
	{ "cs35l56", 0 },
	{}
};
MODULE_DEVICE_TABLE(spi, cs35l56_id_spi);

static struct spi_driver cs35l56_spi_driver = {
	.driver = {
		.name		= "cs35l56",
		.pm = &cs35l56_pm_ops_i2c_spi,
	},
	.id_table	= cs35l56_id_spi,
	.probe		= cs35l56_spi_probe,
	.remove		= cs35l56_spi_remove,
};

module_spi_driver(cs35l56_spi_driver);

MODULE_DESCRIPTION("ASoC CS35L56 SPI driver");
MODULE_IMPORT_NS(SND_SOC_CS35L56_CORE);
MODULE_IMPORT_NS(SND_SOC_CS35L56_SHARED);
MODULE_AUTHOR("Richard Fitzgerald <rf@opensource.cirrus.com>");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
