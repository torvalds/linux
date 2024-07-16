/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header File for Altera SPI Driver.
 */
#ifndef __LINUX_SPI_ALTERA_H
#define __LINUX_SPI_ALTERA_H

#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/types.h>

#define ALTERA_SPI_MAX_CS		32

/**
 * struct altera_spi_platform_data - Platform data of the Altera SPI driver
 * @mode_bits:		Mode bits of SPI master.
 * @num_chipselect:	Number of chipselects.
 * @bits_per_word_mask:	bitmask of supported bits_per_word for transfers.
 * @num_devices:	Number of devices that shall be added when the driver
 *			is probed.
 * @devices:		The devices to add.
 */
struct altera_spi_platform_data {
	u16				mode_bits;
	u16				num_chipselect;
	u32				bits_per_word_mask;
	u16				num_devices;
	struct spi_board_info		*devices;
};

struct altera_spi {
	int irq;
	int len;
	int count;
	int bytes_per_word;
	u32 imr;

	/* data buffers */
	const unsigned char *tx;
	unsigned char *rx;

	struct regmap *regmap;
	u32 regoff;
	struct device *dev;
};

extern irqreturn_t altera_spi_irq(int irq, void *dev);
extern void altera_spi_init_master(struct spi_master *master);
#endif /* __LINUX_SPI_ALTERA_H */
