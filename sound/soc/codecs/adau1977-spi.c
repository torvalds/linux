// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADAU1977/ADAU1978/ADAU1979 driver
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>
#include <sound/soc.h>

#include "adau1977.h"

static void adau1977_spi_switch_mode(struct device *dev)
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

static int adau1977_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap_config config;

	if (!id)
		return -EINVAL;

	config = adau1977_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 16;
	config.read_flag_mask = 0x1;

	return adau1977_probe(&spi->dev,
		devm_regmap_init_spi(spi, &config),
		id->driver_data, adau1977_spi_switch_mode);
}

static const struct spi_device_id adau1977_spi_ids[] = {
	{ "adau1977", ADAU1977 },
	{ "adau1978", ADAU1978 },
	{ "adau1979", ADAU1978 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adau1977_spi_ids);

static const struct of_device_id adau1977_spi_of_match[] = {
        { .compatible = "adi,adau1977" },
        { .compatible = "adi,adau1978" },
        { .compatible = "adi,adau1979" },
        { },
};
MODULE_DEVICE_TABLE(of, adau1977_spi_of_match);

static struct spi_driver adau1977_spi_driver = {
	.driver = {
		.name = "adau1977",
		.of_match_table = of_match_ptr(adau1977_spi_of_match),
	},
	.probe = adau1977_spi_probe,
	.id_table = adau1977_spi_ids,
};
module_spi_driver(adau1977_spi_driver);

MODULE_DESCRIPTION("ASoC ADAU1977/ADAU1978/ADAU1979 driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
