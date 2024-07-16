/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Purna Chandra Mandal, purna.mandal@microchip.com
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 */
#ifndef __PIC32_SDHCI_PDATA_H__
#define __PIC32_SDHCI_PDATA_H__

struct pic32_sdhci_platform_data {
	/* read & write fifo threshold */
	int (*setup_dma)(u32 rfifo, u32 wfifo);
};

#endif
