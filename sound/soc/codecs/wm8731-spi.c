// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8731.c  --  WM8731 ALSA SoC Audio driver
 *
 * Copyright 2005 Openedhand Ltd.
 * Copyright 2006-12 Wolfson Microelectronics, plc
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on wm8753.c by Liam Girdwood
 */

#include <linux/spi/spi.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include "wm8731.h"

static const struct of_device_id wm8731_of_match[] = {
	{ .compatible = "wlf,wm8731", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8731_of_match);

static int wm8731_spi_probe(struct spi_device *spi)
{
	struct wm8731_priv *wm8731;
	int ret;

	wm8731 = devm_kzalloc(&spi->dev, sizeof(*wm8731), GFP_KERNEL);
	if (wm8731 == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, wm8731);

	wm8731->regmap = devm_regmap_init_spi(spi, &wm8731_regmap);
	if (IS_ERR(wm8731->regmap)) {
		ret = PTR_ERR(wm8731->regmap);
		dev_err(&spi->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	return wm8731_init(&spi->dev, wm8731);
}

static struct spi_driver wm8731_spi_driver = {
	.driver = {
		.name	= "wm8731",
		.of_match_table = wm8731_of_match,
	},
	.probe		= wm8731_spi_probe,
};

module_spi_driver(wm8731_spi_driver);

MODULE_DESCRIPTION("ASoC WM8731 driver - SPI");
MODULE_AUTHOR("Richard Purdie");
MODULE_LICENSE("GPL");
