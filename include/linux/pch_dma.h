/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010 Intel Corporation
 */

#ifndef PCH_DMA_H
#define PCH_DMA_H

#include <linux/dmaengine.h>

enum pch_dma_width {
	PCH_DMA_WIDTH_1_BYTE,
	PCH_DMA_WIDTH_2_BYTES,
	PCH_DMA_WIDTH_4_BYTES,
};

struct pch_dma_slave {
	struct device		*dma_dev;
	unsigned int		chan_id;
	dma_addr_t		tx_reg;
	dma_addr_t		rx_reg;
	enum pch_dma_width	width;
};

#endif
