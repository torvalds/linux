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
#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/memory.h>
#include <linux/memremap.h>
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
#include <linux/bootmem.h>
#include <linux/compaction.h>

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
static DEFINE_MUTEX(online_page_callback_lock);

/* The same as the cpu_hotplug lock, but for memory hotplug. */
static struct {
	struct task_struct *active_writer;
	struct mutex lock; /* Synchronizes accesses to refcount, */
	/*
	 * Also blocks the new readers during
	 * an ongoing mem hotplug operation.
	 */
	int refcount;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	struct lockdep_map dep_map;
#endif
} mem_hotplug = {
	.active_writer = NULL,
	.lock = __MUTEX_INITIALIZER(mem_hotplug.lock),
	.refcount = 0,
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	.dep_map = {.name = "mem_hotplug.lock" },
#endif
};

/* Lockdep annotations for get/put_online_mems() and mem_hotplug_begin/end() */
#define memhp_lock_acquire_read() lock_map_acquire_read(&mem_hotplug.dep_map)
#define memhp_lock_acquire()      lock_map_acquire(&mem_hotplug.dep_map)
#define memhp_lock_release()      lock_map_release(&mem_hotplug.dep_map)

#ifndef CONFIG_MEMORY_HOTPLUG_DEFAULT_ONLINE
bool memhp_auto_online;
#else
bool memhp_auto_online = true;
#endif
EXPORT_SYMBOL_GPL(memhp_auto_online);

static int __init setup_memhp_default_state(char *str)
{
	if (!strcmp(str, "online"))
		memhp_auto_online = true;
	else if (!strcmp(str, "offline"))
		memhp_auto_online = false;

	return 1;
}
__setup("memhp_default_state=", setup_memhp_default_state);

void get_online_mems(void)
{
	might_sleep();
	if (mem_hotplug.active_writer == current)
		return;
	memhp_lock_acquire_read();
	mutex_lock(&mem_hotplug.lock);
	mem_hotplug.refcount++;
	mutex_unlock(&mem_hotplug.lock);

}

void put_online_mems(void)
{
	if (mem_hotplug.active_writer == current)
		return;
	mutex_lock(&mem_hotplug.lock);

	if (WARN_ON(!mem_hotplug.refcount))
		mem_hotplug.refcount++; /* try to fix things up */

	if (!--mem_hotplug.refcount && unlikely(mem_hotplug.active_writer))
		wake_up_process(mem_hotplug.active_writer);
	mutex_unlock(&mem_hotplug.lock);
	memhp_lock_release();

}

void mem_hotplug_begin(void)
{
	mem_hotplug.active_writer = current;

	memhp_lock_acquire();
	for (;;) {
		mutex_lock(&mem_hotplug.lock);
		if (likely(!mem_hotplug.refcount))
			break;
		__set_current_state(TASK_UNINTERRUPTIBLE);
		mutex_unlock(&mem_hotplug.lock);
		schedule();
	}
}

void mem_hotplug_done(void)
{
	mem_hotplug.active_writer = NULL;
	mutex_unlock(&mem_hotplug.lock);
	memhp_lock_release();
}

/* add this memory to iomem resource */
static struct resource *register_memory_resource(u64 start, u64 size)
{
	struct resource *res;
	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	res->name = "System RAM";
	res->start = start;
	res->end = start + size - 1;
	res->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	if (request_resource(&iomem_resource, res) < 0) {
		pr_debug("System RAM resource %pR cannot be added\n", res);
		kfree(res);
		return ERR_PTR(-EEXIST);
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
	page_ref_inc(page);
}

void put_page_bootmem(struct page *page)
{
	unsigned long type;

	type = (unsigned long) page->lru.next;
	BUG_ON(type < MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE ||
	       type > MEMORY_HOTPLUG_MAX_BOOTMEM_TYPE);

	if (page_ref_dec_return(page) == 1) {
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

void __init register_page_bootmem_info_node(struct pglist_data *pgdat)
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
		if (pfn_valid(pfn) && (early_pfn_to_nid(pfn) == node))
			register_page_bootmem_info_section(pfn);
	}
}
#endif /* CONFIG_HAVE_BOOTMEM_INFO_NODE */

static void __meminit grow_zone_span(struct zone *zone, unsigned long start_pfn,
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
 * alloc_bootmem_node_nopanic()/memblock_virt_alloc_node_nopanic() */
static int __ref ensure_zone_is_initialized(struct zone *zone,
			unsigned long start_pfn, unsigned long num_pages)
{
	if (!zone_is_initialized(zone))
		return init_currently_empty_zone(zone, start_pfn, num_pages);

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

static struct zone * __meminit move_pfn_range(int zone_shift,
		unsigned long start_pfn, unsigned long end_pfn)
{
	struct zone *zone = page_zone(pfn_to_page(start_pfn));
	int ret = 0;

	if (zone_shift < 0)
		ret = move_pfn_range_left(zone + zone_shift, zone,
					  start_pfn, end_pfn);
	else if (zone_shift)
		ret = move_pfn_range_right(zone, zone + zone_shift,
					   start_pfn, end_pfn);

	if (ret)
		return NULL;

	return zone + zone_shift;
}

static void __meminit grow_pgdat_span(struct pglist_data *pgdat, unsigned long start_pfn,
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
	unsigned long flags, pfn;
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

	/* online_page_range is called later and expects pages reserved */
	for (pfn = phys_start_pfn; pfn < phys_start_pfn + nr_pages; pfn++) {
		if (!pfn_valid(pfn))
			continue;

		SetPageReserved(pfn_to_page(pfn));
	}
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
	struct vmem_altmap *altmap;

	clear_zone_contiguous(zone);

	/* during initialize mem_map, align hot-added range to section */
	start_sec = pfn_to_section_nr(phys_start_pfn);
	end_sec = pfn_to_section_nr(phys_start_pfn + nr_pages - 1);

	altmap = to_vmem_altmap((unsigned long) pfn_to_page(phys_start_pfn));
	if (altmap) {
		/*
		 * Validate altmap is within bounds of the total request
		 */
		if (altmap->base_pfn != phys_start_pfn
				|| vmem_altmap_offset(altmap) > nr_pages) {
			pr_warn_once("memory add fail, invalid altmap\n");
			err = -EINVAL;
			goto out;
		}
		altmap->alloc = 0;
	}

	for (i = start_sec; i <= end_sec; i++) {
		err = __add_section(nid, zone, section_nr_to_pfn(i));

		/*
		 * EEXIST is finally dealt with by ioresource collision
		 * check. see add_memory() => register_memory_resource()
		 * Warning will be printed if there is collision.
		 */
		if (err && (err != -EEXIST))
			break;
		err = 0;
	}
	vmemmap_populate_print_last();
out:
	set_zone_contiguous(zone);
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

static int __remove_section(struct zone *zone, struct mem_section *ms,
		unsigned long map_offset)
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

	sparse_remove_one_section(zone, ms, map_offset);
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
	unsigned long map_offset = 0;
	int sections_to_remove, ret = 0;

	/* In the ZONE_DEVICE case device driver owns the memory region */
	if (is_dev_zone(zone)) {
		struct page *page = pfn_to_page(phys_start_pfn);
		struct vmem_altmap *altmap;

		altmap = to_vmem_altmap((unsigned long) page);
		if (altmap)
			map_offset = vmem_altmap_offset(altmap);
	} else {
		resource_size_t start, size;

		start = phys_start_pfn << PAGE_SHIFT;
		size = nr_pages * PAGE_SIZE;

		ret = release_mem_region_adjustable(&iomem_resource, start,
					size);
		if (ret) {
			resource_size_t endres = start + size - 1;

			pr_warn("Unable to release resource <%pa-%pa> (%d)\n",
					&start, &endres, ret);
		}
	}

	clear_zone_contiguous(zone);

	/*
	 * We can only remove entire sections
	 */
	BUG_ON(phys_start_pfn & ~PAGE_SECTION_MASK);
	BUG_ON(nr_pages % PAGES_PER_SECTION);

	sections_to_remove = nr_pages / PAGES_PER_SECTION;
	for (i = 0; i < sections_to_remove; i++) {
		unsigned long pfn = phys_start_pfn + i*PAGES_PER_SECTION;

		ret = __remove_section(zone, __pfn_to_section(pfn), map_offset);
		map_offset = 0;
		if (ret)
			break;
	}

	set_zone_contiguous(zone);

	return ret;
}
EXPORT_SYMBOL_GPL(__remove_pages);
#endif /* CONFIG_MEMORY_HOTREMOVE */

int set_online_page_callback(online_page_callback_t callback)
{
	int rc = -EINVAL;

	get_online_mems();
	mutex_lock(&online_page_callback_lock);

	if (online_page_callback == generic_online_page) {
		online_page_callback = callback;
		rc = 0;
	}

	mutex_unlock(&online_page_callback_lock);
	put_online_mems();

	return rc;
}
EXPORT_SYMBOL_GPL(set_online_page_callback);

int restore_online_page_callback(online_page_callback_t callback)
{
	int rc = -EINVAL;

	get_online_mems();
	mutex_lock(&online_page_callback_lock);

	if (online_page_callback == callback) {
		online_page_callback = generic_online_page;
		rc = 0;
	}

	mutex_unlock(&online_page_callback_lock);
	put_online_mems();

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

int zone_can_shift(unsigned long pfn, unsigned long nr_pages,
		   enum zone_type target)
{
	struct zone *zone = page_zone(pfn_to_page(pfn));
	enum zone_type idx = zone_idx(zone);
	int i;

	if (idx < target) {
		/* pages must be at end of current zone */
		if (pfn + nr_pages != zone_end_pfn(zone))
			return 0;

		/* no zones in use between current zone and target */
		for (i = idx + 1; i < target; i++)
			if (zone_is_initialized(zone - idx + i))
				return 0;
	}

	if (target < idx) {
		/* pages must be at beginning of current zone */
		if (pfn != zone->zone_start_pfn)
			return 0;

		/* no zones in use between current zone and target */
		for (i = target + 1; i < idx; i++)
			if (zone_is_initialized(zone - idx + i))
				return 0;
	}

	return target - idx;
}

/* Must be protected by mem_hotplug_begin() */
int __ref online_pages(unsigned long pfn, unsigned long nr_pages, int online_type)
{
	unsigned long flags;
	unsigned long onlined_pages = 0;
	struct zone *zone;
	int need_zonelists_rebuild = 0;
	int nid;
	int ret;
	struct memory_notify arg;
	int zone_shift = 0;

	/*
	 * This doesn't need a lock to do pfn_to_page().
	 * The section can't be removed here because of the
	 * memory_block->state_mutex.
	 */
	zone = page_zone(pfn_to_page(pfn));

	if ((zone_idx(zone) > ZONE_NORMAL ||
	    online_type == MMOP_ONLINE_MOVABLE) &&
	    !can_online_high_movable(zone))
		return -EINVAL;

	if (online_type == MMOP_ONLINE_KERNEL)
		zone_shift = zone_can_shift(pfn, nr_pages, ZONE_NORMAL);
	else if (online_type == MMOP_ONLINE_MOVABLE)
		zone_shift = zone_can_shift(pfn, nr_pages, ZONE_MOVABLE);

	zone = move_pfn_range(zone_shift, pfn, pfn + nr_pages);
	if (!zone)
		return -EINVAL;

	arg.start_pfn = pfn;
	arg.nr_pages = nr_pages;
	node_states_check_changes_online(nr_pages, zone, &arg);

	nid = zone_to_nid(zone);

	ret = memory_notify(MEM_GOING_ONLINE, &arg);
	ret = notifier_to_errno(ret);
	if (ret)
		goto failed_addition;

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
		goto failed_addition;
	}

	zone->present_pages += onlined_pages;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	zone->zone_pgdat->node_present_pages += onlined_pages;
	pgdat_resize_unlock(zone->zone_pgdat, &flags);

	if (onlined_pages) {
		node_states_set_node(nid, &arg);
		if (need_zonelists_rebuild)
			build_all_zonelists(NULL, NULL);
		else
			zone_pcp_update(zone);
	}

	mutex_unlock(&zonelists_mutex);

	init_per_zone_wmark_min();

	if (onlined_pages) {
		kswapd_run(nid);
		kcompactd_run(nid);
	}

	vm_total_pages = nr_free_pagecache_pages();

	writeback_set_ratelimit();

	if (onlined_pages)
		memory_notify(MEM_ONLINE, &arg);
	return 0;

failed_addition:
	pr_debug("online_pages [mem %#010llx-%#010llx] failed\n",
		 (unsigned long long) pfn << PAGE_SHIFT,
		 (((unsigned long long) pfn + nr_pages) << PAGE_SHIFT) - 1);
	memory_notify(MEM_CANCEL_ONLINE, &arg);
	return ret;
}
#endif /* CONFIG_MEMORY_HOTPLUG_SPARSE */

static void reset_node_present_pages(pg_data_t *pgdat)
{
	struct zone *z;

	for (z = pgdat->node_zones; z < pgdat->node_zones + MAX_NR_ZONES; z++)
		z->present_pages = 0;

	pgdat->node_present_pages = 0;
}

/* we are OK calling __meminit stuff here - we have CONFIG_MEMORY_HOTPLUG */
static pg_data_t __ref *hotadd_new_pgdat(int nid, u64 start)
{
	struct pglist_data *pgdat;
	unsigned long zones_size[MAX_NR_ZONES] = {0};
	unsigned long zholes_size[MAX_NR_ZONES] = {0};
	unsigned long start_pfn = PFN_DOWN(start);

	pgdat = NODE_DATA(nid);
	if (!pgdat) {
		pgdat = arch_alloc_nodedata(nid);
		if (!pgdat)
			return NULL;

		arch_refresh_nodedata(nid, pgdat);
	} else {
		/* Reset the nr_zones, order and classzone_idx before reuse */
		pgdat->nr_zones = 0;
		pgdat->kswapd_order = 0;
		pgdat->kswapd_classzone_idx = 0;
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

	/*
	 * zone->managed_pages is set to an approximate value in
	 * free_area_init_core(), which will cause
	 * /sys/device/system/node/nodeX/meminfo has wrong data.
	 * So reset it to 0 before any memory is onlined.
	 */
	reset_node_managed_pages(pgdat);

	/*
	 * When memory is hot-added, all the memory is in offline state. So
	 * clear all zones' present_pages because they will be updated in
	 * online_pages() and offline_pages().
	 */
	reset_node_present_pages(pgdat);

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

	mem_hotplug_begin();
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
	mem_hotplug_done();
	return ret;
}

static int check_hotplug_memory_range(u64 start, u64 size)
{
	u64 start_pfn = PFN_DOWN(start);
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

/*
 * If movable zone has already been setup, newly added memory should be check.
 * If its address is higher than movable zone, it should be added as movable.
 * Without this check, movable zone may overlap with other zone.
 */
static int should_add_memory_movable(int nid, u64 start, u64 size)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	pg_data_t *pgdat = NODE_DATA(nid);
	struct zone *movable_zone = pgdat->node_zones + ZONE_MOVABLE;

	if (zone_is_empty(movable_zone))
		return 0;

	if (movable_zone->zone_start_pfn <= start_pfn)
		return 1;

	return 0;
}

int zone_for_memory(int nid, u64 start, u64 size, int zone_default,
		bool for_device)
{
#ifdef CONFIG_ZONE_DEVICE
	if (for_device)
		return ZONE_DEVICE;
#endif
	if (should_add_memory_movable(nid, start, size))
		return ZONE_MOVABLE;

	return zone_default;
}

static int online_memory_block(struct memory_block *mem, void *arg)
{
	return memory_block_change_state(mem, MEM_ONLINE, MEM_OFFLINE);
}

/* we are OK calling __meminit stuff here - we have CONFIG_MEMORY_HOTPLUG */
int __ref add_memory_resource(int nid, struct resource *res, bool online)
{
	u64 start, size;
	pg_data_t *pgdat = NULL;
	bool new_pgdat;
	bool new_node;
	int ret;

	start = res->start;
	size = resource_size(res);

	ret = check_hotplug_memory_range(start, size);
	if (ret)
		return ret;

	{	/* Stupid hack to suppress address-never-null warning */
		void *p = NODE_DATA(nid);
		new_pgdat = !p;
	}

	mem_hotplug_begin();

	/*
	 * Add new range to memblock so that when hotadd_new_pgdat() is called
	 * to allocate new pgdat, get_pfn_range_for_nid() will be able to find
	 * this new range and calculate total pages correctly.  The range will
	 * be removed at hot-remove time.
	 */
	memblock_add_node(start, size, nid);

	new_node = !node_online(nid);
	if (new_node) {
		pgdat = hotadd_new_pgdat(nid, start);
		ret = -ENOMEM;
		if (!pgdat)
			goto error;
	}

	/* call arch's memory hotadd */
	ret = arch_add_memory(nid, start, size, false);

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

	/* online pages if requested */
	if (online)
		walk_memory_range(PFN_DOWN(start), PFN_UP(start + size - 1),
				  NULL, online_memory_block);

	goto out;

error:
	/* rollback pgdat allocation and others */
	if (new_pgdat)
		rollback_node_hotadd(nid, pgdat);
	memblock_remove(start, size);

out:
	mem_hotplug_done();
	return ret;
}
EXPORT_SYMBOL_GPL(add_memory_resource);

int __ref add_memory(int nid, u64 start, u64 size)
{
	struct resource *res;
	int ret;

	res = register_memory_resource(start, size);
	if (IS_ERR(res))
		return PTR_ERR(res);

	ret = add_memory_resource(nid, res, memhp_auto_online);
	if (ret < 0)
		release_memory_resource(res);
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
bool is_mem_section_removable(unsigned long start_pfn, unsigned long nr_pages)
{
	struct page *page = pfn_to_page(start_pfn);
	struct page *end_page = page + nr_pages;

	/* Check the starting page of each pageblock within the range */
	for (; page < end_page; page = next_active_pageblock(page)) {
		if (!is_pageblock_removable_nolock(page))
			return false;
		cond_resched();
	}

	/* All pageblocks in the memory block are likely to be hot-removable */
	return true;
}

/*
 * Confirm all pages in a range [start, end) is belongs to the same zone.
 */
int test_pages_in_a_zone(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn, sec_end_pfn;
	struct zone *zone = NULL;
	struct page *page;
	int i;
	for (pfn = start_pfn, sec_end_pfn = SECTION_ALIGN_UP(start_pfn);
	     pfn < end_pfn;
	     pfn = sec_end_pfn + 1, sec_end_pfn += PAGES_PER_SECTION) {
		/* Make sure the memory section is present first */
		if (!present_section_nr(pfn_to_section_nr(pfn)))
			continue;
		for (; pfn < sec_end_pfn && pfn < end_pfn;
		     pfn += MAX_ORDER_NR_PAGES) {
			i = 0;
			/* This is just a CONFIG_HOLES_IN_ZONE check.*/
			while ((i < MAX_ORDER_NR_PAGES) &&
				!pfn_valid_within(pfn + i))
				i++;
			if (i == MAX_ORDER_NR_PAGES)
				continue;
			page = pfn_to_page(pfn + i);
			if (zone && page_zone(page) != zone)
				return 0;
			zone = page_zone(page);
		}
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
				if (page_huge_active(page))
					return pfn;
				else
					pfn = round_up(pfn + 1,
						1 << compound_order(page)) - 1;
			}
		}
	}
	return 0;
}

static struct page *new_node_page(struct page *page, unsigned long private,
		int **result)
{
	gfp_t gfp_mask = GFP_USER | __GFP_MOVABLE;
	int nid = page_to_nid(page);
	nodemask_t nmask = node_online_map;
	struct page *new_page;

	/*
	 * TODO: allocate a destination hugepage from a nearest neighbor node,
	 * accordance with memory policy of the user process if possible. For
	 * now as a simple work-around, we use the next node for destination.
	 */
	if (PageHuge(page))
		return alloc_huge_page_node(page_hstate(compound_head(page)),
					next_node_in(nid, nmask));

	node_clear(nid, nmask);
	if (PageHighMem(page)
	    || (zone_idx(page_zone(page)) == ZONE_MOVABLE))
		gfp_mask |= __GFP_HIGHMEM;

	new_page = __alloc_pages_nodemask(gfp_mask, 0,
					node_zonelist(nid, gfp_mask), &nmask);
	if (!new_page)
		new_page = __alloc_pages(gfp_mask, 0,
					node_zonelist(nid, gfp_mask));

	return new_page;
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
			inc_node_page_state(page, NR_ISOLATED_ANON +
					    page_is_file_cache(page));

		} else {
#ifdef CONFIG_DEBUG_VM
			pr_alert("removing pfn %lx from LRU failed\n", pfn);
			dump_page(page, "failed to remove from LRU");
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

		/* Allocate a new page from the nearest neighbor node */
		ret = migrate_pages(&source, new_node_page, NULL, 0,
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
	movable_node_enabled = true;
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

	zone = page_zone(pfn_to_page(start_pfn));
	node = zone_to_nid(zone);
	nr_pages = end_pfn - start_pfn;

	if (zone_idx(zone) <= ZONE_NORMAL && !can_offline_normal(zone, nr_pages))
		return -EINVAL;

	/* set above range as isolated */
	ret = start_isolate_page_range(start_pfn, end_pfn,
				       MIGRATE_MOVABLE, true);
	if (ret)
		return ret;

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
		drain_all_pages(zone);
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
	drain_all_pages(zone);
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
	pr_info("Offlined Pages %ld\n", offlined_pages);
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
	if (arg.status_change_nid >= 0) {
		kswapd_stop(node);
		kcompactd_stop(node);
	}

	vm_total_pages = nr_free_pagecache_pages();
	writeback_set_ratelimit();

	memory_notify(MEM_OFFLINE, &arg);
	return 0;

failed_removal:
	pr_debug("memory offlining [mem %#010llx-%#010llx] failed\n",
		 (unsigned long long) start_pfn << PAGE_SHIFT,
		 ((unsigned long long) end_pfn << PAGE_SHIFT) - 1);
	memory_notify(MEM_CANCEL_OFFLINE, &arg);
	/* pushback to free area */
	undo_isolate_page_range(start_pfn, end_pfn, MIGRATE_MOVABLE);
	return ret;
}

/* Must be protected by mem_hotplug_begin() */
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
		pr_warn("removing memory fails, because memory [%pa-%pa] is onlined\n",
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

	/* free waittable in each zone */
	for (i = 0; i < MAX_NR_ZONES; i++) {
		struct zone *zone = pgdat->node_zones + i;

		/*
		 * wait_table may be allocated from boot memory,
		 * here only free if it's allocated by vmalloc.
		 */
		if (is_vmalloc_addr(zone->wait_table)) {
			vfree(zone->wait_table);
			zone->wait_table = NULL;
		}
	}
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

	mem_hotplug_begin();

	/*
	 * All memory blocks must be offlined before removing memory.  Check
	 * whether all memory blocks in question are offline and trigger a BUG()
	 * if this is not the case.
	 */
	ret = walk_memory_range(PFN_DOWN(start), PFN_UP(start + size - 1), NULL,
				check_memblock_offlined_cb);
	if (ret)
		BUG();

	/* remove memmap entry */
	firmware_map_remove(start, start + size, "System RAM");
	memblock_free(start, size);
	memblock_remove(start, size);

	arch_remove_memory(start, size);

	try_offline_node(nid);

	mem_hotplug_done();
}
EXPORT_SYMBOL_GPL(remove_memory);
#endif /* CONFIG_MEMORY_HOTREMOVE */
