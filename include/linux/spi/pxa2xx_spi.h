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
#ifndef __linux_pxa2xx_spi_h
#define __linux_pxa2xx_spi_h

#include <linux/pxa2xx_ssp.h>

#define PXA2XX_CS_ASSERT (0x01)
#define PXA2XX_CS_DEASSERT (0x02)

/* device.platform_data for SSP controller devices */
struct pxa2xx_spi_master {
	u32 clock_enable;
	u16 num_chipselect;
	u8 enable_dma;

	/* DMA engine specific config */
	int rx_chan_id;
	int tx_chan_id;
	int rx_slave_id;
	int tx_slave_id;

	/* For non-PXA arches */
	struct ssp_device ssp;
};

/* spi_board_info.controller_data for SPI slave devices,
 * copied to spi_device.platform_data ... mostly for dma tuning
 */
struct pxa2xx_spi_chip {
	u8 tx_threshold;
	u8 rx_threshold;
	u8 dma_burst_size;
	u32 timeout;
	u8 enable_loopback;
	int gpio_cs;
	void (*cs_control)(u32 command);
};

#if defined(CONFIG_ARCH_PXA) || defined(CONFIG_ARCH_MMP)

#include <linux/clk.h>
#include <mach/dma.h>

extern void pxa2xx_set_spi_info(unsigned id, struct pxa2xx_spi_master *info);

#endif
#endif
