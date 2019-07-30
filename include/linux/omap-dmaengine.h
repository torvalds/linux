/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OMAP DMA Engine support
 */
#ifndef __LINUX_OMAP_DMAENGINE_H
#define __LINUX_OMAP_DMAENGINE_H

struct dma_chan;

#if defined(CONFIG_DMA_OMAP) || (defined(CONFIG_DMA_OMAP_MODULE) && defined(MODULE))
bool omap_dma_filter_fn(struct dma_chan *, void *);
#else
static inline bool omap_dma_filter_fn(struct dma_chan *c, void *d)
{
	return false;
}
#endif
#endif /* __LINUX_OMAP_DMAENGINE_H */
