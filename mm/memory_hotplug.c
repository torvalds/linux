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
#include <linux/export.h>
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
#include <linux/delay.h>
#include <linux/migrate.h>
#include <linux/page-isolation.h>
#include <linux/pfn.h>
#include <linux/suspend.h>
#include <linux/mm_inline.h>
#include <linux/firmware-map.h>
#include <linux/stop_machine.h>
#include <linux/hugetlb.h>
#include <linux/memblock.h>

#include <asm/tlbflush.h>

#include "internal.h"

/*
 * online_page_callback contains pointer to current page onlining function.
 * Initially it is generic_online_page(). If it is required it could be
 * changed by calling set_online_page_callback() for callback registration
 * and restore_online_page_callback() for generic callback restore.
 */

static void generic_online_page(struct page *page);

static online_page_callback_t online_page_callback = generic_online_page;

DEFINE_MUTEX(mem_hotplug_mutex);

void lock_memory_hotplug(void)
{
	mutex_lock(&mem_hotplug_mutex);
}

void unlock_memory_hotplug(void)
{
	mutex_unlock(&mem_hotplug_mutex);
}


/* add this memory to iomem resource */
static struct resource *register_memory_resource(u64 start, u64 size)
{
	struct resource *res;
	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	BUG_ON(!res);

	res->name = "System RAM";
	res->start = start;
	res->end = start + size - 1;
	res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	if (request_resource(&iomem_resource, res) < 0) {
		pr_debug("System RAM resource %pR cannot be added\n", res);
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
void get_page_bootmem(unsigned long info,  struct page *page,
		      unsigned long type)
{
	page->lru.next = (struct list_head *) type;
	SetPagePrivate(page);
	set_page_private(page, info);
	atomic_inc(&page->_count);
}

void put_page_bootmem(struct page *page)
{
	unsigned long type;

	type = (unsigned long) page->lru.next;
	BUG_ON(type < MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE ||
	       type > MEMORY_HOTPLUG_MAX_BOOTMEM_TYPE);

	if (atomic_dec_return(&page->_count) == 1) {
		ClearPagePrivate(page);
		set_page_private(page, 0);
		INIT_LIST_HEAD(&page->lru);
		free_reserved_page(page);
	}
}

#ifdef CONFIG_HAVE_BOOTMEM_INFO_NODE
#ifndef CONFIG_SPARSEMEM_VMEMMAP
static void register_page_bootmem_info_section(unsigned long start_pfn)
{
	unsigned long *usemap, mapsize, section_nr, i;
	struct mem_section *ms;
	struct page *page, *memmap;

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

	usemap = __nr_to_section(section_nr)->pageblock_flags;
	page = virt_to_page(usemap);

	mapsize = PAGE_ALIGN(usemap_size()) >> PAGE_SHIFT;

	for (i = 0; i < mapsize; i++, page++)
		get_page_bootmem(section_nr, page, MIX_SECTION_INFO);

}
#else /* CONFIG_SPARSEMEM_VMEMMAP */
static void register_page_bootmem_info_section(unsigned long start_pfn)
{
	unsigned long *usemap, mapsize, section_nr, i;
	struct mem_section *ms;
	struct page *page, *memmap;

	if (!pfn_valid(start_pfn))
		return;

	section_nr = pfn_to_section_nr(start_pfn);
	ms = __nr_to_section(section_nr);

	memmap = sparse_decode_mem_map(ms->section_mem_map, section_nr);

	register_page_bootmem_memmap(section_nr, memmap, PAGES_PER_SECTION);

	usemap = __nr_to_section(section_nr)->pageblock_flags;
	page = virt_to_page(usemap);

	mapsize = PAGE_ALIGN(usemap_size()) >> PAGE_SHIFT;

	for (i = 0; i < mapsize; i++, page++)
		get_page_bootmem(section_nr, page, MIX_SECTION_INFO);
}
#endif /* !CONFIG_SPARSEMEM_VMEMMAP */

void register_page_bootmem_info_node(struct pglist_data *pgdat)
{
	unsigned long i, pfn, end_pfn, nr_pages;
	int node = pgdat->node_id;
	struct page *page;
	struct zone *zone;

	nr_pages = PAGE_ALIGN(sizeof(struct pglist_data)) >> PAGE_SHIFT;
	page = virt_to_page(pgdat);

	for (i = 0; i < nr_pages; i++, page++)
		get_page_bootmem(node, page, NODE_INFO);

	zone = &pgdat->node_zones[0];
	for (; zone < pgdat->node_zones + MAX_NR_ZONES - 1; zone++) {
		if (zone_is_initialized(zone)) {
			nr_pages = zone->wait_table_hash_nr_entries
				* sizeof(wait_queue_head_t);
			nr_pages = PAGE_ALIGN(nr_pages) >> PAGE_SHIFT;
			page = virt_to_page(zone->wait_table);

			for (i = 0; i < nr_pages; i++, page++)
				get_page_bootmem(node, page, NODE_INFO);
		}
	}

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
		if (pfn_valid(pfn) && (pfn_to_nid(pfn) == node))
			register_page_bootmem_info_section(pfn);
	}
}
#endif /* CONFIG_HAVE_BOOTMEM_INFO_NODE */

static void grow_zone_span(struct zone *zone, unsigned long start_pfn,
			   unsigned long end_pfn)
{
	unsigned long old_zone_end_pfn;

	zone_span_writelock(zone);

	old_zone_end_pfn = zone_end_pfn(zone);
	if (zone_is_empty(zone) || start_pfn < zone->zone_start_pfn)
		zone->zone_start_pfn = start_pfn;

	zone->spanned_pages = max(old_zone_end_pfn, end_pfn) -
				zone->zone_start_pfn;

	zone_span_writeunlock(zone);
}

static void resize_zone(struct zone *zone, unsigned long start_pfn,
		unsigned long end_pfn)
{
	zone_span_writelock(zone);

	if (end_pfn - start_pfn) {
		zone->zone_start_pfn = start_pfn;
		zone->spanned_pages = end_pfn - start_pfn;
	} else {
		/*
		 * make it consist as free_area_init_core(),
		 * if spanned_pages = 0, then keep start_pfn = 0
		 */
		zone->zone_start_pfn = 0;
		zone->spanned_pages = 0;
	}

	zone_span_writeunlock(zone);
}

static void fix_zone_id(struct zone *zone, unsigned long start_pfn,
		unsigned long end_pfn)
{
	enum zone_type zid = zone_idx(zone);
	int nid = zone->zone_pgdat->node_id;
	unsigned long pfn;

	for (pfn = start_pfn; pfn < end_pfn; pfn++)
		set_page_links(pfn_to_page(pfn), zid, nid, pfn);
}

/* Can fail with -ENOMEM from allocating a wait table with vmalloc() or
 * alloc_bootmem_node_nopanic() */
static int __ref ensure_zone_is_initialized(struct zone *zone,
			unsigned long start_pfn, unsigned long num_pages)
{
	if (!zone_is_initialized(zone))
		return init_currently_empty_zone(zone, start_pfn, num_pages,
						 MEMMAP_HOTPLUG);
	return 0;
}

static int __meminit move_pfn_range_left(struct zone *z1, struct zone *z2,
		unsigned long start_pfn, unsigned long end_pfn)
{
	int ret;
	unsigned long flags;
	unsigned long z1_start_pfn;

	ret = ensure_zone_is_initialized(z1, start_pfn, end_pfn - start_pfn);
	if (ret)
		return ret;

	pgdat_resize_lock(z1->zone_pgdat, &flags);

	/* can't move pfns which are higher than @z2 */
	if (end_pfn > zone_end_pfn(z2))
		goto out_fail;
	/* the move out part must be at the left most of @z2 */
	if (start_pfn > z2->zone_start_pfn)
		goto out_fail;
	/* must included/overlap */
	if (end_pfn <= z2->zone_start_pfn)
		goto out_fail;

	/* use start_pfn for z1's start_pfn if z1 is empty */
	if (!zone_is_empty(z1))
		z1_start_pfn = z1->zone_start_pfn;
	else
		z1_start_pfn = start_pfn;

	resize_zone(z1, z1_start_pfn, end_pfn);
	resize_zone(z2, end_pfn, zone_end_pfn(z2));

	pgdat_resize_unlock(z1->zone_pgdat, &flags);

	fix_zone_id(z1, start_pfn, end_pfn);

	return 0;
out_fail:
	pgdat_resize_unlock(z1->zone_pgdat, &flags);
	return -1;
}

static int __meminit move_pfn_range_right(struct zone *z1, struct zone *z2,
		unsigned long start_pfn, unsigned long end_pfn)
{
	int ret;
	unsigned long flags;
	unsigned long z2_end_pfn;

	ret = ensure_zone_is_initialized(z2, start_pfn, end_pfn - start_pfn);
	if (ret)
		return ret;

	pgdat_resize_lock(z1->zone_pgdat, &flags);

	/* can't move pfns which are lower than @z1 */
	if (z1->zone_start_pfn > start_pfn)
		goto out_fail;
	/* the move out part mast at the right most of @z1 */
	if (zone_end_pfn(z1) >  end_pfn)
		goto out_fail;
	/* must included/overlap */
	if (start_pfn >= zone_end_pfn(z1))
		goto out_fail;

	/* use end_pfn for z2's end_pfn if z2 is empty */
	if (!zone_is_empty(z2))
		z2_end_pfn = zone_end_pfn(z2);
	else
		z2_end_pfn = end_pfn;

	resize_zone(z1, z1->zone_start_pfn, start_pfn);
	resize_zone(z2, start_pfn, z2_end_pfn);

	pgdat_resize_unlock(z1->zone_pgdat, &flags);

	fix_zone_id(z2, start_pfn, end_pfn);

	return 0;
out_fail:
	pgdat_resize_unlock(z1->zone_pgdat, &flags);
	return -1;
}

static void grow_pgdat_span(struct pglist_data *pgdat, unsigned long start_pfn,
			    unsigned long end_pfn)
{
	unsigned long old_pgdat_end_pfn = pgdat_end_pfn(pgdat);

	if (!pgdat->node_spanned_pages || start_pfn < pgdat->node_start_pfn)
		pgdat->node_start_pfn = start_pfn;

	pgdat->node_spanned_pages = max(old_pgdat_end_pfn, end_pfn) -
					pgdat->node_start_pfn;
}

static int __meminit __add_zone(struct zone *zone, unsigned long phys_start_pfn)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int nr_pages = PAGES_PER_SECTION;
	int nid = pgdat->node_id;
	int zone_type;
	unsigned long flags;
	int ret;

	zone_type = zone - pgdat->node_zones;
	ret = ensure_zone_is_initialized(zone, phys_start_pfn, nr_pages);
	if (ret)
		return ret;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	grow_zone_span(zone, phys_start_pfn, phys_start_pfn + nr_pages);
	grow_pgdat_span(zone->zone_pgdat, phys_start_pfn,
			phys_start_pfn + nr_pages);
	pgdat_resize_unlock(zone->zone_pgdat, &flags);
	memmap_init_zone(nr_pages, nid, zone_type,
			 phys_start_pfn, MEMMAP_HOTPLUG);
	return 0;
}

static int __meminit __add_section(int nid, struct zone *zone,
					unsigned long phys_start_pfn)
{
	int ret;

	if (pfn_valid(phys_start_pfn))
		return -EEXIST;

	ret = sparse_add_one_section(zone, phys_start_pfn);

	if (ret < 0)
		return ret;

	ret = __add_zone(zone, phys_start_pfn);

	if (ret < 0)
		return ret;

	return register_new_memory(nid, __pfn_to_section(phys_start_pfn));
}

/*
 * Reasonably generic function for adding memory.  It is
 * expected that archs that support memory hotplug will
 * call this function after deciding the zone to which to
 * add the new pages.
 */
int __ref __add_pages(int nid, struct zone *zone, unsigned long phys_start_pfn,
			unsigned long nr_pages)
{
	unsigned long i;
	int err = 0;
	int start_sec, end_sec;
	/* during initialize mem_map, align hot-added range to section */
	start_sec = pfn_to_section_nr(phys_start_pfn);
	end_sec = pfn_to_section_nr(phys_start_pfn + nr_pages - 1);

	for (i = start_sec; i <= end_sec; i++) {
		err = __add_section(nid, zone, i << PFN_SECTION_SHIFT);

		/*
		 * EEXIST is finally dealt with by ioresource collision
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

#ifdef CONFIG_MEMORY_HOTREMOVE
/* find the smallest valid pfn in the range [start_pfn, end_pfn) */
static int find_smallest_section_pfn(int nid, struct zone *zone,
				     unsigned long start_pfn,
				     unsigned long end_pfn)
{
	struct mem_section *ms;

	for (; start_pfn < end_pfn; start_pfn += PAGES_PER_SECTION) {
		ms = __pfn_to_section(start_pfn);

		if (unlikely(!valid_section(ms)))
			continue;

		if (unlikely(pfn_to_nid(start_pfn) != nid))
			continue;

		if (zone && zone != page_zone(pfn_to_page(start_pfn)))
			continue;

		return start_pfn;
	}

	return 0;
}

/* find the biggest valid pfn in the range [start_pfn, end_pfn). */
static int find_biggest_section_pfn(int nid, struct zone *zone,
				    unsigned long start_pfn,
				    unsigned long end_pfn)
{
	struct mem_section *ms;
	unsigned long pfn;

	/* pfn is the end pfn of a memory section. */
	pfn = end_pfn - 1;
	for (; pfn >= start_pfn; pfn -= PAGES_PER_SECTION) {
		ms = __pfn_to_section(pfn);

		if (unlikely(!valid_section(ms)))
			continue;

		if (unlikely(pfn_to_nid(pfn) != nid))
			continue;

		if (zone && zone != page_zone(pfn_to_page(pfn)))
			continue;

		return pfn;
	}

	return 0;
}

static void shrink_zone_span(struct zone *zone, unsigned long start_pfn,
			     unsigned long end_pfn)
{
	unsigned long zone_start_pfn = zone->zone_start_pfn;
	unsigned long z = zone_end_pfn(zone); /* zone_end_pfn namespace clash */
	unsigned long zone_end_pfn = z;
	unsigned long pfn;
	struct mem_section *ms;
	int nid = zone_to_nid(zone);

	zone_span_writelock(zone);
	if (zone_start_pfn == start_pfn) {
		/*
		 * If the section is smallest section in the zone, it need
		 * shrink zone->zone_start_pfn and zone->zone_spanned_pages.
		 * In this case, we find second smallest valid mem_section
		 * for shrinking zone.
		 */
		pfn = find_smallest_section_pfn(nid, zone, end_pfn,
						zone_end_pfn);
		if (pfn) {
			zone->zone_start_pfn = pfn;
			zone->spanned_pages = zone_end_pfn - pfn;
		}
	} else if (zone_end_pfn == end_pfn) {
		/*
		 * If the section is biggest section in the zone, it need
		 * shrink zone->spanned_pages.
		 * In this case, we find second biggest valid mem_section for
		 * shrinking zone.
		 */
		pfn = find_biggest_section_pfn(nid, zone, zone_start_pfn,
					       start_pfn);
		if (pfn)
			zone->spanned_pages = pfn - zone_start_pfn + 1;
	}

	/*
	 * The section is not biggest or smallest mem_section in the zone, it
	 * only creates a hole in the zone. So in this case, we need not
	 * change the zone. But perhaps, the zone has only hole data. Thus
	 * it check the zone has only hole or not.
	 */
	pfn = zone_start_pfn;
	for (; pfn < zone_end_pfn; pfn += PAGES_PER_SECTION) {
		ms = __pfn_to_section(pfn);

		if (unlikely(!valid_section(ms)))
			continue;

		if (page_zone(pfn_to_page(pfn)) != zone)
			continue;

		 /* If the section is current section, it continues the loop */
		if (start_pfn == pfn)
			continue;

		/* If we find valid section, we have nothing to do */
		zone_span_writeunlock(zone);
		return;
	}

	/* The zone has no valid section */
	zone->zone_start_pfn = 0;
	zone->spanned_pages = 0;
	zone_span_writeunlock(zone);
}

static void shrink_pgdat_span(struct pglist_data *pgdat,
			      unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pgdat_start_pfn = pgdat->node_start_pfn;
	unsigned long p = pgdat_end_pfn(pgdat); /* pgdat_end_pfn namespace clash */
	unsigned long pgdat_end_pfn = p;
	unsigned long pfn;
	struct mem_section *ms;
	int nid = pgdat->node_id;

	if (pgdat_start_pfn == start_pfn) {
		/*
		 * If the section is smallest section in the pgdat, it need
		 * shrink pgdat->node_start_pfn and pgdat->node_spanned_pages.
		 * In this case, we find second smallest valid mem_section
		 * for shrinking zone.
		 */
		pfn = find_smallest_section_pfn(nid, NULL, end_pfn,
						pgdat_end_pfn);
		if (pfn) {
			pgdat->node_start_pfn = pfn;
			pgdat->node_spanned_pages = pgdat_end_pfn - pfn;
		}
	} else if (pgdat_end_pfn == end_pfn) {
		/*
		 * If the section is biggest section in the pgdat, it need
		 * shrink pgdat->node_spanned_pages.
		 * In this case, we find second biggest valid mem_section for
		 * shrinking zone.
		 */
		pfn = find_biggest_section_pfn(nid, NULL, pgdat_start_pfn,
					       start_pfn);
		if (pfn)
			pgdat->node_spanned_pages = pfn - pgdat_start_pfn + 1;
	}

	/*
	 * If the section is not biggest or smallest mem_section in the pgdat,
	 * it only creates a hole in the pgdat. So in this case, we need not
	 * change the pgdat.
	 * But perhaps, the pgdat has only hole data. Thus it check the pgdat
	 * has only hole or not.
	 */
	pfn = pgdat_start_pfn;
	for (; pfn < pgdat_end_pfn; pfn += PAGES_PER_SECTION) {
		ms = __pfn_to_section(pfn);

		if (unlikely(!valid_section(ms)))
			continue;

		if (pfn_to_nid(pfn) != nid)
			continue;

		 /* If the section is current section, it continues the loop */
		if (start_pfn == pfn)
			continue;

		/* If we find valid section, we have nothing to do */
		return;
	}

	/* The pgdat has no valid section */
	pgdat->node_start_pfn = 0;
	pgdat->node_spanned_pages = 0;
}

static void __remove_zone(struct zone *zone, unsigned long start_pfn)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int nr_pages = PAGES_PER_SECTION;
	int zone_type;
	unsigned long flags;

	zone_type = zone - pgdat->node_zones;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	shrink_zone_span(zone, start_pfn, start_pfn + nr_pages);
	shrink_pgdat_span(pgdat, start_pfn, start_pfn + nr_pages);
	pgdat_resize_unlock(zone->zone_pgdat, &flags);
}

static int __remove_section(struct zone *zone, struct mem_section *ms)
{
	unsigned long start_pfn;
	int scn_nr;
	int ret = -EINVAL;

	if (!valid_section(ms))
		return ret;

	ret = unregister_memory_section(ms);
	if (ret)
		return ret;

	scn_nr = __section_nr(ms);
	start_pfn = section_nr_to_pfn(scn_nr);
	__remove_zone(zone, start_pfn);

	sparse_remove_one_section(zone, ms);
	return 0;
}

/**
 * __remove_pages() - remove sections of pages from a zone
 * @zone: zone from which pages need to be removed
 * @phys_start_pfn: starting pageframe (must be aligned to start of a section)
 * @nr_pages: number of pages to remove (must be multiple of section size)
 *
 * Generic helper function to remove section mappings and sysfs entries
 * for the section of the memory we are removing. Caller needs to make
 * sure that pages are marked reserved and zones are adjust properly by
 * calling offline_pages().
 */
int __remove_pages(struct zone *zone, unsigned long phys_start_pfn,
		 unsigned long nr_pages)
{
	unsigned long i;
	int sections_to_remove;
	resource_size_t start, size;
	int ret = 0;

	/*
	 * We can only remove entire sections
	 */
	BUG_ON(phys_start_pfn & ~PAGE_SECTION_MASK);
	BUG_ON(nr_pages % PAGES_PER_SECTION);

	start = phys_start_pfn << PAGE_SHIFT;
	size = nr_pages * PAGE_SIZE;
	ret = release_mem_region_adjustable(&iomem_resource, start, size);
	if (ret) {
		resource_size_t endres = start + size - 1;

		pr_warn("Unable to release resource <%pa-%pa> (%d)\n",
				&start, &endres, ret);
	}

	sections_to_remove = nr_pages / PAGES_PER_SECTION;
	for (i = 0; i < sections_to_remove; i++) {
		unsigned long pfn = phys_start_pfn + i*PAGES_PER_SECTION;
		ret = __remove_section(zone, __pfn_to_section(pfn));
		if (ret)
			break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(__remove_pages);
#endif /* CONFIG_MEMORY_HOTREMOVE */

int set_online_page_callback(online_page_callback_t callback)
{
	int rc = -EINVAL;

	lock_memory_hotplug();

	if (online_page_callback == generic_online_page) {
		online_page_callback = callback;
		rc = 0;
	}

	unlock_memory_hotplug();

	return rc;
}
EXPORT_SYMBOL_GPL(set_online_page_callback);

int restore_online_page_callback(online_page_callback_t callback)
{
	int rc = -EINVAL;

	lock_memory_hotplug();

	if (online_page_callback == callback) {
		online_page_callback = generic_online_page;
		rc = 0;
	}

	unlock_memory_hotplug();

	return rc;
}
EXPORT_SYMBOL_GPL(restore_online_page_callback);

void __online_page_set_limits(struct page *page)
{
}
EXPORT_SYMBOL_GPL(__online_page_set_limits);

void __online_page_increment_counters(struct page *page)
{
	adjust_managed_page_count(page, 1);
}
EXPORT_SYMBOL_GPL(__online_page_increment_counters);

void __online_page_free(struct page *page)
{
	__free_reserved_page(page);
}
EXPORT_SYMBOL_GPL(__online_page_free);

static void generic_online_page(struct page *page)
{
	__online_page_set_limits(page);
	__online_page_increment_counters(page);
	__online_page_free(page);
}

static int online_pages_range(unsigned long start_pfn, unsigned long nr_pages,
			void *arg)
{
	unsigned long i;
	unsigned long onlined_pages = *(unsigned long *)arg;
	struct page *page;
	if (PageReserved(pfn_to_page(start_pfn)))
		for (i = 0; i < nr_pages; i++) {
			page = pfn_to_page(start_pfn + i);
			(*online_page_callback)(page);
			onlined_pages++;
		}
	*(unsigned long *)arg = onlined_pages;
	return 0;
}

#ifdef CONFIG_MOVABLE_NODE
/*
 * When CONFIG_MOVABLE_NODE, we permit onlining of a node which doesn't have
 * normal memory.
 */
static bool can_online_high_movable(struct zone *zone)
{
	return true;
}
#else /* CONFIG_MOVABLE_NODE */
/* ensure every online node has NORMAL memory */
static bool can_online_high_movable(struct zone *zone)
{
	return node_state(zone_to_nid(zone), N_NORMAL_MEMORY);
}
#endif /* CONFIG_MOVABLE_NODE */

/* check which state of node_states will be changed when online memory */
static void node_states_check_changes_online(unsigned long nr_pages,
	struct zone *zone, struct memory_notify *arg)
{
	int nid = zone_to_nid(zone);
	enum zone_type zone_last = ZONE_NORMAL;

	/*
	 * If we have HIGHMEM or movable node, node_states[N_NORMAL_MEMORY]
	 * contains nodes which have zones of 0...ZONE_NORMAL,
	 * set zone_last to ZONE_NORMAL.
	 *
	 * If we don't have HIGHMEM nor movable node,
	 * node_states[N_NORMAL_MEMORY] contains nodes which have zones of
	 * 0...ZONE_MOVABLE, set zone_last to ZONE_MOVABLE.
	 */
	if (N_MEMORY == N_NORMAL_MEMORY)
		zone_last = ZONE_MOVABLE;

	/*
	 * if the memory to be online is in a zone of 0...zone_last, and
	 * the zones of 0...zone_last don't have memory before online, we will
	 * need to set the node to node_states[N_NORMAL_MEMORY] after
	 * the memory is online.
	 */
	if (zone_idx(zone) <= zone_last && !node_state(nid, N_NORMAL_MEMORY))
		arg->status_change_nid_normal = nid;
	else
		arg->status_change_nid_normal = -1;

#ifdef CONFIG_HIGHMEM
	/*
	 * If we have movable node, node_states[N_HIGH_MEMORY]
	 * contains nodes which have zones of 0...ZONE_HIGHMEM,
	 * set zone_last to ZONE_HIGHMEM.
	 *
	 * If we don't have movable node, node_states[N_NORMAL_MEMORY]
	 * contains nodes which have zones of 0...ZONE_MOVABLE,
	 * set zone_last to ZONE_MOVABLE.
	 */
	zone_last = ZONE_HIGHMEM;
	if (N_MEMORY == N_HIGH_MEMORY)
		zone_last = ZONE_MOVABLE;

	if (zone_idx(zone) <= zone_last && !node_state(nid, N_HIGH_MEMORY))
		arg->status_change_nid_high = nid;
	else
		arg->status_change_nid_high = -1;
#else
	arg->status_change_nid_high = arg->status_change_nid_normal;
#endif

	/*
	 * if the node don't have memory befor online, we will need to
	 * set the node to node_states[N_MEMORY] after the memory
	 * is online.
	 */
	if (!node_state(nid, N_MEMORY))
		arg->status_change_nid = nid;
	else
		arg->status_change_nid = -1;
}

static void node_states_set_node(int node, struct memory_notify *arg)
{
	if (arg->status_change_nid_normal >= 0)
		node_set_state(node, N_NORMAL_MEMORY);

	if (arg->status_change_nid_high >= 0)
		node_set_state(node, N_HIGH_MEMORY);

	node_set_state(node, N_MEMORY);
}


int __ref online_pages(unsigned long pfn, unsigned long nr_pages, int online_type)
{
	unsigned long flags;
	unsigned long onlined_pages = 0;
	struct zone *zone;
	int need_zonelists_rebuild = 0;
	int nid;
	int ret;
	struct memory_notify arg;

	lock_memory_hotplug();
	/*
	 * This doesn't need a lock to do pfn_to_page().
	 * The section can't be removed here because of the
	 * memory_block->state_mutex.
	 */
	zone = page_zone(pfn_to_page(pfn));

	if ((zone_idx(zone) > ZONE_NORMAL || online_type == ONLINE_MOVABLE) &&
	    !can_online_high_movable(zone)) {
		unlock_memory_hotplug();
		return -EINVAL;
	}

	if (online_type == ONLINE_KERNEL && zone_idx(zone) == ZONE_MOVABLE) {
		if (move_pfn_range_left(zone - 1, zone, pfn, pfn + nr_pages)) {
			unlock_memory_hotplug();
			return -EINVAL;
		}
	}
	if (online_type == ONLINE_MOVABLE && zone_idx(zone) == ZONE_MOVABLE - 1) {
		if (move_pfn_range_right(zone, zone + 1, pfn, pfn + nr_pages)) {
			unlock_memory_hotplug();
			return -EINVAL;
		}
	}

	/* Previous code may changed the zone of the pfn range */
	zone = page_zone(pfn_to_page(pfn));

	arg.start_pfn = pfn;
	arg.nr_pages = nr_pages;
	node_states_check_changes_online(nr_pages, zone, &arg);

	nid = pfn_to_nid(pfn);

	ret = memory_notify(MEM_GOING_ONLINE, &arg);
	ret = notifier_to_errno(ret);
	if (ret) {
		memory_notify(MEM_CANCEL_ONLINE, &arg);
		unlock_memory_hotplug();
		return ret;
	}
	/*
	 * If this zone is not populated, then it is not in zonelist.
	 * This means the page allocator ignores this zone.
	 * So, zonelist must be updated after online.
	 */
	mutex_lock(&zonelists_mutex);
	if (!populated_zone(zone)) {
		need_zonelists_rebuild = 1;
		build_all_zonelists(NULL, zone);
	}

	ret = walk_system_ram_range(pfn, nr_pages, &onlined_pages,
		online_pages_range);
	if (ret) {
		if (need_zonelists_rebuild)
			zone_pcp_reset(zone);
		mutex_unlock(&zonelists_mutex);
		printk(KERN_DEBUG "online_pages [mem %#010llx-%#010llx] failed\n",
		       (unsigned long long) pfn << PAGE_SHIFT,
		       (((unsigned long long) pfn + nr_pages)
			    << PAGE_SHIFT) - 1);
		memory_notify(MEM_CANCEL_ONLINE, &arg);
		unlock_memory_hotplug();
		return ret;
	}

	zone->present_pages += onlined_pages;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	zone->zone_pgdat->node_present_pages += onlined_pages;
	pgdat_resize_unlock(zone->zone_pgdat, &flags);

	if (onlined_pages) {
		node_states_set_node(zone_to_nid(zone), &arg);
		if (need_zonelists_rebuild)
			build_all_zonelists(NULL, NULL);
		else
			zone_pcp_update(zone);
	}

	mutex_unlock(&zonelists_mutex);

	init_per_zone_wmark_min();

	if (onlined_pages)
		kswapd_run(zone_to_nid(zone));

	vm_total_pages = nr_free_pagecache_pages();

	writeback_set_ratelimit();

	if (onlined_pages)
		memory_notify(MEM_ONLINE, &arg);
	unlock_memory_hotplug();

	return 0;
}
#endif /* CONFIG_MEMORY_HOTPLUG_SPARSE */

/* we are OK calling __meminit stuff here - we have CONFIG_MEMORY_HOTPLUG */
static pg_data_t __ref *hotadd_new_pgdat(int nid, u64 start)
{
	struct pglist_data *pgdat;
	unsigned long zones_size[MAX_NR_ZONES] = {0};
	unsigned long zholes_size[MAX_NR_ZONES] = {0};
	unsigned long start_pfn = start >> PAGE_SHIFT;

	pgdat = NODE_DATA(nid);
	if (!pgdat) {
		pgdat = arch_alloc_nodedata(nid);
		if (!pgdat)
			return NULL;

		arch_refresh_nodedata(nid, pgdat);
	}

	/* we can use NODE_DATA(nid) from here */

	/* init node's zones as empty zones, we don't have any present pages.*/
	free_area_init_node(nid, zones_size, start_pfn, zholes_size);

	/*
	 * The node we allocated has no zone fallback lists. For avoiding
	 * to access not-initialized zonelist, build here.
	 */
	mutex_lock(&zonelists_mutex);
	build_all_zonelists(pgdat, NULL);
	mutex_unlock(&zonelists_mutex);

	return pgdat;
}

static void rollback_node_hotadd(int nid, pg_data_t *pgdat)
{
	arch_refresh_nodedata(nid, NULL);
	arch_free_nodedata(pgdat);
	return;
}


/**
 * try_online_node - online a node if offlined
 *
 * called by cpu_up() to online a node without onlined memory.
 */
int try_online_node(int nid)
{
	pg_data_t	*pgdat;
	int	ret;

	if (node_online(nid))
		return 0;

	lock_memory_hotplug();
	pgdat = hotadd_new_pgdat(nid, 0);
	if (!pgdat) {
		pr_err("Cannot online node %d due to NULL pgdat\n", nid);
		ret = -ENOMEM;
		goto out;
	}
	node_set_online(nid);
	ret = register_one_node(nid);
	BUG_ON(ret);

	if (pgdat->node_zonelists->_zonerefs->zone == NULL) {
		mutex_lock(&zonelists_mutex);
		build_all_zonelists(NULL, NULL);
		mutex_unlock(&zonelists_mutex);
	}

out:
	unlock_memory_hotplug();
	return ret;
}

static int check_hotplug_memory_range(u64 start, u64 size)
{
	u64 start_pfn = start >> PAGE_SHIFT;
	u64 nr_pages = size >> PAGE_SHIFT;

	/* Memory range must be aligned with section */
	if ((start_pfn & ~PAGE_SECTION_MASK) ||
	    (nr_pages % PAGES_PER_SECTION) || (!nr_pages)) {
		pr_err("Section-unaligned hotplug range: start 0x%llx, size 0x%llx\n",
				(unsigned long long)start,
				(unsigned long long)size);
		return -EINVAL;
	}

	return 0;
}

/* we are OK calling __meminit stuff here - we have CONFIG_MEMORY_HOTPLUG */
int __ref add_memory(int nid, u64 start, u64 size)
{
	pg_data_t *pgdat = NULL;
	bool new_pgdat;
	bool new_node;
	struct resource *res;
	int ret;

	ret = check_hotplug_memory_range(start, size);
	if (ret)
		return ret;

	lock_memory_hotplug();

	res = register_memory_resource(start, size);
	ret = -EEXIST;
	if (!res)
		goto out;

	{	/* Stupid hack to suppress address-never-null warning */
		void *p = NODE_DATA(nid);
		new_pgdat = !p;
	}
	new_node = !node_online(nid);
	if (new_node) {
		pgdat = hotadd_new_pgdat(nid, start);
		ret = -ENOMEM;
		if (!pgdat)
			goto error;
	}

	/* call arch's memory hotadd */
	ret = arch_add_memory(nid, start, size);

	if (ret < 0)
		goto error;

	/* we online node here. we can't roll back from here. */
	node_set_online(nid);

	if (new_node) {
		ret = register_one_node(nid);
		/*
		 * If sysfs file of new node can't create, cpu on the node
		 * can't be hot-added. There is no rollback way now.
		 * So, check by BUG_ON() to catch it reluctantly..
		 */
		BUG_ON(ret);
	}

	/* create new memmap entry */
	firmware_map_add_hotplug(start, start + size, "System RAM");

	goto out;

error:
	/* rollback pgdat allocation and others */
	if (new_pgdat)
		rollback_node_hotadd(nid, pgdat);
	release_memory_resource(res);

out:
	unlock_memory_hotplug();
	return ret;
}
EXPORT_SYMBOL_GPL(add_memory);

#ifdef CONFIG_MEMORY_HOTREMOVE
/*
 * A free page on the buddy free lists (not the per-cpu lists) has PageBuddy
 * set and the size of the free page is given by page_order(). Using this,
 * the function determines if the pageblock contains only free pages.
 * Due to buddy contraints, a free page at least the size of a pageblock will
 * be located at the start of the pageblock
 */
static inline int pageblock_free(struct page *page)
{
	return PageBuddy(page) && page_order(page) >= pageblock_order;
}

/* Return the start of the next active pageblock after a given page */
static struct page *next_active_pageblock(struct page *page)
{
	/* Ensure the starting page is pageblock-aligned */
	BUG_ON(page_to_pfn(page) & (pageblock_nr_pages - 1));

	/* If the entire pageblock is free, move to the end of free page */
	if (pageblock_free(page)) {
		int order;
		/* be careful. we don't have locks, page_order can be changed.*/
		order = page_order(page);
		if ((order < MAX_ORDER) && (order >= pageblock_order))
			return page + (1 << order);
	}

	return page + pageblock_nr_pages;
}

/* Checks if this range of memory is likely to be hot-removable. */
int is_mem_section_removable(unsigned long start_pfn, unsigned long nr_pages)
{
	struct page *page = pfn_to_page(start_pfn);
	struct page *end_page = page + nr_pages;

	/* Check the starting page of each pageblock within the range */
	for (; page < end_page; page = next_active_pageblock(page)) {
		if (!is_pageblock_removable_nolock(page))
			return 0;
		cond_resched();
	}

	/* All pageblocks in the memory block are likely to be hot-removable */
	return 1;
}

/*
 * Confirm all pages in a range [start, end) is belongs to the same zone.
 */
static int test_pages_in_a_zone(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;
	struct zone *zone = NULL;
	struct page *page;
	int i;
	for (pfn = start_pfn;
	     pfn < end_pfn;
	     pfn += MAX_ORDER_NR_PAGES) {
		i = 0;
		/* This is just a CONFIG_HOLES_IN_ZONE check.*/
		while ((i < MAX_ORDER_NR_PAGES) && !pfn_valid_within(pfn + i))
			i++;
		if (i == MAX_ORDER_NR_PAGES)
			continue;
		page = pfn_to_page(pfn + i);
		if (zone && page_zone(page) != zone)
			return 0;
		zone = page_zone(page);
	}
	return 1;
}

/*
 * Scan pfn range [start,end) to find movable/migratable pages (LRU pages
 * and hugepages). We scan pfn because it's much easier than scanning over
 * linked list. This function returns the pfn of the first found movable
 * page if it's found, otherwise 0.
 */
static unsigned long scan_movable_pages(unsigned long start, unsigned long end)
{
	unsigned long pfn;
	struct page *page;
	for (pfn = start; pfn < end; pfn++) {
		if (pfn_valid(pfn)) {
			page = pfn_to_page(pfn);
			if (PageLRU(page))
				return pfn;
			if (PageHuge(page)) {
				if (is_hugepage_active(page))
					return pfn;
				else
					pfn = round_up(pfn + 1,
						1 << compound_order(page)) - 1;
			}
		}
	}
	return 0;
}

#define NR_OFFLINE_AT_ONCE_PAGES	(256)
static int
do_migrate_range(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;
	struct page *page;
	int move_pages = NR_OFFLINE_AT_ONCE_PAGES;
	int not_managed = 0;
	int ret = 0;
	LIST_HEAD(source);

	for (pfn = start_pfn; pfn < end_pfn && move_pages > 0; pfn++) {
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);

		if (PageHuge(page)) {
			struct page *head = compound_head(page);
			pfn = page_to_pfn(head) + (1<<compound_order(head)) - 1;
			if (compound_order(head) > PFN_SECTION_SHIFT) {
				ret = -EBUSY;
				break;
			}
			if (isolate_huge_page(page, &source))
				move_pages -= 1 << compound_order(head);
			continue;
		}

		if (!get_page_unless_zero(page))
			continue;
		/*
		 * We can skip free pages. And we can only deal with pages on
		 * LRU.
		 */
		ret = isolate_lru_page(page);
		if (!ret) { /* Success */
			put_page(page);
			list_add_tail(&page->lru, &source);
			move_pages--;
			inc_zone_page_state(page, NR_ISOLATED_ANON +
					    page_is_file_cache(page));

		} else {
#ifdef CONFIG_DEBUG_VM
			printk(KERN_ALERT "removing pfn %lx from LRU failed\n",
			       pfn);
			dump_page(page);
#endif
			put_page(page);
			/* Because we don't have big zone->lock. we should
			   check this again here. */
			if (page_count(page)) {
				not_managed++;
				ret = -EBUSY;
				break;
			}
		}
	}
	if (!list_empty(&source)) {
		if (not_managed) {
			putback_movable_pages(&source);
			goto out;
		}

		/*
		 * alloc_migrate_target should be improooooved!!
		 * migrate_pages returns # of failed pages.
		 */
		ret = migrate_pages(&source, alloc_migrate_target, 0,
					MIGRATE_SYNC, MR_MEMORY_HOTPLUG);
		if (ret)
			putback_movable_pages(&source);
	}
out:
	return ret;
}

/*
 * remove from free_area[] and mark all as Reserved.
 */
static int
offline_isolated_pages_cb(unsigned long start, unsigned long nr_pages,
			void *data)
{
	__offline_isolated_pages(start, start + nr_pages);
	return 0;
}

static void
offline_isolated_pages(unsigned long start_pfn, unsigned long end_pfn)
{
	walk_system_ram_range(start_pfn, end_pfn - start_pfn, NULL,
				offline_isolated_pages_cb);
}

/*
 * Check all pages in range, recoreded as memory resource, are isolated.
 */
static int
check_pages_isolated_cb(unsigned long start_pfn, unsigned long nr_pages,
			void *data)
{
	int ret;
	long offlined = *(long *)data;
	ret = test_pages_isolated(start_pfn, start_pfn + nr_pages, true);
	offlined = nr_pages;
	if (!ret)
		*(long *)data += offlined;
	return ret;
}

static long
check_pages_isolated(unsigned long start_pfn, unsigned long end_pfn)
{
	long offlined = 0;
	int ret;

	ret = walk_system_ram_range(start_pfn, end_pfn - start_pfn, &offlined,
			check_pages_isolated_cb);
	if (ret < 0)
		offlined = (long)ret;
	return offlined;
}

#ifdef CONFIG_MOVABLE_NODE
/*
 * When CONFIG_MOVABLE_NODE, we permit offlining of a node which doesn't have
 * normal memory.
 */
static bool can_offline_normal(struct zone *zone, unsigned long nr_pages)
{
	return true;
}
#else /* CONFIG_MOVABLE_NODE */
/* ensure the node has NORMAL memory if it is still online */
static bool can_offline_normal(struct zone *zone, unsigned long nr_pages)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	unsigned long present_pages = 0;
	enum zone_type zt;

	for (zt = 0; zt <= ZONE_NORMAL; zt++)
		present_pages += pgdat->node_zones[zt].present_pages;

	if (present_pages > nr_pages)
		return true;

	present_pages = 0;
	for (; zt <= ZONE_MOVABLE; zt++)
		present_pages += pgdat->node_zones[zt].present_pages;

	/*
	 * we can't offline the last normal memory until all
	 * higher memory is offlined.
	 */
	return present_pages == 0;
}
#endif /* CONFIG_MOVABLE_NODE */

static int __init cmdline_parse_movable_node(char *p)
{
#ifdef CONFIG_MOVABLE_NODE
	/*
	 * Memory used by the kernel cannot be hot-removed because Linux
	 * cannot migrate the kernel pages. When memory hotplug is
	 * enabled, we should prevent memblock from allocating memory
	 * for the kernel.
	 *
	 * ACPI SRAT records all hotpluggable memory ranges. But before
	 * SRAT is parsed, we don't know about it.
	 *
	 * The kernel image is loaded into memory at very early time. We
	 * cannot prevent this anyway. So on NUMA system, we set any
	 * node the kernel resides in as un-hotpluggable.
	 *
	 * Since on modern servers, one node could have double-digit
	 * gigabytes memory, we can assume the memory around the kernel
	 * image is also un-hotpluggable. So before SRAT is parsed, just
	 * allocate memory near the kernel image to try the best to keep
	 * the kernel away from hotpluggable memory.
	 */
	memblock_set_bottom_up(true);
#else
	pr_warn("movable_node option not supported\n");
#endif
	return 0;
}
early_param("movable_node", cmdline_parse_movable_node);

/* check which state of node_states will be changed when offline memory */
static void node_states_check_changes_offline(unsigned long nr_pages,
		struct zone *zone, struct memory_notify *arg)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	unsigned long present_pages = 0;
	enum zone_type zt, zone_last = ZONE_NORMAL;

	/*
	 * If we have HIGHMEM or movable node, node_states[N_NORMAL_MEMORY]
	 * contains nodes which have zones of 0...ZONE_NORMAL,
	 * set zone_last to ZONE_NORMAL.
	 *
	 * If we don't have HIGHMEM nor movable node,
	 * node_states[N_NORMAL_MEMORY] contains nodes which have zones of
	 * 0...ZONE_MOVABLE, set zone_last to ZONE_MOVABLE.
	 */
	if (N_MEMORY == N_NORMAL_MEMORY)
		zone_last = ZONE_MOVABLE;

	/*
	 * check whether node_states[N_NORMAL_MEMORY] will be changed.
	 * If the memory to be offline is in a zone of 0...zone_last,
	 * and it is the last present memory, 0...zone_last will
	 * become empty after offline , thus we can determind we will
	 * need to clear the node from node_states[N_NORMAL_MEMORY].
	 */
	for (zt = 0; zt <= zone_last; zt++)
		present_pages += pgdat->node_zones[zt].present_pages;
	if (zone_idx(zone) <= zone_last && nr_pages >= present_pages)
		arg->status_change_nid_normal = zone_to_nid(zone);
	else
		arg->status_change_nid_normal = -1;

#ifdef CONFIG_HIGHMEM
	/*
	 * If we have movable node, node_states[N_HIGH_MEMORY]
	 * contains nodes which have zones of 0...ZONE_HIGHMEM,
	 * set zone_last to ZONE_HIGHMEM.
	 *
	 * If we don't have movable node, node_states[N_NORMAL_MEMORY]
	 * contains nodes which have zones of 0...ZONE_MOVABLE,
	 * set zone_last to ZONE_MOVABLE.
	 */
	zone_last = ZONE_HIGHMEM;
	if (N_MEMORY == N_HIGH_MEMORY)
		zone_last = ZONE_MOVABLE;

	for (; zt <= zone_last; zt++)
		present_pages += pgdat->node_zones[zt].present_pages;
	if (zone_idx(zone) <= zone_last && nr_pages >= present_pages)
		arg->status_change_nid_high = zone_to_nid(zone);
	else
		arg->status_change_nid_high = -1;
#else
	arg->status_change_nid_high = arg->status_change_nid_normal;
#endif

	/*
	 * node_states[N_HIGH_MEMORY] contains nodes which have 0...ZONE_MOVABLE
	 */
	zone_last = ZONE_MOVABLE;

	/*
	 * check whether node_states[N_HIGH_MEMORY] will be changed
	 * If we try to offline the last present @nr_pages from the node,
	 * we can determind we will need to clear the node from
	 * node_states[N_HIGH_MEMORY].
	 */
	for (; zt <= zone_last; zt++)
		present_pages += pgdat->node_zones[zt].present_pages;
	if (nr_pages >= present_pages)
		arg->status_change_nid = zone_to_nid(zone);
	else
		arg->status_change_nid = -1;
}

static void node_states_clear_node(int node, struct memory_notify *arg)
{
	if (arg->status_change_nid_normal >= 0)
		node_clear_state(node, N_NORMAL_MEMORY);

	if ((N_MEMORY != N_NORMAL_MEMORY) &&
	    (arg->status_change_nid_high >= 0))
		node_clear_state(node, N_HIGH_MEMORY);

	if ((N_MEMORY != N_HIGH_MEMORY) &&
	    (arg->status_change_nid >= 0))
		node_clear_state(node, N_MEMORY);
}

static int __ref __offline_pages(unsigned long start_pfn,
		  unsigned long end_pfn, unsigned long timeout)
{
	unsigned long pfn, nr_pages, expire;
	long offlined_pages;
	int ret, drain, retry_max, node;
	unsigned long flags;
	struct zone *zone;
	struct memory_notify arg;

	/* at least, alignment against pageblock is necessary */
	if (!IS_ALIGNED(start_pfn, pageblock_nr_pages))
		return -EINVAL;
	if (!IS_ALIGNED(end_pfn, pageblock_nr_pages))
		return -EINVAL;
	/* This makes hotplug much easier...and readable.
	   we assume this for now. .*/
	if (!test_pages_in_a_zone(start_pfn, end_pfn))
		return -EINVAL;

	lock_memory_hotplug();

	zone = page_zone(pfn_to_page(start_pfn));
	node = zone_to_nid(zone);
	nr_pages = end_pfn - start_pfn;

	ret = -EINVAL;
	if (zone_idx(zone) <= ZONE_NORMAL && !can_offline_normal(zone, nr_pages))
		goto out;

	/* set above range as isolated */
	ret = start_isolate_page_range(start_pfn, end_pfn,
				       MIGRATE_MOVABLE, true);
	if (ret)
		goto out;

	arg.start_pfn = start_pfn;
	arg.nr_pages = nr_pages;
	node_states_check_changes_offline(nr_pages, zone, &arg);

	ret = memory_notify(MEM_GOING_OFFLINE, &arg);
	ret = notifier_to_errno(ret);
	if (ret)
		goto failed_removal;

	pfn = start_pfn;
	expire = jiffies + timeout;
	drain = 0;
	retry_max = 5;
repeat:
	/* start memory hot removal */
	ret = -EAGAIN;
	if (time_after(jiffies, expire))
		goto failed_removal;
	ret = -EINTR;
	if (signal_pending(current))
		goto failed_removal;
	ret = 0;
	if (drain) {
		lru_add_drain_all();
		cond_resched();
		drain_all_pages();
	}

	pfn = scan_movable_pages(start_pfn, end_pfn);
	if (pfn) { /* We have movable pages */
		ret = do_migrate_range(pfn, end_pfn);
		if (!ret) {
			drain = 1;
			goto repeat;
		} else {
			if (ret < 0)
				if (--retry_max == 0)
					goto failed_removal;
			yield();
			drain = 1;
			goto repeat;
		}
	}
	/* drain all zone's lru pagevec, this is asynchronous... */
	lru_add_drain_all();
	yield();
	/* drain pcp pages, this is synchronous. */
	drain_all_pages();
	/*
	 * dissolve free hugepages in the memory block before doing offlining
	 * actually in order to make hugetlbfs's object counting consistent.
	 */
	dissolve_free_huge_pages(start_pfn, end_pfn);
	/* check again */
	offlined_pages = check_pages_isolated(start_pfn, end_pfn);
	if (offlined_pages < 0) {
		ret = -EBUSY;
		goto failed_removal;
	}
	printk(KERN_INFO "Offlined Pages %ld\n", offlined_pages);
	/* Ok, all of our target is isolated.
	   We cannot do rollback at this point. */
	offline_isolated_pages(start_pfn, end_pfn);
	/* reset pagetype flags and makes migrate type to be MOVABLE */
	undo_isolate_page_range(start_pfn, end_pfn, MIGRATE_MOVABLE);
	/* removal success */
	adjust_managed_page_count(pfn_to_page(start_pfn), -offlined_pages);
	zone->present_pages -= offlined_pages;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	zone->zone_pgdat->node_present_pages -= offlined_pages;
	pgdat_resize_unlock(zone->zone_pgdat, &flags);

	init_per_zone_wmark_min();

	if (!populated_zone(zone)) {
		zone_pcp_reset(zone);
		mutex_lock(&zonelists_mutex);
		build_all_zonelists(NULL, NULL);
		mutex_unlock(&zonelists_mutex);
	} else
		zone_pcp_update(zone);

	node_states_clear_node(node, &arg);
	if (arg.status_change_nid >= 0)
		kswapd_stop(node);

	vm_total_pages = nr_free_pagecache_pages();
	writeback_set_ratelimit();

	memory_notify(MEM_OFFLINE, &arg);
	unlock_memory_hotplug();
	return 0;

failed_removal:
	printk(KERN_INFO "memory offlining [mem %#010llx-%#010llx] failed\n",
	       (unsigned long long) start_pfn << PAGE_SHIFT,
	       ((unsigned long long) end_pfn << PAGE_SHIFT) - 1);
	memory_notify(MEM_CANCEL_OFFLINE, &arg);
	/* pushback to free area */
	undo_isolate_page_range(start_pfn, end_pfn, MIGRATE_MOVABLE);

out:
	unlock_memory_hotplug();
	return ret;
}

int offline_pages(unsigned long start_pfn, unsigned long nr_pages)
{
	return __offline_pages(start_pfn, start_pfn + nr_pages, 120 * HZ);
}
#endif /* CONFIG_MEMORY_HOTREMOVE */

/**
 * walk_memory_range - walks through all mem sections in [start_pfn, end_pfn)
 * @start_pfn: start pfn of the memory range
 * @end_pfn: end pfn of the memory range
 * @arg: argument passed to func
 * @func: callback for each memory section walked
 *
 * This function walks through all present mem sections in range
 * [start_pfn, end_pfn) and call func on each mem section.
 *
 * Returns the return value of func.
 */
int walk_memory_range(unsigned long start_pfn, unsigned long end_pfn,
		void *arg, int (*func)(struct memory_block *, void *))
{
	struct memory_block *mem = NULL;
	struct mem_section *section;
	unsigned long pfn, section_nr;
	int ret;

	for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		section_nr = pfn_to_section_nr(pfn);
		if (!present_section_nr(section_nr))
			continue;

		section = __nr_to_section(section_nr);
		/* same memblock? */
		if (mem)
			if ((section_nr >= mem->start_section_nr) &&
			    (section_nr <= mem->end_section_nr))
				continue;

		mem = find_memory_block_hinted(section, mem);
		if (!mem)
			continue;

		ret = func(mem, arg);
		if (ret) {
			kobject_put(&mem->dev.kobj);
			return ret;
		}
	}

	if (mem)
		kobject_put(&mem->dev.kobj);

	return 0;
}

#ifdef CONFIG_MEMORY_HOTREMOVE
static int check_memblock_offlined_cb(struct memory_block *mem, void *arg)
{
	int ret = !is_memblock_offlined(mem);

	if (unlikely(ret)) {
		phys_addr_t beginpa, endpa;

		beginpa = PFN_PHYS(section_nr_to_pfn(mem->start_section_nr));
		endpa = PFN_PHYS(section_nr_to_pfn(mem->end_section_nr + 1))-1;
		pr_warn("removing memory fails, because memory "
			"[%pa-%pa] is onlined\n",
			&beginpa, &endpa);
	}

	return ret;
}

static int check_cpu_on_node(pg_data_t *pgdat)
{
	int cpu;

	for_each_present_cpu(cpu) {
		if (cpu_to_node(cpu) == pgdat->node_id)
			/*
			 * the cpu on this node isn't removed, and we can't
			 * offline this node.
			 */
			return -EBUSY;
	}

	return 0;
}

static void unmap_cpu_on_node(pg_data_t *pgdat)
{
#ifdef CONFIG_ACPI_NUMA
	int cpu;

	for_each_possible_cpu(cpu)
		if (cpu_to_node(cpu) == pgdat->node_id)
			numa_clear_node(cpu);
#endif
}

static int check_and_unmap_cpu_on_node(pg_data_t *pgdat)
{
	int ret;

	ret = check_cpu_on_node(pgdat);
	if (ret)
		return ret;

	/*
	 * the node will be offlined when we come here, so we can clear
	 * the cpu_to_node() now.
	 */

	unmap_cpu_on_node(pgdat);
	return 0;
}

/**
 * try_offline_node
 *
 * Offline a node if all memory sections and cpus of the node are removed.
 *
 * NOTE: The caller must call lock_device_hotplug() to serialize hotplug
 * and online/offline operations before this call.
 */
void try_offline_node(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	unsigned long start_pfn = pgdat->node_start_pfn;
	unsigned long end_pfn = start_pfn + pgdat->node_spanned_pages;
	unsigned long pfn;
	struct page *pgdat_page = virt_to_page(pgdat);
	int i;

	for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		unsigned long section_nr = pfn_to_section_nr(pfn);

		if (!present_section_nr(section_nr))
			continue;

		if (pfn_to_nid(pfn) != nid)
			continue;

		/*
		 * some memory sections of this node are not removed, and we
		 * can't offline node now.
		 */
		return;
	}

	if (check_and_unmap_cpu_on_node(pgdat))
		return;

	/*
	 * all memory/cpu of this node are removed, we can offline this
	 * node now.
	 */
	node_set_offline(nid);
	unregister_one_node(nid);

	if (!PageSlab(pgdat_page) && !PageCompound(pgdat_page))
		/* node data is allocated from boot memory */
		return;

	/* free waittable in each zone */
	for (i = 0; i < MAX_NR_ZONES; i++) {
		struct zone *zone = pgdat->node_zones + i;

		/*
		 * wait_table may be allocated from boot memory,
		 * here only free if it's allocated by vmalloc.
		 */
		if (is_vmalloc_addr(zone->wait_table))
			vfree(zone->wait_table);
	}

	/*
	 * Since there is no way to guarentee the address of pgdat/zone is not
	 * on stack of any kernel threads or used by other kernel objects
	 * without reference counting or other symchronizing method, do not
	 * reset node_data and free pgdat here. Just reset it to 0 and reuse
	 * the memory when the node is online again.
	 */
	memset(pgdat, 0, sizeof(*pgdat));
}
EXPORT_SYMBOL(try_offline_node);

/**
 * remove_memory
 *
 * NOTE: The caller must call lock_device_hotplug() to serialize hotplug
 * and online/offline operations before this call, as required by
 * try_offline_node().
 */
void __ref remove_memory(int nid, u64 start, u64 size)
{
	int ret;

	BUG_ON(check_hotplug_memory_range(start, size));

	lock_memory_hotplug();

	/*
	 * All memory blocks must be offlined before removing memory.  Check
	 * whether all memory blocks in question are offline and trigger a BUG()
	 * if this is not the case.
	 */
	ret = walk_memory_range(PFN_DOWN(start), PFN_UP(start + size - 1), NULL,
				check_memblock_offlined_cb);
	if (ret) {
		unlock_memory_hotplug();
		BUG();
	}

	/* remove memmap entry */
	firmware_map_remove(start, start + size, "System RAM");

	arch_remove_memory(start, size);

	try_offline_node(nid);

	unlock_memory_hotplug();
}
EXPORT_SYMBOL_GPL(remove_memory);
#endif /* CONFIG_MEMORY_HOTREMOVE */
