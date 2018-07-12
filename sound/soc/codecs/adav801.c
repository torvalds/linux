/*
 * ADAV801 audio driver
 *
 * Copyright 2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#include "adav80x.h"

static const struct spi_device_id adav80x_spi_id[] = {
	{ "adav801", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adav80x_spi_id);

static int adav80x_spi_probe(struct spi_device *spi)
{
	struct regmap_config config;

	config = adav80x_regmap_config;
	config.read_flag_mask = 0x01;

	return adav80x_bus_probe(&spi->dev, devm_regmap_init_spi(spi, &config));
}

static struct spi_driver adav80x_spi_driver = {
	.driver = {
		.name	= "adav801",
	},
	.probe		= adav80x_spi_probe,
	.id_table	= adav80x_spi_id,
};
module_spi_driver(adav80x_spi_driver);

MODULE_DESCRIPTION("ASoC ADAV801 driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_AUTHOR("Yi Li <yi.li@analog.com>>");
MODULE_LICENSE("GPL");
