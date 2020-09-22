/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header is for implementations of dma_map_ops and related code.
 * It should not be included in drivers just using the DMA API.
 */
#ifndef _LINUX_DMA_MAP_OPS_H
#define _LINUX_DMA_MAP_OPS_H

#include <linux/dma-mapping.h>

struct dma_map_ops {
	void *(*alloc)(struct device *dev, size_t size,
			dma_addr_t *dma_handle, gfp_t gfp,
			unsigned long attrs);
	void (*free)(struct device *dev, size_t size, void *vaddr,
			dma_addr_t dma_handle, unsigned long attrs);
	struct page *(*alloc_pages)(struct device *dev, size_t size,
			dma_addr_t *dma_handle, enum dma_data_direction dir,
			gfp_t gfp);
	void (*free_pages)(struct device *dev, size_t size, struct page *vaddr,
			dma_addr_t dma_handle, enum dma_data_direction dir);
	void *(*alloc_noncoherent)(struct device *dev, size_t size,
			dma_addr_t *dma_handle, enum dma_data_direction dir,
			gfp_t gfp);
	void (*free_noncoherent)(struct device *dev, size_t size, void *vaddr,
			dma_addr_t dma_handle, enum dma_data_direction dir);
	int (*mmap)(struct device *, struct vm_area_struct *,
			void *, dma_addr_t, size_t, unsigned long attrs);

	int (*get_sgtable)(struct device *dev, struct sg_table *sgt,
			void *cpu_addr, dma_addr_t dma_addr, size_t size,
			unsigned long attrs);

	dma_addr_t (*map_page)(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction dir, unsigned long attrs);
	void (*unmap_page)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
	/*
	 * map_sg returns 0 on error and a value > 0 on success.
	 * It should never return a value < 0.
	 */
	int (*map_sg)(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir, unsigned long attrs);
	void (*unmap_sg)(struct device *dev, struct scatterlist *sg, int nents,
			enum dma_data_direction dir, unsigned long attrs);
	dma_addr_t (*map_resource)(struct device *dev, phys_addr_t phys_addr,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
	void (*unmap_resource)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir,
			unsigned long attrs);
	void (*sync_single_for_cpu)(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir);
	void (*sync_single_for_device)(struct device *dev,
			dma_addr_t dma_handle, size_t size,
			enum dma_data_direction dir);
	void (*sync_sg_for_cpu)(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir);
	void (*sync_sg_for_device)(struct device *dev, struct scatterlist *sg,
			int nents, enum dma_data_direction dir);
	void (*cache_sync)(struct device *dev, void *vaddr, size_t size,
			enum dma_data_direction direction);
	int (*dma_supported)(struct device *dev, u64 mask);
	u64 (*get_required_mask)(struct device *dev);
	size_t (*max_mapping_size)(struct device *dev);
	unsigned long (*get_merge_boundary)(struct device *dev);
};

#ifdef CONFIG_DMA_OPS
#include <asm/dma-mapping.h>

static inline const struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (dev->dma_ops)
		return dev->dma_ops;
	return get_arch_dma_ops(dev->bus);
}

static inline void set_dma_ops(struct device *dev,
			       const struct dma_map_ops *dma_ops)
{
	dev->dma_ops = dma_ops;
}
#else /* CONFIG_DMA_OPS */
static inline const struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return NULL;
}
static inline void set_dma_ops(struct device *dev,
			       const struct dma_map_ops *dma_ops)
{
}
#endif /* CONFIG_DMA_OPS */

#ifdef CONFIG_DMA_DECLARE_COHERENT
int dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
		dma_addr_t device_addr, size_t size);
int dma_alloc_from_dev_coherent(struct device *dev, ssize_t size,
		dma_addr_t *dma_handle, void **ret);
int dma_release_from_dev_coherent(struct device *dev, int order, void *vaddr);
int dma_mmap_from_dev_coherent(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, size_t size, int *ret);

void *dma_alloc_from_global_coherent(struct device *dev, ssize_t size,
		dma_addr_t *dma_handle);
int dma_release_from_global_coherent(int order, void *vaddr);
int dma_mmap_from_global_coherent(struct vm_area_struct *vma, void *cpu_addr,
		size_t size, int *ret);

#else
static inline int dma_declare_coherent_memory(struct device *dev,
		phys_addr_t phys_addr, dma_addr_t device_addr, size_t size)
{
	return -ENOSYS;
}
#define dma_alloc_from_dev_coherent(dev, size, handle, ret) (0)
#define dma_release_from_dev_coherent(dev, order, vaddr) (0)
#define dma_mmap_from_dev_coherent(dev, vma, vaddr, order, ret) (0)

static inline void *dma_alloc_from_global_coherent(struct device *dev,
		ssize_t size, dma_addr_t *dma_handle)
{
	return NULL;
}
static inline int dma_release_from_global_coherent(int order, void *vaddr)
{
	return 0;
}
static inline int dma_mmap_from_global_coherent(struct vm_area_struct *vma,
		void *cpu_addr, size_t size, int *ret)
{
	return 0;
}
#endif /* CONFIG_DMA_DECLARE_COHERENT */

#ifdef CONFIG_ARCH_HAS_SETUP_DMA_OPS
void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
		const struct iommu_ops *iommu, bool coherent);
#else
static inline void arch_setup_dma_ops(struct device *dev, u64 dma_base,
		u64 size, const struct iommu_ops *iommu, bool coherent)
{
}
#endif /* CONFIG_ARCH_HAS_SETUP_DMA_OPS */

#ifdef CONFIG_ARCH_HAS_TEARDOWN_DMA_OPS
void arch_teardown_dma_ops(struct device *dev);
#else
static inline void arch_teardown_dma_ops(struct device *dev)
{
}
#endif /* CONFIG_ARCH_HAS_TEARDOWN_DMA_OPS */

extern const struct dma_map_ops dma_dummy_ops;

#endif /* _LINUX_DMA_MAP_OPS_H */
