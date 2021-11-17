/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SPI_GPIO_H
#define __LINUX_SPI_GPIO_H

/*
 * For each bitbanged SPI bus, set up a platform_device node with:
 *   - name "spi_gpio"
 *   - id the same as the SPI bus number it implements
 *   - dev.platform data pointing to a struct spi_gpio_platform_data
 *
 * Use spi_board_info with these busses in the usual way.
 *
 * If the bitbanged bus is later switched to a "native" controller,
 * that platform_device and controller_data should be removed.
 */

/**
 * struct spi_gpio_platform_data - parameter for bitbanged SPI master
 * @num_chipselect: how many slaves to allow
 */
struct spi_gpio_platform_data {
	u16		num_chipselect;
};

#endif /* __LINUX_SPI_GPIO_H */
