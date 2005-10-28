/*
 * This is based on both include/asm-sh/dma-mapping.h and
 * include/asm-ppc/pci.h
 */
#ifndef __ASM_PPC_DMA_MAPPING_H
#define __ASM_PPC_DMA_MAPPING_H

#include <linux/config.h>
/* need struct page definitions */
#include <linux/mm.h>
#include <asm/scatterlist.h>
#include <asm/io.h>

#ifdef CONFIG_NOT_COHERENT_CACHE
/*
 * DMA-consistent mapping functions for PowerPCs that don't support
 * cache snooping.  These allocate/free a region of uncached mapped
 * memory space for use with DMA devices.  Alternatively, you could
 * allocate the space "normally" and use the cache management functions
 * to ensure it is consistent.
 */
extern void *__dma_alloc_coherent(size_t size, dma_addr_t *handle, gfp_t gfp);
extern void __dma_free_coherent(size_t size, void *vaddr);
extern void __dma_sync(void *vaddr, size_t size, int direction);
extern void __dma_sync_page(struct page *page, unsigned long offset,
				 size_t size, int direction);
#define dma_cache_inv(_start,_size) \
	invalidate_dcache_range(_start, (_start + _size))
#define dma_cache_wback(_start,_size) \
	clean_dcache_range(_start, (_start + _size))
#define dma_cache_wback_inv(_start,_size) \
	flush_dcache_range(_start, (_start + _size))

#else /* ! CONFIG_NOT_COHERENT_CACHE */
/*
 * Cache coherent cores.
 */

#define dma_cache_inv(_start,_size)		do { } while (0)
#define dma_cache_wback(_start,_size)		do { } while (0)
#define dma_cache_wback_inv(_start,_size)	do { } while (0)

#define __dma_alloc_coherent(gfp, size, handle)	NULL
#define __dma_free_coherent(size, addr)		do { } while (0)
#define __dma_sync(addr, size, rw)		do { } while (0)
#define __dma_sync_page(pg, off, sz, rw)	do { } while (0)

#endif /* ! CONFIG_NOT_COHERENT_CACHE */

#define dma_supported(dev, mask)	(1)

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t * dma_handle,
				       gfp_t gfp)
{
#ifdef CONFIG_NOT_COHERENT_CACHE
	return __dma_alloc_coherent(size, dma_handle, gfp);
#else
	void *ret;
	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || dev->coherent_dma_mask < 0xffffffff)
		gfp |= GFP_DMA;

	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}

	return ret;
#endif
}

static inline void
dma_free_coherent(struct device *dev, size_t size, void *vaddr,
		  dma_addr_t dma_handle)
{
#ifdef CONFIG_NOT_COHERENT_CACHE
	__dma_free_coherent(size, vaddr);
#else
	free_pages((unsigned long)vaddr, get_order(size));
#endif
}

static inline dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	__dma_sync(ptr, size, direction);

	return virt_to_bus(ptr);
}

/* We do nothing. */
#define dma_unmap_single(dev, addr, size, dir)	do { } while (0)

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size,
	     enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	__dma_sync_page(page, offset, size, direction);

	return page_to_bus(page) + offset;
}

/* We do nothing. */
#define dma_unmap_page(dev, handle, size, dir)	do { } while (0)

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nents; i++, sg++) {
		BUG_ON(!sg->page);
		__dma_sync_page(sg->page, sg->offset, sg->length, direction);
		sg->dma_address = page_to_bus(sg->page) + sg->offset;
	}

	return nents;
}

/* We don't do anything here. */
#define dma_unmap_sg(dev, sg, nents, dir)	do { } while (0)

static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
			size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	__dma_sync(bus_to_virt(dma_handle), size, direction);
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
			size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	__dma_sync(bus_to_virt(dma_handle), size, direction);
}

static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nents,
		    enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nents; i++, sg++)
		__dma_sync_page(sg->page, sg->offset, sg->length, direction);
}

static inline void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nents,
		    enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nents; i++, sg++)
		__dma_sync_page(sg->page, sg->offset, sg->length, direction);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#ifdef CONFIG_NOT_COHERENT_CACHE
#define dma_is_consistent(d)	(0)
#else
#define dma_is_consistent(d)	(1)
#endif

static inline int dma_get_cache_alignment(void)
{
	/*
	 * Each processor family will define its own L1_CACHE_SHIFT,
	 * L1_CACHE_BYTES wraps to this, so this is always safe.
	 */
	return L1_CACHE_BYTES;
}

static inline void
dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
		      	      unsigned long offset, size_t size,
		      	      enum dma_data_direction direction)
{
	/* just sync everything for now */
	dma_sync_single_for_cpu(dev, dma_handle, offset + size, direction);
}

static inline void
dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
		    		 unsigned long offset, size_t size,
				 enum dma_data_direction direction)
{
	/* just sync everything for now */
	dma_sync_single_for_device(dev, dma_handle, offset + size, direction);
}

static inline void dma_cache_sync(void *vaddr, size_t size,
				  enum dma_data_direction direction)
{
	__dma_sync(vaddr, size, (int)direction);
}

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	return 0;
}

#endif				/* __ASM_PPC_DMA_MAPPING_H */
