/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_EP93XX_SPI_H
#define __ASM_MACH_EP93XX_SPI_H

struct spi_device;

/**
 * struct ep93xx_spi_info - EP93xx specific SPI descriptor
 * @chipselect: array of gpio numbers to use as chip selects
 * @num_chipselect: ARRAY_SIZE(chipselect)
 * @use_dma: use DMA for the transfers
 */
struct ep93xx_spi_info {
	int	*chipselect;
	int	num_chipselect;
	bool	use_dma;
};

#endif /* __ASM_MACH_EP93XX_SPI_H */
