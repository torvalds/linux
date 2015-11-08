/*
 * wm8804-spi.c  --  WM8804 S/PDIF transceiver driver - SPI
 *
 * Copyright 2015 Cirrus Logic Inc
 *
 * Author: Charles Keepax <ckeepax@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "wm8804.h"

static int wm8804_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &wm8804_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return wm8804_probe(&spi->dev, regmap);
}

static int wm8804_spi_remove(struct spi_device *spi)
{
	wm8804_remove(&spi->dev);
	return 0;
}

static const struct of_device_id wm8804_of_match[] = {
	{ .compatible = "wlf,wm8804", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8804_of_match);

static struct spi_driver wm8804_spi_driver = {
	.driver = {
		.name = "wm8804",
		.pm = &wm8804_pm,
		.of_match_table = wm8804_of_match,
	},
	.probe = wm8804_spi_probe,
	.remove = wm8804_spi_remove
};

module_spi_driver(wm8804_spi_driver);

MODULE_DESCRIPTION("ASoC WM8804 driver - SPI");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
