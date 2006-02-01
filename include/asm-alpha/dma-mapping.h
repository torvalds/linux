#ifndef _ALPHA_DMA_MAPPING_H
#define _ALPHA_DMA_MAPPING_H

#include <linux/config.h>

#ifdef CONFIG_PCI

#include <linux/pci.h>

#define dma_map_single(dev, va, size, dir)		\
		pci_map_single(alpha_gendev_to_pci(dev), va, size, dir)
#define dma_unmap_single(dev, addr, size, dir)		\
		pci_unmap_single(alpha_gendev_to_pci(dev), addr, size, dir)
#define dma_alloc_coherent(dev, size, addr, gfp)	\
		pci_alloc_consistent(alpha_gendev_to_pci(dev), size, addr)
#define dma_free_coherent(dev, size, va, addr)		\
		pci_free_consistent(alpha_gendev_to_pci(dev), size, va, addr)
#define dma_map_page(dev, page, off, size, dir)		\
		pci_map_page(alpha_gendev_to_pci(dev), page, off, size, dir)
#define dma_unmap_page(dev, addr, size, dir)		\
		pci_unmap_page(alpha_gendev_to_pci(dev), addr, size, dir)
#define dma_map_sg(dev, sg, nents, dir)			\
		pci_map_sg(alpha_gendev_to_pci(dev), sg, nents, dir)
#define dma_unmap_sg(dev, sg, nents, dir)		\
		pci_unmap_sg(alpha_gendev_to_pci(dev), sg, nents, dir)
#define dma_supported(dev, mask)			\
		pci_dma_supported(alpha_gendev_to_pci(dev), mask)
#define dma_mapping_error(addr)				\
		pci_dma_mapping_error(addr)

#else	/* no PCI - no IOMMU. */

struct scatterlist;
void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t gfp);
int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	       enum dma_data_direction direction);

#define dma_free_coherent(dev, size, va, addr)		\
		free_pages((unsigned long)va, get_order(size))
#define dma_supported(dev, mask)		(mask < 0x00ffffffUL ? 0 : 1)
#define dma_map_single(dev, va, size, dir)	virt_to_phys(va)
#define dma_map_page(dev, page, off, size, dir)	(page_to_pa(page) + off)

#define dma_unmap_single(dev, addr, size, dir)	do { } while (0)
#define dma_unmap_page(dev, addr, size, dir)	do { } while (0)
#define dma_unmap_sg(dev, sg, nents, dir)	do { } while (0)

#define dma_mapping_error(addr)  (0)

#endif	/* !CONFIG_PCI */

#define dma_alloc_noncoherent(d, s, h, f)	dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h)	dma_free_coherent(d, s, v, h)
#define dma_is_consistent(dev)			(1)

int dma_set_mask(struct device *dev, u64 mask);

#define dma_sync_single_for_cpu(dev, addr, size, dir)	  do { } while (0)
#define dma_sync_single_for_device(dev, addr, size, dir)  do { } while (0)
#define dma_sync_single_range(dev, addr, off, size, dir)  do { } while (0)
#define dma_sync_sg_for_cpu(dev, sg, nents, dir)	  do { } while (0)
#define dma_sync_sg_for_device(dev, sg, nents, dir)	  do { } while (0)
#define dma_cache_sync(va, size, dir)			  do { } while (0)

#define dma_get_cache_alignment()			  L1_CACHE_BYTES

#endif	/* _ALPHA_DMA_MAPPING_H */
