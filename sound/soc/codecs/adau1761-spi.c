// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for ADAU1361/ADAU1461/ADAU1761/ADAU1961 codec
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <sound/soc.h>

#include "adau1761.h"

static void adau1761_spi_switch_mode(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);

	/*
	 * To get the device into SPI mode CLATCH has to be pulled low three
	 * times.  Do this by issuing three dummy reads.
	 */
	spi_w8r8(spi, 0x00);
	spi_w8r8(spi, 0x00);
	spi_w8r8(spi, 0x00);
}

static int adau1761_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap_config config;

	if (!id)
		return -EINVAL;

	config = adau1761_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 24;
	config.read_flag_mask = 0x1;

	return adau1761_probe(&spi->dev,
		devm_regmap_init_spi(spi, &config),
		id->driver_data, adau1761_spi_switch_mode);
}

static void adau1761_spi_remove(struct spi_device *spi)
{
	adau17x1_remove(&spi->dev);
}

static const struct spi_device_id adau1761_spi_id[] = {
	{ "adau1361", ADAU1361 },
	{ "adau1461", ADAU1761 },
	{ "adau1761", ADAU1761 },
	{ "adau1961", ADAU1361 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adau1761_spi_id);

#if defined(CONFIG_OF)
static const struct of_device_id adau1761_spi_dt_ids[] = {
	{ .compatible = "adi,adau1361", },
	{ .compatible = "adi,adau1461", },
	{ .compatible = "adi,adau1761", },
	{ .compatible = "adi,adau1961", },
	{ },
};
MODULE_DEVICE_TABLE(of, adau1761_spi_dt_ids);
#endif

static struct spi_driver adau1761_spi_driver = {
	.driver = {
		.name = "adau1761",
		.of_match_table = of_match_ptr(adau1761_spi_dt_ids),
	},
	.probe = adau1761_spi_probe,
	.remove = adau1761_spi_remove,
	.id_table = adau1761_spi_id,
};
module_spi_driver(adau1761_spi_driver);

MODULE_DESCRIPTION("ASoC ADAU1361/ADAU1461/ADAU1761/ADAU1961 CODEC SPI driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
