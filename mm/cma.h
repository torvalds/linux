/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_CMA_H__
#define __MM_CMA_H__

#include <linux/debugfs.h>
#include <linux/kobject.h>

struct cma_kobject {
	struct kobject kobj;
	struct cma *cma;
};

/*
 * Multi-range support. This can be useful if the size of the allocation
 * is not expected to be larger than the alignment (like with hugetlb_cma),
 * and the total amount of memory requested, while smaller than the total
 * amount of memory available, is large enough that it doesn't fit in a
 * single physical memory range because of memory holes.
 */
struct cma_memrange {
	unsigned long base_pfn;
	unsigned long count;
	unsigned long *bitmap;
#ifdef CONFIG_CMA_DEBUGFS
	struct debugfs_u32_array dfs_bitmap;
#endif
};
#define CMA_MAX_RANGES 8

struct cma {
	unsigned long   count;
	unsigned long	available_count;
	unsigned int order_per_bit; /* Order of pages represented by one bit */
	spinlock_t	lock;
#ifdef CONFIG_CMA_DEBUGFS
	struct hlist_head mem_head;
	spinlock_t mem_head_lock;
#endif
	char name[CMA_MAX_NAME];
	int nranges;
	struct cma_memrange ranges[CMA_MAX_RANGES];
#ifdef CONFIG_CMA_SYSFS
	/* the number of CMA page successful allocations */
	atomic64_t nr_pages_succeeded;
	/* the number of CMA page allocation failures */
	atomic64_t nr_pages_failed;
	/* the number of CMA page released */
	atomic64_t nr_pages_released;
	/* kobject requires dynamic object */
	struct cma_kobject *cma_kobj;
#endif
	unsigned long flags;
	/* NUMA node (NUMA_NO_NODE if unspecified) */
	int nid;
};

enum cma_flags {
	CMA_RESERVE_PAGES_ON_ERROR,
	CMA_ZONES_VALID,
	CMA_ZONES_INVALID,
};

extern struct cma cma_areas[MAX_CMA_AREAS];
extern unsigned int cma_area_count;

static inline unsigned long cma_bitmap_maxno(struct cma *cma,
		struct cma_memrange *cmr)
{
	return cmr->count >> cma->order_per_bit;
}

#ifdef CONFIG_CMA_SYSFS
void cma_sysfs_account_success_pages(struct cma *cma, unsigned long nr_pages);
void cma_sysfs_account_fail_pages(struct cma *cma, unsigned long nr_pages);
void cma_sysfs_account_release_pages(struct cma *cma, unsigned long nr_pages);
#else
static inline void cma_sysfs_account_success_pages(struct cma *cma,
						   unsigned long nr_pages) {};
static inline void cma_sysfs_account_fail_pages(struct cma *cma,
						unsigned long nr_pages) {};
static inline void cma_sysfs_account_release_pages(struct cma *cma,
						   unsigned long nr_pages) {};
#endif
#endif
