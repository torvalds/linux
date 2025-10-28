// SPDX-License-Identifier: GPL-2.0
//
// CS530x CODEC driver
//
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include "cs530x.h"

static const struct of_device_id cs530x_of_match[] = {
	{
		.compatible = "cirrus,cs4282",
		.data = (void *)CS4282,
	}, {
		.compatible = "cirrus,cs4302",
		.data = (void *)CS4302,
	}, {
		.compatible = "cirrus,cs4304",
		.data = (void *)CS4304,
	}, {
		.compatible = "cirrus,cs4308",
		.data = (void *)CS4308,
	}, {
		.compatible = "cirrus,cs5302",
		.data = (void *)CS5302,
	}, {
		.compatible = "cirrus,cs5304",
		.data = (void *)CS5304,
	}, {
		.compatible = "cirrus,cs5304",
		.data = (void *)CS5308,
	},
	{}
};
MODULE_DEVICE_TABLE(of, cs530x_of_match);

static const struct spi_device_id cs530x_spi_id[] = {
	{ "cs4282", CS4282 },
	{ "cs4302", CS4302 },
	{ "cs4304", CS4304 },
	{ "cs4308", CS4308 },
	{ "cs5302", CS5302 },
	{ "cs5304", CS5304 },
	{ "cs5308", CS5308 },
	{ }
};
MODULE_DEVICE_TABLE(spi, cs530x_spi_id);

static int cs530x_spi_probe(struct spi_device *spi)
{
	struct cs530x_priv *cs530x;
	struct device *dev = &spi->dev;
	int ret;

	cs530x = devm_kzalloc(dev, sizeof(struct cs530x_priv), GFP_KERNEL);
	if (cs530x == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, cs530x);

	cs530x->regmap = devm_regmap_init_spi(spi, &cs530x_regmap_spi);
	if (IS_ERR(cs530x->regmap)) {
		ret = PTR_ERR(cs530x->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	cs530x->devtype = (unsigned long)spi_get_device_match_data(spi);
	cs530x->dev = &spi->dev;

	return cs530x_probe(cs530x);
}

static struct spi_driver cs530x_spi_driver = {
	.driver = {
		.name		= "cs530x",
		.of_match_table = cs530x_of_match,
	},
	.id_table	= cs530x_spi_id,
	.probe		= cs530x_spi_probe,
};

module_spi_driver(cs530x_spi_driver);

MODULE_DESCRIPTION("SPI CS530X driver");
MODULE_IMPORT_NS("SND_SOC_CS530X");
MODULE_AUTHOR("Vitaly Rodionov <vitalyr@opensource.cirrus.com>");
MODULE_LICENSE("GPL");
