// SPDX-License-Identifier: GPL-2.0
/*
 * sparse memory mappings.
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/mmzone.h>
#include <linux/memblock.h>
#include <linux/compiler.h>
#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/vmstat.h>
#include "internal.h"
#include "mm_init.h"
#include "sparse.h"
#include <asm/dma.h>

/*
 * Permanent SPARSEMEM data:
 *
 * 1) mem_section	- memory sections, mem_map's for valid memory
 */
#ifdef CONFIG_SPARSEMEM_EXTREME
struct mem_section **mem_section;
#else
struct mem_section mem_section[NR_SECTION_ROOTS][SECTIONS_PER_ROOT]
	____cacheline_internodealigned_in_smp;
#endif
EXPORT_SYMBOL(mem_section);

#ifdef NODE_NOT_IN_PAGE_FLAGS
/*
 * If we did not store the node number in the page then we have to
 * do a lookup in the section_to_node_table in order to find which
 * node the page belongs to.
 */
#if MAX_NUMNODES <= 256
static u8 section_to_node_table[NR_MEM_SECTIONS] __cacheline_aligned;
#else
static u16 section_to_node_table[NR_MEM_SECTIONS] __cacheline_aligned;
#endif

int memdesc_nid(const memdesc_flags_t *mdf)
{
	return section_to_node_table[memdesc_section(mdf)];
}
EXPORT_SYMBOL(memdesc_nid);

static void set_section_nid(unsigned long section_nr, int nid)
{
	section_to_node_table[section_nr] = nid;
}
#else /* !NODE_NOT_IN_PAGE_FLAGS */
static inline void set_section_nid(unsigned long section_nr, int nid)
{
}
#endif

#ifdef CONFIG_SPARSEMEM_EXTREME
static noinline struct mem_section __ref *sparse_index_alloc(int nid)
{
	struct mem_section *section = NULL;
	unsigned long array_size = SECTIONS_PER_ROOT *
				   sizeof(struct mem_section);

	if (slab_is_available()) {
		section = kzalloc_node(array_size, GFP_KERNEL, nid);
	} else {
		section = memblock_alloc_node(array_size, SMP_CACHE_BYTES,
					      nid);
		if (!section)
			panic("%s: Failed to allocate %lu bytes nid=%d\n",
			      __func__, array_size, nid);
	}

	return section;
}

int __meminit sparse_index_init(unsigned long section_nr, int nid)
{
	unsigned long root = SECTION_NR_TO_ROOT(section_nr);
	struct mem_section *section;

	/*
	 * An existing section is possible in the sub-section hotplug
	 * case. First hot-add instantiates, follow-on hot-add reuses
	 * the existing section.
	 *
	 * The mem_hotplug_lock resolves the apparent race below.
	 */
	if (mem_section[root])
		return 0;

	section = sparse_index_alloc(nid);
	if (!section)
		return -ENOMEM;

	mem_section[root] = section;

	return 0;
}

static void __init sparse_extreme_init(void)
{
	const unsigned long size = sizeof(struct mem_section *) * NR_SECTION_ROOTS;

	mem_section = memblock_alloc_or_panic(size, INTERNODE_CACHE_BYTES);
}
#else /* !SPARSEMEM_EXTREME */
int __meminit sparse_index_init(unsigned long section_nr, int nid)
{
	return 0;
}

static void __init sparse_extreme_init(void)
{
}
#endif

/*
 * During early boot, before section_mem_map is used for an actual
 * mem_map, we use section_mem_map to store the section's NUMA
 * node.  This keeps us from having to use another data structure.  The
 * node information is cleared just before we store the real mem_map.
 */
static inline unsigned long sparse_encode_early_nid(int nid)
{
	return ((unsigned long)nid << SECTION_NID_SHIFT);
}

static inline int sparse_early_nid(struct mem_section *section)
{
	return (section->section_mem_map >> SECTION_NID_SHIFT);
}

/* Validate the physical addressing limitations of the model */
static void __init mminit_validate_memmodel_limits(unsigned long *start_pfn,
						unsigned long *end_pfn)
{
	unsigned long max_sparsemem_pfn = (DIRECT_MAP_PHYSMEM_END + 1) >> PAGE_SHIFT;

	/*
	 * Sanity checks - do not allow an architecture to pass
	 * in larger pfns than the maximum scope of sparsemem:
	 */
	if (*start_pfn > max_sparsemem_pfn) {
		mminit_dprintk(MMINIT_WARNING, "pfnvalidation",
			"Start of range %lu -> %lu exceeds SPARSEMEM max %lu\n",
			*start_pfn, *end_pfn, max_sparsemem_pfn);
		WARN_ON_ONCE(1);
		*start_pfn = max_sparsemem_pfn;
		*end_pfn = max_sparsemem_pfn;
	} else if (*end_pfn > max_sparsemem_pfn) {
		mminit_dprintk(MMINIT_WARNING, "pfnvalidation",
			"End of range %lu -> %lu exceeds SPARSEMEM max %lu\n",
			*start_pfn, *end_pfn, max_sparsemem_pfn);
		WARN_ON_ONCE(1);
		*end_pfn = max_sparsemem_pfn;
	}
}

/*
 * There are a number of times that we loop over NR_MEM_SECTIONS,
 * looking for section_present() on each.  But, when we have very
 * large physical address spaces, NR_MEM_SECTIONS can also be
 * very large which makes the loops quite long.
 *
 * Keeping track of this gives us an easy way to break out of
 * those loops early.
 */
unsigned long __highest_present_section_nr;

static inline unsigned long first_present_section_nr(void)
{
	return next_present_section_nr(-1);
}

void __init sparse_sections_init(void)
{
	unsigned long pfn, start_pfn, end_pfn;
	int i, nid;

	sparse_extreme_init();

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		start_pfn &= PAGE_SECTION_MASK;
		mminit_validate_memmodel_limits(&start_pfn, &end_pfn);

		for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
			unsigned long section_nr = pfn_to_section_nr(pfn);
			struct mem_section *ms;

			sparse_index_init(section_nr, nid);
			ms = __nr_to_section(section_nr);
			if (ms->section_mem_map)
				continue;

			set_section_nid(section_nr, nid);
			ms->section_mem_map = sparse_encode_early_nid(nid) |
							SECTION_IS_ONLINE;
			__section_mark_present(ms, section_nr);
		}
	}
}
#ifndef CONFIG_SPARSEMEM_VMEMMAP
struct page __init *__populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid, struct vmem_altmap *altmap,
		struct dev_pagemap *pgmap)
{
	const unsigned long size = PAGE_ALIGN(sizeof(struct page) * PAGES_PER_SECTION);

	return memmap_alloc(size, size, __pa(MAX_DMA_ADDRESS), nid, false);
}
#endif /* !CONFIG_SPARSEMEM_VMEMMAP */

void __weak __meminit vmemmap_populate_print_last(void)
{
}

static void __init sparse_metadata_init_nid(int nid,
		unsigned long start_section_nr, unsigned long end_section_nr,
		unsigned long nr_sections)
{
	struct mem_section_usage *usage;
	unsigned long section_nr;

	usage = memblock_alloc_node(nr_sections * mem_section_usage_size(),
				    SMP_CACHE_BYTES, nid);
	if (!usage)
		panic("Failed to allocate usemap for node %d\n", nid);

	for_each_present_section_nr(start_section_nr, section_nr) {
		const unsigned long pfn = section_nr_to_pfn(section_nr);
		struct page *mem_map;

		if (section_nr >= end_section_nr)
			break;

		mem_map = __populate_section_memmap(pfn, PAGES_PER_SECTION, nid,
						    NULL, NULL);
		if (!mem_map)
			panic("Failed to allocate memmap for section %lu\n",
			      section_nr);
		memmap_boot_pages_add(section_nr_vmemmap_pages(pfn, PAGES_PER_SECTION));
		sparse_init_one_section(__nr_to_section(section_nr), section_nr,
					mem_map, usage, SECTION_IS_EARLY);
		usage = (void *)usage + mem_section_usage_size();
	}
}

static void __init sparse_metadata_init(void)
{
	unsigned long start_section_nr = first_present_section_nr();
	int nid_begin = sparse_early_nid(__nr_to_section(start_section_nr));
	unsigned long section_nr, nr_sections = 1;

	for_each_present_section_nr(start_section_nr + 1, section_nr) {
		const int nid = sparse_early_nid(__nr_to_section(section_nr));

		if (nid == nid_begin) {
			nr_sections++;
			continue;
		}
		sparse_metadata_init_nid(nid_begin, start_section_nr,
					 section_nr, nr_sections);
		nid_begin = nid;
		start_section_nr = section_nr;
		nr_sections = 1;
	}
	sparse_metadata_init_nid(nid_begin, start_section_nr, section_nr,
				 nr_sections);
}

/*
 * Allocate the accumulated non-linear sections, allocate a mem_map
 * for each and record the physical to section mapping.
 */
void __init sparse_init(void)
{
	if (compound_info_has_mask()) {
		VM_WARN_ON_ONCE(!IS_ALIGNED((unsigned long) pfn_to_page(0),
				    MAX_FOLIO_VMEMMAP_ALIGN));
	}

	sparse_metadata_init();
	sparse_init_subsection_map();
	vmemmap_populate_print_last();
}
