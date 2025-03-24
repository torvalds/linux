/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BOOTMEM_INFO_H
#define __LINUX_BOOTMEM_INFO_H

#include <linux/mm.h>
#include <linux/kmemleak.h>

/*
 * Types for free bootmem stored in the low bits of page->private.
 */
enum bootmem_type {
	MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE = 1,
	SECTION_INFO = MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE,
	MIX_SECTION_INFO,
	NODE_INFO,
	MEMORY_HOTPLUG_MAX_BOOTMEM_TYPE = NODE_INFO,
};

#ifdef CONFIG_HAVE_BOOTMEM_INFO_NODE
void __init register_page_bootmem_info_node(struct pglist_data *pgdat);

void get_page_bootmem(unsigned long info, struct page *page,
		enum bootmem_type type);
void put_page_bootmem(struct page *page);

static inline enum bootmem_type bootmem_type(const struct page *page)
{
	return (unsigned long)page->private & 0xf;
}

static inline unsigned long bootmem_info(const struct page *page)
{
	return (unsigned long)page->private >> 4;
}

/*
 * Any memory allocated via the memblock allocator and not via the
 * buddy will be marked reserved already in the memmap. For those
 * pages, we can call this function to free it to buddy allocator.
 */
static inline void free_bootmem_page(struct page *page)
{
	enum bootmem_type type = bootmem_type(page);

	/*
	 * The reserve_bootmem_region sets the reserved flag on bootmem
	 * pages.
	 */
	VM_BUG_ON_PAGE(page_ref_count(page) != 2, page);

	if (type == SECTION_INFO || type == MIX_SECTION_INFO)
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

static inline enum bootmem_type bootmem_type(const struct page *page)
{
	return SECTION_INFO;
}

static inline unsigned long bootmem_info(const struct page *page)
{
	return 0;
}

static inline void get_page_bootmem(unsigned long info, struct page *page,
				    enum bootmem_type type)
{
}

static inline void free_bootmem_page(struct page *page)
{
	kmemleak_free_part_phys(PFN_PHYS(page_to_pfn(page)), PAGE_SIZE);
	free_reserved_page(page);
}
#endif

#endif /* __LINUX_BOOTMEM_INFO_H */
