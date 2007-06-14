#ifndef _ASM_SPARC64_DMA_MAPPING_H
#define _ASM_SPARC64_DMA_MAPPING_H


#ifdef CONFIG_PCI

/* we implement the API below in terms of the existing PCI one,
 * so include it */
#include <linux/pci.h>
/* need struct page definitions */
#include <linux/mm.h>

#include <asm/of_device.h>

static inline int
dma_supported(struct device *dev, u64 mask)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return pci_dma_supported(to_pci_dev(dev), mask);
}

static inline int
dma_set_mask(struct device *dev, u64 dma_mask)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return pci_set_dma_mask(to_pci_dev(dev), dma_mask);
}

static inline void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   gfp_t flag)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return pci_iommu_ops->alloc_consistent(to_pci_dev(dev), size, dma_handle, flag);
}

static inline void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		    dma_addr_t dma_handle)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_free_consistent(to_pci_dev(dev), size, cpu_addr, dma_handle);
}

static inline dma_addr_t
dma_map_single(struct device *dev, void *cpu_addr, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return pci_map_single(to_pci_dev(dev), cpu_addr, size, (int)direction);
}

static inline void
dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		 enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_unmap_single(to_pci_dev(dev), dma_addr, size, (int)direction);
}

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size,
	     enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return pci_map_page(to_pci_dev(dev), page, offset, size, (int)direction);
}

static inline void
dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_unmap_page(to_pci_dev(dev), dma_address, size, (int)direction);
}

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	return pci_map_sg(to_pci_dev(dev), sg, nents, (int)direction);
}

static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	     enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_unmap_sg(to_pci_dev(dev), sg, nhwentries, (int)direction);
}

static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle, size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_dma_sync_single_for_cpu(to_pci_dev(dev), dma_handle,
				    size, (int)direction);
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle, size_t size,
			   enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_dma_sync_single_for_device(to_pci_dev(dev), dma_handle,
				       size, (int)direction);
}

static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
		    enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_dma_sync_sg_for_cpu(to_pci_dev(dev), sg, nelems, (int)direction);
}

static inline void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nelems,
		       enum dma_data_direction direction)
{
	BUG_ON(dev->bus != &pci_bus_type);

	pci_dma_sync_sg_for_device(to_pci_dev(dev), sg, nelems, (int)direction);
}

static inline int
dma_mapping_error(dma_addr_t dma_addr)
{
	return pci_dma_mapping_error(dma_addr);
}

#else

struct device;
struct page;
struct scatterlist;

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
	return 0;
}

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t flag)
{
	BUG();
	return NULL;
}

static inline void dma_free_coherent(struct device *dev, size_t size,
		       void *vaddr, dma_addr_t dma_handle)
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
	return 0;
}

static inline void
dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	     enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle, size_t size,
			enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle, size_t size,
			   enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
		    enum dma_data_direction direction)
{
	BUG();
}

static inline void
dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nelems,
		       enum dma_data_direction direction)
{
	BUG();
}

static inline int
dma_mapping_error(dma_addr_t dma_addr)
{
	BUG();
	return 0;
}

#endif /* PCI */


/* Now for the API extensions over the pci_ one */

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_is_consistent(d, h)	(1)

static inline int
dma_get_cache_alignment(void)
{
	/* no easy way to get cache size on all processors, so return
	 * the maximum possible, to be safe */
	return (1 << INTERNODE_CACHE_SHIFT);
}

static inline void
dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
			      unsigned long offset, size_t size,
			      enum dma_data_direction direction)
{
	/* just sync everything, that's all the pci API can do */
	dma_sync_single_for_cpu(dev, dma_handle, offset+size, direction);
}

static inline void
dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size,
				 enum dma_data_direction direction)
{
	/* just sync everything, that's all the pci API can do */
	dma_sync_single_for_device(dev, dma_handle, offset+size, direction);
}

static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	       enum dma_data_direction direction)
{
	/* could define this in terms of the dma_cache ... operations,
	 * but if you get this on a platform, you should convert the platform
	 * to using the generic device DMA API */
	BUG();
}

#endif /* _ASM_SPARC64_DMA_MAPPING_H */
