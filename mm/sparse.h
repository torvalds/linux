/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * sparse.h:
 *
 * mm/ internal sparse and sparse-vmemmap declarations
 */

#ifndef __MM_SPARSE_H
#define __MM_SPARSE_H

#include <linux/mmzone.h>

/*
 * mm/sparse.c
 */
#ifdef CONFIG_SPARSEMEM
void sparse_init(void);
int sparse_index_init(unsigned long section_nr, int nid);

static inline void sparse_init_one_section(struct mem_section *ms,
		unsigned long pnum, struct page *mem_map,
		struct mem_section_usage *usage, unsigned long flags)
{
	unsigned long coded_mem_map;

	BUILD_BUG_ON(SECTION_MAP_LAST_BIT > PFN_SECTION_SHIFT);

	/*
	 * We encode the start PFN of the section into the mem_map such that
	 * page_to_pfn() on !CONFIG_SPARSEMEM_VMEMMAP can simply subtract it
	 * from the page pointer to obtain the PFN.
	 */
	coded_mem_map = (unsigned long)(mem_map - section_nr_to_pfn(pnum));
	VM_WARN_ON_ONCE(coded_mem_map & ~SECTION_MAP_MASK);

	ms->section_mem_map &= ~SECTION_MAP_MASK;
	ms->section_mem_map |= coded_mem_map;
	ms->section_mem_map |= flags | SECTION_HAS_MEM_MAP;
	ms->usage = usage;
}

static inline void __section_mark_present(struct mem_section *ms,
		unsigned long section_nr)
{
	if (section_nr > __highest_present_section_nr)
		__highest_present_section_nr = section_nr;

	ms->section_mem_map |= SECTION_MARKED_PRESENT;
}
#else
static inline void sparse_init(void) {}
#endif /* CONFIG_SPARSEMEM */

/*
 * mm/sparse-vmemmap.c
 */
#ifdef CONFIG_SPARSEMEM_VMEMMAP
void sparse_init_subsection_map(void);
#else
static inline void sparse_init_subsection_map(void) {}
#endif /* CONFIG_SPARSEMEM_VMEMMAP */

#endif /* __MM_SPARSE_H */
