/*
 * Macros for manipulating and testing flags related to a
 * pageblock_nr_pages number of pages.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
	PB_migrate,
	PB_migrate_end = PB_migrate + 3 - 1,
			/* 3 bits required for migrate types */
	PB_migrate_skip,/* If set the block is skipped by compaction */

	/*
	 * Assume the bits will always align on a word. If this assumption
	 * changes then get/set pageblock needs updating.
	 */
	NR_PAGEBLOCK_BITS
};

#ifdef CONFIG_HUGETLB_PAGE

#ifdef CONFIG_HUGETLB_PAGE_SIZE_VARIABLE

/* Huge page sizes are variable */
extern int pageblock_order;

#else /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

/* Huge pages are a constant size */
#define pageblock_order		HUGETLB_PAGE_ORDER

#endif /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

#else /* CONFIG_HUGETLB_PAGE */

/* If huge pages are not used, group by MAX_ORDER_NR_PAGES */
#define pageblock_order		(MAX_ORDER-1)

#endif /* CONFIG_HUGETLB_PAGE */

#define pageblock_nr_pages	(1UL << pageblock_order)

/* Forward declaration */
struct page;

unsigned long get_pageblock_flags_mask(struct page *page,
				unsigned long end_bitidx,
				unsigned long mask);
void set_pageblock_flags_mask(struct page *page,
				unsigned long flags,
				unsigned long end_bitidx,
				unsigned long mask);

/* Declarations for getting and setting flags. See mm/page_alloc.c */
static inline unsigned long get_pageblock_flags_group(struct page *page,
					int start_bitidx, int end_bitidx)
{
	unsigned long nr_flag_bits = end_bitidx - start_bitidx + 1;
	unsigned long mask = (1 << nr_flag_bits) - 1;

	return get_pageblock_flags_mask(page, end_bitidx, mask);
}

static inline void set_pageblock_flags_group(struct page *page,
					unsigned long flags,
					int start_bitidx, int end_bitidx)
{
	unsigned long nr_flag_bits = end_bitidx - start_bitidx + 1;
	unsigned long mask = (1 << nr_flag_bits) - 1;

	set_pageblock_flags_mask(page, flags, end_bitidx, mask);
}

#ifdef CONFIG_COMPACTION
#define get_pageblock_skip(page) \
			get_pageblock_flags_group(page, PB_migrate_skip,     \
							PB_migrate_skip)
#define clear_pageblock_skip(page) \
			set_pageblock_flags_group(page, 0, PB_migrate_skip,  \
							PB_migrate_skip)
#define set_pageblock_skip(page) \
			set_pageblock_flags_group(page, 1, PB_migrate_skip,  \
							PB_migrate_skip)
#endif /* CONFIG_COMPACTION */

#endif	/* PAGEBLOCK_FLAGS_H */
