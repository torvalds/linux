#ifndef _ASM_IA64_DMA_MAPPING_H
#define _ASM_IA64_DMA_MAPPING_H

/*
 * Copyright (C) 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/machvec.h>

#define dma_alloc_coherent	platform_dma_alloc_coherent
#define dma_alloc_noncoherent	platform_dma_alloc_coherent	/* coherent mem. is cheap */
#define dma_free_coherent	platform_dma_free_coherent
#define dma_free_noncoherent	platform_dma_free_coherent
#define dma_map_single		platform_dma_map_single
#define dma_map_sg		platform_dma_map_sg
#define dma_unmap_single	platform_dma_unmap_single
#define dma_unmap_sg		platform_dma_unmap_sg
#define dma_sync_single_for_cpu	platform_dma_sync_single_for_cpu
#define dma_sync_sg_for_cpu	platform_dma_sync_sg_for_cpu
#define dma_sync_single_for_device platform_dma_sync_single_for_device
#define dma_sync_sg_for_device	platform_dma_sync_sg_for_device
#define dma_mapping_error	platform_dma_mapping_error

#define dma_map_page(dev, pg, off, size, dir)				\
	dma_map_single(dev, page_address(pg) + (off), (size), (dir))
#define dma_unmap_page(dev, dma_addr, size, dir)			\
	dma_unmap_single(dev, dma_addr, size, dir)

/*
 * Rest of this file is part of the "Advanced DMA API".  Use at your own risk.
 * See Documentation/DMA-API.txt for details.
 */

#define dma_sync_single_range_for_cpu(dev, dma_handle, offset, size, dir)	\
	dma_sync_single_for_cpu(dev, dma_handle, size, dir)
#define dma_sync_single_range_for_device(dev, dma_handle, offset, size, dir)	\
	dma_sync_single_for_device(dev, dma_handle, size, dir)

#define dma_supported		platform_dma_supported

static inline int
dma_set_mask (struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;
	*dev->dma_mask = mask;
	return 0;
}

extern int dma_get_cache_alignment(void);

static inline void
dma_cache_sync (void *vaddr, size_t size, enum dma_data_direction dir)
{
	/*
	 * IA-64 is cache-coherent, so this is mostly a no-op.  However, we do need to
	 * ensure that dma_cache_sync() enforces order, hence the mb().
	 */
	mb();
}

#define dma_is_consistent(dma_handle)	(1)	/* all we do is coherent memory... */

#endif /* _ASM_IA64_DMA_MAPPING_H */
