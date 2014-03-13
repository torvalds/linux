/*
 * Driver for the PCM512x CODECs
 *
 * Author:	Mark Brown <broonie@linaro.org>
 *		Copyright 2014 Linaro Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "pcm512x.h"

static int pcm512x_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_spi(spi, &pcm512x_regmap);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		return ret;
	}

	return pcm512x_probe(&spi->dev, regmap);
}

static int pcm512x_spi_remove(struct spi_device *spi)
{
	pcm512x_remove(&spi->dev);
	return 0;
}

static const struct spi_device_id pcm512x_spi_id[] = {
	{ "pcm5121", },
	{ "pcm5122", },
	{ },
};
MODULE_DEVICE_TABLE(spi, pcm512x_spi_id);

static const struct of_device_id pcm512x_of_match[] = {
	{ .compatible = "ti,pcm5121", },
	{ .compatible = "ti,pcm5122", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm512x_of_match);

static struct spi_driver pcm512x_spi_driver = {
	.probe		= pcm512x_spi_probe,
	.remove		= pcm512x_spi_remove,
	.id_table	= pcm512x_spi_id,
	.driver = {
		.name	= "pcm512x",
		.owner	= THIS_MODULE,
		.of_match_table = pcm512x_of_match,
		.pm     = &pcm512x_pm_ops,
	},
};

module_spi_driver(pcm512x_spi_driver);
