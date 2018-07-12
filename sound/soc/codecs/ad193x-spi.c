/*
 * AD1938/AD1939 audio driver
 *
 * Copyright 2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#include "ad193x.h"

static int ad193x_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap_config config;

	config = ad193x_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 16;
	config.read_flag_mask = 0x09;
	config.write_flag_mask = 0x08;

	return ad193x_probe(&spi->dev, devm_regmap_init_spi(spi, &config),
			    (enum ad193x_type)id->driver_data);
}

static const struct spi_device_id ad193x_spi_id[] = {
	{ "ad193x", AD193X },
	{ "ad1933", AD1933 },
	{ "ad1934", AD1934 },
	{ "ad1938", AD193X },
	{ "ad1939", AD193X },
	{ "adau1328", AD193X },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad193x_spi_id);

static struct spi_driver ad193x_spi_driver = {
	.driver = {
		.name	= "ad193x",
	},
	.probe		= ad193x_spi_probe,
	.id_table	= ad193x_spi_id,
};
module_spi_driver(ad193x_spi_driver);

MODULE_DESCRIPTION("ASoC AD1938/AD1939 audio CODEC driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
