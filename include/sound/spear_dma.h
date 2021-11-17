/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
* linux/spear_dma.h
*
* Copyright (ST) 2012 Rajeev Kumar (rajeevkumar.linux@gmail.com)
*/

#ifndef SPEAR_DMA_H
#define SPEAR_DMA_H

#include <linux/dmaengine.h>

struct spear_dma_data {
	void *data;
	dma_addr_t addr;
	u32 max_burst;
	enum dma_slave_buswidth addr_width;
};

#endif /* SPEAR_DMA_H */
