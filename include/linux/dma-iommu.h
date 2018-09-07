/*
 * Copyright (C) 2014-2015 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __DMA_IOMMU_H
#define __DMA_IOMMU_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/errno.h>

#ifdef CONFIG_IOMMU_DMA
#include <linux/iommu.h>

int iommu_dma_init(void);

/* Domain management interface for IOMMU drivers */
int iommu_get_dma_cookie(struct iommu_domain *domain);
void iommu_put_dma_cookie(struct iommu_domain *domain);

/* Setup call for arch DMA mapping code */
int iommu_dma_init_domain(struct iommu_domain *domain, dma_addr_t base, u64 size);

/* General helpers for DMA-API <-> IOMMU-API interaction */
int dma_direction_to_prot(enum dma_data_direction dir, bool coherent);

/*
 * These implement the bulk of the relevant DMA mapping callbacks, but require
 * the arch code to take care of attributes and cache maintenance
 */
struct page **iommu_dma_alloc(struct device *dev, size_t size,
		gfp_t gfp, int prot, dma_addr_t *handle,
		void (*flush_page)(struct device *, const void *, phys_addr_t));
void iommu_dma_free(struct device *dev, struct page **pages, size_t size,
		dma_addr_t *handle);

int iommu_dma_mmap(struct page **pages, size_t size, struct vm_area_struct *vma);

dma_addr_t iommu_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, int prot);
int iommu_dma_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, int prot);

/*
 * Arch code with no special attribute handling may use these
 * directly as DMA mapping callbacks for simplicity
 */
void iommu_dma_unmap_page(struct device *dev, dma_addr_t handle, size_t size,
		enum dma_data_direction dir, struct dma_attrs *attrs);
void iommu_dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, struct dma_attrs *attrs);
int iommu_dma_supported(struct device *dev, u64 mask);
int iommu_dma_mapping_error(struct device *dev, dma_addr_t dma_addr);

bool common_iommu_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
				  const struct iommu_ops *ops);
void common_iommu_teardown_dma_ops(struct device *dev);

#else

struct iommu_domain;

static inline int iommu_dma_init(void)
{
	return 0;
}

static inline int iommu_get_dma_cookie(struct iommu_domain *domain)
{
	return -ENODEV;
}

static inline void iommu_put_dma_cookie(struct iommu_domain *domain)
{
}

static inline bool common_iommu_setup_dma_ops(struct device *dev, u64 dma_base,
					u64 size, const struct iommu_ops *ops)
{
	return false;
}

static inline void common_iommu_teardown_dma_ops(struct device *dev)
{
}

#endif	/* CONFIG_IOMMU_DMA */
#endif	/* __KERNEL__ */
#endif	/* __DMA_IOMMU_H */
