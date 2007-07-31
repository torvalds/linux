#ifndef __ASM_SH_DMA_MAPPING_H
#define __ASM_SH_DMA_MAPPING_H

#include <linux/mm.h>
#include <asm/scatterlist.h>
#include <asm/io.h>

struct pci_dev;
extern void *consistent_alloc(struct pci_dev *hwdev, size_t size,
				    dma_addr_t *dma_handle);
extern void consistent_free(struct pci_dev *hwdev, size_t size,
				  void *vaddr, dma_addr_t dma_handle);

#define dma_supported(dev, mask)	(1)

static inline int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t flag)
{
	return consistent_alloc(NULL, size, dma_handle);
}

static inline void dma_free_coherent(struct device *dev, size_t size,
		       void *vaddr, dma_addr_t dma_handle)
{
	consistent_free(NULL, size, vaddr, dma_handle);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_is_consistent(d, h) (1)

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
				  enum dma_data_direction dir)
{
	dma_cache_wback_inv((unsigned long)vaddr, size);
}

static inline dma_addr_t dma_map_single(struct device *dev,
					void *ptr, size_t size,
					enum dma_data_direction dir)
{
#if defined(CONFIG_PCI) && !defined(CONFIG_SH_PCIDMA_NONCOHERENT)
	if (dev->bus == &pci_bus_type)
		return virt_to_phys(ptr);
#endif
	dma_cache_sync(dev, ptr, size, dir);

	return virt_to_phys(ptr);
}

#define dma_unmap_single(dev, addr, size, dir)	do { } while (0)

static inline int dma_map_sg(struct device *dev, struct scatterlist *sg,
			     int nents, enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < nents; i++) {
#if !defined(CONFIG_PCI) || defined(CONFIG_SH_PCIDMA_NONCOHERENT)
		dma_cache_sync(dev, page_address(sg[i].page) + sg[i].offset,
			       sg[i].length, dir);
#endif
		sg[i].dma_address = page_to_phys(sg[i].page) + sg[i].offset;
	}

	return nents;
}

#define dma_unmap_sg(dev, sg, nents, dir)	do { } while (0)

static inline dma_addr_t dma_map_page(struct device *dev, struct page *page,
				      unsigned long offset, size_t size,
				      enum dma_data_direction dir)
{
	return dma_map_single(dev, page_address(page) + offset, size, dir);
}

static inline void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
				  size_t size, enum dma_data_direction dir)
{
	dma_unmap_single(dev, dma_address, size, dir);
}

static inline void dma_sync_single(struct device *dev, dma_addr_t dma_handle,
				   size_t size, enum dma_data_direction dir)
{
#if defined(CONFIG_PCI) && !defined(CONFIG_SH_PCIDMA_NONCOHERENT)
	if (dev->bus == &pci_bus_type)
		return;
#endif
	dma_cache_sync(dev, phys_to_virt(dma_handle), size, dir);
}

static inline void dma_sync_single_range(struct device *dev,
					 dma_addr_t dma_handle,
					 unsigned long offset, size_t size,
					 enum dma_data_direction dir)
{
#if defined(CONFIG_PCI) && !defined(CONFIG_SH_PCIDMA_NONCOHERENT)
	if (dev->bus == &pci_bus_type)
		return;
#endif
	dma_cache_sync(dev, phys_to_virt(dma_handle) + offset, size, dir);
}

static inline void dma_sync_sg(struct device *dev, struct scatterlist *sg,
			       int nelems, enum dma_data_direction dir)
{
	int i;

	for (i = 0; i < nelems; i++) {
#if !defined(CONFIG_PCI) || defined(CONFIG_SH_PCIDMA_NONCOHERENT)
		dma_cache_sync(dev, page_address(sg[i].page) + sg[i].offset,
			       sg[i].length, dir);
#endif
		sg[i].dma_address = page_to_phys(sg[i].page) + sg[i].offset;
	}
}

static inline void dma_sync_single_for_cpu(struct device *dev,
					   dma_addr_t dma_handle, size_t size,
					   enum dma_data_direction dir)
{
	dma_sync_single(dev, dma_handle, size, dir);
}

static inline void dma_sync_single_for_device(struct device *dev,
					   dma_addr_t dma_handle, size_t size,
					   enum dma_data_direction dir)
{
	dma_sync_single(dev, dma_handle, size, dir);
}

static inline void dma_sync_sg_for_cpu(struct device *dev,
				       struct scatterlist *sg, int nelems,
				       enum dma_data_direction dir)
{
	dma_sync_sg(dev, sg, nelems, dir);
}

static inline void dma_sync_sg_for_device(struct device *dev,
				       struct scatterlist *sg, int nelems,
				       enum dma_data_direction dir)
{
	dma_sync_sg(dev, sg, nelems, dir);
}

static inline int dma_get_cache_alignment(void)
{
	/*
	 * Each processor family will define its own L1_CACHE_SHIFT,
	 * L1_CACHE_BYTES wraps to this, so this is always safe.
	 */
	return L1_CACHE_BYTES;
}

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	return dma_addr == 0;
}

#endif /* __ASM_SH_DMA_MAPPING_H */

