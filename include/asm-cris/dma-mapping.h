#ifndef _ASM_CRIS_DMA_MAPPING_H
#define _ASM_CRIS_DMA_MAPPING_H

#include "scatterlist.h"

static inline int
dma_supported(struct device *dev, u64 mask)
{
	BUG();
	return 0;
}

static inline int
dma_set_mask(struct device *dev, u64 dma_mask)
{
	BUG();
	return 1;
}

static inline void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   int flag)
{
	BUG();
	return NULL;
}

static inline void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		    dma_addr_t dma_handle)
{
	BUG();
}

static inline dma_addr_t
dma_map_single(struct device *dev, void *cpu_addr, size_t size,
	       enum dma_data_direction direction)
{
	BUG();
	return 0;
}

static inline void
dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction direction)
{
	BUG();
}

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size,
	     enum dma_data_direction direction)
{
	BUG();
	return 0;
}

static inline void
dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	       enum dma_data_direction direction)
{
	BUG();
}

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	BUG();
	return 1;
}

static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	     enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_single(struct device *dev, dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_sg(struct device *dev, struct scatterlist *sg, int nelems,
	    enum dma_data_direction direction)
{
	BUG();
}

/* Now for the API extensions over the pci_ one */

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_is_consistent(d)	(1)

static inline int
dma_get_cache_alignment(void)
{
	/* no easy way to get cache size on all processors, so return
	 * the maximum possible, to be safe */
	return (1 << L1_CACHE_SHIFT_MAX);
}

static inline void
dma_sync_single_range(struct device *dev, dma_addr_t dma_handle,
		      unsigned long offset, size_t size,
		      enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_cache_sync(void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
	BUG();
}

#endif

