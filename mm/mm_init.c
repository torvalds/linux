// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm_init.c - Memory initialisation verification and debugging
 *
 * Copyright 2008 IBM Corporation, 2008
 * Author Mel Gorman <mel@csn.ul.ie>
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/export.h>
#include <linux/memory.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/mman.h>
#include <linux/memblock.h>
#include <linux/page-isolation.h>
#include <linux/padata.h>
#include <linux/nmi.h>
#include <linux/buffer_head.h>
#include <linux/kmemleak.h>
#include <linux/kfence.h>
#include <linux/page_ext.h>
#include <linux/pti.h>
#include <linux/pgtable.h>
#include <linux/stackdepot.h>
#include <linux/swap.h>
#include <linux/cma.h>
#include <linux/crash_dump.h>
#include <linux/execmem.h>
#include <linux/vmstat.h>
#include <linux/hugetlb.h>
#include "internal.h"
#include "slab.h"
#include "shuffle.h"

#include <asm/setup.h>

#ifndef CONFIG_NUMA
unsigned long max_mapnr;
EXPORT_SYMBOL(max_mapnr);

struct page *mem_map;
EXPORT_SYMBOL(mem_map);
#endif

/*
 * high_memory defines the upper bound on direct map memory, then end
 * of ZONE_NORMAL.
 */
void *high_memory;
EXPORT_SYMBOL(high_memory);

#ifdef CONFIG_DEBUG_MEMORY_INIT
int __meminitdata mminit_loglevel;

/* The zonelists are simply reported, validation is manual. */
void __init mminit_verify_zonelist(void)
{
	int nid;

	if (mminit_loglevel < MMINIT_VERIFY)
		return;

	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		struct zone *zone;
		struct zoneref *z;
		struct zonelist *zonelist;
		int i, listid, zoneid;

		for (i = 0; i < MAX_ZONELISTS * MAX_NR_ZONES; i++) {

			/* Identify the zone and nodelist */
			zoneid = i % MAX_NR_ZONES;
			listid = i / MAX_NR_ZONES;
			zonelist = &pgdat->node_zonelists[listid];
			zone = &pgdat->node_zones[zoneid];
			if (!populated_zone(zone))
				continue;

			/* Print information about the zonelist */
			printk(KERN_DEBUG "mminit::zonelist %s %d:%s = ",
				listid > 0 ? "thisnode" : "general", nid,
				zone->name);

			/* Iterate the zonelist */
			for_each_zone_zonelist(zone, z, zonelist, zoneid)
				pr_cont("%d:%s ", zone_to_nid(zone), zone->name);
			pr_cont("\n");
		}
	}
}

void __init mminit_verify_pageflags_layout(void)
{
	int shift, width;
	unsigned long or_mask, add_mask;

	shift = BITS_PER_LONG;
	width = shift - NR_NON_PAGEFLAG_BITS;
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_widths",
		"Section %d Node %d Zone %d Lastcpupid %d Kasantag %d Gen %d Tier %d Flags %d\n",
		SECTIONS_WIDTH,
		NODES_WIDTH,
		ZONES_WIDTH,
		LAST_CPUPID_WIDTH,
		KASAN_TAG_WIDTH,
		LRU_GEN_WIDTH,
		LRU_REFS_WIDTH,
		NR_PAGEFLAGS);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_shifts",
		"Section %d Node %d Zone %d Lastcpupid %d Kasantag %d\n",
		SECTIONS_SHIFT,
		NODES_SHIFT,
		ZONES_SHIFT,
		LAST_CPUPID_SHIFT,
		KASAN_TAG_WIDTH);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_pgshifts",
		"Section %lu Node %lu Zone %lu Lastcpupid %lu Kasantag %lu\n",
		(unsigned long)SECTIONS_PGSHIFT,
		(unsigned long)NODES_PGSHIFT,
		(unsigned long)ZONES_PGSHIFT,
		(unsigned long)LAST_CPUPID_PGSHIFT,
		(unsigned long)KASAN_TAG_PGSHIFT);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_nodezoneid",
		"Node/Zone ID: %lu -> %lu\n",
		(unsigned long)(ZONEID_PGOFF + ZONEID_SHIFT),
		(unsigned long)ZONEID_PGOFF);
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_usage",
		"location: %d -> %d layout %d -> %d unused %d -> %d page-flags\n",
		shift, width, width, NR_PAGEFLAGS, NR_PAGEFLAGS, 0);
#ifdef NODE_NOT_IN_PAGE_FLAGS
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_nodeflags",
		"Node not in page flags");
#endif
#ifdef LAST_CPUPID_NOT_IN_PAGE_FLAGS
	mminit_dprintk(MMINIT_TRACE, "pageflags_layout_nodeflags",
		"Last cpupid not in page flags");
#endif

	if (SECTIONS_WIDTH) {
		shift -= SECTIONS_WIDTH;
		BUG_ON(shift != SECTIONS_PGSHIFT);
	}
	if (NODES_WIDTH) {
		shift -= NODES_WIDTH;
		BUG_ON(shift != NODES_PGSHIFT);
	}
	if (ZONES_WIDTH) {
		shift -= ZONES_WIDTH;
		BUG_ON(shift != ZONES_PGSHIFT);
	}

	/* Check for bitmask overlaps */
	or_mask = (ZONES_MASK << ZONES_PGSHIFT) |
			(NODES_MASK << NODES_PGSHIFT) |
			(SECTIONS_MASK << SECTIONS_PGSHIFT);
	add_mask = (ZONES_MASK << ZONES_PGSHIFT) +
			(NODES_MASK << NODES_PGSHIFT) +
			(SECTIONS_MASK << SECTIONS_PGSHIFT);
	BUG_ON(or_mask != add_mask);
}

static __init int set_mminit_loglevel(char *str)
{
	get_option(&str, &mminit_loglevel);
	return 0;
}
early_param("mminit_loglevel", set_mminit_loglevel);
#endif /* CONFIG_DEBUG_MEMORY_INIT */

struct kobject *mm_kobj;

#ifdef CONFIG_SMP
s32 vm_committed_as_batch = 32;

void mm_compute_batch(int overcommit_policy)
{
	u64 memsized_batch;
	s32 nr = num_present_cpus();
	s32 batch = max_t(s32, nr*2, 32);
	unsigned long ram_pages = totalram_pages();

	/*
	 * For policy OVERCOMMIT_NEVER, set batch size to 0.4% of
	 * (total memory/#cpus), and lift it to 25% for other policies
	 * to easy the possible lock contention for percpu_counter
	 * vm_committed_as, while the max limit is INT_MAX
	 */
	if (overcommit_policy == OVERCOMMIT_NEVER)
		memsized_batch = min_t(u64, ram_pages/nr/256, INT_MAX);
	else
		memsized_batch = min_t(u64, ram_pages/nr/4, INT_MAX);

	vm_committed_as_batch = max_t(s32, memsized_batch, batch);
}

static int __meminit mm_compute_batch_notifier(struct notifier_block *self,
					unsigned long action, void *arg)
{
	switch (action) {
	case MEM_ONLINE:
	case MEM_OFFLINE:
		mm_compute_batch(sysctl_overcommit_memory);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int __init mm_compute_batch_init(void)
{
	mm_compute_batch(sysctl_overcommit_memory);
	hotplug_memory_notifier(mm_compute_batch_notifier, MM_COMPUTE_BATCH_PRI);
	return 0;
}

__initcall(mm_compute_batch_init);

#endif

static int __init mm_sysfs_init(void)
{
	mm_kobj = kobject_create_and_add("mm", kernel_kobj);
	if (!mm_kobj)
		return -ENOMEM;

	return 0;
}
postcore_initcall(mm_sysfs_init);

static unsigned long arch_zone_lowest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long arch_zone_highest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long zone_movable_pfn[MAX_NUMNODES] __initdata;

static unsigned long required_kernelcore __initdata;
static unsigned long required_kernelcore_percent __initdata;
static unsigned long required_movablecore __initdata;
static unsigned long required_movablecore_percent __initdata;

static unsigned long nr_kernel_pages __initdata;
static unsigned long nr_all_pages __initdata;

static bool deferred_struct_pages __meminitdata;

static DEFINE_PER_CPU(struct per_cpu_nodestat, boot_nodestats);

static int __init cmdline_parse_core(char *p, unsigned long *core,
				     unsigned long *percent)
{
	unsigned long long coremem;
	char *endptr;

	if (!p)
		return -EINVAL;

	/* Value may be a percentage of total memory, otherwise bytes */
	coremem = simple_strtoull(p, &endptr, 0);
	if (*endptr == '%') {
		/* Paranoid check for percent values greater than 100 */
		WARN_ON(coremem > 100);

		*percent = coremem;
	} else {
		coremem = memparse(p, &p);
		/* Paranoid check that UL is enough for the coremem value */
		WARN_ON((coremem >> PAGE_SHIFT) > ULONG_MAX);

		*core = coremem >> PAGE_SHIFT;
		*percent = 0UL;
	}
	return 0;
}

bool mirrored_kernelcore __initdata_memblock;

/*
 * kernelcore=size sets the amount of memory for use for allocations that
 * cannot be reclaimed or migrated.
 */
static int __init cmdline_parse_kernelcore(char *p)
{
	/* parse kernelcore=mirror */
	if (parse_option_str(p, "mirror")) {
		mirrored_kernelcore = true;
		return 0;
	}

	return cmdline_parse_core(p, &required_kernelcore,
				  &required_kernelcore_percent);
}
early_param("kernelcore", cmdline_parse_kernelcore);

/*
 * movablecore=size sets the amount of memory for use for allocations that
 * can be reclaimed or migrated.
 */
static int __init cmdline_parse_movablecore(char *p)
{
	return cmdline_parse_core(p, &required_movablecore,
				  &required_movablecore_percent);
}
early_param("movablecore", cmdline_parse_movablecore);

/*
 * early_calculate_totalpages()
 * Sum pages in active regions for movable zone.
 * Populate N_MEMORY for calculating usable_nodes.
 */
static unsigned long __init early_calculate_totalpages(void)
{
	unsigned long totalpages = 0;
	unsigned long start_pfn, end_pfn;
	int i, nid;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		unsigned long pages = end_pfn - start_pfn;

		totalpages += pages;
		if (pages)
			node_set_state(nid, N_MEMORY);
	}
	return totalpages;
}

/*
 * This finds a zone that can be used for ZONE_MOVABLE pages. The
 * assumption is made that zones within a node are ordered in monotonic
 * increasing memory addresses so that the "highest" populated zone is used
 */
static void __init find_usable_zone_for_movable(void)
{
	int zone_index;
	for (zone_index = MAX_NR_ZONES - 1; zone_index >= 0; zone_index--) {
		if (zone_index == ZONE_MOVABLE)
			continue;

		if (arch_zone_highest_possible_pfn[zone_index] >
				arch_zone_lowest_possible_pfn[zone_index])
			break;
	}

	VM_BUG_ON(zone_index == -1);
	movable_zone = zone_index;
}

/*
 * Find the PFN the Movable zone begins in each node. Kernel memory
 * is spread evenly between nodes as long as the nodes have enough
 * memory. When they don't, some nodes will have more kernelcore than
 * others
 */
static void __init find_zone_movable_pfns_for_nodes(void)
{
	int i, nid;
	unsigned long usable_startpfn;
	unsigned long kernelcore_node, kernelcore_remaining;
	/* save the state before borrow the nodemask */
	nodemask_t saved_node_state = node_states[N_MEMORY];
	unsigned long totalpages = early_calculate_totalpages();
	int usable_nodes = nodes_weight(node_states[N_MEMORY]);
	struct memblock_region *r;

	/* Need to find movable_zone earlier when movable_node is specified. */
	find_usable_zone_for_movable();

	/*
	 * If movable_node is specified, ignore kernelcore and movablecore
	 * options.
	 */
	if (movable_node_is_enabled()) {
		for_each_mem_region(r) {
			if (!memblock_is_hotpluggable(r))
				continue;

			nid = memblock_get_region_node(r);

			usable_startpfn = memblock_region_memory_base_pfn(r);
			zone_movable_pfn[nid] = zone_movable_pfn[nid] ?
				min(usable_startpfn, zone_movable_pfn[nid]) :
				usable_startpfn;
		}

		goto out2;
	}

	/*
	 * If kernelcore=mirror is specified, ignore movablecore option
	 */
	if (mirrored_kernelcore) {
		bool mem_below_4gb_not_mirrored = false;

		if (!memblock_has_mirror()) {
			pr_warn("The system has no mirror memory, ignore kernelcore=mirror.\n");
			goto out;
		}

		if (is_kdump_kernel()) {
			pr_warn("The system is under kdump, ignore kernelcore=mirror.\n");
			goto out;
		}

		for_each_mem_region(r) {
			if (memblock_is_mirror(r))
				continue;

			nid = memblock_get_region_node(r);

			usable_startpfn = memblock_region_memory_base_pfn(r);

			if (usable_startpfn < PHYS_PFN(SZ_4G)) {
				mem_below_4gb_not_mirrored = true;
				continue;
			}

			zone_movable_pfn[nid] = zone_movable_pfn[nid] ?
				min(usable_startpfn, zone_movable_pfn[nid]) :
				usable_startpfn;
		}

		if (mem_below_4gb_not_mirrored)
			pr_warn("This configuration results in unmirrored kernel memory.\n");

		goto out2;
	}

	/*
	 * If kernelcore=nn% or movablecore=nn% was specified, calculate the
	 * amount of necessary memory.
	 */
	if (required_kernelcore_percent)
		required_kernelcore = (totalpages * 100 * required_kernelcore_percent) /
				       10000UL;
	if (required_movablecore_percent)
		required_movablecore = (totalpages * 100 * required_movablecore_percent) /
					10000UL;

	/*
	 * If movablecore= was specified, calculate what size of
	 * kernelcore that corresponds so that memory usable for
	 * any allocation type is evenly spread. If both kernelcore
	 * and movablecore are specified, then the value of kernelcore
	 * will be used for required_kernelcore if it's greater than
	 * what movablecore would have allowed.
	 */
	if (required_movablecore) {
		unsigned long corepages;

		/*
		 * Round-up so that ZONE_MOVABLE is at least as large as what
		 * was requested by the user
		 */
		required_movablecore =
			round_up(required_movablecore, MAX_ORDER_NR_PAGES);
		required_movablecore = min(totalpages, required_movablecore);
		corepages = totalpages - required_movablecore;

		required_kernelcore = max(required_kernelcore, corepages);
	}

	/*
	 * If kernelcore was not specified or kernelcore size is larger
	 * than totalpages, there is no ZONE_MOVABLE.
	 */
	if (!required_kernelcore || required_kernelcore >= totalpages)
		goto out;

	/* usable_startpfn is the lowest possible pfn ZONE_MOVABLE can be at */
	usable_startpfn = arch_zone_lowest_possible_pfn[movable_zone];

restart:
	/* Spread kernelcore memory as evenly as possible throughout nodes */
	kernelcore_node = required_kernelcore / usable_nodes;
	for_each_node_state(nid, N_MEMORY) {
		unsigned long start_pfn, end_pfn;

		/*
		 * Recalculate kernelcore_node if the division per node
		 * now exceeds what is necessary to satisfy the requested
		 * amount of memory for the kernel
		 */
		if (required_kernelcore < kernelcore_node)
			kernelcore_node = required_kernelcore / usable_nodes;

		/*
		 * As the map is walked, we track how much memory is usable
		 * by the kernel using kernelcore_remaining. When it is
		 * 0, the rest of the node is usable by ZONE_MOVABLE
		 */
		kernelcore_remaining = kernelcore_node;

		/* Go through each range of PFNs within this node */
		for_each_mem_pfn_range(i, nid, &start_pfn, &end_pfn, NULL) {
			unsigned long size_pages;

			start_pfn = max(start_pfn, zone_movable_pfn[nid]);
			if (start_pfn >= end_pfn)
				continue;

			/* Account for what is only usable for kernelcore */
			if (start_pfn < usable_startpfn) {
				unsigned long kernel_pages;
				kernel_pages = min(end_pfn, usable_startpfn)
								- start_pfn;

				kernelcore_remaining -= min(kernel_pages,
							kernelcore_remaining);
				required_kernelcore -= min(kernel_pages,
							required_kernelcore);

				/* Continue if range is now fully accounted */
				if (end_pfn <= usable_startpfn) {

					/*
					 * Push zone_movable_pfn to the end so
					 * that if we have to rebalance
					 * kernelcore across nodes, we will
					 * not double account here
					 */
					zone_movable_pfn[nid] = end_pfn;
					continue;
				}
				start_pfn = usable_startpfn;
			}

			/*
			 * The usable PFN range for ZONE_MOVABLE is from
			 * start_pfn->end_pfn. Calculate size_pages as the
			 * number of pages used as kernelcore
			 */
			size_pages = end_pfn - start_pfn;
			if (size_pages > kernelcore_remaining)
				size_pages = kernelcore_remaining;
			zone_movable_pfn[nid] = start_pfn + size_pages;

			/*
			 * Some kernelcore has been met, update counts and
			 * break if the kernelcore for this node has been
			 * satisfied
			 */
			required_kernelcore -= min(required_kernelcore,
								size_pages);
			kernelcore_remaining -= size_pages;
			if (!kernelcore_remaining)
				break;
		}
	}

	/*
	 * If there is still required_kernelcore, we do another pass with one
	 * less node in the count. This will push zone_movable_pfn[nid] further
	 * along on the nodes that still have memory until kernelcore is
	 * satisfied
	 */
	usable_nodes--;
	if (usable_nodes && required_kernelcore > usable_nodes)
		goto restart;

out2:
	/* Align start of ZONE_MOVABLE on all nids to MAX_ORDER_NR_PAGES */
	for_each_node_state(nid, N_MEMORY) {
		unsigned long start_pfn, end_pfn;

		zone_movable_pfn[nid] =
			round_up(zone_movable_pfn[nid], MAX_ORDER_NR_PAGES);

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		if (zone_movable_pfn[nid] >= end_pfn)
			zone_movable_pfn[nid] = 0;
	}

out:
	/* restore the node_state */
	node_states[N_MEMORY] = saved_node_state;
}

void __meminit __init_single_page(struct page *page, unsigned long pfn,
				unsigned long zone, int nid)
{
	mm_zero_struct_page(page);
	set_page_links(page, zone, nid, pfn);
	init_page_count(page);
	atomic_set(&page->_mapcount, -1);
	page_cpupid_reset_last(page);
	page_kasan_tag_reset(page);

	INIT_LIST_HEAD(&page->lru);
#ifdef WANT_PAGE_VIRTUAL
	/* The shift won't overflow because ZONE_NORMAL is below 4G. */
	if (!is_highmem_idx(zone))
		set_page_address(page, __va(pfn << PAGE_SHIFT));
#endif
}

#ifdef CONFIG_NUMA
/*
 * During memory init memblocks map pfns to nids. The search is expensive and
 * this caches recent lookups. The implementation of __early_pfn_to_nid
 * treats start/end as pfns.
 */
struct mminit_pfnnid_cache {
	unsigned long last_start;
	unsigned long last_end;
	int last_nid;
};

static struct mminit_pfnnid_cache early_pfnnid_cache __meminitdata;

/*
 * Required by SPARSEMEM. Given a PFN, return what node the PFN is on.
 */
static int __meminit __early_pfn_to_nid(unsigned long pfn,
					struct mminit_pfnnid_cache *state)
{
	unsigned long start_pfn, end_pfn;
	int nid;

	if (state->last_start <= pfn && pfn < state->last_end)
		return state->last_nid;

	nid = memblock_search_pfn_nid(pfn, &start_pfn, &end_pfn);
	if (nid != NUMA_NO_NODE) {
		state->last_start = start_pfn;
		state->last_end = end_pfn;
		state->last_nid = nid;
	}

	return nid;
}

int __meminit early_pfn_to_nid(unsigned long pfn)
{
	static DEFINE_SPINLOCK(early_pfn_lock);
	int nid;

	spin_lock(&early_pfn_lock);
	nid = __early_pfn_to_nid(pfn, &early_pfnnid_cache);
	if (nid < 0)
		nid = first_online_node;
	spin_unlock(&early_pfn_lock);

	return nid;
}

int hashdist = HASHDIST_DEFAULT;

static int __init set_hashdist(char *str)
{
	if (!str)
		return 0;
	hashdist = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("hashdist=", set_hashdist);

static inline void fixup_hashdist(void)
{
	if (num_node_state(N_MEMORY) == 1)
		hashdist = 0;
}
#else
static inline void fixup_hashdist(void) {}
#endif /* CONFIG_NUMA */

/*
 * Initialize a reserved page unconditionally, finding its zone first.
 */
void __meminit __init_page_from_nid(unsigned long pfn, int nid)
{
	pg_data_t *pgdat;
	int zid;

	pgdat = NODE_DATA(nid);

	for (zid = 0; zid < MAX_NR_ZONES; zid++) {
		struct zone *zone = &pgdat->node_zones[zid];

		if (zone_spans_pfn(zone, pfn))
			break;
	}
	__init_single_page(pfn_to_page(pfn), pfn, zid, nid);

	if (pageblock_aligned(pfn))
		set_pageblock_migratetype(pfn_to_page(pfn), MIGRATE_MOVABLE);
}

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
static inline void pgdat_set_deferred_range(pg_data_t *pgdat)
{
	pgdat->first_deferred_pfn = ULONG_MAX;
}

/* Returns true if the struct page for the pfn is initialised */
static inline bool __meminit early_page_initialised(unsigned long pfn, int nid)
{
	if (node_online(nid) && pfn >= NODE_DATA(nid)->first_deferred_pfn)
		return false;

	return true;
}

/*
 * Returns true when the remaining initialisation should be deferred until
 * later in the boot cycle when it can be parallelised.
 */
static bool __meminit
defer_init(int nid, unsigned long pfn, unsigned long end_pfn)
{
	static unsigned long prev_end_pfn, nr_initialised;

	if (early_page_ext_enabled())
		return false;

	/* Always populate low zones for address-constrained allocations */
	if (end_pfn < pgdat_end_pfn(NODE_DATA(nid)))
		return false;

	if (NODE_DATA(nid)->first_deferred_pfn != ULONG_MAX)
		return true;

	/*
	 * prev_end_pfn static that contains the end of previous zone
	 * No need to protect because called very early in boot before smp_init.
	 */
	if (prev_end_pfn != end_pfn) {
		prev_end_pfn = end_pfn;
		nr_initialised = 0;
	}

	/*
	 * We start only with one section of pages, more pages are added as
	 * needed until the rest of deferred pages are initialized.
	 */
	nr_initialised++;
	if ((nr_initialised > PAGES_PER_SECTION) &&
	    (pfn & (PAGES_PER_SECTION - 1)) == 0) {
		NODE_DATA(nid)->first_deferred_pfn = pfn;
		return true;
	}
	return false;
}

static void __meminit init_deferred_page(unsigned long pfn, int nid)
{
	if (early_page_initialised(pfn, nid))
		return;

	__init_page_from_nid(pfn, nid);
}
#else
static inline void pgdat_set_deferred_range(pg_data_t *pgdat) {}

static inline bool early_page_initialised(unsigned long pfn, int nid)
{
	return true;
}

static inline bool defer_init(int nid, unsigned long pfn, unsigned long end_pfn)
{
	return false;
}

static inline void init_deferred_page(unsigned long pfn, int nid)
{
}
#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

/*
 * Initialised pages do not have PageReserved set. This function is
 * called for each range allocated by the bootmem allocator and
 * marks the pages PageReserved. The remaining valid pages are later
 * sent to the buddy page allocator.
 */
void __meminit reserve_bootmem_region(phys_addr_t start,
				      phys_addr_t end, int nid)
{
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long end_pfn = PFN_UP(end);

	for (; start_pfn < end_pfn; start_pfn++) {
		if (pfn_valid(start_pfn)) {
			struct page *page = pfn_to_page(start_pfn);

			init_deferred_page(start_pfn, nid);

			/*
			 * no need for atomic set_bit because the struct
			 * page is not visible yet so nobody should
			 * access it yet.
			 */
			__SetPageReserved(page);
		}
	}
}

/* If zone is ZONE_MOVABLE but memory is mirrored, it is an overlapped init */
static bool __meminit
overlap_memmap_init(unsigned long zone, unsigned long *pfn)
{
	static struct memblock_region *r;

	if (mirrored_kernelcore && zone == ZONE_MOVABLE) {
		if (!r || *pfn >= memblock_region_memory_end_pfn(r)) {
			for_each_mem_region(r) {
				if (*pfn < memblock_region_memory_end_pfn(r))
					break;
			}
		}
		if (*pfn >= memblock_region_memory_base_pfn(r) &&
		    memblock_is_mirror(r)) {
			*pfn = memblock_region_memory_end_pfn(r);
			return true;
		}
	}
	return false;
}

/*
 * Only struct pages that correspond to ranges defined by memblock.memory
 * are zeroed and initialized by going through __init_single_page() during
 * memmap_init_zone_range().
 *
 * But, there could be struct pages that correspond to holes in
 * memblock.memory. This can happen because of the following reasons:
 * - physical memory bank size is not necessarily the exact multiple of the
 *   arbitrary section size
 * - early reserved memory may not be listed in memblock.memory
 * - non-memory regions covered by the contigious flatmem mapping
 * - memory layouts defined with memmap= kernel parameter may not align
 *   nicely with memmap sections
 *
 * Explicitly initialize those struct pages so that:
 * - PG_Reserved is set
 * - zone and node links point to zone and node that span the page if the
 *   hole is in the middle of a zone
 * - zone and node links point to adjacent zone/node if the hole falls on
 *   the zone boundary; the pages in such holes will be prepended to the
 *   zone/node above the hole except for the trailing pages in the last
 *   section that will be appended to the zone/node below.
 */
static void __init init_unavailable_range(unsigned long spfn,
					  unsigned long epfn,
					  int zone, int node)
{
	unsigned long pfn;
	u64 pgcnt = 0;

	for (pfn = spfn; pfn < epfn; pfn++) {
		if (!pfn_valid(pageblock_start_pfn(pfn))) {
			pfn = pageblock_end_pfn(pfn) - 1;
			continue;
		}
		__init_single_page(pfn_to_page(pfn), pfn, zone, node);
		__SetPageReserved(pfn_to_page(pfn));
		pgcnt++;
	}

	if (pgcnt)
		pr_info("On node %d, zone %s: %lld pages in unavailable ranges\n",
			node, zone_names[zone], pgcnt);
}

/*
 * Initially all pages are reserved - free ones are freed
 * up by memblock_free_all() once the early boot process is
 * done. Non-atomic initialization, single-pass.
 *
 * All aligned pageblocks are initialized to the specified migratetype
 * (usually MIGRATE_MOVABLE). Besides setting the migratetype, no related
 * zone stats (e.g., nr_isolate_pageblock) are touched.
 */
void __meminit memmap_init_range(unsigned long size, int nid, unsigned long zone,
		unsigned long start_pfn, unsigned long zone_end_pfn,
		enum meminit_context context,
		struct vmem_altmap *altmap, int migratetype)
{
	unsigned long pfn, end_pfn = start_pfn + size;
	struct page *page;

	if (highest_memmap_pfn < end_pfn - 1)
		highest_memmap_pfn = end_pfn - 1;

#ifdef CONFIG_ZONE_DEVICE
	/*
	 * Honor reservation requested by the driver for this ZONE_DEVICE
	 * memory. We limit the total number of pages to initialize to just
	 * those that might contain the memory mapping. We will defer the
	 * ZONE_DEVICE page initialization until after we have released
	 * the hotplug lock.
	 */
	if (zone == ZONE_DEVICE) {
		if (!altmap)
			return;

		if (start_pfn == altmap->base_pfn)
			start_pfn += altmap->reserve;
		end_pfn = altmap->base_pfn + vmem_altmap_offset(altmap);
	}
#endif

	for (pfn = start_pfn; pfn < end_pfn; ) {
		/*
		 * There can be holes in boot-time mem_map[]s handed to this
		 * function.  They do not exist on hotplugged memory.
		 */
		if (context == MEMINIT_EARLY) {
			if (overlap_memmap_init(zone, &pfn))
				continue;
			if (defer_init(nid, pfn, zone_end_pfn)) {
				deferred_struct_pages = true;
				break;
			}
		}

		page = pfn_to_page(pfn);
		__init_single_page(page, pfn, zone, nid);
		if (context == MEMINIT_HOTPLUG) {
#ifdef CONFIG_ZONE_DEVICE
			if (zone == ZONE_DEVICE)
				__SetPageReserved(page);
			else
#endif
				__SetPageOffline(page);
		}

		/*
		 * Usually, we want to mark the pageblock MIGRATE_MOVABLE,
		 * such that unmovable allocations won't be scattered all
		 * over the place during system boot.
		 */
		if (pageblock_aligned(pfn)) {
			set_pageblock_migratetype(page, migratetype);
			cond_resched();
		}
		pfn++;
	}
}

static void __init memmap_init_zone_range(struct zone *zone,
					  unsigned long start_pfn,
					  unsigned long end_pfn,
					  unsigned long *hole_pfn)
{
	unsigned long zone_start_pfn = zone->zone_start_pfn;
	unsigned long zone_end_pfn = zone_start_pfn + zone->spanned_pages;
	int nid = zone_to_nid(zone), zone_id = zone_idx(zone);

	start_pfn = clamp(start_pfn, zone_start_pfn, zone_end_pfn);
	end_pfn = clamp(end_pfn, zone_start_pfn, zone_end_pfn);

	if (start_pfn >= end_pfn)
		return;

	memmap_init_range(end_pfn - start_pfn, nid, zone_id, start_pfn,
			  zone_end_pfn, MEMINIT_EARLY, NULL, MIGRATE_MOVABLE);

	if (*hole_pfn < start_pfn)
		init_unavailable_range(*hole_pfn, start_pfn, zone_id, nid);

	*hole_pfn = end_pfn;
}

static void __init memmap_init(void)
{
	unsigned long start_pfn, end_pfn;
	unsigned long hole_pfn = 0;
	int i, j, zone_id = 0, nid;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		struct pglist_data *node = NODE_DATA(nid);

		for (j = 0; j < MAX_NR_ZONES; j++) {
			struct zone *zone = node->node_zones + j;

			if (!populated_zone(zone))
				continue;

			memmap_init_zone_range(zone, start_pfn, end_pfn,
					       &hole_pfn);
			zone_id = j;
		}
	}

	/*
	 * Initialize the memory map for hole in the range [memory_end,
	 * section_end] for SPARSEMEM and in the range [memory_end, memmap_end]
	 * for FLATMEM.
	 * Append the pages in this hole to the highest zone in the last
	 * node.
	 */
#ifdef CONFIG_SPARSEMEM
	end_pfn = round_up(end_pfn, PAGES_PER_SECTION);
#else
	end_pfn = round_up(end_pfn, MAX_ORDER_NR_PAGES);
#endif
	if (hole_pfn < end_pfn)
		init_unavailable_range(hole_pfn, end_pfn, zone_id, nid);
}

#ifdef CONFIG_ZONE_DEVICE
static void __ref __init_zone_device_page(struct page *page, unsigned long pfn,
					  unsigned long zone_idx, int nid,
					  struct dev_pagemap *pgmap)
{

	__init_single_page(page, pfn, zone_idx, nid);

	/*
	 * Mark page reserved as it will need to wait for onlining
	 * phase for it to be fully associated with a zone.
	 *
	 * We can use the non-atomic __set_bit operation for setting
	 * the flag as we are still initializing the pages.
	 */
	__SetPageReserved(page);

	/*
	 * ZONE_DEVICE pages union ->lru with a ->pgmap back pointer
	 * and zone_device_data.  It is a bug if a ZONE_DEVICE page is
	 * ever freed or placed on a driver-private list.
	 */
	page_folio(page)->pgmap = pgmap;
	page->zone_device_data = NULL;

	/*
	 * Mark the block movable so that blocks are reserved for
	 * movable at startup. This will force kernel allocations
	 * to reserve their blocks rather than leaking throughout
	 * the address space during boot when many long-lived
	 * kernel allocations are made.
	 *
	 * Please note that MEMINIT_HOTPLUG path doesn't clear memmap
	 * because this is done early in section_activate()
	 */
	if (pageblock_aligned(pfn)) {
		set_pageblock_migratetype(page, MIGRATE_MOVABLE);
		cond_resched();
	}

	/*
	 * ZONE_DEVICE pages other than MEMORY_TYPE_GENERIC are released
	 * directly to the driver page allocator which will set the page count
	 * to 1 when allocating the page.
	 *
	 * MEMORY_TYPE_GENERIC and MEMORY_TYPE_FS_DAX pages automatically have
	 * their refcount reset to one whenever they are freed (ie. after
	 * their refcount drops to 0).
	 */
	switch (pgmap->type) {
	case MEMORY_DEVICE_FS_DAX:
	case MEMORY_DEVICE_PRIVATE:
	case MEMORY_DEVICE_COHERENT:
	case MEMORY_DEVICE_PCI_P2PDMA:
		set_page_count(page, 0);
		break;

	case MEMORY_DEVICE_GENERIC:
		break;
	}
}

/*
 * With compound page geometry and when struct pages are stored in ram most
 * tail pages are reused. Consequently, the amount of unique struct pages to
 * initialize is a lot smaller that the total amount of struct pages being
 * mapped. This is a paired / mild layering violation with explicit knowledge
 * of how the sparse_vmemmap internals handle compound pages in the lack
 * of an altmap. See vmemmap_populate_compound_pages().
 */
static inline unsigned long compound_nr_pages(struct vmem_altmap *altmap,
					      struct dev_pagemap *pgmap)
{
	if (!vmemmap_can_optimize(altmap, pgmap))
		return pgmap_vmemmap_nr(pgmap);

	return VMEMMAP_RESERVE_NR * (PAGE_SIZE / sizeof(struct page));
}

static void __ref memmap_init_compound(struct page *head,
				       unsigned long head_pfn,
				       unsigned long zone_idx, int nid,
				       struct dev_pagemap *pgmap,
				       unsigned long nr_pages)
{
	unsigned long pfn, end_pfn = head_pfn + nr_pages;
	unsigned int order = pgmap->vmemmap_shift;

	__SetPageHead(head);
	for (pfn = head_pfn + 1; pfn < end_pfn; pfn++) {
		struct page *page = pfn_to_page(pfn);

		__init_zone_device_page(page, pfn, zone_idx, nid, pgmap);
		prep_compound_tail(head, pfn - head_pfn);
		set_page_count(page, 0);

		/*
		 * The first tail page stores important compound page info.
		 * Call prep_compound_head() after the first tail page has
		 * been initialized, to not have the data overwritten.
		 */
		if (pfn == head_pfn + 1)
			prep_compound_head(head, order);
	}
}

void __ref memmap_init_zone_device(struct zone *zone,
				   unsigned long start_pfn,
				   unsigned long nr_pages,
				   struct dev_pagemap *pgmap)
{
	unsigned long pfn, end_pfn = start_pfn + nr_pages;
	struct pglist_data *pgdat = zone->zone_pgdat;
	struct vmem_altmap *altmap = pgmap_altmap(pgmap);
	unsigned int pfns_per_compound = pgmap_vmemmap_nr(pgmap);
	unsigned long zone_idx = zone_idx(zone);
	unsigned long start = jiffies;
	int nid = pgdat->node_id;

	if (WARN_ON_ONCE(!pgmap || zone_idx != ZONE_DEVICE))
		return;

	/*
	 * The call to memmap_init should have already taken care
	 * of the pages reserved for the memmap, so we can just jump to
	 * the end of that region and start processing the device pages.
	 */
	if (altmap) {
		start_pfn = altmap->base_pfn + vmem_altmap_offset(altmap);
		nr_pages = end_pfn - start_pfn;
	}

	for (pfn = start_pfn; pfn < end_pfn; pfn += pfns_per_compound) {
		struct page *page = pfn_to_page(pfn);

		__init_zone_device_page(page, pfn, zone_idx, nid, pgmap);

		if (pfns_per_compound == 1)
			continue;

		memmap_init_compound(page, pfn, zone_idx, nid, pgmap,
				     compound_nr_pages(altmap, pgmap));
	}

	pr_debug("%s initialised %lu pages in %ums\n", __func__,
		nr_pages, jiffies_to_msecs(jiffies - start));
}
#endif

/*
 * The zone ranges provided by the architecture do not include ZONE_MOVABLE
 * because it is sized independent of architecture. Unlike the other zones,
 * the starting point for ZONE_MOVABLE is not fixed. It may be different
 * in each node depending on the size of each node and how evenly kernelcore
 * is distributed. This helper function adjusts the zone ranges
 * provided by the architecture for a given node by using the end of the
 * highest usable zone for ZONE_MOVABLE. This preserves the assumption that
 * zones within a node are in order of monotonic increases memory addresses
 */
static void __init adjust_zone_range_for_zone_movable(int nid,
					unsigned long zone_type,
					unsigned long node_end_pfn,
					unsigned long *zone_start_pfn,
					unsigned long *zone_end_pfn)
{
	/* Only adjust if ZONE_MOVABLE is on this node */
	if (zone_movable_pfn[nid]) {
		/* Size ZONE_MOVABLE */
		if (zone_type == ZONE_MOVABLE) {
			*zone_start_pfn = zone_movable_pfn[nid];
			*zone_end_pfn = min(node_end_pfn,
				arch_zone_highest_possible_pfn[movable_zone]);

		/* Adjust for ZONE_MOVABLE starting within this range */
		} else if (!mirrored_kernelcore &&
			*zone_start_pfn < zone_movable_pfn[nid] &&
			*zone_end_pfn > zone_movable_pfn[nid]) {
			*zone_end_pfn = zone_movable_pfn[nid];

		/* Check if this whole range is within ZONE_MOVABLE */
		} else if (*zone_start_pfn >= zone_movable_pfn[nid])
			*zone_start_pfn = *zone_end_pfn;
	}
}

/*
 * Return the number of holes in a range on a node. If nid is MAX_NUMNODES,
 * then all holes in the requested range will be accounted for.
 */
static unsigned long __init __absent_pages_in_range(int nid,
				unsigned long range_start_pfn,
				unsigned long range_end_pfn)
{
	unsigned long nr_absent = range_end_pfn - range_start_pfn;
	unsigned long start_pfn, end_pfn;
	int i;

	for_each_mem_pfn_range(i, nid, &start_pfn, &end_pfn, NULL) {
		start_pfn = clamp(start_pfn, range_start_pfn, range_end_pfn);
		end_pfn = clamp(end_pfn, range_start_pfn, range_end_pfn);
		nr_absent -= end_pfn - start_pfn;
	}
	return nr_absent;
}

/**
 * absent_pages_in_range - Return number of page frames in holes within a range
 * @start_pfn: The start PFN to start searching for holes
 * @end_pfn: The end PFN to stop searching for holes
 *
 * Return: the number of pages frames in memory holes within a range.
 */
unsigned long __init absent_pages_in_range(unsigned long start_pfn,
							unsigned long end_pfn)
{
	return __absent_pages_in_range(MAX_NUMNODES, start_pfn, end_pfn);
}

/* Return the number of page frames in holes in a zone on a node */
static unsigned long __init zone_absent_pages_in_node(int nid,
					unsigned long zone_type,
					unsigned long zone_start_pfn,
					unsigned long zone_end_pfn)
{
	unsigned long nr_absent;

	/* zone is empty, we don't have any absent pages */
	if (zone_start_pfn == zone_end_pfn)
		return 0;

	nr_absent = __absent_pages_in_range(nid, zone_start_pfn, zone_end_pfn);

	/*
	 * ZONE_MOVABLE handling.
	 * Treat pages to be ZONE_MOVABLE in ZONE_NORMAL as absent pages
	 * and vice versa.
	 */
	if (mirrored_kernelcore && zone_movable_pfn[nid]) {
		unsigned long start_pfn, end_pfn;
		struct memblock_region *r;

		for_each_mem_region(r) {
			start_pfn = clamp(memblock_region_memory_base_pfn(r),
					  zone_start_pfn, zone_end_pfn);
			end_pfn = clamp(memblock_region_memory_end_pfn(r),
					zone_start_pfn, zone_end_pfn);

			if (zone_type == ZONE_MOVABLE &&
			    memblock_is_mirror(r))
				nr_absent += end_pfn - start_pfn;

			if (zone_type == ZONE_NORMAL &&
			    !memblock_is_mirror(r))
				nr_absent += end_pfn - start_pfn;
		}
	}

	return nr_absent;
}

/*
 * Return the number of pages a zone spans in a node, including holes
 * present_pages = zone_spanned_pages_in_node() - zone_absent_pages_in_node()
 */
static unsigned long __init zone_spanned_pages_in_node(int nid,
					unsigned long zone_type,
					unsigned long node_start_pfn,
					unsigned long node_end_pfn,
					unsigned long *zone_start_pfn,
					unsigned long *zone_end_pfn)
{
	unsigned long zone_low = arch_zone_lowest_possible_pfn[zone_type];
	unsigned long zone_high = arch_zone_highest_possible_pfn[zone_type];

	/* Get the start and end of the zone */
	*zone_start_pfn = clamp(node_start_pfn, zone_low, zone_high);
	*zone_end_pfn = clamp(node_end_pfn, zone_low, zone_high);
	adjust_zone_range_for_zone_movable(nid, zone_type, node_end_pfn,
					   zone_start_pfn, zone_end_pfn);

	/* Check that this node has pages within the zone's required range */
	if (*zone_end_pfn < node_start_pfn || *zone_start_pfn > node_end_pfn)
		return 0;

	/* Move the zone boundaries inside the node if necessary */
	*zone_end_pfn = min(*zone_end_pfn, node_end_pfn);
	*zone_start_pfn = max(*zone_start_pfn, node_start_pfn);

	/* Return the spanned pages */
	return *zone_end_pfn - *zone_start_pfn;
}

static void __init reset_memoryless_node_totalpages(struct pglist_data *pgdat)
{
	struct zone *z;

	for (z = pgdat->node_zones; z < pgdat->node_zones + MAX_NR_ZONES; z++) {
		z->zone_start_pfn = 0;
		z->spanned_pages = 0;
		z->present_pages = 0;
#if defined(CONFIG_MEMORY_HOTPLUG)
		z->present_early_pages = 0;
#endif
	}

	pgdat->node_spanned_pages = 0;
	pgdat->node_present_pages = 0;
	pr_debug("On node %d totalpages: 0\n", pgdat->node_id);
}

static void __init calc_nr_kernel_pages(void)
{
	unsigned long start_pfn, end_pfn;
	phys_addr_t start_addr, end_addr;
	u64 u;
#ifdef CONFIG_HIGHMEM
	unsigned long high_zone_low = arch_zone_lowest_possible_pfn[ZONE_HIGHMEM];
#endif

	for_each_free_mem_range(u, NUMA_NO_NODE, MEMBLOCK_NONE, &start_addr, &end_addr, NULL) {
		start_pfn = PFN_UP(start_addr);
		end_pfn   = PFN_DOWN(end_addr);

		if (start_pfn < end_pfn) {
			nr_all_pages += end_pfn - start_pfn;
#ifdef CONFIG_HIGHMEM
			start_pfn = clamp(start_pfn, 0, high_zone_low);
			end_pfn = clamp(end_pfn, 0, high_zone_low);
#endif
			nr_kernel_pages += end_pfn - start_pfn;
		}
	}
}

static void __init calculate_node_totalpages(struct pglist_data *pgdat,
						unsigned long node_start_pfn,
						unsigned long node_end_pfn)
{
	unsigned long realtotalpages = 0, totalpages = 0;
	enum zone_type i;

	for (i = 0; i < MAX_NR_ZONES; i++) {
		struct zone *zone = pgdat->node_zones + i;
		unsigned long zone_start_pfn, zone_end_pfn;
		unsigned long spanned, absent;
		unsigned long real_size;

		spanned = zone_spanned_pages_in_node(pgdat->node_id, i,
						     node_start_pfn,
						     node_end_pfn,
						     &zone_start_pfn,
						     &zone_end_pfn);
		absent = zone_absent_pages_in_node(pgdat->node_id, i,
						   zone_start_pfn,
						   zone_end_pfn);

		real_size = spanned - absent;

		if (spanned)
			zone->zone_start_pfn = zone_start_pfn;
		else
			zone->zone_start_pfn = 0;
		zone->spanned_pages = spanned;
		zone->present_pages = real_size;
#if defined(CONFIG_MEMORY_HOTPLUG)
		zone->present_early_pages = real_size;
#endif

		totalpages += spanned;
		realtotalpages += real_size;
	}

	pgdat->node_spanned_pages = totalpages;
	pgdat->node_present_pages = realtotalpages;
	pr_debug("On node %d totalpages: %lu\n", pgdat->node_id, realtotalpages);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static void pgdat_init_split_queue(struct pglist_data *pgdat)
{
	struct deferred_split *ds_queue = &pgdat->deferred_split_queue;

	spin_lock_init(&ds_queue->split_queue_lock);
	INIT_LIST_HEAD(&ds_queue->split_queue);
	ds_queue->split_queue_len = 0;
}
#else
static void pgdat_init_split_queue(struct pglist_data *pgdat) {}
#endif

#ifdef CONFIG_COMPACTION
static void pgdat_init_kcompactd(struct pglist_data *pgdat)
{
	init_waitqueue_head(&pgdat->kcompactd_wait);
}
#else
static void pgdat_init_kcompactd(struct pglist_data *pgdat) {}
#endif

static void __meminit pgdat_init_internals(struct pglist_data *pgdat)
{
	int i;

	pgdat_resize_init(pgdat);
	pgdat_kswapd_lock_init(pgdat);

	pgdat_init_split_queue(pgdat);
	pgdat_init_kcompactd(pgdat);

	init_waitqueue_head(&pgdat->kswapd_wait);
	init_waitqueue_head(&pgdat->pfmemalloc_wait);

	for (i = 0; i < NR_VMSCAN_THROTTLE; i++)
		init_waitqueue_head(&pgdat->reclaim_wait[i]);

	pgdat_page_ext_init(pgdat);
	lruvec_init(&pgdat->__lruvec);
}

static void __meminit zone_init_internals(struct zone *zone, enum zone_type idx, int nid,
							unsigned long remaining_pages)
{
	atomic_long_set(&zone->managed_pages, remaining_pages);
	zone_set_nid(zone, nid);
	zone->name = zone_names[idx];
	zone->zone_pgdat = NODE_DATA(nid);
	spin_lock_init(&zone->lock);
	zone_seqlock_init(zone);
	zone_pcp_init(zone);
}

static void __meminit zone_init_free_lists(struct zone *zone)
{
	unsigned int order, t;
	for_each_migratetype_order(order, t) {
		INIT_LIST_HEAD(&zone->free_area[order].free_list[t]);
		zone->free_area[order].nr_free = 0;
	}

#ifdef CONFIG_UNACCEPTED_MEMORY
	INIT_LIST_HEAD(&zone->unaccepted_pages);
#endif
}

void __meminit init_currently_empty_zone(struct zone *zone,
					unsigned long zone_start_pfn,
					unsigned long size)
{
	struct pglist_data *pgdat = zone->zone_pgdat;
	int zone_idx = zone_idx(zone) + 1;

	if (zone_idx > pgdat->nr_zones)
		pgdat->nr_zones = zone_idx;

	zone->zone_start_pfn = zone_start_pfn;

	mminit_dprintk(MMINIT_TRACE, "memmap_init",
			"Initialising map node %d zone %lu pfns %lu -> %lu\n",
			pgdat->node_id,
			(unsigned long)zone_idx(zone),
			zone_start_pfn, (zone_start_pfn + size));

	zone_init_free_lists(zone);
	zone->initialized = 1;
}

#ifndef CONFIG_SPARSEMEM
/*
 * Calculate the size of the zone->pageblock_flags rounded to an unsigned long
 * Start by making sure zonesize is a multiple of pageblock_order by rounding
 * up. Then use 1 NR_PAGEBLOCK_BITS worth of bits per pageblock, finally
 * round what is now in bits to nearest long in bits, then return it in
 * bytes.
 */
static unsigned long __init usemap_size(unsigned long zone_start_pfn, unsigned long zonesize)
{
	unsigned long usemapsize;

	zonesize += zone_start_pfn & (pageblock_nr_pages-1);
	usemapsize = round_up(zonesize, pageblock_nr_pages);
	usemapsize = usemapsize >> pageblock_order;
	usemapsize *= NR_PAGEBLOCK_BITS;
	usemapsize = round_up(usemapsize, BITS_PER_LONG);

	return usemapsize / BITS_PER_BYTE;
}

static void __ref setup_usemap(struct zone *zone)
{
	unsigned long usemapsize = usemap_size(zone->zone_start_pfn,
					       zone->spanned_pages);
	zone->pageblock_flags = NULL;
	if (usemapsize) {
		zone->pageblock_flags =
			memblock_alloc_node(usemapsize, SMP_CACHE_BYTES,
					    zone_to_nid(zone));
		if (!zone->pageblock_flags)
			panic("Failed to allocate %ld bytes for zone %s pageblock flags on node %d\n",
			      usemapsize, zone->name, zone_to_nid(zone));
	}
}
#else
static inline void setup_usemap(struct zone *zone) {}
#endif /* CONFIG_SPARSEMEM */

#ifdef CONFIG_HUGETLB_PAGE_SIZE_VARIABLE

/* Initialise the number of pages represented by NR_PAGEBLOCK_BITS */
void __init set_pageblock_order(void)
{
	unsigned int order = MAX_PAGE_ORDER;

	/* Check that pageblock_nr_pages has not already been setup */
	if (pageblock_order)
		return;

	/* Don't let pageblocks exceed the maximum allocation granularity. */
	if (HPAGE_SHIFT > PAGE_SHIFT && HUGETLB_PAGE_ORDER < order)
		order = HUGETLB_PAGE_ORDER;

	/*
	 * Assume the largest contiguous order of interest is a huge page.
	 * This value may be variable depending on boot parameters on powerpc.
	 */
	pageblock_order = order;
}
#else /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

/*
 * When CONFIG_HUGETLB_PAGE_SIZE_VARIABLE is not set, set_pageblock_order()
 * is unused as pageblock_order is set at compile-time. See
 * include/linux/pageblock-flags.h for the values of pageblock_order based on
 * the kernel config
 */
void __init set_pageblock_order(void)
{
}

#endif /* CONFIG_HUGETLB_PAGE_SIZE_VARIABLE */

/*
 * Set up the zone data structures
 * - init pgdat internals
 * - init all zones belonging to this node
 *
 * NOTE: this function is only called during memory hotplug
 */
#ifdef CONFIG_MEMORY_HOTPLUG
void __ref free_area_init_core_hotplug(struct pglist_data *pgdat)
{
	int nid = pgdat->node_id;
	enum zone_type z;
	int cpu;

	pgdat_init_internals(pgdat);

	if (pgdat->per_cpu_nodestats == &boot_nodestats)
		pgdat->per_cpu_nodestats = alloc_percpu(struct per_cpu_nodestat);

	/*
	 * Reset the nr_zones, order and highest_zoneidx before reuse.
	 * Note that kswapd will init kswapd_highest_zoneidx properly
	 * when it starts in the near future.
	 */
	pgdat->nr_zones = 0;
	pgdat->kswapd_order = 0;
	pgdat->kswapd_highest_zoneidx = 0;
	pgdat->node_start_pfn = 0;
	pgdat->node_present_pages = 0;

	for_each_online_cpu(cpu) {
		struct per_cpu_nodestat *p;

		p = per_cpu_ptr(pgdat->per_cpu_nodestats, cpu);
		memset(p, 0, sizeof(*p));
	}

	/*
	 * When memory is hot-added, all the memory is in offline state. So
	 * clear all zones' present_pages and managed_pages because they will
	 * be updated in online_pages() and offline_pages().
	 */
	for (z = 0; z < MAX_NR_ZONES; z++) {
		struct zone *zone = pgdat->node_zones + z;

		zone->present_pages = 0;
		zone_init_internals(zone, z, nid, 0);
	}
}
#endif

static void __init free_area_init_core(struct pglist_data *pgdat)
{
	enum zone_type j;
	int nid = pgdat->node_id;

	pgdat_init_internals(pgdat);
	pgdat->per_cpu_nodestats = &boot_nodestats;

	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size = zone->spanned_pages;

		/*
		 * Initialize zone->managed_pages as 0 , it will be reset
		 * when memblock allocator frees pages into buddy system.
		 */
		zone_init_internals(zone, j, nid, zone->present_pages);

		if (!size)
			continue;

		setup_usemap(zone);
		init_currently_empty_zone(zone, zone->zone_start_pfn, size);
	}
}

void __init *memmap_alloc(phys_addr_t size, phys_addr_t align,
			  phys_addr_t min_addr, int nid, bool exact_nid)
{
	void *ptr;

	/*
	 * Kmemleak will explicitly scan mem_map by traversing all valid
	 * `struct *page`,so memblock does not need to be added to the scan list.
	 */
	if (exact_nid)
		ptr = memblock_alloc_exact_nid_raw(size, align, min_addr,
						   MEMBLOCK_ALLOC_NOLEAKTRACE,
						   nid);
	else
		ptr = memblock_alloc_try_nid_raw(size, align, min_addr,
						 MEMBLOCK_ALLOC_NOLEAKTRACE,
						 nid);

	if (ptr && size > 0)
		page_init_poison(ptr, size);

	return ptr;
}

#ifdef CONFIG_FLATMEM
static void __init alloc_node_mem_map(struct pglist_data *pgdat)
{
	unsigned long start, offset, size, end;
	struct page *map;

	/* Skip empty nodes */
	if (!pgdat->node_spanned_pages)
		return;

	start = pgdat->node_start_pfn & ~(MAX_ORDER_NR_PAGES - 1);
	offset = pgdat->node_start_pfn - start;
	/*
	 * The zone's endpoints aren't required to be MAX_PAGE_ORDER
	 * aligned but the node_mem_map endpoints must be in order
	 * for the buddy allocator to function correctly.
	 */
	end = ALIGN(pgdat_end_pfn(pgdat), MAX_ORDER_NR_PAGES);
	size =  (end - start) * sizeof(struct page);
	map = memmap_alloc(size, SMP_CACHE_BYTES, MEMBLOCK_LOW_LIMIT,
			   pgdat->node_id, false);
	if (!map)
		panic("Failed to allocate %ld bytes for node %d memory map\n",
		      size, pgdat->node_id);
	pgdat->node_mem_map = map + offset;
	memmap_boot_pages_add(DIV_ROUND_UP(size, PAGE_SIZE));
	pr_debug("%s: node %d, pgdat %08lx, node_mem_map %08lx\n",
		 __func__, pgdat->node_id, (unsigned long)pgdat,
		 (unsigned long)pgdat->node_mem_map);

	/* the global mem_map is just set as node 0's */
	WARN_ON(pgdat != NODE_DATA(0));

	mem_map = pgdat->node_mem_map;
	if (page_to_pfn(mem_map) != pgdat->node_start_pfn)
		mem_map -= offset;

	max_mapnr = end - start;
}
#else
static inline void alloc_node_mem_map(struct pglist_data *pgdat) { }
#endif /* CONFIG_FLATMEM */

/**
 * get_pfn_range_for_nid - Return the start and end page frames for a node
 * @nid: The nid to return the range for. If MAX_NUMNODES, the min and max PFN are returned.
 * @start_pfn: Passed by reference. On return, it will have the node start_pfn.
 * @end_pfn: Passed by reference. On return, it will have the node end_pfn.
 *
 * It returns the start and end page frame of a node based on information
 * provided by memblock_set_node(). If called for a node
 * with no available memory, the start and end PFNs will be 0.
 */
void __init get_pfn_range_for_nid(unsigned int nid,
			unsigned long *start_pfn, unsigned long *end_pfn)
{
	unsigned long this_start_pfn, this_end_pfn;
	int i;

	*start_pfn = -1UL;
	*end_pfn = 0;

	for_each_mem_pfn_range(i, nid, &this_start_pfn, &this_end_pfn, NULL) {
		*start_pfn = min(*start_pfn, this_start_pfn);
		*end_pfn = max(*end_pfn, this_end_pfn);
	}

	if (*start_pfn == -1UL)
		*start_pfn = 0;
}

static void __init free_area_init_node(int nid)
{
	pg_data_t *pgdat = NODE_DATA(nid);
	unsigned long start_pfn = 0;
	unsigned long end_pfn = 0;

	/* pg_data_t should be reset to zero when it's allocated */
	WARN_ON(pgdat->nr_zones || pgdat->kswapd_highest_zoneidx);

	get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);

	pgdat->node_id = nid;
	pgdat->node_start_pfn = start_pfn;
	pgdat->per_cpu_nodestats = NULL;

	if (start_pfn != end_pfn) {
		pr_info("Initmem setup node %d [mem %#018Lx-%#018Lx]\n", nid,
			(u64)start_pfn << PAGE_SHIFT,
			end_pfn ? ((u64)end_pfn << PAGE_SHIFT) - 1 : 0);

		calculate_node_totalpages(pgdat, start_pfn, end_pfn);
	} else {
		pr_info("Initmem setup node %d as memoryless\n", nid);

		reset_memoryless_node_totalpages(pgdat);
	}

	alloc_node_mem_map(pgdat);
	pgdat_set_deferred_range(pgdat);

	free_area_init_core(pgdat);
	lru_gen_init_pgdat(pgdat);
}

/* Any regular or high memory on that node ? */
static void __init check_for_memory(pg_data_t *pgdat)
{
	enum zone_type zone_type;

	for (zone_type = 0; zone_type <= ZONE_MOVABLE - 1; zone_type++) {
		struct zone *zone = &pgdat->node_zones[zone_type];
		if (populated_zone(zone)) {
			if (IS_ENABLED(CONFIG_HIGHMEM))
				node_set_state(pgdat->node_id, N_HIGH_MEMORY);
			if (zone_type <= ZONE_NORMAL)
				node_set_state(pgdat->node_id, N_NORMAL_MEMORY);
			break;
		}
	}
}

#if MAX_NUMNODES > 1
/*
 * Figure out the number of possible node ids.
 */
void __init setup_nr_node_ids(void)
{
	unsigned int highest;

	highest = find_last_bit(node_possible_map.bits, MAX_NUMNODES);
	nr_node_ids = highest + 1;
}
#endif

/*
 * Some architectures, e.g. ARC may have ZONE_HIGHMEM below ZONE_NORMAL. For
 * such cases we allow max_zone_pfn sorted in the descending order
 */
static bool arch_has_descending_max_zone_pfns(void)
{
	return IS_ENABLED(CONFIG_ARC) && !IS_ENABLED(CONFIG_ARC_HAS_PAE40);
}

static void set_high_memory(void)
{
	phys_addr_t highmem = memblock_end_of_DRAM();

	/*
	 * Some architectures (e.g. ARM) set high_memory very early and
	 * use it in arch setup code.
	 * If an architecture already set high_memory don't overwrite it
	 */
	if (high_memory)
		return;

#ifdef CONFIG_HIGHMEM
	if (arch_has_descending_max_zone_pfns() ||
	    highmem > PFN_PHYS(arch_zone_lowest_possible_pfn[ZONE_HIGHMEM]))
		highmem = PFN_PHYS(arch_zone_lowest_possible_pfn[ZONE_HIGHMEM]);
#endif

	high_memory = phys_to_virt(highmem - 1) + 1;
}

/**
 * free_area_init - Initialise all pg_data_t and zone data
 * @max_zone_pfn: an array of max PFNs for each zone
 *
 * This will call free_area_init_node() for each active node in the system.
 * Using the page ranges provided by memblock_set_node(), the size of each
 * zone in each node and their holes is calculated. If the maximum PFN
 * between two adjacent zones match, it is assumed that the zone is empty.
 * For example, if arch_max_dma_pfn == arch_max_dma32_pfn, it is assumed
 * that arch_max_dma32_pfn has no pages. It is also assumed that a zone
 * starts where the previous one ended. For example, ZONE_DMA32 starts
 * at arch_max_dma_pfn.
 */
void __init free_area_init(unsigned long *max_zone_pfn)
{
	unsigned long start_pfn, end_pfn;
	int i, nid, zone;
	bool descending;

	/* Record where the zone boundaries are */
	memset(arch_zone_lowest_possible_pfn, 0,
				sizeof(arch_zone_lowest_possible_pfn));
	memset(arch_zone_highest_possible_pfn, 0,
				sizeof(arch_zone_highest_possible_pfn));

	start_pfn = PHYS_PFN(memblock_start_of_DRAM());
	descending = arch_has_descending_max_zone_pfns();

	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (descending)
			zone = MAX_NR_ZONES - i - 1;
		else
			zone = i;

		if (zone == ZONE_MOVABLE)
			continue;

		end_pfn = max(max_zone_pfn[zone], start_pfn);
		arch_zone_lowest_possible_pfn[zone] = start_pfn;
		arch_zone_highest_possible_pfn[zone] = end_pfn;

		start_pfn = end_pfn;
	}

	/* Find the PFNs that ZONE_MOVABLE begins at in each node */
	memset(zone_movable_pfn, 0, sizeof(zone_movable_pfn));
	find_zone_movable_pfns_for_nodes();

	/* Print out the zone ranges */
	pr_info("Zone ranges:\n");
	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (i == ZONE_MOVABLE)
			continue;
		pr_info("  %-8s ", zone_names[i]);
		if (arch_zone_lowest_possible_pfn[i] ==
				arch_zone_highest_possible_pfn[i])
			pr_cont("empty\n");
		else
			pr_cont("[mem %#018Lx-%#018Lx]\n",
				(u64)arch_zone_lowest_possible_pfn[i]
					<< PAGE_SHIFT,
				((u64)arch_zone_highest_possible_pfn[i]
					<< PAGE_SHIFT) - 1);
	}

	/* Print out the PFNs ZONE_MOVABLE begins at in each node */
	pr_info("Movable zone start for each node\n");
	for (i = 0; i < MAX_NUMNODES; i++) {
		if (zone_movable_pfn[i])
			pr_info("  Node %d: %#018Lx\n", i,
			       (u64)zone_movable_pfn[i] << PAGE_SHIFT);
	}

	/*
	 * Print out the early node map, and initialize the
	 * subsection-map relative to active online memory ranges to
	 * enable future "sub-section" extensions of the memory map.
	 */
	pr_info("Early memory node ranges\n");
	for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn, &nid) {
		pr_info("  node %3d: [mem %#018Lx-%#018Lx]\n", nid,
			(u64)start_pfn << PAGE_SHIFT,
			((u64)end_pfn << PAGE_SHIFT) - 1);
		subsection_map_init(start_pfn, end_pfn - start_pfn);
	}

	/* Initialise every node */
	mminit_verify_pageflags_layout();
	setup_nr_node_ids();
	set_pageblock_order();

	for_each_node(nid) {
		pg_data_t *pgdat;

		if (!node_online(nid))
			alloc_offline_node_data(nid);

		pgdat = NODE_DATA(nid);
		free_area_init_node(nid);

		/*
		 * No sysfs hierarcy will be created via register_one_node()
		 *for memory-less node because here it's not marked as N_MEMORY
		 *and won't be set online later. The benefit is userspace
		 *program won't be confused by sysfs files/directories of
		 *memory-less node. The pgdat will get fully initialized by
		 *hotadd_init_pgdat() when memory is hotplugged into this node.
		 */
		if (pgdat->node_present_pages) {
			node_set_state(nid, N_MEMORY);
			check_for_memory(pgdat);
		}
	}

	for_each_node_state(nid, N_MEMORY)
		sparse_vmemmap_init_nid_late(nid);

	calc_nr_kernel_pages();
	memmap_init();

	/* disable hash distribution for systems with a single node */
	fixup_hashdist();

	set_high_memory();
}

/**
 * node_map_pfn_alignment - determine the maximum internode alignment
 *
 * This function should be called after node map is populated and sorted.
 * It calculates the maximum power of two alignment which can distinguish
 * all the nodes.
 *
 * For example, if all nodes are 1GiB and aligned to 1GiB, the return value
 * would indicate 1GiB alignment with (1 << (30 - PAGE_SHIFT)).  If the
 * nodes are shifted by 256MiB, 256MiB.  Note that if only the last node is
 * shifted, 1GiB is enough and this function will indicate so.
 *
 * This is used to test whether pfn -> nid mapping of the chosen memory
 * model has fine enough granularity to avoid incorrect mapping for the
 * populated node map.
 *
 * Return: the determined alignment in pfn's.  0 if there is no alignment
 * requirement (single node).
 */
unsigned long __init node_map_pfn_alignment(void)
{
	unsigned long accl_mask = 0, last_end = 0;
	unsigned long start, end, mask;
	int last_nid = NUMA_NO_NODE;
	int i, nid;

	for_each_mem_pfn_range(i, MAX_NUMNODES, &start, &end, &nid) {
		if (!start || last_nid < 0 || last_nid == nid) {
			last_nid = nid;
			last_end = end;
			continue;
		}

		/*
		 * Start with a mask granular enough to pin-point to the
		 * start pfn and tick off bits one-by-one until it becomes
		 * too coarse to separate the current node from the last.
		 */
		mask = ~((1 << __ffs(start)) - 1);
		while (mask && last_end <= (start & (mask << 1)))
			mask <<= 1;

		/* accumulate all internode masks */
		accl_mask |= mask;
	}

	/* convert mask to number of pages */
	return ~accl_mask + 1;
}

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
static void __init deferred_free_pages(unsigned long pfn,
		unsigned long nr_pages)
{
	struct page *page;
	unsigned long i;

	if (!nr_pages)
		return;

	page = pfn_to_page(pfn);

	/* Free a large naturally-aligned chunk if possible */
	if (nr_pages == MAX_ORDER_NR_PAGES && IS_MAX_ORDER_ALIGNED(pfn)) {
		for (i = 0; i < nr_pages; i += pageblock_nr_pages)
			set_pageblock_migratetype(page + i, MIGRATE_MOVABLE);
		__free_pages_core(page, MAX_PAGE_ORDER, MEMINIT_EARLY);
		return;
	}

	/* Accept chunks smaller than MAX_PAGE_ORDER upfront */
	accept_memory(PFN_PHYS(pfn), nr_pages * PAGE_SIZE);

	for (i = 0; i < nr_pages; i++, page++, pfn++) {
		if (pageblock_aligned(pfn))
			set_pageblock_migratetype(page, MIGRATE_MOVABLE);
		__free_pages_core(page, 0, MEMINIT_EARLY);
	}
}

/* Completion tracking for deferred_init_memmap() threads */
static atomic_t pgdat_init_n_undone __initdata;
static __initdata DECLARE_COMPLETION(pgdat_init_all_done_comp);

static inline void __init pgdat_init_report_one_done(void)
{
	if (atomic_dec_and_test(&pgdat_init_n_undone))
		complete(&pgdat_init_all_done_comp);
}

/*
 * Initialize struct pages.  We minimize pfn page lookups and scheduler checks
 * by performing it only once every MAX_ORDER_NR_PAGES.
 * Return number of pages initialized.
 */
static unsigned long __init deferred_init_pages(struct zone *zone,
		unsigned long pfn, unsigned long end_pfn)
{
	int nid = zone_to_nid(zone);
	unsigned long nr_pages = end_pfn - pfn;
	int zid = zone_idx(zone);
	struct page *page = pfn_to_page(pfn);

	for (; pfn < end_pfn; pfn++, page++)
		__init_single_page(page, pfn, zid, nid);
	return nr_pages;
}

/*
 * This function is meant to pre-load the iterator for the zone init from
 * a given point.
 * Specifically it walks through the ranges starting with initial index
 * passed to it until we are caught up to the first_init_pfn value and
 * exits there. If we never encounter the value we return false indicating
 * there are no valid ranges left.
 */
static bool __init
deferred_init_mem_pfn_range_in_zone(u64 *i, struct zone *zone,
				    unsigned long *spfn, unsigned long *epfn,
				    unsigned long first_init_pfn)
{
	u64 j = *i;

	if (j == 0)
		__next_mem_pfn_range_in_zone(&j, zone, spfn, epfn);

	/*
	 * Start out by walking through the ranges in this zone that have
	 * already been initialized. We don't need to do anything with them
	 * so we just need to flush them out of the system.
	 */
	for_each_free_mem_pfn_range_in_zone_from(j, zone, spfn, epfn) {
		if (*epfn <= first_init_pfn)
			continue;
		if (*spfn < first_init_pfn)
			*spfn = first_init_pfn;
		*i = j;
		return true;
	}

	return false;
}

/*
 * Initialize and free pages. We do it in two loops: first we initialize
 * struct page, then free to buddy allocator, because while we are
 * freeing pages we can access pages that are ahead (computing buddy
 * page in __free_one_page()).
 *
 * In order to try and keep some memory in the cache we have the loop
 * broken along max page order boundaries. This way we will not cause
 * any issues with the buddy page computation.
 */
static unsigned long __init
deferred_init_maxorder(u64 *i, struct zone *zone, unsigned long *start_pfn,
		       unsigned long *end_pfn)
{
	unsigned long mo_pfn = ALIGN(*start_pfn + 1, MAX_ORDER_NR_PAGES);
	unsigned long spfn = *start_pfn, epfn = *end_pfn;
	unsigned long nr_pages = 0;
	u64 j = *i;

	/* First we loop through and initialize the page values */
	for_each_free_mem_pfn_range_in_zone_from(j, zone, start_pfn, end_pfn) {
		unsigned long t;

		if (mo_pfn <= *start_pfn)
			break;

		t = min(mo_pfn, *end_pfn);
		nr_pages += deferred_init_pages(zone, *start_pfn, t);

		if (mo_pfn < *end_pfn) {
			*start_pfn = mo_pfn;
			break;
		}
	}

	/* Reset values and now loop through freeing pages as needed */
	swap(j, *i);

	for_each_free_mem_pfn_range_in_zone_from(j, zone, &spfn, &epfn) {
		unsigned long t;

		if (mo_pfn <= spfn)
			break;

		t = min(mo_pfn, epfn);
		deferred_free_pages(spfn, t - spfn);

		if (mo_pfn <= epfn)
			break;
	}

	return nr_pages;
}

static void __init
deferred_init_memmap_chunk(unsigned long start_pfn, unsigned long end_pfn,
			   void *arg)
{
	unsigned long spfn, epfn;
	struct zone *zone = arg;
	u64 i = 0;

	deferred_init_mem_pfn_range_in_zone(&i, zone, &spfn, &epfn, start_pfn);

	/*
	 * Initialize and free pages in MAX_PAGE_ORDER sized increments so that
	 * we can avoid introducing any issues with the buddy allocator.
	 */
	while (spfn < end_pfn) {
		deferred_init_maxorder(&i, zone, &spfn, &epfn);
		cond_resched();
	}
}

static unsigned int __init
deferred_page_init_max_threads(const struct cpumask *node_cpumask)
{
	return max(cpumask_weight(node_cpumask), 1U);
}

/* Initialise remaining memory on a node */
static int __init deferred_init_memmap(void *data)
{
	pg_data_t *pgdat = data;
	const struct cpumask *cpumask = cpumask_of_node(pgdat->node_id);
	unsigned long spfn = 0, epfn = 0;
	unsigned long first_init_pfn, flags;
	unsigned long start = jiffies;
	struct zone *zone;
	int max_threads;
	u64 i = 0;

	/* Bind memory initialisation thread to a local node if possible */
	if (!cpumask_empty(cpumask))
		set_cpus_allowed_ptr(current, cpumask);

	pgdat_resize_lock(pgdat, &flags);
	first_init_pfn = pgdat->first_deferred_pfn;
	if (first_init_pfn == ULONG_MAX) {
		pgdat_resize_unlock(pgdat, &flags);
		pgdat_init_report_one_done();
		return 0;
	}

	/* Sanity check boundaries */
	BUG_ON(pgdat->first_deferred_pfn < pgdat->node_start_pfn);
	BUG_ON(pgdat->first_deferred_pfn > pgdat_end_pfn(pgdat));
	pgdat->first_deferred_pfn = ULONG_MAX;

	/*
	 * Once we unlock here, the zone cannot be grown anymore, thus if an
	 * interrupt thread must allocate this early in boot, zone must be
	 * pre-grown prior to start of deferred page initialization.
	 */
	pgdat_resize_unlock(pgdat, &flags);

	/* Only the highest zone is deferred */
	zone = pgdat->node_zones + pgdat->nr_zones - 1;

	max_threads = deferred_page_init_max_threads(cpumask);

	while (deferred_init_mem_pfn_range_in_zone(&i, zone, &spfn, &epfn, first_init_pfn)) {
		first_init_pfn = ALIGN(epfn, PAGES_PER_SECTION);
		struct padata_mt_job job = {
			.thread_fn   = deferred_init_memmap_chunk,
			.fn_arg      = zone,
			.start       = spfn,
			.size        = first_init_pfn - spfn,
			.align       = PAGES_PER_SECTION,
			.min_chunk   = PAGES_PER_SECTION,
			.max_threads = max_threads,
			.numa_aware  = false,
		};

		padata_do_multithreaded(&job);
	}

	/* Sanity check that the next zone really is unpopulated */
	WARN_ON(pgdat->nr_zones < MAX_NR_ZONES && populated_zone(++zone));

	pr_info("node %d deferred pages initialised in %ums\n",
		pgdat->node_id, jiffies_to_msecs(jiffies - start));

	pgdat_init_report_one_done();
	return 0;
}

/*
 * If this zone has deferred pages, try to grow it by initializing enough
 * deferred pages to satisfy the allocation specified by order, rounded up to
 * the nearest PAGES_PER_SECTION boundary.  So we're adding memory in increments
 * of SECTION_SIZE bytes by initializing struct pages in increments of
 * PAGES_PER_SECTION * sizeof(struct page) bytes.
 *
 * Return true when zone was grown, otherwise return false. We return true even
 * when we grow less than requested, to let the caller decide if there are
 * enough pages to satisfy the allocation.
 */
bool __init deferred_grow_zone(struct zone *zone, unsigned int order)
{
	unsigned long nr_pages_needed = ALIGN(1 << order, PAGES_PER_SECTION);
	pg_data_t *pgdat = zone->zone_pgdat;
	unsigned long first_deferred_pfn = pgdat->first_deferred_pfn;
	unsigned long spfn, epfn, flags;
	unsigned long nr_pages = 0;
	u64 i = 0;

	/* Only the last zone may have deferred pages */
	if (zone_end_pfn(zone) != pgdat_end_pfn(pgdat))
		return false;

	pgdat_resize_lock(pgdat, &flags);

	/*
	 * If someone grew this zone while we were waiting for spinlock, return
	 * true, as there might be enough pages already.
	 */
	if (first_deferred_pfn != pgdat->first_deferred_pfn) {
		pgdat_resize_unlock(pgdat, &flags);
		return true;
	}

	/* If the zone is empty somebody else may have cleared out the zone */
	if (!deferred_init_mem_pfn_range_in_zone(&i, zone, &spfn, &epfn,
						 first_deferred_pfn)) {
		pgdat->first_deferred_pfn = ULONG_MAX;
		pgdat_resize_unlock(pgdat, &flags);
		/* Retry only once. */
		return first_deferred_pfn != ULONG_MAX;
	}

	/*
	 * Initialize and free pages in MAX_PAGE_ORDER sized increments so
	 * that we can avoid introducing any issues with the buddy
	 * allocator.
	 */
	while (spfn < epfn) {
		/* update our first deferred PFN for this section */
		first_deferred_pfn = spfn;

		nr_pages += deferred_init_maxorder(&i, zone, &spfn, &epfn);
		touch_nmi_watchdog();

		/* We should only stop along section boundaries */
		if ((first_deferred_pfn ^ spfn) < PAGES_PER_SECTION)
			continue;

		/* If our quota has been met we can stop here */
		if (nr_pages >= nr_pages_needed)
			break;
	}

	pgdat->first_deferred_pfn = spfn;
	pgdat_resize_unlock(pgdat, &flags);

	return nr_pages > 0;
}

#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

#ifdef CONFIG_CMA
void __init init_cma_reserved_pageblock(struct page *page)
{
	unsigned i = pageblock_nr_pages;
	struct page *p = page;

	do {
		__ClearPageReserved(p);
		set_page_count(p, 0);
	} while (++p, --i);

	set_pageblock_migratetype(page, MIGRATE_CMA);
	set_page_refcounted(page);
	/* pages were reserved and not allocated */
	clear_page_tag_ref(page);
	__free_pages(page, pageblock_order);

	adjust_managed_page_count(page, pageblock_nr_pages);
	page_zone(page)->cma_pages += pageblock_nr_pages;
}
/*
 * Similar to above, but only set the migrate type and stats.
 */
void __init init_cma_pageblock(struct page *page)
{
	set_pageblock_migratetype(page, MIGRATE_CMA);
	adjust_managed_page_count(page, pageblock_nr_pages);
	page_zone(page)->cma_pages += pageblock_nr_pages;
}
#endif

void set_zone_contiguous(struct zone *zone)
{
	unsigned long block_start_pfn = zone->zone_start_pfn;
	unsigned long block_end_pfn;

	block_end_pfn = pageblock_end_pfn(block_start_pfn);
	for (; block_start_pfn < zone_end_pfn(zone);
			block_start_pfn = block_end_pfn,
			 block_end_pfn += pageblock_nr_pages) {

		block_end_pfn = min(block_end_pfn, zone_end_pfn(zone));

		if (!__pageblock_pfn_to_page(block_start_pfn,
					     block_end_pfn, zone))
			return;
		cond_resched();
	}

	/* We confirm that there is no hole */
	zone->contiguous = true;
}

/*
 * Check if a PFN range intersects multiple zones on one or more
 * NUMA nodes. Specify the @nid argument if it is known that this
 * PFN range is on one node, NUMA_NO_NODE otherwise.
 */
bool pfn_range_intersects_zones(int nid, unsigned long start_pfn,
			   unsigned long nr_pages)
{
	struct zone *zone, *izone = NULL;

	for_each_zone(zone) {
		if (nid != NUMA_NO_NODE && zone_to_nid(zone) != nid)
			continue;

		if (zone_intersects(zone, start_pfn, nr_pages)) {
			if (izone != NULL)
				return true;
			izone = zone;
		}

	}

	return false;
}

static void __init mem_init_print_info(void);
void __init page_alloc_init_late(void)
{
	struct zone *zone;
	int nid;

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT

	/* There will be num_node_state(N_MEMORY) threads */
	atomic_set(&pgdat_init_n_undone, num_node_state(N_MEMORY));
	for_each_node_state(nid, N_MEMORY) {
		kthread_run(deferred_init_memmap, NODE_DATA(nid), "pgdatinit%d", nid);
	}

	/* Block until all are initialised */
	wait_for_completion(&pgdat_init_all_done_comp);

	/*
	 * We initialized the rest of the deferred pages.  Permanently disable
	 * on-demand struct page initialization.
	 */
	static_branch_disable(&deferred_pages);

	/* Reinit limits that are based on free pages after the kernel is up */
	files_maxfiles_init();
#endif

	/* Accounting of total+free memory is stable at this point. */
	mem_init_print_info();
	buffer_init();

	/* Discard memblock private memory */
	memblock_discard();

	for_each_node_state(nid, N_MEMORY)
		shuffle_free_memory(NODE_DATA(nid));

	for_each_populated_zone(zone)
		set_zone_contiguous(zone);

	/* Initialize page ext after all struct pages are initialized. */
	if (deferred_struct_pages)
		page_ext_init();

	page_alloc_sysctl_init();
}

/*
 * Adaptive scale is meant to reduce sizes of hash tables on large memory
 * machines. As memory size is increased the scale is also increased but at
 * slower pace.  Starting from ADAPT_SCALE_BASE (64G), every time memory
 * quadruples the scale is increased by one, which means the size of hash table
 * only doubles, instead of quadrupling as well.
 * Because 32-bit systems cannot have large physical memory, where this scaling
 * makes sense, it is disabled on such platforms.
 */
#if __BITS_PER_LONG > 32
#define ADAPT_SCALE_BASE	(64ul << 30)
#define ADAPT_SCALE_SHIFT	2
#define ADAPT_SCALE_NPAGES	(ADAPT_SCALE_BASE >> PAGE_SHIFT)
#endif

/*
 * allocate a large system hash table from bootmem
 * - it is assumed that the hash table must contain an exact power-of-2
 *   quantity of entries
 * - limit is the number of hash buckets, not the total allocation size
 */
void *__init alloc_large_system_hash(const char *tablename,
				     unsigned long bucketsize,
				     unsigned long numentries,
				     int scale,
				     int flags,
				     unsigned int *_hash_shift,
				     unsigned int *_hash_mask,
				     unsigned long low_limit,
				     unsigned long high_limit)
{
	unsigned long long max = high_limit;
	unsigned long log2qty, size;
	void *table;
	gfp_t gfp_flags;
	bool virt;
	bool huge;

	/* allow the kernel cmdline to have a say */
	if (!numentries) {
		/* round applicable memory size up to nearest megabyte */
		numentries = nr_kernel_pages;

		/* It isn't necessary when PAGE_SIZE >= 1MB */
		if (PAGE_SIZE < SZ_1M)
			numentries = round_up(numentries, SZ_1M / PAGE_SIZE);

#if __BITS_PER_LONG > 32
		if (!high_limit) {
			unsigned long adapt;

			for (adapt = ADAPT_SCALE_NPAGES; adapt < numentries;
			     adapt <<= ADAPT_SCALE_SHIFT)
				scale++;
		}
#endif

		/* limit to 1 bucket per 2^scale bytes of low memory */
		if (scale > PAGE_SHIFT)
			numentries >>= (scale - PAGE_SHIFT);
		else
			numentries <<= (PAGE_SHIFT - scale);

		if (unlikely((numentries * bucketsize) < PAGE_SIZE))
			numentries = PAGE_SIZE / bucketsize;
	}
	numentries = roundup_pow_of_two(numentries);

	/* limit allocation size to 1/16 total memory by default */
	if (max == 0) {
		max = ((unsigned long long)nr_all_pages << PAGE_SHIFT) >> 4;
		do_div(max, bucketsize);
	}
	max = min(max, 0x80000000ULL);

	if (numentries < low_limit)
		numentries = low_limit;
	if (numentries > max)
		numentries = max;

	log2qty = ilog2(numentries);

	gfp_flags = (flags & HASH_ZERO) ? GFP_ATOMIC | __GFP_ZERO : GFP_ATOMIC;
	do {
		virt = false;
		size = bucketsize << log2qty;
		if (flags & HASH_EARLY) {
			if (flags & HASH_ZERO)
				table = memblock_alloc(size, SMP_CACHE_BYTES);
			else
				table = memblock_alloc_raw(size,
							   SMP_CACHE_BYTES);
		} else if (get_order(size) > MAX_PAGE_ORDER || hashdist) {
			table = vmalloc_huge(size, gfp_flags);
			virt = true;
			if (table)
				huge = is_vm_area_hugepages(table);
		} else {
			/*
			 * If bucketsize is not a power-of-two, we may free
			 * some pages at the end of hash table which
			 * alloc_pages_exact() automatically does
			 */
			table = alloc_pages_exact(size, gfp_flags);
			kmemleak_alloc(table, size, 1, gfp_flags);
		}
	} while (!table && size > PAGE_SIZE && --log2qty);

	if (!table)
		panic("Failed to allocate %s hash table\n", tablename);

	pr_info("%s hash table entries: %ld (order: %d, %lu bytes, %s)\n",
		tablename, 1UL << log2qty, ilog2(size) - PAGE_SHIFT, size,
		virt ? (huge ? "vmalloc hugepage" : "vmalloc") : "linear");

	if (_hash_shift)
		*_hash_shift = log2qty;
	if (_hash_mask)
		*_hash_mask = (1 << log2qty) - 1;

	return table;
}

void __init memblock_free_pages(struct page *page, unsigned long pfn,
							unsigned int order)
{
	if (IS_ENABLED(CONFIG_DEFERRED_STRUCT_PAGE_INIT)) {
		int nid = early_pfn_to_nid(pfn);

		if (!early_page_initialised(pfn, nid))
			return;
	}

	if (!kmsan_memblock_free_pages(page, order)) {
		/* KMSAN will take care of these pages. */
		return;
	}

	/* pages were reserved and not allocated */
	clear_page_tag_ref(page);
	__free_pages_core(page, order, MEMINIT_EARLY);
}

DEFINE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_ALLOC_DEFAULT_ON, init_on_alloc);
EXPORT_SYMBOL(init_on_alloc);

DEFINE_STATIC_KEY_MAYBE(CONFIG_INIT_ON_FREE_DEFAULT_ON, init_on_free);
EXPORT_SYMBOL(init_on_free);

static bool _init_on_alloc_enabled_early __read_mostly
				= IS_ENABLED(CONFIG_INIT_ON_ALLOC_DEFAULT_ON);
static int __init early_init_on_alloc(char *buf)
{

	return kstrtobool(buf, &_init_on_alloc_enabled_early);
}
early_param("init_on_alloc", early_init_on_alloc);

static bool _init_on_free_enabled_early __read_mostly
				= IS_ENABLED(CONFIG_INIT_ON_FREE_DEFAULT_ON);
static int __init early_init_on_free(char *buf)
{
	return kstrtobool(buf, &_init_on_free_enabled_early);
}
early_param("init_on_free", early_init_on_free);

DEFINE_STATIC_KEY_MAYBE(CONFIG_DEBUG_VM, check_pages_enabled);

/*
 * Enable static keys related to various memory debugging and hardening options.
 * Some override others, and depend on early params that are evaluated in the
 * order of appearance. So we need to first gather the full picture of what was
 * enabled, and then make decisions.
 */
static void __init mem_debugging_and_hardening_init(void)
{
	bool page_poisoning_requested = false;
	bool want_check_pages = false;

#ifdef CONFIG_PAGE_POISONING
	/*
	 * Page poisoning is debug page alloc for some arches. If
	 * either of those options are enabled, enable poisoning.
	 */
	if (page_poisoning_enabled() ||
	     (!IS_ENABLED(CONFIG_ARCH_SUPPORTS_DEBUG_PAGEALLOC) &&
	      debug_pagealloc_enabled())) {
		static_branch_enable(&_page_poisoning_enabled);
		page_poisoning_requested = true;
		want_check_pages = true;
	}
#endif

	if ((_init_on_alloc_enabled_early || _init_on_free_enabled_early) &&
	    page_poisoning_requested) {
		pr_info("mem auto-init: CONFIG_PAGE_POISONING is on, "
			"will take precedence over init_on_alloc and init_on_free\n");
		_init_on_alloc_enabled_early = false;
		_init_on_free_enabled_early = false;
	}

	if (_init_on_alloc_enabled_early) {
		want_check_pages = true;
		static_branch_enable(&init_on_alloc);
	} else {
		static_branch_disable(&init_on_alloc);
	}

	if (_init_on_free_enabled_early) {
		want_check_pages = true;
		static_branch_enable(&init_on_free);
	} else {
		static_branch_disable(&init_on_free);
	}

	if (IS_ENABLED(CONFIG_KMSAN) &&
	    (_init_on_alloc_enabled_early || _init_on_free_enabled_early))
		pr_info("mem auto-init: please make sure init_on_alloc and init_on_free are disabled when running KMSAN\n");

#ifdef CONFIG_DEBUG_PAGEALLOC
	if (debug_pagealloc_enabled()) {
		want_check_pages = true;
		static_branch_enable(&_debug_pagealloc_enabled);

		if (debug_guardpage_minorder())
			static_branch_enable(&_debug_guardpage_enabled);
	}
#endif

	/*
	 * Any page debugging or hardening option also enables sanity checking
	 * of struct pages being allocated or freed. With CONFIG_DEBUG_VM it's
	 * enabled already.
	 */
	if (!IS_ENABLED(CONFIG_DEBUG_VM) && want_check_pages)
		static_branch_enable(&check_pages_enabled);
}

/* Report memory auto-initialization states for this boot. */
static void __init report_meminit(void)
{
	const char *stack;

	if (IS_ENABLED(CONFIG_INIT_STACK_ALL_PATTERN))
		stack = "all(pattern)";
	else if (IS_ENABLED(CONFIG_INIT_STACK_ALL_ZERO))
		stack = "all(zero)";
	else if (IS_ENABLED(CONFIG_GCC_PLUGIN_STRUCTLEAK_BYREF_ALL))
		stack = "byref_all(zero)";
	else if (IS_ENABLED(CONFIG_GCC_PLUGIN_STRUCTLEAK_BYREF))
		stack = "byref(zero)";
	else if (IS_ENABLED(CONFIG_GCC_PLUGIN_STRUCTLEAK_USER))
		stack = "__user(zero)";
	else
		stack = "off";

	pr_info("mem auto-init: stack:%s, heap alloc:%s, heap free:%s\n",
		stack, str_on_off(want_init_on_alloc(GFP_KERNEL)),
		str_on_off(want_init_on_free()));
	if (want_init_on_free())
		pr_info("mem auto-init: clearing system memory may take some time...\n");
}

static void __init mem_init_print_info(void)
{
	unsigned long physpages, codesize, datasize, rosize, bss_size;
	unsigned long init_code_size, init_data_size;

	physpages = get_num_physpages();
	codesize = _etext - _stext;
	datasize = _edata - _sdata;
	rosize = __end_rodata - __start_rodata;
	bss_size = __bss_stop - __bss_start;
	init_data_size = __init_end - __init_begin;
	init_code_size = _einittext - _sinittext;

	/*
	 * Detect special cases and adjust section sizes accordingly:
	 * 1) .init.* may be embedded into .data sections
	 * 2) .init.text.* may be out of [__init_begin, __init_end],
	 *    please refer to arch/tile/kernel/vmlinux.lds.S.
	 * 3) .rodata.* may be embedded into .text or .data sections.
	 */
#define adj_init_size(start, end, size, pos, adj) \
	do { \
		if (&start[0] <= &pos[0] && &pos[0] < &end[0] && size > adj) \
			size -= adj; \
	} while (0)

	adj_init_size(__init_begin, __init_end, init_data_size,
		     _sinittext, init_code_size);
	adj_init_size(_stext, _etext, codesize, _sinittext, init_code_size);
	adj_init_size(_sdata, _edata, datasize, __init_begin, init_data_size);
	adj_init_size(_stext, _etext, codesize, __start_rodata, rosize);
	adj_init_size(_sdata, _edata, datasize, __start_rodata, rosize);

#undef	adj_init_size

	pr_info("Memory: %luK/%luK available (%luK kernel code, %luK rwdata, %luK rodata, %luK init, %luK bss, %luK reserved, %luK cma-reserved"
#ifdef	CONFIG_HIGHMEM
		", %luK highmem"
#endif
		")\n",
		K(nr_free_pages()), K(physpages),
		codesize / SZ_1K, datasize / SZ_1K, rosize / SZ_1K,
		(init_data_size + init_code_size) / SZ_1K, bss_size / SZ_1K,
		K(physpages - totalram_pages() - totalcma_pages),
		K(totalcma_pages)
#ifdef	CONFIG_HIGHMEM
		, K(totalhigh_pages())
#endif
		);
}

void __init __weak arch_mm_preinit(void)
{
}

void __init __weak mem_init(void)
{
}

/*
 * Set up kernel memory allocators
 */
void __init mm_core_init(void)
{
	arch_mm_preinit();
	hugetlb_bootmem_alloc();

	/* Initializations relying on SMP setup */
	BUILD_BUG_ON(MAX_ZONELISTS > 2);
	build_all_zonelists(NULL);
	page_alloc_init_cpuhp();
	alloc_tag_sec_init();
	/*
	 * page_ext requires contiguous pages,
	 * bigger than MAX_PAGE_ORDER unless SPARSEMEM.
	 */
	page_ext_init_flatmem();
	mem_debugging_and_hardening_init();
	kfence_alloc_pool_and_metadata();
	report_meminit();
	kmsan_init_shadow();
	stack_depot_early_init();
	memblock_free_all();
	mem_init();
	kmem_cache_init();
	/*
	 * page_owner must be initialized after buddy is ready, and also after
	 * slab is ready so that stack_depot_init() works properly
	 */
	page_ext_init_flatmem_late();
	kmemleak_init();
	ptlock_cache_init();
	pgtable_cache_init();
	debug_objects_mem_init();
	vmalloc_init();
	/* If no deferred init page_ext now, as vmap is fully initialized */
	if (!deferred_struct_pages)
		page_ext_init();
	/* Should be run before the first non-init thread is created */
	init_espfix_bsp();
	/* Should be run after espfix64 is set up. */
	pti_init();
	kmsan_init_runtime();
	mm_cache_init();
	execmem_init();
}
