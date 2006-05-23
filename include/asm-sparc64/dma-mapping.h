#ifndef _ASM_SPARC64_DMA_MAPPING_H
#define _ASM_SPARC64_DMA_MAPPING_H

#include <linux/config.h>

#ifdef CONFIG_PCI

/* we implement the API below in terms of the existing PCI one,
 * so include it */
#include <linux/pci.h>
/* need struct page definitions */
#include <linux/mm.h>

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

#endif /* PCI */

#endif /* _ASM_SPARC64_DMA_MAPPING_H */
