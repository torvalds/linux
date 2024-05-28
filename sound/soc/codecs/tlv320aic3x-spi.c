/* SPDX-License-Identifier: GPL-2.0
 *
 * ALSA SoC TLV320AIC3x codec driver SPI interface
 *
 * Author:      Arun KS, <arunks@mistralsolutions.com>
 * Copyright:   (C) 2008 Mistral Solutions Pvt Ltd.,
 *
 * Based on sound/soc/codecs/wm8731.c by Richard Purdie
 *
 */

#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "tlv320aic3x.h"

static int aic3x_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	struct regmap_config config;
	const struct spi_device_id *id = spi_get_device_id(spi);

	config = aic3x_regmap;
	config.reg_bits = 7;
	config.pad_bits = 1;
	config.val_bits = 8;
	config.read_flag_mask = 0x01;

	dev_dbg(&spi->dev, "probing tlv320aic3x spi device\n");

	regmap = devm_regmap_init_spi(spi, &config);
	return aic3x_probe(&spi->dev, regmap, id->driver_data);
}

static void aic3x_spi_remove(struct spi_device *spi)
{
	aic3x_remove(&spi->dev);
}

static const struct spi_device_id aic3x_spi_id[] = {
	{ "tlv320aic3x", AIC3X_MODEL_3X },
	{ "tlv320aic33", AIC3X_MODEL_33 },
	{ "tlv320aic3007", AIC3X_MODEL_3007 },
	{ "tlv320aic3104", AIC3X_MODEL_3104 },
	{ "tlv320aic3106", AIC3X_MODEL_3106 },
	{ }
};
MODULE_DEVICE_TABLE(spi, aic3x_spi_id);

static const struct of_device_id aic3x_of_id[] = {
	{ .compatible = "ti,tlv320aic3x", },
	{ .compatible = "ti,tlv320aic33" },
	{ .compatible = "ti,tlv320aic3007" },
	{ .compatible = "ti,tlv320aic3104" },
	{ .compatible = "ti,tlv320aic3106" },
	{},
};
MODULE_DEVICE_TABLE(of, aic3x_of_id);

static struct spi_driver aic3x_spi_driver = {
	.driver = {
		.name = "tlv320aic3x",
		.of_match_table = aic3x_of_id,
	},
	.probe = aic3x_spi_probe,
	.remove = aic3x_spi_remove,
	.id_table = aic3x_spi_id,
};

module_spi_driver(aic3x_spi_driver);

MODULE_DESCRIPTION("ASoC TLV320AIC3x codec driver SPI");
MODULE_AUTHOR("Arun KS <arunks@mistralsolutions.com>");
MODULE_LICENSE("GPL");
