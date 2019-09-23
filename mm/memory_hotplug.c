// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/memory_hotplug.c
 *
 *  Copyright (C)
 */

#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
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
#include <linux/compaction.h>
#include <linux/rmap.h>

#include <asm/tlbflush.h>

#include "internal.h"
#include "shuffle.h"

/*
 * online_page_callback contains pointer to current page onlining function.
 * Initially it is generic_online_page(). If it is required it could be
 * changed by calling set_online_page_callback() for callback registration
 * and restore_online_page_callback() for generic callback restore.
 */

static void generic_online_page(struct page *page, unsigned int order);

static online_page_callback_t online_page_callback = generic_online_page;
static DEFINE_MUTEX(online_page_callback_lock);

DEFINE_STATIC_PERCPU_RWSEM(mem_hotplug_lock);

void get_online_mems(void)
{
	percpu_down_read(&mem_hotplug_lock);
}

void put_online_mems(void)
{
	percpu_up_read(&mem_hotplug_lock);
}

bool movable_node_enabled = false;

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

void mem_hotplug_begin(void)
{
	cpus_read_lock();
	percpu_down_write(&mem_hotplug_lock);
}

void mem_hotplug_done(void)
{
	percpu_up_write(&mem_hotplug_lock);
	cpus_read_unlock();
}

u64 max_mem_size = U64_MAX;

/* add this memory to iomem resource */
static struct resource *register_memory_resource(u64 start, u64 size)
{
	struct resource *res;
	unsigned long flags =  IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
	char *resource_name = "System RAM";

	if (start + size > max_mem_size)
		return ERR_PTR(-E2BIG);

	/*
	 * Request ownership of the new memory range.  This might be
	 * a child of an existing resource that was present but
	 * not marked as busy.
	 */
	res = __request_region(&iomem_resource, start, size,
			       resource_name, flags);

	if (!res) {
		pr_debug("Unable to reserve System RAM region: %016llx->%016llx\n",
				start, start + size);
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
}

#ifdef CONFIG_MEMORY_HOTPLUG_SPARSE
void get_page_bootmem(unsigned long info,  struct page *page,
		      unsigned long type)
{
	page->freelist = (void *)type;
	SetPagePrivate(page);
	set_page_private(page, info);
	page_ref_inc(page);
}

void put_page_bootmem(struct page *page)
{
	unsigned long type;

	type = (unsigned long) page->freelist;
	BUG_ON(type < MEMORY_HOTPLUG_MIN_BOOTMEM_TYPE ||
	       type > MEMORY_HOTPLUG_MAX_BOOTMEM_TYPE);

	if (page_ref_dec_return(page) == 1) {
		page->freelist = NULL;
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
static void register_page_bootmem_info_section(unsigned long start_pfn)
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
#endif /* CONFIG_HAVE_BOOTMEM_INFO_NODE */

static int check_pfn_span(unsigned long pfn, unsigned long nr_pages,
		const char *reason)
{
	/*
	 * Disallow all operations smaller than a sub-section and only
	 * allow operations smaller than a section for
	 * SPARSEMEM_VMEMMAP. Note that check_hotplug_memory_range()
	 * enforces a larger memory_block_size_bytes() granularity for
	 * memory that will be marked online, so this check should only
	 * fire for direct arch_{add,remove}_memory() users outside of
	 * add_memory_resource().
	 */
	unsigned long min_align;

	if (IS_ENABLED(CONFIG_SPARSEMEM_VMEMMAP))
		min_align = PAGES_PER_SUBSECTION;
	else
		min_align = PAGES_PER_SECTION;
	if (!IS_ALIGNED(pfn, min_align)
			|| !IS_ALIGNED(nr_pages, min_align)) {
		WARN(1, "Misaligned __%s_pages start: %#lx end: #%lx\n",
				reason, pfn, pfn + nr_pages - 1);
		return -EINVAL;
	}
	return 0;
}

/*
 * Reasonably generic function for adding memory.  It is
 * expected that archs that support memory hotplug will
 * call this function after deciding the zone to which to
 * add the new pages.
 */
int __ref __add_pages(int nid, unsigned long pfn, unsigned long nr_pages,
		struct mhp_restrictions *restrictions)
{
	int err;
	unsigned long nr, start_sec, end_sec;
	struct vmem_altmap *altmap = restrictions->altmap;

	if (altmap) {
		/*
		 * Validate altmap is within bounds of the total request
		 */
		if (altmap->base_pfn != pfn
				|| vmem_altmap_offset(altmap) > nr_pages) {
			pr_warn_once("memory add fail, invalid altmap\n");
			return -EINVAL;
		}
		altmap->alloc = 0;
	}

	err = check_pfn_span(pfn, nr_pages, "add");
	if (err)
		return err;

	start_sec = pfn_to_section_nr(pfn);
	end_sec = pfn_to_section_nr(pfn + nr_pages - 1);
	for (nr = start_sec; nr <= end_sec; nr++) {
		unsigned long pfns;

		pfns = min(nr_pages, PAGES_PER_SECTION
				- (pfn & ~PAGE_SECTION_MASK));
		err = sparse_add_section(nid, pfn, pfns, altmap);
		if (err)
			break;
		pfn += pfns;
		nr_pages -= pfns;
		cond_resched();
	}
	vmemmap_populate_print_last();
	return err;
}

/* find the smallest valid pfn in the range [start_pfn, end_pfn) */
static unsigned long find_smallest_section_pfn(int nid, struct zone *zone,
				     unsigned long start_pfn,
				     unsigned long end_pfn)
{
	for (; start_pfn < end_pfn; start_pfn += PAGES_PER_SUBSECTION) {
		if (unlikely(!pfn_valid(start_pfn)))
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
static unsigned long find_biggest_section_pfn(int nid, struct zone *zone,
				    unsigned long start_pfn,
				    unsigned long end_pfn)
{
	unsigned long pfn;

	/* pfn is the end pfn of a memory section. */
	pfn = end_pfn - 1;
	for (; pfn >= start_pfn; pfn -= PAGES_PER_SUBSECTION) {
		if (unlikely(!pfn_valid(pfn)))
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
	for (; pfn < zone_end_pfn; pfn += PAGES_PER_SUBSECTION) {
		if (unlikely(!pfn_valid(pfn)))
			continue;

		if (page_zone(pfn_to_page(pfn)) != zone)
			continue;

		/* Skip range to be removed */
		if (pfn >= start_pfn && pfn < end_pfn)
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
	for (; pfn < pgdat_end_pfn; pfn += PAGES_PER_SUBSECTION) {
		if (unlikely(!pfn_valid(pfn)))
			continue;

		if (pfn_to_nid(pfn) != nid)
			continue;

		/* Skip range to be removed */
		if (pfn >= start_pfn && pfn < end_pfn)
			continue;

		/* If we find valid section, we have nothing to do */
		return;
	}

	/* The pgdat has no valid section */
	pgdat->node_start_pfn = 0;
	pgdat->node_spanned_pages = 0;
}

static void __remove_zone(struct zone *zone, unsigned long start_pfn,
		unsigned long nr_pages)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	unsigned long flags;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	shrink_zone_span(zone, start_pfn, start_pfn + nr_pages);
	shrink_pgdat_span(pgdat, start_pfn, start_pfn + nr_pages);
	pgdat_resize_unlock(zone->zone_pgdat, &flags);
}

static void __remove_section(struct zone *zone, unsigned long pfn,
		unsigned long nr_pages, unsigned long map_offset,
		struct vmem_altmap *altmap)
{
	struct mem_section *ms = __nr_to_section(pfn_to_section_nr(pfn));

	if (WARN_ON_ONCE(!valid_section(ms)))
		return;

	__remove_zone(zone, pfn, nr_pages);
	sparse_remove_section(ms, pfn, nr_pages, map_offset, altmap);
}

/**
 * __remove_pages() - remove sections of pages from a zone
 * @zone: zone from which pages need to be removed
 * @pfn: starting pageframe (must be aligned to start of a section)
 * @nr_pages: number of pages to remove (must be multiple of section size)
 * @altmap: alternative device page map or %NULL if default memmap is used
 *
 * Generic helper function to remove section mappings and sysfs entries
 * for the section of the memory we are removing. Caller needs to make
 * sure that pages are marked reserved and zones are adjust properly by
 * calling offline_pages().
 */
void __remove_pages(struct zone *zone, unsigned long pfn,
		    unsigned long nr_pages, struct vmem_altmap *altmap)
{
	unsigned long map_offset = 0;
	unsigned long nr, start_sec, end_sec;

	map_offset = vmem_altmap_offset(altmap);

	clear_zone_contiguous(zone);

	if (check_pfn_span(pfn, nr_pages, "remove"))
		return;

	start_sec = pfn_to_section_nr(pfn);
	end_sec = pfn_to_section_nr(pfn + nr_pages - 1);
	for (nr = start_sec; nr <= end_sec; nr++) {
		unsigned long pfns;

		cond_resched();
		pfns = min(nr_pages, PAGES_PER_SECTION
				- (pfn & ~PAGE_SECTION_MASK));
		__remove_section(zone, pfn, pfns, map_offset, altmap);
		pfn += pfns;
		nr_pages -= pfns;
		map_offset = 0;
	}

	set_zone_contiguous(zone);
}

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

static void generic_online_page(struct page *page, unsigned int order)
{
	kernel_map_pages(page, 1 << order, 1);
	__free_pages_core(page, order);
	totalram_pages_add(1UL << order);
#ifdef CONFIG_HIGHMEM
	if (PageHighMem(page))
		totalhigh_pages_add(1UL << order);
#endif
}

static int online_pages_blocks(unsigned long start, unsigned long nr_pages)
{
	unsigned long end = start + nr_pages;
	int order, onlined_pages = 0;

	while (start < end) {
		order = min(MAX_ORDER - 1,
			get_order(PFN_PHYS(end) - PFN_PHYS(start)));
		(*online_page_callback)(pfn_to_page(start), order);

		onlined_pages += (1UL << order);
		start += (1UL << order);
	}
	return onlined_pages;
}

static int online_pages_range(unsigned long start_pfn, unsigned long nr_pages,
			void *arg)
{
	unsigned long onlined_pages = *(unsigned long *)arg;

	if (PageReserved(pfn_to_page(start_pfn)))
		onlined_pages += online_pages_blocks(start_pfn, nr_pages);

	online_mem_sections(start_pfn, start_pfn + nr_pages);

	*(unsigned long *)arg = onlined_pages;
	return 0;
}

/* check which state of node_states will be changed when online memory */
static void node_states_check_changes_online(unsigned long nr_pages,
	struct zone *zone, struct memory_notify *arg)
{
	int nid = zone_to_nid(zone);

	arg->status_change_nid = NUMA_NO_NODE;
	arg->status_change_nid_normal = NUMA_NO_NODE;
	arg->status_change_nid_high = NUMA_NO_NODE;

	if (!node_state(nid, N_MEMORY))
		arg->status_change_nid = nid;
	if (zone_idx(zone) <= ZONE_NORMAL && !node_state(nid, N_NORMAL_MEMORY))
		arg->status_change_nid_normal = nid;
#ifdef CONFIG_HIGHMEM
	if (zone_idx(zone) <= ZONE_HIGHMEM && !node_state(nid, N_HIGH_MEMORY))
		arg->status_change_nid_high = nid;
#endif
}

static void node_states_set_node(int node, struct memory_notify *arg)
{
	if (arg->status_change_nid_normal >= 0)
		node_set_state(node, N_NORMAL_MEMORY);

	if (arg->status_change_nid_high >= 0)
		node_set_state(node, N_HIGH_MEMORY);

	if (arg->status_change_nid >= 0)
		node_set_state(node, N_MEMORY);
}

static void __meminit resize_zone_range(struct zone *zone, unsigned long start_pfn,
		unsigned long nr_pages)
{
	unsigned long old_end_pfn = zone_end_pfn(zone);

	if (zone_is_empty(zone) || start_pfn < zone->zone_start_pfn)
		zone->zone_start_pfn = start_pfn;

	zone->spanned_pages = max(start_pfn + nr_pages, old_end_pfn) - zone->zone_start_pfn;
}

static void __meminit resize_pgdat_range(struct pglist_data *pgdat, unsigned long start_pfn,
                                     unsigned long nr_pages)
{
	unsigned long old_end_pfn = pgdat_end_pfn(pgdat);

	if (!pgdat->node_spanned_pages || start_pfn < pgdat->node_start_pfn)
		pgdat->node_start_pfn = start_pfn;

	pgdat->node_spanned_pages = max(start_pfn + nr_pages, old_end_pfn) - pgdat->node_start_pfn;

}
/*
 * Associate the pfn range with the given zone, initializing the memmaps
 * and resizing the pgdat/zone data to span the added pages. After this
 * call, all affected pages are PG_reserved.
 */
void __ref move_pfn_range_to_zone(struct zone *zone, unsigned long start_pfn,
		unsigned long nr_pages, struct vmem_altmap *altmap)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int nid = pgdat->node_id;
	unsigned long flags;

	clear_zone_contiguous(zone);

	/* TODO Huh pgdat is irqsave while zone is not. It used to be like that before */
	pgdat_resize_lock(pgdat, &flags);
	zone_span_writelock(zone);
	if (zone_is_empty(zone))
		init_currently_empty_zone(zone, start_pfn, nr_pages);
	resize_zone_range(zone, start_pfn, nr_pages);
	zone_span_writeunlock(zone);
	resize_pgdat_range(pgdat, start_pfn, nr_pages);
	pgdat_resize_unlock(pgdat, &flags);

	/*
	 * TODO now we have a visible range of pages which are not associated
	 * with their zone properly. Not nice but set_pfnblock_flags_mask
	 * expects the zone spans the pfn range. All the pages in the range
	 * are reserved so nobody should be touching them so we should be safe
	 */
	memmap_init_zone(nr_pages, nid, zone_idx(zone), start_pfn,
			MEMMAP_HOTPLUG, altmap);

	set_zone_contiguous(zone);
}

/*
 * Returns a default kernel memory zone for the given pfn range.
 * If no kernel zone covers this pfn range it will automatically go
 * to the ZONE_NORMAL.
 */
static struct zone *default_kernel_zone_for_pfn(int nid, unsigned long start_pfn,
		unsigned long nr_pages)
{
	struct pglist_data *pgdat = NODE_DATA(nid);
	int zid;

	for (zid = 0; zid <= ZONE_NORMAL; zid++) {
		struct zone *zone = &pgdat->node_zones[zid];

		if (zone_intersects(zone, start_pfn, nr_pages))
			return zone;
	}

	return &pgdat->node_zones[ZONE_NORMAL];
}

static inline struct zone *default_zone_for_pfn(int nid, unsigned long start_pfn,
		unsigned long nr_pages)
{
	struct zone *kernel_zone = default_kernel_zone_for_pfn(nid, start_pfn,
			nr_pages);
	struct zone *movable_zone = &NODE_DATA(nid)->node_zones[ZONE_MOVABLE];
	bool in_kernel = zone_intersects(kernel_zone, start_pfn, nr_pages);
	bool in_movable = zone_intersects(movable_zone, start_pfn, nr_pages);

	/*
	 * We inherit the existing zone in a simple case where zones do not
	 * overlap in the given range
	 */
	if (in_kernel ^ in_movable)
		return (in_kernel) ? kernel_zone : movable_zone;

	/*
	 * If the range doesn't belong to any zone or two zones overlap in the
	 * given range then we use movable zone only if movable_node is
	 * enabled because we always online to a kernel zone by default.
	 */
	return movable_node_enabled ? movable_zone : kernel_zone;
}

struct zone * zone_for_pfn_range(int online_type, int nid, unsigned start_pfn,
		unsigned long nr_pages)
{
	if (online_type == MMOP_ONLINE_KERNEL)
		return default_kernel_zone_for_pfn(nid, start_pfn, nr_pages);

	if (online_type == MMOP_ONLINE_MOVABLE)
		return &NODE_DATA(nid)->node_zones[ZONE_MOVABLE];

	return default_zone_for_pfn(nid, start_pfn, nr_pages);
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
	struct memory_block *mem;

	mem_hotplug_begin();

	/*
	 * We can't use pfn_to_nid() because nid might be stored in struct page
	 * which is not yet initialized. Instead, we find nid from memory block.
	 */
	mem = find_memory_block(__pfn_to_section(pfn));
	nid = mem->nid;
	put_device(&mem->dev);

	/* associate pfn range with the zone */
	zone = zone_for_pfn_range(online_type, nid, pfn, nr_pages);
	move_pfn_range_to_zone(zone, pfn, nr_pages, NULL);

	arg.start_pfn = pfn;
	arg.nr_pages = nr_pages;
	node_states_check_changes_online(nr_pages, zone, &arg);

	ret = memory_notify(MEM_GOING_ONLINE, &arg);
	ret = notifier_to_errno(ret);
	if (ret)
		goto failed_addition;

	/*
	 * If this zone is not populated, then it is not in zonelist.
	 * This means the page allocator ignores this zone.
	 * So, zonelist must be updated after online.
	 */
	if (!populated_zone(zone)) {
		need_zonelists_rebuild = 1;
		setup_zone_pageset(zone);
	}

	ret = walk_system_ram_range(pfn, nr_pages, &onlined_pages,
		online_pages_range);
	if (ret) {
		if (need_zonelists_rebuild)
			zone_pcp_reset(zone);
		goto failed_addition;
	}

	zone->present_pages += onlined_pages;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	zone->zone_pgdat->node_present_pages += onlined_pages;
	pgdat_resize_unlock(zone->zone_pgdat, &flags);

	shuffle_zone(zone);

	if (onlined_pages) {
		node_states_set_node(nid, &arg);
		if (need_zonelists_rebuild)
			build_all_zonelists(NULL);
		else
			zone_pcp_update(zone);
	}

	init_per_zone_wmark_min();

	if (onlined_pages) {
		kswapd_run(nid);
		kcompactd_run(nid);
	}

	vm_total_pages = nr_free_pagecache_pages();

	writeback_set_ratelimit();

	if (onlined_pages)
		memory_notify(MEM_ONLINE, &arg);
	mem_hotplug_done();
	return 0;

failed_addition:
	pr_debug("online_pages [mem %#010llx-%#010llx] failed\n",
		 (unsigned long long) pfn << PAGE_SHIFT,
		 (((unsigned long long) pfn + nr_pages) << PAGE_SHIFT) - 1);
	memory_notify(MEM_CANCEL_ONLINE, &arg);
	mem_hotplug_done();
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
	unsigned long start_pfn = PFN_DOWN(start);

	pgdat = NODE_DATA(nid);
	if (!pgdat) {
		pgdat = arch_alloc_nodedata(nid);
		if (!pgdat)
			return NULL;

		arch_refresh_nodedata(nid, pgdat);
	} else {
		/*
		 * Reset the nr_zones, order and classzone_idx before reuse.
		 * Note that kswapd will init kswapd_classzone_idx properly
		 * when it starts in the near future.
		 */
		pgdat->nr_zones = 0;
		pgdat->kswapd_order = 0;
		pgdat->kswapd_classzone_idx = 0;
	}

	/* we can use NODE_DATA(nid) from here */

	pgdat->node_id = nid;
	pgdat->node_start_pfn = start_pfn;

	/* init node's zones as empty zones, we don't have any present pages.*/
	free_area_init_core_hotplug(nid);
	pgdat->per_cpu_nodestats = alloc_percpu(struct per_cpu_nodestat);

	/*
	 * The node we allocated has no zone fallback lists. For avoiding
	 * to access not-initialized zonelist, build here.
	 */
	build_all_zonelists(pgdat);

	/*
	 * When memory is hot-added, all the memory is in offline state. So
	 * clear all zones' present_pages because they will be updated in
	 * online_pages() and offline_pages().
	 */
	reset_node_managed_pages(pgdat);
	reset_node_present_pages(pgdat);

	return pgdat;
}

static void rollback_node_hotadd(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);

	arch_refresh_nodedata(nid, NULL);
	free_percpu(pgdat->per_cpu_nodestats);
	arch_free_nodedata(pgdat);
}


/**
 * try_online_node - online a node if offlined
 * @nid: the node ID
 * @start: start addr of the node
 * @set_node_online: Whether we want to online the node
 * called by cpu_up() to online a node without onlined memory.
 *
 * Returns:
 * 1 -> a new node has been allocated
 * 0 -> the node is already online
 * -ENOMEM -> the node could not be allocated
 */
static int __try_online_node(int nid, u64 start, bool set_node_online)
{
	pg_data_t *pgdat;
	int ret = 1;

	if (node_online(nid))
		return 0;

	pgdat = hotadd_new_pgdat(nid, start);
	if (!pgdat) {
		pr_err("Cannot online node %d due to NULL pgdat\n", nid);
		ret = -ENOMEM;
		goto out;
	}

	if (set_node_online) {
		node_set_online(nid);
		ret = register_one_node(nid);
		BUG_ON(ret);
	}
out:
	return ret;
}

/*
 * Users of this function always want to online/register the node
 */
int try_online_node(int nid)
{
	int ret;

	mem_hotplug_begin();
	ret =  __try_online_node(nid, 0, true);
	mem_hotplug_done();
	return ret;
}

static int check_hotplug_memory_range(u64 start, u64 size)
{
	/* memory range must be block size aligned */
	if (!size || !IS_ALIGNED(start, memory_block_size_bytes()) ||
	    !IS_ALIGNED(size, memory_block_size_bytes())) {
		pr_err("Block size [%#lx] unaligned hotplug range: start %#llx, size %#llx",
		       memory_block_size_bytes(), start, size);
		return -EINVAL;
	}

	return 0;
}

static int online_memory_block(struct memory_block *mem, void *arg)
{
	return device_online(&mem->dev);
}

/*
 * NOTE: The caller must call lock_device_hotplug() to serialize hotplug
 * and online/offline operations (triggered e.g. by sysfs).
 *
 * we are OK calling __meminit stuff here - we have CONFIG_MEMORY_HOTPLUG
 */
int __ref add_memory_resource(int nid, struct resource *res)
{
	struct mhp_restrictions restrictions = {};
	u64 start, size;
	bool new_node = false;
	int ret;

	start = res->start;
	size = resource_size(res);

	ret = check_hotplug_memory_range(start, size);
	if (ret)
		return ret;

	mem_hotplug_begin();

	/*
	 * Add new range to memblock so that when hotadd_new_pgdat() is called
	 * to allocate new pgdat, get_pfn_range_for_nid() will be able to find
	 * this new range and calculate total pages correctly.  The range will
	 * be removed at hot-remove time.
	 */
	memblock_add_node(start, size, nid);

	ret = __try_online_node(nid, start, false);
	if (ret < 0)
		goto error;
	new_node = ret;

	/* call arch's memory hotadd */
	ret = arch_add_memory(nid, start, size, &restrictions);
	if (ret < 0)
		goto error;

	/* create memory block devices after memory was added */
	ret = create_memory_block_devices(start, size);
	if (ret) {
		arch_remove_memory(nid, start, size, NULL);
		goto error;
	}

	if (new_node) {
		/* If sysfs file of new node can't be created, cpu on the node
		 * can't be hot-added. There is no rollback way now.
		 * So, check by BUG_ON() to catch it reluctantly..
		 * We online node here. We can't roll back from here.
		 */
		node_set_online(nid);
		ret = __register_one_node(nid);
		BUG_ON(ret);
	}

	/* link memory sections under this node.*/
	ret = link_mem_sections(nid, PFN_DOWN(start), PFN_UP(start + size - 1));
	BUG_ON(ret);

	/* create new memmap entry */
	firmware_map_add_hotplug(start, start + size, "System RAM");

	/* device_online() will take the lock when calling online_pages() */
	mem_hotplug_done();

	/* online pages if requested */
	if (memhp_auto_online)
		walk_memory_blocks(start, size, NULL, online_memory_block);

	return ret;
error:
	/* rollback pgdat allocation and others */
	if (new_node)
		rollback_node_hotadd(nid);
	memblock_remove(start, size);
	mem_hotplug_done();
	return ret;
}

/* requires device_hotplug_lock, see add_memory_resource() */
int __ref __add_memory(int nid, u64 start, u64 size)
{
	struct resource *res;
	int ret;

	res = register_memory_resource(start, size);
	if (IS_ERR(res))
		return PTR_ERR(res);

	ret = add_memory_resource(nid, res);
	if (ret < 0)
		release_memory_resource(res);
	return ret;
}

int add_memory(int nid, u64 start, u64 size)
{
	int rc;

	lock_device_hotplug();
	rc = __add_memory(nid, start, size);
	unlock_device_hotplug();

	return rc;
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

/* Return the pfn of the start of the next active pageblock after a given pfn */
static unsigned long next_active_pageblock(unsigned long pfn)
{
	struct page *page = pfn_to_page(pfn);

	/* Ensure the starting page is pageblock-aligned */
	BUG_ON(pfn & (pageblock_nr_pages - 1));

	/* If the entire pageblock is free, move to the end of free page */
	if (pageblock_free(page)) {
		int order;
		/* be careful. we don't have locks, page_order can be changed.*/
		order = page_order(page);
		if ((order < MAX_ORDER) && (order >= pageblock_order))
			return pfn + (1 << order);
	}

	return pfn + pageblock_nr_pages;
}

static bool is_pageblock_removable_nolock(unsigned long pfn)
{
	struct page *page = pfn_to_page(pfn);
	struct zone *zone;

	/*
	 * We have to be careful here because we are iterating over memory
	 * sections which are not zone aware so we might end up outside of
	 * the zone but still within the section.
	 * We have to take care about the node as well. If the node is offline
	 * its NODE_DATA will be NULL - see page_zone.
	 */
	if (!node_online(page_to_nid(page)))
		return false;

	zone = page_zone(page);
	pfn = page_to_pfn(page);
	if (!zone_spans_pfn(zone, pfn))
		return false;

	return !has_unmovable_pages(zone, page, 0, MIGRATE_MOVABLE, SKIP_HWPOISON);
}

/* Checks if this range of memory is likely to be hot-removable. */
bool is_mem_section_removable(unsigned long start_pfn, unsigned long nr_pages)
{
	unsigned long end_pfn, pfn;

	end_pfn = min(start_pfn + nr_pages,
			zone_end_pfn(page_zone(pfn_to_page(start_pfn))));

	/* Check the starting page of each pageblock within the range */
	for (pfn = start_pfn; pfn < end_pfn; pfn = next_active_pageblock(pfn)) {
		if (!is_pageblock_removable_nolock(pfn))
			return false;
		cond_resched();
	}

	/* All pageblocks in the memory block are likely to be hot-removable */
	return true;
}

/*
 * Confirm all pages in a range [start, end) belong to the same zone.
 * When true, return its valid [start, end).
 */
int test_pages_in_a_zone(unsigned long start_pfn, unsigned long end_pfn,
			 unsigned long *valid_start, unsigned long *valid_end)
{
	unsigned long pfn, sec_end_pfn;
	unsigned long start, end;
	struct zone *zone = NULL;
	struct page *page;
	int i;
	for (pfn = start_pfn, sec_end_pfn = SECTION_ALIGN_UP(start_pfn + 1);
	     pfn < end_pfn;
	     pfn = sec_end_pfn, sec_end_pfn += PAGES_PER_SECTION) {
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
			if (i == MAX_ORDER_NR_PAGES || pfn + i >= end_pfn)
				continue;
			/* Check if we got outside of the zone */
			if (zone && !zone_spans_pfn(zone, pfn + i))
				return 0;
			page = pfn_to_page(pfn + i);
			if (zone && page_zone(page) != zone)
				return 0;
			if (!zone)
				start = pfn + i;
			zone = page_zone(page);
			end = pfn + MAX_ORDER_NR_PAGES;
		}
	}

	if (zone) {
		*valid_start = start;
		*valid_end = min(end, end_pfn);
		return 1;
	} else {
		return 0;
	}
}

/*
 * Scan pfn range [start,end) to find movable/migratable pages (LRU pages,
 * non-lru movable pages and hugepages). We scan pfn because it's much
 * easier than scanning over linked list. This function returns the pfn
 * of the first found movable page if it's found, otherwise 0.
 */
static unsigned long scan_movable_pages(unsigned long start, unsigned long end)
{
	unsigned long pfn;

	for (pfn = start; pfn < end; pfn++) {
		struct page *page, *head;
		unsigned long skip;

		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		if (PageLRU(page))
			return pfn;
		if (__PageMovable(page))
			return pfn;

		if (!PageHuge(page))
			continue;
		head = compound_head(page);
		if (page_huge_active(head))
			return pfn;
		skip = compound_nr(head) - (page - head);
		pfn += skip - 1;
	}
	return 0;
}

static struct page *new_node_page(struct page *page, unsigned long private)
{
	int nid = page_to_nid(page);
	nodemask_t nmask = node_states[N_MEMORY];

	/*
	 * try to allocate from a different node but reuse this node if there
	 * are no other online nodes to be used (e.g. we are offlining a part
	 * of the only existing node)
	 */
	node_clear(nid, nmask);
	if (nodes_empty(nmask))
		node_set(nid, nmask);

	return new_page_nodemask(page, nid, &nmask);
}

static int
do_migrate_range(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;
	struct page *page;
	int ret = 0;
	LIST_HEAD(source);

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);

		if (PageHuge(page)) {
			struct page *head = compound_head(page);
			pfn = page_to_pfn(head) + compound_nr(head) - 1;
			isolate_huge_page(head, &source);
			continue;
		} else if (PageTransHuge(page))
			pfn = page_to_pfn(compound_head(page))
				+ hpage_nr_pages(page) - 1;

		/*
		 * HWPoison pages have elevated reference counts so the migration would
		 * fail on them. It also doesn't make any sense to migrate them in the
		 * first place. Still try to unmap such a page in case it is still mapped
		 * (e.g. current hwpoison implementation doesn't unmap KSM pages but keep
		 * the unmap as the catch all safety net).
		 */
		if (PageHWPoison(page)) {
			if (WARN_ON(PageLRU(page)))
				isolate_lru_page(page);
			if (page_mapped(page))
				try_to_unmap(page, TTU_IGNORE_MLOCK | TTU_IGNORE_ACCESS);
			continue;
		}

		if (!get_page_unless_zero(page))
			continue;
		/*
		 * We can skip free pages. And we can deal with pages on
		 * LRU and non-lru movable pages.
		 */
		if (PageLRU(page))
			ret = isolate_lru_page(page);
		else
			ret = isolate_movable_page(page, ISOLATE_UNEVICTABLE);
		if (!ret) { /* Success */
			list_add_tail(&page->lru, &source);
			if (!__PageMovable(page))
				inc_node_page_state(page, NR_ISOLATED_ANON +
						    page_is_file_cache(page));

		} else {
			pr_warn("failed to isolate pfn %lx\n", pfn);
			dump_page(page, "isolation failed");
		}
		put_page(page);
	}
	if (!list_empty(&source)) {
		/* Allocate a new page from the nearest neighbor node */
		ret = migrate_pages(&source, new_node_page, NULL, 0,
					MIGRATE_SYNC, MR_MEMORY_HOTPLUG);
		if (ret) {
			list_for_each_entry(page, &source, lru) {
				pr_warn("migrating pfn %lx failed ret:%d ",
				       page_to_pfn(page), ret);
				dump_page(page, "migration failure");
			}
			putback_movable_pages(&source);
		}
	}

	return ret;
}

/*
 * remove from free_area[] and mark all as Reserved.
 */
static int
offline_isolated_pages_cb(unsigned long start, unsigned long nr_pages,
			void *data)
{
	unsigned long *offlined_pages = (unsigned long *)data;

	*offlined_pages += __offline_isolated_pages(start, start + nr_pages);
	return 0;
}

/*
 * Check all pages in range, recoreded as memory resource, are isolated.
 */
static int
check_pages_isolated_cb(unsigned long start_pfn, unsigned long nr_pages,
			void *data)
{
	return test_pages_isolated(start_pfn, start_pfn + nr_pages, true);
}

static int __init cmdline_parse_movable_node(char *p)
{
#ifdef CONFIG_HAVE_MEMBLOCK_NODE_MAP
	movable_node_enabled = true;
#else
	pr_warn("movable_node parameter depends on CONFIG_HAVE_MEMBLOCK_NODE_MAP to work properly\n");
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
	enum zone_type zt;

	arg->status_change_nid = NUMA_NO_NODE;
	arg->status_change_nid_normal = NUMA_NO_NODE;
	arg->status_change_nid_high = NUMA_NO_NODE;

	/*
	 * Check whether node_states[N_NORMAL_MEMORY] will be changed.
	 * If the memory to be offline is within the range
	 * [0..ZONE_NORMAL], and it is the last present memory there,
	 * the zones in that range will become empty after the offlining,
	 * thus we can determine that we need to clear the node from
	 * node_states[N_NORMAL_MEMORY].
	 */
	for (zt = 0; zt <= ZONE_NORMAL; zt++)
		present_pages += pgdat->node_zones[zt].present_pages;
	if (zone_idx(zone) <= ZONE_NORMAL && nr_pages >= present_pages)
		arg->status_change_nid_normal = zone_to_nid(zone);

#ifdef CONFIG_HIGHMEM
	/*
	 * node_states[N_HIGH_MEMORY] contains nodes which
	 * have normal memory or high memory.
	 * Here we add the present_pages belonging to ZONE_HIGHMEM.
	 * If the zone is within the range of [0..ZONE_HIGHMEM), and
	 * we determine that the zones in that range become empty,
	 * we need to clear the node for N_HIGH_MEMORY.
	 */
	present_pages += pgdat->node_zones[ZONE_HIGHMEM].present_pages;
	if (zone_idx(zone) <= ZONE_HIGHMEM && nr_pages >= present_pages)
		arg->status_change_nid_high = zone_to_nid(zone);
#endif

	/*
	 * We have accounted the pages from [0..ZONE_NORMAL), and
	 * in case of CONFIG_HIGHMEM the pages from ZONE_HIGHMEM
	 * as well.
	 * Here we count the possible pages from ZONE_MOVABLE.
	 * If after having accounted all the pages, we see that the nr_pages
	 * to be offlined is over or equal to the accounted pages,
	 * we know that the node will become empty, and so, we can clear
	 * it for N_MEMORY as well.
	 */
	present_pages += pgdat->node_zones[ZONE_MOVABLE].present_pages;

	if (nr_pages >= present_pages)
		arg->status_change_nid = zone_to_nid(zone);
}

static void node_states_clear_node(int node, struct memory_notify *arg)
{
	if (arg->status_change_nid_normal >= 0)
		node_clear_state(node, N_NORMAL_MEMORY);

	if (arg->status_change_nid_high >= 0)
		node_clear_state(node, N_HIGH_MEMORY);

	if (arg->status_change_nid >= 0)
		node_clear_state(node, N_MEMORY);
}

static int __ref __offline_pages(unsigned long start_pfn,
		  unsigned long end_pfn)
{
	unsigned long pfn, nr_pages;
	unsigned long offlined_pages = 0;
	int ret, node, nr_isolate_pageblock;
	unsigned long flags;
	unsigned long valid_start, valid_end;
	struct zone *zone;
	struct memory_notify arg;
	char *reason;

	mem_hotplug_begin();

	/* This makes hotplug much easier...and readable.
	   we assume this for now. .*/
	if (!test_pages_in_a_zone(start_pfn, end_pfn, &valid_start,
				  &valid_end)) {
		ret = -EINVAL;
		reason = "multizone range";
		goto failed_removal;
	}

	zone = page_zone(pfn_to_page(valid_start));
	node = zone_to_nid(zone);
	nr_pages = end_pfn - start_pfn;

	/* set above range as isolated */
	ret = start_isolate_page_range(start_pfn, end_pfn,
				       MIGRATE_MOVABLE,
				       SKIP_HWPOISON | REPORT_FAILURE);
	if (ret < 0) {
		reason = "failure to isolate range";
		goto failed_removal;
	}
	nr_isolate_pageblock = ret;

	arg.start_pfn = start_pfn;
	arg.nr_pages = nr_pages;
	node_states_check_changes_offline(nr_pages, zone, &arg);

	ret = memory_notify(MEM_GOING_OFFLINE, &arg);
	ret = notifier_to_errno(ret);
	if (ret) {
		reason = "notifier failure";
		goto failed_removal_isolated;
	}

	do {
		for (pfn = start_pfn; pfn;) {
			if (signal_pending(current)) {
				ret = -EINTR;
				reason = "signal backoff";
				goto failed_removal_isolated;
			}

			cond_resched();
			lru_add_drain_all();

			pfn = scan_movable_pages(pfn, end_pfn);
			if (pfn) {
				/*
				 * TODO: fatal migration failures should bail
				 * out
				 */
				do_migrate_range(pfn, end_pfn);
			}
		}

		/*
		 * Dissolve free hugepages in the memory block before doing
		 * offlining actually in order to make hugetlbfs's object
		 * counting consistent.
		 */
		ret = dissolve_free_huge_pages(start_pfn, end_pfn);
		if (ret) {
			reason = "failure to dissolve huge pages";
			goto failed_removal_isolated;
		}
		/* check again */
		ret = walk_system_ram_range(start_pfn, end_pfn - start_pfn,
					    NULL, check_pages_isolated_cb);
	} while (ret);

	/* Ok, all of our target is isolated.
	   We cannot do rollback at this point. */
	walk_system_ram_range(start_pfn, end_pfn - start_pfn,
			      &offlined_pages, offline_isolated_pages_cb);
	pr_info("Offlined Pages %ld\n", offlined_pages);
	/*
	 * Onlining will reset pagetype flags and makes migrate type
	 * MOVABLE, so just need to decrease the number of isolated
	 * pageblocks zone counter here.
	 */
	spin_lock_irqsave(&zone->lock, flags);
	zone->nr_isolate_pageblock -= nr_isolate_pageblock;
	spin_unlock_irqrestore(&zone->lock, flags);

	/* removal success */
	adjust_managed_page_count(pfn_to_page(start_pfn), -offlined_pages);
	zone->present_pages -= offlined_pages;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	zone->zone_pgdat->node_present_pages -= offlined_pages;
	pgdat_resize_unlock(zone->zone_pgdat, &flags);

	init_per_zone_wmark_min();

	if (!populated_zone(zone)) {
		zone_pcp_reset(zone);
		build_all_zonelists(NULL);
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
	mem_hotplug_done();
	return 0;

failed_removal_isolated:
	undo_isolate_page_range(start_pfn, end_pfn, MIGRATE_MOVABLE);
	memory_notify(MEM_CANCEL_OFFLINE, &arg);
failed_removal:
	pr_debug("memory offlining [mem %#010llx-%#010llx] failed due to %s\n",
		 (unsigned long long) start_pfn << PAGE_SHIFT,
		 ((unsigned long long) end_pfn << PAGE_SHIFT) - 1,
		 reason);
	/* pushback to free area */
	mem_hotplug_done();
	return ret;
}

int offline_pages(unsigned long start_pfn, unsigned long nr_pages)
{
	return __offline_pages(start_pfn, start_pfn + nr_pages);
}

static int check_memblock_offlined_cb(struct memory_block *mem, void *arg)
{
	int ret = !is_memblock_offlined(mem);

	if (unlikely(ret)) {
		phys_addr_t beginpa, endpa;

		beginpa = PFN_PHYS(section_nr_to_pfn(mem->start_section_nr));
		endpa = PFN_PHYS(section_nr_to_pfn(mem->end_section_nr + 1))-1;
		pr_warn("removing memory fails, because memory [%pa-%pa] is onlined\n",
			&beginpa, &endpa);

		return -EBUSY;
	}
	return 0;
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

/**
 * try_offline_node
 * @nid: the node ID
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

	if (check_cpu_on_node(pgdat))
		return;

	/*
	 * all memory/cpu of this node are removed, we can offline this
	 * node now.
	 */
	node_set_offline(nid);
	unregister_one_node(nid);
}
EXPORT_SYMBOL(try_offline_node);

static void __release_memory_resource(resource_size_t start,
				      resource_size_t size)
{
	int ret;

	/*
	 * When removing memory in the same granularity as it was added,
	 * this function never fails. It might only fail if resources
	 * have to be adjusted or split. We'll ignore the error, as
	 * removing of memory cannot fail.
	 */
	ret = release_mem_region_adjustable(&iomem_resource, start, size);
	if (ret) {
		resource_size_t endres = start + size - 1;

		pr_warn("Unable to release resource <%pa-%pa> (%d)\n",
			&start, &endres, ret);
	}
}

static int __ref try_remove_memory(int nid, u64 start, u64 size)
{
	int rc = 0;

	BUG_ON(check_hotplug_memory_range(start, size));

	mem_hotplug_begin();

	/*
	 * All memory blocks must be offlined before removing memory.  Check
	 * whether all memory blocks in question are offline and return error
	 * if this is not the case.
	 */
	rc = walk_memory_blocks(start, size, NULL, check_memblock_offlined_cb);
	if (rc)
		goto done;

	/* remove memmap entry */
	firmware_map_remove(start, start + size, "System RAM");
	memblock_free(start, size);
	memblock_remove(start, size);

	/* remove memory block devices before removing memory */
	remove_memory_block_devices(start, size);

	arch_remove_memory(nid, start, size, NULL);
	__release_memory_resource(start, size);

	try_offline_node(nid);

done:
	mem_hotplug_done();
	return rc;
}

/**
 * remove_memory
 * @nid: the node ID
 * @start: physical address of the region to remove
 * @size: size of the region to remove
 *
 * NOTE: The caller must call lock_device_hotplug() to serialize hotplug
 * and online/offline operations before this call, as required by
 * try_offline_node().
 */
void __remove_memory(int nid, u64 start, u64 size)
{

	/*
	 * trigger BUG() is some memory is not offlined prior to calling this
	 * function
	 */
	if (try_remove_memory(nid, start, size))
		BUG();
}

/*
 * Remove memory if every memory block is offline, otherwise return -EBUSY is
 * some memory is not offline
 */
int remove_memory(int nid, u64 start, u64 size)
{
	int rc;

	lock_device_hotplug();
	rc  = try_remove_memory(nid, start, size);
	unlock_device_hotplug();

	return rc;
}
EXPORT_SYMBOL_GPL(remove_memory);
#endif /* CONFIG_MEMORY_HOTREMOVE */
