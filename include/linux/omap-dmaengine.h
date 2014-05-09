/*
 * OMAP DMA Engine support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LINUX_OMAP_DMAENGINE_H
#define __LINUX_OMAP_DMAENGINE_H

struct dma_chan;

#if defined(CONFIG_DMA_OMAP) || defined(CONFIG_DMA_OMAP_MODULE)
bool omap_dma_filter_fn(struct dma_chan *, void *);
#else
static inline bool omap_dma_filter_fn(struct dma_chan *c, void *d)
{
	return false;
}
#endif
#endif /* __LINUX_OMAP_DMAENGINE_H */
