// SPDX-License-Identifier: GPL-2.0
/*
 * Cirrus Logic CS42448/CS42888 Audio CODEC DAI SPI driver
 *
 * Copyright 2026 NXP
 *
 */

#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <sound/soc.h>

#include "cs42xx8.h"

/*
 * CS42448/CS42888 SPI register access (from datasheet Figure 23):
 *
 * The SPI frame is 3 bytes:
 *   Byte 0: chip address [7:1] = 1001111, bit[0] = R/W (0=write, 1=read)
 *           Write: 0x9E,  Read: 0x9F
 *   Byte 1: MAP - Memory Address Pointer
 *           bit[7] = INCR (auto-increment for burst), bits[6:0] = address
 *   Byte 2: data byte
 *
 * We configure reg_bits=16 so that regmap treats the address field as 2 bytes
 * (big-endian). The chip address byte (0x9E/0x9F) is placed in the high byte
 * via write_flag_mask / read_flag_mask, and the MAP register address occupies
 * the low byte. Currently INCR (MAP bit[7]) is not set and use_single_read/write
 * are enabled. This produces the correct 3-byte on-wire frame without any
 * custom bus implementation:
 *
 *   write: [0x9E, MAP_addr, data]
 *   read:  [0x9F, MAP_addr] -> [data]
 */

static int cs42xx8_spi_probe(struct spi_device *spi)
{
	struct cs42xx8_driver_data *drvdata;
	struct regmap_config config;
	int ret;

	drvdata = (struct cs42xx8_driver_data *)spi_get_device_match_data(spi);
	if (!drvdata)
		return dev_err_probe(&spi->dev, -EINVAL,
				     "failed to find driver data\n");

	config = cs42xx8_regmap_config;
	/*
	 * reg_bits=16 makes regmap send a 2-byte address field (big-endian).
	 * write_flag_mask/read_flag_mask are OR'd into that address field:
	 */
	config.reg_bits           = 16;
	config.write_flag_mask    = 0x9E;
	config.read_flag_mask     = 0x9F;

	ret = cs42xx8_probe(&spi->dev,
			    devm_regmap_init_spi(spi, &config), drvdata);
	if (ret)
		return ret;

	pm_runtime_enable(&spi->dev);
	pm_request_idle(&spi->dev);

	return 0;
}

static void cs42xx8_spi_remove(struct spi_device *spi)
{
	pm_runtime_disable(&spi->dev);
}

static const struct of_device_id cs42xx8_of_match[] = {
	{ .compatible = "cirrus,cs42448", .data = &cs42448_data, },
	{ .compatible = "cirrus,cs42888", .data = &cs42888_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cs42xx8_of_match);

static const struct spi_device_id cs42xx8_spi_id[] = {
	{ .name = "cs42448", .driver_data = (kernel_ulong_t)&cs42448_data },
	{ .name = "cs42888", .driver_data = (kernel_ulong_t)&cs42888_data },
	{ }
};
MODULE_DEVICE_TABLE(spi, cs42xx8_spi_id);

static struct spi_driver cs42xx8_spi_driver = {
	.driver = {
		.name = "cs42xx8",
		.pm = pm_ptr(&cs42xx8_pm),
		.of_match_table = cs42xx8_of_match,
	},
	.probe = cs42xx8_spi_probe,
	.remove = cs42xx8_spi_remove,
	.id_table = cs42xx8_spi_id,
};

module_spi_driver(cs42xx8_spi_driver);

MODULE_DESCRIPTION("Cirrus Logic CS42448/CS42888 ALSA SoC Codec SPI Driver");
MODULE_AUTHOR("Chancel Liu <chancel.liu@nxp.com>");
MODULE_LICENSE("GPL");
