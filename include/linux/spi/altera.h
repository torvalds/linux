/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header File for Altera SPI Driver.
 */
#ifndef __LINUX_SPI_ALTERA_H
#define __LINUX_SPI_ALTERA_H

#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

/**
 * struct altera_spi_platform_data - Platform data of the Altera SPI driver
 * @mode_bits:		Mode bits of SPI master.
 * @num_chipselect:	Number of chipselects.
 * @bits_per_word_mask:	bitmask of supported bits_per_word for transfers.
 */
struct altera_spi_platform_data {
	u16				mode_bits;
	u16				num_chipselect;
	u32				bits_per_word_mask;
};

#endif /* __LINUX_SPI_ALTERA_H */
