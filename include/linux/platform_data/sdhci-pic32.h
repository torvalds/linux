/*
 * Purna Chandra Mandal, purna.mandal@microchip.com
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 */
#ifndef __PIC32_SDHCI_PDATA_H__
#define __PIC32_SDHCI_PDATA_H__

struct pic32_sdhci_platform_data {
	/* read & write fifo threshold */
	int (*setup_dma)(u32 rfifo, u32 wfifo);
};

#endif
