// SPDX-License-Identifier: GPL-2.0
/*
 * Bootmem core functions.
 *
 * Copyright (c) 2020, Bytedance.
 *
 *     Author: Muchun Song <songmuchun@bytedance.com>
 *
 */
#include <linux/mm.h>
#include <linux/compiler.h>
#include <linux/memblock.h>
#include <linux/bootmem_info.h>
#include <linux/memory_hotplug.h>
#include <linux/kmemleak.h>

void get_page_bootmem(unsigned long info, struct page *page,
		enum bootmem_type type)
{
	BUG_ON(type > 0xf);
	BUG_ON(info > (ULONG_MAX >> 4));
	SetPagePrivate(page);
	set_page_private(page, info << 4 | type);
	page_ref_inc(page);
}

void put_page_bootmem(struct page *page)
{
	enum bootmem_type type = bootmem_type(page);

	BUG_ON(type < MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE ||
	       type > MEMORY_HOTPLUG_MAX_BOOTMEM_TYPE);

	if (page_ref_dec_return(page) == 1) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		INIT_LIST_HEAD(&page->lru);
		kmemleak_free_part_phys(PFN_PHYS(page_to_pfn(page)), PAGE_SIZE);
		free_reserved_page(page);
	}
}

#ifndef CONFIG_SPARSEMEM_VMEMMAP
static void __init register_page_bootmem_info_section(unsigned long start_pfn)
{
	unsigned long mapsize, section_nr, i;
	struct mem_section *ms;
	struct page *page, *memmap;
	struct mem_section_usage *usage;

	section_nr = pfn_to_section_nr(start_pfn);
	ms = __nr_to_section(section_nr);

	/* Get section's memmap address */
	memmap = sparse_decode_mem_map(ms->section_mem_map, section_nr);

	/*
	 * Get page for the memmap's phys address
	 * XXX: need more consideration for sparse_vmemmap...
	 */
	page = virt_to_page(memmap);
	mapsize = sizeof(struct page) * PAGES_PER_SECTION;
	mapsize = PAGE_ALIGN(mapsize) >> PAGE_SHIFT;

	/* remember memmap's page */
	for (i = 0; i < mapsize; i++, page++)
		get_page_bootmem(section_nr, page, SECTION_INFO);

	usage = ms->usage;
	page = virt_to_page(usage);

	mapsize = PAGE_ALIGN(mem_section_usage_size()) >> PAGE_SHIFT;

	for (i = 0; i < mapsize; i++, page++)
		get_page_bootmem(section_nr, page, MIX_SECTION_INFO);

}
#else /* CONFIG_SPARSEMEM_VMEMMAP */
static void __init register_page_bootmem_info_section(unsigned long start_pfn)
{
	unsigned long mapsize, section_nr, i;
	struct mem_section *ms;
	struct page *page, *memmap;
	struct mem_section_usage *usage;

	section_nr = pfn_to_section_nr(start_pfn);
	ms = __nr_to_section(section_nr);

	memmap = sparse_decode_mem_map(ms->section_mem_map, section_nr);

	register_page_bootmem_memmap(section_nr, memmap, PAGES_PER_SECTION);

	usage = ms->usage;
	page = virt_to_page(usage);

	mapsize = PAGE_ALIGN(mem_section_usage_size()) >> PAGE_SHIFT;

	for (i = 0; i < mapsize; i++, page++)
		get_page_bootmem(section_nr, page, MIX_SECTION_INFO);
}
#endif /* !CONFIG_SPARSEMEM_VMEMMAP */

void __init register_page_bootmem_info_node(struct pglist_data *pgdat)
{
	unsigned long i, pfn, end_pfn, nr_pages;
	int node = pgdat->node_id;
	struct page *page;

	nr_pages = PAGE_ALIGN(sizeof(struct pglist_data)) >> PAGE_SHIFT;
	page = virt_to_page(pgdat);

	for (i = 0; i < nr_pages; i++, page++)
		get_page_bootmem(node, page, NODE_INFO);

	pfn = pgdat->node_start_pfn;
	end_pfn = pgdat_end_pfn(pgdat);

	/* register section info */
	for (; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		/*
		 * Some platforms can assign the same pfn to multiple nodes - on
		 * node0 as well as nodeN.  To avoid registering a pfn against
		 * multiple nodes we check that this pfn does not already
		 * reside in some other nodes.
		 */
		if (pfn_valid(pfn) && (early_pfn_to_nid(pfn) == node))
			register_page_bootmem_info_section(pfn);
	}
}
