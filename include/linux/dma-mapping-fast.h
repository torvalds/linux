/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2019, 2021, The Linux Foundation. All rights reserved.
 */

#ifndef __LINUX_DMA_MAPPING_FAST_H
#define __LINUX_DMA_MAPPING_FAST_H

#include <linux/iommu.h>
#include <linux/io-pgtable-fast.h>
#include <linux/rbtree.h>

struct dma_iommu_mapping;
struct io_pgtable_ops;
struct iova_domain;

struct dma_fast_smmu_mapping {
	struct device		*dev;
	struct iommu_domain	*domain;
	struct iova_domain	*iovad;

	dma_addr_t	 base;
	size_t		 size;
	size_t		 num_4k_pages;

	unsigned int	bitmap_size;
	/* bitmap has 1s marked only valid mappings */
	unsigned long	*bitmap;
	/* clean_bitmap has 1s marked for both valid and stale tlb mappings */
	unsigned long	*clean_bitmap;
	unsigned long	next_start;
	bool		have_stale_tlbs;

	dma_addr_t	pgtbl_dma_handle;
	struct io_pgtable_ops *pgtbl_ops;

	spinlock_t	lock;
	struct notifier_block notifier;
	struct rb_node node;
};

#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST
int fast_smmu_init_mapping(struct device *dev, struct iommu_domain *domain,
			   struct io_pgtable_ops *pgtable_ops);
void fast_smmu_put_dma_cookie(struct iommu_domain *domain);
void fast_smmu_setup_dma_ops(struct device *dev, u64 dma_base, u64 size);
int __init dma_mapping_fast_init(void);
void dma_mapping_fast_exit(void);
#else
static inline int fast_smmu_init_mapping(struct device *dev,
					 struct iommu_domain *domain,
					 struct io_pgtable_ops *pgtable_ops)
{
	return -ENODEV;
}

static inline void fast_smmu_put_dma_cookie(struct iommu_domain *domain) {}
static inline void fast_smmu_setup_dma_ops(struct device *dev, u64 dma_base, u64 size) {}

static inline int __init dma_mapping_fast_init(void)
{
	return 0;
}

static inline void dma_mapping_fast_exit(void) {}
#endif

#endif /* __LINUX_DMA_MAPPING_FAST_H */
