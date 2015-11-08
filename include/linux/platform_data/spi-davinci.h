/*
 * Copyright 2009 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ARCH_ARM_DAVINCI_SPI_H
#define __ARCH_ARM_DAVINCI_SPI_H

#include <linux/platform_data/edma.h>

#define SPI_INTERN_CS	0xFF

enum {
	SPI_VERSION_1, /* For DM355/DM365/DM6467 */
	SPI_VERSION_2, /* For DA8xx */
};

/**
 * davinci_spi_platform_data - Platform data for SPI master device on DaVinci
 *
 * @version:	version of the SPI IP. Different DaVinci devices have slightly
 *		varying versions of the same IP.
 * @num_chipselect: number of chipselects supported by this SPI master
 * @intr_line:	interrupt line used to connect the SPI IP to the ARM interrupt
 *		controller withn the SoC. Possible values are 0 and 1.
 * @chip_sel:	list of GPIOs which can act as chip-selects for the SPI.
 *		SPI_INTERN_CS denotes internal SPI chip-select. Not necessary
 *		to populate if all chip-selects are internal.
 * @cshold_bug:	set this to true if the SPI controller on your chip requires
 *		a write to CSHOLD bit in between transfers (like in DM355).
 * @dma_event_q: DMA event queue to use if SPI_IO_TYPE_DMA is used for any
 *		device on the bus.
 */
struct davinci_spi_platform_data {
	u8			version;
	u8			num_chipselect;
	u8			intr_line;
	u8			*chip_sel;
	u8			prescaler_limit;
	bool			cshold_bug;
	enum dma_event_q	dma_event_q;
};

/**
 * davinci_spi_config - Per-chip-select configuration for SPI slave devices
 *
 * @wdelay:	amount of delay between transmissions. Measured in number of
 *		SPI module clocks.
 * @odd_parity:	polarity of parity flag at the end of transmit data stream.
 *		0 - odd parity, 1 - even parity.
 * @parity_enable: enable transmission of parity at end of each transmit
 *		data stream.
 * @io_type:	type of IO transfer. Choose between polled, interrupt and DMA.
 * @timer_disable: disable chip-select timers (setup and hold)
 * @c2tdelay:	chip-select setup time. Measured in number of SPI module clocks.
 * @t2cdelay:	chip-select hold time. Measured in number of SPI module clocks.
 * @t2edelay:	transmit data finished to SPI ENAn pin inactive time. Measured
 *		in number of SPI clocks.
 * @c2edelay:	chip-select active to SPI ENAn signal active time. Measured in
 *		number of SPI clocks.
 */
struct davinci_spi_config {
	u8	wdelay;
	u8	odd_parity;
	u8	parity_enable;
#define SPI_IO_TYPE_INTR	0
#define SPI_IO_TYPE_POLL	1
#define SPI_IO_TYPE_DMA		2
	u8	io_type;
	u8	timer_disable;
	u8	c2tdelay;
	u8	t2cdelay;
	u8	t2edelay;
	u8	c2edelay;
};

#endif	/* __ARCH_ARM_DAVINCI_SPI_H */
