#ifndef _ASM_DMA_MAPPING_H_
#define _ASM_DMA_MAPPING_H_

/*
 * IOMMU interface. See Documentation/DMA-mapping.txt and DMA-API.txt for
 * documentation.
 */

#include <linux/scatterlist.h>
#include <asm/io.h>
#include <asm/swiotlb.h>

extern dma_addr_t bad_dma_address;
extern int iommu_merge;
extern struct device fallback_dev;
extern int panic_on_overflow;
extern int forbid_dac;
extern int force_iommu;

struct dma_mapping_ops {
	int             (*mapping_error)(dma_addr_t dma_addr);
	void*           (*alloc_coherent)(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t gfp);
	void            (*free_coherent)(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle);
	dma_addr_t      (*map_single)(struct device *hwdev, phys_addr_t ptr,
				size_t size, int direction);
	/* like map_single, but doesn't check the device mask */
	dma_addr_t      (*map_simple)(struct device *hwdev, phys_addr_t ptr,
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

extern const struct dma_mapping_ops *dma_ops;

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	if (dma_ops->mapping_error)
		return dma_ops->mapping_error(dma_addr);

	return (dma_addr == bad_dma_address);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

void *dma_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flag);

void dma_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle);


extern int dma_supported(struct device *hwdev, u64 mask);
extern int dma_set_mask(struct device *dev, u64 mask);

static inline dma_addr_t
dma_map_single(struct device *hwdev, void *ptr, size_t size,
	       int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	return dma_ops->map_single(hwdev, virt_to_phys(ptr), size, direction);
}

static inline void
dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
		 int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->unmap_single)
		dma_ops->unmap_single(dev, addr, size, direction);
}

static inline int
dma_map_sg(struct device *hwdev, struct scatterlist *sg,
	   int nents, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	return dma_ops->map_sg(hwdev, sg, nents, direction);
}

static inline void
dma_unmap_sg(struct device *hwdev, struct scatterlist *sg, int nents,
	     int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->unmap_sg)
		dma_ops->unmap_sg(hwdev, sg, nents, direction);
}

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
	if (dma_ops->sync_single_range_for_cpu)
		dma_ops->sync_single_range_for_cpu(hwdev, dma_handle, offset,
						   size, direction);

	flush_write_buffers();
}

static inline void
dma_sync_single_range_for_device(struct device *hwdev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size,
				 int direction)
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
	if (dma_ops->sync_sg_for_device)
		dma_ops->sync_sg_for_device(hwdev, sg, nelems, direction);

	flush_write_buffers();
}

static inline dma_addr_t dma_map_page(struct device *dev, struct page *page,
				      size_t offset, size_t size,
				      int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	return dma_ops->map_single(dev, page_to_phys(page)+offset,
				   size, direction);
}

static inline void dma_unmap_page(struct device *dev, dma_addr_t addr,
				  size_t size, int direction)
{
	dma_unmap_single(dev, addr, size, direction);
}

static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	enum dma_data_direction dir)
{
	flush_write_buffers();
}

static inline int dma_get_cache_alignment(void)
{
	/* no easy way to get cache size on all x86, so return the
	 * maximum possible, to be safe */
	return boot_cpu_data.x86_clflush_size;
}

#define dma_is_consistent(d, h)	(1)

#ifdef CONFIG_X86_32
#  define ARCH_HAS_DMA_DECLARE_COHERENT_MEMORY
struct dma_coherent_mem {
	void		*virt_base;
	u32		device_base;
	int		size;
	int		flags;
	unsigned long	*bitmap;
};

extern int
dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
			    dma_addr_t device_addr, size_t size, int flags);

extern void
dma_release_declared_memory(struct device *dev);

extern void *
dma_mark_declared_memory_occupied(struct device *dev,
				  dma_addr_t device_addr, size_t size);
#endif /* CONFIG_X86_32 */
#endif
