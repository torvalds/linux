/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 *
 * DMA operations that map physical memory through IOMMU.
 */
#ifndef _LINUX_IOMMU_DMA_H
#define _LINUX_IOMMU_DMA_H

#include <linux/dma-direction.h>

#ifdef CONFIG_IOMMU_DMA
static inline bool use_dma_iommu(struct device *dev)
{
	return dev->dma_iommu;
}
#else
static inline bool use_dma_iommu(struct device *dev)
{
	return false;
}
#endif /* CONFIG_IOMMU_DMA */

dma_addr_t iommu_dma_map_phys(struct device *dev, phys_addr_t phys, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
void iommu_dma_unmap_phys(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir, unsigned long attrs);
int iommu_dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, unsigned long attrs);
void iommu_dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, unsigned long attrs);
void *iommu_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		gfp_t gfp, unsigned long attrs);
int iommu_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int iommu_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
unsigned long iommu_dma_get_merge_boundary(struct device *dev);
size_t iommu_dma_opt_mapping_size(void);
size_t iommu_dma_max_mapping_size(struct device *dev);
void iommu_dma_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t handle, unsigned long attrs);
struct sg_table *iommu_dma_alloc_noncontiguous(struct device *dev, size_t size,
		enum dma_data_direction dir, gfp_t gfp, unsigned long attrs);
void iommu_dma_free_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir);
void *iommu_dma_vmap_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt);
#define iommu_dma_vunmap_noncontiguous(dev, vaddr) \
	vunmap(vaddr);
int iommu_dma_mmap_noncontiguous(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct sg_table *sgt);
void iommu_dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir);
void iommu_dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir);
void iommu_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sgl,
		int nelems, enum dma_data_direction dir);
void iommu_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
		int nelems, enum dma_data_direction dir);

#endif /* _LINUX_IOMMU_DMA_H */
