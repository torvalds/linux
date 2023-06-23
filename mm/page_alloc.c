// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/page_alloc.c
 *
 *  Manages the free list, the system allocates free pages here.
 *  Note that kmalloc() lives in slab.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *  Reshaped it to be a zoned allocator, Ingo Molnar, Red Hat, 1999
 *  Discontiguous memory support, Kanoj Sarcar, SGI, Nov 1999
 *  Zone balancing, Kanoj Sarcar, SGI, Jan 2000
 *  Per cpu hot/cold page lists, bulk allocation, Martin J. Bligh, Sept 2002
 *          (lots of bits borrowed from Ingo Molnar & Andrew Morton)
 */

#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/jiffies.h>
#include <linux/memblock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/kasan.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/oom.h>
#include <linux/topology.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/memory_hotplug.h>
#include <linux/nodemask.h>
#include <linux/vmalloc.h>
#include <linux/vmstat.h>
#include <linux/mempolicy.h>
#include <linux/memremap.h>
#include <linux/stop_machine.h>
#include <linux/random.h>
#include <linux/sort.h>
#include <linux/pfn.h>
#include <linux/backing-dev.h>
#include <linux/fault-inject.h>
#include <linux/page-isolation.h>
#include <linux/debugobjects.h>
#include <linux/kmemleak.h>
#include <linux/compaction.h>
#include <trace/events/kmem.h>
#include <trace/events/oom.h>
#include <linux/prefetch.h>
#include <linux/mm_inline.h>
#include <linux/mmu_notifier.h>
#include <linux/migrate.h>
#include <linux/hugetlb.h>
#include <linux/sched/rt.h>
#include <linux/sched/mm.h>
#include <linux/page_owner.h>
#include <linux/page_table_check.h>
#include <linux/kthread.h>
#include <linux/memcontrol.h>
#include <linux/ftrace.h>
#include <linux/lockdep.h>
#include <linux/nmi.h>
#include <linux/psi.h>
#include <linux/padata.h>
#include <linux/khugepaged.h>
#include <linux/buffer_head.h>
#include <linux/delayacct.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/div64.h>
#include "internal.h"
#include "shuffle.h"
#include "page_reporting.h"
#include "swap.h"

/* Free Page Internal flags: for internal, non-pcp variants of free_pages(). */
typedef int __bitwise fpi_t;

/* No special request */
#define FPI_NONE		((__force fpi_t)0)

/*
 * Skip free page reporting notification for the (possibly merged) page.
 * This does not hinder free page reporting from grabbing the page,
 * reporting it and marking it "reported" -  it only skips notifying
 * the free page reporting infrastructure about a newly freed page. For
 * example, used when temporarily pulling a page from a freelist and
 * putting it back unmodified.
 */
#define FPI_SKIP_REPORT_NOTIFY	((__force fpi_t)BIT(0))

/*
 * Place the (possibly merged) page to the tail of the freelist. Will ignore
 * page shuffling (relevant code - e.g., memory onlining - is expected to
 * shuffle the whole zone).
 *
 * Note: No code should rely on this flag for correctness - it's purely
 *       to allow for optimizations when handing back either fresh pages
 *       (memory onlining) or untouched pages (page isolation, free page
 *       reporting).
 */
#define FPI_TO_TAIL		((__force fpi_t)BIT(1))

/*
 * Don't poison memory with KASAN (only for the tag-based modes).
 * During boot, all non-reserved memblock memory is exposed to page_alloc.
 * Poisoning all that memory lengthens boot time, especially on systems with
 * large amount of RAM. This flag is used to skip that poisoning.
 * This is only done for the tag-based KASAN modes, as those are able to
 * detect memory corruptions with the memory tags assigned by default.
 * All memory allocated normally after boot gets poisoned as usual.
 */
#define FPI_SKIP_KASAN_POISON	((__force fpi_t)BIT(2))

/* prevent >1 _updater_ of zone percpu pageset ->high and ->batch fields */
static DEFINE_MUTEX(pcp_batch_high_lock);
#define MIN_PERCPU_PAGELIST_HIGH_FRACTION (8)

#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT_RT)
/*
 * On SMP, spin_trylock is sufficient protection.
 * On PREEMPT_RT, spin_trylock is equivalent on both SMP and UP.
 */
#define pcp_trylock_prepare(flags)	do { } while (0)
#define pcp_trylock_finish(flag)	do { } while (0)
#else

/* UP spin_trylock always succeeds so disable IRQs to prevent re-entrancy. */
#define pcp_trylock_prepare(flags)	local_irq_save(flags)
#define pcp_trylock_finish(flags)	local_irq_restore(flags)
#endif

/*
 * Locking a pcp requires a PCP lookup followed by a spinlock. To avoid
 * a migration causing the wrong PCP to be locked and remote memory being
 * potentially allocated, pin the task to the CPU for the lookup+lock.
 * preempt_disable is used on !RT because it is faster than migrate_disable.
 * migrate_disable is used on RT because otherwise RT spinlock usage is
 * interfered with and a high priority task cannot preempt the allocator.
 */
#ifndef CONFIG_PREEMPT_RT
#define pcpu_task_pin()		preempt_disable()
#define pcpu_task_unpin()	preempt_enable()
#else
#define pcpu_task_pin()		migrate_disable()
#define pcpu_task_unpin()	migrate_enable()
#endif

/*
 * Generic helper to lookup and a per-cpu variable with an embedded spinlock.
 * Return value should be used with equivalent unlock helper.
 */
#define pcpu_spin_lock(type, member, ptr)				\
({									\
	type *_ret;							\
	pcpu_task_pin();						\
	_ret = this_cpu_ptr(ptr);					\
	spin_lock(&_ret->member);					\
	_ret;								\
})

#define pcpu_spin_lock_irqsave(type, member, ptr, flags)		\
({									\
	type *_ret;							\
	pcpu_task_pin();						\
	_ret = this_cpu_ptr(ptr);					\
	spin_lock_irqsave(&_ret->member, flags);			\
	_ret;								\
})

#define pcpu_spin_trylock_irqsave(type, member, ptr, flags)		\
({									\
	type *_ret;							\
	pcpu_task_pin();						\
	_ret = this_cpu_ptr(ptr);					\
	if (!spin_trylock_irqsave(&_ret->member, flags)) {		\
		pcpu_task_unpin();					\
		_ret = NULL;						\
	}								\
	_ret;								\
})

#define pcpu_spin_unlock(member, ptr)					\
({									\
	spin_unlock(&ptr->member);					\
	pcpu_task_unpin();						\
})

#define pcpu_spin_unlock_irqrestore(member, ptr, flags)			\
({									\
	spin_unlock_irqrestore(&ptr->member, flags);			\
	pcpu_task_unpin();						\
})

/* struct per_cpu_pages specific helpers. */
#define pcp_spin_lock(ptr)						\
	pcpu_spin_lock(struct per_cpu_pages, lock, ptr)

#define pcp_spin_lock_irqsave(ptr, flags)				\
	pcpu_spin_lock_irqsave(struct per_cpu_pages, lock, ptr, flags)

#define pcp_spin_trylock_irqsave(ptr, flags)				\
	pcpu_spin_trylock_irqsave(struct per_cpu_pages, lock, ptr, flags)

#define pcp_spin_unlock(ptr)						\
	pcpu_spin_unlock(lock, ptr)

#define pcp_spin_unlock_irqrestore(ptr, flags)				\
	pcpu_spin_unlock_irqrestore(lock, ptr, flags)
#ifdef CONFIG_USE_PERCPU_NUMA_NODE_ID
DEFINE_PER_CPU(int, numa_node);
EXPORT_PER_CPU_SYMBOL(numa_node);
#endif

DEFINE_STATIC_KEY_TRUE(vm_numa_stat_key);

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
/*
 * N.B., Do NOT reference the '_numa_mem_' per cpu variable directly.
 * It will not be defined when CONFIG_HAVE_MEMORYLESS_NODES is not defined.
 * Use the accessor functions set_numa_mem(), numa_mem_id() and cpu_to_mem()
 * defined in <linux/topology.h>.
 */
DEFINE_PER_CPU(int, _numa_mem_);		/* Kernel "local memory" node */
EXPORT_PER_CPU_SYMBOL(_numa_mem_);
#endif

static DEFINE_MUTEX(pcpu_drain_mutex);

#ifdef CONFIG_GCC_PLUGIN_LATENT_ENTROPY
volatile unsigned long latent_entropy __latent_entropy;
EXPORT_SYMBOL(latent_entropy);
#endif

/*
 * Array of node states.
 */
nodemask_t node_states[NR_NODE_STATES] __read_mostly = {
	[N_POSSIBLE] = NODE_MASK_ALL,
	[N_ONLINE] = { { [0] = 1UL } },
#ifndef CONFIG_NUMA
	[N_NORMAL_MEMORY] = { { [0] = 1UL } },
#ifdef CONFIG_HIGHMEM
	[N_HIGH_MEMORY] = { { [0] = 1UL } },
#endif
	[N_MEMORY] = { { [0] = 1UL } },
	[N_CPU] = { { [0] = 1UL } },
#endif	/* NUMA */
};
EXPORT_SYMBOL(node_states);

atomic_long_t _totalram_pages __read_mostly;
EXPORT_SYMBOL(_totalram_pages);
unsigned long totalreserve_pages __read_mostly;
unsigned long totalcma_pages __read_mostly;

int percpu_pagelist_high_fraction;
gfp_t gfp_allowed_mask __read_mostly = GFP_BOOT_MASK;
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

/*
 * A cached value of the page's pageblock's migratetype, used when the page is
 * put on a pcplist. Used to avoid the pageblock migratetype lookup when
 * freeing from pcplists in most cases, at the cost of possibly becoming stale.
 * Also the migratetype set in the page does not necessarily match the pcplist
 * index, e.g. page might have MIGRATE_CMA set but be on a pcplist with any
 * other index - this ensures that it will be put on the correct CMA freelist.
 */
static inline int get_pcppage_migratetype(struct page *page)
{
	return page->index;
}

static inline void set_pcppage_migratetype(struct page *page, int migratetype)
{
	page->index = migratetype;
}

#ifdef CONFIG_PM_SLEEP
/*
 * The following functions are used by the suspend/hibernate code to temporarily
 * change gfp_allowed_mask in order to avoid using I/O during memory allocations
 * while devices are suspended.  To avoid races with the suspend/hibernate code,
 * they should always be called with system_transition_mutex held
 * (gfp_allowed_mask also should only be modified with system_transition_mutex
 * held, unless the suspend/hibernate code is guaranteed not to run in parallel
 * with that modification).
 */

static gfp_t saved_gfp_mask;

void pm_restore_gfp_mask(void)
{
	WARN_ON(!mutex_is_locked(&system_transition_mutex));
	if (saved_gfp_mask) {
		gfp_allowed_mask = saved_gfp_mask;
		saved_gfp_mask = 0;
	}
}

void pm_restrict_gfp_mask(void)
{
	WARN_ON(!mutex_is_locked(&system_transition_mutex));
	WARN_ON(saved_gfp_mask);
	saved_gfp_mask = gfp_allowed_mask;
	gfp_allowed_mask &= ~(__GFP_IO | __GFP_FS);
}

bool pm_suspended_storage(void)
{
	if ((gfp_allowed_mask & (__GFP_IO | __GFP_FS)) == (__GFP_IO | __GFP_FS))
		return false;
	return true;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_HUGETLB_PAGE_SIZE_VARIABLE
unsigned int pageblock_order __read_mostly;
#endif

static void __free_pages_ok(struct page *page, unsigned int order,
			    fpi_t fpi_flags);

/*
 * results with 256, 32 in the lowmem_reserve sysctl:
 *	1G machine -> (16M dma, 800M-16M normal, 1G-800M high)
 *	1G machine -> (16M dma, 784M normal, 224M high)
 *	NORMAL allocation will leave 784M/256 of ram reserved in the ZONE_DMA
 *	HIGHMEM allocation will leave 224M/32 of ram reserved in ZONE_NORMAL
 *	HIGHMEM allocation will leave (224M+784M)/256 of ram reserved in ZONE_DMA
 *
 * TBD: should special case ZONE_DMA32 machines here - in those we normally
 * don't need any ZONE_NORMAL reservation
 */
int sysctl_lowmem_reserve_ratio[MAX_NR_ZONES] = {
#ifdef CONFIG_ZONE_DMA
	[ZONE_DMA] = 256,
#endif
#ifdef CONFIG_ZONE_DMA32
	[ZONE_DMA32] = 256,
#endif
	[ZONE_NORMAL] = 32,
#ifdef CONFIG_HIGHMEM
	[ZONE_HIGHMEM] = 0,
#endif
	[ZONE_MOVABLE] = 0,
};

static char * const zone_names[MAX_NR_ZONES] = {
#ifdef CONFIG_ZONE_DMA
	 "DMA",
#endif
#ifdef CONFIG_ZONE_DMA32
	 "DMA32",
#endif
	 "Normal",
#ifdef CONFIG_HIGHMEM
	 "HighMem",
#endif
	 "Movable",
#ifdef CONFIG_ZONE_DEVICE
	 "Device",
#endif
};

const char * const migratetype_names[MIGRATE_TYPES] = {
	"Unmovable",
	"Movable",
	"Reclaimable",
	"HighAtomic",
#ifdef CONFIG_CMA
	"CMA",
#endif
#ifdef CONFIG_MEMORY_ISOLATION
	"Isolate",
#endif
};

compound_page_dtor * const compound_page_dtors[NR_COMPOUND_DTORS] = {
	[NULL_COMPOUND_DTOR] = NULL,
	[COMPOUND_PAGE_DTOR] = free_compound_page,
#ifdef CONFIG_HUGETLB_PAGE
	[HUGETLB_PAGE_DTOR] = free_huge_page,
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	[TRANSHUGE_PAGE_DTOR] = free_transhuge_page,
#endif
};

int min_free_kbytes = 1024;
int user_min_free_kbytes = -1;
int watermark_boost_factor __read_mostly = 15000;
int watermark_scale_factor = 10;

static unsigned long nr_kernel_pages __initdata;
static unsigned long nr_all_pages __initdata;
static unsigned long dma_reserve __initdata;

static unsigned long arch_zone_lowest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long arch_zone_highest_possible_pfn[MAX_NR_ZONES] __initdata;
static unsigned long required_kernelcore __initdata;
static unsigned long required_kernelcore_percent __initdata;
static unsigned long required_movablecore __initdata;
static unsigned long required_movablecore_percent __initdata;
static unsigned long zone_movable_pfn[MAX_NUMNODES] __initdata;
bool mirrored_kernelcore __initdata_memblock;

/* movable_zone is the "real" zone pages in ZONE_MOVABLE are taken from */
int movable_zone;
EXPORT_SYMBOL(movable_zone);

#if MAX_NUMNODES > 1
unsigned int nr_node_ids __read_mostly = MAX_NUMNODES;
unsigned int nr_online_nodes __read_mostly = 1;
EXPORT_SYMBOL(nr_node_ids);
EXPORT_SYMBOL(nr_online_nodes);
#endif

int page_group_by_mobility_disabled __read_mostly;

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
/*
 * During boot we initialize deferred pages on-demand, as needed, but once
 * page_alloc_init_late() has finished, the deferred pages are all initialized,
 * and we can permanently disable that path.
 */
static DEFINE_STATIC_KEY_TRUE(deferred_pages);

static inline bool deferred_pages_enabled(void)
{
	return static_branch_unlikely(&deferred_pages);
}

/* Returns true if the struct page for the pfn is uninitialised */
static inline bool __meminit early_page_uninitialised(unsigned long pfn)
{
	int nid = early_pfn_to_nid(pfn);

	if (node_online(nid) && pfn >= NODE_DATA(nid)->first_deferred_pfn)
		return true;

	return false;
}

/*
 * Returns true when the remaining initialisation should be deferred until
 * later in the boot cycle when it can be parallelised.
 */
static bool __meminit
defer_init(int nid, unsigned long pfn, unsigned long end_pfn)
{
	static unsigned long prev_end_pfn, nr_initialised;

	/*
	 * prev_end_pfn static that contains the end of previous zone
	 * No need to protect because called very early in boot before smp_init.
	 */
	if (prev_end_pfn != end_pfn) {
		prev_end_pfn = end_pfn;
		nr_initialised = 0;
	}

	/* Always populate low zones for address-constrained allocations */
	if (end_pfn < pgdat_end_pfn(NODE_DATA(nid)))
		return false;

	if (NODE_DATA(nid)->first_deferred_pfn != ULONG_MAX)
		return true;
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
#else
static inline bool deferred_pages_enabled(void)
{
	return false;
}

static inline bool early_page_uninitialised(unsigned long pfn)
{
	return false;
}

static inline bool defer_init(int nid, unsigned long pfn, unsigned long end_pfn)
{
	return false;
}
#endif

/* Return a pointer to the bitmap storing bits affecting a block of pages */
static inline unsigned long *get_pageblock_bitmap(const struct page *page,
							unsigned long pfn)
{
#ifdef CONFIG_SPARSEMEM
	return section_to_usemap(__pfn_to_section(pfn));
#else
	return page_zone(page)->pageblock_flags;
#endif /* CONFIG_SPARSEMEM */
}

static inline int pfn_to_bitidx(const struct page *page, unsigned long pfn)
{
#ifdef CONFIG_SPARSEMEM
	pfn &= (PAGES_PER_SECTION-1);
#else
	pfn = pfn - round_down(page_zone(page)->zone_start_pfn, pageblock_nr_pages);
#endif /* CONFIG_SPARSEMEM */
	return (pfn >> pageblock_order) * NR_PAGEBLOCK_BITS;
}

static __always_inline
unsigned long __get_pfnblock_flags_mask(const struct page *page,
					unsigned long pfn,
					unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long word;

	bitmap = get_pageblock_bitmap(page, pfn);
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG-1);
	/*
	 * This races, without locks, with set_pfnblock_flags_mask(). Ensure
	 * a consistent read of the memory array, so that results, even though
	 * racy, are not corrupted.
	 */
	word = READ_ONCE(bitmap[word_bitidx]);
	return (word >> bitidx) & mask;
}

/**
 * get_pfnblock_flags_mask - Return the requested group of flags for the pageblock_nr_pages block of pages
 * @page: The page within the block of interest
 * @pfn: The target page frame number
 * @mask: mask of bits that the caller is interested in
 *
 * Return: pageblock_bits flags
 */
unsigned long get_pfnblock_flags_mask(const struct page *page,
					unsigned long pfn, unsigned long mask)
{
	return __get_pfnblock_flags_mask(page, pfn, mask);
}

static __always_inline int get_pfnblock_migratetype(const struct page *page,
					unsigned long pfn)
{
	return __get_pfnblock_flags_mask(page, pfn, MIGRATETYPE_MASK);
}

/**
 * set_pfnblock_flags_mask - Set the requested group of flags for a pageblock_nr_pages block of pages
 * @page: The page within the block of interest
 * @flags: The flags to set
 * @pfn: The target page frame number
 * @mask: mask of bits that the caller is interested in
 */
void set_pfnblock_flags_mask(struct page *page, unsigned long flags,
					unsigned long pfn,
					unsigned long mask)
{
	unsigned long *bitmap;
	unsigned long bitidx, word_bitidx;
	unsigned long word;

	BUILD_BUG_ON(NR_PAGEBLOCK_BITS != 4);
	BUILD_BUG_ON(MIGRATE_TYPES > (1 << PB_migratetype_bits));

	bitmap = get_pageblock_bitmap(page, pfn);
	bitidx = pfn_to_bitidx(page, pfn);
	word_bitidx = bitidx / BITS_PER_LONG;
	bitidx &= (BITS_PER_LONG-1);

	VM_BUG_ON_PAGE(!zone_spans_pfn(page_zone(page), pfn), page);

	mask <<= bitidx;
	flags <<= bitidx;

	word = READ_ONCE(bitmap[word_bitidx]);
	do {
	} while (!try_cmpxchg(&bitmap[word_bitidx], &word, (word & ~mask) | flags));
}

void set_pageblock_migratetype(struct page *page, int migratetype)
{
	if (unlikely(page_group_by_mobility_disabled &&
		     migratetype < MIGRATE_PCPTYPES))
		migratetype = MIGRATE_UNMOVABLE;

	set_pfnblock_flags_mask(page, (unsigned long)migratetype,
				page_to_pfn(page), MIGRATETYPE_MASK);
}

#ifdef CONFIG_DEBUG_VM
static int page_outside_zone_boundaries(struct zone *zone, struct page *page)
{
	int ret = 0;
	unsigned seq;
	unsigned long pfn = page_to_pfn(page);
	unsigned long sp, start_pfn;

	do {
		seq = zone_span_seqbegin(zone);
		start_pfn = zone->zone_start_pfn;
		sp = zone->spanned_pages;
		if (!zone_spans_pfn(zone, pfn))
			ret = 1;
	} while (zone_span_seqretry(zone, seq));

	if (ret)
		pr_err("page 0x%lx outside node %d zone %s [ 0x%lx - 0x%lx ]\n",
			pfn, zone_to_nid(zone), zone->name,
			start_pfn, start_pfn + sp);

	return ret;
}

static int page_is_consistent(struct zone *zone, struct page *page)
{
	if (zone != page_zone(page))
		return 0;

	return 1;
}
/*
 * Temporary debugging check for pages not lying within a given zone.
 */
static int __maybe_unused bad_range(struct zone *zone, struct page *page)
{
	if (page_outside_zone_boundaries(zone, page))
		return 1;
	if (!page_is_consistent(zone, page))
		return 1;

	return 0;
}
#else
static inline int __maybe_unused bad_range(struct zone *zone, struct page *page)
{
	return 0;
}
#endif

static void bad_page(struct page *page, const char *reason)
{
	static unsigned long resume;
	static unsigned long nr_shown;
	static unsigned long nr_unshown;

	/*
	 * Allow a burst of 60 reports, then keep quiet for that minute;
	 * or allow a steady drip of one report per second.
	 */
	if (nr_shown == 60) {
		if (time_before(jiffies, resume)) {
			nr_unshown++;
			goto out;
		}
		if (nr_unshown) {
			pr_alert(
			      "BUG: Bad page state: %lu messages suppressed\n",
				nr_unshown);
			nr_unshown = 0;
		}
		nr_shown = 0;
	}
	if (nr_shown++ == 0)
		resume = jiffies + 60 * HZ;

	pr_alert("BUG: Bad page state in process %s  pfn:%05lx\n",
		current->comm, page_to_pfn(page));
	dump_page(page, reason);

	print_modules();
	dump_stack();
out:
	/* Leave bad fields for debug, except PageBuddy could make trouble */
	page_mapcount_reset(page); /* remove PageBuddy */
	add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE);
}

static inline unsigned int order_to_pindex(int migratetype, int order)
{
	int base = order;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (order > PAGE_ALLOC_COSTLY_ORDER) {
		VM_BUG_ON(order != pageblock_order);
		return NR_LOWORDER_PCP_LISTS;
	}
#else
	VM_BUG_ON(order > PAGE_ALLOC_COSTLY_ORDER);
#endif

	return (MIGRATE_PCPTYPES * base) + migratetype;
}

static inline int pindex_to_order(unsigned int pindex)
{
	int order = pindex / MIGRATE_PCPTYPES;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (pindex == NR_LOWORDER_PCP_LISTS)
		order = pageblock_order;
#else
	VM_BUG_ON(order > PAGE_ALLOC_COSTLY_ORDER);
#endif

	return order;
}

static inline bool pcp_allowed_order(unsigned int order)
{
	if (order <= PAGE_ALLOC_COSTLY_ORDER)
		return true;
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	if (order == pageblock_order)
		return true;
#endif
	return false;
}

static inline void free_the_page(struct page *page, unsigned int order)
{
	if (pcp_allowed_order(order))		/* Via pcp? */
		free_unref_page(page, order);
	else
		__free_pages_ok(page, order, FPI_NONE);
}

/*
 * Higher-order pages are called "compound pages".  They are structured thusly:
 *
 * The first PAGE_SIZE page is called the "head page" and have PG_head set.
 *
 * The remaining PAGE_SIZE pages are called "tail pages". PageTail() is encoded
 * in bit 0 of page->compound_head. The rest of bits is pointer to head page.
 *
 * The first tail page's ->compound_dtor holds the offset in array of compound
 * page destructors. See compound_page_dtors.
 *
 * The first tail page's ->compound_order holds the order of allocation.
 * This usage means that zero-order pages may not be compound.
 */

void free_compound_page(struct page *page)
{
	mem_cgroup_uncharge(page_folio(page));
	free_the_page(page, compound_order(page));
}

static void prep_compound_head(struct page *page, unsigned int order)
{
	set_compound_page_dtor(page, COMPOUND_PAGE_DTOR);
	set_compound_order(page, order);
	atomic_set(compound_mapcount_ptr(page), -1);
	atomic_set(compound_pincount_ptr(page), 0);
}

static void prep_compound_tail(struct page *head, int tail_idx)
{
	struct page *p = head + tail_idx;

	p->mapping = TAIL_MAPPING;
	set_compound_head(p, head);
}

void prep_compound_page(struct page *page, unsigned int order)
{
	int i;
	int nr_pages = 1 << order;

	__SetPageHead(page);
	for (i = 1; i < nr_pages; i++)
		prep_compound_tail(page, i);

	prep_compound_head(page, order);
}

void destroy_large_folio(struct folio *folio)
{
	enum compound_dtor_id dtor = folio_page(folio, 1)->compound_dtor;

	VM_BUG_ON_FOLIO(dtor >= NR_COMPOUND_DTORS, folio);
	compound_page_dtors[dtor](&folio->page);
}

#ifdef CONFIG_DEBUG_PAGEALLOC
unsigned int _debug_guardpage_minorder;

bool _debug_pagealloc_enabled_early __read_mostly
			= IS_ENABLED(CONFIG_DEBUG_PAGEALLOC_ENABLE_DEFAULT);
EXPORT_SYMBOL(_debug_pagealloc_enabled_early);
DEFINE_STATIC_KEY_FALSE(_debug_pagealloc_enabled);
EXPORT_SYMBOL(_debug_pagealloc_enabled);

DEFINE_STATIC_KEY_FALSE(_debug_guardpage_enabled);

static int __init early_debug_pagealloc(char *buf)
{
	return kstrtobool(buf, &_debug_pagealloc_enabled_early);
}
early_param("debug_pagealloc", early_debug_pagealloc);

static int __init debug_guardpage_minorder_setup(char *buf)
{
	unsigned long res;

	if (kstrtoul(buf, 10, &res) < 0 ||  res > MAX_ORDER / 2) {
		pr_err("Bad debug_guardpage_minorder value\n");
		return 0;
	}
	_debug_guardpage_minorder = res;
	pr_info("Setting debug_guardpage_minorder to %lu\n", res);
	return 0;
}
early_param("debug_guardpage_minorder", debug_guardpage_minorder_setup);

static inline bool set_page_guard(struct zone *zone, struct page *page,
				unsigned int order, int migratetype)
{
	if (!debug_guardpage_enabled())
		return false;

	if (order >= debug_guardpage_minorder())
		return false;

	__SetPageGuard(page);
	INIT_LIST_HEAD(&page->buddy_list);
	set_page_private(page, order);
	/* Guard pages are not available for any usage */
	__mod_zone_freepage_state(zone, -(1 << order), migratetype);

	return true;
}

static inline void clear_page_guard(struct zone *zone, struct page *page,
				unsigned int order, int migratetype)
{
	if (!debug_guardpage_enabled())
		return;

	__ClearPageGuard(page);

	set_page_private(page, 0);
	if (!is_migrate_isolate(migratetype))
		__mod_zone_freepage_state(zone, (1 << order), migratetype);
}
#else
static inline bool set_page_guard(struct zone *zone, struct page *page,
			unsigned int order, int migratetype) { return false; }
static inline void clear_page_guard(struct zone *zone, struct page *page,
				unsigned int order, int migratetype) {}
#endif

/*
 * Enable static keys related to various memory debugging and hardening options.
 * Some override others, and depend on early params that are evaluated in the
 * order of appearance. So we need to first gather the full picture of what was
 * enabled, and then make decisions.
 */
void init_mem_debugging_and_hardening(void)
{
	bool page_poisoning_requested = false;

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
	}
#endif

	if ((_init_on_alloc_enabled_early || _init_on_free_enabled_early) &&
	    page_poisoning_requested) {
		pr_info("mem auto-init: CONFIG_PAGE_POISONING is on, "
			"will take precedence over init_on_alloc and init_on_free\n");
		_init_on_alloc_enabled_early = false;
		_init_on_free_enabled_early = false;
	}

	if (_init_on_alloc_enabled_early)
		static_branch_enable(&init_on_alloc);
	else
		static_branch_disable(&init_on_alloc);

	if (_init_on_free_enabled_early)
		static_branch_enable(&init_on_free);
	else
		static_branch_disable(&init_on_free);

#ifdef CONFIG_DEBUG_PAGEALLOC
	if (!debug_pagealloc_enabled())
		return;

	static_branch_enable(&_debug_pagealloc_enabled);

	if (!debug_guardpage_minorder())
		return;

	static_branch_enable(&_debug_guardpage_enabled);
#endif
}

static inline void set_buddy_order(struct page *page, unsigned int order)
{
	set_page_private(page, order);
	__SetPageBuddy(page);
}

#ifdef CONFIG_COMPACTION
static inline struct capture_control *task_capc(struct zone *zone)
{
	struct capture_control *capc = current->capture_control;

	return unlikely(capc) &&
		!(current->flags & PF_KTHREAD) &&
		!capc->page &&
		capc->cc->zone == zone ? capc : NULL;
}

static inline bool
compaction_capture(struct capture_control *capc, struct page *page,
		   int order, int migratetype)
{
	if (!capc || order != capc->cc->order)
		return false;

	/* Do not accidentally pollute CMA or isolated regions*/
	if (is_migrate_cma(migratetype) ||
	    is_migrate_isolate(migratetype))
		return false;

	/*
	 * Do not let lower order allocations pollute a movable pageblock.
	 * This might let an unmovable request use a reclaimable pageblock
	 * and vice-versa but no more than normal fallback logic which can
	 * have trouble finding a high-order free page.
	 */
	if (order < pageblock_order && migratetype == MIGRATE_MOVABLE)
		return false;

	capc->page = page;
	return true;
}

#else
static inline struct capture_control *task_capc(struct zone *zone)
{
	return NULL;
}

static inline bool
compaction_capture(struct capture_control *capc, struct page *page,
		   int order, int migratetype)
{
	return false;
}
#endif /* CONFIG_COMPACTION */

/* Used for pages not on another list */
static inline void add_to_free_list(struct page *page, struct zone *zone,
				    unsigned int order, int migratetype)
{
	struct free_area *area = &zone->free_area[order];

	list_add(&page->buddy_list, &area->free_list[migratetype]);
	area->nr_free++;
}

/* Used for pages not on another list */
static inline void add_to_free_list_tail(struct page *page, struct zone *zone,
					 unsigned int order, int migratetype)
{
	struct free_area *area = &zone->free_area[order];

	list_add_tail(&page->buddy_list, &area->free_list[migratetype]);
	area->nr_free++;
}

/*
 * Used for pages which are on another list. Move the pages to the tail
 * of the list - so the moved pages won't immediately be considered for
 * allocation again (e.g., optimization for memory onlining).
 */
static inline void move_to_free_list(struct page *page, struct zone *zone,
				     unsigned int order, int migratetype)
{
	struct free_area *area = &zone->free_area[order];

	list_move_tail(&page->buddy_list, &area->free_list[migratetype]);
}

static inline void del_page_from_free_list(struct page *page, struct zone *zone,
					   unsigned int order)
{
	/* clear reported state and update reported page count */
	if (page_reported(page))
		__ClearPageReported(page);

	list_del(&page->buddy_list);
	__ClearPageBuddy(page);
	set_page_private(page, 0);
	zone->free_area[order].nr_free--;
}

/*
 * If this is not the largest possible page, check if the buddy
 * of the next-highest order is free. If it is, it's possible
 * that pages are being freed that will coalesce soon. In case,
 * that is happening, add the free page to the tail of the list
 * so it's less likely to be used soon and more likely to be merged
 * as a higher order page
 */
static inline bool
buddy_merge_likely(unsigned long pfn, unsigned long buddy_pfn,
		   struct page *page, unsigned int order)
{
	unsigned long higher_page_pfn;
	struct page *higher_page;

	if (order >= MAX_ORDER - 2)
		return false;

	higher_page_pfn = buddy_pfn & pfn;
	higher_page = page + (higher_page_pfn - pfn);

	return find_buddy_page_pfn(higher_page, higher_page_pfn, order + 1,
			NULL) != NULL;
}

/*
 * Freeing function for a buddy system allocator.
 *
 * The concept of a buddy system is to maintain direct-mapped table
 * (containing bit values) for memory blocks of various "orders".
 * The bottom level table contains the map for the smallest allocatable
 * units of memory (here, pages), and each level above it describes
 * pairs of units from the levels below, hence, "buddies".
 * At a high level, all that happens here is marking the table entry
 * at the bottom level available, and propagating the changes upward
 * as necessary, plus some accounting needed to play nicely with other
 * parts of the VM system.
 * At each level, we keep a list of pages, which are heads of continuous
 * free pages of length of (1 << order) and marked with PageBuddy.
 * Page's order is recorded in page_private(page) field.
 * So when we are allocating or freeing one, we can derive the state of the
 * other.  That is, if we allocate a small block, and both were
 * free, the remainder of the region must be split into blocks.
 * If a block is freed, and its buddy is also free, then this
 * triggers coalescing into a block of larger size.
 *
 * -- nyc
 */

static inline void __free_one_page(struct page *page,
		unsigned long pfn,
		struct zone *zone, unsigned int order,
		int migratetype, fpi_t fpi_flags)
{
	struct capture_control *capc = task_capc(zone);
	unsigned long buddy_pfn;
	unsigned long combined_pfn;
	struct page *buddy;
	bool to_tail;

	VM_BUG_ON(!zone_is_initialized(zone));
	VM_BUG_ON_PAGE(page->flags & PAGE_FLAGS_CHECK_AT_PREP, page);

	VM_BUG_ON(migratetype == -1);
	if (likely(!is_migrate_isolate(migratetype)))
		__mod_zone_freepage_state(zone, 1 << order, migratetype);

	VM_BUG_ON_PAGE(pfn & ((1 << order) - 1), page);
	VM_BUG_ON_PAGE(bad_range(zone, page), page);

	while (order < MAX_ORDER - 1) {
		if (compaction_capture(capc, page, order, migratetype)) {
			__mod_zone_freepage_state(zone, -(1 << order),
								migratetype);
			return;
		}

		buddy = find_buddy_page_pfn(page, pfn, order, &buddy_pfn);
		if (!buddy)
			goto done_merging;

		if (unlikely(order >= pageblock_order)) {
			/*
			 * We want to prevent merge between freepages on pageblock
			 * without fallbacks and normal pageblock. Without this,
			 * pageblock isolation could cause incorrect freepage or CMA
			 * accounting or HIGHATOMIC accounting.
			 */
			int buddy_mt = get_pageblock_migratetype(buddy);

			if (migratetype != buddy_mt
					&& (!migratetype_is_mergeable(migratetype) ||
						!migratetype_is_mergeable(buddy_mt)))
				goto done_merging;
		}

		/*
		 * Our buddy is free or it is CONFIG_DEBUG_PAGEALLOC guard page,
		 * merge with it and move up one order.
		 */
		if (page_is_guard(buddy))
			clear_page_guard(zone, buddy, order, migratetype);
		else
			del_page_from_free_list(buddy, zone, order);
		combined_pfn = buddy_pfn & pfn;
		page = page + (combined_pfn - pfn);
		pfn = combined_pfn;
		order++;
	}

done_merging:
	set_buddy_order(page, order);

	if (fpi_flags & FPI_TO_TAIL)
		to_tail = true;
	else if (is_shuffle_order(order))
		to_tail = shuffle_pick_tail();
	else
		to_tail = buddy_merge_likely(pfn, buddy_pfn, page, order);

	if (to_tail)
		add_to_free_list_tail(page, zone, order, migratetype);
	else
		add_to_free_list(page, zone, order, migratetype);

	/* Notify page reporting subsystem of freed page */
	if (!(fpi_flags & FPI_SKIP_REPORT_NOTIFY))
		page_reporting_notify_free(order);
}

/**
 * split_free_page() -- split a free page at split_pfn_offset
 * @free_page:		the original free page
 * @order:		the order of the page
 * @split_pfn_offset:	split offset within the page
 *
 * Return -ENOENT if the free page is changed, otherwise 0
 *
 * It is used when the free page crosses two pageblocks with different migratetypes
 * at split_pfn_offset within the page. The split free page will be put into
 * separate migratetype lists afterwards. Otherwise, the function achieves
 * nothing.
 */
int split_free_page(struct page *free_page,
			unsigned int order, unsigned long split_pfn_offset)
{
	struct zone *zone = page_zone(free_page);
	unsigned long free_page_pfn = page_to_pfn(free_page);
	unsigned long pfn;
	unsigned long flags;
	int free_page_order;
	int mt;
	int ret = 0;

	if (split_pfn_offset == 0)
		return ret;

	spin_lock_irqsave(&zone->lock, flags);

	if (!PageBuddy(free_page) || buddy_order(free_page) != order) {
		ret = -ENOENT;
		goto out;
	}

	mt = get_pageblock_migratetype(free_page);
	if (likely(!is_migrate_isolate(mt)))
		__mod_zone_freepage_state(zone, -(1UL << order), mt);

	del_page_from_free_list(free_page, zone, order);
	for (pfn = free_page_pfn;
	     pfn < free_page_pfn + (1UL << order);) {
		int mt = get_pfnblock_migratetype(pfn_to_page(pfn), pfn);

		free_page_order = min_t(unsigned int,
					pfn ? __ffs(pfn) : order,
					__fls(split_pfn_offset));
		__free_one_page(pfn_to_page(pfn), pfn, zone, free_page_order,
				mt, FPI_NONE);
		pfn += 1UL << free_page_order;
		split_pfn_offset -= (1UL << free_page_order);
		/* we have done the first part, now switch to second part */
		if (split_pfn_offset == 0)
			split_pfn_offset = (1UL << order) - (pfn - free_page_pfn);
	}
out:
	spin_unlock_irqrestore(&zone->lock, flags);
	return ret;
}
/*
 * A bad page could be due to a number of fields. Instead of multiple branches,
 * try and check multiple fields with one check. The caller must do a detailed
 * check if necessary.
 */
static inline bool page_expected_state(struct page *page,
					unsigned long check_flags)
{
	if (unlikely(atomic_read(&page->_mapcount) != -1))
		return false;

	if (unlikely((unsigned long)page->mapping |
			page_ref_count(page) |
#ifdef CONFIG_MEMCG
			page->memcg_data |
#endif
			(page->flags & check_flags)))
		return false;

	return true;
}

static const char *page_bad_reason(struct page *page, unsigned long flags)
{
	const char *bad_reason = NULL;

	if (unlikely(atomic_read(&page->_mapcount) != -1))
		bad_reason = "nonzero mapcount";
	if (unlikely(page->mapping != NULL))
		bad_reason = "non-NULL mapping";
	if (unlikely(page_ref_count(page) != 0))
		bad_reason = "nonzero _refcount";
	if (unlikely(page->flags & flags)) {
		if (flags == PAGE_FLAGS_CHECK_AT_PREP)
			bad_reason = "PAGE_FLAGS_CHECK_AT_PREP flag(s) set";
		else
			bad_reason = "PAGE_FLAGS_CHECK_AT_FREE flag(s) set";
	}
#ifdef CONFIG_MEMCG
	if (unlikely(page->memcg_data))
		bad_reason = "page still charged to cgroup";
#endif
	return bad_reason;
}

static void check_free_page_bad(struct page *page)
{
	bad_page(page,
		 page_bad_reason(page, PAGE_FLAGS_CHECK_AT_FREE));
}

static inline int check_free_page(struct page *page)
{
	if (likely(page_expected_state(page, PAGE_FLAGS_CHECK_AT_FREE)))
		return 0;

	/* Something has gone sideways, find it */
	check_free_page_bad(page);
	return 1;
}

static int free_tail_pages_check(struct page *head_page, struct page *page)
{
	int ret = 1;

	/*
	 * We rely page->lru.next never has bit 0 set, unless the page
	 * is PageTail(). Let's make sure that's true even for poisoned ->lru.
	 */
	BUILD_BUG_ON((unsigned long)LIST_POISON1 & 1);

	if (!IS_ENABLED(CONFIG_DEBUG_VM)) {
		ret = 0;
		goto out;
	}
	switch (page - head_page) {
	case 1:
		/* the first tail page: ->mapping may be compound_mapcount() */
		if (unlikely(compound_mapcount(page))) {
			bad_page(page, "nonzero compound_mapcount");
			goto out;
		}
		break;
	case 2:
		/*
		 * the second tail page: ->mapping is
		 * deferred_list.next -- ignore value.
		 */
		break;
	default:
		if (page->mapping != TAIL_MAPPING) {
			bad_page(page, "corrupted mapping in tail page");
			goto out;
		}
		break;
	}
	if (unlikely(!PageTail(page))) {
		bad_page(page, "PageTail not set");
		goto out;
	}
	if (unlikely(compound_head(page) != head_page)) {
		bad_page(page, "compound_head not consistent");
		goto out;
	}
	ret = 0;
out:
	page->mapping = NULL;
	clear_compound_head(page);
	return ret;
}

/*
 * Skip KASAN memory poisoning when either:
 *
 * 1. Deferred memory initialization has not yet completed,
 *    see the explanation below.
 * 2. Skipping poisoning is requested via FPI_SKIP_KASAN_POISON,
 *    see the comment next to it.
 * 3. Skipping poisoning is requested via __GFP_SKIP_KASAN_POISON,
 *    see the comment next to it.
 *
 * Poisoning pages during deferred memory init will greatly lengthen the
 * process and cause problem in large memory systems as the deferred pages
 * initialization is done with interrupt disabled.
 *
 * Assuming that there will be no reference to those newly initialized
 * pages before they are ever allocated, this should have no effect on
 * KASAN memory tracking as the poison will be properly inserted at page
 * allocation time. The only corner case is when pages are allocated by
 * on-demand allocation and then freed again before the deferred pages
 * initialization is done, but this is not likely to happen.
 */
static inline bool should_skip_kasan_poison(struct page *page, fpi_t fpi_flags)
{
	return deferred_pages_enabled() ||
	       (!IS_ENABLED(CONFIG_KASAN_GENERIC) &&
		(fpi_flags & FPI_SKIP_KASAN_POISON)) ||
	       PageSkipKASanPoison(page);
}

static void kernel_init_pages(struct page *page, int numpages)
{
	int i;

	/* s390's use of memset() could override KASAN redzones. */
	kasan_disable_current();
	for (i = 0; i < numpages; i++)
		clear_highpage_kasan_tagged(page + i);
	kasan_enable_current();
}

static __always_inline bool free_pages_prepare(struct page *page,
			unsigned int order, bool check_free, fpi_t fpi_flags)
{
	int bad = 0;
	bool init = want_init_on_free();

	VM_BUG_ON_PAGE(PageTail(page), page);

	trace_mm_page_free(page, order);

	if (unlikely(PageHWPoison(page)) && !order) {
		/*
		 * Do not let hwpoison pages hit pcplists/buddy
		 * Untie memcg state and reset page's owner
		 */
		if (memcg_kmem_enabled() && PageMemcgKmem(page))
			__memcg_kmem_uncharge_page(page, order);
		reset_page_owner(page, order);
		page_table_check_free(page, order);
		return false;
	}

	/*
	 * Check tail pages before head page information is cleared to
	 * avoid checking PageCompound for order-0 pages.
	 */
	if (unlikely(order)) {
		bool compound = PageCompound(page);
		int i;

		VM_BUG_ON_PAGE(compound && compound_order(page) != order, page);

		if (compound) {
			ClearPageDoubleMap(page);
			ClearPageHasHWPoisoned(page);
		}
		for (i = 1; i < (1 << order); i++) {
			if (compound)
				bad += free_tail_pages_check(page, page + i);
			if (unlikely(check_free_page(page + i))) {
				bad++;
				continue;
			}
			(page + i)->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
		}
	}
	if (PageMappingFlags(page))
		page->mapping = NULL;
	if (memcg_kmem_enabled() && PageMemcgKmem(page))
		__memcg_kmem_uncharge_page(page, order);
	if (check_free)
		bad += check_free_page(page);
	if (bad)
		return false;

	page_cpupid_reset_last(page);
	page->flags &= ~PAGE_FLAGS_CHECK_AT_PREP;
	reset_page_owner(page, order);
	page_table_check_free(page, order);

	if (!PageHighMem(page)) {
		debug_check_no_locks_freed(page_address(page),
					   PAGE_SIZE << order);
		debug_check_no_obj_freed(page_address(page),
					   PAGE_SIZE << order);
	}

	kernel_poison_pages(page, 1 << order);

	/*
	 * As memory initialization might be integrated into KASAN,
	 * KASAN poisoning and memory initialization code must be
	 * kept together to avoid discrepancies in behavior.
	 *
	 * With hardware tag-based KASAN, memory tags must be set before the
	 * page becomes unavailable via debug_pagealloc or arch_free_page.
	 */
	if (!should_skip_kasan_poison(page, fpi_flags)) {
		kasan_poison_pages(page, order, init);

		/* Memory is already initialized if KASAN did it internally. */
		if (kasan_has_integrated_init())
			init = false;
	}
	if (init)
		kernel_init_pages(page, 1 << order);

	/*
	 * arch_free_page() can make the page's contents inaccessible.  s390
	 * does this.  So nothing which can access the page's contents should
	 * happen after this.
	 */
	arch_free_page(page, order);

	debug_pagealloc_unmap_pages(page, 1 << order);

	return true;
}

#ifdef CONFIG_DEBUG_VM
/*
 * With DEBUG_VM enabled, order-0 pages are checked immediately when being freed
 * to pcp lists. With debug_pagealloc also enabled, they are also rechecked when
 * moved from pcp lists to free lists.
 */
static bool free_pcp_prepare(struct page *page, unsigned int order)
{
	return free_pages_prepare(page, order, true, FPI_NONE);
}

static bool bulkfree_pcp_prepare(struct page *page)
{
	if (debug_pagealloc_enabled_static())
		return check_free_page(page);
	else
		return false;
}
#else
/*
 * With DEBUG_VM disabled, order-0 pages being freed are checked only when
 * moving from pcp lists to free list in order to reduce overhead. With
 * debug_pagealloc enabled, they are checked also immediately when being freed
 * to the pcp lists.
 */
static bool free_pcp_prepare(struct page *page, unsigned int order)
{
	if (debug_pagealloc_enabled_static())
		return free_pages_prepare(page, order, true, FPI_NONE);
	else
		return free_pages_prepare(page, order, false, FPI_NONE);
}

static bool bulkfree_pcp_prepare(struct page *page)
{
	return check_free_page(page);
}
#endif /* CONFIG_DEBUG_VM */

/*
 * Frees a number of pages from the PCP lists
 * Assumes all pages on list are in same zone.
 * count is the number of pages to free.
 */
static void free_pcppages_bulk(struct zone *zone, int count,
					struct per_cpu_pages *pcp,
					int pindex)
{
	int min_pindex = 0;
	int max_pindex = NR_PCP_LISTS - 1;
	unsigned int order;
	bool isolated_pageblocks;
	struct page *page;

	/*
	 * Ensure proper count is passed which otherwise would stuck in the
	 * below while (list_empty(list)) loop.
	 */
	count = min(pcp->count, count);

	/* Ensure requested pindex is drained first. */
	pindex = pindex - 1;

	/* Caller must hold IRQ-safe pcp->lock so IRQs are disabled. */
	spin_lock(&zone->lock);
	isolated_pageblocks = has_isolate_pageblock(zone);

	while (count > 0) {
		struct list_head *list;
		int nr_pages;

		/* Remove pages from lists in a round-robin fashion. */
		do {
			if (++pindex > max_pindex)
				pindex = min_pindex;
			list = &pcp->lists[pindex];
			if (!list_empty(list))
				break;

			if (pindex == max_pindex)
				max_pindex--;
			if (pindex == min_pindex)
				min_pindex++;
		} while (1);

		order = pindex_to_order(pindex);
		nr_pages = 1 << order;
		BUILD_BUG_ON(MAX_ORDER >= (1<<NR_PCP_ORDER_WIDTH));
		do {
			int mt;

			page = list_last_entry(list, struct page, pcp_list);
			mt = get_pcppage_migratetype(page);

			/* must delete to avoid corrupting pcp list */
			list_del(&page->pcp_list);
			count -= nr_pages;
			pcp->count -= nr_pages;

			if (bulkfree_pcp_prepare(page))
				continue;

			/* MIGRATE_ISOLATE page should not go to pcplists */
			VM_BUG_ON_PAGE(is_migrate_isolate(mt), page);
			/* Pageblock could have been isolated meanwhile */
			if (unlikely(isolated_pageblocks))
				mt = get_pageblock_migratetype(page);

			__free_one_page(page, page_to_pfn(page), zone, order, mt, FPI_NONE);
			trace_mm_page_pcpu_drain(page, order, mt);
		} while (count > 0 && !list_empty(list));
	}

	spin_unlock(&zone->lock);
}

static void free_one_page(struct zone *zone,
				struct page *page, unsigned long pfn,
				unsigned int order,
				int migratetype, fpi_t fpi_flags)
{
	unsigned long flags;

	spin_lock_irqsave(&zone->lock, flags);
	if (unlikely(has_isolate_pageblock(zone) ||
		is_migrate_isolate(migratetype))) {
		migratetype = get_pfnblock_migratetype(page, pfn);
	}
	__free_one_page(page, pfn, zone, order, migratetype, fpi_flags);
	spin_unlock_irqrestore(&zone->lock, flags);
}

static void __meminit __init_single_page(struct page *page, unsigned long pfn,
				unsigned long zone, int nid)
{
	mm_zero_struct_page(page);
	set_page_links(page, zone, nid, pfn);
	init_page_count(page);
	page_mapcount_reset(page);
	page_cpupid_reset_last(page);
	page_kasan_tag_reset(page);

	INIT_LIST_HEAD(&page->lru);
#ifdef WANT_PAGE_VIRTUAL
	/* The shift won't overflow because ZONE_NORMAL is below 4G. */
	if (!is_highmem_idx(zone))
		set_page_address(page, __va(pfn << PAGE_SHIFT));
#endif
}

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
static void __meminit init_reserved_page(unsigned long pfn)
{
	pg_data_t *pgdat;
	int nid, zid;

	if (!early_page_uninitialised(pfn))
		return;

	nid = early_pfn_to_nid(pfn);
	pgdat = NODE_DATA(nid);

	for (zid = 0; zid < MAX_NR_ZONES; zid++) {
		struct zone *zone = &pgdat->node_zones[zid];

		if (zone_spans_pfn(zone, pfn))
			break;
	}
	__init_single_page(pfn_to_page(pfn), pfn, zid, nid);
}
#else
static inline void init_reserved_page(unsigned long pfn)
{
}
#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

/*
 * Initialised pages do not have PageReserved set. This function is
 * called for each range allocated by the bootmem allocator and
 * marks the pages PageReserved. The remaining valid pages are later
 * sent to the buddy page allocator.
 */
void __meminit reserve_bootmem_region(phys_addr_t start, phys_addr_t end)
{
	unsigned long start_pfn = PFN_DOWN(start);
	unsigned long end_pfn = PFN_UP(end);

	for (; start_pfn < end_pfn; start_pfn++) {
		if (pfn_valid(start_pfn)) {
			struct page *page = pfn_to_page(start_pfn);

			init_reserved_page(start_pfn);

			/* Avoid false-positive PageTail() */
			INIT_LIST_HEAD(&page->lru);

			/*
			 * no need for atomic set_bit because the struct
			 * page is not visible yet so nobody should
			 * access it yet.
			 */
			__SetPageReserved(page);
		}
	}
}

static void __free_pages_ok(struct page *page, unsigned int order,
			    fpi_t fpi_flags)
{
	unsigned long flags;
	int migratetype;
	unsigned long pfn = page_to_pfn(page);
	struct zone *zone = page_zone(page);

	if (!free_pages_prepare(page, order, true, fpi_flags))
		return;

	migratetype = get_pfnblock_migratetype(page, pfn);

	spin_lock_irqsave(&zone->lock, flags);
	if (unlikely(has_isolate_pageblock(zone) ||
		is_migrate_isolate(migratetype))) {
		migratetype = get_pfnblock_migratetype(page, pfn);
	}
	__free_one_page(page, pfn, zone, order, migratetype, fpi_flags);
	spin_unlock_irqrestore(&zone->lock, flags);

	__count_vm_events(PGFREE, 1 << order);
}

void __free_pages_core(struct page *page, unsigned int order)
{
	unsigned int nr_pages = 1 << order;
	struct page *p = page;
	unsigned int loop;

	/*
	 * When initializing the memmap, __init_single_page() sets the refcount
	 * of all pages to 1 ("allocated"/"not free"). We have to set the
	 * refcount of all involved pages to 0.
	 */
	prefetchw(p);
	for (loop = 0; loop < (nr_pages - 1); loop++, p++) {
		prefetchw(p + 1);
		__ClearPageReserved(p);
		set_page_count(p, 0);
	}
	__ClearPageReserved(p);
	set_page_count(p, 0);

	atomic_long_add(nr_pages, &page_zone(page)->managed_pages);

	/*
	 * Bypass PCP and place fresh pages right to the tail, primarily
	 * relevant for memory onlining.
	 */
	__free_pages_ok(page, order, FPI_TO_TAIL | FPI_SKIP_KASAN_POISON);
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
#endif /* CONFIG_NUMA */

void __init memblock_free_pages(struct page *page, unsigned long pfn,
							unsigned int order)
{
	if (early_page_uninitialised(pfn))
		return;
	__free_pages_core(page, order);
}

/*
 * Check that the whole (or subset of) a pageblock given by the interval of
 * [start_pfn, end_pfn) is valid and within the same zone, before scanning it
 * with the migration of free compaction scanner.
 *
 * Return struct page pointer of start_pfn, or NULL if checks were not passed.
 *
 * It's possible on some configurations to have a setup like node0 node1 node0
 * i.e. it's possible that all pages within a zones range of pages do not
 * belong to a single zone. We assume that a border between node0 and node1
 * can occur within a single pageblock, but not a node0 node1 node0
 * interleaving within a single pageblock. It is therefore sufficient to check
 * the first and last page of a pageblock and avoid checking each individual
 * page in a pageblock.
 */
struct page *__pageblock_pfn_to_page(unsigned long start_pfn,
				     unsigned long end_pfn, struct zone *zone)
{
	struct page *start_page;
	struct page *end_page;

	/* end_pfn is one past the range we are checking */
	end_pfn--;

	if (!pfn_valid(start_pfn) || !pfn_valid(end_pfn))
		return NULL;

	start_page = pfn_to_online_page(start_pfn);
	if (!start_page)
		return NULL;

	if (page_zone(start_page) != zone)
		return NULL;

	end_page = pfn_to_page(end_pfn);

	/* This gives a shorter code than deriving page_zone(end_page) */
	if (page_zone_id(start_page) != page_zone_id(end_page))
		return NULL;

	return start_page;
}

void set_zone_contiguous(struct zone *zone)
{
	unsigned long block_start_pfn = zone->zone_start_pfn;
	unsigned long block_end_pfn;

	block_end_pfn = ALIGN(block_start_pfn + 1, pageblock_nr_pages);
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

void clear_zone_contiguous(struct zone *zone)
{
	zone->contiguous = false;
}

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
static void __init deferred_free_range(unsigned long pfn,
				       unsigned long nr_pages)
{
	struct page *page;
	unsigned long i;

	if (!nr_pages)
		return;

	page = pfn_to_page(pfn);

	/* Free a large naturally-aligned chunk if possible */
	if (nr_pages == pageblock_nr_pages &&
	    (pfn & (pageblock_nr_pages - 1)) == 0) {
		set_pageblock_migratetype(page, MIGRATE_MOVABLE);
		__free_pages_core(page, pageblock_order);
		return;
	}

	for (i = 0; i < nr_pages; i++, page++, pfn++) {
		if ((pfn & (pageblock_nr_pages - 1)) == 0)
			set_pageblock_migratetype(page, MIGRATE_MOVABLE);
		__free_pages_core(page, 0);
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
 * Returns true if page needs to be initialized or freed to buddy allocator.
 *
 * First we check if pfn is valid on architectures where it is possible to have
 * holes within pageblock_nr_pages. On systems where it is not possible, this
 * function is optimized out.
 *
 * Then, we check if a current large page is valid by only checking the validity
 * of the head pfn.
 */
static inline bool __init deferred_pfn_valid(unsigned long pfn)
{
	if (!(pfn & (pageblock_nr_pages - 1)) && !pfn_valid(pfn))
		return false;
	return true;
}

/*
 * Free pages to buddy allocator. Try to free aligned pages in
 * pageblock_nr_pages sizes.
 */
static void __init deferred_free_pages(unsigned long pfn,
				       unsigned long end_pfn)
{
	unsigned long nr_pgmask = pageblock_nr_pages - 1;
	unsigned long nr_free = 0;

	for (; pfn < end_pfn; pfn++) {
		if (!deferred_pfn_valid(pfn)) {
			deferred_free_range(pfn - nr_free, nr_free);
			nr_free = 0;
		} else if (!(pfn & nr_pgmask)) {
			deferred_free_range(pfn - nr_free, nr_free);
			nr_free = 1;
		} else {
			nr_free++;
		}
	}
	/* Free the last block of pages to allocator */
	deferred_free_range(pfn - nr_free, nr_free);
}

/*
 * Initialize struct pages.  We minimize pfn page lookups and scheduler checks
 * by performing it only once every pageblock_nr_pages.
 * Return number of pages initialized.
 */
static unsigned long  __init deferred_init_pages(struct zone *zone,
						 unsigned long pfn,
						 unsigned long end_pfn)
{
	unsigned long nr_pgmask = pageblock_nr_pages - 1;
	int nid = zone_to_nid(zone);
	unsigned long nr_pages = 0;
	int zid = zone_idx(zone);
	struct page *page = NULL;

	for (; pfn < end_pfn; pfn++) {
		if (!deferred_pfn_valid(pfn)) {
			page = NULL;
			continue;
		} else if (!page || !(pfn & nr_pgmask)) {
			page = pfn_to_page(pfn);
		} else {
			page++;
		}
		__init_single_page(page, pfn, zid, nid);
		nr_pages++;
	}
	return (nr_pages);
}

/*
 * This function is meant to pre-load the iterator for the zone init.
 * Specifically it walks through the ranges until we are caught up to the
 * first_init_pfn value and exits there. If we never encounter the value we
 * return false indicating there are no valid ranges left.
 */
static bool __init
deferred_init_mem_pfn_range_in_zone(u64 *i, struct zone *zone,
				    unsigned long *spfn, unsigned long *epfn,
				    unsigned long first_init_pfn)
{
	u64 j;

	/*
	 * Start out by walking through the ranges in this zone that have
	 * already been initialized. We don't need to do anything with them
	 * so we just need to flush them out of the system.
	 */
	for_each_free_mem_pfn_range_in_zone(j, zone, spfn, epfn) {
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
		deferred_free_pages(spfn, t);

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
	u64 i;

	deferred_init_mem_pfn_range_in_zone(&i, zone, &spfn, &epfn, start_pfn);

	/*
	 * Initialize and free pages in MAX_ORDER sized increments so that we
	 * can avoid introducing any issues with the buddy allocator.
	 */
	while (spfn < end_pfn) {
		deferred_init_maxorder(&i, zone, &spfn, &epfn);
		cond_resched();
	}
}

/* An arch may override for more concurrency. */
__weak int __init
deferred_page_init_max_threads(const struct cpumask *node_cpumask)
{
	return 1;
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
	int zid, max_threads;
	u64 i;

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

	/* Only the highest zone is deferred so find it */
	for (zid = 0; zid < MAX_NR_ZONES; zid++) {
		zone = pgdat->node_zones + zid;
		if (first_init_pfn < zone_end_pfn(zone))
			break;
	}

	/* If the zone is empty somebody else may have cleared out the zone */
	if (!deferred_init_mem_pfn_range_in_zone(&i, zone, &spfn, &epfn,
						 first_init_pfn))
		goto zone_empty;

	max_threads = deferred_page_init_max_threads(cpumask);

	while (spfn < epfn) {
		unsigned long epfn_align = ALIGN(epfn, PAGES_PER_SECTION);
		struct padata_mt_job job = {
			.thread_fn   = deferred_init_memmap_chunk,
			.fn_arg      = zone,
			.start       = spfn,
			.size        = epfn_align - spfn,
			.align       = PAGES_PER_SECTION,
			.min_chunk   = PAGES_PER_SECTION,
			.max_threads = max_threads,
		};

		padata_do_multithreaded(&job);
		deferred_init_mem_pfn_range_in_zone(&i, zone, &spfn, &epfn,
						    epfn_align);
	}
zone_empty:
	/* Sanity check that the next zone really is unpopulated */
	WARN_ON(++zid < MAX_NR_ZONES && populated_zone(++zone));

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
 *
 * Note: We use noinline because this function is needed only during boot, and
 * it is called from a __ref function _deferred_grow_zone. This way we are
 * making sure that it is not inlined into permanent text section.
 */
static noinline bool __init
deferred_grow_zone(struct zone *zone, unsigned int order)
{
	unsigned long nr_pages_needed = ALIGN(1 << order, PAGES_PER_SECTION);
	pg_data_t *pgdat = zone->zone_pgdat;
	unsigned long first_deferred_pfn = pgdat->first_deferred_pfn;
	unsigned long spfn, epfn, flags;
	unsigned long nr_pages = 0;
	u64 i;

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
	 * Initialize and free pages in MAX_ORDER sized increments so
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

/*
 * deferred_grow_zone() is __init, but it is called from
 * get_page_from_freelist() during early boot until deferred_pages permanently
 * disables this call. This is why we have refdata wrapper to avoid warning,
 * and to ensure that the function body gets unloaded.
 */
static bool __ref
_deferred_grow_zone(struct zone *zone, unsigned int order)
{
	return deferred_grow_zone(zone, order);
}

#endif /* CONFIG_DEFERRED_STRUCT_PAGE_INIT */

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

	buffer_init();

	/* Discard memblock private memory */
	memblock_discard();

	for_each_node_state(nid, N_MEMORY)
		shuffle_free_memory(NODE_DATA(nid));

	for_each_populated_zone(zone)
		set_zone_contiguous(zone);
}

#ifdef CONFIG_CMA
/* Free whole pageblock and set its migration type to MIGRATE_CMA. */
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
	__free_pages(page, pageblock_order);

	adjust_managed_page_count(page, pageblock_nr_pages);
	page_zone(page)->cma_pages += pageblock_nr_pages;
}
#endif

/*
 * The order of subdivision here is critical for the IO subsystem.
 * Please do not alter this order without good reasons and regression
 * testing. Specifically, as large blocks of memory are subdivided,
 * the order in which smaller blocks are delivered depends on the order
 * they're subdivided in this function. This is the primary factor
 * influencing the order in which pages are delivered to the IO
 * subsystem according to empirical testing, and this is also justified
 * by considering the behavior of a buddy system containing a single
 * large block of memory acted on by a series of small allocations.
 * This behavior is a critical factor in sglist merging's success.
 *
 * -- nyc
 */
static inline void expand(struct zone *zone, struct page *page,
	int low, int high, int migratetype)
{
	unsigned long size = 1 << high;

	while (high > low) {
		high--;
		size >>= 1;
		VM_BUG_ON_PAGE(bad_range(zone, &page[size]), &page[size]);

		/*
		 * Mark as guard pages (or page), that will allow to
		 * merge back to allocator when buddy will be freed.
		 * Corresponding page table entries will not be touched,
		 * pages will stay not present in virtual address space
		 */
		if (set_page_guard(zone, &page[size], high, migratetype))
			continue;

		add_to_free_list(&page[size], zone, high, migratetype);
		set_buddy_order(&page[size], high);
	}
}

static void check_new_page_bad(struct page *page)
{
	if (unlikely(page->flags & __PG_HWPOISON)) {
		/* Don't complain about hwpoisoned pages */
		page_mapcount_reset(page); /* remove PageBuddy */
		return;
	}

	bad_page(page,
		 page_bad_reason(page, PAGE_FLAGS_CHECK_AT_PREP));
}

/*
 * This page is about to be returned from the page allocator
 */
static inline int check_new_page(struct page *page)
{
	if (likely(page_expected_state(page,
				PAGE_FLAGS_CHECK_AT_PREP|__PG_HWPOISON)))
		return 0;

	check_new_page_bad(page);
	return 1;
}

static bool check_new_pages(struct page *page, unsigned int order)
{
	int i;
	for (i = 0; i < (1 << order); i++) {
		struct page *p = page + i;

		if (unlikely(check_new_page(p)))
			return true;
	}

	return false;
}

#ifdef CONFIG_DEBUG_VM
/*
 * With DEBUG_VM enabled, order-0 pages are checked for expected state when
 * being allocated from pcp lists. With debug_pagealloc also enabled, they are
 * also checked when pcp lists are refilled from the free lists.
 */
static inline bool check_pcp_refill(struct page *page, unsigned int order)
{
	if (debug_pagealloc_enabled_static())
		return check_new_pages(page, order);
	else
		return false;
}

static inline bool check_new_pcp(struct page *page, unsigned int order)
{
	return check_new_pages(page, order);
}
#else
/*
 * With DEBUG_VM disabled, free order-0 pages are checked for expected state
 * when pcp lists are being refilled from the free lists. With debug_pagealloc
 * enabled, they are also checked when being allocated from the pcp lists.
 */
static inline bool check_pcp_refill(struct page *page, unsigned int order)
{
	return check_new_pages(page, order);
}
static inline bool check_new_pcp(struct page *page, unsigned int order)
{
	if (debug_pagealloc_enabled_static())
		return check_new_pages(page, order);
	else
		return false;
}
#endif /* CONFIG_DEBUG_VM */

static inline bool should_skip_kasan_unpoison(gfp_t flags)
{
	/* Don't skip if a software KASAN mode is enabled. */
	if (IS_ENABLED(CONFIG_KASAN_GENERIC) ||
	    IS_ENABLED(CONFIG_KASAN_SW_TAGS))
		return false;

	/* Skip, if hardware tag-based KASAN is not enabled. */
	if (!kasan_hw_tags_enabled())
		return true;

	/*
	 * With hardware tag-based KASAN enabled, skip if this has been
	 * requested via __GFP_SKIP_KASAN_UNPOISON.
	 */
	return flags & __GFP_SKIP_KASAN_UNPOISON;
}

static inline bool should_skip_init(gfp_t flags)
{
	/* Don't skip, if hardware tag-based KASAN is not enabled. */
	if (!kasan_hw_tags_enabled())
		return false;

	/* For hardware tag-based KASAN, skip if requested. */
	return (flags & __GFP_SKIP_ZERO);
}

inline void post_alloc_hook(struct page *page, unsigned int order,
				gfp_t gfp_flags)
{
	bool init = !want_init_on_free() && want_init_on_alloc(gfp_flags) &&
			!should_skip_init(gfp_flags);
	bool init_tags = init && (gfp_flags & __GFP_ZEROTAGS);
	int i;

	set_page_private(page, 0);
	set_page_refcounted(page);

	arch_alloc_page(page, order);
	debug_pagealloc_map_pages(page, 1 << order);

	/*
	 * Page unpoisoning must happen before memory initialization.
	 * Otherwise, the poison pattern will be overwritten for __GFP_ZERO
	 * allocations and the page unpoisoning code will complain.
	 */
	kernel_unpoison_pages(page, 1 << order);

	/*
	 * As memory initialization might be integrated into KASAN,
	 * KASAN unpoisoning and memory initializion code must be
	 * kept together to avoid discrepancies in behavior.
	 */

	/*
	 * If memory tags should be zeroed (which happens only when memory
	 * should be initialized as well).
	 */
	if (init_tags) {
		/* Initialize both memory and tags. */
		for (i = 0; i != 1 << order; ++i)
			tag_clear_highpage(page + i);

		/* Note that memory is already initialized by the loop above. */
		init = false;
	}
	if (!should_skip_kasan_unpoison(gfp_flags)) {
		/* Unpoison shadow memory or set memory tags. */
		kasan_unpoison_pages(page, order, init);

		/* Note that memory is already initialized by KASAN. */
		if (kasan_has_integrated_init())
			init = false;
	} else {
		/* Ensure page_address() dereferencing does not fault. */
		for (i = 0; i != 1 << order; ++i)
			page_kasan_tag_reset(page + i);
	}
	/* If memory is still not initialized, do it now. */
	if (init)
		kernel_init_pages(page, 1 << order);
	/* Propagate __GFP_SKIP_KASAN_POISON to page flags. */
	if (kasan_hw_tags_enabled() && (gfp_flags & __GFP_SKIP_KASAN_POISON))
		SetPageSkipKASanPoison(page);

	set_page_owner(page, order, gfp_flags);
	page_table_check_alloc(page, order);
}

static void prep_new_page(struct page *page, unsigned int order, gfp_t gfp_flags,
							unsigned int alloc_flags)
{
	post_alloc_hook(page, order, gfp_flags);

	if (order && (gfp_flags & __GFP_COMP))
		prep_compound_page(page, order);

	/*
	 * page is set pfmemalloc when ALLOC_NO_WATERMARKS was necessary to
	 * allocate the page. The expectation is that the caller is taking
	 * steps that will free more memory. The caller should avoid the page
	 * being used for !PFMEMALLOC purposes.
	 */
	if (alloc_flags & ALLOC_NO_WATERMARKS)
		set_page_pfmemalloc(page);
	else
		clear_page_pfmemalloc(page);
}

/*
 * Go through the free lists for the given migratetype and remove
 * the smallest available page from the freelists
 */
static __always_inline
struct page *__rmqueue_smallest(struct zone *zone, unsigned int order,
						int migratetype)
{
	unsigned int current_order;
	struct free_area *area;
	struct page *page;

	/* Find a page of the appropriate size in the preferred list */
	for (current_order = order; current_order < MAX_ORDER; ++current_order) {
		area = &(zone->free_area[current_order]);
		page = get_page_from_free_area(area, migratetype);
		if (!page)
			continue;
		del_page_from_free_list(page, zone, current_order);
		expand(zone, page, order, current_order, migratetype);
		set_pcppage_migratetype(page, migratetype);
		trace_mm_page_alloc_zone_locked(page, order, migratetype,
				pcp_allowed_order(order) &&
				migratetype < MIGRATE_PCPTYPES);
		return page;
	}

	return NULL;
}


/*
 * This array describes the order lists are fallen back to when
 * the free lists for the desirable migrate type are depleted
 *
 * The other migratetypes do not have fallbacks.
 */
static int fallbacks[MIGRATE_TYPES][3] = {
	[MIGRATE_UNMOVABLE]   = { MIGRATE_RECLAIMABLE, MIGRATE_MOVABLE,   MIGRATE_TYPES },
	[MIGRATE_MOVABLE]     = { MIGRATE_RECLAIMABLE, MIGRATE_UNMOVABLE, MIGRATE_TYPES },
	[MIGRATE_RECLAIMABLE] = { MIGRATE_UNMOVABLE,   MIGRATE_MOVABLE,   MIGRATE_TYPES },
};

#ifdef CONFIG_CMA
static __always_inline struct page *__rmqueue_cma_fallback(struct zone *zone,
					unsigned int order)
{
	return __rmqueue_smallest(zone, order, MIGRATE_CMA);
}
#else
static inline struct page *__rmqueue_cma_fallback(struct zone *zone,
					unsigned int order) { return NULL; }
#endif

/*
 * Move the free pages in a range to the freelist tail of the requested type.
 * Note that start_page and end_pages are not aligned on a pageblock
 * boundary. If alignment is required, use move_freepages_block()
 */
static int move_freepages(struct zone *zone,
			  unsigned long start_pfn, unsigned long end_pfn,
			  int migratetype, int *num_movable)
{
	struct page *page;
	unsigned long pfn;
	unsigned int order;
	int pages_moved = 0;

	for (pfn = start_pfn; pfn <= end_pfn;) {
		page = pfn_to_page(pfn);
		if (!PageBuddy(page)) {
			/*
			 * We assume that pages that could be isolated for
			 * migration are movable. But we don't actually try
			 * isolating, as that would be expensive.
			 */
			if (num_movable &&
					(PageLRU(page) || __PageMovable(page)))
				(*num_movable)++;
			pfn++;
			continue;
		}

		/* Make sure we are not inadvertently changing nodes */
		VM_BUG_ON_PAGE(page_to_nid(page) != zone_to_nid(zone), page);
		VM_BUG_ON_PAGE(page_zone(page) != zone, page);

		order = buddy_order(page);
		move_to_free_list(page, zone, order, migratetype);
		pfn += 1 << order;
		pages_moved += 1 << order;
	}

	return pages_moved;
}

int move_freepages_block(struct zone *zone, struct page *page,
				int migratetype, int *num_movable)
{
	unsigned long start_pfn, end_pfn, pfn;

	if (num_movable)
		*num_movable = 0;

	pfn = page_to_pfn(page);
	start_pfn = pfn & ~(pageblock_nr_pages - 1);
	end_pfn = start_pfn + pageblock_nr_pages - 1;

	/* Do not cross zone boundaries */
	if (!zone_spans_pfn(zone, start_pfn))
		start_pfn = pfn;
	if (!zone_spans_pfn(zone, end_pfn))
		return 0;

	return move_freepages(zone, start_pfn, end_pfn, migratetype,
								num_movable);
}

static void change_pageblock_range(struct page *pageblock_page,
					int start_order, int migratetype)
{
	int nr_pageblocks = 1 << (start_order - pageblock_order);

	while (nr_pageblocks--) {
		set_pageblock_migratetype(pageblock_page, migratetype);
		pageblock_page += pageblock_nr_pages;
	}
}

/*
 * When we are falling back to another migratetype during allocation, try to
 * steal extra free pages from the same pageblocks to satisfy further
 * allocations, instead of polluting multiple pageblocks.
 *
 * If we are stealing a relatively large buddy page, it is likely there will
 * be more free pages in the pageblock, so try to steal them all. For
 * reclaimable and unmovable allocations, we steal regardless of page size,
 * as fragmentation caused by those allocations polluting movable pageblocks
 * is worse than movable allocations stealing from unmovable and reclaimable
 * pageblocks.
 */
static bool can_steal_fallback(unsigned int order, int start_mt)
{
	/*
	 * Leaving this order check is intended, although there is
	 * relaxed order check in next check. The reason is that
	 * we can actually steal whole pageblock if this condition met,
	 * but, below check doesn't guarantee it and that is just heuristic
	 * so could be changed anytime.
	 */
	if (order >= pageblock_order)
		return true;

	if (order >= pageblock_order / 2 ||
		start_mt == MIGRATE_RECLAIMABLE ||
		start_mt == MIGRATE_UNMOVABLE ||
		page_group_by_mobility_disabled)
		return true;

	return false;
}

static inline bool boost_watermark(struct zone *zone)
{
	unsigned long max_boost;

	if (!watermark_boost_factor)
		return false;
	/*
	 * Don't bother in zones that are unlikely to produce results.
	 * On small machines, including kdump capture kernels running
	 * in a small area, boosting the watermark can cause an out of
	 * memory situation immediately.
	 */
	if ((pageblock_nr_pages * 4) > zone_managed_pages(zone))
		return false;

	max_boost = mult_frac(zone->_watermark[WMARK_HIGH],
			watermark_boost_factor, 10000);

	/*
	 * high watermark may be uninitialised if fragmentation occurs
	 * very early in boot so do not boost. We do not fall
	 * through and boost by pageblock_nr_pages as failing
	 * allocations that early means that reclaim is not going
	 * to help and it may even be impossible to reclaim the
	 * boosted watermark resulting in a hang.
	 */
	if (!max_boost)
		return false;

	max_boost = max(pageblock_nr_pages, max_boost);

	zone->watermark_boost = min(zone->watermark_boost + pageblock_nr_pages,
		max_boost);

	return true;
}

/*
 * This function implements actual steal behaviour. If order is large enough,
 * we can steal whole pageblock. If not, we first move freepages in this
 * pageblock to our migratetype and determine how many already-allocated pages
 * are there in the pageblock with a compatible migratetype. If at least half
 * of pages are free or compatible, we can change migratetype of the pageblock
 * itself, so pages freed in the future will be put on the correct free list.
 */
static void steal_suitable_fallback(struct zone *zone, struct page *page,
		unsigned int alloc_flags, int start_type, bool whole_block)
{
	unsigned int current_order = buddy_order(page);
	int free_pages, movable_pages, alike_pages;
	int old_block_type;

	old_block_type = get_pageblock_migratetype(page);

	/*
	 * This can happen due to races and we want to prevent broken
	 * highatomic accounting.
	 */
	if (is_migrate_highatomic(old_block_type))
		goto single_page;

	/* Take ownership for orders >= pageblock_order */
	if (current_order >= pageblock_order) {
		change_pageblock_range(page, current_order, start_type);
		goto single_page;
	}

	/*
	 * Boost watermarks to increase reclaim pressure to reduce the
	 * likelihood of future fallbacks. Wake kswapd now as the node
	 * may be balanced overall and kswapd will not wake naturally.
	 */
	if (boost_watermark(zone) && (alloc_flags & ALLOC_KSWAPD))
		set_bit(ZONE_BOOSTED_WATERMARK, &zone->flags);

	/* We are not allowed to try stealing from the whole block */
	if (!whole_block)
		goto single_page;

	free_pages = move_freepages_block(zone, page, start_type,
						&movable_pages);
	/*
	 * Determine how many pages are compatible with our allocation.
	 * For movable allocation, it's the number of movable pages which
	 * we just obtained. For other types it's a bit more tricky.
	 */
	if (start_type == MIGRATE_MOVABLE) {
		alike_pages = movable_pages;
	} else {
		/*
		 * If we are falling back a RECLAIMABLE or UNMOVABLE allocation
		 * to MOVABLE pageblock, consider all non-movable pages as
		 * compatible. If it's UNMOVABLE falling back to RECLAIMABLE or
		 * vice versa, be conservative since we can't distinguish the
		 * exact migratetype of non-movable pages.
		 */
		if (old_block_type == MIGRATE_MOVABLE)
			alike_pages = pageblock_nr_pages
						- (free_pages + movable_pages);
		else
			alike_pages = 0;
	}

	/* moving whole block can fail due to zone boundary conditions */
	if (!free_pages)
		goto single_page;

	/*
	 * If a sufficient number of pages in the block are either free or of
	 * comparable migratability as our allocation, claim the whole block.
	 */
	if (free_pages + alike_pages >= (1 << (pageblock_order-1)) ||
			page_group_by_mobility_disabled)
		set_pageblock_migratetype(page, start_type);

	return;

single_page:
	move_to_free_list(page, zone, current_order, start_type);
}

/*
 * Check whether there is a suitable fallback freepage with requested order.
 * If only_stealable is true, this function returns fallback_mt only if
 * we can steal other freepages all together. This would help to reduce
 * fragmentation due to mixed migratetype pages in one pageblock.
 */
int find_suitable_fallback(struct free_area *area, unsigned int order,
			int migratetype, bool only_stealable, bool *can_steal)
{
	int i;
	int fallback_mt;

	if (area->nr_free == 0)
		return -1;

	*can_steal = false;
	for (i = 0;; i++) {
		fallback_mt = fallbacks[migratetype][i];
		if (fallback_mt == MIGRATE_TYPES)
			break;

		if (free_area_empty(area, fallback_mt))
			continue;

		if (can_steal_fallback(order, migratetype))
			*can_steal = true;

		if (!only_stealable)
			return fallback_mt;

		if (*can_steal)
			return fallback_mt;
	}

	return -1;
}

/*
 * Reserve a pageblock for exclusive use of high-order atomic allocations if
 * there are no empty page blocks that contain a page with a suitable order
 */
static void reserve_highatomic_pageblock(struct page *page, struct zone *zone,
				unsigned int alloc_order)
{
	int mt;
	unsigned long max_managed, flags;

	/*
	 * Limit the number reserved to 1 pageblock or roughly 1% of a zone.
	 * Check is race-prone but harmless.
	 */
	max_managed = (zone_managed_pages(zone) / 100) + pageblock_nr_pages;
	if (zone->nr_reserved_highatomic >= max_managed)
		return;

	spin_lock_irqsave(&zone->lock, flags);

	/* Recheck the nr_reserved_highatomic limit under the lock */
	if (zone->nr_reserved_highatomic >= max_managed)
		goto out_unlock;

	/* Yoink! */
	mt = get_pageblock_migratetype(page);
	/* Only reserve normal pageblocks (i.e., they can merge with others) */
	if (migratetype_is_mergeable(mt)) {
		zone->nr_reserved_highatomic += pageblock_nr_pages;
		set_pageblock_migratetype(page, MIGRATE_HIGHATOMIC);
		move_freepages_block(zone, page, MIGRATE_HIGHATOMIC, NULL);
	}

out_unlock:
	spin_unlock_irqrestore(&zone->lock, flags);
}

/*
 * Used when an allocation is about to fail under memory pressure. This
 * potentially hurts the reliability of high-order allocations when under
 * intense memory pressure but failed atomic allocations should be easier
 * to recover from than an OOM.
 *
 * If @force is true, try to unreserve a pageblock even though highatomic
 * pageblock is exhausted.
 */
static bool unreserve_highatomic_pageblock(const struct alloc_context *ac,
						bool force)
{
	struct zonelist *zonelist = ac->zonelist;
	unsigned long flags;
	struct zoneref *z;
	struct zone *zone;
	struct page *page;
	int order;
	bool ret;

	for_each_zone_zonelist_nodemask(zone, z, zonelist, ac->highest_zoneidx,
								ac->nodemask) {
		/*
		 * Preserve at least one pageblock unless memory pressure
		 * is really high.
		 */
		if (!force && zone->nr_reserved_highatomic <=
					pageblock_nr_pages)
			continue;

		spin_lock_irqsave(&zone->lock, flags);
		for (order = 0; order < MAX_ORDER; order++) {
			struct free_area *area = &(zone->free_area[order]);

			page = get_page_from_free_area(area, MIGRATE_HIGHATOMIC);
			if (!page)
				continue;

			/*
			 * In page freeing path, migratetype change is racy so
			 * we can counter several free pages in a pageblock
			 * in this loop although we changed the pageblock type
			 * from highatomic to ac->migratetype. So we should
			 * adjust the count once.
			 */
			if (is_migrate_highatomic_page(page)) {
				/*
				 * It should never happen but changes to
				 * locking could inadvertently allow a per-cpu
				 * drain to add pages to MIGRATE_HIGHATOMIC
				 * while unreserving so be safe and watch for
				 * underflows.
				 */
				zone->nr_reserved_highatomic -= min(
						pageblock_nr_pages,
						zone->nr_reserved_highatomic);
			}

			/*
			 * Convert to ac->migratetype and avoid the normal
			 * pageblock stealing heuristics. Minimally, the caller
			 * is doing the work and needs the pages. More
			 * importantly, if the block was always converted to
			 * MIGRATE_UNMOVABLE or another type then the number
			 * of pageblocks that cannot be completely freed
			 * may increase.
			 */
			set_pageblock_migratetype(page, ac->migratetype);
			ret = move_freepages_block(zone, page, ac->migratetype,
									NULL);
			if (ret) {
				spin_unlock_irqrestore(&zone->lock, flags);
				return ret;
			}
		}
		spin_unlock_irqrestore(&zone->lock, flags);
	}

	return false;
}

/*
 * Try finding a free buddy page on the fallback list and put it on the free
 * list of requested migratetype, possibly along with other pages from the same
 * block, depending on fragmentation avoidance heuristics. Returns true if
 * fallback was found so that __rmqueue_smallest() can grab it.
 *
 * The use of signed ints for order and current_order is a deliberate
 * deviation from the rest of this file, to make the for loop
 * condition simpler.
 */
static __always_inline bool
__rmqueue_fallback(struct zone *zone, int order, int start_migratetype,
						unsigned int alloc_flags)
{
	struct free_area *area;
	int current_order;
	int min_order = order;
	struct page *page;
	int fallback_mt;
	bool can_steal;

	/*
	 * Do not steal pages from freelists belonging to other pageblocks
	 * i.e. orders < pageblock_order. If there are no local zones free,
	 * the zonelists will be reiterated without ALLOC_NOFRAGMENT.
	 */
	if (alloc_flags & ALLOC_NOFRAGMENT)
		min_order = pageblock_order;

	/*
	 * Find the largest available free page in the other list. This roughly
	 * approximates finding the pageblock with the most free pages, which
	 * would be too costly to do exactly.
	 */
	for (current_order = MAX_ORDER - 1; current_order >= min_order;
				--current_order) {
		area = &(zone->free_area[current_order]);
		fallback_mt = find_suitable_fallback(area, current_order,
				start_migratetype, false, &can_steal);
		if (fallback_mt == -1)
			continue;

		/*
		 * We cannot steal all free pages from the pageblock and the
		 * requested migratetype is movable. In that case it's better to
		 * steal and split the smallest available page instead of the
		 * largest available page, because even if the next movable
		 * allocation falls back into a different pageblock than this
		 * one, it won't cause permanent fragmentation.
		 */
		if (!can_steal && start_migratetype == MIGRATE_MOVABLE
					&& current_order > order)
			goto find_smallest;

		goto do_steal;
	}

	return false;

find_smallest:
	for (current_order = order; current_order < MAX_ORDER;
							current_order++) {
		area = &(zone->free_area[current_order]);
		fallback_mt = find_suitable_fallback(area, current_order,
				start_migratetype, false, &can_steal);
		if (fallback_mt != -1)
			break;
	}

	/*
	 * This should not happen - we already found a suitable fallback
	 * when looking for the largest page.
	 */
	VM_BUG_ON(current_order == MAX_ORDER);

do_steal:
	page = get_page_from_free_area(area, fallback_mt);

	steal_suitable_fallback(zone, page, alloc_flags, start_migratetype,
								can_steal);

	trace_mm_page_alloc_extfrag(page, order, current_order,
		start_migratetype, fallback_mt);

	return true;

}

/*
 * Do the hard work of removing an element from the buddy allocator.
 * Call me with the zone->lock already held.
 */
static __always_inline struct page *
__rmqueue(struct zone *zone, unsigned int order, int migratetype,
						unsigned int alloc_flags)
{
	struct page *page;

	if (IS_ENABLED(CONFIG_CMA)) {
		/*
		 * Balance movable allocations between regular and CMA areas by
		 * allocating from CMA when over half of the zone's free memory
		 * is in the CMA area.
		 */
		if (alloc_flags & ALLOC_CMA &&
		    zone_page_state(zone, NR_FREE_CMA_PAGES) >
		    zone_page_state(zone, NR_FREE_PAGES) / 2) {
			page = __rmqueue_cma_fallback(zone, order);
			if (page)
				return page;
		}
	}
retry:
	page = __rmqueue_smallest(zone, order, migratetype);
	if (unlikely(!page)) {
		if (alloc_flags & ALLOC_CMA)
			page = __rmqueue_cma_fallback(zone, order);

		if (!page && __rmqueue_fallback(zone, order, migratetype,
								alloc_flags))
			goto retry;
	}
	return page;
}

/*
 * Obtain a specified number of elements from the buddy allocator, all under
 * a single hold of the lock, for efficiency.  Add them to the supplied list.
 * Returns the number of new pages which were placed at *list.
 */
static int rmqueue_bulk(struct zone *zone, unsigned int order,
			unsigned long count, struct list_head *list,
			int migratetype, unsigned int alloc_flags)
{
	int i, allocated = 0;

	/* Caller must hold IRQ-safe pcp->lock so IRQs are disabled. */
	spin_lock(&zone->lock);
	for (i = 0; i < count; ++i) {
		struct page *page = __rmqueue(zone, order, migratetype,
								alloc_flags);
		if (unlikely(page == NULL))
			break;

		if (unlikely(check_pcp_refill(page, order)))
			continue;

		/*
		 * Split buddy pages returned by expand() are received here in
		 * physical page order. The page is added to the tail of
		 * caller's list. From the callers perspective, the linked list
		 * is ordered by page number under some conditions. This is
		 * useful for IO devices that can forward direction from the
		 * head, thus also in the physical page order. This is useful
		 * for IO devices that can merge IO requests if the physical
		 * pages are ordered properly.
		 */
		list_add_tail(&page->pcp_list, list);
		allocated++;
		if (is_migrate_cma(get_pcppage_migratetype(page)))
			__mod_zone_page_state(zone, NR_FREE_CMA_PAGES,
					      -(1 << order));
	}

	/*
	 * i pages were removed from the buddy list even if some leak due
	 * to check_pcp_refill failing so adjust NR_FREE_PAGES based
	 * on i. Do not confuse with 'allocated' which is the number of
	 * pages added to the pcp list.
	 */
	__mod_zone_page_state(zone, NR_FREE_PAGES, -(i << order));
	spin_unlock(&zone->lock);
	return allocated;
}

#ifdef CONFIG_NUMA
/*
 * Called from the vmstat counter updater to drain pagesets of this
 * currently executing processor on remote nodes after they have
 * expired.
 */
void drain_zone_pages(struct zone *zone, struct per_cpu_pages *pcp)
{
	int to_drain, batch;

	batch = READ_ONCE(pcp->batch);
	to_drain = min(pcp->count, batch);
	if (to_drain > 0) {
		unsigned long flags;

		/*
		 * free_pcppages_bulk expects IRQs disabled for zone->lock
		 * so even though pcp->lock is not intended to be IRQ-safe,
		 * it's needed in this context.
		 */
		spin_lock_irqsave(&pcp->lock, flags);
		free_pcppages_bulk(zone, to_drain, pcp, 0);
		spin_unlock_irqrestore(&pcp->lock, flags);
	}
}
#endif

/*
 * Drain pcplists of the indicated processor and zone.
 */
static void drain_pages_zone(unsigned int cpu, struct zone *zone)
{
	struct per_cpu_pages *pcp;

	pcp = per_cpu_ptr(zone->per_cpu_pageset, cpu);
	if (pcp->count) {
		unsigned long flags;

		/* See drain_zone_pages on why this is disabling IRQs */
		spin_lock_irqsave(&pcp->lock, flags);
		free_pcppages_bulk(zone, pcp->count, pcp, 0);
		spin_unlock_irqrestore(&pcp->lock, flags);
	}
}

/*
 * Drain pcplists of all zones on the indicated processor.
 */
static void drain_pages(unsigned int cpu)
{
	struct zone *zone;

	for_each_populated_zone(zone) {
		drain_pages_zone(cpu, zone);
	}
}

/*
 * Spill all of this CPU's per-cpu pages back into the buddy allocator.
 */
void drain_local_pages(struct zone *zone)
{
	int cpu = smp_processor_id();

	if (zone)
		drain_pages_zone(cpu, zone);
	else
		drain_pages(cpu);
}

/*
 * The implementation of drain_all_pages(), exposing an extra parameter to
 * drain on all cpus.
 *
 * drain_all_pages() is optimized to only execute on cpus where pcplists are
 * not empty. The check for non-emptiness can however race with a free to
 * pcplist that has not yet increased the pcp->count from 0 to 1. Callers
 * that need the guarantee that every CPU has drained can disable the
 * optimizing racy check.
 */
static void __drain_all_pages(struct zone *zone, bool force_all_cpus)
{
	int cpu;

	/*
	 * Allocate in the BSS so we won't require allocation in
	 * direct reclaim path for CONFIG_CPUMASK_OFFSTACK=y
	 */
	static cpumask_t cpus_with_pcps;

	/*
	 * Do not drain if one is already in progress unless it's specific to
	 * a zone. Such callers are primarily CMA and memory hotplug and need
	 * the drain to be complete when the call returns.
	 */
	if (unlikely(!mutex_trylock(&pcpu_drain_mutex))) {
		if (!zone)
			return;
		mutex_lock(&pcpu_drain_mutex);
	}

	/*
	 * We don't care about racing with CPU hotplug event
	 * as offline notification will cause the notified
	 * cpu to drain that CPU pcps and on_each_cpu_mask
	 * disables preemption as part of its processing
	 */
	for_each_online_cpu(cpu) {
		struct per_cpu_pages *pcp;
		struct zone *z;
		bool has_pcps = false;

		if (force_all_cpus) {
			/*
			 * The pcp.count check is racy, some callers need a
			 * guarantee that no cpu is missed.
			 */
			has_pcps = true;
		} else if (zone) {
			pcp = per_cpu_ptr(zone->per_cpu_pageset, cpu);
			if (pcp->count)
				has_pcps = true;
		} else {
			for_each_populated_zone(z) {
				pcp = per_cpu_ptr(z->per_cpu_pageset, cpu);
				if (pcp->count) {
					has_pcps = true;
					break;
				}
			}
		}

		if (has_pcps)
			cpumask_set_cpu(cpu, &cpus_with_pcps);
		else
			cpumask_clear_cpu(cpu, &cpus_with_pcps);
	}

	for_each_cpu(cpu, &cpus_with_pcps) {
		if (zone)
			drain_pages_zone(cpu, zone);
		else
			drain_pages(cpu);
	}

	mutex_unlock(&pcpu_drain_mutex);
}

/*
 * Spill all the per-cpu pages from all CPUs back into the buddy allocator.
 *
 * When zone parameter is non-NULL, spill just the single zone's pages.
 */
void drain_all_pages(struct zone *zone)
{
	__drain_all_pages(zone, false);
}

#ifdef CONFIG_HIBERNATION

/*
 * Touch the watchdog for every WD_PAGE_COUNT pages.
 */
#define WD_PAGE_COUNT	(128*1024)

void mark_free_pages(struct zone *zone)
{
	unsigned long pfn, max_zone_pfn, page_count = WD_PAGE_COUNT;
	unsigned long flags;
	unsigned int order, t;
	struct page *page;

	if (zone_is_empty(zone))
		return;

	spin_lock_irqsave(&zone->lock, flags);

	max_zone_pfn = zone_end_pfn(zone);
	for (pfn = zone->zone_start_pfn; pfn < max_zone_pfn; pfn++)
		if (pfn_valid(pfn)) {
			page = pfn_to_page(pfn);

			if (!--page_count) {
				touch_nmi_watchdog();
				page_count = WD_PAGE_COUNT;
			}

			if (page_zone(page) != zone)
				continue;

			if (!swsusp_page_is_forbidden(page))
				swsusp_unset_page_free(page);
		}

	for_each_migratetype_order(order, t) {
		list_for_each_entry(page,
				&zone->free_area[order].free_list[t], buddy_list) {
			unsigned long i;

			pfn = page_to_pfn(page);
			for (i = 0; i < (1UL << order); i++) {
				if (!--page_count) {
					touch_nmi_watchdog();
					page_count = WD_PAGE_COUNT;
				}
				swsusp_set_page_free(pfn_to_page(pfn + i));
			}
		}
	}
	spin_unlock_irqrestore(&zone->lock, flags);
}
#endif /* CONFIG_PM */

static bool free_unref_page_prepare(struct page *page, unsigned long pfn,
							unsigned int order)
{
	int migratetype;

	if (!free_pcp_prepare(page, order))
		return false;

	migratetype = get_pfnblock_migratetype(page, pfn);
	set_pcppage_migratetype(page, migratetype);
	return true;
}

static int nr_pcp_free(struct per_cpu_pages *pcp, int high, int batch,
		       bool free_high)
{
	int min_nr_free, max_nr_free;

	/* Free everything if batch freeing high-order pages. */
	if (unlikely(free_high))
		return pcp->count;

	/* Check for PCP disabled or boot pageset */
	if (unlikely(high < batch))
		return 1;

	/* Leave at least pcp->batch pages on the list */
	min_nr_free = batch;
	max_nr_free = high - batch;

	/*
	 * Double the number of pages freed each time there is subsequent
	 * freeing of pages without any allocation.
	 */
	batch <<= pcp->free_factor;
	if (batch < max_nr_free)
		pcp->free_factor++;
	batch = clamp(batch, min_nr_free, max_nr_free);

	return batch;
}

static int nr_pcp_high(struct per_cpu_pages *pcp, struct zone *zone,
		       bool free_high)
{
	int high = READ_ONCE(pcp->high);

	if (unlikely(!high || free_high))
		return 0;

	if (!test_bit(ZONE_RECLAIM_ACTIVE, &zone->flags))
		return high;

	/*
	 * If reclaim is active, limit the number of pages that can be
	 * stored on pcp lists
	 */
	return min(READ_ONCE(pcp->batch) << 2, high);
}

static void free_unref_page_commit(struct zone *zone, struct per_cpu_pages *pcp,
				   struct page *page, int migratetype,
				   unsigned int order)
{
	int high;
	int pindex;
	bool free_high;

	__count_vm_event(PGFREE);
	pindex = order_to_pindex(migratetype, order);
	list_add(&page->pcp_list, &pcp->lists[pindex]);
	pcp->count += 1 << order;

	/*
	 * As high-order pages other than THP's stored on PCP can contribute
	 * to fragmentation, limit the number stored when PCP is heavily
	 * freeing without allocation. The remainder after bulk freeing
	 * stops will be drained from vmstat refresh context.
	 */
	free_high = (pcp->free_factor && order && order <= PAGE_ALLOC_COSTLY_ORDER);

	high = nr_pcp_high(pcp, zone, free_high);
	if (pcp->count >= high) {
		int batch = READ_ONCE(pcp->batch);

		free_pcppages_bulk(zone, nr_pcp_free(pcp, high, batch, free_high), pcp, pindex);
	}
}

/*
 * Free a pcp page
 */
void free_unref_page(struct page *page, unsigned int order)
{
	unsigned long flags;
	unsigned long __maybe_unused UP_flags;
	struct per_cpu_pages *pcp;
	struct zone *zone;
	unsigned long pfn = page_to_pfn(page);
	int migratetype;

	if (!free_unref_page_prepare(page, pfn, order))
		return;

	/*
	 * We only track unmovable, reclaimable and movable on pcp lists.
	 * Place ISOLATE pages on the isolated list because they are being
	 * offlined but treat HIGHATOMIC as movable pages so we can get those
	 * areas back if necessary. Otherwise, we may have to free
	 * excessively into the page allocator
	 */
	migratetype = get_pcppage_migratetype(page);
	if (unlikely(migratetype >= MIGRATE_PCPTYPES)) {
		if (unlikely(is_migrate_isolate(migratetype))) {
			free_one_page(page_zone(page), page, pfn, order, migratetype, FPI_NONE);
			return;
		}
		migratetype = MIGRATE_MOVABLE;
	}

	zone = page_zone(page);
	pcp_trylock_prepare(UP_flags);
	pcp = pcp_spin_trylock_irqsave(zone->per_cpu_pageset, flags);
	if (pcp) {
		free_unref_page_commit(zone, pcp, page, migratetype, order);
		pcp_spin_unlock_irqrestore(pcp, flags);
	} else {
		free_one_page(zone, page, pfn, order, migratetype, FPI_NONE);
	}
	pcp_trylock_finish(UP_flags);
}

/*
 * Free a list of 0-order pages
 */
void free_unref_page_list(struct list_head *list)
{
	struct page *page, *next;
	struct per_cpu_pages *pcp = NULL;
	struct zone *locked_zone = NULL;
	unsigned long flags;
	int batch_count = 0;
	int migratetype;

	/* Prepare pages for freeing */
	list_for_each_entry_safe(page, next, list, lru) {
		unsigned long pfn = page_to_pfn(page);
		if (!free_unref_page_prepare(page, pfn, 0)) {
			list_del(&page->lru);
			continue;
		}

		/*
		 * Free isolated pages directly to the allocator, see
		 * comment in free_unref_page.
		 */
		migratetype = get_pcppage_migratetype(page);
		if (unlikely(is_migrate_isolate(migratetype))) {
			list_del(&page->lru);
			free_one_page(page_zone(page), page, pfn, 0, migratetype, FPI_NONE);
			continue;
		}
	}

	list_for_each_entry_safe(page, next, list, lru) {
		struct zone *zone = page_zone(page);

		/* Different zone, different pcp lock. */
		if (zone != locked_zone) {
			if (pcp)
				pcp_spin_unlock_irqrestore(pcp, flags);

			locked_zone = zone;
			pcp = pcp_spin_lock_irqsave(locked_zone->per_cpu_pageset, flags);
		}

		/*
		 * Non-isolated types over MIGRATE_PCPTYPES get added
		 * to the MIGRATE_MOVABLE pcp list.
		 */
		migratetype = get_pcppage_migratetype(page);
		if (unlikely(migratetype >= MIGRATE_PCPTYPES))
			migratetype = MIGRATE_MOVABLE;

		trace_mm_page_free_batched(page);
		free_unref_page_commit(zone, pcp, page, migratetype, 0);

		/*
		 * Guard against excessive IRQ disabled times when we get
		 * a large list of pages to free.
		 */
		if (++batch_count == SWAP_CLUSTER_MAX) {
			pcp_spin_unlock_irqrestore(pcp, flags);
			batch_count = 0;
			pcp = pcp_spin_lock_irqsave(locked_zone->per_cpu_pageset, flags);
		}
	}

	if (pcp)
		pcp_spin_unlock_irqrestore(pcp, flags);
}

/*
 * split_page takes a non-compound higher-order page, and splits it into
 * n (1<<order) sub-pages: page[0..n]
 * Each sub-page must be freed individually.
 *
 * Note: this is probably too low level an operation for use in drivers.
 * Please consult with lkml before using this in your driver.
 */
void split_page(struct page *page, unsigned int order)
{
	int i;

	VM_BUG_ON_PAGE(PageCompound(page), page);
	VM_BUG_ON_PAGE(!page_count(page), page);

	for (i = 1; i < (1 << order); i++)
		set_page_refcounted(page + i);
	split_page_owner(page, 1 << order);
	split_page_memcg(page, 1 << order);
}
EXPORT_SYMBOL_GPL(split_page);

int __isolate_free_page(struct page *page, unsigned int order)
{
	unsigned long watermark;
	struct zone *zone;
	int mt;

	BUG_ON(!PageBuddy(page));

	zone = page_zone(page);
	mt = get_pageblock_migratetype(page);

	if (!is_migrate_isolate(mt)) {
		/*
		 * Obey watermarks as if the page was being allocated. We can
		 * emulate a high-order watermark check with a raised order-0
		 * watermark, because we already know our high-order page
		 * exists.
		 */
		watermark = zone->_watermark[WMARK_MIN] + (1UL << order);
		if (!zone_watermark_ok(zone, 0, watermark, 0, ALLOC_CMA))
			return 0;

		__mod_zone_freepage_state(zone, -(1UL << order), mt);
	}

	/* Remove page from free list */

	del_page_from_free_list(page, zone, order);

	/*
	 * Set the pageblock if the isolated page is at least half of a
	 * pageblock
	 */
	if (order >= pageblock_order - 1) {
		struct page *endpage = page + (1 << order) - 1;
		for (; page < endpage; page += pageblock_nr_pages) {
			int mt = get_pageblock_migratetype(page);
			/*
			 * Only change normal pageblocks (i.e., they can merge
			 * with others)
			 */
			if (migratetype_is_mergeable(mt))
				set_pageblock_migratetype(page,
							  MIGRATE_MOVABLE);
		}
	}


	return 1UL << order;
}

/**
 * __putback_isolated_page - Return a now-isolated page back where we got it
 * @page: Page that was isolated
 * @order: Order of the isolated page
 * @mt: The page's pageblock's migratetype
 *
 * This function is meant to return a page pulled from the free lists via
 * __isolate_free_page back to the free lists they were pulled from.
 */
void __putback_isolated_page(struct page *page, unsigned int order, int mt)
{
	struct zone *zone = page_zone(page);

	/* zone lock should be held when this function is called */
	lockdep_assert_held(&zone->lock);

	/* Return isolated page to tail of freelist. */
	__free_one_page(page, page_to_pfn(page), zone, order, mt,
			FPI_SKIP_REPORT_NOTIFY | FPI_TO_TAIL);
}

/*
 * Update NUMA hit/miss statistics
 *
 * Must be called with interrupts disabled.
 */
static inline void zone_statistics(struct zone *preferred_zone, struct zone *z,
				   long nr_account)
{
#ifdef CONFIG_NUMA
	enum numa_stat_item local_stat = NUMA_LOCAL;

	/* skip numa counters update if numa stats is disabled */
	if (!static_branch_likely(&vm_numa_stat_key))
		return;

	if (zone_to_nid(z) != numa_node_id())
		local_stat = NUMA_OTHER;

	if (zone_to_nid(z) == zone_to_nid(preferred_zone))
		__count_numa_events(z, NUMA_HIT, nr_account);
	else {
		__count_numa_events(z, NUMA_MISS, nr_account);
		__count_numa_events(preferred_zone, NUMA_FOREIGN, nr_account);
	}
	__count_numa_events(z, local_stat, nr_account);
#endif
}

static __always_inline
struct page *rmqueue_buddy(struct zone *preferred_zone, struct zone *zone,
			   unsigned int order, unsigned int alloc_flags,
			   int migratetype)
{
	struct page *page;
	unsigned long flags;

	do {
		page = NULL;
		spin_lock_irqsave(&zone->lock, flags);
		/*
		 * order-0 request can reach here when the pcplist is skipped
		 * due to non-CMA allocation context. HIGHATOMIC area is
		 * reserved for high-order atomic allocation, so order-0
		 * request should skip it.
		 */
		if (order > 0 && alloc_flags & ALLOC_HARDER)
			page = __rmqueue_smallest(zone, order, MIGRATE_HIGHATOMIC);
		if (!page) {
			page = __rmqueue(zone, order, migratetype, alloc_flags);
			if (!page) {
				spin_unlock_irqrestore(&zone->lock, flags);
				return NULL;
			}
		}
		__mod_zone_freepage_state(zone, -(1 << order),
					  get_pcppage_migratetype(page));
		spin_unlock_irqrestore(&zone->lock, flags);
	} while (check_new_pages(page, order));

	__count_zid_vm_events(PGALLOC, page_zonenum(page), 1 << order);
	zone_statistics(preferred_zone, zone, 1);

	return page;
}

/* Remove page from the per-cpu list, caller must protect the list */
static inline
struct page *__rmqueue_pcplist(struct zone *zone, unsigned int order,
			int migratetype,
			unsigned int alloc_flags,
			struct per_cpu_pages *pcp,
			struct list_head *list)
{
	struct page *page;

	do {
		if (list_empty(list)) {
			int batch = READ_ONCE(pcp->batch);
			int alloced;

			/*
			 * Scale batch relative to order if batch implies
			 * free pages can be stored on the PCP. Batch can
			 * be 1 for small zones or for boot pagesets which
			 * should never store free pages as the pages may
			 * belong to arbitrary zones.
			 */
			if (batch > 1)
				batch = max(batch >> order, 2);
			alloced = rmqueue_bulk(zone, order,
					batch, list,
					migratetype, alloc_flags);

			pcp->count += alloced << order;
			if (unlikely(list_empty(list)))
				return NULL;
		}

		page = list_first_entry(list, struct page, pcp_list);
		list_del(&page->pcp_list);
		pcp->count -= 1 << order;
	} while (check_new_pcp(page, order));

	return page;
}

/* Lock and remove page from the per-cpu list */
static struct page *rmqueue_pcplist(struct zone *preferred_zone,
			struct zone *zone, unsigned int order,
			gfp_t gfp_flags, int migratetype,
			unsigned int alloc_flags)
{
	struct per_cpu_pages *pcp;
	struct list_head *list;
	struct page *page;
	unsigned long flags;
	unsigned long __maybe_unused UP_flags;

	/*
	 * spin_trylock may fail due to a parallel drain. In the future, the
	 * trylock will also protect against IRQ reentrancy.
	 */
	pcp_trylock_prepare(UP_flags);
	pcp = pcp_spin_trylock_irqsave(zone->per_cpu_pageset, flags);
	if (!pcp) {
		pcp_trylock_finish(UP_flags);
		return NULL;
	}

	/*
	 * On allocation, reduce the number of pages that are batch freed.
	 * See nr_pcp_free() where free_factor is increased for subsequent
	 * frees.
	 */
	pcp->free_factor >>= 1;
	list = &pcp->lists[order_to_pindex(migratetype, order)];
	page = __rmqueue_pcplist(zone, order, migratetype, alloc_flags, pcp, list);
	pcp_spin_unlock_irqrestore(pcp, flags);
	pcp_trylock_finish(UP_flags);
	if (page) {
		__count_zid_vm_events(PGALLOC, page_zonenum(page), 1);
		zone_statistics(preferred_zone, zone, 1);
	}
	return page;
}

/*
 * Allocate a page from the given zone. Use pcplists for order-0 allocations.
 */
static inline
struct page *rmqueue(struct zone *preferred_zone,
			struct zone *zone, unsigned int order,
			gfp_t gfp_flags, unsigned int alloc_flags,
			int migratetype)
{
	struct page *page;

	/*
	 * We most definitely don't want callers attempting to
	 * allocate greater than order-1 page units with __GFP_NOFAIL.
	 */
	WARN_ON_ONCE((gfp_flags & __GFP_NOFAIL) && (order > 1));

	if (likely(pcp_allowed_order(order))) {
		/*
		 * MIGRATE_MOVABLE pcplist could have the pages on CMA area and
		 * we need to skip it when CMA area isn't allowed.
		 */
		if (!IS_ENABLED(CONFIG_CMA) || alloc_flags & ALLOC_CMA ||
				migratetype != MIGRATE_MOVABLE) {
			page = rmqueue_pcplist(preferred_zone, zone, order,
					gfp_flags, migratetype, alloc_flags);
			if (likely(page))
				goto out;
		}
	}

	page = rmqueue_buddy(preferred_zone, zone, order, alloc_flags,
							migratetype);

out:
	/* Separate test+clear to avoid unnecessary atomics */
	if (unlikely(test_bit(ZONE_BOOSTED_WATERMARK, &zone->flags))) {
		clear_bit(ZONE_BOOSTED_WATERMARK, &zone->flags);
		wakeup_kswapd(zone, 0, 0, zone_idx(zone));
	}

	VM_BUG_ON_PAGE(page && bad_range(zone, page), page);
	return page;
}

#ifdef CONFIG_FAIL_PAGE_ALLOC

static struct {
	struct fault_attr attr;

	bool ignore_gfp_highmem;
	bool ignore_gfp_reclaim;
	u32 min_order;
} fail_page_alloc = {
	.attr = FAULT_ATTR_INITIALIZER,
	.ignore_gfp_reclaim = true,
	.ignore_gfp_highmem = true,
	.min_order = 1,
};

static int __init setup_fail_page_alloc(char *str)
{
	return setup_fault_attr(&fail_page_alloc.attr, str);
}
__setup("fail_page_alloc=", setup_fail_page_alloc);

static bool __should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	if (order < fail_page_alloc.min_order)
		return false;
	if (gfp_mask & __GFP_NOFAIL)
		return false;
	if (fail_page_alloc.ignore_gfp_highmem && (gfp_mask & __GFP_HIGHMEM))
		return false;
	if (fail_page_alloc.ignore_gfp_reclaim &&
			(gfp_mask & __GFP_DIRECT_RECLAIM))
		return false;

	if (gfp_mask & __GFP_NOWARN)
		fail_page_alloc.attr.no_warn = true;

	return should_fail(&fail_page_alloc.attr, 1 << order);
}

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

static int __init fail_page_alloc_debugfs(void)
{
	umode_t mode = S_IFREG | 0600;
	struct dentry *dir;

	dir = fault_create_debugfs_attr("fail_page_alloc", NULL,
					&fail_page_alloc.attr);

	debugfs_create_bool("ignore-gfp-wait", mode, dir,
			    &fail_page_alloc.ignore_gfp_reclaim);
	debugfs_create_bool("ignore-gfp-highmem", mode, dir,
			    &fail_page_alloc.ignore_gfp_highmem);
	debugfs_create_u32("min-order", mode, dir, &fail_page_alloc.min_order);

	return 0;
}

late_initcall(fail_page_alloc_debugfs);

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */

#else /* CONFIG_FAIL_PAGE_ALLOC */

static inline bool __should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	return false;
}

#endif /* CONFIG_FAIL_PAGE_ALLOC */

noinline bool should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	return __should_fail_alloc_page(gfp_mask, order);
}
ALLOW_ERROR_INJECTION(should_fail_alloc_page, TRUE);

static inline long __zone_watermark_unusable_free(struct zone *z,
				unsigned int order, unsigned int alloc_flags)
{
	const bool alloc_harder = (alloc_flags & (ALLOC_HARDER|ALLOC_OOM));
	long unusable_free = (1 << order) - 1;

	/*
	 * If the caller does not have rights to ALLOC_HARDER then subtract
	 * the high-atomic reserves. This will over-estimate the size of the
	 * atomic reserve but it avoids a search.
	 */
	if (likely(!alloc_harder))
		unusable_free += z->nr_reserved_highatomic;

#ifdef CONFIG_CMA
	/* If allocation can't use CMA areas don't use free CMA pages */
	if (!(alloc_flags & ALLOC_CMA))
		unusable_free += zone_page_state(z, NR_FREE_CMA_PAGES);
#endif

	return unusable_free;
}

/*
 * Return true if free base pages are above 'mark'. For high-order checks it
 * will return true of the order-0 watermark is reached and there is at least
 * one free page of a suitable size. Checking now avoids taking the zone lock
 * to check in the allocation paths if no pages are free.
 */
bool __zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
			 int highest_zoneidx, unsigned int alloc_flags,
			 long free_pages)
{
	long min = mark;
	int o;
	const bool alloc_harder = (alloc_flags & (ALLOC_HARDER|ALLOC_OOM));

	/* free_pages may go negative - that's OK */
	free_pages -= __zone_watermark_unusable_free(z, order, alloc_flags);

	if (alloc_flags & ALLOC_HIGH)
		min -= min / 2;

	if (unlikely(alloc_harder)) {
		/*
		 * OOM victims can try even harder than normal ALLOC_HARDER
		 * users on the grounds that it's definitely going to be in
		 * the exit path shortly and free memory. Any allocation it
		 * makes during the free path will be small and short-lived.
		 */
		if (alloc_flags & ALLOC_OOM)
			min -= min / 2;
		else
			min -= min / 4;
	}

	/*
	 * Check watermarks for an order-0 allocation request. If these
	 * are not met, then a high-order request also cannot go ahead
	 * even if a suitable page happened to be free.
	 */
	if (free_pages <= min + z->lowmem_reserve[highest_zoneidx])
		return false;

	/* If this is an order-0 request then the watermark is fine */
	if (!order)
		return true;

	/* For a high-order request, check at least one suitable page is free */
	for (o = order; o < MAX_ORDER; o++) {
		struct free_area *area = &z->free_area[o];
		int mt;

		if (!area->nr_free)
			continue;

		for (mt = 0; mt < MIGRATE_PCPTYPES; mt++) {
			if (!free_area_empty(area, mt))
				return true;
		}

#ifdef CONFIG_CMA
		if ((alloc_flags & ALLOC_CMA) &&
		    !free_area_empty(area, MIGRATE_CMA)) {
			return true;
		}
#endif
		if (alloc_harder && !free_area_empty(area, MIGRATE_HIGHATOMIC))
			return true;
	}
	return false;
}

bool zone_watermark_ok(struct zone *z, unsigned int order, unsigned long mark,
		      int highest_zoneidx, unsigned int alloc_flags)
{
	return __zone_watermark_ok(z, order, mark, highest_zoneidx, alloc_flags,
					zone_page_state(z, NR_FREE_PAGES));
}

static inline bool zone_watermark_fast(struct zone *z, unsigned int order,
				unsigned long mark, int highest_zoneidx,
				unsigned int alloc_flags, gfp_t gfp_mask)
{
	long free_pages;

	free_pages = zone_page_state(z, NR_FREE_PAGES);

	/*
	 * Fast check for order-0 only. If this fails then the reserves
	 * need to be calculated.
	 */
	if (!order) {
		long usable_free;
		long reserved;

		usable_free = free_pages;
		reserved = __zone_watermark_unusable_free(z, 0, alloc_flags);

		/* reserved may over estimate high-atomic reserves. */
		usable_free -= min(usable_free, reserved);
		if (usable_free > mark + z->lowmem_reserve[highest_zoneidx])
			return true;
	}

	if (__zone_watermark_ok(z, order, mark, highest_zoneidx, alloc_flags,
					free_pages))
		return true;
	/*
	 * Ignore watermark boosting for GFP_ATOMIC order-0 allocations
	 * when checking the min watermark. The min watermark is the
	 * point where boosting is ignored so that kswapd is woken up
	 * when below the low watermark.
	 */
	if (unlikely(!order && (gfp_mask & __GFP_ATOMIC) && z->watermark_boost
		&& ((alloc_flags & ALLOC_WMARK_MASK) == WMARK_MIN))) {
		mark = z->_watermark[WMARK_MIN];
		return __zone_watermark_ok(z, order, mark, highest_zoneidx,
					alloc_flags, free_pages);
	}

	return false;
}

bool zone_watermark_ok_safe(struct zone *z, unsigned int order,
			unsigned long mark, int highest_zoneidx)
{
	long free_pages = zone_page_state(z, NR_FREE_PAGES);

	if (z->percpu_drift_mark && free_pages < z->percpu_drift_mark)
		free_pages = zone_page_state_snapshot(z, NR_FREE_PAGES);

	return __zone_watermark_ok(z, order, mark, highest_zoneidx, 0,
								free_pages);
}

#ifdef CONFIG_NUMA
int __read_mostly node_reclaim_distance = RECLAIM_DISTANCE;

static bool zone_allows_reclaim(struct zone *local_zone, struct zone *zone)
{
	return node_distance(zone_to_nid(local_zone), zone_to_nid(zone)) <=
				node_reclaim_distance;
}
#else	/* CONFIG_NUMA */
static bool zone_allows_reclaim(struct zone *local_zone, struct zone *zone)
{
	return true;
}
#endif	/* CONFIG_NUMA */

/*
 * The restriction on ZONE_DMA32 as being a suitable zone to use to avoid
 * fragmentation is subtle. If the preferred zone was HIGHMEM then
 * premature use of a lower zone may cause lowmem pressure problems that
 * are worse than fragmentation. If the next zone is ZONE_DMA then it is
 * probably too small. It only makes sense to spread allocations to avoid
 * fragmentation between the Normal and DMA32 zones.
 */
static inline unsigned int
alloc_flags_nofragment(struct zone *zone, gfp_t gfp_mask)
{
	unsigned int alloc_flags;

	/*
	 * __GFP_KSWAPD_RECLAIM is assumed to be the same as ALLOC_KSWAPD
	 * to save a branch.
	 */
	alloc_flags = (__force int) (gfp_mask & __GFP_KSWAPD_RECLAIM);

#ifdef CONFIG_ZONE_DMA32
	if (!zone)
		return alloc_flags;

	if (zone_idx(zone) != ZONE_NORMAL)
		return alloc_flags;

	/*
	 * If ZONE_DMA32 exists, assume it is the one after ZONE_NORMAL and
	 * the pointer is within zone->zone_pgdat->node_zones[]. Also assume
	 * on UMA that if Normal is populated then so is DMA32.
	 */
	BUILD_BUG_ON(ZONE_NORMAL - ZONE_DMA32 != 1);
	if (nr_online_nodes > 1 && !populated_zone(--zone))
		return alloc_flags;

	alloc_flags |= ALLOC_NOFRAGMENT;
#endif /* CONFIG_ZONE_DMA32 */
	return alloc_flags;
}

/* Must be called after current_gfp_context() which can change gfp_mask */
static inline unsigned int gfp_to_alloc_flags_cma(gfp_t gfp_mask,
						  unsigned int alloc_flags)
{
#ifdef CONFIG_CMA
	if (gfp_migratetype(gfp_mask) == MIGRATE_MOVABLE)
		alloc_flags |= ALLOC_CMA;
#endif
	return alloc_flags;
}

/*
 * get_page_from_freelist goes through the zonelist trying to allocate
 * a page.
 */
static struct page *
get_page_from_freelist(gfp_t gfp_mask, unsigned int order, int alloc_flags,
						const struct alloc_context *ac)
{
	struct zoneref *z;
	struct zone *zone;
	struct pglist_data *last_pgdat = NULL;
	bool last_pgdat_dirty_ok = false;
	bool no_fallback;

retry:
	/*
	 * Scan zonelist, looking for a zone with enough free.
	 * See also __cpuset_node_allowed() comment in kernel/cgroup/cpuset.c.
	 */
	no_fallback = alloc_flags & ALLOC_NOFRAGMENT;
	z = ac->preferred_zoneref;
	for_next_zone_zonelist_nodemask(zone, z, ac->highest_zoneidx,
					ac->nodemask) {
		struct page *page;
		unsigned long mark;

		if (cpusets_enabled() &&
			(alloc_flags & ALLOC_CPUSET) &&
			!__cpuset_zone_allowed(zone, gfp_mask))
				continue;
		/*
		 * When allocating a page cache page for writing, we
		 * want to get it from a node that is within its dirty
		 * limit, such that no single node holds more than its
		 * proportional share of globally allowed dirty pages.
		 * The dirty limits take into account the node's
		 * lowmem reserves and high watermark so that kswapd
		 * should be able to balance it without having to
		 * write pages from its LRU list.
		 *
		 * XXX: For now, allow allocations to potentially
		 * exceed the per-node dirty limit in the slowpath
		 * (spread_dirty_pages unset) before going into reclaim,
		 * which is important when on a NUMA setup the allowed
		 * nodes are together not big enough to reach the
		 * global limit.  The proper fix for these situations
		 * will require awareness of nodes in the
		 * dirty-throttling and the flusher threads.
		 */
		if (ac->spread_dirty_pages) {
			if (last_pgdat != zone->zone_pgdat) {
				last_pgdat = zone->zone_pgdat;
				last_pgdat_dirty_ok = node_dirty_ok(zone->zone_pgdat);
			}

			if (!last_pgdat_dirty_ok)
				continue;
		}

		if (no_fallback && nr_online_nodes > 1 &&
		    zone != ac->preferred_zoneref->zone) {
			int local_nid;

			/*
			 * If moving to a remote node, retry but allow
			 * fragmenting fallbacks. Locality is more important
			 * than fragmentation avoidance.
			 */
			local_nid = zone_to_nid(ac->preferred_zoneref->zone);
			if (zone_to_nid(zone) != local_nid) {
				alloc_flags &= ~ALLOC_NOFRAGMENT;
				goto retry;
			}
		}

		mark = wmark_pages(zone, alloc_flags & ALLOC_WMARK_MASK);
		if (!zone_watermark_fast(zone, order, mark,
				       ac->highest_zoneidx, alloc_flags,
				       gfp_mask)) {
			int ret;

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
			/*
			 * Watermark failed for this zone, but see if we can
			 * grow this zone if it contains deferred pages.
			 */
			if (static_branch_unlikely(&deferred_pages)) {
				if (_deferred_grow_zone(zone, order))
					goto try_this_zone;
			}
#endif
			/* Checked here to keep the fast path fast */
			BUILD_BUG_ON(ALLOC_NO_WATERMARKS < NR_WMARK);
			if (alloc_flags & ALLOC_NO_WATERMARKS)
				goto try_this_zone;

			if (!node_reclaim_enabled() ||
			    !zone_allows_reclaim(ac->preferred_zoneref->zone, zone))
				continue;

			ret = node_reclaim(zone->zone_pgdat, gfp_mask, order);
			switch (ret) {
			case NODE_RECLAIM_NOSCAN:
				/* did not scan */
				continue;
			case NODE_RECLAIM_FULL:
				/* scanned but unreclaimable */
				continue;
			default:
				/* did we reclaim enough */
				if (zone_watermark_ok(zone, order, mark,
					ac->highest_zoneidx, alloc_flags))
					goto try_this_zone;

				continue;
			}
		}

try_this_zone:
		page = rmqueue(ac->preferred_zoneref->zone, zone, order,
				gfp_mask, alloc_flags, ac->migratetype);
		if (page) {
			prep_new_page(page, order, gfp_mask, alloc_flags);

			/*
			 * If this is a high-order atomic allocation then check
			 * if the pageblock should be reserved for the future
			 */
			if (unlikely(order && (alloc_flags & ALLOC_HARDER)))
				reserve_highatomic_pageblock(page, zone, order);

			return page;
		} else {
#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
			/* Try again if zone has deferred pages */
			if (static_branch_unlikely(&deferred_pages)) {
				if (_deferred_grow_zone(zone, order))
					goto try_this_zone;
			}
#endif
		}
	}

	/*
	 * It's possible on a UMA machine to get through all zones that are
	 * fragmented. If avoiding fragmentation, reset and try again.
	 */
	if (no_fallback) {
		alloc_flags &= ~ALLOC_NOFRAGMENT;
		goto retry;
	}

	return NULL;
}

static void warn_alloc_show_mem(gfp_t gfp_mask, nodemask_t *nodemask)
{
	unsigned int filter = SHOW_MEM_FILTER_NODES;

	/*
	 * This documents exceptions given to allocations in certain
	 * contexts that are allowed to allocate outside current's set
	 * of allowed nodes.
	 */
	if (!(gfp_mask & __GFP_NOMEMALLOC))
		if (tsk_is_oom_victim(current) ||
		    (current->flags & (PF_MEMALLOC | PF_EXITING)))
			filter &= ~SHOW_MEM_FILTER_NODES;
	if (!in_task() || !(gfp_mask & __GFP_DIRECT_RECLAIM))
		filter &= ~SHOW_MEM_FILTER_NODES;

	show_mem(filter, nodemask);
}

void warn_alloc(gfp_t gfp_mask, nodemask_t *nodemask, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	static DEFINE_RATELIMIT_STATE(nopage_rs, 10*HZ, 1);

	if ((gfp_mask & __GFP_NOWARN) ||
	     !__ratelimit(&nopage_rs) ||
	     ((gfp_mask & __GFP_DMA) && !has_managed_dma()))
		return;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_warn("%s: %pV, mode:%#x(%pGg), nodemask=%*pbl",
			current->comm, &vaf, gfp_mask, &gfp_mask,
			nodemask_pr_args(nodemask));
	va_end(args);

	cpuset_print_current_mems_allowed();
	pr_cont("\n");
	dump_stack();
	warn_alloc_show_mem(gfp_mask, nodemask);
}

static inline struct page *
__alloc_pages_cpuset_fallback(gfp_t gfp_mask, unsigned int order,
			      unsigned int alloc_flags,
			      const struct alloc_context *ac)
{
	struct page *page;

	page = get_page_from_freelist(gfp_mask, order,
			alloc_flags|ALLOC_CPUSET, ac);
	/*
	 * fallback to ignore cpuset restriction if our nodes
	 * are depleted
	 */
	if (!page)
		page = get_page_from_freelist(gfp_mask, order,
				alloc_flags, ac);

	return page;
}

static inline struct page *
__alloc_pages_may_oom(gfp_t gfp_mask, unsigned int order,
	const struct alloc_context *ac, unsigned long *did_some_progress)
{
	struct oom_control oc = {
		.zonelist = ac->zonelist,
		.nodemask = ac->nodemask,
		.memcg = NULL,
		.gfp_mask = gfp_mask,
		.order = order,
	};
	struct page *page;

	*did_some_progress = 0;

	/*
	 * Acquire the oom lock.  If that fails, somebody else is
	 * making progress for us.
	 */
	if (!mutex_trylock(&oom_lock)) {
		*did_some_progress = 1;
		schedule_timeout_uninterruptible(1);
		return NULL;
	}

	/*
	 * Go through the zonelist yet one more time, keep very high watermark
	 * here, this is only to catch a parallel oom killing, we must fail if
	 * we're still under heavy pressure. But make sure that this reclaim
	 * attempt shall not depend on __GFP_DIRECT_RECLAIM && !__GFP_NORETRY
	 * allocation which will never fail due to oom_lock already held.
	 */
	page = get_page_from_freelist((gfp_mask | __GFP_HARDWALL) &
				      ~__GFP_DIRECT_RECLAIM, order,
				      ALLOC_WMARK_HIGH|ALLOC_CPUSET, ac);
	if (page)
		goto out;

	/* Coredumps can quickly deplete all memory reserves */
	if (current->flags & PF_DUMPCORE)
		goto out;
	/* The OOM killer will not help higher order allocs */
	if (order > PAGE_ALLOC_COSTLY_ORDER)
		goto out;
	/*
	 * We have already exhausted all our reclaim opportunities without any
	 * success so it is time to admit defeat. We will skip the OOM killer
	 * because it is very likely that the caller has a more reasonable
	 * fallback than shooting a random task.
	 *
	 * The OOM killer may not free memory on a specific node.
	 */
	if (gfp_mask & (__GFP_RETRY_MAYFAIL | __GFP_THISNODE))
		goto out;
	/* The OOM killer does not needlessly kill tasks for lowmem */
	if (ac->highest_zoneidx < ZONE_NORMAL)
		goto out;
	if (pm_suspended_storage())
		goto out;
	/*
	 * XXX: GFP_NOFS allocations should rather fail than rely on
	 * other request to make a forward progress.
	 * We are in an unfortunate situation where out_of_memory cannot
	 * do much for this context but let's try it to at least get
	 * access to memory reserved if the current task is killed (see
	 * out_of_memory). Once filesystems are ready to handle allocation
	 * failures more gracefully we should just bail out here.
	 */

	/* Exhausted what can be done so it's blame time */
	if (out_of_memory(&oc) ||
	    WARN_ON_ONCE_GFP(gfp_mask & __GFP_NOFAIL, gfp_mask)) {
		*did_some_progress = 1;

		/*
		 * Help non-failing allocations by giving them access to memory
		 * reserves
		 */
		if (gfp_mask & __GFP_NOFAIL)
			page = __alloc_pages_cpuset_fallback(gfp_mask, order,
					ALLOC_NO_WATERMARKS, ac);
	}
out:
	mutex_unlock(&oom_lock);
	return page;
}

/*
 * Maximum number of compaction retries with a progress before OOM
 * killer is consider as the only way to move forward.
 */
#define MAX_COMPACT_RETRIES 16

#ifdef CONFIG_COMPACTION
/* Try memory compaction for high-order allocations before reclaim */
static struct page *
__alloc_pages_direct_compact(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum compact_priority prio, enum compact_result *compact_result)
{
	struct page *page = NULL;
	unsigned long pflags;
	unsigned int noreclaim_flag;

	if (!order)
		return NULL;

	psi_memstall_enter(&pflags);
	delayacct_compact_start();
	noreclaim_flag = memalloc_noreclaim_save();

	*compact_result = try_to_compact_pages(gfp_mask, order, alloc_flags, ac,
								prio, &page);

	memalloc_noreclaim_restore(noreclaim_flag);
	psi_memstall_leave(&pflags);
	delayacct_compact_end();

	if (*compact_result == COMPACT_SKIPPED)
		return NULL;
	/*
	 * At least in one zone compaction wasn't deferred or skipped, so let's
	 * count a compaction stall
	 */
	count_vm_event(COMPACTSTALL);

	/* Prep a captured page if available */
	if (page)
		prep_new_page(page, order, gfp_mask, alloc_flags);

	/* Try get a page from the freelist if available */
	if (!page)
		page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);

	if (page) {
		struct zone *zone = page_zone(page);

		zone->compact_blockskip_flush = false;
		compaction_defer_reset(zone, order, true);
		count_vm_event(COMPACTSUCCESS);
		return page;
	}

	/*
	 * It's bad if compaction run occurs and fails. The most likely reason
	 * is that pages exist, but not enough to satisfy watermarks.
	 */
	count_vm_event(COMPACTFAIL);

	cond_resched();

	return NULL;
}

static inline bool
should_compact_retry(struct alloc_context *ac, int order, int alloc_flags,
		     enum compact_result compact_result,
		     enum compact_priority *compact_priority,
		     int *compaction_retries)
{
	int max_retries = MAX_COMPACT_RETRIES;
	int min_priority;
	bool ret = false;
	int retries = *compaction_retries;
	enum compact_priority priority = *compact_priority;

	if (!order)
		return false;

	if (fatal_signal_pending(current))
		return false;

	if (compaction_made_progress(compact_result))
		(*compaction_retries)++;

	/*
	 * compaction considers all the zone as desperately out of memory
	 * so it doesn't really make much sense to retry except when the
	 * failure could be caused by insufficient priority
	 */
	if (compaction_failed(compact_result))
		goto check_priority;

	/*
	 * compaction was skipped because there are not enough order-0 pages
	 * to work with, so we retry only if it looks like reclaim can help.
	 */
	if (compaction_needs_reclaim(compact_result)) {
		ret = compaction_zonelist_suitable(ac, order, alloc_flags);
		goto out;
	}

	/*
	 * make sure the compaction wasn't deferred or didn't bail out early
	 * due to locks contention before we declare that we should give up.
	 * But the next retry should use a higher priority if allowed, so
	 * we don't just keep bailing out endlessly.
	 */
	if (compaction_withdrawn(compact_result)) {
		goto check_priority;
	}

	/*
	 * !costly requests are much more important than __GFP_RETRY_MAYFAIL
	 * costly ones because they are de facto nofail and invoke OOM
	 * killer to move on while costly can fail and users are ready
	 * to cope with that. 1/4 retries is rather arbitrary but we
	 * would need much more detailed feedback from compaction to
	 * make a better decision.
	 */
	if (order > PAGE_ALLOC_COSTLY_ORDER)
		max_retries /= 4;
	if (*compaction_retries <= max_retries) {
		ret = true;
		goto out;
	}

	/*
	 * Make sure there are attempts at the highest priority if we exhausted
	 * all retries or failed at the lower priorities.
	 */
check_priority:
	min_priority = (order > PAGE_ALLOC_COSTLY_ORDER) ?
			MIN_COMPACT_COSTLY_PRIORITY : MIN_COMPACT_PRIORITY;

	if (*compact_priority > min_priority) {
		(*compact_priority)--;
		*compaction_retries = 0;
		ret = true;
	}
out:
	trace_compact_retry(order, priority, compact_result, retries, max_retries, ret);
	return ret;
}
#else
static inline struct page *
__alloc_pages_direct_compact(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		enum compact_priority prio, enum compact_result *compact_result)
{
	*compact_result = COMPACT_SKIPPED;
	return NULL;
}

static inline bool
should_compact_retry(struct alloc_context *ac, unsigned int order, int alloc_flags,
		     enum compact_result compact_result,
		     enum compact_priority *compact_priority,
		     int *compaction_retries)
{
	struct zone *zone;
	struct zoneref *z;

	if (!order || order > PAGE_ALLOC_COSTLY_ORDER)
		return false;

	/*
	 * There are setups with compaction disabled which would prefer to loop
	 * inside the allocator rather than hit the oom killer prematurely.
	 * Let's give them a good hope and keep retrying while the order-0
	 * watermarks are OK.
	 */
	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist,
				ac->highest_zoneidx, ac->nodemask) {
		if (zone_watermark_ok(zone, 0, min_wmark_pages(zone),
					ac->highest_zoneidx, alloc_flags))
			return true;
	}
	return false;
}
#endif /* CONFIG_COMPACTION */

#ifdef CONFIG_LOCKDEP
static struct lockdep_map __fs_reclaim_map =
	STATIC_LOCKDEP_MAP_INIT("fs_reclaim", &__fs_reclaim_map);

static bool __need_reclaim(gfp_t gfp_mask)
{
	/* no reclaim without waiting on it */
	if (!(gfp_mask & __GFP_DIRECT_RECLAIM))
		return false;

	/* this guy won't enter reclaim */
	if (current->flags & PF_MEMALLOC)
		return false;

	if (gfp_mask & __GFP_NOLOCKDEP)
		return false;

	return true;
}

void __fs_reclaim_acquire(unsigned long ip)
{
	lock_acquire_exclusive(&__fs_reclaim_map, 0, 0, NULL, ip);
}

void __fs_reclaim_release(unsigned long ip)
{
	lock_release(&__fs_reclaim_map, ip);
}

void fs_reclaim_acquire(gfp_t gfp_mask)
{
	gfp_mask = current_gfp_context(gfp_mask);

	if (__need_reclaim(gfp_mask)) {
		if (gfp_mask & __GFP_FS)
			__fs_reclaim_acquire(_RET_IP_);

#ifdef CONFIG_MMU_NOTIFIER
		lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
		lock_map_release(&__mmu_notifier_invalidate_range_start_map);
#endif

	}
}
EXPORT_SYMBOL_GPL(fs_reclaim_acquire);

void fs_reclaim_release(gfp_t gfp_mask)
{
	gfp_mask = current_gfp_context(gfp_mask);

	if (__need_reclaim(gfp_mask)) {
		if (gfp_mask & __GFP_FS)
			__fs_reclaim_release(_RET_IP_);
	}
}
EXPORT_SYMBOL_GPL(fs_reclaim_release);
#endif

/*
 * Zonelists may change due to hotplug during allocation. Detect when zonelists
 * have been rebuilt so allocation retries. Reader side does not lock and
 * retries the allocation if zonelist changes. Writer side is protected by the
 * embedded spin_lock.
 */
static DEFINE_SEQLOCK(zonelist_update_seq);

static unsigned int zonelist_iter_begin(void)
{
	if (IS_ENABLED(CONFIG_MEMORY_HOTREMOVE))
		return read_seqbegin(&zonelist_update_seq);

	return 0;
}

static unsigned int check_retry_zonelist(unsigned int seq)
{
	if (IS_ENABLED(CONFIG_MEMORY_HOTREMOVE))
		return read_seqretry(&zonelist_update_seq, seq);

	return seq;
}

/* Perform direct synchronous page reclaim */
static unsigned long
__perform_reclaim(gfp_t gfp_mask, unsigned int order,
					const struct alloc_context *ac)
{
	unsigned int noreclaim_flag;
	unsigned long progress;

	cond_resched();

	/* We now go into synchronous reclaim */
	cpuset_memory_pressure_bump();
	fs_reclaim_acquire(gfp_mask);
	noreclaim_flag = memalloc_noreclaim_save();

	progress = try_to_free_pages(ac->zonelist, order, gfp_mask,
								ac->nodemask);

	memalloc_noreclaim_restore(noreclaim_flag);
	fs_reclaim_release(gfp_mask);

	cond_resched();

	return progress;
}

/* The really slow allocator path where we enter direct reclaim */
static inline struct page *
__alloc_pages_direct_reclaim(gfp_t gfp_mask, unsigned int order,
		unsigned int alloc_flags, const struct alloc_context *ac,
		unsigned long *did_some_progress)
{
	struct page *page = NULL;
	unsigned long pflags;
	bool drained = false;

	psi_memstall_enter(&pflags);
	*did_some_progress = __perform_reclaim(gfp_mask, order, ac);
	if (unlikely(!(*did_some_progress)))
		goto out;

retry:
	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);

	/*
	 * If an allocation failed after direct reclaim, it could be because
	 * pages are pinned on the per-cpu lists or in high alloc reserves.
	 * Shrink them and try again
	 */
	if (!page && !drained) {
		unreserve_highatomic_pageblock(ac, false);
		drain_all_pages(NULL);
		drained = true;
		goto retry;
	}
out:
	psi_memstall_leave(&pflags);

	return page;
}

static void wake_all_kswapds(unsigned int order, gfp_t gfp_mask,
			     const struct alloc_context *ac)
{
	struct zoneref *z;
	struct zone *zone;
	pg_data_t *last_pgdat = NULL;
	enum zone_type highest_zoneidx = ac->highest_zoneidx;

	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist, highest_zoneidx,
					ac->nodemask) {
		if (!managed_zone(zone))
			continue;
		if (last_pgdat != zone->zone_pgdat) {
			wakeup_kswapd(zone, gfp_mask, order, highest_zoneidx);
			last_pgdat = zone->zone_pgdat;
		}
	}
}

static inline unsigned int
gfp_to_alloc_flags(gfp_t gfp_mask)
{
	unsigned int alloc_flags = ALLOC_WMARK_MIN | ALLOC_CPUSET;

	/*
	 * __GFP_HIGH is assumed to be the same as ALLOC_HIGH
	 * and __GFP_KSWAPD_RECLAIM is assumed to be the same as ALLOC_KSWAPD
	 * to save two branches.
	 */
	BUILD_BUG_ON(__GFP_HIGH != (__force gfp_t) ALLOC_HIGH);
	BUILD_BUG_ON(__GFP_KSWAPD_RECLAIM != (__force gfp_t) ALLOC_KSWAPD);

	/*
	 * The caller may dip into page reserves a bit more if the caller
	 * cannot run direct reclaim, or if the caller has realtime scheduling
	 * policy or is asking for __GFP_HIGH memory.  GFP_ATOMIC requests will
	 * set both ALLOC_HARDER (__GFP_ATOMIC) and ALLOC_HIGH (__GFP_HIGH).
	 */
	alloc_flags |= (__force int)
		(gfp_mask & (__GFP_HIGH | __GFP_KSWAPD_RECLAIM));

	if (gfp_mask & __GFP_ATOMIC) {
		/*
		 * Not worth trying to allocate harder for __GFP_NOMEMALLOC even
		 * if it can't schedule.
		 */
		if (!(gfp_mask & __GFP_NOMEMALLOC))
			alloc_flags |= ALLOC_HARDER;
		/*
		 * Ignore cpuset mems for GFP_ATOMIC rather than fail, see the
		 * comment for __cpuset_node_allowed().
		 */
		alloc_flags &= ~ALLOC_CPUSET;
	} else if (unlikely(rt_task(current)) && in_task())
		alloc_flags |= ALLOC_HARDER;

	alloc_flags = gfp_to_alloc_flags_cma(gfp_mask, alloc_flags);

	return alloc_flags;
}

static bool oom_reserves_allowed(struct task_struct *tsk)
{
	if (!tsk_is_oom_victim(tsk))
		return false;

	/*
	 * !MMU doesn't have oom reaper so give access to memory reserves
	 * only to the thread with TIF_MEMDIE set
	 */
	if (!IS_ENABLED(CONFIG_MMU) && !test_thread_flag(TIF_MEMDIE))
		return false;

	return true;
}

/*
 * Distinguish requests which really need access to full memory
 * reserves from oom victims which can live with a portion of it
 */
static inline int __gfp_pfmemalloc_flags(gfp_t gfp_mask)
{
	if (unlikely(gfp_mask & __GFP_NOMEMALLOC))
		return 0;
	if (gfp_mask & __GFP_MEMALLOC)
		return ALLOC_NO_WATERMARKS;
	if (in_serving_softirq() && (current->flags & PF_MEMALLOC))
		return ALLOC_NO_WATERMARKS;
	if (!in_interrupt()) {
		if (current->flags & PF_MEMALLOC)
			return ALLOC_NO_WATERMARKS;
		else if (oom_reserves_allowed(current))
			return ALLOC_OOM;
	}

	return 0;
}

bool gfp_pfmemalloc_allowed(gfp_t gfp_mask)
{
	return !!__gfp_pfmemalloc_flags(gfp_mask);
}

/*
 * Checks whether it makes sense to retry the reclaim to make a forward progress
 * for the given allocation request.
 *
 * We give up when we either have tried MAX_RECLAIM_RETRIES in a row
 * without success, or when we couldn't even meet the watermark if we
 * reclaimed all remaining pages on the LRU lists.
 *
 * Returns true if a retry is viable or false to enter the oom path.
 */
static inline bool
should_reclaim_retry(gfp_t gfp_mask, unsigned order,
		     struct alloc_context *ac, int alloc_flags,
		     bool did_some_progress, int *no_progress_loops)
{
	struct zone *zone;
	struct zoneref *z;
	bool ret = false;

	/*
	 * Costly allocations might have made a progress but this doesn't mean
	 * their order will become available due to high fragmentation so
	 * always increment the no progress counter for them
	 */
	if (did_some_progress && order <= PAGE_ALLOC_COSTLY_ORDER)
		*no_progress_loops = 0;
	else
		(*no_progress_loops)++;

	/*
	 * Make sure we converge to OOM if we cannot make any progress
	 * several times in the row.
	 */
	if (*no_progress_loops > MAX_RECLAIM_RETRIES) {
		/* Before OOM, exhaust highatomic_reserve */
		return unreserve_highatomic_pageblock(ac, true);
	}

	/*
	 * Keep reclaiming pages while there is a chance this will lead
	 * somewhere.  If none of the target zones can satisfy our allocation
	 * request even if all reclaimable pages are considered then we are
	 * screwed and have to go OOM.
	 */
	for_each_zone_zonelist_nodemask(zone, z, ac->zonelist,
				ac->highest_zoneidx, ac->nodemask) {
		unsigned long available;
		unsigned long reclaimable;
		unsigned long min_wmark = min_wmark_pages(zone);
		bool wmark;

		available = reclaimable = zone_reclaimable_pages(zone);
		available += zone_page_state_snapshot(zone, NR_FREE_PAGES);

		/*
		 * Would the allocation succeed if we reclaimed all
		 * reclaimable pages?
		 */
		wmark = __zone_watermark_ok(zone, order, min_wmark,
				ac->highest_zoneidx, alloc_flags, available);
		trace_reclaim_retry_zone(z, order, reclaimable,
				available, min_wmark, *no_progress_loops, wmark);
		if (wmark) {
			ret = true;
			break;
		}
	}

	/*
	 * Memory allocation/reclaim might be called from a WQ context and the
	 * current implementation of the WQ concurrency control doesn't
	 * recognize that a particular WQ is congested if the worker thread is
	 * looping without ever sleeping. Therefore we have to do a short sleep
	 * here rather than calling cond_resched().
	 */
	if (current->flags & PF_WQ_WORKER)
		schedule_timeout_uninterruptible(1);
	else
		cond_resched();
	return ret;
}

static inline bool
check_retry_cpuset(int cpuset_mems_cookie, struct alloc_context *ac)
{
	/*
	 * It's possible that cpuset's mems_allowed and the nodemask from
	 * mempolicy don't intersect. This should be normally dealt with by
	 * policy_nodemask(), but it's possible to race with cpuset update in
	 * such a way the check therein was true, and then it became false
	 * before we got our cpuset_mems_cookie here.
	 * This assumes that for all allocations, ac->nodemask can come only
	 * from MPOL_BIND mempolicy (whose documented semantics is to be ignored
	 * when it does not intersect with the cpuset restrictions) or the
	 * caller can deal with a violated nodemask.
	 */
	if (cpusets_enabled() && ac->nodemask &&
			!cpuset_nodemask_valid_mems_allowed(ac->nodemask)) {
		ac->nodemask = NULL;
		return true;
	}

	/*
	 * When updating a task's mems_allowed or mempolicy nodemask, it is
	 * possible to race with parallel threads in such a way that our
	 * allocation can fail while the mask is being updated. If we are about
	 * to fail, check if the cpuset changed during allocation and if so,
	 * retry.
	 */
	if (read_mems_allowed_retry(cpuset_mems_cookie))
		return true;

	return false;
}

static inline struct page *
__alloc_pages_slowpath(gfp_t gfp_mask, unsigned int order,
						struct alloc_context *ac)
{
	bool can_direct_reclaim = gfp_mask & __GFP_DIRECT_RECLAIM;
	const bool costly_order = order > PAGE_ALLOC_COSTLY_ORDER;
	struct page *page = NULL;
	unsigned int alloc_flags;
	unsigned long did_some_progress;
	enum compact_priority compact_priority;
	enum compact_result compact_result;
	int compaction_retries;
	int no_progress_loops;
	unsigned int cpuset_mems_cookie;
	unsigned int zonelist_iter_cookie;
	int reserve_flags;

	/*
	 * We also sanity check to catch abuse of atomic reserves being used by
	 * callers that are not in atomic context.
	 */
	if (WARN_ON_ONCE((gfp_mask & (__GFP_ATOMIC|__GFP_DIRECT_RECLAIM)) ==
				(__GFP_ATOMIC|__GFP_DIRECT_RECLAIM)))
		gfp_mask &= ~__GFP_ATOMIC;

restart:
	compaction_retries = 0;
	no_progress_loops = 0;
	compact_priority = DEF_COMPACT_PRIORITY;
	cpuset_mems_cookie = read_mems_allowed_begin();
	zonelist_iter_cookie = zonelist_iter_begin();

	/*
	 * The fast path uses conservative alloc_flags to succeed only until
	 * kswapd needs to be woken up, and to avoid the cost of setting up
	 * alloc_flags precisely. So we do that now.
	 */
	alloc_flags = gfp_to_alloc_flags(gfp_mask);

	/*
	 * We need to recalculate the starting point for the zonelist iterator
	 * because we might have used different nodemask in the fast path, or
	 * there was a cpuset modification and we are retrying - otherwise we
	 * could end up iterating over non-eligible zones endlessly.
	 */
	ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->highest_zoneidx, ac->nodemask);
	if (!ac->preferred_zoneref->zone)
		goto nopage;

	/*
	 * Check for insane configurations where the cpuset doesn't contain
	 * any suitable zone to satisfy the request - e.g. non-movable
	 * GFP_HIGHUSER allocations from MOVABLE nodes only.
	 */
	if (cpusets_insane_config() && (gfp_mask & __GFP_HARDWALL)) {
		struct zoneref *z = first_zones_zonelist(ac->zonelist,
					ac->highest_zoneidx,
					&cpuset_current_mems_allowed);
		if (!z->zone)
			goto nopage;
	}

	if (alloc_flags & ALLOC_KSWAPD)
		wake_all_kswapds(order, gfp_mask, ac);

	/*
	 * The adjusted alloc_flags might result in immediate success, so try
	 * that first
	 */
	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	if (page)
		goto got_pg;

	/*
	 * For costly allocations, try direct compaction first, as it's likely
	 * that we have enough base pages and don't need to reclaim. For non-
	 * movable high-order allocations, do that as well, as compaction will
	 * try prevent permanent fragmentation by migrating from blocks of the
	 * same migratetype.
	 * Don't try this for allocations that are allowed to ignore
	 * watermarks, as the ALLOC_NO_WATERMARKS attempt didn't yet happen.
	 */
	if (can_direct_reclaim &&
			(costly_order ||
			   (order > 0 && ac->migratetype != MIGRATE_MOVABLE))
			&& !gfp_pfmemalloc_allowed(gfp_mask)) {
		page = __alloc_pages_direct_compact(gfp_mask, order,
						alloc_flags, ac,
						INIT_COMPACT_PRIORITY,
						&compact_result);
		if (page)
			goto got_pg;

		/*
		 * Checks for costly allocations with __GFP_NORETRY, which
		 * includes some THP page fault allocations
		 */
		if (costly_order && (gfp_mask & __GFP_NORETRY)) {
			/*
			 * If allocating entire pageblock(s) and compaction
			 * failed because all zones are below low watermarks
			 * or is prohibited because it recently failed at this
			 * order, fail immediately unless the allocator has
			 * requested compaction and reclaim retry.
			 *
			 * Reclaim is
			 *  - potentially very expensive because zones are far
			 *    below their low watermarks or this is part of very
			 *    bursty high order allocations,
			 *  - not guaranteed to help because isolate_freepages()
			 *    may not iterate over freed pages as part of its
			 *    linear scan, and
			 *  - unlikely to make entire pageblocks free on its
			 *    own.
			 */
			if (compact_result == COMPACT_SKIPPED ||
			    compact_result == COMPACT_DEFERRED)
				goto nopage;

			/*
			 * Looks like reclaim/compaction is worth trying, but
			 * sync compaction could be very expensive, so keep
			 * using async compaction.
			 */
			compact_priority = INIT_COMPACT_PRIORITY;
		}
	}

retry:
	/* Ensure kswapd doesn't accidentally go to sleep as long as we loop */
	if (alloc_flags & ALLOC_KSWAPD)
		wake_all_kswapds(order, gfp_mask, ac);

	reserve_flags = __gfp_pfmemalloc_flags(gfp_mask);
	if (reserve_flags)
		alloc_flags = gfp_to_alloc_flags_cma(gfp_mask, reserve_flags);

	/*
	 * Reset the nodemask and zonelist iterators if memory policies can be
	 * ignored. These allocations are high priority and system rather than
	 * user oriented.
	 */
	if (!(alloc_flags & ALLOC_CPUSET) || reserve_flags) {
		ac->nodemask = NULL;
		ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->highest_zoneidx, ac->nodemask);
	}

	/* Attempt with potentially adjusted zonelist and alloc_flags */
	page = get_page_from_freelist(gfp_mask, order, alloc_flags, ac);
	if (page)
		goto got_pg;

	/* Caller is not willing to reclaim, we can't balance anything */
	if (!can_direct_reclaim)
		goto nopage;

	/* Avoid recursion of direct reclaim */
	if (current->flags & PF_MEMALLOC)
		goto nopage;

	/* Try direct reclaim and then allocating */
	page = __alloc_pages_direct_reclaim(gfp_mask, order, alloc_flags, ac,
							&did_some_progress);
	if (page)
		goto got_pg;

	/* Try direct compaction and then allocating */
	page = __alloc_pages_direct_compact(gfp_mask, order, alloc_flags, ac,
					compact_priority, &compact_result);
	if (page)
		goto got_pg;

	/* Do not loop if specifically requested */
	if (gfp_mask & __GFP_NORETRY)
		goto nopage;

	/*
	 * Do not retry costly high order allocations unless they are
	 * __GFP_RETRY_MAYFAIL
	 */
	if (costly_order && !(gfp_mask & __GFP_RETRY_MAYFAIL))
		goto nopage;

	if (should_reclaim_retry(gfp_mask, order, ac, alloc_flags,
				 did_some_progress > 0, &no_progress_loops))
		goto retry;

	/*
	 * It doesn't make any sense to retry for the compaction if the order-0
	 * reclaim is not able to make any progress because the current
	 * implementation of the compaction depends on the sufficient amount
	 * of free memory (see __compaction_suitable)
	 */
	if (did_some_progress > 0 &&
			should_compact_retry(ac, order, alloc_flags,
				compact_result, &compact_priority,
				&compaction_retries))
		goto retry;


	/*
	 * Deal with possible cpuset update races or zonelist updates to avoid
	 * a unnecessary OOM kill.
	 */
	if (check_retry_cpuset(cpuset_mems_cookie, ac) ||
	    check_retry_zonelist(zonelist_iter_cookie))
		goto restart;

	/* Reclaim has failed us, start killing things */
	page = __alloc_pages_may_oom(gfp_mask, order, ac, &did_some_progress);
	if (page)
		goto got_pg;

	/* Avoid allocations with no watermarks from looping endlessly */
	if (tsk_is_oom_victim(current) &&
	    (alloc_flags & ALLOC_OOM ||
	     (gfp_mask & __GFP_NOMEMALLOC)))
		goto nopage;

	/* Retry as long as the OOM killer is making progress */
	if (did_some_progress) {
		no_progress_loops = 0;
		goto retry;
	}

nopage:
	/*
	 * Deal with possible cpuset update races or zonelist updates to avoid
	 * a unnecessary OOM kill.
	 */
	if (check_retry_cpuset(cpuset_mems_cookie, ac) ||
	    check_retry_zonelist(zonelist_iter_cookie))
		goto restart;

	/*
	 * Make sure that __GFP_NOFAIL request doesn't leak out and make sure
	 * we always retry
	 */
	if (gfp_mask & __GFP_NOFAIL) {
		/*
		 * All existing users of the __GFP_NOFAIL are blockable, so warn
		 * of any new users that actually require GFP_NOWAIT
		 */
		if (WARN_ON_ONCE_GFP(!can_direct_reclaim, gfp_mask))
			goto fail;

		/*
		 * PF_MEMALLOC request from this context is rather bizarre
		 * because we cannot reclaim anything and only can loop waiting
		 * for somebody to do a work for us
		 */
		WARN_ON_ONCE_GFP(current->flags & PF_MEMALLOC, gfp_mask);

		/*
		 * non failing costly orders are a hard requirement which we
		 * are not prepared for much so let's warn about these users
		 * so that we can identify them and convert them to something
		 * else.
		 */
		WARN_ON_ONCE_GFP(order > PAGE_ALLOC_COSTLY_ORDER, gfp_mask);

		/*
		 * Help non-failing allocations by giving them access to memory
		 * reserves but do not use ALLOC_NO_WATERMARKS because this
		 * could deplete whole memory reserves which would just make
		 * the situation worse
		 */
		page = __alloc_pages_cpuset_fallback(gfp_mask, order, ALLOC_HARDER, ac);
		if (page)
			goto got_pg;

		cond_resched();
		goto retry;
	}
fail:
	warn_alloc(gfp_mask, ac->nodemask,
			"page allocation failure: order:%u", order);
got_pg:
	return page;
}

static inline bool prepare_alloc_pages(gfp_t gfp_mask, unsigned int order,
		int preferred_nid, nodemask_t *nodemask,
		struct alloc_context *ac, gfp_t *alloc_gfp,
		unsigned int *alloc_flags)
{
	ac->highest_zoneidx = gfp_zone(gfp_mask);
	ac->zonelist = node_zonelist(preferred_nid, gfp_mask);
	ac->nodemask = nodemask;
	ac->migratetype = gfp_migratetype(gfp_mask);

	if (cpusets_enabled()) {
		*alloc_gfp |= __GFP_HARDWALL;
		/*
		 * When we are in the interrupt context, it is irrelevant
		 * to the current task context. It means that any node ok.
		 */
		if (in_task() && !ac->nodemask)
			ac->nodemask = &cpuset_current_mems_allowed;
		else
			*alloc_flags |= ALLOC_CPUSET;
	}

	might_alloc(gfp_mask);

	if (should_fail_alloc_page(gfp_mask, order))
		return false;

	*alloc_flags = gfp_to_alloc_flags_cma(gfp_mask, *alloc_flags);

	/* Dirty zone balancing only done in the fast path */
	ac->spread_dirty_pages = (gfp_mask & __GFP_WRITE);

	/*
	 * The preferred zone is used for statistics but crucially it is
	 * also used as the starting point for the zonelist iterator. It
	 * may get reset for allocations that ignore memory policies.
	 */
	ac->preferred_zoneref = first_zones_zonelist(ac->zonelist,
					ac->highest_zoneidx, ac->nodemask);

	return true;
}

/*
 * __alloc_pages_bulk - Allocate a number of order-0 pages to a list or array
 * @gfp: GFP flags for the allocation
 * @preferred_nid: The preferred NUMA node ID to allocate from
 * @nodemask: Set of nodes to allocate from, may be NULL
 * @nr_pages: The number of pages desired on the list or array
 * @page_list: Optional list to store the allocated pages
 * @page_array: Optional array to store the pages
 *
 * This is a batched version of the page allocator that attempts to
 * allocate nr_pages quickly. Pages are added to page_list if page_list
 * is not NULL, otherwise it is assumed that the page_array is valid.
 *
 * For lists, nr_pages is the number of pages that should be allocated.
 *
 * For arrays, only NULL elements are populated with pages and nr_pages
 * is the maximum number of pages that will be stored in the array.
 *
 * Returns the number of pages on the list or array.
 */
unsigned long __alloc_pages_bulk(gfp_t gfp, int preferred_nid,
			nodemask_t *nodemask, int nr_pages,
			struct list_head *page_list,
			struct page **page_array)
{
	struct page *page;
	unsigned long flags;
	unsigned long __maybe_unused UP_flags;
	struct zone *zone;
	struct zoneref *z;
	struct per_cpu_pages *pcp;
	struct list_head *pcp_list;
	struct alloc_context ac;
	gfp_t alloc_gfp;
	unsigned int alloc_flags = ALLOC_WMARK_LOW;
	int nr_populated = 0, nr_account = 0;

	/*
	 * Skip populated array elements to determine if any pages need
	 * to be allocated before disabling IRQs.
	 */
	while (page_array && nr_populated < nr_pages && page_array[nr_populated])
		nr_populated++;

	/* No pages requested? */
	if (unlikely(nr_pages <= 0))
		goto out;

	/* Already populated array? */
	if (unlikely(page_array && nr_pages - nr_populated == 0))
		goto out;

	/* Bulk allocator does not support memcg accounting. */
	if (memcg_kmem_enabled() && (gfp & __GFP_ACCOUNT))
		goto failed;

	/* Use the single page allocator for one page. */
	if (nr_pages - nr_populated == 1)
		goto failed;

#ifdef CONFIG_PAGE_OWNER
	/*
	 * PAGE_OWNER may recurse into the allocator to allocate space to
	 * save the stack with pagesets.lock held. Releasing/reacquiring
	 * removes much of the performance benefit of bulk allocation so
	 * force the caller to allocate one page at a time as it'll have
	 * similar performance to added complexity to the bulk allocator.
	 */
	if (static_branch_unlikely(&page_owner_inited))
		goto failed;
#endif

	/* May set ALLOC_NOFRAGMENT, fragmentation will return 1 page. */
	gfp &= gfp_allowed_mask;
	alloc_gfp = gfp;
	if (!prepare_alloc_pages(gfp, 0, preferred_nid, nodemask, &ac, &alloc_gfp, &alloc_flags))
		goto out;
	gfp = alloc_gfp;

	/* Find an allowed local zone that meets the low watermark. */
	for_each_zone_zonelist_nodemask(zone, z, ac.zonelist, ac.highest_zoneidx, ac.nodemask) {
		unsigned long mark;

		if (cpusets_enabled() && (alloc_flags & ALLOC_CPUSET) &&
		    !__cpuset_zone_allowed(zone, gfp)) {
			continue;
		}

		if (nr_online_nodes > 1 && zone != ac.preferred_zoneref->zone &&
		    zone_to_nid(zone) != zone_to_nid(ac.preferred_zoneref->zone)) {
			goto failed;
		}

		mark = wmark_pages(zone, alloc_flags & ALLOC_WMARK_MASK) + nr_pages;
		if (zone_watermark_fast(zone, 0,  mark,
				zonelist_zone_idx(ac.preferred_zoneref),
				alloc_flags, gfp)) {
			break;
		}
	}

	/*
	 * If there are no allowed local zones that meets the watermarks then
	 * try to allocate a single page and reclaim if necessary.
	 */
	if (unlikely(!zone))
		goto failed;

	/* Is a parallel drain in progress? */
	pcp_trylock_prepare(UP_flags);
	pcp = pcp_spin_trylock_irqsave(zone->per_cpu_pageset, flags);
	if (!pcp)
		goto failed_irq;

	/* Attempt the batch allocation */
	pcp_list = &pcp->lists[order_to_pindex(ac.migratetype, 0)];
	while (nr_populated < nr_pages) {

		/* Skip existing pages */
		if (page_array && page_array[nr_populated]) {
			nr_populated++;
			continue;
		}

		page = __rmqueue_pcplist(zone, 0, ac.migratetype, alloc_flags,
								pcp, pcp_list);
		if (unlikely(!page)) {
			/* Try and allocate at least one page */
			if (!nr_account) {
				pcp_spin_unlock_irqrestore(pcp, flags);
				goto failed_irq;
			}
			break;
		}
		nr_account++;

		prep_new_page(page, 0, gfp, 0);
		if (page_list)
			list_add(&page->lru, page_list);
		else
			page_array[nr_populated] = page;
		nr_populated++;
	}

	pcp_spin_unlock_irqrestore(pcp, flags);
	pcp_trylock_finish(UP_flags);

	__count_zid_vm_events(PGALLOC, zone_idx(zone), nr_account);
	zone_statistics(ac.preferred_zoneref->zone, zone, nr_account);

out:
	return nr_populated;

failed_irq:
	pcp_trylock_finish(UP_flags);

failed:
	page = __alloc_pages(gfp, 0, preferred_nid, nodemask);
	if (page) {
		if (page_list)
			list_add(&page->lru, page_list);
		else
			page_array[nr_populated] = page;
		nr_populated++;
	}

	goto out;
}
EXPORT_SYMBOL_GPL(__alloc_pages_bulk);

/*
 * This is the 'heart' of the zoned buddy allocator.
 */
struct page *__alloc_pages(gfp_t gfp, unsigned int order, int preferred_nid,
							nodemask_t *nodemask)
{
	struct page *page;
	unsigned int alloc_flags = ALLOC_WMARK_LOW;
	gfp_t alloc_gfp; /* The gfp_t that was actually used for allocation */
	struct alloc_context ac = { };

	/*
	 * There are several places where we assume that the order value is sane
	 * so bail out early if the request is out of bound.
	 */
	if (WARN_ON_ONCE_GFP(order >= MAX_ORDER, gfp))
		return NULL;

	gfp &= gfp_allowed_mask;
	/*
	 * Apply scoped allocation constraints. This is mainly about GFP_NOFS
	 * resp. GFP_NOIO which has to be inherited for all allocation requests
	 * from a particular context which has been marked by
	 * memalloc_no{fs,io}_{save,restore}. And PF_MEMALLOC_PIN which ensures
	 * movable zones are not used during allocation.
	 */
	gfp = current_gfp_context(gfp);
	alloc_gfp = gfp;
	if (!prepare_alloc_pages(gfp, order, preferred_nid, nodemask, &ac,
			&alloc_gfp, &alloc_flags))
		return NULL;

	/*
	 * Forbid the first pass from falling back to types that fragment
	 * memory until all local zones are considered.
	 */
	alloc_flags |= alloc_flags_nofragment(ac.preferred_zoneref->zone, gfp);

	/* First allocation attempt */
	page = get_page_from_freelist(alloc_gfp, order, alloc_flags, &ac);
	if (likely(page))
		goto out;

	alloc_gfp = gfp;
	ac.spread_dirty_pages = false;

	/*
	 * Restore the original nodemask if it was potentially replaced with
	 * &cpuset_current_mems_allowed to optimize the fast-path attempt.
	 */
	ac.nodemask = nodemask;

	page = __alloc_pages_slowpath(alloc_gfp, order, &ac);

out:
	if (memcg_kmem_enabled() && (gfp & __GFP_ACCOUNT) && page &&
	    unlikely(__memcg_kmem_charge_page(page, gfp, order) != 0)) {
		__free_pages(page, order);
		page = NULL;
	}

	trace_mm_page_alloc(page, order, alloc_gfp, ac.migratetype);

	return page;
}
EXPORT_SYMBOL(__alloc_pages);

struct folio *__folio_alloc(gfp_t gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask)
{
	struct page *page = __alloc_pages(gfp | __GFP_COMP, order,
			preferred_nid, nodemask);

	if (page && order > 1)
		prep_transhuge_page(page);
	return (struct folio *)page;
}
EXPORT_SYMBOL(__folio_alloc);

/*
 * Common helper functions. Never use with __GFP_HIGHMEM because the returned
 * address cannot represent highmem pages. Use alloc_pages and then kmap if
 * you need to access high mem.
 */
unsigned long __get_free_pages(gfp_t gfp_mask, unsigned int order)
{
	struct page *page;

	page = alloc_pages(gfp_mask & ~__GFP_HIGHMEM, order);
	if (!page)
		return 0;
	return (unsigned long) page_address(page);
}
EXPORT_SYMBOL(__get_free_pages);

unsigned long get_zeroed_page(gfp_t gfp_mask)
{
	return __get_free_pages(gfp_mask | __GFP_ZERO, 0);
}
EXPORT_SYMBOL(get_zeroed_page);

/**
 * __free_pages - Free pages allocated with alloc_pages().
 * @page: The page pointer returned from alloc_pages().
 * @order: The order of the allocation.
 *
 * This function can free multi-page allocations that are not compound
 * pages.  It does not check that the @order passed in matches that of
 * the allocation, so it is easy to leak memory.  Freeing more memory
 * than was allocated will probably emit a warning.
 *
 * If the last reference to this page is speculative, it will be released
 * by put_page() which only frees the first page of a non-compound
 * allocation.  To prevent the remaining pages from being leaked, we free
 * the subsequent pages here.  If you want to use the page's reference
 * count to decide when to free the allocation, you should allocate a
 * compound page, and use put_page() instead of __free_pages().
 *
 * Context: May be called in interrupt context or while holding a normal
 * spinlock, but not in NMI context or while holding a raw spinlock.
 */
void __free_pages(struct page *page, unsigned int order)
{
	if (put_page_testzero(page))
		free_the_page(page, order);
	else if (!PageHead(page))
		while (order-- > 0)
			free_the_page(page + (1 << order), order);
}
EXPORT_SYMBOL(__free_pages);

void free_pages(unsigned long addr, unsigned int order)
{
	if (addr != 0) {
		VM_BUG_ON(!virt_addr_valid((void *)addr));
		__free_pages(virt_to_page((void *)addr), order);
	}
}

EXPORT_SYMBOL(free_pages);

/*
 * Page Fragment:
 *  An arbitrary-length arbitrary-offset area of memory which resides
 *  within a 0 or higher order page.  Multiple fragments within that page
 *  are individually refcounted, in the page's reference counter.
 *
 * The page_frag functions below provide a simple allocation framework for
 * page fragments.  This is used by the network stack and network device
 * drivers to provide a backing region of memory for use as either an
 * sk_buff->head, or to be used in the "frags" portion of skb_shared_info.
 */
static struct page *__page_frag_cache_refill(struct page_frag_cache *nc,
					     gfp_t gfp_mask)
{
	struct page *page = NULL;
	gfp_t gfp = gfp_mask;

#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
	gfp_mask |= __GFP_COMP | __GFP_NOWARN | __GFP_NORETRY |
		    __GFP_NOMEMALLOC;
	page = alloc_pages_node(NUMA_NO_NODE, gfp_mask,
				PAGE_FRAG_CACHE_MAX_ORDER);
	nc->size = page ? PAGE_FRAG_CACHE_MAX_SIZE : PAGE_SIZE;
#endif
	if (unlikely(!page))
		page = alloc_pages_node(NUMA_NO_NODE, gfp, 0);

	nc->va = page ? page_address(page) : NULL;

	return page;
}

void __page_frag_cache_drain(struct page *page, unsigned int count)
{
	VM_BUG_ON_PAGE(page_ref_count(page) == 0, page);

	if (page_ref_sub_and_test(page, count))
		free_the_page(page, compound_order(page));
}
EXPORT_SYMBOL(__page_frag_cache_drain);

void *page_frag_alloc_align(struct page_frag_cache *nc,
		      unsigned int fragsz, gfp_t gfp_mask,
		      unsigned int align_mask)
{
	unsigned int size = PAGE_SIZE;
	struct page *page;
	int offset;

	if (unlikely(!nc->va)) {
refill:
		page = __page_frag_cache_refill(nc, gfp_mask);
		if (!page)
			return NULL;

#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
		/* if size can vary use size else just use PAGE_SIZE */
		size = nc->size;
#endif
		/* Even if we own the page, we do not use atomic_set().
		 * This would break get_page_unless_zero() users.
		 */
		page_ref_add(page, PAGE_FRAG_CACHE_MAX_SIZE);

		/* reset page count bias and offset to start of new frag */
		nc->pfmemalloc = page_is_pfmemalloc(page);
		nc->pagecnt_bias = PAGE_FRAG_CACHE_MAX_SIZE + 1;
		nc->offset = size;
	}

	offset = nc->offset - fragsz;
	if (unlikely(offset < 0)) {
		page = virt_to_page(nc->va);

		if (!page_ref_sub_and_test(page, nc->pagecnt_bias))
			goto refill;

		if (unlikely(nc->pfmemalloc)) {
			free_the_page(page, compound_order(page));
			goto refill;
		}

#if (PAGE_SIZE < PAGE_FRAG_CACHE_MAX_SIZE)
		/* if size can vary use size else just use PAGE_SIZE */
		size = nc->size;
#endif
		/* OK, page count is 0, we can safely set it */
		set_page_count(page, PAGE_FRAG_CACHE_MAX_SIZE + 1);

		/* reset page count bias and offset to start of new frag */
		nc->pagecnt_bias = PAGE_FRAG_CACHE_MAX_SIZE + 1;
		offset = size - fragsz;
		if (unlikely(offset < 0)) {
			/*
			 * The caller is trying to allocate a fragment
			 * with fragsz > PAGE_SIZE but the cache isn't big
			 * enough to satisfy the request, this may
			 * happen in low memory conditions.
			 * We don't release the cache page because
			 * it could make memory pressure worse
			 * so we simply return NULL here.
			 */
			return NULL;
		}
	}

	nc->pagecnt_bias--;
	offset &= align_mask;
	nc->offset = offset;

	return nc->va + offset;
}
EXPORT_SYMBOL(page_frag_alloc_align);

/*
 * Frees a page fragment allocated out of either a compound or order 0 page.
 */
void page_frag_free(void *addr)
{
	struct page *page = virt_to_head_page(addr);

	if (unlikely(put_page_testzero(page)))
		free_the_page(page, compound_order(page));
}
EXPORT_SYMBOL(page_frag_free);

static void *make_alloc_exact(unsigned long addr, unsigned int order,
		size_t size)
{
	if (addr) {
		unsigned long alloc_end = addr + (PAGE_SIZE << order);
		unsigned long used = addr + PAGE_ALIGN(size);

		split_page(virt_to_page((void *)addr), order);
		while (used < alloc_end) {
			free_page(used);
			used += PAGE_SIZE;
		}
	}
	return (void *)addr;
}

/**
 * alloc_pages_exact - allocate an exact number physically-contiguous pages.
 * @size: the number of bytes to allocate
 * @gfp_mask: GFP flags for the allocation, must not contain __GFP_COMP
 *
 * This function is similar to alloc_pages(), except that it allocates the
 * minimum number of pages to satisfy the request.  alloc_pages() can only
 * allocate memory in power-of-two pages.
 *
 * This function is also limited by MAX_ORDER.
 *
 * Memory allocated by this function must be released by free_pages_exact().
 *
 * Return: pointer to the allocated area or %NULL in case of error.
 */
void *alloc_pages_exact(size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	unsigned long addr;

	if (WARN_ON_ONCE(gfp_mask & (__GFP_COMP | __GFP_HIGHMEM)))
		gfp_mask &= ~(__GFP_COMP | __GFP_HIGHMEM);

	addr = __get_free_pages(gfp_mask, order);
	return make_alloc_exact(addr, order, size);
}
EXPORT_SYMBOL(alloc_pages_exact);

/**
 * alloc_pages_exact_nid - allocate an exact number of physically-contiguous
 *			   pages on a node.
 * @nid: the preferred node ID where memory should be allocated
 * @size: the number of bytes to allocate
 * @gfp_mask: GFP flags for the allocation, must not contain __GFP_COMP
 *
 * Like alloc_pages_exact(), but try to allocate on node nid first before falling
 * back.
 *
 * Return: pointer to the allocated area or %NULL in case of error.
 */
void * __meminit alloc_pages_exact_nid(int nid, size_t size, gfp_t gfp_mask)
{
	unsigned int order = get_order(size);
	struct page *p;

	if (WARN_ON_ONCE(gfp_mask & (__GFP_COMP | __GFP_HIGHMEM)))
		gfp_mask &= ~(__GFP_COMP | __GFP_HIGHMEM);

	p = alloc_pages_node(nid, gfp_mask, order);
	if (!p)
		return NULL;
	return make_alloc_exact((unsigned long)page_address(p), order, size);
}

/**
 * free_pages_exact - release memory allocated via alloc_pages_exact()
 * @virt: the value returned by alloc_pages_exact.
 * @size: size of allocation, same value as passed to alloc_pages_exact().
 *
 * Release the memory allocated by a previous call to alloc_pages_exact.
 */
void free_pages_exact(void *virt, size_t size)
{
	unsigned long addr = (unsigned long)virt;
	unsigned long end = addr + PAGE_ALIGN(size);

	while (addr < end) {
		free_page(addr);
		addr += PAGE_SIZE;
	}
}
EXPORT_SYMBOL(free_pages_exact);

/**
 * nr_free_zone_pages - count number of pages beyond high watermark
 * @offset: The zone index of the highest zone
 *
 * nr_free_zone_pages() counts the number of pages which are beyond the
 * high watermark within all zones at or below a given zone index.  For each
 * zone, the number of pages is calculated as:
 *
 *     nr_free_zone_pages = managed_pages - high_pages
 *
 * Return: number of pages beyond high watermark.
 */
static unsigned long nr_free_zone_pages(int offset)
{
	struct zoneref *z;
	struct zone *zone;

	/* Just pick one node, since fallback list is circular */
	unsigned long sum = 0;

	struct zonelist *zonelist = node_zonelist(numa_node_id(), GFP_KERNEL);

	for_each_zone_zonelist(zone, z, zonelist, offset) {
		unsigned long size = zone_managed_pages(zone);
		unsigned long high = high_wmark_pages(zone);
		if (size > high)
			sum += size - high;
	}

	return sum;
}

/**
 * nr_free_buffer_pages - count number of pages beyond high watermark
 *
 * nr_free_buffer_pages() counts the number of pages which are beyond the high
 * watermark within ZONE_DMA and ZONE_NORMAL.
 *
 * Return: number of pages beyond high watermark within ZONE_DMA and
 * ZONE_NORMAL.
 */
unsigned long nr_free_buffer_pages(void)
{
	return nr_free_zone_pages(gfp_zone(GFP_USER));
}
EXPORT_SYMBOL_GPL(nr_free_buffer_pages);

static inline void show_node(struct zone *zone)
{
	if (IS_ENABLED(CONFIG_NUMA))
		printk("Node %d ", zone_to_nid(zone));
}

long si_mem_available(void)
{
	long available;
	unsigned long pagecache;
	unsigned long wmark_low = 0;
	unsigned long pages[NR_LRU_LISTS];
	unsigned long reclaimable;
	struct zone *zone;
	int lru;

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_node_page_state(NR_LRU_BASE + lru);

	for_each_zone(zone)
		wmark_low += low_wmark_pages(zone);

	/*
	 * Estimate the amount of memory available for userspace allocations,
	 * without causing swapping or OOM.
	 */
	available = global_zone_page_state(NR_FREE_PAGES) - totalreserve_pages;

	/*
	 * Not all the page cache can be freed, otherwise the system will
	 * start swapping or thrashing. Assume at least half of the page
	 * cache, or the low watermark worth of cache, needs to stay.
	 */
	pagecache = pages[LRU_ACTIVE_FILE] + pages[LRU_INACTIVE_FILE];
	pagecache -= min(pagecache / 2, wmark_low);
	available += pagecache;

	/*
	 * Part of the reclaimable slab and other kernel memory consists of
	 * items that are in use, and cannot be freed. Cap this estimate at the
	 * low watermark.
	 */
	reclaimable = global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B) +
		global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE);
	available += reclaimable - min(reclaimable / 2, wmark_low);

	if (available < 0)
		available = 0;
	return available;
}
EXPORT_SYMBOL_GPL(si_mem_available);

void si_meminfo(struct sysinfo *val)
{
	val->totalram = totalram_pages();
	val->sharedram = global_node_page_state(NR_SHMEM);
	val->freeram = global_zone_page_state(NR_FREE_PAGES);
	val->bufferram = nr_blockdev_pages();
	val->totalhigh = totalhigh_pages();
	val->freehigh = nr_free_highpages();
	val->mem_unit = PAGE_SIZE;
}

EXPORT_SYMBOL(si_meminfo);

#ifdef CONFIG_NUMA
void si_meminfo_node(struct sysinfo *val, int nid)
{
	int zone_type;		/* needs to be signed */
	unsigned long managed_pages = 0;
	unsigned long managed_highpages = 0;
	unsigned long free_highpages = 0;
	pg_data_t *pgdat = NODE_DATA(nid);

	for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++)
		managed_pages += zone_managed_pages(&pgdat->node_zones[zone_type]);
	val->totalram = managed_pages;
	val->sharedram = node_page_state(pgdat, NR_SHMEM);
	val->freeram = sum_zone_node_page_state(nid, NR_FREE_PAGES);
#ifdef CONFIG_HIGHMEM
	for (zone_type = 0; zone_type < MAX_NR_ZONES; zone_type++) {
		struct zone *zone = &pgdat->node_zones[zone_type];

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
 * Determine whether the node should be displayed or not, depending on whether
 * SHOW_MEM_FILTER_NODES was passed to show_free_areas().
 */
static bool show_mem_node_skip(unsigned int flags, int nid, nodemask_t *nodemask)
{
	if (!(flags & SHOW_MEM_FILTER_NODES))
		return false;

	/*
	 * no node mask - aka implicit memory numa policy. Do not bother with
	 * the synchronization - read_mems_allowed_begin - because we do not
	 * have to be precise here.
	 */
	if (!nodemask)
		nodemask = &cpuset_current_mems_allowed;

	return !node_isset(nid, *nodemask);
}

#define K(x) ((x) << (PAGE_SHIFT-10))

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

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 *
 * Bits in @filter:
 * SHOW_MEM_FILTER_NODES: suppress nodes that are not allowed by current's
 *   cpuset.
 */
void show_free_areas(unsigned int filter, nodemask_t *nodemask)
{
	unsigned long free_pcp = 0;
	int cpu, nid;
	struct zone *zone;
	pg_data_t *pgdat;

	for_each_populated_zone(zone) {
		if (show_mem_node_skip(filter, zone_to_nid(zone), nodemask))
			continue;

		for_each_online_cpu(cpu)
			free_pcp += per_cpu_ptr(zone->per_cpu_pageset, cpu)->count;
	}

	printk("active_anon:%lu inactive_anon:%lu isolated_anon:%lu\n"
		" active_file:%lu inactive_file:%lu isolated_file:%lu\n"
		" unevictable:%lu dirty:%lu writeback:%lu\n"
		" slab_reclaimable:%lu slab_unreclaimable:%lu\n"
		" mapped:%lu shmem:%lu pagetables:%lu bounce:%lu\n"
		" kernel_misc_reclaimable:%lu\n"
		" free:%lu free_pcp:%lu free_cma:%lu\n",
		global_node_page_state(NR_ACTIVE_ANON),
		global_node_page_state(NR_INACTIVE_ANON),
		global_node_page_state(NR_ISOLATED_ANON),
		global_node_page_state(NR_ACTIVE_FILE),
		global_node_page_state(NR_INACTIVE_FILE),
		global_node_page_state(NR_ISOLATED_FILE),
		global_node_page_state(NR_UNEVICTABLE),
		global_node_page_state(NR_FILE_DIRTY),
		global_node_page_state(NR_WRITEBACK),
		global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B),
		global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B),
		global_node_page_state(NR_FILE_MAPPED),
		global_node_page_state(NR_SHMEM),
		global_node_page_state(NR_PAGETABLE),
		global_zone_page_state(NR_BOUNCE),
		global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE),
		global_zone_page_state(NR_FREE_PAGES),
		free_pcp,
		global_zone_page_state(NR_FREE_CMA_PAGES));

	for_each_online_pgdat(pgdat) {
		if (show_mem_node_skip(filter, pgdat->node_id, nodemask))
			continue;

		printk("Node %d"
			" active_anon:%lukB"
			" inactive_anon:%lukB"
			" active_file:%lukB"
			" inactive_file:%lukB"
			" unevictable:%lukB"
			" isolated(anon):%lukB"
			" isolated(file):%lukB"
			" mapped:%lukB"
			" dirty:%lukB"
			" writeback:%lukB"
			" shmem:%lukB"
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			" shmem_thp: %lukB"
			" shmem_pmdmapped: %lukB"
			" anon_thp: %lukB"
#endif
			" writeback_tmp:%lukB"
			" kernel_stack:%lukB"
#ifdef CONFIG_SHADOW_CALL_STACK
			" shadow_call_stack:%lukB"
#endif
			" pagetables:%lukB"
			" all_unreclaimable? %s"
			"\n",
			pgdat->node_id,
			K(node_page_state(pgdat, NR_ACTIVE_ANON)),
			K(node_page_state(pgdat, NR_INACTIVE_ANON)),
			K(node_page_state(pgdat, NR_ACTIVE_FILE)),
			K(node_page_state(pgdat, NR_INACTIVE_FILE)),
			K(node_page_state(pgdat, NR_UNEVICTABLE)),
			K(node_page_state(pgdat, NR_ISOLATED_ANON)),
			K(node_page_state(pgdat, NR_ISOLATED_FILE)),
			K(node_page_state(pgdat, NR_FILE_MAPPED)),
			K(node_page_state(pgdat, NR_FILE_DIRTY)),
			K(node_page_state(pgdat, NR_WRITEBACK)),
			K(node_page_state(pgdat, NR_SHMEM)),
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
			K(node_page_state(pgdat, NR_SHMEM_THPS)),
			K(node_page_state(pgdat, NR_SHMEM_PMDMAPPED)),
			K(node_page_state(pgdat, NR_ANON_THPS)),
#endif
			K(node_page_state(pgdat, NR_WRITEBACK_TEMP)),
			node_page_state(pgdat, NR_KERNEL_STACK_KB),
#ifdef CONFIG_SHADOW_CALL_STACK
			node_page_state(pgdat, NR_KERNEL_SCS_KB),
#endif
			K(node_page_state(pgdat, NR_PAGETABLE)),
			pgdat->kswapd_failures >= MAX_RECLAIM_RETRIES ?
				"yes" : "no");
	}

	for_each_populated_zone(zone) {
		int i;

		if (show_mem_node_skip(filter, zone_to_nid(zone), nodemask))
			continue;

		free_pcp = 0;
		for_each_online_cpu(cpu)
			free_pcp += per_cpu_ptr(zone->per_cpu_pageset, cpu)->count;

		show_node(zone);
		printk(KERN_CONT
			"%s"
			" free:%lukB"
			" boost:%lukB"
			" min:%lukB"
			" low:%lukB"
			" high:%lukB"
			" reserved_highatomic:%luKB"
			" active_anon:%lukB"
			" inactive_anon:%lukB"
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
			K(zone_page_state(zone, NR_ZONE_ACTIVE_ANON)),
			K(zone_page_state(zone, NR_ZONE_INACTIVE_ANON)),
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
		unsigned long nr[MAX_ORDER], flags, total = 0;
		unsigned char types[MAX_ORDER];

		if (show_mem_node_skip(filter, zone_to_nid(zone), nodemask))
			continue;
		show_node(zone);
		printk(KERN_CONT "%s: ", zone->name);

		spin_lock_irqsave(&zone->lock, flags);
		for (order = 0; order < MAX_ORDER; order++) {
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
		for (order = 0; order < MAX_ORDER; order++) {
			printk(KERN_CONT "%lu*%lukB ",
			       nr[order], K(1UL) << order);
			if (nr[order])
				show_migration_types(types[order]);
		}
		printk(KERN_CONT "= %lukB\n", K(total));
	}

	for_each_online_node(nid) {
		if (show_mem_node_skip(filter, nid, nodemask))
			continue;
		hugetlb_show_meminfo_node(nid);
	}

	printk("%ld total pagecache pages\n", global_node_page_state(NR_FILE_PAGES));

	show_swap_cache_info();
}

static void zoneref_set_zone(struct zone *zone, struct zoneref *zoneref)
{
	zoneref->zone = zone;
	zoneref->zone_idx = zone_idx(zone);
}

/*
 * Builds allocation fallback zone lists.
 *
 * Add all populated zones of a node to the zonelist.
 */
static int build_zonerefs_node(pg_data_t *pgdat, struct zoneref *zonerefs)
{
	struct zone *zone;
	enum zone_type zone_type = MAX_NR_ZONES;
	int nr_zones = 0;

	do {
		zone_type--;
		zone = pgdat->node_zones + zone_type;
		if (populated_zone(zone)) {
			zoneref_set_zone(zone, &zonerefs[nr_zones++]);
			check_highest_zone(zone_type);
		}
	} while (zone_type);

	return nr_zones;
}

#ifdef CONFIG_NUMA

static int __parse_numa_zonelist_order(char *s)
{
	/*
	 * We used to support different zonelists modes but they turned
	 * out to be just not useful. Let's keep the warning in place
	 * if somebody still use the cmd line parameter so that we do
	 * not fail it silently
	 */
	if (!(*s == 'd' || *s == 'D' || *s == 'n' || *s == 'N')) {
		pr_warn("Ignoring unsupported numa_zonelist_order value:  %s\n", s);
		return -EINVAL;
	}
	return 0;
}

char numa_zonelist_order[] = "Node";

/*
 * sysctl handler for numa_zonelist_order
 */
int numa_zonelist_order_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	if (write)
		return __parse_numa_zonelist_order(buffer);
	return proc_dostring(table, write, buffer, length, ppos);
}


static int node_load[MAX_NUMNODES];

/**
 * find_next_best_node - find the next node that should appear in a given node's fallback list
 * @node: node whose fallback list we're appending
 * @used_node_mask: nodemask_t of already used nodes
 *
 * We use a number of factors to determine which is the next node that should
 * appear on a given node's fallback list.  The node should not have appeared
 * already in @node's fallback list, and it should be the next closest node
 * according to the distance array (which contains arbitrary distance values
 * from each node to each node in the system), and should also prefer nodes
 * with no CPUs, since presumably they'll have very little allocation pressure
 * on them otherwise.
 *
 * Return: node id of the found node or %NUMA_NO_NODE if no node is found.
 */
int find_next_best_node(int node, nodemask_t *used_node_mask)
{
	int n, val;
	int min_val = INT_MAX;
	int best_node = NUMA_NO_NODE;

	/* Use the local node if we haven't already */
	if (!node_isset(node, *used_node_mask)) {
		node_set(node, *used_node_mask);
		return node;
	}

	for_each_node_state(n, N_MEMORY) {

		/* Don't want a node to appear more than once */
		if (node_isset(n, *used_node_mask))
			continue;

		/* Use the distance array to find the distance */
		val = node_distance(node, n);

		/* Penalize nodes under us ("prefer the next node") */
		val += (n < node);

		/* Give preference to headless and unused nodes */
		if (!cpumask_empty(cpumask_of_node(n)))
			val += PENALTY_FOR_NODE_WITH_CPUS;

		/* Slight preference for less loaded node */
		val *= MAX_NUMNODES;
		val += node_load[n];

		if (val < min_val) {
			min_val = val;
			best_node = n;
		}
	}

	if (best_node >= 0)
		node_set(best_node, *used_node_mask);

	return best_node;
}


/*
 * Build zonelists ordered by node and zones within node.
 * This results in maximum locality--normal zone overflows into local
 * DMA zone, if any--but risks exhausting DMA zone.
 */
static void build_zonelists_in_node_order(pg_data_t *pgdat, int *node_order,
		unsigned nr_nodes)
{
	struct zoneref *zonerefs;
	int i;

	zonerefs = pgdat->node_zonelists[ZONELIST_FALLBACK]._zonerefs;

	for (i = 0; i < nr_nodes; i++) {
		int nr_zones;

		pg_data_t *node = NODE_DATA(node_order[i]);

		nr_zones = build_zonerefs_node(node, zonerefs);
		zonerefs += nr_zones;
	}
	zonerefs->zone = NULL;
	zonerefs->zone_idx = 0;
}

/*
 * Build gfp_thisnode zonelists
 */
static void build_thisnode_zonelists(pg_data_t *pgdat)
{
	struct zoneref *zonerefs;
	int nr_zones;

	zonerefs = pgdat->node_zonelists[ZONELIST_NOFALLBACK]._zonerefs;
	nr_zones = build_zonerefs_node(pgdat, zonerefs);
	zonerefs += nr_zones;
	zonerefs->zone = NULL;
	zonerefs->zone_idx = 0;
}

/*
 * Build zonelists ordered by zone and nodes within zones.
 * This results in conserving DMA zone[s] until all Normal memory is
 * exhausted, but results in overflowing to remote node while memory
 * may still exist in local DMA zone.
 */

static void build_zonelists(pg_data_t *pgdat)
{
	static int node_order[MAX_NUMNODES];
	int node, nr_nodes = 0;
	nodemask_t used_mask = NODE_MASK_NONE;
	int local_node, prev_node;

	/* NUMA-aware ordering of nodes */
	local_node = pgdat->node_id;
	prev_node = local_node;

	memset(node_order, 0, sizeof(node_order));
	while ((node = find_next_best_node(local_node, &used_mask)) >= 0) {
		/*
		 * We don't want to pressure a particular node.
		 * So adding penalty to the first node in same
		 * distance group to make it round-robin.
		 */
		if (node_distance(local_node, node) !=
		    node_distance(local_node, prev_node))
			node_load[node] += 1;

		node_order[nr_nodes++] = node;
		prev_node = node;
	}

	build_zonelists_in_node_order(pgdat, node_order, nr_nodes);
	build_thisnode_zonelists(pgdat);
	pr_info("Fallback order for Node %d: ", local_node);
	for (node = 0; node < nr_nodes; node++)
		pr_cont("%d ", node_order[node]);
	pr_cont("\n");
}

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
/*
 * Return node id of node used for "local" allocations.
 * I.e., first node id of first zone in arg node's generic zonelist.
 * Used for initializing percpu 'numa_mem', which is used primarily
 * for kernel allocations, so use GFP_KERNEL flags to locate zonelist.
 */
int local_memory_node(int node)
{
	struct zoneref *z;

	z = first_zones_zonelist(node_zonelist(node, GFP_KERNEL),
				   gfp_zone(GFP_KERNEL),
				   NULL);
	return zone_to_nid(z->zone);
}
#endif

static void setup_min_unmapped_ratio(void);
static void setup_min_slab_ratio(void);
#else	/* CONFIG_NUMA */

static void build_zonelists(pg_data_t *pgdat)
{
	int node, local_node;
	struct zoneref *zonerefs;
	int nr_zones;

	local_node = pgdat->node_id;

	zonerefs = pgdat->node_zonelists[ZONELIST_FALLBACK]._zonerefs;
	nr_zones = build_zonerefs_node(pgdat, zonerefs);
	zonerefs += nr_zones;

	/*
	 * Now we build the zonelist so that it contains the zones
	 * of all the other nodes.
	 * We don't want to pressure a particular node, so when
	 * building the zones for node N, we make sure that the
	 * zones coming right after the local ones are those from
	 * node N+1 (modulo N)
	 */
	for (node = local_node + 1; node < MAX_NUMNODES; node++) {
		if (!node_online(node))
			continue;
		nr_zones = build_zonerefs_node(NODE_DATA(node), zonerefs);
		zonerefs += nr_zones;
	}
	for (node = 0; node < local_node; node++) {
		if (!node_online(node))
			continue;
		nr_zones = build_zonerefs_node(NODE_DATA(node), zonerefs);
		zonerefs += nr_zones;
	}

	zonerefs->zone = NULL;
	zonerefs->zone_idx = 0;
}

#endif	/* CONFIG_NUMA */

/*
 * Boot pageset table. One per cpu which is going to be used for all
 * zones and all nodes. The parameters will be set in such a way
 * that an item put on a list will immediately be handed over to
 * the buddy list. This is safe since pageset manipulation is done
 * with interrupts disabled.
 *
 * The boot_pagesets must be kept even after bootup is complete for
 * unused processors and/or zones. They do play a role for bootstrapping
 * hotplugged processors.
 *
 * zoneinfo_show() and maybe other functions do
 * not check if the processor is online before following the pageset pointer.
 * Other parts of the kernel may not check if the zone is available.
 */
static void per_cpu_pages_init(struct per_cpu_pages *pcp, struct per_cpu_zonestat *pzstats);
/* These effectively disable the pcplists in the boot pageset completely */
#define BOOT_PAGESET_HIGH	0
#define BOOT_PAGESET_BATCH	1
static DEFINE_PER_CPU(struct per_cpu_pages, boot_pageset);
static DEFINE_PER_CPU(struct per_cpu_zonestat, boot_zonestats);
DEFINE_PER_CPU(struct per_cpu_nodestat, boot_nodestats);

static void __build_all_zonelists(void *data)
{
	int nid;
	int __maybe_unused cpu;
	pg_data_t *self = data;

	write_seqlock(&zonelist_update_seq);

#ifdef CONFIG_NUMA
	memset(node_load, 0, sizeof(node_load));
#endif

	/*
	 * This node is hotadded and no memory is yet present.   So just
	 * building zonelists is fine - no need to touch other nodes.
	 */
	if (self && !node_online(self->node_id)) {
		build_zonelists(self);
	} else {
		/*
		 * All possible nodes have pgdat preallocated
		 * in free_area_init
		 */
		for_each_node(nid) {
			pg_data_t *pgdat = NODE_DATA(nid);

			build_zonelists(pgdat);
		}

#ifdef CONFIG_HAVE_MEMORYLESS_NODES
		/*
		 * We now know the "local memory node" for each node--
		 * i.e., the node of the first zone in the generic zonelist.
		 * Set up numa_mem percpu variable for on-line cpus.  During
		 * boot, only the boot cpu should be on-line;  we'll init the
		 * secondary cpus' numa_mem as they come on-line.  During
		 * node/memory hotplug, we'll fixup all on-line cpus.
		 */
		for_each_online_cpu(cpu)
			set_cpu_numa_mem(cpu, local_memory_node(cpu_to_node(cpu)));
#endif
	}

	write_sequnlock(&zonelist_update_seq);
}

static noinline void __init
build_all_zonelists_init(void)
{
	int cpu;

	__build_all_zonelists(NULL);

	/*
	 * Initialize the boot_pagesets that are going to be used
	 * for bootstrapping processors. The real pagesets for
	 * each zone will be allocated later when the per cpu
	 * allocator is available.
	 *
	 * boot_pagesets are used also for bootstrapping offline
	 * cpus if the system is already booted because the pagesets
	 * are needed to initialize allocators on a specific cpu too.
	 * F.e. the percpu allocator needs the page allocator which
	 * needs the percpu allocator in order to allocate its pagesets
	 * (a chicken-egg dilemma).
	 */
	for_each_possible_cpu(cpu)
		per_cpu_pages_init(&per_cpu(boot_pageset, cpu), &per_cpu(boot_zonestats, cpu));

	mminit_verify_zonelist();
	cpuset_init_current_mems_allowed();
}

/*
 * unless system_state == SYSTEM_BOOTING.
 *
 * __ref due to call of __init annotated helper build_all_zonelists_init
 * [protected by SYSTEM_BOOTING].
 */
void __ref build_all_zonelists(pg_data_t *pgdat)
{
	unsigned long vm_total_pages;

	if (system_state == SYSTEM_BOOTING) {
		build_all_zonelists_init();
	} else {
		__build_all_zonelists(pgdat);
		/* cpuset refresh routine should be here */
	}
	/* Get the number of free pages beyond high watermark in all zones. */
	vm_total_pages = nr_free_zone_pages(gfp_zone(GFP_HIGHUSER_MOVABLE));
	/*
	 * Disable grouping by mobility if the number of pages in the
	 * system is too low to allow the mechanism to work. It would be
	 * more accurate, but expensive to check per-zone. This check is
	 * made on memory-hotadd so a system can start with mobility
	 * disabled and enable it later
	 */
	if (vm_total_pages < (pageblock_nr_pages * MIGRATE_TYPES))
		page_group_by_mobility_disabled = 1;
	else
		page_group_by_mobility_disabled = 0;

	pr_info("Built %u zonelists, mobility grouping %s.  Total pages: %ld\n",
		nr_online_nodes,
		page_group_by_mobility_disabled ? "off" : "on",
		vm_total_pages);
#ifdef CONFIG_NUMA
	pr_info("Policy zone: %s\n", zone_names[policy_zone]);
#endif
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
			if (defer_init(nid, pfn, zone_end_pfn))
				break;
		}

		page = pfn_to_page(pfn);
		__init_single_page(page, pfn, zone, nid);
		if (context == MEMINIT_HOTPLUG)
			__SetPageReserved(page);

		/*
		 * Usually, we want to mark the pageblock MIGRATE_MOVABLE,
		 * such that unmovable allocations won't be scattered all
		 * over the place during system boot.
		 */
		if (IS_ALIGNED(pfn, pageblock_nr_pages)) {
			set_pageblock_migratetype(page, migratetype);
			cond_resched();
		}
		pfn++;
	}
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
	page->pgmap = pgmap;
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
	if (IS_ALIGNED(pfn, pageblock_nr_pages)) {
		set_pageblock_migratetype(page, MIGRATE_MOVABLE);
		cond_resched();
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
					      unsigned long nr_pages)
{
	return is_power_of_2(sizeof(struct page)) &&
		!altmap ? 2 * (PAGE_SIZE / sizeof(struct page)) : nr_pages;
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
		 * The first tail page stores compound_mapcount_ptr() and
		 * compound_order() and the second tail page stores
		 * compound_pincount_ptr(). Call prep_compound_head() after
		 * the first and second tail pages have been initialized to
		 * not have the data overwritten.
		 */
		if (pfn == head_pfn + 2)
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

	if (WARN_ON_ONCE(!pgmap || zone_idx(zone) != ZONE_DEVICE))
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
				     compound_nr_pages(altmap, pfns_per_compound));
	}

	pr_info("%s initialised %lu pages in %ums\n", __func__,
		nr_pages, jiffies_to_msecs(jiffies - start));
}

#endif
static void __meminit zone_init_free_lists(struct zone *zone)
{
	unsigned int order, t;
	for_each_migratetype_order(order, t) {
		INIT_LIST_HEAD(&zone->free_area[order].free_list[t]);
		zone->free_area[order].nr_free = 0;
	}
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
		if (!pfn_valid(ALIGN_DOWN(pfn, pageblock_nr_pages))) {
			pfn = ALIGN_DOWN(pfn, pageblock_nr_pages)
				+ pageblock_nr_pages - 1;
			continue;
		}
		__init_single_page(pfn_to_page(pfn), pfn, zone, node);
		__SetPageReserved(pfn_to_page(pfn));
		pgcnt++;
	}

	if (pgcnt)
		pr_info("On node %d, zone %s: %lld pages in unavailable ranges",
			node, zone_names[zone], pgcnt);
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

#ifdef CONFIG_SPARSEMEM
	/*
	 * Initialize the memory map for hole in the range [memory_end,
	 * section_end].
	 * Append the pages in this hole to the highest zone in the last
	 * node.
	 * The call to init_unavailable_range() is outside the ifdef to
	 * silence the compiler warining about zone_id set but not used;
	 * for FLATMEM it is a nop anyway
	 */
	end_pfn = round_up(end_pfn, PAGES_PER_SECTION);
	if (hole_pfn < end_pfn)
#endif
		init_unavailable_range(hole_pfn, end_pfn, zone_id, nid);
}

void __init *memmap_alloc(phys_addr_t size, phys_addr_t align,
			  phys_addr_t min_addr, int nid, bool exact_nid)
{
	void *ptr;

	if (exact_nid)
		ptr = memblock_alloc_exact_nid_raw(size, align, min_addr,
						   MEMBLOCK_ALLOC_ACCESSIBLE,
						   nid);
	else
		ptr = memblock_alloc_try_nid_raw(size, align, min_addr,
						 MEMBLOCK_ALLOC_ACCESSIBLE,
						 nid);

	if (ptr && size > 0)
		page_init_poison(ptr, size);

	return ptr;
}

static int zone_batchsize(struct zone *zone)
{
#ifdef CONFIG_MMU
	int batch;

	/*
	 * The number of pages to batch allocate is either ~0.1%
	 * of the zone or 1MB, whichever is smaller. The batch
	 * size is striking a balance between allocation latency
	 * and zone lock contention.
	 */
	batch = min(zone_managed_pages(zone) >> 10, (1024 * 1024) / PAGE_SIZE);
	batch /= 4;		/* We effectively *= 4 below */
	if (batch < 1)
		batch = 1;

	/*
	 * Clamp the batch to a 2^n - 1 value. Having a power
	 * of 2 value was found to be more likely to have
	 * suboptimal cache aliasing properties in some cases.
	 *
	 * For example if 2 tasks are alternately allocating
	 * batches of pages, one task can end up with a lot
	 * of pages of one half of the possible page colors
	 * and the other with pages of the other colors.
	 */
	batch = rounddown_pow_of_two(batch + batch/2) - 1;

	return batch;

#else
	/* The deferral and batching of frees should be suppressed under NOMMU
	 * conditions.
	 *
	 * The problem is that NOMMU needs to be able to allocate large chunks
	 * of contiguous memory as there's no hardware page translation to
	 * assemble apparent contiguous memory from discontiguous pages.
	 *
	 * Queueing large contiguous runs of pages for batching, however,
	 * causes the pages to actually be freed in smaller chunks.  As there
	 * can be a significant delay between the individual batches being
	 * recycled, this leads to the once large chunks of space being
	 * fragmented and becoming unavailable for high-order allocations.
	 */
	return 0;
#endif
}

static int zone_highsize(struct zone *zone, int batch, int cpu_online)
{
#ifdef CONFIG_MMU
	int high;
	int nr_split_cpus;
	unsigned long total_pages;

	if (!percpu_pagelist_high_fraction) {
		/*
		 * By default, the high value of the pcp is based on the zone
		 * low watermark so that if they are full then background
		 * reclaim will not be started prematurely.
		 */
		total_pages = low_wmark_pages(zone);
	} else {
		/*
		 * If percpu_pagelist_high_fraction is configured, the high
		 * value is based on a fraction of the managed pages in the
		 * zone.
		 */
		total_pages = zone_managed_pages(zone) / percpu_pagelist_high_fraction;
	}

	/*
	 * Split the high value across all online CPUs local to the zone. Note
	 * that early in boot that CPUs may not be online yet and that during
	 * CPU hotplug that the cpumask is not yet updated when a CPU is being
	 * onlined. For memory nodes that have no CPUs, split pcp->high across
	 * all online CPUs to mitigate the risk that reclaim is triggered
	 * prematurely due to pages stored on pcp lists.
	 */
	nr_split_cpus = cpumask_weight(cpumask_of_node(zone_to_nid(zone))) + cpu_online;
	if (!nr_split_cpus)
		nr_split_cpus = num_online_cpus();
	high = total_pages / nr_split_cpus;

	/*
	 * Ensure high is at least batch*4. The multiple is based on the
	 * historical relationship between high and batch.
	 */
	high = max(high, batch << 2);

	return high;
#else
	return 0;
#endif
}

/*
 * pcp->high and pcp->batch values are related and generally batch is lower
 * than high. They are also related to pcp->count such that count is lower
 * than high, and as soon as it reaches high, the pcplist is flushed.
 *
 * However, guaranteeing these relations at all times would require e.g. write
 * barriers here but also careful usage of read barriers at the read side, and
 * thus be prone to error and bad for performance. Thus the update only prevents
 * store tearing. Any new users of pcp->batch and pcp->high should ensure they
 * can cope with those fields changing asynchronously, and fully trust only the
 * pcp->count field on the local CPU with interrupts disabled.
 *
 * mutex_is_locked(&pcp_batch_high_lock) required when calling this function
 * outside of boot time (or some other assurance that no concurrent updaters
 * exist).
 */
static void pageset_update(struct per_cpu_pages *pcp, unsigned long high,
		unsigned long batch)
{
	WRITE_ONCE(pcp->batch, batch);
	WRITE_ONCE(pcp->high, high);
}

static void per_cpu_pages_init(struct per_cpu_pages *pcp, struct per_cpu_zonestat *pzstats)
{
	int pindex;

	memset(pcp, 0, sizeof(*pcp));
	memset(pzstats, 0, sizeof(*pzstats));

	spin_lock_init(&pcp->lock);
	for (pindex = 0; pindex < NR_PCP_LISTS; pindex++)
		INIT_LIST_HEAD(&pcp->lists[pindex]);

	/*
	 * Set batch and high values safe for a boot pageset. A true percpu
	 * pageset's initialization will update them subsequently. Here we don't
	 * need to be as careful as pageset_update() as nobody can access the
	 * pageset yet.
	 */
	pcp->high = BOOT_PAGESET_HIGH;
	pcp->batch = BOOT_PAGESET_BATCH;
	pcp->free_factor = 0;
}

static void __zone_set_pageset_high_and_batch(struct zone *zone, unsigned long high,
		unsigned long batch)
{
	struct per_cpu_pages *pcp;
	int cpu;

	for_each_possible_cpu(cpu) {
		pcp = per_cpu_ptr(zone->per_cpu_pageset, cpu);
		pageset_update(pcp, high, batch);
	}
}

/*
 * Calculate and set new high and batch values for all per-cpu pagesets of a
 * zone based on the zone's size.
 */
static void zone_set_pageset_high_and_batch(struct zone *zone, int cpu_online)
{
	int new_high, new_batch;

	new_batch = max(1, zone_batchsize(zone));
	new_high = zone_highsize(zone, new_batch, cpu_online);

	if (zone->pageset_high == new_high &&
	    zone->pageset_batch == new_batch)
		return;

	zone->pageset_high = new_high;
	zone->pageset_batch = new_batch;

	__zone_set_pageset_high_and_batch(zone, new_high, new_batch);
}

void __meminit setup_zone_pageset(struct zone *zone)
{
	int cpu;

	/* Size may be 0 on !SMP && !NUMA */
	if (sizeof(struct per_cpu_zonestat) > 0)
		zone->per_cpu_zonestats = alloc_percpu(struct per_cpu_zonestat);

	zone->per_cpu_pageset = alloc_percpu(struct per_cpu_pages);
	for_each_possible_cpu(cpu) {
		struct per_cpu_pages *pcp;
		struct per_cpu_zonestat *pzstats;

		pcp = per_cpu_ptr(zone->per_cpu_pageset, cpu);
		pzstats = per_cpu_ptr(zone->per_cpu_zonestats, cpu);
		per_cpu_pages_init(pcp, pzstats);
	}

	zone_set_pageset_high_and_batch(zone, 0);
}

/*
 * Allocate per cpu pagesets and initialize them.
 * Before this call only boot pagesets were available.
 */
void __init setup_per_cpu_pageset(void)
{
	struct pglist_data *pgdat;
	struct zone *zone;
	int __maybe_unused cpu;

	for_each_populated_zone(zone)
		setup_zone_pageset(zone);

#ifdef CONFIG_NUMA
	/*
	 * Unpopulated zones continue using the boot pagesets.
	 * The numa stats for these pagesets need to be reset.
	 * Otherwise, they will end up skewing the stats of
	 * the nodes these zones are associated with.
	 */
	for_each_possible_cpu(cpu) {
		struct per_cpu_zonestat *pzstats = &per_cpu(boot_zonestats, cpu);
		memset(pzstats->vm_numa_event, 0,
		       sizeof(pzstats->vm_numa_event));
	}
#endif

	for_each_online_pgdat(pgdat)
		pgdat->per_cpu_nodestats =
			alloc_percpu(struct per_cpu_nodestat);
}

static __meminit void zone_pcp_init(struct zone *zone)
{
	/*
	 * per cpu subsystem is not up at this point. The following code
	 * relies on the ability of the linker to provide the
	 * offset of a (static) per cpu variable into the per cpu area.
	 */
	zone->per_cpu_pageset = &boot_pageset;
	zone->per_cpu_zonestats = &boot_zonestats;
	zone->pageset_high = BOOT_PAGESET_HIGH;
	zone->pageset_batch = BOOT_PAGESET_BATCH;

	if (populated_zone(zone))
		pr_debug("  %s zone: %lu pages, LIFO batch:%u\n", zone->name,
			 zone->present_pages, zone_batchsize(zone));
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

/**
 * get_pfn_range_for_nid - Return the start and end page frames for a node
 * @nid: The nid to return the range for. If MAX_NUMNODES, the min and max PFN are returned.
 * @start_pfn: Passed by reference. On return, it will have the node start_pfn.
 * @end_pfn: Passed by reference. On return, it will have the node end_pfn.
 *
 * It returns the start and end page frame of a node based on information
 * provided by memblock_set_node(). If called for a node
 * with no available memory, a warning is printed and the start and end
 * PFNs will be 0.
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
					unsigned long node_start_pfn,
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
	/* When hotadd a new node from cpu_up(), the node should be empty */
	if (!node_start_pfn && !node_end_pfn)
		return 0;

	/* Get the start and end of the zone */
	*zone_start_pfn = clamp(node_start_pfn, zone_low, zone_high);
	*zone_end_pfn = clamp(node_end_pfn, zone_low, zone_high);
	adjust_zone_range_for_zone_movable(nid, zone_type,
				node_start_pfn, node_end_pfn,
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

/*
 * Return the number of holes in a range on a node. If nid is MAX_NUMNODES,
 * then all holes in the requested range will be accounted for.
 */
unsigned long __init __absent_pages_in_range(int nid,
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
					unsigned long node_start_pfn,
					unsigned long node_end_pfn)
{
	unsigned long zone_low = arch_zone_lowest_possible_pfn[zone_type];
	unsigned long zone_high = arch_zone_highest_possible_pfn[zone_type];
	unsigned long zone_start_pfn, zone_end_pfn;
	unsigned long nr_absent;

	/* When hotadd a new node from cpu_up(), the node should be empty */
	if (!node_start_pfn && !node_end_pfn)
		return 0;

	zone_start_pfn = clamp(node_start_pfn, zone_low, zone_high);
	zone_end_pfn = clamp(node_end_pfn, zone_low, zone_high);

	adjust_zone_range_for_zone_movable(nid, zone_type,
			node_start_pfn, node_end_pfn,
			&zone_start_pfn, &zone_end_pfn);
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
		unsigned long size, real_size;

		spanned = zone_spanned_pages_in_node(pgdat->node_id, i,
						     node_start_pfn,
						     node_end_pfn,
						     &zone_start_pfn,
						     &zone_end_pfn);
		absent = zone_absent_pages_in_node(pgdat->node_id, i,
						   node_start_pfn,
						   node_end_pfn);

		size = spanned;
		real_size = size - absent;

		if (size)
			zone->zone_start_pfn = zone_start_pfn;
		else
			zone->zone_start_pfn = 0;
		zone->spanned_pages = size;
		zone->present_pages = real_size;
#if defined(CONFIG_MEMORY_HOTPLUG)
		zone->present_early_pages = real_size;
#endif

		totalpages += size;
		realtotalpages += real_size;
	}

	pgdat->node_spanned_pages = totalpages;
	pgdat->node_present_pages = realtotalpages;
	pr_debug("On node %d totalpages: %lu\n", pgdat->node_id, realtotalpages);
}

#ifndef CONFIG_SPARSEMEM
/*
 * Calculate the size of the zone->blockflags rounded to an unsigned long
 * Start by making sure zonesize is a multiple of pageblock_order by rounding
 * up. Then use 1 NR_PAGEBLOCK_BITS worth of bits per pageblock, finally
 * round what is now in bits to nearest long in bits, then return it in
 * bytes.
 */
static unsigned long __init usemap_size(unsigned long zone_start_pfn, unsigned long zonesize)
{
	unsigned long usemapsize;

	zonesize += zone_start_pfn & (pageblock_nr_pages-1);
	usemapsize = roundup(zonesize, pageblock_nr_pages);
	usemapsize = usemapsize >> pageblock_order;
	usemapsize *= NR_PAGEBLOCK_BITS;
	usemapsize = roundup(usemapsize, 8 * sizeof(unsigned long));

	return usemapsize / 8;
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
	unsigned int order = MAX_ORDER - 1;

	/* Check that pageblock_nr_pages has not already been setup */
	if (pageblock_order)
		return;

	/* Don't let pageblocks exceed the maximum allocation granularity. */
	if (HPAGE_SHIFT > PAGE_SHIFT && HUGETLB_PAGE_ORDER < order)
		order = HUGETLB_PAGE_ORDER;

	/*
	 * Assume the largest contiguous order of interest is a huge page.
	 * This value may be variable depending on boot parameters on IA64 and
	 * powerpc.
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

static unsigned long __init calc_memmap_size(unsigned long spanned_pages,
						unsigned long present_pages)
{
	unsigned long pages = spanned_pages;

	/*
	 * Provide a more accurate estimation if there are holes within
	 * the zone and SPARSEMEM is in use. If there are holes within the
	 * zone, each populated memory region may cost us one or two extra
	 * memmap pages due to alignment because memmap pages for each
	 * populated regions may not be naturally aligned on page boundary.
	 * So the (present_pages >> 4) heuristic is a tradeoff for that.
	 */
	if (spanned_pages > present_pages + (present_pages >> 4) &&
	    IS_ENABLED(CONFIG_SPARSEMEM))
		pages = present_pages;

	return PAGE_ALIGN(pages * sizeof(struct page)) >> PAGE_SHIFT;
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
	for_each_online_cpu(cpu) {
		struct per_cpu_nodestat *p;

		p = per_cpu_ptr(pgdat->per_cpu_nodestats, cpu);
		memset(p, 0, sizeof(*p));
	}

	for (z = 0; z < MAX_NR_ZONES; z++)
		zone_init_internals(&pgdat->node_zones[z], z, nid, 0);
}
#endif

/*
 * Set up the zone data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 *
 * NOTE: pgdat should get zeroed by caller.
 * NOTE: this function is only called during early init.
 */
static void __init free_area_init_core(struct pglist_data *pgdat)
{
	enum zone_type j;
	int nid = pgdat->node_id;

	pgdat_init_internals(pgdat);
	pgdat->per_cpu_nodestats = &boot_nodestats;

	for (j = 0; j < MAX_NR_ZONES; j++) {
		struct zone *zone = pgdat->node_zones + j;
		unsigned long size, freesize, memmap_pages;

		size = zone->spanned_pages;
		freesize = zone->present_pages;

		/*
		 * Adjust freesize so that it accounts for how much memory
		 * is used by this zone for memmap. This affects the watermark
		 * and per-cpu initialisations
		 */
		memmap_pages = calc_memmap_size(size, freesize);
		if (!is_highmem_idx(j)) {
			if (freesize >= memmap_pages) {
				freesize -= memmap_pages;
				if (memmap_pages)
					pr_debug("  %s zone: %lu pages used for memmap\n",
						 zone_names[j], memmap_pages);
			} else
				pr_warn("  %s zone: %lu memmap pages exceeds freesize %lu\n",
					zone_names[j], memmap_pages, freesize);
		}

		/* Account for reserved pages */
		if (j == 0 && freesize > dma_reserve) {
			freesize -= dma_reserve;
			pr_debug("  %s zone: %lu pages reserved\n", zone_names[0], dma_reserve);
		}

		if (!is_highmem_idx(j))
			nr_kernel_pages += freesize;
		/* Charge for highmem memmap if there are enough kernel pages */
		else if (nr_kernel_pages > memmap_pages * 2)
			nr_kernel_pages -= memmap_pages;
		nr_all_pages += freesize;

		/*
		 * Set an approximate value for lowmem here, it will be adjusted
		 * when the bootmem allocator frees pages into the buddy system.
		 * And all highmem pages will be managed by the buddy system.
		 */
		zone_init_internals(zone, j, nid, freesize);

		if (!size)
			continue;

		set_pageblock_order();
		setup_usemap(zone);
		init_currently_empty_zone(zone, zone->zone_start_pfn, size);
	}
}

#ifdef CONFIG_FLATMEM
static void __init alloc_node_mem_map(struct pglist_data *pgdat)
{
	unsigned long __maybe_unused start = 0;
	unsigned long __maybe_unused offset = 0;

	/* Skip empty nodes */
	if (!pgdat->node_spanned_pages)
		return;

	start = pgdat->node_start_pfn & ~(MAX_ORDER_NR_PAGES - 1);
	offset = pgdat->node_start_pfn - start;
	/* ia64 gets its own node_mem_map, before this, without bootmem */
	if (!pgdat->node_mem_map) {
		unsigned long size, end;
		struct page *map;

		/*
		 * The zone's endpoints aren't required to be MAX_ORDER
		 * aligned but the node_mem_map endpoints must be in order
		 * for the buddy allocator to function correctly.
		 */
		end = pgdat_end_pfn(pgdat);
		end = ALIGN(end, MAX_ORDER_NR_PAGES);
		size =  (end - start) * sizeof(struct page);
		map = memmap_alloc(size, SMP_CACHE_BYTES, MEMBLOCK_LOW_LIMIT,
				   pgdat->node_id, false);
		if (!map)
			panic("Failed to allocate %ld bytes for node %d memory map\n",
			      size, pgdat->node_id);
		pgdat->node_mem_map = map + offset;
	}
	pr_debug("%s: node %d, pgdat %08lx, node_mem_map %08lx\n",
				__func__, pgdat->node_id, (unsigned long)pgdat,
				(unsigned long)pgdat->node_mem_map);
#ifndef CONFIG_NUMA
	/*
	 * With no DISCONTIG, the global mem_map is just set as node 0's
	 */
	if (pgdat == NODE_DATA(0)) {
		mem_map = NODE_DATA(0)->node_mem_map;
		if (page_to_pfn(mem_map) != pgdat->node_start_pfn)
			mem_map -= offset;
	}
#endif
}
#else
static inline void alloc_node_mem_map(struct pglist_data *pgdat) { }
#endif /* CONFIG_FLATMEM */

#ifdef CONFIG_DEFERRED_STRUCT_PAGE_INIT
static inline void pgdat_set_deferred_range(pg_data_t *pgdat)
{
	pgdat->first_deferred_pfn = ULONG_MAX;
}
#else
static inline void pgdat_set_deferred_range(pg_data_t *pgdat) {}
#endif

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
	} else {
		pr_info("Initmem setup node %d as memoryless\n", nid);
	}

	calculate_node_totalpages(pgdat, start_pfn, end_pfn);

	alloc_node_mem_map(pgdat);
	pgdat_set_deferred_range(pgdat);

	free_area_init_core(pgdat);
}

static void __init free_area_init_memoryless_node(int nid)
{
	free_area_init_node(nid);
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

/**
 * find_min_pfn_with_active_regions - Find the minimum PFN registered
 *
 * Return: the minimum PFN based on information provided via
 * memblock_set_node().
 */
unsigned long __init find_min_pfn_with_active_regions(void)
{
	return PHYS_PFN(memblock_start_of_DRAM());
}

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

			usable_startpfn = PFN_DOWN(r->base);
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
			roundup(required_movablecore, MAX_ORDER_NR_PAGES);
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
	for (nid = 0; nid < MAX_NUMNODES; nid++) {
		unsigned long start_pfn, end_pfn;

		zone_movable_pfn[nid] =
			roundup(zone_movable_pfn[nid], MAX_ORDER_NR_PAGES);

		get_pfn_range_for_nid(nid, &start_pfn, &end_pfn);
		if (zone_movable_pfn[nid] >= end_pfn)
			zone_movable_pfn[nid] = 0;
	}

out:
	/* restore the node_state */
	node_states[N_MEMORY] = saved_node_state;
}

/* Any regular or high memory on that node ? */
static void check_for_memory(pg_data_t *pgdat, int nid)
{
	enum zone_type zone_type;

	for (zone_type = 0; zone_type <= ZONE_MOVABLE - 1; zone_type++) {
		struct zone *zone = &pgdat->node_zones[zone_type];
		if (populated_zone(zone)) {
			if (IS_ENABLED(CONFIG_HIGHMEM))
				node_set_state(nid, N_HIGH_MEMORY);
			if (zone_type <= ZONE_NORMAL)
				node_set_state(nid, N_NORMAL_MEMORY);
			break;
		}
	}
}

/*
 * Some architectures, e.g. ARC may have ZONE_HIGHMEM below ZONE_NORMAL. For
 * such cases we allow max_zone_pfn sorted in the descending order
 */
bool __weak arch_has_descending_max_zone_pfns(void)
{
	return false;
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

	start_pfn = find_min_pfn_with_active_regions();
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
	for_each_node(nid) {
		pg_data_t *pgdat;

		if (!node_online(nid)) {
			pr_info("Initializing node %d as memoryless\n", nid);

			/* Allocator not initialized yet */
			pgdat = arch_alloc_nodedata(nid);
			if (!pgdat) {
				pr_err("Cannot allocate %zuB for node %d.\n",
						sizeof(*pgdat), nid);
				continue;
			}
			arch_refresh_nodedata(nid, pgdat);
			free_area_init_memoryless_node(nid);

			/*
			 * We do not want to confuse userspace by sysfs
			 * files/directories for node without any memory
			 * attached to it, so this node is not marked as
			 * N_MEMORY and not marked online so that no sysfs
			 * hierarchy will be created via register_one_node for
			 * it. The pgdat will get fully initialized by
			 * hotadd_init_pgdat() when memory is hotplugged into
			 * this node.
			 */
			continue;
		}

		pgdat = NODE_DATA(nid);
		free_area_init_node(nid);

		/* Any memory on that node */
		if (pgdat->node_present_pages)
			node_set_state(nid, N_MEMORY);
		check_for_memory(pgdat, nid);
	}

	memmap_init();
}

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

/*
 * movablecore=size sets the amount of memory for use for allocations that
 * can be reclaimed or migrated.
 */
static int __init cmdline_parse_movablecore(char *p)
{
	return cmdline_parse_core(p, &required_movablecore,
				  &required_movablecore_percent);
}

early_param("kernelcore", cmdline_parse_kernelcore);
early_param("movablecore", cmdline_parse_movablecore);

void adjust_managed_page_count(struct page *page, long count)
{
	atomic_long_add(count, &page_zone(page)->managed_pages);
	totalram_pages_add(count);
#ifdef CONFIG_HIGHMEM
	if (PageHighMem(page))
		totalhigh_pages_add(count);
#endif
}
EXPORT_SYMBOL(adjust_managed_page_count);

unsigned long free_reserved_area(void *start, void *end, int poison, const char *s)
{
	void *pos;
	unsigned long pages = 0;

	start = (void *)PAGE_ALIGN((unsigned long)start);
	end = (void *)((unsigned long)end & PAGE_MASK);
	for (pos = start; pos < end; pos += PAGE_SIZE, pages++) {
		struct page *page = virt_to_page(pos);
		void *direct_map_addr;

		/*
		 * 'direct_map_addr' might be different from 'pos'
		 * because some architectures' virt_to_page()
		 * work with aliases.  Getting the direct map
		 * address ensures that we get a _writeable_
		 * alias for the memset().
		 */
		direct_map_addr = page_address(page);
		/*
		 * Perform a kasan-unchecked memset() since this memory
		 * has not been initialized.
		 */
		direct_map_addr = kasan_reset_tag(direct_map_addr);
		if ((unsigned int)poison <= 0xFF)
			memset(direct_map_addr, poison, PAGE_SIZE);

		free_reserved_page(page);
	}

	if (pages && s)
		pr_info("Freeing %s memory: %ldK\n", s, K(pages));

	return pages;
}

void __init mem_init_print_info(void)
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
		codesize >> 10, datasize >> 10, rosize >> 10,
		(init_data_size + init_code_size) >> 10, bss_size >> 10,
		K(physpages - totalram_pages() - totalcma_pages),
		K(totalcma_pages)
#ifdef	CONFIG_HIGHMEM
		, K(totalhigh_pages())
#endif
		);
}

/**
 * set_dma_reserve - set the specified number of pages reserved in the first zone
 * @new_dma_reserve: The number of pages to mark reserved
 *
 * The per-cpu batchsize and zone watermarks are determined by managed_pages.
 * In the DMA zone, a significant percentage may be consumed by kernel image
 * and other unfreeable allocations which can skew the watermarks badly. This
 * function may optionally be used to account for unfreeable pages in the
 * first zone (e.g., ZONE_DMA). The effect will be lower watermarks and
 * smaller per-cpu batchsize.
 */
void __init set_dma_reserve(unsigned long new_dma_reserve)
{
	dma_reserve = new_dma_reserve;
}

static int page_alloc_cpu_dead(unsigned int cpu)
{
	struct zone *zone;

	lru_add_drain_cpu(cpu);
	mlock_page_drain_remote(cpu);
	drain_pages(cpu);

	/*
	 * Spill the event counters of the dead processor
	 * into the current processors event counters.
	 * This artificially elevates the count of the current
	 * processor.
	 */
	vm_events_fold_cpu(cpu);

	/*
	 * Zero the differential counters of the dead processor
	 * so that the vm statistics are consistent.
	 *
	 * This is only okay since the processor is dead and cannot
	 * race with what we are doing.
	 */
	cpu_vm_stats_fold(cpu);

	for_each_populated_zone(zone)
		zone_pcp_update(zone, 0);

	return 0;
}

static int page_alloc_cpu_online(unsigned int cpu)
{
	struct zone *zone;

	for_each_populated_zone(zone)
		zone_pcp_update(zone, 1);
	return 0;
}

#ifdef CONFIG_NUMA
int hashdist = HASHDIST_DEFAULT;

static int __init set_hashdist(char *str)
{
	if (!str)
		return 0;
	hashdist = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("hashdist=", set_hashdist);
#endif

void __init page_alloc_init(void)
{
	int ret;

#ifdef CONFIG_NUMA
	if (num_node_state(N_MEMORY) == 1)
		hashdist = 0;
#endif

	ret = cpuhp_setup_state_nocalls(CPUHP_PAGE_ALLOC,
					"mm/page_alloc:pcp",
					page_alloc_cpu_online,
					page_alloc_cpu_dead);
	WARN_ON(ret < 0);
}

/*
 * calculate_totalreserve_pages - called when sysctl_lowmem_reserve_ratio
 *	or min_free_kbytes changes.
 */
static void calculate_totalreserve_pages(void)
{
	struct pglist_data *pgdat;
	unsigned long reserve_pages = 0;
	enum zone_type i, j;

	for_each_online_pgdat(pgdat) {

		pgdat->totalreserve_pages = 0;

		for (i = 0; i < MAX_NR_ZONES; i++) {
			struct zone *zone = pgdat->node_zones + i;
			long max = 0;
			unsigned long managed_pages = zone_managed_pages(zone);

			/* Find valid and maximum lowmem_reserve in the zone */
			for (j = i; j < MAX_NR_ZONES; j++) {
				if (zone->lowmem_reserve[j] > max)
					max = zone->lowmem_reserve[j];
			}

			/* we treat the high watermark as reserved pages. */
			max += high_wmark_pages(zone);

			if (max > managed_pages)
				max = managed_pages;

			pgdat->totalreserve_pages += max;

			reserve_pages += max;
		}
	}
	totalreserve_pages = reserve_pages;
}

/*
 * setup_per_zone_lowmem_reserve - called whenever
 *	sysctl_lowmem_reserve_ratio changes.  Ensures that each zone
 *	has a correct pages reserved value, so an adequate number of
 *	pages are left in the zone after a successful __alloc_pages().
 */
static void setup_per_zone_lowmem_reserve(void)
{
	struct pglist_data *pgdat;
	enum zone_type i, j;

	for_each_online_pgdat(pgdat) {
		for (i = 0; i < MAX_NR_ZONES - 1; i++) {
			struct zone *zone = &pgdat->node_zones[i];
			int ratio = sysctl_lowmem_reserve_ratio[i];
			bool clear = !ratio || !zone_managed_pages(zone);
			unsigned long managed_pages = 0;

			for (j = i + 1; j < MAX_NR_ZONES; j++) {
				struct zone *upper_zone = &pgdat->node_zones[j];

				managed_pages += zone_managed_pages(upper_zone);

				if (clear)
					zone->lowmem_reserve[j] = 0;
				else
					zone->lowmem_reserve[j] = managed_pages / ratio;
			}
		}
	}

	/* update totalreserve_pages */
	calculate_totalreserve_pages();
}

static void __setup_per_zone_wmarks(void)
{
	unsigned long pages_min = min_free_kbytes >> (PAGE_SHIFT - 10);
	unsigned long lowmem_pages = 0;
	struct zone *zone;
	unsigned long flags;

	/* Calculate total number of !ZONE_HIGHMEM pages */
	for_each_zone(zone) {
		if (!is_highmem(zone))
			lowmem_pages += zone_managed_pages(zone);
	}

	for_each_zone(zone) {
		u64 tmp;

		spin_lock_irqsave(&zone->lock, flags);
		tmp = (u64)pages_min * zone_managed_pages(zone);
		do_div(tmp, lowmem_pages);
		if (is_highmem(zone)) {
			/*
			 * __GFP_HIGH and PF_MEMALLOC allocations usually don't
			 * need highmem pages, so cap pages_min to a small
			 * value here.
			 *
			 * The WMARK_HIGH-WMARK_LOW and (WMARK_LOW-WMARK_MIN)
			 * deltas control async page reclaim, and so should
			 * not be capped for highmem.
			 */
			unsigned long min_pages;

			min_pages = zone_managed_pages(zone) / 1024;
			min_pages = clamp(min_pages, SWAP_CLUSTER_MAX, 128UL);
			zone->_watermark[WMARK_MIN] = min_pages;
		} else {
			/*
			 * If it's a lowmem zone, reserve a number of pages
			 * proportionate to the zone's size.
			 */
			zone->_watermark[WMARK_MIN] = tmp;
		}

		/*
		 * Set the kswapd watermarks distance according to the
		 * scale factor in proportion to available memory, but
		 * ensure a minimum size on small systems.
		 */
		tmp = max_t(u64, tmp >> 2,
			    mult_frac(zone_managed_pages(zone),
				      watermark_scale_factor, 10000));

		zone->watermark_boost = 0;
		zone->_watermark[WMARK_LOW]  = min_wmark_pages(zone) + tmp;
		zone->_watermark[WMARK_HIGH] = low_wmark_pages(zone) + tmp;
		zone->_watermark[WMARK_PROMO] = high_wmark_pages(zone) + tmp;

		spin_unlock_irqrestore(&zone->lock, flags);
	}

	/* update totalreserve_pages */
	calculate_totalreserve_pages();
}

/**
 * setup_per_zone_wmarks - called when min_free_kbytes changes
 * or when memory is hot-{added|removed}
 *
 * Ensures that the watermark[min,low,high] values for each zone are set
 * correctly with respect to min_free_kbytes.
 */
void setup_per_zone_wmarks(void)
{
	struct zone *zone;
	static DEFINE_SPINLOCK(lock);

	spin_lock(&lock);
	__setup_per_zone_wmarks();
	spin_unlock(&lock);

	/*
	 * The watermark size have changed so update the pcpu batch
	 * and high limits or the limits may be inappropriate.
	 */
	for_each_zone(zone)
		zone_pcp_update(zone, 0);
}

/*
 * Initialise min_free_kbytes.
 *
 * For small machines we want it small (128k min).  For large machines
 * we want it large (256MB max).  But it is not linear, because network
 * bandwidth does not increase linearly with machine size.  We use
 *
 *	min_free_kbytes = 4 * sqrt(lowmem_kbytes), for better accuracy:
 *	min_free_kbytes = sqrt(lowmem_kbytes * 16)
 *
 * which yields
 *
 * 16MB:	512k
 * 32MB:	724k
 * 64MB:	1024k
 * 128MB:	1448k
 * 256MB:	2048k
 * 512MB:	2896k
 * 1024MB:	4096k
 * 2048MB:	5792k
 * 4096MB:	8192k
 * 8192MB:	11584k
 * 16384MB:	16384k
 */
void calculate_min_free_kbytes(void)
{
	unsigned long lowmem_kbytes;
	int new_min_free_kbytes;

	lowmem_kbytes = nr_free_buffer_pages() * (PAGE_SIZE >> 10);
	new_min_free_kbytes = int_sqrt(lowmem_kbytes * 16);

	if (new_min_free_kbytes > user_min_free_kbytes)
		min_free_kbytes = clamp(new_min_free_kbytes, 128, 262144);
	else
		pr_warn("min_free_kbytes is not updated to %d because user defined value %d is preferred\n",
				new_min_free_kbytes, user_min_free_kbytes);

}

int __meminit init_per_zone_wmark_min(void)
{
	calculate_min_free_kbytes();
	setup_per_zone_wmarks();
	refresh_zone_stat_thresholds();
	setup_per_zone_lowmem_reserve();

#ifdef CONFIG_NUMA
	setup_min_unmapped_ratio();
	setup_min_slab_ratio();
#endif

	khugepaged_min_free_kbytes_update();

	return 0;
}
postcore_initcall(init_per_zone_wmark_min)

/*
 * min_free_kbytes_sysctl_handler - just a wrapper around proc_dointvec() so
 *	that we can call two helper functions whenever min_free_kbytes
 *	changes.
 */
int min_free_kbytes_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	if (write) {
		user_min_free_kbytes = min_free_kbytes;
		setup_per_zone_wmarks();
	}
	return 0;
}

int watermark_scale_factor_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	if (write)
		setup_per_zone_wmarks();

	return 0;
}

#ifdef CONFIG_NUMA
static void setup_min_unmapped_ratio(void)
{
	pg_data_t *pgdat;
	struct zone *zone;

	for_each_online_pgdat(pgdat)
		pgdat->min_unmapped_pages = 0;

	for_each_zone(zone)
		zone->zone_pgdat->min_unmapped_pages += (zone_managed_pages(zone) *
						         sysctl_min_unmapped_ratio) / 100;
}


int sysctl_min_unmapped_ratio_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	setup_min_unmapped_ratio();

	return 0;
}

static void setup_min_slab_ratio(void)
{
	pg_data_t *pgdat;
	struct zone *zone;

	for_each_online_pgdat(pgdat)
		pgdat->min_slab_pages = 0;

	for_each_zone(zone)
		zone->zone_pgdat->min_slab_pages += (zone_managed_pages(zone) *
						     sysctl_min_slab_ratio) / 100;
}

int sysctl_min_slab_ratio_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int rc;

	rc = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (rc)
		return rc;

	setup_min_slab_ratio();

	return 0;
}
#endif

/*
 * lowmem_reserve_ratio_sysctl_handler - just a wrapper around
 *	proc_dointvec() so that we can call setup_per_zone_lowmem_reserve()
 *	whenever sysctl_lowmem_reserve_ratio changes.
 *
 * The reserve ratio obviously has absolutely no relation with the
 * minimum watermarks. The lowmem reserve ratio can only make sense
 * if in function of the boot time zone sizes.
 */
int lowmem_reserve_ratio_sysctl_handler(struct ctl_table *table, int write,
		void *buffer, size_t *length, loff_t *ppos)
{
	int i;

	proc_dointvec_minmax(table, write, buffer, length, ppos);

	for (i = 0; i < MAX_NR_ZONES; i++) {
		if (sysctl_lowmem_reserve_ratio[i] < 1)
			sysctl_lowmem_reserve_ratio[i] = 0;
	}

	setup_per_zone_lowmem_reserve();
	return 0;
}

/*
 * percpu_pagelist_high_fraction - changes the pcp->high for each zone on each
 * cpu. It is the fraction of total pages in each zone that a hot per cpu
 * pagelist can have before it gets flushed back to buddy allocator.
 */
int percpu_pagelist_high_fraction_sysctl_handler(struct ctl_table *table,
		int write, void *buffer, size_t *length, loff_t *ppos)
{
	struct zone *zone;
	int old_percpu_pagelist_high_fraction;
	int ret;

	mutex_lock(&pcp_batch_high_lock);
	old_percpu_pagelist_high_fraction = percpu_pagelist_high_fraction;

	ret = proc_dointvec_minmax(table, write, buffer, length, ppos);
	if (!write || ret < 0)
		goto out;

	/* Sanity checking to avoid pcp imbalance */
	if (percpu_pagelist_high_fraction &&
	    percpu_pagelist_high_fraction < MIN_PERCPU_PAGELIST_HIGH_FRACTION) {
		percpu_pagelist_high_fraction = old_percpu_pagelist_high_fraction;
		ret = -EINVAL;
		goto out;
	}

	/* No change? */
	if (percpu_pagelist_high_fraction == old_percpu_pagelist_high_fraction)
		goto out;

	for_each_populated_zone(zone)
		zone_set_pageset_high_and_batch(zone, 0);
out:
	mutex_unlock(&pcp_batch_high_lock);
	return ret;
}

#ifndef __HAVE_ARCH_RESERVED_KERNEL_PAGES
/*
 * Returns the number of pages that arch has reserved but
 * is not known to alloc_large_system_hash().
 */
static unsigned long __init arch_reserved_kernel_pages(void)
{
	return 0;
}
#endif

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
	void *table = NULL;
	gfp_t gfp_flags;
	bool virt;
	bool huge;

	/* allow the kernel cmdline to have a say */
	if (!numentries) {
		/* round applicable memory size up to nearest megabyte */
		numentries = nr_kernel_pages;
		numentries -= arch_reserved_kernel_pages();

		/* It isn't necessary when PAGE_SIZE >= 1MB */
		if (PAGE_SHIFT < 20)
			numentries = round_up(numentries, (1<<20)/PAGE_SIZE);

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

		/* Make sure we've got at least a 0-order allocation.. */
		if (unlikely(flags & HASH_SMALL)) {
			/* Makes no sense without HASH_EARLY */
			WARN_ON(!(flags & HASH_EARLY));
			if (!(numentries >> *_hash_shift)) {
				numentries = 1UL << *_hash_shift;
				BUG_ON(!numentries);
			}
		} else if (unlikely((numentries * bucketsize) < PAGE_SIZE))
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
		} else if (get_order(size) >= MAX_ORDER || hashdist) {
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

#ifdef CONFIG_CONTIG_ALLOC
#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
/* Usage: See admin-guide/dynamic-debug-howto.rst */
static void alloc_contig_dump_pages(struct list_head *page_list)
{
	DEFINE_DYNAMIC_DEBUG_METADATA(descriptor, "migrate failure");

	if (DYNAMIC_DEBUG_BRANCH(descriptor)) {
		struct page *page;

		dump_stack();
		list_for_each_entry(page, page_list, lru)
			dump_page(page, "migration failure");
	}
}
#else
static inline void alloc_contig_dump_pages(struct list_head *page_list)
{
}
#endif

/* [start, end) must belong to a single zone. */
int __alloc_contig_migrate_range(struct compact_control *cc,
					unsigned long start, unsigned long end)
{
	/* This function is based on compact_zone() from compaction.c. */
	unsigned int nr_reclaimed;
	unsigned long pfn = start;
	unsigned int tries = 0;
	int ret = 0;
	struct migration_target_control mtc = {
		.nid = zone_to_nid(cc->zone),
		.gfp_mask = GFP_USER | __GFP_MOVABLE | __GFP_RETRY_MAYFAIL,
	};

	lru_cache_disable();

	while (pfn < end || !list_empty(&cc->migratepages)) {
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		if (list_empty(&cc->migratepages)) {
			cc->nr_migratepages = 0;
			ret = isolate_migratepages_range(cc, pfn, end);
			if (ret && ret != -EAGAIN)
				break;
			pfn = cc->migrate_pfn;
			tries = 0;
		} else if (++tries == 5) {
			ret = -EBUSY;
			break;
		}

		nr_reclaimed = reclaim_clean_pages_from_list(cc->zone,
							&cc->migratepages);
		cc->nr_migratepages -= nr_reclaimed;

		ret = migrate_pages(&cc->migratepages, alloc_migration_target,
			NULL, (unsigned long)&mtc, cc->mode, MR_CONTIG_RANGE, NULL);

		/*
		 * On -ENOMEM, migrate_pages() bails out right away. It is pointless
		 * to retry again over this error, so do the same here.
		 */
		if (ret == -ENOMEM)
			break;
	}

	lru_cache_enable();
	if (ret < 0) {
		if (!(cc->gfp_mask & __GFP_NOWARN) && ret == -EBUSY)
			alloc_contig_dump_pages(&cc->migratepages);
		putback_movable_pages(&cc->migratepages);
		return ret;
	}
	return 0;
}

/**
 * alloc_contig_range() -- tries to allocate given range of pages
 * @start:	start PFN to allocate
 * @end:	one-past-the-last PFN to allocate
 * @migratetype:	migratetype of the underlying pageblocks (either
 *			#MIGRATE_MOVABLE or #MIGRATE_CMA).  All pageblocks
 *			in range must have the same migratetype and it must
 *			be either of the two.
 * @gfp_mask:	GFP mask to use during compaction
 *
 * The PFN range does not have to be pageblock aligned. The PFN range must
 * belong to a single zone.
 *
 * The first thing this routine does is attempt to MIGRATE_ISOLATE all
 * pageblocks in the range.  Once isolated, the pageblocks should not
 * be modified by others.
 *
 * Return: zero on success or negative error code.  On success all
 * pages which PFN is in [start, end) are allocated for the caller and
 * need to be freed with free_contig_range().
 */
int alloc_contig_range(unsigned long start, unsigned long end,
		       unsigned migratetype, gfp_t gfp_mask)
{
	unsigned long outer_start, outer_end;
	int order;
	int ret = 0;

	struct compact_control cc = {
		.nr_migratepages = 0,
		.order = -1,
		.zone = page_zone(pfn_to_page(start)),
		.mode = MIGRATE_SYNC,
		.ignore_skip_hint = true,
		.no_set_skip_hint = true,
		.gfp_mask = current_gfp_context(gfp_mask),
		.alloc_contig = true,
	};
	INIT_LIST_HEAD(&cc.migratepages);

	/*
	 * What we do here is we mark all pageblocks in range as
	 * MIGRATE_ISOLATE.  Because pageblock and max order pages may
	 * have different sizes, and due to the way page allocator
	 * work, start_isolate_page_range() has special handlings for this.
	 *
	 * Once the pageblocks are marked as MIGRATE_ISOLATE, we
	 * migrate the pages from an unaligned range (ie. pages that
	 * we are interested in). This will put all the pages in
	 * range back to page allocator as MIGRATE_ISOLATE.
	 *
	 * When this is done, we take the pages in range from page
	 * allocator removing them from the buddy system.  This way
	 * page allocator will never consider using them.
	 *
	 * This lets us mark the pageblocks back as
	 * MIGRATE_CMA/MIGRATE_MOVABLE so that free pages in the
	 * aligned range but not in the unaligned, original range are
	 * put back to page allocator so that buddy can use them.
	 */

	ret = start_isolate_page_range(start, end, migratetype, 0, gfp_mask);
	if (ret)
		goto done;

	drain_all_pages(cc.zone);

	/*
	 * In case of -EBUSY, we'd like to know which page causes problem.
	 * So, just fall through. test_pages_isolated() has a tracepoint
	 * which will report the busy page.
	 *
	 * It is possible that busy pages could become available before
	 * the call to test_pages_isolated, and the range will actually be
	 * allocated.  So, if we fall through be sure to clear ret so that
	 * -EBUSY is not accidentally used or returned to caller.
	 */
	ret = __alloc_contig_migrate_range(&cc, start, end);
	if (ret && ret != -EBUSY)
		goto done;
	ret = 0;

	/*
	 * Pages from [start, end) are within a pageblock_nr_pages
	 * aligned blocks that are marked as MIGRATE_ISOLATE.  What's
	 * more, all pages in [start, end) are free in page allocator.
	 * What we are going to do is to allocate all pages from
	 * [start, end) (that is remove them from page allocator).
	 *
	 * The only problem is that pages at the beginning and at the
	 * end of interesting range may be not aligned with pages that
	 * page allocator holds, ie. they can be part of higher order
	 * pages.  Because of this, we reserve the bigger range and
	 * once this is done free the pages we are not interested in.
	 *
	 * We don't have to hold zone->lock here because the pages are
	 * isolated thus they won't get removed from buddy.
	 */

	order = 0;
	outer_start = start;
	while (!PageBuddy(pfn_to_page(outer_start))) {
		if (++order >= MAX_ORDER) {
			outer_start = start;
			break;
		}
		outer_start &= ~0UL << order;
	}

	if (outer_start != start) {
		order = buddy_order(pfn_to_page(outer_start));

		/*
		 * outer_start page could be small order buddy page and
		 * it doesn't include start page. Adjust outer_start
		 * in this case to report failed page properly
		 * on tracepoint in test_pages_isolated()
		 */
		if (outer_start + (1UL << order) <= start)
			outer_start = start;
	}

	/* Make sure the range is really isolated. */
	if (test_pages_isolated(outer_start, end, 0)) {
		ret = -EBUSY;
		goto done;
	}

	/* Grab isolated pages from freelists. */
	outer_end = isolate_freepages_range(&cc, outer_start, end);
	if (!outer_end) {
		ret = -EBUSY;
		goto done;
	}

	/* Free head and tail (if any) */
	if (start != outer_start)
		free_contig_range(outer_start, start - outer_start);
	if (end != outer_end)
		free_contig_range(end, outer_end - end);

done:
	undo_isolate_page_range(start, end, migratetype);
	return ret;
}
EXPORT_SYMBOL(alloc_contig_range);

static int __alloc_contig_pages(unsigned long start_pfn,
				unsigned long nr_pages, gfp_t gfp_mask)
{
	unsigned long end_pfn = start_pfn + nr_pages;

	return alloc_contig_range(start_pfn, end_pfn, MIGRATE_MOVABLE,
				  gfp_mask);
}

static bool pfn_range_valid_contig(struct zone *z, unsigned long start_pfn,
				   unsigned long nr_pages)
{
	unsigned long i, end_pfn = start_pfn + nr_pages;
	struct page *page;

	for (i = start_pfn; i < end_pfn; i++) {
		page = pfn_to_online_page(i);
		if (!page)
			return false;

		if (page_zone(page) != z)
			return false;

		if (PageReserved(page))
			return false;
	}
	return true;
}

static bool zone_spans_last_pfn(const struct zone *zone,
				unsigned long start_pfn, unsigned long nr_pages)
{
	unsigned long last_pfn = start_pfn + nr_pages - 1;

	return zone_spans_pfn(zone, last_pfn);
}

/**
 * alloc_contig_pages() -- tries to find and allocate contiguous range of pages
 * @nr_pages:	Number of contiguous pages to allocate
 * @gfp_mask:	GFP mask to limit search and used during compaction
 * @nid:	Target node
 * @nodemask:	Mask for other possible nodes
 *
 * This routine is a wrapper around alloc_contig_range(). It scans over zones
 * on an applicable zonelist to find a contiguous pfn range which can then be
 * tried for allocation with alloc_contig_range(). This routine is intended
 * for allocation requests which can not be fulfilled with the buddy allocator.
 *
 * The allocated memory is always aligned to a page boundary. If nr_pages is a
 * power of two, then allocated range is also guaranteed to be aligned to same
 * nr_pages (e.g. 1GB request would be aligned to 1GB).
 *
 * Allocated pages can be freed with free_contig_range() or by manually calling
 * __free_page() on each allocated page.
 *
 * Return: pointer to contiguous pages on success, or NULL if not successful.
 */
struct page *alloc_contig_pages(unsigned long nr_pages, gfp_t gfp_mask,
				int nid, nodemask_t *nodemask)
{
	unsigned long ret, pfn, flags;
	struct zonelist *zonelist;
	struct zone *zone;
	struct zoneref *z;

	zonelist = node_zonelist(nid, gfp_mask);
	for_each_zone_zonelist_nodemask(zone, z, zonelist,
					gfp_zone(gfp_mask), nodemask) {
		spin_lock_irqsave(&zone->lock, flags);

		pfn = ALIGN(zone->zone_start_pfn, nr_pages);
		while (zone_spans_last_pfn(zone, pfn, nr_pages)) {
			if (pfn_range_valid_contig(zone, pfn, nr_pages)) {
				/*
				 * We release the zone lock here because
				 * alloc_contig_range() will also lock the zone
				 * at some point. If there's an allocation
				 * spinning on this lock, it may win the race
				 * and cause alloc_contig_range() to fail...
				 */
				spin_unlock_irqrestore(&zone->lock, flags);
				ret = __alloc_contig_pages(pfn, nr_pages,
							gfp_mask);
				if (!ret)
					return pfn_to_page(pfn);
				spin_lock_irqsave(&zone->lock, flags);
			}
			pfn += nr_pages;
		}
		spin_unlock_irqrestore(&zone->lock, flags);
	}
	return NULL;
}
#endif /* CONFIG_CONTIG_ALLOC */

void free_contig_range(unsigned long pfn, unsigned long nr_pages)
{
	unsigned long count = 0;

	for (; nr_pages--; pfn++) {
		struct page *page = pfn_to_page(pfn);

		count += page_count(page) != 1;
		__free_page(page);
	}
	WARN(count != 0, "%lu pages are still in use!\n", count);
}
EXPORT_SYMBOL(free_contig_range);

/*
 * The zone indicated has a new number of managed_pages; batch sizes and percpu
 * page high values need to be recalculated.
 */
void zone_pcp_update(struct zone *zone, int cpu_online)
{
	mutex_lock(&pcp_batch_high_lock);
	zone_set_pageset_high_and_batch(zone, cpu_online);
	mutex_unlock(&pcp_batch_high_lock);
}

/*
 * Effectively disable pcplists for the zone by setting the high limit to 0
 * and draining all cpus. A concurrent page freeing on another CPU that's about
 * to put the page on pcplist will either finish before the drain and the page
 * will be drained, or observe the new high limit and skip the pcplist.
 *
 * Must be paired with a call to zone_pcp_enable().
 */
void zone_pcp_disable(struct zone *zone)
{
	mutex_lock(&pcp_batch_high_lock);
	__zone_set_pageset_high_and_batch(zone, 0, 1);
	__drain_all_pages(zone, true);
}

void zone_pcp_enable(struct zone *zone)
{
	__zone_set_pageset_high_and_batch(zone, zone->pageset_high, zone->pageset_batch);
	mutex_unlock(&pcp_batch_high_lock);
}

void zone_pcp_reset(struct zone *zone)
{
	int cpu;
	struct per_cpu_zonestat *pzstats;

	if (zone->per_cpu_pageset != &boot_pageset) {
		for_each_online_cpu(cpu) {
			pzstats = per_cpu_ptr(zone->per_cpu_zonestats, cpu);
			drain_zonestat(zone, pzstats);
		}
		free_percpu(zone->per_cpu_pageset);
		free_percpu(zone->per_cpu_zonestats);
		zone->per_cpu_pageset = &boot_pageset;
		zone->per_cpu_zonestats = &boot_zonestats;
	}
}

#ifdef CONFIG_MEMORY_HOTREMOVE
/*
 * All pages in the range must be in a single zone, must not contain holes,
 * must span full sections, and must be isolated before calling this function.
 */
void __offline_isolated_pages(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long pfn = start_pfn;
	struct page *page;
	struct zone *zone;
	unsigned int order;
	unsigned long flags;

	offline_mem_sections(pfn, end_pfn);
	zone = page_zone(pfn_to_page(pfn));
	spin_lock_irqsave(&zone->lock, flags);
	while (pfn < end_pfn) {
		page = pfn_to_page(pfn);
		/*
		 * The HWPoisoned page may be not in buddy system, and
		 * page_count() is not 0.
		 */
		if (unlikely(!PageBuddy(page) && PageHWPoison(page))) {
			pfn++;
			continue;
		}
		/*
		 * At this point all remaining PageOffline() pages have a
		 * reference count of 0 and can simply be skipped.
		 */
		if (PageOffline(page)) {
			BUG_ON(page_count(page));
			BUG_ON(PageBuddy(page));
			pfn++;
			continue;
		}

		BUG_ON(page_count(page));
		BUG_ON(!PageBuddy(page));
		order = buddy_order(page);
		del_page_from_free_list(page, zone, order);
		pfn += (1 << order);
	}
	spin_unlock_irqrestore(&zone->lock, flags);
}
#endif

/*
 * This function returns a stable result only if called under zone lock.
 */
bool is_free_buddy_page(struct page *page)
{
	unsigned long pfn = page_to_pfn(page);
	unsigned int order;

	for (order = 0; order < MAX_ORDER; order++) {
		struct page *page_head = page - (pfn & ((1 << order) - 1));

		if (PageBuddy(page_head) &&
		    buddy_order_unsafe(page_head) >= order)
			break;
	}

	return order < MAX_ORDER;
}
EXPORT_SYMBOL(is_free_buddy_page);

#ifdef CONFIG_MEMORY_FAILURE
/*
 * Break down a higher-order page in sub-pages, and keep our target out of
 * buddy allocator.
 */
static void break_down_buddy_pages(struct zone *zone, struct page *page,
				   struct page *target, int low, int high,
				   int migratetype)
{
	unsigned long size = 1 << high;
	struct page *current_buddy, *next_page;

	while (high > low) {
		high--;
		size >>= 1;

		if (target >= &page[size]) {
			next_page = page + size;
			current_buddy = page;
		} else {
			next_page = page;
			current_buddy = page + size;
		}

		if (set_page_guard(zone, current_buddy, high, migratetype))
			continue;

		if (current_buddy != target) {
			add_to_free_list(current_buddy, zone, high, migratetype);
			set_buddy_order(current_buddy, high);
			page = next_page;
		}
	}
}

/*
 * Take a page that will be marked as poisoned off the buddy allocator.
 */
bool take_page_off_buddy(struct page *page)
{
	struct zone *zone = page_zone(page);
	unsigned long pfn = page_to_pfn(page);
	unsigned long flags;
	unsigned int order;
	bool ret = false;

	spin_lock_irqsave(&zone->lock, flags);
	for (order = 0; order < MAX_ORDER; order++) {
		struct page *page_head = page - (pfn & ((1 << order) - 1));
		int page_order = buddy_order(page_head);

		if (PageBuddy(page_head) && page_order >= order) {
			unsigned long pfn_head = page_to_pfn(page_head);
			int migratetype = get_pfnblock_migratetype(page_head,
								   pfn_head);

			del_page_from_free_list(page_head, zone, page_order);
			break_down_buddy_pages(zone, page_head, page, 0,
						page_order, migratetype);
			SetPageHWPoisonTakenOff(page);
			if (!is_migrate_isolate(migratetype))
				__mod_zone_freepage_state(zone, -1, migratetype);
			ret = true;
			break;
		}
		if (page_count(page_head) > 0)
			break;
	}
	spin_unlock_irqrestore(&zone->lock, flags);
	return ret;
}

/*
 * Cancel takeoff done by take_page_off_buddy().
 */
bool put_page_back_buddy(struct page *page)
{
	struct zone *zone = page_zone(page);
	unsigned long pfn = page_to_pfn(page);
	unsigned long flags;
	int migratetype = get_pfnblock_migratetype(page, pfn);
	bool ret = false;

	spin_lock_irqsave(&zone->lock, flags);
	if (put_page_testzero(page)) {
		ClearPageHWPoisonTakenOff(page);
		__free_one_page(page, pfn, zone, 0, migratetype, FPI_NONE);
		if (TestClearPageHWPoison(page)) {
			ret = true;
		}
	}
	spin_unlock_irqrestore(&zone->lock, flags);

	return ret;
}
#endif

#ifdef CONFIG_ZONE_DMA
bool has_managed_dma(void)
{
	struct pglist_data *pgdat;

	for_each_online_pgdat(pgdat) {
		struct zone *zone = &pgdat->node_zones[ZONE_DMA];

		if (managed_zone(zone))
			return true;
	}
	return false;
}
#endif /* CONFIG_ZONE_DMA */
