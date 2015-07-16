/*
 * SSM2602 SPI audio driver
 *
 * Copyright 2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>

#include <sound/soc.h>

#include "ssm2602.h"

static int ssm2602_spi_probe(struct spi_device *spi)
{
	return ssm2602_probe(&spi->dev, SSM2602,
		devm_regmap_init_spi(spi, &ssm2602_regmap_config));
}

static int ssm2602_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static const struct of_device_id ssm2602_of_match[] = {
	{ .compatible = "adi,ssm2602", },
	{ }
};
MODULE_DEVICE_TABLE(of, ssm2602_of_match);

static struct spi_driver ssm2602_spi_driver = {
	.driver = {
		.name	= "ssm2602",
		.owner	= THIS_MODULE,
		.of_match_table = ssm2602_of_match,
	},
	.probe		= ssm2602_spi_probe,
	.remove		= ssm2602_spi_remove,
};
module_spi_driver(ssm2602_spi_driver);

MODULE_DESCRIPTION("ASoC SSM2602 SPI driver");
MODULE_AUTHOR("Cliff Cai");
MODULE_LICENSE("GPL");
