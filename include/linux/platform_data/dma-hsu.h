/*
 * Driver for the High Speed UART DMA
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _PLATFORM_DATA_DMA_HSU_H
#define _PLATFORM_DATA_DMA_HSU_H

#include <linux/device.h>

struct hsu_dma_slave {
	struct device	*dma_dev;
	int		chan_id;
};

struct hsu_dma_platform_data {
	unsigned short	nr_channels;
};

#endif /* _PLATFORM_DATA_DMA_HSU_H */
