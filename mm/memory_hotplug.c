/*
 *  linux/mm/memory_hotplug.c
 *
 *  Copyright (C)
 */

#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include <linux/ioport.h>
#include <linux/cpuset.h>

#include <asm/tlbflush.h>

/* add this memory to iomem resource */
static struct resource *register_memory_resource(u64 start, u64 size)
{
	struct resource *res;
	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	BUG_ON(!res);

	res->name = "System RAM";
	res->start = start;
	res->end = start + size - 1;
	res->flags = IORESOURCE_MEM;
	if (request_resource(&iomem_resource, res) < 0) {
		printk("System RAM resource %llx - %llx cannot be added\n",
		(unsigned long long)res->start, (unsigned long long)res->end);
		kfree(res);
		res = NULL;
	}
	return res;
}

static void release_memory_resource(struct resource *res)
{
	if (!res)
		return;
	release_resource(res);
	kfree(res);
	return;
}


#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
static int __add_zone(struct zone *zone, unsigned long phys_start_pfn)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int nr_pages = PAGES_PER_SECTION;
	int nid = pgdat->node_id;
	int zone_type;

	zone_type = zone - pgdat->node_zones;
	if (!populated_zone(zone)) {
		int ret = 0;
		ret = init_currently_empty_zone(zone, phys_start_pfn,
						nr_pages, MEMMAP_HOTPLUG);
		if (ret < 0)
			return ret;
	}
	memmap_init_zone(nr_pages, nid, zone_type,
			 phys_start_pfn, MEMMAP_HOTPLUG);
	return 0;
}

static int __add_section(struct zone *zone, unsigned long phys_start_pfn)
{
	int nr_pages = PAGES_PER_SECTION;
	int ret;

	if (pfn_valid(phys_start_pfn))
		return -EEXIST;

	ret = sparse_add_one_section(zone, phys_start_pfn, nr_pages);

	if (ret < 0)
		return ret;

	ret = __add_zone(zone, phys_start_pfn);

	if (ret < 0)
		return ret;

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
	int start_sec, end_sec;
	/* during initialize mem_map, align hot-added range to section */
	start_sec = pfn_to_section_nr(phys_start_pfn);
	end_sec = pfn_to_section_nr(phys_start_pfn + nr_pages - 1);

	for (i = start_sec; i <= end_sec; i++) {
		err = __add_section(zone, i << PFN_SECTION_SHIFT);

		/*
		 * EEXIST is finally dealed with by ioresource collision
		 * check. see add_memory() => register_memory_resource()
		 * Warning will be printed if there is collision.
		 */
		if (err && (err != -EEXIST))
			break;
		err = 0;
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

	zone->spanned_pages = max(old_zone_end_pfn, end_pfn) -
				zone->zone_start_pfn;

	zone_span_writeunlock(zone);
}

static void grow_pgdat_span(struct pglist_data *pgdat,
		unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long old_pgdat_end_pfn =
		pgdat->node_start_pfn + pgdat->node_spanned_pages;

	if (start_pfn < pgdat->node_start_pfn)
		pgdat->node_start_pfn = start_pfn;

	pgdat->node_spanned_pages = max(old_pgdat_end_pfn, end_pfn) -
					pgdat->node_start_pfn;
}

int online_pages(unsigned long pfn, unsigned long nr_pages)
{
	unsigned long i;
	unsigned long flags;
	unsigned long onlined_pages = 0;
	struct resource res;
	u64 section_end;
	unsigned long start_pfn;
	struct zone *zone;
	int need_zonelists_rebuild = 0;

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

	/*
	 * If this zone is not populated, then it is not in zonelist.
	 * This means the page allocator ignores this zone.
	 * So, zonelist must be updated after online.
	 */
	if (!populated_zone(zone))
		need_zonelists_rebuild = 1;

	res.start = (u64)pfn << PAGE_SHIFT;
	res.end = res.start + ((u64)nr_pages << PAGE_SHIFT) - 1;
	res.flags = IORESOURCE_MEM; /* we just need system ram */
	section_end = res.end;

	while ((res.start < res.end) && (find_next_system_ram(&res) >= 0)) {
		start_pfn = (unsigned long)(res.start >> PAGE_SHIFT);
		nr_pages = (unsigned long)
                           ((res.end + 1 - res.start) >> PAGE_SHIFT);

		if (PageReserved(pfn_to_page(start_pfn))) {
			/* this region's page is not onlined now */
			for (i = 0; i < nr_pages; i++) {
				struct page *page = pfn_to_page(start_pfn + i);
				online_page(page);
				onlined_pages++;
			}
		}

		res.start = res.end + 1;
		res.end = section_end;
	}
	zone->present_pages += onlined_pages;
	zone->zone_pgdat->node_present_pages += onlined_pages;

	setup_per_zone_pages_min();

	if (need_zonelists_rebuild)
		build_all_zonelists();
	vm_total_pages = nr_free_pagecache_pages();
	writeback_set_ratelimit();
	return 0;
}
#endif /* CONFIG_MEMORY_HOTPLUG_SPARSE */

static pg_data_t *hotadd_new_pgdat(int nid, u64 start)
{
	struct pglist_data *pgdat;
	unsigned long zones_size[MAX_NR_ZONES] = {0};
	unsigned long zholes_size[MAX_NR_ZONES] = {0};
	unsigned long start_pfn = start >> PAGE_SHIFT;

	pgdat = arch_alloc_nodedata(nid);
	if (!pgdat)
		return NULL;

	arch_refresh_nodedata(nid, pgdat);

	/* we can use NODE_DATA(nid) from here */

	/* init node's zones as empty zones, we don't have any present pages.*/
	free_area_init_node(nid, pgdat, zones_size, start_pfn, zholes_size);

	return pgdat;
}

static void rollback_node_hotadd(int nid, pg_data_t *pgdat)
{
	arch_refresh_nodedata(nid, NULL);
	arch_free_nodedata(pgdat);
	return;
}


int add_memory(int nid, u64 start, u64 size)
{
	pg_data_t *pgdat = NULL;
	int new_pgdat = 0;
	struct resource *res;
	int ret;

	res = register_memory_resource(start, size);
	if (!res)
		return -EEXIST;

	if (!node_online(nid)) {
		pgdat = hotadd_new_pgdat(nid, start);
		if (!pgdat)
			return -ENOMEM;
		new_pgdat = 1;
		ret = kswapd_run(nid);
		if (ret)
			goto error;
	}

	/* call arch's memory hotadd */
	ret = arch_add_memory(nid, start, size);

	if (ret < 0)
		goto error;

	/* we online node here. we can't roll back from here. */
	node_set_online(nid);

	cpuset_track_online_nodes();

	if (new_pgdat) {
		ret = register_one_node(nid);
		/*
		 * If sysfs file of new node can't create, cpu on the node
		 * can't be hot-added. There is no rollback way now.
		 * So, check by BUG_ON() to catch it reluctantly..
		 */
		BUG_ON(ret);
	}

	return ret;
error:
	/* rollback pgdat allocation and others */
	if (new_pgdat)
		rollback_node_hotadd(nid, pgdat);
	if (res)
		release_memory_resource(res);

	return ret;
}
EXPORT_SYMBOL_GPL(add_memory);
