#ifndef __LINUX_SPI_GPIO_H
#define __LINUX_SPI_GPIO_H

/*
 * For each bitbanged SPI bus, set up a platform_device node with:
 *   - name "spi_gpio"
 *   - id the same as the SPI bus number it implements
 *   - dev.platform data pointing to a struct spi_gpio_platform_data
 *
 * Or, see the driver code for information about speedups that are
 * possible on platforms that support inlined access for GPIOs (no
 * spi_gpio_platform_data is used).
 *
 * Use spi_board_info with these busses in the usual way, being sure
 * that the controller_data being the GPIO used for each device's
 * chipselect:
 *
 *	static struct spi_board_info ... [] = {
 *	...
 *		// this slave uses GPIO 42 for its chipselect
 *		.controller_data = (void *) 42,
 *	...
 *		// this one uses GPIO 86 for its chipselect
 *		.controller_data = (void *) 86,
 *	...
 *	};
 *
 * If chipselect is not used (there's only one device on the bus), assign
 * SPI_GPIO_NO_CHIPSELECT to the controller_data:
 *		.controller_data = (void *) SPI_GPIO_NO_CHIPSELECT;
 *
 * If the MISO or MOSI pin is not available then it should be set to
 * SPI_GPIO_NO_MISO or SPI_GPIO_NO_MOSI.
 *
 * If the bitbanged bus is later switched to a "native" controller,
 * that platform_device and controller_data should be removed.
 */

#define SPI_GPIO_NO_CHIPSELECT		((unsigned long)-1l)
#define SPI_GPIO_NO_MISO		((unsigned long)-1l)
#define SPI_GPIO_NO_MOSI		((unsigned long)-1l)

/**
 * struct spi_gpio_platform_data - parameter for bitbanged SPI master
 * @sck: number of the GPIO used for clock output
 * @mosi: number of the GPIO used for Master Output, Slave In (MOSI) data
 * @miso: number of the GPIO used for Master Input, Slave Output (MISO) data
 * @num_chipselect: how many slaves to allow
 *
 * All GPIO signals used with the SPI bus managed through this driver
 * (chipselects, MOSI, MISO, SCK) must be configured as GPIOs, instead
 * of some alternate function.
 *
 * It can be convenient to use this driver with pins that have alternate
 * functions associated with a "native" SPI controller if a driver for that
 * controller is not available, or is missing important functionality.
 *
 * On platforms which can do so, configure MISO with a weak pullup unless
 * there's an external pullup on that signal.  That saves power by avoiding
 * floating signals.  (A weak pulldown would save power too, but many
 * drivers expect to see all-ones data as the no slave "response".)
 */
struct spi_gpio_platform_data {
	unsigned	sck;
	unsigned long	mosi;
	unsigned long	miso;

	u16		num_chipselect;
};

#endif /* __LINUX_SPI_GPIO_H */
