/*
 * Copyright (C) 2005 Stephen Street / StreetFire Sound Labs
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

#ifndef PXA2XX_SPI_H_
#define PXA2XX_SPI_H_

#define PXA2XX_CS_ASSERT (0x01)
#define PXA2XX_CS_DEASSERT (0x02)

#if defined(CONFIG_PXA25x)
#define CLOCK_SPEED_HZ 3686400
#define SSP1_SerClkDiv(x) (((CLOCK_SPEED_HZ/2/(x+1))<<8)&0x0000ff00)
#define SSP2_SerClkDiv(x) (((CLOCK_SPEED_HZ/(x+1))<<8)&0x000fff00)
#define SSP3_SerClkDiv(x) (((CLOCK_SPEED_HZ/(x+1))<<8)&0x000fff00)
#elif defined(CONFIG_PXA27x)
#define CLOCK_SPEED_HZ 13000000
#define SSP1_SerClkDiv(x) (((CLOCK_SPEED_HZ/(x+1))<<8)&0x000fff00)
#define SSP2_SerClkDiv(x) (((CLOCK_SPEED_HZ/(x+1))<<8)&0x000fff00)
#define SSP3_SerClkDiv(x) (((CLOCK_SPEED_HZ/(x+1))<<8)&0x000fff00)
#endif

#define SSP1_VIRT ((void *)(io_p2v(__PREG(SSCR0_P(1)))))
#define SSP2_VIRT ((void *)(io_p2v(__PREG(SSCR0_P(2)))))
#define SSP3_VIRT ((void *)(io_p2v(__PREG(SSCR0_P(3)))))

enum pxa_ssp_type {
	SSP_UNDEFINED = 0,
	PXA25x_SSP,  /* pxa 210, 250, 255, 26x */
	PXA25x_NSSP, /* pxa 255, 26x (including ASSP) */
	PXA27x_SSP,
};

/* device.platform_data for SSP controller devices */
struct pxa2xx_spi_master {
	enum pxa_ssp_type ssp_type;
	u32 clock_enable;
	u16 num_chipselect;
	u8 enable_dma;
};

/* spi_board_info.controller_data for SPI slave devices,
 * copied to spi_device.platform_data ... mostly for dma tuning
 */
struct pxa2xx_spi_chip {
	u8 tx_threshold;
	u8 rx_threshold;
	u8 dma_burst_size;
	u32 timeout_microsecs;
	u8 enable_loopback;
	void (*cs_control)(u32 command);
};

#endif /*PXA2XX_SPI_H_*/
