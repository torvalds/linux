/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 */

#ifndef __LINUX_IO_PGTABLE_FAST_H
#define __LINUX_IO_PGTABLE_FAST_H

#include <linux/notifier.h>
#include <linux/io-pgtable.h>
/*
 * This ought to be private to io-pgtable-fast, but dma-mapping-fast
 * currently requires it for a debug usecase.
 */
typedef u64 av8l_fast_iopte;

struct io_pgtable_ops;
struct scatterlist;

struct av8l_fast_io_pgtable {
	struct io_pgtable	  iop;
	av8l_fast_iopte		 *pgd;
	av8l_fast_iopte		 *puds[4];
	av8l_fast_iopte		 *pmds;
	struct page		**pages; /* page table memory */
	int			  nr_pages;
	dma_addr_t		  base;
	dma_addr_t		  end;
};

/* Struct accessors */
#define iof_pgtable_to_data(x)						\
	container_of((x), struct av8l_fast_io_pgtable, iop)

#define iof_pgtable_ops_to_pgtable(x)					\
	container_of((x), struct io_pgtable, ops)

#define iof_pgtable_ops_to_data(x)					\
	iof_pgtable_to_data(iof_pgtable_ops_to_pgtable(x))


#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST

int av8l_fast_map_public(struct io_pgtable_ops *ops, unsigned long iova,
			 phys_addr_t paddr, size_t size, int prot);

void av8l_fast_unmap_public(struct io_pgtable_ops *ops, unsigned long iova,
				size_t size);

int av8l_fast_map_sg_public(struct io_pgtable_ops *ops,
			unsigned long iova, struct scatterlist *sgl,
			unsigned int nents, int prot, size_t *size);

bool av8l_fast_iova_coherent_public(struct io_pgtable_ops *ops,
					unsigned long iova);

phys_addr_t av8l_fast_iova_to_phys_public(struct io_pgtable_ops *ops,
					  unsigned long iova);
#else
static inline int
av8l_fast_map_public(struct io_pgtable_ops *ops, unsigned long iova,
		     phys_addr_t paddr, size_t size, int prot)
{
	return -EINVAL;
}
static inline void av8l_fast_unmap_public(struct io_pgtable_ops *ops,
					  unsigned long iova, size_t size)
{
}

static inline int av8l_fast_map_sg_public(struct io_pgtable_ops *ops,
				unsigned long iova, struct scatterlist *sgl,
				unsigned int nents, int prot, size_t *size)
{
	return 0;
}

static inline bool av8l_fast_iova_coherent_public(struct io_pgtable_ops *ops,
						  unsigned long iova)
{
	return false;
}
static inline phys_addr_t
av8l_fast_iova_to_phys_public(struct io_pgtable_ops *ops,
				  unsigned long iova)
{
	return 0;
}
#endif /* CONFIG_IOMMU_IO_PGTABLE_FAST */


/* events for notifiers passed to av8l_register_notify */
#define MAPPED_OVER_STALE_TLB 1


#ifdef CONFIG_IOMMU_IO_PGTABLE_FAST_PROVE_TLB
/*
 * Doesn't matter what we use as long as bit 0 is unset.  The reason why we
 * need a different value at all is that there are certain hardware
 * platforms with erratum that require that a PTE actually be zero'd out
 * and not just have its valid bit unset.
 */
#define AV8L_FAST_PTE_UNMAPPED_NEED_TLBI 0xa

void av8l_fast_clear_stale_ptes(struct io_pgtable_ops *ops, u64 base, u64 end,
				bool skip_sync);
void av8l_register_notify(struct notifier_block *nb);

#else  /* !CONFIG_IOMMU_IO_PGTABLE_FAST_PROVE_TLB */

#define AV8L_FAST_PTE_UNMAPPED_NEED_TLBI 0

static inline void av8l_fast_clear_stale_ptes(struct io_pgtable_ops *ops,
					      u64 base,
					      u64 end,
					      bool skip_sync)
{
}

static inline void av8l_register_notify(struct notifier_block *nb)
{
}

#endif	/* CONFIG_IOMMU_IO_PGTABLE_FAST_PROVE_TLB */

#endif /* __LINUX_IO_PGTABLE_FAST_H */
