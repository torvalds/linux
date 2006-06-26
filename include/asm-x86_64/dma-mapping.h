#ifndef _X8664_DMA_MAPPING_H
#define _X8664_DMA_MAPPING_H 1

/*
 * IOMMU interface. See Documentation/DMA-mapping.txt and DMA-API.txt for
 * documentation.
 */


#include <asm/scatterlist.h>
#include <asm/io.h>
#include <asm/swiotlb.h>

struct dma_mapping_ops {
	int             (*mapping_error)(dma_addr_t dma_addr);
	void*           (*alloc_coherent)(struct device *dev, size_t size,
                                dma_addr_t *dma_handle, gfp_t gfp);
	void            (*free_coherent)(struct device *dev, size_t size,
                                void *vaddr, dma_addr_t dma_handle);
	dma_addr_t      (*map_single)(struct device *hwdev, void *ptr,
                                size_t size, int direction);
	/* like map_single, but doesn't check the device mask */
	dma_addr_t      (*map_simple)(struct device *hwdev, char *ptr,
                                size_t size, int direction);
	void            (*unmap_single)(struct device *dev, dma_addr_t addr,
		                size_t size, int direction);
	void            (*sync_single_for_cpu)(struct device *hwdev,
		                dma_addr_t dma_handle, size_t size,
				int direction);
	void            (*sync_single_for_device)(struct device *hwdev,
                                dma_addr_t dma_handle, size_t size,
				int direction);
	void            (*sync_single_range_for_cpu)(struct device *hwdev,
                                dma_addr_t dma_handle, unsigned long offset,
		                size_t size, int direction);
	void            (*sync_single_range_for_device)(struct device *hwdev,
				dma_addr_t dma_handle, unsigned long offset,
		                size_t size, int direction);
	void            (*sync_sg_for_cpu)(struct device *hwdev,
                                struct scatterlist *sg, int nelems,
				int direction);
	void            (*sync_sg_for_device)(struct device *hwdev,
				struct scatterlist *sg, int nelems,
				int direction);
	int             (*map_sg)(struct device *hwdev, struct scatterlist *sg,
		                int nents, int direction);
	void            (*unmap_sg)(struct device *hwdev,
				struct scatterlist *sg, int nents,
				int direction);
	int             (*dma_supported)(struct device *hwdev, u64 mask);
	int		is_phys;
};

extern dma_addr_t bad_dma_address;
extern struct dma_mapping_ops* dma_ops;
extern int iommu_merge;

static inline int valid_dma_direction(int dma_direction)
{
	return ((dma_direction == DMA_BIDIRECTIONAL) ||
		(dma_direction == DMA_TO_DEVICE) ||
		(dma_direction == DMA_FROM_DEVICE));
}

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	if (dma_ops->mapping_error)
		return dma_ops->mapping_error(dma_addr);

	return (dma_addr == bad_dma_address);
}

extern void *dma_alloc_coherent(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t gfp);
extern void dma_free_coherent(struct device *dev, size_t size, void *vaddr,
			      dma_addr_t dma_handle);

static inline dma_addr_t
dma_map_single(struct device *hwdev, void *ptr, size_t size,
	       int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	return dma_ops->map_single(hwdev, ptr, size, direction);
}

static inline void
dma_unmap_single(struct device *dev, dma_addr_t addr,size_t size,
		 int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	dma_ops->unmap_single(dev, addr, size, direction);
}

#define dma_map_page(dev,page,offset,size,dir) \
	dma_map_single((dev), page_address(page)+(offset), (size), (dir))

#define dma_unmap_page dma_unmap_single

static inline void
dma_sync_single_for_cpu(struct device *hwdev, dma_addr_t dma_handle,
			size_t size, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_single_for_cpu)
		dma_ops->sync_single_for_cpu(hwdev, dma_handle, size,
					     direction);
	flush_write_buffers();
}

static inline void
dma_sync_single_for_device(struct device *hwdev, dma_addr_t dma_handle,
			   size_t size, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_single_for_device)
		dma_ops->sync_single_for_device(hwdev, dma_handle, size,
						direction);
	flush_write_buffers();
}

static inline void
dma_sync_single_range_for_cpu(struct device *hwdev, dma_addr_t dma_handle,
			      unsigned long offset, size_t size, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_single_range_for_cpu) {
		dma_ops->sync_single_range_for_cpu(hwdev, dma_handle, offset, size, direction);
	}

	flush_write_buffers();
}

static inline void
dma_sync_single_range_for_device(struct device *hwdev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_single_range_for_device)
		dma_ops->sync_single_range_for_device(hwdev, dma_handle,
						      offset, size, direction);

	flush_write_buffers();
}

static inline void
dma_sync_sg_for_cpu(struct device *hwdev, struct scatterlist *sg,
		    int nelems, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_sg_for_cpu)
		dma_ops->sync_sg_for_cpu(hwdev, sg, nelems, direction);
	flush_write_buffers();
}

static inline void
dma_sync_sg_for_device(struct device *hwdev, struct scatterlist *sg,
		       int nelems, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_sg_for_device) {
		dma_ops->sync_sg_for_device(hwdev, sg, nelems, direction);
	}

	flush_write_buffers();
}

static inline int
dma_map_sg(struct device *hwdev, struct scatterlist *sg, int nents, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	return dma_ops->map_sg(hwdev, sg, nents, direction);
}

static inline void
dma_unmap_sg(struct device *hwdev, struct scatterlist *sg, int nents,
	     int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	dma_ops->unmap_sg(hwdev, sg, nents, direction);
}

extern int dma_supported(struct device *hwdev, u64 mask);

/* same for gart, swiotlb, and nommu */
static inline int dma_get_cache_alignment(void)
{
	return boot_cpu_data.x86_clflush_size;
}

#define dma_is_consistent(h) 1

extern int dma_set_mask(struct device *dev, u64 mask);

static inline void
dma_cache_sync(void *vaddr, size_t size, enum dma_data_direction dir)
{
	flush_write_buffers();
}

extern struct device fallback_dev;
extern int panic_on_overflow;

#endif /* _X8664_DMA_MAPPING_H */
