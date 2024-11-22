// SPDX-License-Identifier: GPL-2.0
//
// PCM3060 SPI driver
//
// Copyright (C) 2018 Kirill Marinushkin <k.marinushkin@gmail.com>

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <sound/soc.h>

#include "pcm3060.h"

static int pcm3060_spi_probe(struct spi_device *spi)
{
	struct pcm3060_priv *priv;

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spi_set_drvdata(spi, priv);

	priv->regmap = devm_regmap_init_spi(spi, &pcm3060_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	return pcm3060_probe(&spi->dev);
}

static const struct spi_device_id pcm3060_spi_id[] = {
	{ .name = "pcm3060" },
	{ },
};
MODULE_DEVICE_TABLE(spi, pcm3060_spi_id);

#ifdef CONFIG_OF
static const struct of_device_id pcm3060_of_match[] = {
	{ .compatible = "ti,pcm3060" },
	{ },
};
MODULE_DEVICE_TABLE(of, pcm3060_of_match);
#endif /* CONFIG_OF */

static struct spi_driver pcm3060_spi_driver = {
	.driver = {
		.name = "pcm3060",
#ifdef CONFIG_OF
		.of_match_table = pcm3060_of_match,
#endif /* CONFIG_OF */
	},
	.id_table = pcm3060_spi_id,
	.probe = pcm3060_spi_probe,
};

module_spi_driver(pcm3060_spi_driver);

MODULE_DESCRIPTION("PCM3060 SPI driver");
MODULE_AUTHOR("Kirill Marinushkin <k.marinushkin@gmail.com>");
MODULE_LICENSE("GPL v2");
