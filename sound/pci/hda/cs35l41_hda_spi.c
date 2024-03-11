// SPDX-License-Identifier: GPL-2.0
//
// CS35l41 HDA SPI driver
//
// Copyright 2021 Cirrus Logic, Inc.
//
// Author: Lucas Tanure <tanureal@opensource.cirrus.com>

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "cs35l41_hda.h"

static int cs35l41_hda_spi_probe(struct spi_device *spi)
{
	const char *device_name;

	/*
	 * Compare against the device name so it works for SPI, normal ACPI
	 * and for ACPI by serial-multi-instantiate matching cases.
	 */
	if (strstr(dev_name(&spi->dev), "CSC3551"))
		device_name = "CSC3551";
	else
		return -ENODEV;

	return cs35l41_hda_probe(&spi->dev, device_name, spi_get_chipselect(spi, 0), spi->irq,
				 devm_regmap_init_spi(spi, &cs35l41_regmap_spi), SPI);
}

static void cs35l41_hda_spi_remove(struct spi_device *spi)
{
	cs35l41_hda_remove(&spi->dev);
}

static const struct spi_device_id cs35l41_hda_spi_id[] = {
	{ "cs35l41-hda", 0 },
	{}
};

static const struct acpi_device_id cs35l41_acpi_hda_match[] = {
	{ "CSC3551", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, cs35l41_acpi_hda_match);

static struct spi_driver cs35l41_spi_driver = {
	.driver = {
		.name		= "cs35l41-hda",
		.acpi_match_table = cs35l41_acpi_hda_match,
		.pm		= &cs35l41_hda_pm_ops,
	},
	.id_table	= cs35l41_hda_spi_id,
	.probe		= cs35l41_hda_spi_probe,
	.remove		= cs35l41_hda_spi_remove,
};
module_spi_driver(cs35l41_spi_driver);

MODULE_DESCRIPTION("HDA CS35L41 driver");
MODULE_IMPORT_NS(SND_HDA_SCODEC_CS35L41);
MODULE_AUTHOR("Lucas Tanure <tanureal@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
