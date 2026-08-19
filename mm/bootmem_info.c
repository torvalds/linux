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
	set_page_private(page, info << 4 | type);
	page_ref_inc(page);
}

void put_page_bootmem(struct page *page)
{
	enum bootmem_type type = bootmem_type(page);

	BUG_ON(type < MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE ||
	       type > MEMORY_HOTPLUG_MAX_BOOTMEM_TYPE);

	if (page_ref_dec_return(page) == 1) {
		set_page_private(page, 0);
		free_reserved_page(page);
	}
}

static void __init register_page_bootmem_info_section(unsigned long start_pfn)
{
	unsigned long section_nr;
	struct mem_section *ms;

	start_pfn = SECTION_ALIGN_DOWN(start_pfn);
	section_nr = pfn_to_section_nr(start_pfn);
	ms = __nr_to_section(section_nr);

	if (!preinited_vmemmap_section(ms))
		register_page_bootmem_memmap(section_nr, pfn_to_page(start_pfn),
					     PAGES_PER_SECTION);
}

void __init register_page_bootmem_info_node(struct pglist_data *pgdat)
{
	unsigned long pfn, end_pfn;
	int node = pgdat->node_id;

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
