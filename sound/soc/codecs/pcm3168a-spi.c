// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCM3168A codec spi driver
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <sound/soc.h>

#include "pcm3168a.h"

static int pcm3168a_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_spi(spi, &pcm3168a_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return pcm3168a_probe(&spi->dev, regmap);
}

static int pcm3168a_spi_remove(struct spi_device *spi)
{
	pcm3168a_remove(&spi->dev);

	return 0;
}

static const struct spi_device_id pcm3168a_spi_id[] = {
	{ "pcm3168a", },
	{ },
};
MODULE_DEVICE_TABLE(spi, pcm3168a_spi_id);

static const struct of_device_id pcm3168a_of_match[] = {
	{ .compatible = "ti,pcm3168a", },
	{ }
};
MODULE_DEVICE_TABLE(of, pcm3168a_of_match);

static struct spi_driver pcm3168a_spi_driver = {
	.probe		= pcm3168a_spi_probe,
	.remove		= pcm3168a_spi_remove,
	.id_table	= pcm3168a_spi_id,
	.driver = {
		.name	= "pcm3168a",
		.of_match_table = pcm3168a_of_match,
		.pm		= &pcm3168a_pm_ops,
	},
};
module_spi_driver(pcm3168a_spi_driver);

MODULE_DESCRIPTION("PCM3168A SPI codec driver");
MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_LICENSE("GPL v2");
