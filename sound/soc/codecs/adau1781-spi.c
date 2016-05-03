/*
 * Driver for ADAU1381/ADAU1781 CODEC
 *
 * Copyright 2014 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <sound/soc.h>

#include "adau1781.h"

static void adau1781_spi_switch_mode(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);

	/*
	 * To get the device into SPI mode CLATCH has to be pulled low three
	 * times.  Do this by issuing three dummy reads.
	 */
	spi_w8r8(spi, 0x00);
	spi_w8r8(spi, 0x00);
	spi_w8r8(spi, 0x00);
}

static int adau1781_spi_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct regmap_config config;

	if (!id)
		return -EINVAL;

	config = adau1781_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 24;
	config.read_flag_mask = 0x1;

	return adau1781_probe(&spi->dev,
		devm_regmap_init_spi(spi, &config),
		id->driver_data, adau1781_spi_switch_mode);
}

static int adau1781_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static const struct spi_device_id adau1781_spi_id[] = {
	{ "adau1381", ADAU1381 },
	{ "adau1781", ADAU1781 },
	{ }
};
MODULE_DEVICE_TABLE(spi, adau1781_spi_id);

#if defined(CONFIG_OF)
static const struct of_device_id adau1781_spi_dt_ids[] = {
	{ .compatible = "adi,adau1381", },
	{ .compatible = "adi,adau1781", },
	{ },
};
MODULE_DEVICE_TABLE(of, adau1781_spi_dt_ids);
#endif

static struct spi_driver adau1781_spi_driver = {
	.driver = {
		.name = "adau1781",
		.of_match_table = of_match_ptr(adau1781_spi_dt_ids),
	},
	.probe = adau1781_spi_probe,
	.remove = adau1781_spi_remove,
	.id_table = adau1781_spi_id,
};
module_spi_driver(adau1781_spi_driver);

MODULE_DESCRIPTION("ASoC ADAU1381/ADAU1781 CODEC SPI driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
