// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the PCM512x CODECs
 *
 * Author:	Mark Brown <broonie@kernel.org>
 *		Copyright 2014 Linaro Ltd
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
	{ "pcm5141", },
	{ "pcm5142", },
	{ },
};
MODULE_DEVICE_TABLE(spi, pcm512x_spi_id);

static const struct of_device_id pcm512x_of_match[] = {
	{ .compatible = "ti,pcm5121", },
	{ .compatible = "ti,pcm5122", },
	{ .compatible = "ti,pcm5141", },
	{ .compatible = "ti,pcm5142", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm512x_of_match);

static struct spi_driver pcm512x_spi_driver = {
	.probe		= pcm512x_spi_probe,
	.remove		= pcm512x_spi_remove,
	.id_table	= pcm512x_spi_id,
	.driver = {
		.name	= "pcm512x",
		.of_match_table = pcm512x_of_match,
		.pm     = &pcm512x_pm_ops,
	},
};

module_spi_driver(pcm512x_spi_driver);

MODULE_DESCRIPTION("ASoC PCM512x codec driver - SPI");
MODULE_AUTHOR("Mark Brown <broonie@kernel.org>");
MODULE_LICENSE("GPL v2");
