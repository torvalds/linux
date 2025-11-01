/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Macros for manipulating and testing flags related to a
 * pageblock_nr_pages number of pages.
 *
 * Copyright (C) IBM Corporation, 2006
 *
 * Original author, Mel Gorman
 * Major cleanups and reduction of bit operations, Andy Whitcroft
 */
#ifndef PAGEBLOCK_FLAGS_H
#define PAGEBLOCK_FLAGS_H

#include <linux/types.h>

/* Bit indices that affect a whole block of pages */
enum pageblock_bits {
	PB_migrate_0,
	PB_migrate_1,
	PB_migrate_2,
	PB_compact_skip,/* If set the block is skipped by compaction */

#ifdef CONFIG_MEMORY_ISOLATION
	/*
	 * Pageblock isolation is represented with a separate bit, so that
	 * the migratetype of a block is not overwritten by isolation.
	 */
	PB_migrate_isolate, /* If set the block is isolated */
#endif
	/*
	 * Assume the bits will always align on a word. If this assumption
	 * changes then get/set pageblock needs updating.
	 */
	__NR_PAGEBLOCK_BITS
};

#define NR_PAGEBLOCK_BITS (roundup_pow_of_two(__NR_PAGEBLOCK_BITS))

#define MIGRATETYPE_MASK (BIT(PB_migrate_0)|BIT(PB_migrate_1)|BIT(PB_migrate_2))

#ifdef CONFIG_MEMORY_ISOLATION
#define MIGRATETYPE_AND_ISO_MASK (MIGRATETYPE_MASK | BIT(PB_migrate_isolate))
#else
#define MIGRATETYPE_AND_ISO_MASK MIGRATETYPE_MASK
#endif

#if defined(CONFIG_HUGETLB_PAGE)

#ifdef CONFIG_HUGETLB_PAGE_SIZE_VARIABLE

/* Huge page sizes are variable */
extern unsigned int pageblock_order;

#else /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

/*
 * Huge pages are a constant size, but don't exceed the maximum allocation
 * granularity.
 */
#define pageblock_order		MIN_T(unsigned int, HUGETLB_PAGE_ORDER, PAGE_BLOCK_MAX_ORDER)

#endif /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

#elif defined(CONFIG_TRANSPARENT_HUGEPAGE)

#define pageblock_order		MIN_T(unsigned int, HPAGE_PMD_ORDER, PAGE_BLOCK_MAX_ORDER)

#else /* CONFIG_TRANSPARENT_HUGEPAGE */

/* If huge pages are not used, group by PAGE_BLOCK_MAX_ORDER */
#define pageblock_order		PAGE_BLOCK_MAX_ORDER

#endif /* CONFIG_HUGETLB_PAGE */

#define pageblock_nr_pages	(1UL << pageblock_order)
#define pageblock_align(pfn)	ALIGN((pfn), pageblock_nr_pages)
#define pageblock_aligned(pfn)	IS_ALIGNED((pfn), pageblock_nr_pages)
#define pageblock_start_pfn(pfn)	ALIGN_DOWN((pfn), pageblock_nr_pages)
#define pageblock_end_pfn(pfn)		ALIGN((pfn) + 1, pageblock_nr_pages)

/* Forward declaration */
struct page;

enum migratetype get_pfnblock_migratetype(const struct page *page,
					  unsigned long pfn);
bool get_pfnblock_bit(const struct page *page, unsigned long pfn,
		      enum pageblock_bits pb_bit);
void set_pfnblock_bit(const struct page *page, unsigned long pfn,
		      enum pageblock_bits pb_bit);
void clear_pfnblock_bit(const struct page *page, unsigned long pfn,
			enum pageblock_bits pb_bit);

/* Declarations for getting and setting flags. See mm/page_alloc.c */
#ifdef CONFIG_COMPACTION
#define get_pageblock_skip(page) \
	get_pfnblock_bit(page, page_to_pfn(page), PB_compact_skip)
#define clear_pageblock_skip(page) \
	clear_pfnblock_bit(page, page_to_pfn(page), PB_compact_skip)
#define set_pageblock_skip(page) \
	set_pfnblock_bit(page, page_to_pfn(page), PB_compact_skip)
#else
static inline bool get_pageblock_skip(struct page *page)
{
	return false;
}
static inline void clear_pageblock_skip(struct page *page)
{
}
static inline void set_pageblock_skip(struct page *page)
{
}
#endif /* CONFIG_COMPACTION */

#endif	/* PAGEBLOCK_FLAGS_H */
