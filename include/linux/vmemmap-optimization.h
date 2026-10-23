/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * vmemmap-optimization.h
 *
 * Generic vmemmap optimization declarations.
 *
 * Author: Muchun Song <songmuchun@bytedance.com>
 */
#ifndef _LINUX_VMEMMAP_OPTIMIZATION_H
#define _LINUX_VMEMMAP_OPTIMIZATION_H

#include <linux/align.h>
#include <linux/log2.h>
#include <linux/mmdebug.h>
#include <linux/mmzone.h>

/*
 * HugeTLB Vmemmap Optimization (HVO) requires struct pages of the head page to
 * be naturally aligned with regard to the folio size.
 *
 * HVO which is only active if the size of struct page is a power of 2.
 */
#define MAX_FOLIO_VMEMMAP_ALIGN					\
	(IS_ENABLED(CONFIG_VMEMMAP_OPTIMIZATION) &&		\
	 is_power_of_2(sizeof(struct page)) ?			\
	 MAX_FOLIO_NR_PAGES * sizeof(struct page) : 0)

/* The number of retained vmemmap pages with HVO enabled. */
#define VMEMMAP_OPTIMIZATION_PAGES		1
#define VMEMMAP_OPTIMIZATION_NR_STRUCT_PAGES	\
	(VMEMMAP_OPTIMIZATION_PAGES * PAGE_SIZE / sizeof(struct page))
#define VMEMMAP_OPTIMIZATION_MIN_ORDER		(ilog2(VMEMMAP_OPTIMIZATION_NR_STRUCT_PAGES) + 1)

#ifdef CONFIG_VMEMMAP_OPTIMIZATION
static inline unsigned int section_compound_order(const struct mem_section *section)
{
	return section->compound_page_order;
}

static inline void section_set_compound_order(struct mem_section *section,
					      unsigned int order)
{
	VM_WARN_ON(section_compound_order(section) && order &&
		   section_compound_order(section) != order);
	section->compound_page_order = order;
}

static inline void section_set_compound_order_range(unsigned long pfn,
		unsigned long nr_pages, unsigned int order)
{
	unsigned long section_nr = pfn_to_section_nr(pfn);

	if (!IS_ALIGNED(pfn | nr_pages, PAGES_PER_SECTION))
		return;

	for (unsigned long i = 0; i < nr_pages / PAGES_PER_SECTION; i++)
		section_set_compound_order(__nr_to_section(section_nr + i), order);
}

static inline unsigned int pfn_to_section_compound_order(unsigned long pfn)
{
	return section_compound_order(__pfn_to_section(pfn));
}

struct page *vmemmap_shared_tail_page(unsigned int order, struct zone *zone);
#else
static inline unsigned int section_compound_order(const struct mem_section *section)
{
	return 0;
}

static inline void section_set_compound_order(struct mem_section *section,
					      unsigned int order)
{
}

static inline void section_set_compound_order_range(unsigned long pfn,
		unsigned long nr_pages, unsigned int order)
{
}

static inline unsigned int pfn_to_section_compound_order(unsigned long pfn)
{
	return 0;
}

static inline struct page *vmemmap_shared_tail_page(unsigned int order,
						    struct zone *zone)
{
	return NULL;
}
#endif /* CONFIG_VMEMMAP_OPTIMIZATION */

static inline bool vmemmap_optimizable_pfn(unsigned long pfn)
{
	const unsigned int order = pfn_to_section_compound_order(pfn);
	const unsigned long nr_pages = 1UL << order;

	if (!is_power_of_2(sizeof(struct page)))
		return false;

	return (pfn & (nr_pages - 1)) >= VMEMMAP_OPTIMIZATION_NR_STRUCT_PAGES;
}

static inline bool vmemmap_optimizable_order(unsigned int order)
{
	if (!IS_ENABLED(CONFIG_VMEMMAP_OPTIMIZATION))
		return false;

	if (!is_power_of_2(sizeof(struct page)))
		return false;

	return order >= VMEMMAP_OPTIMIZATION_MIN_ORDER;
}
#endif /* _LINUX_VMEMMAP_OPTIMIZATION_H */
