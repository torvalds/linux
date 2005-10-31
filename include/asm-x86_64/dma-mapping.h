#ifndef _X8664_DMA_MAPPING_H
#define _X8664_DMA_MAPPING_H 1

/*
 * IOMMU interface. See Documentation/DMA-mapping.txt and DMA-API.txt for
 * documentation.
 */

#include <linux/config.h>

#include <asm/scatterlist.h>
#include <asm/io.h>
#include <asm/swiotlb.h>

extern dma_addr_t bad_dma_address;
#define dma_mapping_error(x) \
	(swiotlb ? swiotlb_dma_mapping_error(x) : ((x) == bad_dma_address))

void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
			 gfp_t gfp);
void dma_free_coherent(struct device *dev, size_t size, void *vaddr,
			 dma_addr_t dma_handle);

#ifdef CONFIG_GART_IOMMU

extern dma_addr_t dma_map_single(struct device *hwdev, void *ptr, size_t size,
				 int direction);
extern void dma_unmap_single(struct device *dev, dma_addr_t addr,size_t size,
			     int direction);

#else

/* No IOMMU */

static inline dma_addr_t dma_map_single(struct device *hwdev, void *ptr,
					size_t size, int direction)
{
	dma_addr_t addr;

	if (direction == DMA_NONE)
		out_of_line_bug();
	addr = virt_to_bus(ptr);

	if ((addr+size) & ~*hwdev->dma_mask)
		out_of_line_bug();
	return addr;
}

static inline void dma_unmap_single(struct device *hwdev, dma_addr_t dma_addr,
				    size_t size, int direction)
{
	if (direction == DMA_NONE)
		out_of_line_bug();
	/* Nothing to do */
}

#endif

#define dma_map_page(dev,page,offset,size,dir) \
	dma_map_single((dev), page_address(page)+(offset), (size), (dir))

static inline void dma_sync_single_for_cpu(struct device *hwdev,
					       dma_addr_t dma_handle,
					       size_t size, int direction)
{
	if (direction == DMA_NONE)
		out_of_line_bug();

	if (swiotlb)
		return swiotlb_sync_single_for_cpu(hwdev,dma_handle,size,direction);

	flush_write_buffers();
}

static inline void dma_sync_single_for_device(struct device *hwdev,
						  dma_addr_t dma_handle,
						  size_t size, int direction)
{
        if (direction == DMA_NONE)
		out_of_line_bug();

	if (swiotlb)
		return swiotlb_sync_single_for_device(hwdev,dma_handle,size,direction);

	flush_write_buffers();
}

static inline void dma_sync_single_range_for_cpu(struct device *hwdev,
						 dma_addr_t dma_handle,
						 unsigned long offset,
						 size_t size, int direction)
{
	if (direction == DMA_NONE)
		out_of_line_bug();

	if (swiotlb)
		return swiotlb_sync_single_range_for_cpu(hwdev,dma_handle,offset,size,direction);

	flush_write_buffers();
}

static inline void dma_sync_single_range_for_device(struct device *hwdev,
						    dma_addr_t dma_handle,
						    unsigned long offset,
						    size_t size, int direction)
{
        if (direction == DMA_NONE)
		out_of_line_bug();

	if (swiotlb)
		return swiotlb_sync_single_range_for_device(hwdev,dma_handle,offset,size,direction);

	flush_write_buffers();
}

static inline void dma_sync_sg_for_cpu(struct device *hwdev,
				       struct scatterlist *sg,
				       int nelems, int direction)
{
	if (direction == DMA_NONE)
		out_of_line_bug();

	if (swiotlb)
		return swiotlb_sync_sg_for_cpu(hwdev,sg,nelems,direction);

	flush_write_buffers();
}

static inline void dma_sync_sg_for_device(struct device *hwdev,
					  struct scatterlist *sg,
					  int nelems, int direction)
{
	if (direction == DMA_NONE)
		out_of_line_bug();

	if (swiotlb)
		return swiotlb_sync_sg_for_device(hwdev,sg,nelems,direction);

	flush_write_buffers();
}

extern int dma_map_sg(struct device *hwdev, struct scatterlist *sg,
		      int nents, int direction);
extern void dma_unmap_sg(struct device *hwdev, struct scatterlist *sg,
			 int nents, int direction);

#define dma_unmap_page dma_unmap_single

extern int dma_supported(struct device *hwdev, u64 mask);
extern int dma_get_cache_alignment(void);
#define dma_is_consistent(h) 1

static inline int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;
	*dev->dma_mask = mask;
	return 0;
}

static inline void dma_cache_sync(void *vaddr, size_t size, enum dma_data_direction dir)
{
	flush_write_buffers();
}

#endif
