/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006, 07  Ralf Baechle <ralf@linux-mips.org>
 * Copyright (C) 2007 Lemote, Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 */
#ifndef __ASM_MACH_LEMOTE_DMA_COHERENCE_H
#define __ASM_MACH_LEMOTE_DMA_COHERENCE_H

struct device;

static inline dma_addr_t plat_map_dma_mem(struct device *dev, void *addr,
					  size_t size)
{
	return virt_to_phys(addr) | 0x80000000;
}

static inline dma_addr_t plat_map_dma_mem_page(struct device *dev,
					       struct page *page)
{
	return page_to_phys(page) | 0x80000000;
}

static inline unsigned long plat_dma_addr_to_phys(dma_addr_t dma_addr)
{
	return dma_addr & 0x7fffffff;
}

static inline void plat_unmap_dma_mem(dma_addr_t dma_addr)
{
}

static inline int plat_device_is_coherent(struct device *dev)
{
	return 0;
}

#endif /* __ASM_MACH_LEMOTE_DMA_COHERENCE_H */
