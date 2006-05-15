/*
 *  linux/mm/memory_hotplug.c
 *
 *  Copyright (C)
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/pagevec.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>

#include <asm/tlbflush.h>

extern void zonetable_add(struct zone *zone, int nid, int zid, unsigned long pfn,
			  unsigned long size);
static void __add_zone(struct zone *zone, unsigned long phys_start_pfn)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int nr_pages = PAGES_PER_SECTION;
	int nid = pgdat->node_id;
	int zone_type;

	zone_type = zone - pgdat->node_zones;
	memmap_init_zone(nr_pages, nid, zone_type, phys_start_pfn);
	zonetable_add(zone, nid, zone_type, phys_start_pfn, nr_pages);
}

extern int sparse_add_one_section(struct zone *zone, unsigned long start_pfn,
				  int nr_pages);
static int __add_section(struct zone *zone, unsigned long phys_start_pfn)
{
	int nr_pages = PAGES_PER_SECTION;
	int ret;

	ret = sparse_add_one_section(zone, phys_start_pfn, nr_pages);

	if (ret < 0)
		return ret;

	__add_zone(zone, phys_start_pfn);
	return register_new_memory(__pfn_to_section(phys_start_pfn));
}

/*
 * Reasonably generic function for adding memory.  It is
 * expected that archs that support memory hotplug will
 * call this function after deciding the zone to which to
 * add the new pages.
 */
int __add_pages(struct zone *zone, unsigned long phys_start_pfn,
		 unsigned long nr_pages)
{
	unsigned long i;
	int err = 0;

	for (i = 0; i < nr_pages; i += PAGES_PER_SECTION) {
		err = __add_section(zone, phys_start_pfn + i);

		/* We want to keep adding the rest of the
		 * sections if the first ones already exist
		 */
		if (err && (err != -EEXIST))
			break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(__add_pages);

static void grow_zone_span(struct zone *zone,
		unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long old_zone_end_pfn;

	zone_span_writelock(zone);

	old_zone_end_pfn = zone->zone_start_pfn + zone->spanned_pages;
	if (start_pfn < zone->zone_start_pfn)
		zone->zone_start_pfn = start_pfn;

	if (end_pfn > old_zone_end_pfn)
		zone->spanned_pages = end_pfn - zone->zone_start_pfn;

	zone_span_writeunlock(zone);
}

static void grow_pgdat_span(struct pglist_data *pgdat,
		unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long old_pgdat_end_pfn =
		pgdat->node_start_pfn + pgdat->node_spanned_pages;

	if (start_pfn < pgdat->node_start_pfn)
		pgdat->node_start_pfn = start_pfn;

	if (end_pfn > old_pgdat_end_pfn)
		pgdat->node_spanned_pages = end_pfn - pgdat->node_start_pfn;
}

int online_pages(unsigned long pfn, unsigned long nr_pages)
{
	unsigned long i;
	unsigned long flags;
	unsigned long onlined_pages = 0;
	struct zone *zone;

	/*
	 * This doesn't need a lock to do pfn_to_page().
	 * The section can't be removed here because of the
	 * memory_block->state_sem.
	 */
	zone = page_zone(pfn_to_page(pfn));
	pgdat_resize_lock(zone->zone_pgdat, &flags);
	grow_zone_span(zone, pfn, pfn + nr_pages);
	grow_pgdat_span(zone->zone_pgdat, pfn, pfn + nr_pages);
	pgdat_resize_unlock(zone->zone_pgdat, &flags);

	for (i = 0; i < nr_pages; i++) {
		struct page *page = pfn_to_page(pfn + i);
		online_page(page);
		onlined_pages++;
	}
	zone->present_pages += onlined_pages;
	zone->zone_pgdat->node_present_pages += onlined_pages;

	setup_per_zone_pages_min();

	return 0;
}
