/*
 * Driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2007 Atmel Corporation
 * Copyright (C) 2010-2011 ST Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _DMA_DW_H
#define _DMA_DW_H

#include <linux/dmaengine.h>

/* DMA API extensions */
struct dw_desc;

struct dw_cyclic_desc {
	struct dw_desc	**desc;
	unsigned long	periods;
	void		(*period_callback)(void *param);
	void		*period_callback_param;
};

struct dw_cyclic_desc *dw_dma_cyclic_prep(struct dma_chan *chan,
		dma_addr_t buf_addr, size_t buf_len, size_t period_len,
		enum dma_transfer_direction direction);
void dw_dma_cyclic_free(struct dma_chan *chan);
int dw_dma_cyclic_start(struct dma_chan *chan);
void dw_dma_cyclic_stop(struct dma_chan *chan);

dma_addr_t dw_dma_get_src_addr(struct dma_chan *chan);

dma_addr_t dw_dma_get_dst_addr(struct dma_chan *chan);

#endif /* _DMA_DW_H */
