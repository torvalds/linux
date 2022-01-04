/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BOOTMEM_INFO_H
#define __LINUX_BOOTMEM_INFO_H

#include <linux/mm.h>

/*
 * Types for free bootmem stored in page->lru.next. These have to be in
 * some random range in unsigned long space for debugging purposes.
 */
enum {
	MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE = 12,
	SECTION_INFO = MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE,
	MIX_SECTION_INFO,
	NODE_INFO,
	MEMORY_HOTPLUG_MAX_BOOTMEM_TYPE = NODE_INFO,
};

#ifdef CONFIG_HAVE_BOOTMEM_INFO_NODE
void __init register_page_bootmem_info_node(struct pglist_data *pgdat);

void get_page_bootmem(unsigned long info, struct page *page,
		      unsigned long type);
void put_page_bootmem(struct page *page);

/*
 * Any memory allocated via the memblock allocator and not via the
 * buddy will be marked reserved already in the memmap. For those
 * pages, we can call this function to free it to buddy allocator.
 */
static inline void free_bootmem_page(struct page *page)
{
	unsigned long magic = (unsigned long)page->freelist;

	/*
	 * The reserve_bootmem_region sets the reserved flag on bootmem
	 * pages.
	 */
	VM_BUG_ON_PAGE(page_ref_count(page) != 2, page);

	if (magic == SECTION_INFO || magic == MIX_SECTION_INFO)
		put_page_bootmem(page);
	else
		VM_BUG_ON_PAGE(1, page);
}
#else
static inline void register_page_bootmem_info_node(struct pglist_data *pgdat)
{
}

static inline void put_page_bootmem(struct page *page)
{
}

static inline void get_page_bootmem(unsigned long info, struct page *page,
				    unsigned long type)
{
}

static inline void free_bootmem_page(struct page *page)
{
	free_reserved_page(page);
}
#endif

#endif /* __LINUX_BOOTMEM_INFO_H */
