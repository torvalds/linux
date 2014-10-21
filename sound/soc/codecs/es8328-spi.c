/*
 * es8328.c  --  ES8328 ALSA SoC SPI Audio driver
 *
 * Copyright 2014 Sutajio Ko-Usagi PTE LTD
 *
 * Author: Sean Cross <xobs@kosagi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <sound/soc.h>
#include "es8328.h"

static const struct of_device_id es8328_of_match[] = {
	{ .compatible = "everest,es8328", },
	{ }
};
MODULE_DEVICE_TABLE(of, es8328_of_match);

static int es8328_spi_probe(struct spi_device *spi)
{
	return es8328_probe(&spi->dev,
			devm_regmap_init_spi(spi, &es8328_regmap_config));
}

static int es8328_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static struct spi_driver es8328_spi_driver = {
	.driver = {
		.name		= "es8328",
		.of_match_table	= es8328_of_match,
	},
	.probe	= es8328_spi_probe,
	.remove	= es8328_spi_remove,
};

module_spi_driver(es8328_spi_driver);
MODULE_DESCRIPTION("ASoC ES8328 audio CODEC SPI driver");
MODULE_AUTHOR("Sean Cross <xobs@kosagi.com>");
MODULE_LICENSE("GPL");
