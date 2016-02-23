/*
 * TI EDMA DMA engine driver
 *
 * Copyright 2012 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __LINUX_EDMA_H
#define __LINUX_EDMA_H

struct dma_chan;

#if defined(CONFIG_TI_EDMA) || defined(CONFIG_TI_EDMA_MODULE)
bool edma_filter_fn(struct dma_chan *, void *);
#else
static inline bool edma_filter_fn(struct dma_chan *chan, void *param)
{
	return false;
}
#endif

#endif
