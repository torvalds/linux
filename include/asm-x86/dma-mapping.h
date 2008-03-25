#ifndef _ASM_DMA_MAPPING_H_
#define _ASM_DMA_MAPPING_H_

/*
 * IOMMU interface. See Documentation/DMA-mapping.txt and DMA-API.txt for
 * documentation.
 */

#include <linux/scatterlist.h>
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

#ifdef CONFIG_X86_32
# include "dma-mapping_32.h"
#else
# include "dma-mapping_64.h"
#endif

#endif
