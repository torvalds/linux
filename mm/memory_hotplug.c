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
int memhp_default_online_type = MMOP_OFFLINE;
#else
int memhp_default_online_type = MMOP_ONLINE;
#endif

static int __init setup_memhp_default_state(char *str)
{
	const int online_type = memhp_online_type_from_str(str);

	if (online_type >= 0)
		memhp_default_online_type = online_type;

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
static struct resource *register_memory_resource(u64 start, u64 size,
						 const char *resource_name)
{
	struct resource *res;
	unsigned long flags =  IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;

	if (strcmp(resource_name, "System RAM"))
		flags |= IORESOURCE_SYSRAM_DRIVER_MANAGED;

	/*
	 * Make sure value parsed from 'mem=' only restricts memory adding
	 * while booting, so that memory hotplug won't be impacted. Please
	 * refer to document of 'mem=' in kernel-parameters.txt for more
	 * details.
	 */
	if (start + size > max_mem_size && system_state < SYSTEM_RUNNING)
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

static int check_hotplug_memory_addressable(unsigned long pfn,
					    unsigned long nr_pages)
{
	const u64 max_addr = PFN_PHYS(pfn + nr_pages) - 1;

	if (max_addr >> MAX_PHYSMEM_BITS) {
		const u64 max_allowed = (1ull << (MAX_PHYSMEM_BITS + 1)) - 1;
		WARN(1,
		     "Hotplugged memory exceeds maximum addressable address, range=%#llx-%#llx, maximum=%#llx\n",
		     (u64)PFN_PHYS(pfn), max_addr, max_allowed);
		return -E2BIG;
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
		struct mhp_params *params)
{
	const unsigned long end_pfn = pfn + nr_pages;
	unsigned long cur_nr_pages;
	int err;
	struct vmem_altmap *altmap = params->altmap;

	if (WARN_ON_ONCE(!params->pgprot.pgprot))
		return -EINVAL;

	err = check_hotplug_memory_addressable(pfn, nr_pages);
	if (err)
		return err;

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

	for (; pfn < end_pfn; pfn += cur_nr_pages) {
		/* Select all remaining pages up to the next section boundary */
		cur_nr_pages = min(end_pfn - pfn,
				   SECTION_ALIGN_UP(pfn + 1) - pfn);
		err = sparse_add_section(nid, pfn, cur_nr_pages, altmap);
		if (err)
			break;
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
		if (unlikely(!pfn_to_online_page(start_pfn)))
			continue;

		if (unlikely(pfn_to_nid(start_pfn) != nid))
			continue;

		if (zone != page_zone(pfn_to_page(start_pfn)))
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
		if (unlikely(!pfn_to_online_page(pfn)))
			continue;

		if (unlikely(pfn_to_nid(pfn) != nid))
			continue;

		if (zone != page_zone(pfn_to_page(pfn)))
			continue;

		return pfn;
	}

	return 0;
}

static void shrink_zone_span(struct zone *zone, unsigned long start_pfn,
			     unsigned long end_pfn)
{
	unsigned long pfn;
	int nid = zone_to_nid(zone);

	zone_span_writelock(zone);
	if (zone->zone_start_pfn == start_pfn) {
		/*
		 * If the section is smallest section in the zone, it need
		 * shrink zone->zone_start_pfn and zone->zone_spanned_pages.
		 * In this case, we find second smallest valid mem_section
		 * for shrinking zone.
		 */
		pfn = find_smallest_section_pfn(nid, zone, end_pfn,
						zone_end_pfn(zone));
		if (pfn) {
			zone->spanned_pages = zone_end_pfn(zone) - pfn;
			zone->zone_start_pfn = pfn;
		} else {
			zone->zone_start_pfn = 0;
			zone->spanned_pages = 0;
		}
	} else if (zone_end_pfn(zone) == end_pfn) {
		/*
		 * If the section is biggest section in the zone, it need
		 * shrink zone->spanned_pages.
		 * In this case, we find second biggest valid mem_section for
		 * shrinking zone.
		 */
		pfn = find_biggest_section_pfn(nid, zone, zone->zone_start_pfn,
					       start_pfn);
		if (pfn)
			zone->spanned_pages = pfn - zone->zone_start_pfn + 1;
		else {
			zone->zone_start_pfn = 0;
			zone->spanned_pages = 0;
		}
	}
	zone_span_writeunlock(zone);
}

static void update_pgdat_span(struct pglist_data *pgdat)
{
	unsigned long node_start_pfn = 0, node_end_pfn = 0;
	struct zone *zone;

	for (zone = pgdat->node_zones;
	     zone < pgdat->node_zones + MAX_NR_ZONES; zone++) {
		unsigned long zone_end_pfn = zone->zone_start_pfn +
					     zone->spanned_pages;

		/* No need to lock the zones, they can't change. */
		if (!zone->spanned_pages)
			continue;
		if (!node_end_pfn) {
			node_start_pfn = zone->zone_start_pfn;
			node_end_pfn = zone_end_pfn;
			continue;
		}

		if (zone_end_pfn > node_end_pfn)
			node_end_pfn = zone_end_pfn;
		if (zone->zone_start_pfn < node_start_pfn)
			node_start_pfn = zone->zone_start_pfn;
	}

	pgdat->node_start_pfn = node_start_pfn;
	pgdat->node_spanned_pages = node_end_pfn - node_start_pfn;
}

void __ref remove_pfn_range_from_zone(struct zone *zone,
				      unsigned long start_pfn,
				      unsigned long nr_pages)
{
	const unsigned long end_pfn = start_pfn + nr_pages;
	struct pglist_data *pgdat = zone->zone_pgdat;
	unsigned long pfn, cur_nr_pages, flags;

	/* Poison struct pages because they are now uninitialized again. */
	for (pfn = start_pfn; pfn < end_pfn; pfn += cur_nr_pages) {
		cond_resched();

		/* Select all remaining pages up to the next section boundary */
		cur_nr_pages =
			min(end_pfn - pfn, SECTION_ALIGN_UP(pfn + 1) - pfn);
		page_init_poison(pfn_to_page(pfn),
				 sizeof(struct page) * cur_nr_pages);
	}

#ifdef CONFIG_ZONE_DEVICE
	/*
	 * Zone shrinking code cannot properly deal with ZONE_DEVICE. So
	 * we will not try to shrink the zones - which is okay as
	 * set_zone_contiguous() cannot deal with ZONE_DEVICE either way.
	 */
	if (zone_idx(zone) == ZONE_DEVICE)
		return;
#endif

	clear_zone_contiguous(zone);

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	shrink_zone_span(zone, start_pfn, start_pfn + nr_pages);
	update_pgdat_span(pgdat);
	pgdat_resize_unlock(zone->zone_pgdat, &flags);

	set_zone_contiguous(zone);
}

static void __remove_section(unsigned long pfn, unsigned long nr_pages,
			     unsigned long map_offset,
			     struct vmem_altmap *altmap)
{
	struct mem_section *ms = __pfn_to_section(pfn);

	if (WARN_ON_ONCE(!valid_section(ms)))
		return;

	sparse_remove_section(ms, pfn, nr_pages, map_offset, altmap);
}

/**
 * __remove_pages() - remove sections of pages
 * @pfn: starting pageframe (must be aligned to start of a section)
 * @nr_pages: number of pages to remove (must be multiple of section size)
 * @altmap: alternative device page map or %NULL if default memmap is used
 *
 * Generic helper function to remove section mappings and sysfs entries
 * for the section of the memory we are removing. Caller needs to make
 * sure that pages are marked reserved and zones are adjust properly by
 * calling offline_pages().
 */
void __remove_pages(unsigned long pfn, unsigned long nr_pages,
		    struct vmem_altmap *altmap)
{
	const unsigned long end_pfn = pfn + nr_pages;
	unsigned long cur_nr_pages;
	unsigned long map_offset = 0;

	map_offset = vmem_altmap_offset(altmap);

	if (check_pfn_span(pfn, nr_pages, "remove"))
		return;

	for (; pfn < end_pfn; pfn += cur_nr_pages) {
		cond_resched();
		/* Select all remaining pages up to the next section boundary */
		cur_nr_pages = min(end_pfn - pfn,
				   SECTION_ALIGN_UP(pfn + 1) - pfn);
		__remove_section(pfn, cur_nr_pages, map_offset, altmap);
		map_offset = 0;
	}
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

void generic_online_page(struct page *page, unsigned int order)
{
	/*
	 * Freeing the page with debug_pagealloc enabled will try to unmap it,
	 * so we should map it first. This is better than introducing a special
	 * case in page freeing fast path.
	 */
	debug_pagealloc_map_pages(page, 1 << order);
	__free_pages_core(page, order);
	totalram_pages_add(1UL << order);
#ifdef CONFIG_HIGHMEM
	if (PageHighMem(page))
		totalhigh_pages_add(1UL << order);
#endif
}
EXPORT_SYMBOL_GPL(generic_online_page);

static void online_pages_range(unsigned long start_pfn, unsigned long nr_pages)
{
	const unsigned long end_pfn = start_pfn + nr_pages;
	unsigned long pfn;

	/*
	 * Online the pages in MAX_ORDER - 1 aligned chunks. The callback might
	 * decide to not expose all pages to the buddy (e.g., expose them
	 * later). We account all pages as being online and belonging to this
	 * zone ("present").
	 */
	for (pfn = start_pfn; pfn < end_pfn; pfn += MAX_ORDER_NR_PAGES)
		(*online_page_callback)(pfn_to_page(pfn), MAX_ORDER - 1);

	/* mark all involved sections as online */
	online_mem_sections(start_pfn, end_pfn);
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
 *
 * All aligned pageblocks are initialized to the specified migratetype
 * (usually MIGRATE_MOVABLE). Besides setting the migratetype, no related
 * zone stats (e.g., nr_isolate_pageblock) are touched.
 */
void __ref move_pfn_range_to_zone(struct zone *zone, unsigned long start_pfn,
				  unsigned long nr_pages,
				  struct vmem_altmap *altmap, int migratetype)
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
	memmap_init_zone(nr_pages, nid, zone_idx(zone), start_pfn, 0,
			 MEMINIT_HOTPLUG, altmap, migratetype);

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

int __ref online_pages(unsigned long pfn, unsigned long nr_pages,
		       int online_type, int nid)
{
	unsigned long flags;
	struct zone *zone;
	int need_zonelists_rebuild = 0;
	int ret;
	struct memory_notify arg;

	/* We can only online full sections (e.g., SECTION_IS_ONLINE) */
	if (WARN_ON_ONCE(!nr_pages ||
			 !IS_ALIGNED(pfn | nr_pages, PAGES_PER_SECTION)))
		return -EINVAL;

	mem_hotplug_begin();

	/* associate pfn range with the zone */
	zone = zone_for_pfn_range(online_type, nid, pfn, nr_pages);
	move_pfn_range_to_zone(zone, pfn, nr_pages, NULL, MIGRATE_ISOLATE);

	arg.start_pfn = pfn;
	arg.nr_pages = nr_pages;
	node_states_check_changes_online(nr_pages, zone, &arg);

	ret = memory_notify(MEM_GOING_ONLINE, &arg);
	ret = notifier_to_errno(ret);
	if (ret)
		goto failed_addition;

	/*
	 * Fixup the number of isolated pageblocks before marking the sections
	 * onlining, such that undo_isolate_page_range() works correctly.
	 */
	spin_lock_irqsave(&zone->lock, flags);
	zone->nr_isolate_pageblock += nr_pages / pageblock_nr_pages;
	spin_unlock_irqrestore(&zone->lock, flags);

	/*
	 * If this zone is not populated, then it is not in zonelist.
	 * This means the page allocator ignores this zone.
	 * So, zonelist must be updated after online.
	 */
	if (!populated_zone(zone)) {
		need_zonelists_rebuild = 1;
		setup_zone_pageset(zone);
	}

	online_pages_range(pfn, nr_pages);
	zone->present_pages += nr_pages;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	zone->zone_pgdat->node_present_pages += nr_pages;
	pgdat_resize_unlock(zone->zone_pgdat, &flags);

	node_states_set_node(nid, &arg);
	if (need_zonelists_rebuild)
		build_all_zonelists(NULL);
	zone_pcp_update(zone);

	/* Basic onlining is complete, allow allocation of onlined pages. */
	undo_isolate_page_range(pfn, pfn + nr_pages, MIGRATE_MOVABLE);

	/*
	 * Freshly onlined pages aren't shuffled (e.g., all pages are placed to
	 * the tail of the freelist when undoing isolation). Shuffle the whole
	 * zone to make sure the just onlined pages are properly distributed
	 * across the whole freelist - to create an initial shuffle.
	 */
	shuffle_zone(zone);

	init_per_zone_wmark_min();

	kswapd_run(nid);
	kcompactd_run(nid);

	writeback_set_ratelimit();

	memory_notify(MEM_ONLINE, &arg);
	mem_hotplug_done();
	return 0;

failed_addition:
	pr_debug("online_pages [mem %#010llx-%#010llx] failed\n",
		 (unsigned long long) pfn << PAGE_SHIFT,
		 (((unsigned long long) pfn + nr_pages) << PAGE_SHIFT) - 1);
	memory_notify(MEM_CANCEL_ONLINE, &arg);
	remove_pfn_range_from_zone(zone, pfn, nr_pages);
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
static pg_data_t __ref *hotadd_new_pgdat(int nid)
{
	struct pglist_data *pgdat;

	pgdat = NODE_DATA(nid);
	if (!pgdat) {
		pgdat = arch_alloc_nodedata(nid);
		if (!pgdat)
			return NULL;

		pgdat->per_cpu_nodestats =
			alloc_percpu(struct per_cpu_nodestat);
		arch_refresh_nodedata(nid, pgdat);
	} else {
		int cpu;
		/*
		 * Reset the nr_zones, order and highest_zoneidx before reuse.
		 * Note that kswapd will init kswapd_highest_zoneidx properly
		 * when it starts in the near future.
		 */
		pgdat->nr_zones = 0;
		pgdat->kswapd_order = 0;
		pgdat->kswapd_highest_zoneidx = 0;
		for_each_online_cpu(cpu) {
			struct per_cpu_nodestat *p;

			p = per_cpu_ptr(pgdat->per_cpu_nodestats, cpu);
			memset(p, 0, sizeof(*p));
		}
	}

	/* we can use NODE_DATA(nid) from here */
	pgdat->node_id = nid;
	pgdat->node_start_pfn = 0;

	/* init node's zones as empty zones, we don't have any present pages.*/
	free_area_init_core_hotplug(nid);

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
 * @set_node_online: Whether we want to online the node
 * called by cpu_up() to online a node without onlined memory.
 *
 * Returns:
 * 1 -> a new node has been allocated
 * 0 -> the node is already online
 * -ENOMEM -> the node could not be allocated
 */
static int __try_online_node(int nid, bool set_node_online)
{
	pg_data_t *pgdat;
	int ret = 1;

	if (node_online(nid))
		return 0;

	pgdat = hotadd_new_pgdat(nid);
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
	ret =  __try_online_node(nid, true);
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
	mem->online_type = memhp_default_online_type;
	return device_online(&mem->dev);
}

/*
 * NOTE: The caller must call lock_device_hotplug() to serialize hotplug
 * and online/offline operations (triggered e.g. by sysfs).
 *
 * we are OK calling __meminit stuff here - we have CONFIG_MEMORY_HOTPLUG
 */
int __ref add_memory_resource(int nid, struct resource *res, mhp_t mhp_flags)
{
	struct mhp_params params = { .pgprot = pgprot_mhp(PAGE_KERNEL) };
	u64 start, size;
	bool new_node = false;
	int ret;

	start = res->start;
	size = resource_size(res);

	ret = check_hotplug_memory_range(start, size);
	if (ret)
		return ret;

	if (!node_possible(nid)) {
		WARN(1, "node %d was absent from the node_possible_map\n", nid);
		return -EINVAL;
	}

	mem_hotplug_begin();

	if (IS_ENABLED(CONFIG_ARCH_KEEP_MEMBLOCK))
		memblock_add_node(start, size, nid);

	ret = __try_online_node(nid, false);
	if (ret < 0)
		goto error;
	new_node = ret;

	/* call arch's memory hotadd */
	ret = arch_add_memory(nid, start, size, &params);
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
	link_mem_sections(nid, PFN_DOWN(start), PFN_UP(start + size - 1),
			  MEMINIT_HOTPLUG);

	/* create new memmap entry */
	if (!strcmp(res->name, "System RAM"))
		firmware_map_add_hotplug(start, start + size, "System RAM");

	/* device_online() will take the lock when calling online_pages() */
	mem_hotplug_done();

	/*
	 * In case we're allowed to merge the resource, flag it and trigger
	 * merging now that adding succeeded.
	 */
	if (mhp_flags & MEMHP_MERGE_RESOURCE)
		merge_system_ram_resource(res);

	/* online pages if requested */
	if (memhp_default_online_type != MMOP_OFFLINE)
		walk_memory_blocks(start, size, NULL, online_memory_block);

	return ret;
error:
	/* rollback pgdat allocation and others */
	if (new_node)
		rollback_node_hotadd(nid);
	if (IS_ENABLED(CONFIG_ARCH_KEEP_MEMBLOCK))
		memblock_remove(start, size);
	mem_hotplug_done();
	return ret;
}

/* requires device_hotplug_lock, see add_memory_resource() */
int __ref __add_memory(int nid, u64 start, u64 size, mhp_t mhp_flags)
{
	struct resource *res;
	int ret;

	res = register_memory_resource(start, size, "System RAM");
	if (IS_ERR(res))
		return PTR_ERR(res);

	ret = add_memory_resource(nid, res, mhp_flags);
	if (ret < 0)
		release_memory_resource(res);
	return ret;
}

int add_memory(int nid, u64 start, u64 size, mhp_t mhp_flags)
{
	int rc;

	lock_device_hotplug();
	rc = __add_memory(nid, start, size, mhp_flags);
	unlock_device_hotplug();

	return rc;
}
EXPORT_SYMBOL_GPL(add_memory);

int add_memory_subsection(int nid, u64 start, u64 size)
{
	struct mhp_params params = { .pgprot = PAGE_KERNEL };
	struct resource *res;
	int ret;

	if (size == memory_block_size_bytes())
		return add_memory(nid, start, size, MHP_NONE);

	if (!IS_ALIGNED(start, SUBSECTION_SIZE) ||
	    !IS_ALIGNED(size, SUBSECTION_SIZE)) {
		pr_err("%s: start 0x%llx size 0x%llx not aligned to subsection size\n",
			   __func__, start, size);
		return -EINVAL;
	}

	res = register_memory_resource(start, size, "System RAM");
	if (IS_ERR(res))
		return PTR_ERR(res);

	mem_hotplug_begin();

	nid = memory_add_physaddr_to_nid(start);

	if (IS_ENABLED(CONFIG_ARCH_KEEP_MEMBLOCK))
		memblock_add_node(start, size, nid);

	ret = arch_add_memory(nid, start, size, &params);
	if (ret) {
		if (IS_ENABLED(CONFIG_ARCH_KEEP_MEMBLOCK))
			memblock_remove(start, size);
		pr_err("%s failed to add subsection start 0x%llx size 0x%llx\n",
			   __func__, start, size);
	}
	mem_hotplug_done();

	return ret;
}
EXPORT_SYMBOL_GPL(add_memory_subsection);

/*
 * Add special, driver-managed memory to the system as system RAM. Such
 * memory is not exposed via the raw firmware-provided memmap as system
 * RAM, instead, it is detected and added by a driver - during cold boot,
 * after a reboot, and after kexec.
 *
 * Reasons why this memory should not be used for the initial memmap of a
 * kexec kernel or for placing kexec images:
 * - The booting kernel is in charge of determining how this memory will be
 *   used (e.g., use persistent memory as system RAM)
 * - Coordination with a hypervisor is required before this memory
 *   can be used (e.g., inaccessible parts).
 *
 * For this memory, no entries in /sys/firmware/memmap ("raw firmware-provided
 * memory map") are created. Also, the created memory resource is flagged
 * with IORESOURCE_SYSRAM_DRIVER_MANAGED, so in-kernel users can special-case
 * this memory as well (esp., not place kexec images onto it).
 *
 * The resource_name (visible via /proc/iomem) has to have the format
 * "System RAM ($DRIVER)".
 */
int add_memory_driver_managed(int nid, u64 start, u64 size,
			      const char *resource_name, mhp_t mhp_flags)
{
	struct resource *res;
	int rc;

	if (!resource_name ||
	    strstr(resource_name, "System RAM (") != resource_name ||
	    resource_name[strlen(resource_name) - 1] != ')')
		return -EINVAL;

	lock_device_hotplug();

	res = register_memory_resource(start, size, resource_name);
	if (IS_ERR(res)) {
		rc = PTR_ERR(res);
		goto out_unlock;
	}

	rc = add_memory_resource(nid, res, mhp_flags);
	if (rc < 0)
		release_memory_resource(res);

out_unlock:
	unlock_device_hotplug();
	return rc;
}
EXPORT_SYMBOL_GPL(add_memory_driver_managed);

#ifdef CONFIG_MEMORY_HOTREMOVE
/*
 * Confirm all pages in a range [start, end) belong to the same zone (skipping
 * memory holes). When true, return the zone.
 */
struct zone *test_pages_in_a_zone(unsigned long start_pfn,
				  unsigned long end_pfn)
{
	unsigned long pfn, sec_end_pfn;
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
				return NULL;
			page = pfn_to_page(pfn + i);
			if (zone && page_zone(page) != zone)
				return NULL;
			zone = page_zone(page);
		}
	}

	return zone;
}

/*
 * Scan pfn range [start,end) to find movable/migratable pages (LRU pages,
 * non-lru movable pages and hugepages). Will skip over most unmovable
 * pages (esp., pages that can be skipped when offlining), but bail out on
 * definitely unmovable pages.
 *
 * Returns:
 *	0 in case a movable page is found and movable_pfn was updated.
 *	-ENOENT in case no movable page was found.
 *	-EBUSY in case a definitely unmovable page was found.
 */
static int scan_movable_pages(unsigned long start, unsigned long end,
			      unsigned long *movable_pfn)
{
	unsigned long pfn;

	for (pfn = start; pfn < end; pfn++) {
		struct page *page, *head;
		unsigned long skip;

		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		if (PageLRU(page))
			goto found;
		if (__PageMovable(page))
			goto found;

		/*
		 * PageOffline() pages that are not marked __PageMovable() and
		 * have a reference count > 0 (after MEM_GOING_OFFLINE) are
		 * definitely unmovable. If their reference count would be 0,
		 * they could at least be skipped when offlining memory.
		 */
		if (PageOffline(page) && page_count(page))
			return -EBUSY;

		if (!PageHuge(page))
			continue;
		head = compound_head(page);
		if (page_huge_active(head))
			goto found;
		skip = compound_nr(head) - (page - head);
		pfn += skip - 1;
	}
	return -ENOENT;
found:
	*movable_pfn = pfn;
	return 0;
}

static int
do_migrate_range(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn;
	struct page *page, *head;
	int ret = 0;
	LIST_HEAD(source);
	static DEFINE_RATELIMIT_STATE(migrate_rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	for (pfn = start_pfn; pfn < end_pfn; pfn++) {
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		head = compound_head(page);

		if (PageHuge(page)) {
			pfn = page_to_pfn(head) + compound_nr(head) - 1;
			isolate_huge_page(head, &source);
			continue;
		} else if (PageTransHuge(page))
			pfn = page_to_pfn(head) + thp_nr_pages(page) - 1;

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
				try_to_unmap(page, TTU_IGNORE_MLOCK);
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
						    page_is_file_lru(page));

		} else {
			if (__ratelimit(&migrate_rs)) {
				pr_warn("failed to isolate pfn %lx\n", pfn);
				dump_page(page, "isolation failed");
			}
		}
		put_page(page);
	}
	if (!list_empty(&source)) {
		nodemask_t nmask = node_states[N_MEMORY];
		struct migration_target_control mtc = {
			.nmask = &nmask,
			.gfp_mask = GFP_USER | __GFP_MOVABLE | __GFP_RETRY_MAYFAIL,
		};

		/*
		 * We have checked that migration range is on a single zone so
		 * we can use the nid of the first page to all the others.
		 */
		mtc.nid = page_to_nid(list_first_entry(&source, struct page, lru));

		/*
		 * try to allocate from a different node but reuse this node
		 * if there are no other online nodes to be used (e.g. we are
		 * offlining a part of the only existing node)
		 */
		node_clear(mtc.nid, nmask);
		if (nodes_empty(nmask))
			node_set(mtc.nid, nmask);
		ret = migrate_pages(&source, alloc_migration_target, NULL,
			(unsigned long)&mtc, MIGRATE_SYNC, MR_MEMORY_HOTPLUG);
		if (ret) {
			list_for_each_entry(page, &source, lru) {
				if (__ratelimit(&migrate_rs)) {
					pr_warn("migrating pfn %lx failed ret:%d\n",
						page_to_pfn(page), ret);
					dump_page(page, "migration failure");
				}
			}
			putback_movable_pages(&source);
		}
	}

	return ret;
}

static int __init cmdline_parse_movable_node(char *p)
{
	movable_node_enabled = true;
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

static int count_system_ram_pages_cb(unsigned long start_pfn,
				     unsigned long nr_pages, void *data)
{
	unsigned long *nr_system_ram_pages = data;

	*nr_system_ram_pages += nr_pages;
	return 0;
}

int __ref offline_pages(unsigned long start_pfn, unsigned long nr_pages)
{
	const unsigned long end_pfn = start_pfn + nr_pages;
	unsigned long pfn, system_ram_pages = 0;
	unsigned long flags;
	struct zone *zone;
	struct memory_notify arg;
	int ret, node;
	char *reason;

	/* We can only offline full sections (e.g., SECTION_IS_ONLINE) */
	if (WARN_ON_ONCE(!nr_pages ||
			 !IS_ALIGNED(start_pfn | nr_pages, PAGES_PER_SECTION)))
		return -EINVAL;

	mem_hotplug_begin();

	/*
	 * Don't allow to offline memory blocks that contain holes.
	 * Consequently, memory blocks with holes can never get onlined
	 * via the hotplug path - online_pages() - as hotplugged memory has
	 * no holes. This way, we e.g., don't have to worry about marking
	 * memory holes PG_reserved, don't need pfn_valid() checks, and can
	 * avoid using walk_system_ram_range() later.
	 */
	walk_system_ram_range(start_pfn, nr_pages, &system_ram_pages,
			      count_system_ram_pages_cb);
	if (system_ram_pages != nr_pages) {
		ret = -EINVAL;
		reason = "memory holes";
		goto failed_removal;
	}

	/* This makes hotplug much easier...and readable.
	   we assume this for now. .*/
	zone = test_pages_in_a_zone(start_pfn, end_pfn);
	if (!zone) {
		ret = -EINVAL;
		reason = "multizone range";
		goto failed_removal;
	}
	node = zone_to_nid(zone);

	lru_cache_disable();
	/* set above range as isolated */
	ret = start_isolate_page_range(start_pfn, end_pfn,
				       MIGRATE_MOVABLE,
				       MEMORY_OFFLINE | REPORT_FAILURE);
	if (ret) {
		reason = "failure to isolate range";
		goto failed_removal;
	}

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
		pfn = start_pfn;
		do {
			if (signal_pending(current)) {
				ret = -EINTR;
				reason = "signal backoff";
				goto failed_removal_isolated;
			}

			cond_resched();

			ret = scan_movable_pages(pfn, end_pfn, &pfn);
			if (!ret) {
				/*
				 * TODO: fatal migration failures should bail
				 * out
				 */
				do_migrate_range(pfn, end_pfn);
			}
		} while (!ret);

		if (ret != -ENOENT) {
			reason = "unmovable page";
			goto failed_removal_isolated;
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

		/*
		 * per-cpu pages are drained in start_isolate_page_range, but if
		 * there are still pages that are not free, make sure that we
		 * drain again, because when we isolated range we might
		 * have raced with another thread that was adding pages to pcp
		 * list.
		 *
		 * Forward progress should be still guaranteed because
		 * pages on the pcp list can only belong to MOVABLE_ZONE
		 * because has_unmovable_pages explicitly checks for
		 * PageBuddy on freed pages on other zones.
		 */
		ret = test_pages_isolated(start_pfn, end_pfn, MEMORY_OFFLINE);
		if (ret)
			drain_all_pages(zone);
	} while (ret);

	/* Mark all sections offline and remove free pages from the buddy. */
	__offline_isolated_pages(start_pfn, end_pfn);
	pr_info("Offlined Pages %ld\n", nr_pages);

	/*
	 * The memory sections are marked offline, and the pageblock flags
	 * effectively stale; nobody should be touching them. Fixup the number
	 * of isolated pageblocks, memory onlining will properly revert this.
	 */
	spin_lock_irqsave(&zone->lock, flags);
	zone->nr_isolate_pageblock -= nr_pages / pageblock_nr_pages;
	spin_unlock_irqrestore(&zone->lock, flags);

	lru_cache_enable();
	/* removal success */
	adjust_managed_page_count(pfn_to_page(start_pfn), -nr_pages);
	zone->present_pages -= nr_pages;

	pgdat_resize_lock(zone->zone_pgdat, &flags);
	zone->zone_pgdat->node_present_pages -= nr_pages;
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

	writeback_set_ratelimit();

	memory_notify(MEM_OFFLINE, &arg);
	remove_pfn_range_from_zone(zone, start_pfn, nr_pages);
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

static int check_memblock_offlined_cb(struct memory_block *mem, void *arg)
{
	int ret = !is_memblock_offlined(mem);

	if (unlikely(ret)) {
		phys_addr_t beginpa, endpa;

		beginpa = PFN_PHYS(section_nr_to_pfn(mem->start_section_nr));
		endpa = beginpa + memory_block_size_bytes() - 1;
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

static int check_no_memblock_for_node_cb(struct memory_block *mem, void *arg)
{
	int nid = *(int *)arg;

	/*
	 * If a memory block belongs to multiple nodes, the stored nid is not
	 * reliable. However, such blocks are always online (e.g., cannot get
	 * offlined) and, therefore, are still spanned by the node.
	 */
	return mem->nid == nid ? -EEXIST : 0;
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
	int rc;

	/*
	 * If the node still spans pages (especially ZONE_DEVICE), don't
	 * offline it. A node spans memory after move_pfn_range_to_zone(),
	 * e.g., after the memory block was onlined.
	 */
	if (pgdat->node_spanned_pages)
		return;

	/*
	 * Especially offline memory blocks might not be spanned by the
	 * node. They will get spanned by the node once they get onlined.
	 * However, they link to the node in sysfs and can get onlined later.
	 */
	rc = for_each_memory_block(&nid, check_no_memblock_for_node_cb);
	if (rc)
		return;

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

static int __ref try_remove_memory(int nid, u64 start, u64 size)
{
	int rc = 0;

	BUG_ON(check_hotplug_memory_range(start, size));

	/*
	 * All memory blocks must be offlined before removing memory.  Check
	 * whether all memory blocks in question are offline and return error
	 * if this is not the case.
	 */
	rc = walk_memory_blocks(start, size, NULL, check_memblock_offlined_cb);
	if (rc)
		return rc;

	/* remove memmap entry */
	firmware_map_remove(start, start + size, "System RAM");

	/*
	 * Memory block device removal under the device_hotplug_lock is
	 * a barrier against racing online attempts.
	 */
	remove_memory_block_devices(start, size);

	mem_hotplug_begin();

	arch_remove_memory(nid, start, size, NULL);

	if (IS_ENABLED(CONFIG_ARCH_KEEP_MEMBLOCK)) {
		memblock_free(start, size);
		memblock_remove(start, size);
	}

	release_mem_region_adjustable(start, size);

	try_offline_node(nid);

	mem_hotplug_done();
	return 0;
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
	 * trigger BUG() if some memory is not offlined prior to calling this
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

static bool __check_sections_offline(unsigned long start_pfn,
		unsigned long nr_pages)
{
	const unsigned long end_pfn = start_pfn + nr_pages;
	unsigned long pfn, sec_nr;

	for (pfn = start_pfn; pfn < end_pfn; pfn += PAGES_PER_SECTION) {
		sec_nr = pfn_to_section_nr(pfn);

		if (!valid_section_nr(sec_nr) || online_section_nr(sec_nr))
			return false;
	}

	return true;
}

int remove_memory_subsection(int nid, u64 start, u64 size)
{
	if (size ==  memory_block_size_bytes())
		return remove_memory(nid, start, size);

	if (!IS_ALIGNED(start, SUBSECTION_SIZE) ||
	    !IS_ALIGNED(size, SUBSECTION_SIZE)) {
		pr_err("%s: start 0x%llx size 0x%llx not aligned to subsection size\n",
			   __func__, start, size);
		return -EINVAL;
	}

	mem_hotplug_begin();

	/* we cannot remove subsections that are invalid or online */
	if(!__check_sections_offline(PHYS_PFN(start), size >> PAGE_SHIFT)) {
		pr_err("%s: [%llx, %llx) sections are not offlined\n",
			   __func__, start, start + size);
		mem_hotplug_done();
		return -EBUSY;
	}

	arch_remove_memory(nid, start, size, NULL);

	if (IS_ENABLED(CONFIG_ARCH_KEEP_MEMBLOCK))
		memblock_remove(start, size);

	release_mem_region_adjustable(start, size);

	mem_hotplug_done();

	return 0;
}
EXPORT_SYMBOL_GPL(remove_memory_subsection);

/*
 * Try to offline and remove a memory block. Might take a long time to
 * finish in case memory is still in use. Primarily useful for memory devices
 * that logically unplugged all memory (so it's no longer in use) and want to
 * offline + remove the memory block.
 */
int offline_and_remove_memory(int nid, u64 start, u64 size)
{
	struct memory_block *mem;
	int rc = -EINVAL;

	if (!IS_ALIGNED(start, memory_block_size_bytes()) ||
	    size != memory_block_size_bytes())
		return rc;

	lock_device_hotplug();
	mem = find_memory_block(__pfn_to_section(PFN_DOWN(start)));
	if (mem)
		rc = device_offline(&mem->dev);
	/* Ignore if the device is already offline. */
	if (rc > 0)
		rc = 0;

	/*
	 * In case we succeeded to offline the memory block, remove it.
	 * This cannot fail as it cannot get onlined in the meantime.
	 */
	if (!rc) {
		rc = try_remove_memory(nid, start, size);
		WARN_ON_ONCE(rc);
	}
	unlock_device_hotplug();

	return rc;
}
EXPORT_SYMBOL_GPL(offline_and_remove_memory);
#endif /* CONFIG_MEMORY_HOTREMOVE */
