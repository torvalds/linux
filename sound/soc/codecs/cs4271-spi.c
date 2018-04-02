/*
 * CS4271 SPI audio driver
 *
 * Copyright (c) 2010 Alexander Sverdlin <subaparts@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include "cs4271.h"

static int cs4271_spi_probe(struct spi_device *spi)
{
	struct regmap_config config;

	config = cs4271_regmap_config;
	config.reg_bits = 16;
	config.val_bits = 8;
	config.read_flag_mask = 0x21;
	config.write_flag_mask = 0x20;

	return cs4271_probe(&spi->dev, devm_regmap_init_spi(spi, &config));
}

static struct spi_driver cs4271_spi_driver = {
	.driver = {
		.name	= "cs4271",
		.of_match_table = of_match_ptr(cs4271_dt_ids),
	},
	.probe		= cs4271_spi_probe,
};
module_spi_driver(cs4271_spi_driver);

MODULE_DESCRIPTION("ASoC CS4271 SPI Driver");
MODULE_AUTHOR("Alexander Sverdlin <subaparts@yandex.ru>");
MODULE_LICENSE("GPL");
