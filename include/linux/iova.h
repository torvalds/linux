/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2006, Intel Corporation.
 *
 * Copyright (C) 2006-2008 Intel Corporation
 * Author: Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 */

#ifndef _IOVA_H_
#define _IOVA_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/rbtree.h>
#include <linux/dma-mapping.h>

/* iova structure */
struct iova {
	struct rb_node	node;
	unsigned long	pfn_hi; /* Highest allocated pfn */
	unsigned long	pfn_lo; /* Lowest allocated pfn */
};

struct iova_magazine;
struct iova_cpu_rcache;

#define IOVA_RANGE_CACHE_MAX_SIZE 6	/* log of max cached IOVA range size (in pages) */
#define MAX_GLOBAL_MAGS 32	/* magazines per bin */

struct iova_rcache {
	spinlock_t lock;
	unsigned long depot_size;
	struct iova_magazine *depot[MAX_GLOBAL_MAGS];
	struct iova_cpu_rcache __percpu *cpu_rcaches;
};

/* holds all the iova translations for a domain */
struct iova_domain {
	spinlock_t	iova_rbtree_lock; /* Lock to protect update of rbtree */
	struct rb_root	rbroot;		/* iova domain rbtree root */
	struct rb_node	*cached_node;	/* Save last alloced node */
	struct rb_node	*cached32_node; /* Save last 32-bit alloced node */
	unsigned long	granule;	/* pfn granularity for this domain */
	unsigned long	start_pfn;	/* Lower limit for this domain */
	unsigned long	dma_32bit_pfn;
	unsigned long	max32_alloc_size; /* Size of last failed allocation */
	struct iova	anchor;		/* rbtree lookup anchor */

	struct iova_rcache rcaches[IOVA_RANGE_CACHE_MAX_SIZE];	/* IOVA range caches */
	struct hlist_node	cpuhp_dead;
};

static inline unsigned long iova_size(struct iova *iova)
{
	return iova->pfn_hi - iova->pfn_lo + 1;
}

static inline unsigned long iova_shift(struct iova_domain *iovad)
{
	return __ffs(iovad->granule);
}

static inline unsigned long iova_mask(struct iova_domain *iovad)
{
	return iovad->granule - 1;
}

static inline size_t iova_offset(struct iova_domain *iovad, dma_addr_t iova)
{
	return iova & iova_mask(iovad);
}

static inline size_t iova_align(struct iova_domain *iovad, size_t size)
{
	return ALIGN(size, iovad->granule);
}

static inline dma_addr_t iova_dma_addr(struct iova_domain *iovad, struct iova *iova)
{
	return (dma_addr_t)iova->pfn_lo << iova_shift(iovad);
}

static inline unsigned long iova_pfn(struct iova_domain *iovad, dma_addr_t iova)
{
	return iova >> iova_shift(iovad);
}

#if IS_ENABLED(CONFIG_IOMMU_IOVA)
int iova_cache_get(void);
void iova_cache_put(void);

void free_iova(struct iova_domain *iovad, unsigned long pfn);
void __free_iova(struct iova_domain *iovad, struct iova *iova);
struct iova *alloc_iova(struct iova_domain *iovad, unsigned long size,
	unsigned long limit_pfn,
	bool size_aligned);
void free_iova_fast(struct iova_domain *iovad, unsigned long pfn,
		    unsigned long size);
unsigned long alloc_iova_fast(struct iova_domain *iovad, unsigned long size,
			      unsigned long limit_pfn, bool flush_rcache);
struct iova *reserve_iova(struct iova_domain *iovad, unsigned long pfn_lo,
	unsigned long pfn_hi);
void init_iova_domain(struct iova_domain *iovad, unsigned long granule,
	unsigned long start_pfn);
struct iova *find_iova(struct iova_domain *iovad, unsigned long pfn);
void put_iova_domain(struct iova_domain *iovad);
#else
static inline int iova_cache_get(void)
{
	return -ENOTSUPP;
}

static inline void iova_cache_put(void)
{
}

static inline void free_iova(struct iova_domain *iovad, unsigned long pfn)
{
}

static inline void __free_iova(struct iova_domain *iovad, struct iova *iova)
{
}

static inline struct iova *alloc_iova(struct iova_domain *iovad,
				      unsigned long size,
				      unsigned long limit_pfn,
				      bool size_aligned)
{
	return NULL;
}

static inline void free_iova_fast(struct iova_domain *iovad,
				  unsigned long pfn,
				  unsigned long size)
{
}

static inline unsigned long alloc_iova_fast(struct iova_domain *iovad,
					    unsigned long size,
					    unsigned long limit_pfn,
					    bool flush_rcache)
{
	return 0;
}

static inline struct iova *reserve_iova(struct iova_domain *iovad,
					unsigned long pfn_lo,
					unsigned long pfn_hi)
{
	return NULL;
}

static inline void init_iova_domain(struct iova_domain *iovad,
				    unsigned long granule,
				    unsigned long start_pfn)
{
}

static inline struct iova *find_iova(struct iova_domain *iovad,
				     unsigned long pfn)
{
	return NULL;
}

static inline void put_iova_domain(struct iova_domain *iovad)
{
}

#endif

#endif
