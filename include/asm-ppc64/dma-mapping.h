/* Copyright (C) 2004 IBM
 *
 * Implements the generic device dma API for ppc64. Handles
 * the pci and vio busses
 */

#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <linux/types.h>
#include <linux/cache.h>
/* need struct page definitions */
#include <linux/mm.h>
#include <asm/scatterlist.h>
#include <asm/bug.h>

#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)

extern int dma_supported(struct device *dev, u64 mask);
extern int dma_set_mask(struct device *dev, u64 dma_mask);
extern void *dma_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t flag);
extern void dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_handle);
extern dma_addr_t dma_map_single(struct device *dev, void *cpu_addr,
		size_t size, enum dma_data_direction direction);
extern void dma_unmap_single(struct device *dev, dma_addr_t dma_addr,
		size_t size, enum dma_data_direction direction);
extern dma_addr_t dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size,
		enum dma_data_direction direction);
extern void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
		size_t size, enum dma_data_direction direction);
extern int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction direction);
extern void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
		int nhwentries, enum dma_data_direction direction);

static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle, size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	/* nothing to do */
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle, size_t size,
			   enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	/* nothing to do */
}

static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
		    enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	/* nothing to do */
}

static inline void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nelems,
		       enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	/* nothing to do */
}

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	return (dma_addr == DMA_ERROR_CODE);
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
dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
			      unsigned long offset, size_t size,
			      enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	/* nothing to do */
}

static inline void
dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size,
				 enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	/* nothing to do */
}

static inline void
dma_cache_sync(void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	/* nothing to do */
}

/*
 * DMA operations are abstracted for G5 vs. i/pSeries, PCI vs. VIO
 */
struct dma_mapping_ops {
	void *		(*alloc_coherent)(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t flag);
	void		(*free_coherent)(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle);
	dma_addr_t	(*map_single)(struct device *dev, void *ptr,
				size_t size, enum dma_data_direction direction);
	void		(*unmap_single)(struct device *dev, dma_addr_t dma_addr,
				size_t size, enum dma_data_direction direction);
	int		(*map_sg)(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction direction);
	void		(*unmap_sg)(struct device *dev, struct scatterlist *sg,
				int nents, enum dma_data_direction direction);
	int		(*dma_supported)(struct device *dev, u64 mask);
	int		(*dac_dma_supported)(struct device *dev, u64 mask);
};

#endif	/* _ASM_DMA_MAPPING_H */
