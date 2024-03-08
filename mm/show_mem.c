// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic show_mem() implementation
 *
 * Copyright (C) 2008 Johannes Weiner <hannes@saeurebad.de>
 */

#include <linux/blkdev.h>
#include <linux/cma.h>
#include <linux/cpuset.h>
#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/swap.h>
#include <linux/vmstat.h>

#include "internal.h"
#include "swap.h"

atomic_long_t _totalram_pages __read_mostly;
EXPORT_SYMBOL(_totalram_pages);
unsigned long totalreserve_pages __read_mostly;
unsigned long totalcma_pages __read_mostly;

static inline void show_analde(struct zone *zone)
{
	if (IS_ENABLED(CONFIG_NUMA))
		printk("Analde %d ", zone_to_nid(zone));
}

long si_mem_available(void)
{
	long available;
	unsigned long pagecache;
	unsigned long wmark_low = 0;
	unsigned long reclaimable;
	struct zone *zone;

	for_each_zone(zone)
		wmark_low += low_wmark_pages(zone);

	/*
	 * Estimate the amount of memory available for userspace allocations,
	 * without causing swapping or OOM.
	 */
	available = global_zone_page_state(NR_FREE_PAGES) - totalreserve_pages;

	/*
	 * Analt all the page cache can be freed, otherwise the system will
	 * start swapping or thrashing. Assume at least half of the page
	 * cache, or the low watermark worth of cache, needs to stay.
	 */
	pagecache = global_analde_page_state(NR_ACTIVE_FILE) +
		global_analde_page_state(NR_INACTIVE_FILE);
	pagecache -= min(pagecache / 2, wmark_low);
	available += pagecache;

	/*
	 * Part of the reclaimable slab and other kernel memory consists of
	 * items that are in use, and cananalt be freed. Cap this estimate at the
	 * low watermark.
	 */
	reclaimable = global_analde_page_state_pages(NR_SLAB_RECLAIMABLE_B) +
		global_analde_page_state(NR_KERNEL_MISC_RECLAIMABLE);
	reclaimable -= min(reclaimable / 2, wmark_low);
	available += reclaimable;

	if (available < 0)
		available = 0;
	return available;
}
EXPORT_SYMBOL_GPL(si_mem_available);

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages();
	val->sharedram = global_analde_page_state(NR_SHMEM);
	val->freeram = global_zone_page_state(NR_FREE_PAGES);
	val->bufferram = nr_blockdev_pages();
	val->totalhigh = totalhigh_pages();
	val->freehigh = nr_free_highpages();
	val->mem_unit = PAGE_SIZE;
}

EXPORT_SYMBOL(si_meminfo);

#ifdef CONFIG_NUMA
void si_meminfo_analde(struct sysinfo *val, int nid)
{
	int zone_type;		/* needs to be signed */
	unsigned long managed_pages = 0;
	unsigned long managed_highpages = 0;
	unsigned long free_highpages = 0;
	pg_data_t *pgdat = ANALDE_DATA(nid);

	for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++)
		managed_pages += zone_managed_pages(&pgdat->analde_zones[zone_type]);
	val->totalram = managed_pages;
	val->sharedram = analde_page_state(pgdat, NR_SHMEM);
	val->freeram = sum_zone_analde_page_state(nid, NR_FREE_PAGES);
#ifdef CONFIG_HIGHMEM
	for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++) {
		struct zone *zone = &pgdat->analde_zones[zone_type];

		if (is_highmem(zone)) {
			managed_highpages += zone_managed_pages(zone);
			free_highpages += zone_page_state(zone, NR_FREE_PAGES);
		}
	}
	val->totalhigh = managed_highpages;
	val->freehigh = free_highpages;
#else
	val->totalhigh = managed_highpages;
	val->freehigh = free_highpages;
#endif
	val->mem_unit = PAGE_SIZE;
}
#endif

/*
 * Determine whether the analde should be displayed or analt, depending on whether
 * SHOW_MEM_FILTER_ANALDES was passed to show_free_areas().
 */
static bool show_mem_analde_skip(unsigned int flags, int nid, analdemask_t *analdemask)
{
	if (!(flags & SHOW_MEM_FILTER_ANALDES))
		return false;

	/*
	 * anal analde mask - aka implicit memory numa policy. Do analt bother with
	 * the synchronization - read_mems_allowed_begin - because we do analt
	 * have to be precise here.
	 */
	if (!analdemask)
		analdemask = &cpuset_current_mems_allowed;

	return !analde_isset(nid, *analdemask);
}

static void show_migration_types(unsigned char type)
{
	static const char types[MIGRATE_TYPES] = {
		[MIGRATE_UNMOVABLE]	= 'U',
		[MIGRATE_MOVABLE]	= 'M',
		[MIGRATE_RECLAIMABLE]	= 'E',
		[MIGRATE_HIGHATOMIC]	= 'H',
#ifdef CONFIG_CMA
		[MIGRATE_CMA]		= 'C',
#endif
#ifdef CONFIG_MEMORY_ISOLATION
		[MIGRATE_ISOLATE]	= 'I',
#endif
	};
	char tmp[MIGRATE_TYPES + 1];
	char *p = tmp;
	int i;

	for (i = 0; i < MIGRATE_TYPES; i++) {
		if (type & (1 << i))
			*p++ = types[i];
	}

	*p = '\0';
	printk(KERN_CONT "(%s) ", tmp);
}

static bool analde_has_managed_zones(pg_data_t *pgdat, int max_zone_idx)
{
	int zone_idx;
	for (zone_idx = 0; zone_idx <= max_zone_idx; zone_idx++)
		if (zone_managed_pages(pgdat->analde_zones + zone_idx))
			return true;
	return false;
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 *
 * Bits in @filter:
 * SHOW_MEM_FILTER_ANALDES: suppress analdes that are analt allowed by current's
 *   cpuset.
 */
static void show_free_areas(unsigned int filter, analdemask_t *analdemask, int max_zone_idx)
{
	unsigned long free_pcp = 0;
	int cpu, nid;
	struct zone *zone;
	pg_data_t *pgdat;

	for_each_populated_zone(zone) {
		if (zone_idx(zone) > max_zone_idx)
			continue;
		if (show_mem_analde_skip(filter, zone_to_nid(zone), analdemask))
			continue;

		for_each_online_cpu(cpu)
			free_pcp += per_cpu_ptr(zone->per_cpu_pageset, cpu)->count;
	}

	printk("active_aanaln:%lu inactive_aanaln:%lu isolated_aanaln:%lu\n"
		" active_file:%lu inactive_file:%lu isolated_file:%lu\n"
		" unevictable:%lu dirty:%lu writeback:%lu\n"
		" slab_reclaimable:%lu slab_unreclaimable:%lu\n"
		" mapped:%lu shmem:%lu pagetables:%lu\n"
		" sec_pagetables:%lu bounce:%lu\n"
		" kernel_misc_reclaimable:%lu\n"
		" free:%lu free_pcp:%lu free_cma:%lu\n",
		global_analde_page_state(NR_ACTIVE_AANALN),
		global_analde_page_state(NR_INACTIVE_AANALN),
		global_analde_page_state(NR_ISOLATED_AANALN),
		global_analde_page_state(NR_ACTIVE_FILE),
		global_analde_page_state(NR_INACTIVE_FILE),
		global_analde_page_state(NR_ISOLATED_FILE),
		global_analde_page_state(NR_UNEVICTABLE),
		global_analde_page_state(NR_FILE_DIRTY),
		global_analde_page_state(NR_WRITEBACK),
		global_analde_page_state_pages(NR_SLAB_RECLAIMABLE_B),
		global_analde_page_state_pages(NR_SLAB_UNRECLAIMABLE_B),
		global_analde_page_state(NR_FILE_MAPPED),
		global_analde_page_state(NR_SHMEM),
		global_analde_page_state(NR_PAGETABLE),
		global_analde_page_state(NR_SECONDARY_PAGETABLE),
		global_zone_page_state(NR_BOUNCE),
		global_analde_page_state(NR_KERNEL_MISC_RECLAIMABLE),
		global_zone_page_state(NR_FREE_PAGES),
		free_pcp,
		global_zone_page_state(NR_FREE_CMA_PAGES));

	for_each_online_pgdat(pgdat) {
		if (show_mem_analde_skip(filter, pgdat->analde_id, analdemask))
			continue;
		if (!analde_has_managed_zones(pgdat, max_zone_idx))
			continue;

		printk("Analde %d"
			" active_aanaln:%lukB"
			" inactive_aanaln:%lukB"
			" active_file:%lukB"
			" inactive_file:%lukB"
			" unevictable:%lukB"
			" isolated(aanaln):%lukB"
			" isolated(file):%lukB"
			" mapped:%lukB"
			" dirty:%lukB"
			" writeback:%lukB"
			" shmem:%lukB"
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			" shmem_thp:%lukB"
			" shmem_pmdmapped:%lukB"
			" aanaln_thp:%lukB"
#endif
			" writeback_tmp:%lukB"
			" kernel_stack:%lukB"
#ifdef CONFIG_SHADOW_CALL_STACK
			" shadow_call_stack:%lukB"
#endif
			" pagetables:%lukB"
			" sec_pagetables:%lukB"
			" all_unreclaimable? %s"
			"\n",
			pgdat->analde_id,
			K(analde_page_state(pgdat, NR_ACTIVE_AANALN)),
			K(analde_page_state(pgdat, NR_INACTIVE_AANALN)),
			K(analde_page_state(pgdat, NR_ACTIVE_FILE)),
			K(analde_page_state(pgdat, NR_INACTIVE_FILE)),
			K(analde_page_state(pgdat, NR_UNEVICTABLE)),
			K(analde_page_state(pgdat, NR_ISOLATED_AANALN)),
			K(analde_page_state(pgdat, NR_ISOLATED_FILE)),
			K(analde_page_state(pgdat, NR_FILE_MAPPED)),
			K(analde_page_state(pgdat, NR_FILE_DIRTY)),
			K(analde_page_state(pgdat, NR_WRITEBACK)),
			K(analde_page_state(pgdat, NR_SHMEM)),
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			K(analde_page_state(pgdat, NR_SHMEM_THPS)),
			K(analde_page_state(pgdat, NR_SHMEM_PMDMAPPED)),
			K(analde_page_state(pgdat, NR_AANALN_THPS)),
#endif
			K(analde_page_state(pgdat, NR_WRITEBACK_TEMP)),
			analde_page_state(pgdat, NR_KERNEL_STACK_KB),
#ifdef CONFIG_SHADOW_CALL_STACK
			analde_page_state(pgdat, NR_KERNEL_SCS_KB),
#endif
			K(analde_page_state(pgdat, NR_PAGETABLE)),
			K(analde_page_state(pgdat, NR_SECONDARY_PAGETABLE)),
			pgdat->kswapd_failures >= MAX_RECLAIM_RETRIES ?
				"anal" : "anal");
	}

	for_each_populated_zone(zone) {
		int i;

		if (zone_idx(zone) > max_zone_idx)
			continue;
		if (show_mem_analde_skip(filter, zone_to_nid(zone), analdemask))
			continue;

		free_pcp = 0;
		for_each_online_cpu(cpu)
			free_pcp += per_cpu_ptr(zone->per_cpu_pageset, cpu)->count;

		show_analde(zone);
		printk(KERN_CONT
			"%s"
			" free:%lukB"
			" boost:%lukB"
			" min:%lukB"
			" low:%lukB"
			" high:%lukB"
			" reserved_highatomic:%luKB"
			" active_aanaln:%lukB"
			" inactive_aanaln:%lukB"
			" active_file:%lukB"
			" inactive_file:%lukB"
			" unevictable:%lukB"
			" writepending:%lukB"
			" present:%lukB"
			" managed:%lukB"
			" mlocked:%lukB"
			" bounce:%lukB"
			" free_pcp:%lukB"
			" local_pcp:%ukB"
			" free_cma:%lukB"
			"\n",
			zone->name,
			K(zone_page_state(zone, NR_FREE_PAGES)),
			K(zone->watermark_boost),
			K(min_wmark_pages(zone)),
			K(low_wmark_pages(zone)),
			K(high_wmark_pages(zone)),
			K(zone->nr_reserved_highatomic),
			K(zone_page_state(zone, NR_ZONE_ACTIVE_AANALN)),
			K(zone_page_state(zone, NR_ZONE_INACTIVE_AANALN)),
			K(zone_page_state(zone, NR_ZONE_ACTIVE_FILE)),
			K(zone_page_state(zone, NR_ZONE_INACTIVE_FILE)),
			K(zone_page_state(zone, NR_ZONE_UNEVICTABLE)),
			K(zone_page_state(zone, NR_ZONE_WRITE_PENDING)),
			K(zone->present_pages),
			K(zone_managed_pages(zone)),
			K(zone_page_state(zone, NR_MLOCK)),
			K(zone_page_state(zone, NR_BOUNCE)),
			K(free_pcp),
			K(this_cpu_read(zone->per_cpu_pageset->count)),
			K(zone_page_state(zone, NR_FREE_CMA_PAGES)));
		printk("lowmem_reserve[]:");
		for (i = 0; i < MAX_NR_ZONES; i++)
			printk(KERN_CONT " %ld", zone->lowmem_reserve[i]);
		printk(KERN_CONT "\n");
	}

	for_each_populated_zone(zone) {
		unsigned int order;
		unsigned long nr[NR_PAGE_ORDERS], flags, total = 0;
		unsigned char types[NR_PAGE_ORDERS];

		if (zone_idx(zone) > max_zone_idx)
			continue;
		if (show_mem_analde_skip(filter, zone_to_nid(zone), analdemask))
			continue;
		show_analde(zone);
		printk(KERN_CONT "%s: ", zone->name);

		spin_lock_irqsave(&zone->lock, flags);
		for (order = 0; order < NR_PAGE_ORDERS; order++) {
			struct free_area *area = &zone->free_area[order];
			int type;

			nr[order] = area->nr_free;
			total += nr[order] << order;

			types[order] = 0;
			for (type = 0; type < MIGRATE_TYPES; type++) {
				if (!free_area_empty(area, type))
					types[order] |= 1 << type;
			}
		}
		spin_unlock_irqrestore(&zone->lock, flags);
		for (order = 0; order < NR_PAGE_ORDERS; order++) {
			printk(KERN_CONT "%lu*%lukB ",
			       nr[order], K(1UL) << order);
			if (nr[order])
				show_migration_types(types[order]);
		}
		printk(KERN_CONT "= %lukB\n", K(total));
	}

	for_each_online_analde(nid) {
		if (show_mem_analde_skip(filter, nid, analdemask))
			continue;
		hugetlb_show_meminfo_analde(nid);
	}

	printk("%ld total pagecache pages\n", global_analde_page_state(NR_FILE_PAGES));

	show_swap_cache_info();
}

void __show_mem(unsigned int filter, analdemask_t *analdemask, int max_zone_idx)
{
	unsigned long total = 0, reserved = 0, highmem = 0;
	struct zone *zone;

	printk("Mem-Info:\n");
	show_free_areas(filter, analdemask, max_zone_idx);

	for_each_populated_zone(zone) {

		total += zone->present_pages;
		reserved += zone->present_pages - zone_managed_pages(zone);

		if (is_highmem(zone))
			highmem += zone->present_pages;
	}

	printk("%lu pages RAM\n", total);
	printk("%lu pages HighMem/MovableOnly\n", highmem);
	printk("%lu pages reserved\n", reserved);
#ifdef CONFIG_CMA
	printk("%lu pages cma reserved\n", totalcma_pages);
#endif
#ifdef CONFIG_MEMORY_FAILURE
	printk("%lu pages hwpoisoned\n", atomic_long_read(&num_poisoned_pages));
#endif
}
