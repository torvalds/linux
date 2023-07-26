/* SPDX-License-Identifier: GPL-2.0-or-later */
/* memcontrol.h - Memory Controller
 *
 * Copyright IBM Corporation, 2007
 * Author Balbir Singh <balbir@linux.vnet.ibm.com>
 *
 * Copyright 2007 OpenVZ SWsoft Inc
 * Author: Pavel Emelianov <xemul@openvz.org>
 */

#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H
#include <linux/cgroup.h>
#include <linux/vm_event_item.h>
#include <linux/hardirq.h>
#include <linux/jump_label.h>
#include <linux/page_counter.h>
#include <linux/vmpressure.h>
#include <linux/eventfd.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/writeback.h>
#include <linux/page-flags.h>

struct mem_cgroup;
struct obj_cgroup;
struct page;
struct mm_struct;
struct kmem_cache;

/* Cgroup-specific page state, on top of universal node page state */
enum memcg_stat_item {
	MEMCG_SWAP = NR_VM_NODE_STAT_ITEMS,
	MEMCG_SOCK,
	MEMCG_PERCPU_B,
	MEMCG_VMALLOC,
	MEMCG_KMEM,
	MEMCG_ZSWAP_B,
	MEMCG_ZSWAPPED,
	MEMCG_NR_STAT,
};

enum memcg_memory_event {
	MEMCG_LOW,
	MEMCG_HIGH,
	MEMCG_MAX,
	MEMCG_OOM,
	MEMCG_OOM_KILL,
	MEMCG_OOM_GROUP_KILL,
	MEMCG_SWAP_HIGH,
	MEMCG_SWAP_MAX,
	MEMCG_SWAP_FAIL,
	MEMCG_NR_MEMORY_EVENTS,
};

struct mem_cgroup_reclaim_cookie {
	pg_data_t *pgdat;
	unsigned int generation;
};

#ifdef CONFIG_MEMCG

#define MEM_CGROUP_ID_SHIFT	16

struct mem_cgroup_id {
	int id;
	refcount_t ref;
};

/*
 * Per memcg event counter is incremented at every pagein/pageout. With THP,
 * it will be incremented by the number of pages. This counter is used
 * to trigger some periodic events. This is straightforward and better
 * than using jiffies etc. to handle periodic memcg event.
 */
enum mem_cgroup_events_target {
	MEM_CGROUP_TARGET_THRESH,
	MEM_CGROUP_TARGET_SOFTLIMIT,
	MEM_CGROUP_NTARGETS,
};

struct memcg_vmstats_percpu;
struct memcg_vmstats;

struct mem_cgroup_reclaim_iter {
	struct mem_cgroup *position;
	/* scan generation, increased every round-trip */
	unsigned int generation;
};

/*
 * Bitmap and deferred work of shrinker::id corresponding to memcg-aware
 * shrinkers, which have elements charged to this memcg.
 */
struct shrinker_info {
	struct rcu_head rcu;
	atomic_long_t *nr_deferred;
	unsigned long *map;
	int map_nr_max;
};

struct lruvec_stats_percpu {
	/* Local (CPU and cgroup) state */
	long state[NR_VM_NODE_STAT_ITEMS];

	/* Delta calculation for lockless upward propagation */
	long state_prev[NR_VM_NODE_STAT_ITEMS];
};

struct lruvec_stats {
	/* Aggregated (CPU and subtree) state */
	long state[NR_VM_NODE_STAT_ITEMS];

	/* Non-hierarchical (CPU aggregated) state */
	long state_local[NR_VM_NODE_STAT_ITEMS];

	/* Pending child counts during tree propagation */
	long state_pending[NR_VM_NODE_STAT_ITEMS];
};

/*
 * per-node information in memory controller.
 */
struct mem_cgroup_per_node {
	struct lruvec		lruvec;

	struct lruvec_stats_percpu __percpu	*lruvec_stats_percpu;
	struct lruvec_stats			lruvec_stats;

	unsigned long		lru_zone_size[MAX_NR_ZONES][NR_LRU_LISTS];

	struct mem_cgroup_reclaim_iter	iter;

	struct shrinker_info __rcu	*shrinker_info;

	struct rb_node		tree_node;	/* RB tree node */
	unsigned long		usage_in_excess;/* Set to the value by which */
						/* the soft limit is exceeded*/
	bool			on_tree;
	struct mem_cgroup	*memcg;		/* Back pointer, we cannot */
						/* use container_of	   */
};

struct mem_cgroup_threshold {
	struct eventfd_ctx *eventfd;
	unsigned long threshold;
};

/* For threshold */
struct mem_cgroup_threshold_ary {
	/* An array index points to threshold just below or equal to usage. */
	int current_threshold;
	/* Size of entries[] */
	unsigned int size;
	/* Array of thresholds */
	struct mem_cgroup_threshold entries[];
};

struct mem_cgroup_thresholds {
	/* Primary thresholds array */
	struct mem_cgroup_threshold_ary *primary;
	/*
	 * Spare threshold array.
	 * This is needed to make mem_cgroup_unregister_event() "never fail".
	 * It must be able to store at least primary->size - 1 entries.
	 */
	struct mem_cgroup_threshold_ary *spare;
};

/*
 * Remember four most recent foreign writebacks with dirty pages in this
 * cgroup.  Inode sharing is expected to be uncommon and, even if we miss
 * one in a given round, we're likely to catch it later if it keeps
 * foreign-dirtying, so a fairly low count should be enough.
 *
 * See mem_cgroup_track_foreign_dirty_slowpath() for details.
 */
#define MEMCG_CGWB_FRN_CNT	4

struct memcg_cgwb_frn {
	u64 bdi_id;			/* bdi->id of the foreign inode */
	int memcg_id;			/* memcg->css.id of foreign inode */
	u64 at;				/* jiffies_64 at the time of dirtying */
	struct wb_completion done;	/* tracks in-flight foreign writebacks */
};

/*
 * Bucket for arbitrarily byte-sized objects charged to a memory
 * cgroup. The bucket can be reparented in one piece when the cgroup
 * is destroyed, without having to round up the individual references
 * of all live memory objects in the wild.
 */
struct obj_cgroup {
	struct percpu_ref refcnt;
	struct mem_cgroup *memcg;
	atomic_t nr_charged_bytes;
	union {
		struct list_head list; /* protected by objcg_lock */
		struct rcu_head rcu;
	};
};

/*
 * The memory controller data structure. The memory controller controls both
 * page cache and RSS per cgroup. We would eventually like to provide
 * statistics based on the statistics developed by Rik Van Riel for clock-pro,
 * to help the administrator determine what knobs to tune.
 */
struct mem_cgroup {
	struct cgroup_subsys_state css;

	/* Private memcg ID. Used to ID objects that outlive the cgroup */
	struct mem_cgroup_id id;

	/* Accounted resources */
	struct page_counter memory;		/* Both v1 & v2 */

	union {
		struct page_counter swap;	/* v2 only */
		struct page_counter memsw;	/* v1 only */
	};

	/* Legacy consumer-oriented counters */
	struct page_counter kmem;		/* v1 only */
	struct page_counter tcpmem;		/* v1 only */

	/* Range enforcement for interrupt charges */
	struct work_struct high_work;

#if defined(CONFIG_MEMCG_KMEM) && defined(CONFIG_ZSWAP)
	unsigned long zswap_max;
#endif

	unsigned long soft_limit;

	/* vmpressure notifications */
	struct vmpressure vmpressure;

	/*
	 * Should the OOM killer kill all belonging tasks, had it kill one?
	 */
	bool oom_group;

	/* protected by memcg_oom_lock */
	bool		oom_lock;
	int		under_oom;

	int	swappiness;
	/* OOM-Killer disable */
	int		oom_kill_disable;

	/* memory.events and memory.events.local */
	struct cgroup_file events_file;
	struct cgroup_file events_local_file;

	/* handle for "memory.swap.events" */
	struct cgroup_file swap_events_file;

	/* protect arrays of thresholds */
	struct mutex thresholds_lock;

	/* thresholds for memory usage. RCU-protected */
	struct mem_cgroup_thresholds thresholds;

	/* thresholds for mem+swap usage. RCU-protected */
	struct mem_cgroup_thresholds memsw_thresholds;

	/* For oom notifier event fd */
	struct list_head oom_notify;

	/*
	 * Should we move charges of a task when a task is moved into this
	 * mem_cgroup ? And what type of charges should we move ?
	 */
	unsigned long move_charge_at_immigrate;
	/* taken only while moving_account > 0 */
	spinlock_t		move_lock;
	unsigned long		move_lock_flags;

	CACHELINE_PADDING(_pad1_);

	/* memory.stat */
	struct memcg_vmstats	*vmstats;

	/* memory.events */
	atomic_long_t		memory_events[MEMCG_NR_MEMORY_EVENTS];
	atomic_long_t		memory_events_local[MEMCG_NR_MEMORY_EVENTS];

	unsigned long		socket_pressure;

	/* Legacy tcp memory accounting */
	bool			tcpmem_active;
	int			tcpmem_pressure;

#ifdef CONFIG_MEMCG_KMEM
	int kmemcg_id;
	struct obj_cgroup __rcu *objcg;
	/* list of inherited objcgs, protected by objcg_lock */
	struct list_head objcg_list;
#endif

	CACHELINE_PADDING(_pad2_);

	/*
	 * set > 0 if pages under this cgroup are moving to other cgroup.
	 */
	atomic_t		moving_account;
	struct task_struct	*move_lock_task;

	struct memcg_vmstats_percpu __percpu *vmstats_percpu;

#ifdef CONFIG_CGROUP_WRITEBACK
	struct list_head cgwb_list;
	struct wb_domain cgwb_domain;
	struct memcg_cgwb_frn cgwb_frn[MEMCG_CGWB_FRN_CNT];
#endif

	/* List of events which userspace want to receive */
	struct list_head event_list;
	spinlock_t event_list_lock;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct deferred_split deferred_split_queue;
#endif

#ifdef CONFIG_LRU_GEN
	/* per-memcg mm_struct list */
	struct lru_gen_mm_list mm_list;
#endif

	struct mem_cgroup_per_node *nodeinfo[];
};

/*
 * size of first charge trial.
 * TODO: maybe necessary to use big numbers in big irons or dynamic based of the
 * workload.
 */
#define MEMCG_CHARGE_BATCH 64U

extern struct mem_cgroup *root_mem_cgroup;

enum page_memcg_data_flags {
	/* page->memcg_data is a pointer to an objcgs vector */
	MEMCG_DATA_OBJCGS = (1UL << 0),
	/* page has been accounted as a non-slab kernel page */
	MEMCG_DATA_KMEM = (1UL << 1),
	/* the next bit after the last actual flag */
	__NR_MEMCG_DATA_FLAGS  = (1UL << 2),
};

#define MEMCG_DATA_FLAGS_MASK (__NR_MEMCG_DATA_FLAGS - 1)

static inline bool folio_memcg_kmem(struct folio *folio);

/*
 * After the initialization objcg->memcg is always pointing at
 * a valid memcg, but can be atomically swapped to the parent memcg.
 *
 * The caller must ensure that the returned memcg won't be released:
 * e.g. acquire the rcu_read_lock or css_set_lock.
 */
static inline struct mem_cgroup *obj_cgroup_memcg(struct obj_cgroup *objcg)
{
	return READ_ONCE(objcg->memcg);
}

/*
 * __folio_memcg - Get the memory cgroup associated with a non-kmem folio
 * @folio: Pointer to the folio.
 *
 * Returns a pointer to the memory cgroup associated with the folio,
 * or NULL. This function assumes that the folio is known to have a
 * proper memory cgroup pointer. It's not safe to call this function
 * against some type of folios, e.g. slab folios or ex-slab folios or
 * kmem folios.
 */
static inline struct mem_cgroup *__folio_memcg(struct folio *folio)
{
	unsigned long memcg_data = folio->memcg_data;

	VM_BUG_ON_FOLIO(folio_test_slab(folio), folio);
	VM_BUG_ON_FOLIO(memcg_data & MEMCG_DATA_OBJCGS, folio);
	VM_BUG_ON_FOLIO(memcg_data & MEMCG_DATA_KMEM, folio);

	return (struct mem_cgroup *)(memcg_data & ~MEMCG_DATA_FLAGS_MASK);
}

/*
 * __folio_objcg - get the object cgroup associated with a kmem folio.
 * @folio: Pointer to the folio.
 *
 * Returns a pointer to the object cgroup associated with the folio,
 * or NULL. This function assumes that the folio is known to have a
 * proper object cgroup pointer. It's not safe to call this function
 * against some type of folios, e.g. slab folios or ex-slab folios or
 * LRU folios.
 */
static inline struct obj_cgroup *__folio_objcg(struct folio *folio)
{
	unsigned long memcg_data = folio->memcg_data;

	VM_BUG_ON_FOLIO(folio_test_slab(folio), folio);
	VM_BUG_ON_FOLIO(memcg_data & MEMCG_DATA_OBJCGS, folio);
	VM_BUG_ON_FOLIO(!(memcg_data & MEMCG_DATA_KMEM), folio);

	return (struct obj_cgroup *)(memcg_data & ~MEMCG_DATA_FLAGS_MASK);
}

/*
 * folio_memcg - Get the memory cgroup associated with a folio.
 * @folio: Pointer to the folio.
 *
 * Returns a pointer to the memory cgroup associated with the folio,
 * or NULL. This function assumes that the folio is known to have a
 * proper memory cgroup pointer. It's not safe to call this function
 * against some type of folios, e.g. slab folios or ex-slab folios.
 *
 * For a non-kmem folio any of the following ensures folio and memcg binding
 * stability:
 *
 * - the folio lock
 * - LRU isolation
 * - folio_memcg_lock()
 * - exclusive reference
 * - mem_cgroup_trylock_pages()
 *
 * For a kmem folio a caller should hold an rcu read lock to protect memcg
 * associated with a kmem folio from being released.
 */
static inline struct mem_cgroup *folio_memcg(struct folio *folio)
{
	if (folio_memcg_kmem(folio))
		return obj_cgroup_memcg(__folio_objcg(folio));
	return __folio_memcg(folio);
}

static inline struct mem_cgroup *page_memcg(struct page *page)
{
	return folio_memcg(page_folio(page));
}

/**
 * folio_memcg_rcu - Locklessly get the memory cgroup associated with a folio.
 * @folio: Pointer to the folio.
 *
 * This function assumes that the folio is known to have a
 * proper memory cgroup pointer. It's not safe to call this function
 * against some type of folios, e.g. slab folios or ex-slab folios.
 *
 * Return: A pointer to the memory cgroup associated with the folio,
 * or NULL.
 */
static inline struct mem_cgroup *folio_memcg_rcu(struct folio *folio)
{
	unsigned long memcg_data = READ_ONCE(folio->memcg_data);

	VM_BUG_ON_FOLIO(folio_test_slab(folio), folio);
	WARN_ON_ONCE(!rcu_read_lock_held());

	if (memcg_data & MEMCG_DATA_KMEM) {
		struct obj_cgroup *objcg;

		objcg = (void *)(memcg_data & ~MEMCG_DATA_FLAGS_MASK);
		return obj_cgroup_memcg(objcg);
	}

	return (struct mem_cgroup *)(memcg_data & ~MEMCG_DATA_FLAGS_MASK);
}

/*
 * folio_memcg_check - Get the memory cgroup associated with a folio.
 * @folio: Pointer to the folio.
 *
 * Returns a pointer to the memory cgroup associated with the folio,
 * or NULL. This function unlike folio_memcg() can take any folio
 * as an argument. It has to be used in cases when it's not known if a folio
 * has an associated memory cgroup pointer or an object cgroups vector or
 * an object cgroup.
 *
 * For a non-kmem folio any of the following ensures folio and memcg binding
 * stability:
 *
 * - the folio lock
 * - LRU isolation
 * - lock_folio_memcg()
 * - exclusive reference
 * - mem_cgroup_trylock_pages()
 *
 * For a kmem folio a caller should hold an rcu read lock to protect memcg
 * associated with a kmem folio from being released.
 */
static inline struct mem_cgroup *folio_memcg_check(struct folio *folio)
{
	/*
	 * Because folio->memcg_data might be changed asynchronously
	 * for slabs, READ_ONCE() should be used here.
	 */
	unsigned long memcg_data = READ_ONCE(folio->memcg_data);

	if (memcg_data & MEMCG_DATA_OBJCGS)
		return NULL;

	if (memcg_data & MEMCG_DATA_KMEM) {
		struct obj_cgroup *objcg;

		objcg = (void *)(memcg_data & ~MEMCG_DATA_FLAGS_MASK);
		return obj_cgroup_memcg(objcg);
	}

	return (struct mem_cgroup *)(memcg_data & ~MEMCG_DATA_FLAGS_MASK);
}

static inline struct mem_cgroup *page_memcg_check(struct page *page)
{
	if (PageTail(page))
		return NULL;
	return folio_memcg_check((struct folio *)page);
}

static inline struct mem_cgroup *get_mem_cgroup_from_objcg(struct obj_cgroup *objcg)
{
	struct mem_cgroup *memcg;

	rcu_read_lock();
retry:
	memcg = obj_cgroup_memcg(objcg);
	if (unlikely(!css_tryget(&memcg->css)))
		goto retry;
	rcu_read_unlock();

	return memcg;
}

#ifdef CONFIG_MEMCG_KMEM
/*
 * folio_memcg_kmem - Check if the folio has the memcg_kmem flag set.
 * @folio: Pointer to the folio.
 *
 * Checks if the folio has MemcgKmem flag set. The caller must ensure
 * that the folio has an associated memory cgroup. It's not safe to call
 * this function against some types of folios, e.g. slab folios.
 */
static inline bool folio_memcg_kmem(struct folio *folio)
{
	VM_BUG_ON_PGFLAGS(PageTail(&folio->page), &folio->page);
	VM_BUG_ON_FOLIO(folio->memcg_data & MEMCG_DATA_OBJCGS, folio);
	return folio->memcg_data & MEMCG_DATA_KMEM;
}


#else
static inline bool folio_memcg_kmem(struct folio *folio)
{
	return false;
}

#endif

static inline bool PageMemcgKmem(struct page *page)
{
	return folio_memcg_kmem(page_folio(page));
}

static inline bool mem_cgroup_is_root(struct mem_cgroup *memcg)
{
	return (memcg == root_mem_cgroup);
}

static inline bool mem_cgroup_disabled(void)
{
	return !cgroup_subsys_enabled(memory_cgrp_subsys);
}

static inline void mem_cgroup_protection(struct mem_cgroup *root,
					 struct mem_cgroup *memcg,
					 unsigned long *min,
					 unsigned long *low)
{
	*min = *low = 0;

	if (mem_cgroup_disabled())
		return;

	/*
	 * There is no reclaim protection applied to a targeted reclaim.
	 * We are special casing this specific case here because
	 * mem_cgroup_calculate_protection is not robust enough to keep
	 * the protection invariant for calculated effective values for
	 * parallel reclaimers with different reclaim target. This is
	 * especially a problem for tail memcgs (as they have pages on LRU)
	 * which would want to have effective values 0 for targeted reclaim
	 * but a different value for external reclaim.
	 *
	 * Example
	 * Let's have global and A's reclaim in parallel:
	 *  |
	 *  A (low=2G, usage = 3G, max = 3G, children_low_usage = 1.5G)
	 *  |\
	 *  | C (low = 1G, usage = 2.5G)
	 *  B (low = 1G, usage = 0.5G)
	 *
	 * For the global reclaim
	 * A.elow = A.low
	 * B.elow = min(B.usage, B.low) because children_low_usage <= A.elow
	 * C.elow = min(C.usage, C.low)
	 *
	 * With the effective values resetting we have A reclaim
	 * A.elow = 0
	 * B.elow = B.low
	 * C.elow = C.low
	 *
	 * If the global reclaim races with A's reclaim then
	 * B.elow = C.elow = 0 because children_low_usage > A.elow)
	 * is possible and reclaiming B would be violating the protection.
	 *
	 */
	if (root == memcg)
		return;

	*min = READ_ONCE(memcg->memory.emin);
	*low = READ_ONCE(memcg->memory.elow);
}

void mem_cgroup_calculate_protection(struct mem_cgroup *root,
				     struct mem_cgroup *memcg);

static inline bool mem_cgroup_unprotected(struct mem_cgroup *target,
					  struct mem_cgroup *memcg)
{
	/*
	 * The root memcg doesn't account charges, and doesn't support
	 * protection. The target memcg's protection is ignored, see
	 * mem_cgroup_calculate_protection() and mem_cgroup_protection()
	 */
	return mem_cgroup_disabled() || mem_cgroup_is_root(memcg) ||
		memcg == target;
}

static inline bool mem_cgroup_below_low(struct mem_cgroup *target,
					struct mem_cgroup *memcg)
{
	if (mem_cgroup_unprotected(target, memcg))
		return false;

	return READ_ONCE(memcg->memory.elow) >=
		page_counter_read(&memcg->memory);
}

static inline bool mem_cgroup_below_min(struct mem_cgroup *target,
					struct mem_cgroup *memcg)
{
	if (mem_cgroup_unprotected(target, memcg))
		return false;

	return READ_ONCE(memcg->memory.emin) >=
		page_counter_read(&memcg->memory);
}

int __mem_cgroup_charge(struct folio *folio, struct mm_struct *mm, gfp_t gfp);

/**
 * mem_cgroup_charge - Charge a newly allocated folio to a cgroup.
 * @folio: Folio to charge.
 * @mm: mm context of the allocating task.
 * @gfp: Reclaim mode.
 *
 * Try to charge @folio to the memcg that @mm belongs to, reclaiming
 * pages according to @gfp if necessary.  If @mm is NULL, try to
 * charge to the active memcg.
 *
 * Do not use this for folios allocated for swapin.
 *
 * Return: 0 on success. Otherwise, an error code is returned.
 */
static inline int mem_cgroup_charge(struct folio *folio, struct mm_struct *mm,
				    gfp_t gfp)
{
	if (mem_cgroup_disabled())
		return 0;
	return __mem_cgroup_charge(folio, mm, gfp);
}

int mem_cgroup_swapin_charge_folio(struct folio *folio, struct mm_struct *mm,
				  gfp_t gfp, swp_entry_t entry);
void mem_cgroup_swapin_uncharge_swap(swp_entry_t entry);

void __mem_cgroup_uncharge(struct folio *folio);

/**
 * mem_cgroup_uncharge - Uncharge a folio.
 * @folio: Folio to uncharge.
 *
 * Uncharge a folio previously charged with mem_cgroup_charge().
 */
static inline void mem_cgroup_uncharge(struct folio *folio)
{
	if (mem_cgroup_disabled())
		return;
	__mem_cgroup_uncharge(folio);
}

void __mem_cgroup_uncharge_list(struct list_head *page_list);
static inline void mem_cgroup_uncharge_list(struct list_head *page_list)
{
	if (mem_cgroup_disabled())
		return;
	__mem_cgroup_uncharge_list(page_list);
}

void mem_cgroup_migrate(struct folio *old, struct folio *new);

/**
 * mem_cgroup_lruvec - get the lru list vector for a memcg & node
 * @memcg: memcg of the wanted lruvec
 * @pgdat: pglist_data
 *
 * Returns the lru list vector holding pages for a given @memcg &
 * @pgdat combination. This can be the node lruvec, if the memory
 * controller is disabled.
 */
static inline struct lruvec *mem_cgroup_lruvec(struct mem_cgroup *memcg,
					       struct pglist_data *pgdat)
{
	struct mem_cgroup_per_node *mz;
	struct lruvec *lruvec;

	if (mem_cgroup_disabled()) {
		lruvec = &pgdat->__lruvec;
		goto out;
	}

	if (!memcg)
		memcg = root_mem_cgroup;

	mz = memcg->nodeinfo[pgdat->node_id];
	lruvec = &mz->lruvec;
out:
	/*
	 * Since a node can be onlined after the mem_cgroup was created,
	 * we have to be prepared to initialize lruvec->pgdat here;
	 * and if offlined then reonlined, we need to reinitialize it.
	 */
	if (unlikely(lruvec->pgdat != pgdat))
		lruvec->pgdat = pgdat;
	return lruvec;
}

/**
 * folio_lruvec - return lruvec for isolating/putting an LRU folio
 * @folio: Pointer to the folio.
 *
 * This function relies on folio->mem_cgroup being stable.
 */
static inline struct lruvec *folio_lruvec(struct folio *folio)
{
	struct mem_cgroup *memcg = folio_memcg(folio);

	VM_WARN_ON_ONCE_FOLIO(!memcg && !mem_cgroup_disabled(), folio);
	return mem_cgroup_lruvec(memcg, folio_pgdat(folio));
}

struct mem_cgroup *mem_cgroup_from_task(struct task_struct *p);

struct mem_cgroup *get_mem_cgroup_from_mm(struct mm_struct *mm);

struct lruvec *folio_lruvec_lock(struct folio *folio);
struct lruvec *folio_lruvec_lock_irq(struct folio *folio);
struct lruvec *folio_lruvec_lock_irqsave(struct folio *folio,
						unsigned long *flags);

#ifdef CONFIG_DEBUG_VM
void lruvec_memcg_debug(struct lruvec *lruvec, struct folio *folio);
#else
static inline
void lruvec_memcg_debug(struct lruvec *lruvec, struct folio *folio)
{
}
#endif

static inline
struct mem_cgroup *mem_cgroup_from_css(struct cgroup_subsys_state *css){
	return css ? container_of(css, struct mem_cgroup, css) : NULL;
}

static inline bool obj_cgroup_tryget(struct obj_cgroup *objcg)
{
	return percpu_ref_tryget(&objcg->refcnt);
}

static inline void obj_cgroup_get(struct obj_cgroup *objcg)
{
	percpu_ref_get(&objcg->refcnt);
}

static inline void obj_cgroup_get_many(struct obj_cgroup *objcg,
				       unsigned long nr)
{
	percpu_ref_get_many(&objcg->refcnt, nr);
}

static inline void obj_cgroup_put(struct obj_cgroup *objcg)
{
	percpu_ref_put(&objcg->refcnt);
}

static inline bool mem_cgroup_tryget(struct mem_cgroup *memcg)
{
	return !memcg || css_tryget(&memcg->css);
}

static inline void mem_cgroup_put(struct mem_cgroup *memcg)
{
	if (memcg)
		css_put(&memcg->css);
}

#define mem_cgroup_from_counter(counter, member)	\
	container_of(counter, struct mem_cgroup, member)

struct mem_cgroup *mem_cgroup_iter(struct mem_cgroup *,
				   struct mem_cgroup *,
				   struct mem_cgroup_reclaim_cookie *);
void mem_cgroup_iter_break(struct mem_cgroup *, struct mem_cgroup *);
void mem_cgroup_scan_tasks(struct mem_cgroup *memcg,
			   int (*)(struct task_struct *, void *), void *arg);

static inline unsigned short mem_cgroup_id(struct mem_cgroup *memcg)
{
	if (mem_cgroup_disabled())
		return 0;

	return memcg->id.id;
}
struct mem_cgroup *mem_cgroup_from_id(unsigned short id);

#ifdef CONFIG_SHRINKER_DEBUG
static inline unsigned long mem_cgroup_ino(struct mem_cgroup *memcg)
{
	return memcg ? cgroup_ino(memcg->css.cgroup) : 0;
}

struct mem_cgroup *mem_cgroup_get_from_ino(unsigned long ino);
#endif

static inline struct mem_cgroup *mem_cgroup_from_seq(struct seq_file *m)
{
	return mem_cgroup_from_css(seq_css(m));
}

static inline struct mem_cgroup *lruvec_memcg(struct lruvec *lruvec)
{
	struct mem_cgroup_per_node *mz;

	if (mem_cgroup_disabled())
		return NULL;

	mz = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	return mz->memcg;
}

/**
 * parent_mem_cgroup - find the accounting parent of a memcg
 * @memcg: memcg whose parent to find
 *
 * Returns the parent memcg, or NULL if this is the root.
 */
static inline struct mem_cgroup *parent_mem_cgroup(struct mem_cgroup *memcg)
{
	return mem_cgroup_from_css(memcg->css.parent);
}

static inline bool mem_cgroup_is_descendant(struct mem_cgroup *memcg,
			      struct mem_cgroup *root)
{
	if (root == memcg)
		return true;
	return cgroup_is_descendant(memcg->css.cgroup, root->css.cgroup);
}

static inline bool mm_match_cgroup(struct mm_struct *mm,
				   struct mem_cgroup *memcg)
{
	struct mem_cgroup *task_memcg;
	bool match = false;

	rcu_read_lock();
	task_memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (task_memcg)
		match = mem_cgroup_is_descendant(task_memcg, memcg);
	rcu_read_unlock();
	return match;
}

struct cgroup_subsys_state *mem_cgroup_css_from_folio(struct folio *folio);
ino_t page_cgroup_ino(struct page *page);

static inline bool mem_cgroup_online(struct mem_cgroup *memcg)
{
	if (mem_cgroup_disabled())
		return true;
	return !!(memcg->css.flags & CSS_ONLINE);
}

void mem_cgroup_update_lru_size(struct lruvec *lruvec, enum lru_list lru,
		int zid, int nr_pages);

static inline
unsigned long mem_cgroup_get_zone_lru_size(struct lruvec *lruvec,
		enum lru_list lru, int zone_idx)
{
	struct mem_cgroup_per_node *mz;

	mz = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	return READ_ONCE(mz->lru_zone_size[zone_idx][lru]);
}

void mem_cgroup_handle_over_high(void);

unsigned long mem_cgroup_get_max(struct mem_cgroup *memcg);

unsigned long mem_cgroup_size(struct mem_cgroup *memcg);

void mem_cgroup_print_oom_context(struct mem_cgroup *memcg,
				struct task_struct *p);

void mem_cgroup_print_oom_meminfo(struct mem_cgroup *memcg);

static inline void mem_cgroup_enter_user_fault(void)
{
	WARN_ON(current->in_user_fault);
	current->in_user_fault = 1;
}

static inline void mem_cgroup_exit_user_fault(void)
{
	WARN_ON(!current->in_user_fault);
	current->in_user_fault = 0;
}

static inline bool task_in_memcg_oom(struct task_struct *p)
{
	return p->memcg_in_oom;
}

bool mem_cgroup_oom_synchronize(bool wait);
struct mem_cgroup *mem_cgroup_get_oom_group(struct task_struct *victim,
					    struct mem_cgroup *oom_domain);
void mem_cgroup_print_oom_group(struct mem_cgroup *memcg);

void folio_memcg_lock(struct folio *folio);
void folio_memcg_unlock(struct folio *folio);

void __mod_memcg_state(struct mem_cgroup *memcg, int idx, int val);

/* try to stablize folio_memcg() for all the pages in a memcg */
static inline bool mem_cgroup_trylock_pages(struct mem_cgroup *memcg)
{
	rcu_read_lock();

	if (mem_cgroup_disabled() || !atomic_read(&memcg->moving_account))
		return true;

	rcu_read_unlock();
	return false;
}

static inline void mem_cgroup_unlock_pages(void)
{
	rcu_read_unlock();
}

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void mod_memcg_state(struct mem_cgroup *memcg,
				   int idx, int val)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_memcg_state(memcg, idx, val);
	local_irq_restore(flags);
}

static inline void mod_memcg_page_state(struct page *page,
					int idx, int val)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return;

	rcu_read_lock();
	memcg = page_memcg(page);
	if (memcg)
		mod_memcg_state(memcg, idx, val);
	rcu_read_unlock();
}

unsigned long memcg_page_state(struct mem_cgroup *memcg, int idx);

static inline unsigned long lruvec_page_state(struct lruvec *lruvec,
					      enum node_stat_item idx)
{
	struct mem_cgroup_per_node *pn;
	long x;

	if (mem_cgroup_disabled())
		return node_page_state(lruvec_pgdat(lruvec), idx);

	pn = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	x = READ_ONCE(pn->lruvec_stats.state[idx]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

static inline unsigned long lruvec_page_state_local(struct lruvec *lruvec,
						    enum node_stat_item idx)
{
	struct mem_cgroup_per_node *pn;
	long x = 0;

	if (mem_cgroup_disabled())
		return node_page_state(lruvec_pgdat(lruvec), idx);

	pn = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	x = READ_ONCE(pn->lruvec_stats.state_local[idx]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

void mem_cgroup_flush_stats(void);
void mem_cgroup_flush_stats_ratelimited(void);

void __mod_memcg_lruvec_state(struct lruvec *lruvec, enum node_stat_item idx,
			      int val);
void __mod_lruvec_kmem_state(void *p, enum node_stat_item idx, int val);

static inline void mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					 int val)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_lruvec_kmem_state(p, idx, val);
	local_irq_restore(flags);
}

static inline void mod_memcg_lruvec_state(struct lruvec *lruvec,
					  enum node_stat_item idx, int val)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_memcg_lruvec_state(lruvec, idx, val);
	local_irq_restore(flags);
}

void __count_memcg_events(struct mem_cgroup *memcg, enum vm_event_item idx,
			  unsigned long count);

static inline void count_memcg_events(struct mem_cgroup *memcg,
				      enum vm_event_item idx,
				      unsigned long count)
{
	unsigned long flags;

	local_irq_save(flags);
	__count_memcg_events(memcg, idx, count);
	local_irq_restore(flags);
}

static inline void count_memcg_page_event(struct page *page,
					  enum vm_event_item idx)
{
	struct mem_cgroup *memcg = page_memcg(page);

	if (memcg)
		count_memcg_events(memcg, idx, 1);
}

static inline void count_memcg_folio_events(struct folio *folio,
		enum vm_event_item idx, unsigned long nr)
{
	struct mem_cgroup *memcg = folio_memcg(folio);

	if (memcg)
		count_memcg_events(memcg, idx, nr);
}

static inline void count_memcg_event_mm(struct mm_struct *mm,
					enum vm_event_item idx)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (likely(memcg))
		count_memcg_events(memcg, idx, 1);
	rcu_read_unlock();
}

static inline void memcg_memory_event(struct mem_cgroup *memcg,
				      enum memcg_memory_event event)
{
	bool swap_event = event == MEMCG_SWAP_HIGH || event == MEMCG_SWAP_MAX ||
			  event == MEMCG_SWAP_FAIL;

	atomic_long_inc(&memcg->memory_events_local[event]);
	if (!swap_event)
		cgroup_file_notify(&memcg->events_local_file);

	do {
		atomic_long_inc(&memcg->memory_events[event]);
		if (swap_event)
			cgroup_file_notify(&memcg->swap_events_file);
		else
			cgroup_file_notify(&memcg->events_file);

		if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
			break;
		if (cgrp_dfl_root.flags & CGRP_ROOT_MEMORY_LOCAL_EVENTS)
			break;
	} while ((memcg = parent_mem_cgroup(memcg)) &&
		 !mem_cgroup_is_root(memcg));
}

static inline void memcg_memory_event_mm(struct mm_struct *mm,
					 enum memcg_memory_event event)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (likely(memcg))
		memcg_memory_event(memcg, event);
	rcu_read_unlock();
}

void split_page_memcg(struct page *head, unsigned int nr);

unsigned long mem_cgroup_soft_limit_reclaim(pg_data_t *pgdat, int order,
						gfp_t gfp_mask,
						unsigned long *total_scanned);

#else /* CONFIG_MEMCG */

#define MEM_CGROUP_ID_SHIFT	0

static inline struct mem_cgroup *folio_memcg(struct folio *folio)
{
	return NULL;
}

static inline struct mem_cgroup *page_memcg(struct page *page)
{
	return NULL;
}

static inline struct mem_cgroup *folio_memcg_rcu(struct folio *folio)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return NULL;
}

static inline struct mem_cgroup *folio_memcg_check(struct folio *folio)
{
	return NULL;
}

static inline struct mem_cgroup *page_memcg_check(struct page *page)
{
	return NULL;
}

static inline bool folio_memcg_kmem(struct folio *folio)
{
	return false;
}

static inline bool PageMemcgKmem(struct page *page)
{
	return false;
}

static inline bool mem_cgroup_is_root(struct mem_cgroup *memcg)
{
	return true;
}

static inline bool mem_cgroup_disabled(void)
{
	return true;
}

static inline void memcg_memory_event(struct mem_cgroup *memcg,
				      enum memcg_memory_event event)
{
}

static inline void memcg_memory_event_mm(struct mm_struct *mm,
					 enum memcg_memory_event event)
{
}

static inline void mem_cgroup_protection(struct mem_cgroup *root,
					 struct mem_cgroup *memcg,
					 unsigned long *min,
					 unsigned long *low)
{
	*min = *low = 0;
}

static inline void mem_cgroup_calculate_protection(struct mem_cgroup *root,
						   struct mem_cgroup *memcg)
{
}

static inline bool mem_cgroup_unprotected(struct mem_cgroup *target,
					  struct mem_cgroup *memcg)
{
	return true;
}
static inline bool mem_cgroup_below_low(struct mem_cgroup *target,
					struct mem_cgroup *memcg)
{
	return false;
}

static inline bool mem_cgroup_below_min(struct mem_cgroup *target,
					struct mem_cgroup *memcg)
{
	return false;
}

static inline int mem_cgroup_charge(struct folio *folio,
		struct mm_struct *mm, gfp_t gfp)
{
	return 0;
}

static inline int mem_cgroup_swapin_charge_folio(struct folio *folio,
			struct mm_struct *mm, gfp_t gfp, swp_entry_t entry)
{
	return 0;
}

static inline void mem_cgroup_swapin_uncharge_swap(swp_entry_t entry)
{
}

static inline void mem_cgroup_uncharge(struct folio *folio)
{
}

static inline void mem_cgroup_uncharge_list(struct list_head *page_list)
{
}

static inline void mem_cgroup_migrate(struct folio *old, struct folio *new)
{
}

static inline struct lruvec *mem_cgroup_lruvec(struct mem_cgroup *memcg,
					       struct pglist_data *pgdat)
{
	return &pgdat->__lruvec;
}

static inline struct lruvec *folio_lruvec(struct folio *folio)
{
	struct pglist_data *pgdat = folio_pgdat(folio);
	return &pgdat->__lruvec;
}

static inline
void lruvec_memcg_debug(struct lruvec *lruvec, struct folio *folio)
{
}

static inline struct mem_cgroup *parent_mem_cgroup(struct mem_cgroup *memcg)
{
	return NULL;
}

static inline bool mm_match_cgroup(struct mm_struct *mm,
		struct mem_cgroup *memcg)
{
	return true;
}

static inline struct mem_cgroup *get_mem_cgroup_from_mm(struct mm_struct *mm)
{
	return NULL;
}

static inline
struct mem_cgroup *mem_cgroup_from_css(struct cgroup_subsys_state *css)
{
	return NULL;
}

static inline void obj_cgroup_put(struct obj_cgroup *objcg)
{
}

static inline bool mem_cgroup_tryget(struct mem_cgroup *memcg)
{
	return true;
}

static inline void mem_cgroup_put(struct mem_cgroup *memcg)
{
}

static inline struct lruvec *folio_lruvec_lock(struct folio *folio)
{
	struct pglist_data *pgdat = folio_pgdat(folio);

	spin_lock(&pgdat->__lruvec.lru_lock);
	return &pgdat->__lruvec;
}

static inline struct lruvec *folio_lruvec_lock_irq(struct folio *folio)
{
	struct pglist_data *pgdat = folio_pgdat(folio);

	spin_lock_irq(&pgdat->__lruvec.lru_lock);
	return &pgdat->__lruvec;
}

static inline struct lruvec *folio_lruvec_lock_irqsave(struct folio *folio,
		unsigned long *flagsp)
{
	struct pglist_data *pgdat = folio_pgdat(folio);

	spin_lock_irqsave(&pgdat->__lruvec.lru_lock, *flagsp);
	return &pgdat->__lruvec;
}

static inline struct mem_cgroup *
mem_cgroup_iter(struct mem_cgroup *root,
		struct mem_cgroup *prev,
		struct mem_cgroup_reclaim_cookie *reclaim)
{
	return NULL;
}

static inline void mem_cgroup_iter_break(struct mem_cgroup *root,
					 struct mem_cgroup *prev)
{
}

static inline void mem_cgroup_scan_tasks(struct mem_cgroup *memcg,
		int (*fn)(struct task_struct *, void *), void *arg)
{
}

static inline unsigned short mem_cgroup_id(struct mem_cgroup *memcg)
{
	return 0;
}

static inline struct mem_cgroup *mem_cgroup_from_id(unsigned short id)
{
	WARN_ON_ONCE(id);
	/* XXX: This should always return root_mem_cgroup */
	return NULL;
}

#ifdef CONFIG_SHRINKER_DEBUG
static inline unsigned long mem_cgroup_ino(struct mem_cgroup *memcg)
{
	return 0;
}

static inline struct mem_cgroup *mem_cgroup_get_from_ino(unsigned long ino)
{
	return NULL;
}
#endif

static inline struct mem_cgroup *mem_cgroup_from_seq(struct seq_file *m)
{
	return NULL;
}

static inline struct mem_cgroup *lruvec_memcg(struct lruvec *lruvec)
{
	return NULL;
}

static inline bool mem_cgroup_online(struct mem_cgroup *memcg)
{
	return true;
}

static inline
unsigned long mem_cgroup_get_zone_lru_size(struct lruvec *lruvec,
		enum lru_list lru, int zone_idx)
{
	return 0;
}

static inline unsigned long mem_cgroup_get_max(struct mem_cgroup *memcg)
{
	return 0;
}

static inline unsigned long mem_cgroup_size(struct mem_cgroup *memcg)
{
	return 0;
}

static inline void
mem_cgroup_print_oom_context(struct mem_cgroup *memcg, struct task_struct *p)
{
}

static inline void
mem_cgroup_print_oom_meminfo(struct mem_cgroup *memcg)
{
}

static inline void folio_memcg_lock(struct folio *folio)
{
}

static inline void folio_memcg_unlock(struct folio *folio)
{
}

static inline bool mem_cgroup_trylock_pages(struct mem_cgroup *memcg)
{
	/* to match folio_memcg_rcu() */
	rcu_read_lock();
	return true;
}

static inline void mem_cgroup_unlock_pages(void)
{
	rcu_read_unlock();
}

static inline void mem_cgroup_handle_over_high(void)
{
}

static inline void mem_cgroup_enter_user_fault(void)
{
}

static inline void mem_cgroup_exit_user_fault(void)
{
}

static inline bool task_in_memcg_oom(struct task_struct *p)
{
	return false;
}

static inline bool mem_cgroup_oom_synchronize(bool wait)
{
	return false;
}

static inline struct mem_cgroup *mem_cgroup_get_oom_group(
	struct task_struct *victim, struct mem_cgroup *oom_domain)
{
	return NULL;
}

static inline void mem_cgroup_print_oom_group(struct mem_cgroup *memcg)
{
}

static inline void __mod_memcg_state(struct mem_cgroup *memcg,
				     int idx,
				     int nr)
{
}

static inline void mod_memcg_state(struct mem_cgroup *memcg,
				   int idx,
				   int nr)
{
}

static inline void mod_memcg_page_state(struct page *page,
					int idx, int val)
{
}

static inline unsigned long memcg_page_state(struct mem_cgroup *memcg, int idx)
{
	return 0;
}

static inline unsigned long lruvec_page_state(struct lruvec *lruvec,
					      enum node_stat_item idx)
{
	return node_page_state(lruvec_pgdat(lruvec), idx);
}

static inline unsigned long lruvec_page_state_local(struct lruvec *lruvec,
						    enum node_stat_item idx)
{
	return node_page_state(lruvec_pgdat(lruvec), idx);
}

static inline void mem_cgroup_flush_stats(void)
{
}

static inline void mem_cgroup_flush_stats_ratelimited(void)
{
}

static inline void __mod_memcg_lruvec_state(struct lruvec *lruvec,
					    enum node_stat_item idx, int val)
{
}

static inline void __mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					   int val)
{
	struct page *page = virt_to_head_page(p);

	__mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void mod_lruvec_kmem_state(void *p, enum node_stat_item idx,
					 int val)
{
	struct page *page = virt_to_head_page(p);

	mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void count_memcg_events(struct mem_cgroup *memcg,
				      enum vm_event_item idx,
				      unsigned long count)
{
}

static inline void __count_memcg_events(struct mem_cgroup *memcg,
					enum vm_event_item idx,
					unsigned long count)
{
}

static inline void count_memcg_page_event(struct page *page,
					  int idx)
{
}

static inline void count_memcg_folio_events(struct folio *folio,
		enum vm_event_item idx, unsigned long nr)
{
}

static inline
void count_memcg_event_mm(struct mm_struct *mm, enum vm_event_item idx)
{
}

static inline void split_page_memcg(struct page *head, unsigned int nr)
{
}

static inline
unsigned long mem_cgroup_soft_limit_reclaim(pg_data_t *pgdat, int order,
					    gfp_t gfp_mask,
					    unsigned long *total_scanned)
{
	return 0;
}
#endif /* CONFIG_MEMCG */

static inline void __inc_lruvec_kmem_state(void *p, enum node_stat_item idx)
{
	__mod_lruvec_kmem_state(p, idx, 1);
}

static inline void __dec_lruvec_kmem_state(void *p, enum node_stat_item idx)
{
	__mod_lruvec_kmem_state(p, idx, -1);
}

static inline struct lruvec *parent_lruvec(struct lruvec *lruvec)
{
	struct mem_cgroup *memcg;

	memcg = lruvec_memcg(lruvec);
	if (!memcg)
		return NULL;
	memcg = parent_mem_cgroup(memcg);
	if (!memcg)
		return NULL;
	return mem_cgroup_lruvec(memcg, lruvec_pgdat(lruvec));
}

static inline void unlock_page_lruvec(struct lruvec *lruvec)
{
	spin_unlock(&lruvec->lru_lock);
}

static inline void unlock_page_lruvec_irq(struct lruvec *lruvec)
{
	spin_unlock_irq(&lruvec->lru_lock);
}

static inline void unlock_page_lruvec_irqrestore(struct lruvec *lruvec,
		unsigned long flags)
{
	spin_unlock_irqrestore(&lruvec->lru_lock, flags);
}

/* Test requires a stable page->memcg binding, see page_memcg() */
static inline bool folio_matches_lruvec(struct folio *folio,
		struct lruvec *lruvec)
{
	return lruvec_pgdat(lruvec) == folio_pgdat(folio) &&
	       lruvec_memcg(lruvec) == folio_memcg(folio);
}

/* Don't lock again iff page's lruvec locked */
static inline struct lruvec *folio_lruvec_relock_irq(struct folio *folio,
		struct lruvec *locked_lruvec)
{
	if (locked_lruvec) {
		if (folio_matches_lruvec(folio, locked_lruvec))
			return locked_lruvec;

		unlock_page_lruvec_irq(locked_lruvec);
	}

	return folio_lruvec_lock_irq(folio);
}

/* Don't lock again iff page's lruvec locked */
static inline struct lruvec *folio_lruvec_relock_irqsave(struct folio *folio,
		struct lruvec *locked_lruvec, unsigned long *flags)
{
	if (locked_lruvec) {
		if (folio_matches_lruvec(folio, locked_lruvec))
			return locked_lruvec;

		unlock_page_lruvec_irqrestore(locked_lruvec, *flags);
	}

	return folio_lruvec_lock_irqsave(folio, flags);
}

#ifdef CONFIG_CGROUP_WRITEBACK

struct wb_domain *mem_cgroup_wb_domain(struct bdi_writeback *wb);
void mem_cgroup_wb_stats(struct bdi_writeback *wb, unsigned long *pfilepages,
			 unsigned long *pheadroom, unsigned long *pdirty,
			 unsigned long *pwriteback);

void mem_cgroup_track_foreign_dirty_slowpath(struct folio *folio,
					     struct bdi_writeback *wb);

static inline void mem_cgroup_track_foreign_dirty(struct folio *folio,
						  struct bdi_writeback *wb)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return;

	memcg = folio_memcg(folio);
	if (unlikely(memcg && &memcg->css != wb->memcg_css))
		mem_cgroup_track_foreign_dirty_slowpath(folio, wb);
}

void mem_cgroup_flush_foreign(struct bdi_writeback *wb);

#else	/* CONFIG_CGROUP_WRITEBACK */

static inline struct wb_domain *mem_cgroup_wb_domain(struct bdi_writeback *wb)
{
	return NULL;
}

static inline void mem_cgroup_wb_stats(struct bdi_writeback *wb,
				       unsigned long *pfilepages,
				       unsigned long *pheadroom,
				       unsigned long *pdirty,
				       unsigned long *pwriteback)
{
}

static inline void mem_cgroup_track_foreign_dirty(struct folio *folio,
						  struct bdi_writeback *wb)
{
}

static inline void mem_cgroup_flush_foreign(struct bdi_writeback *wb)
{
}

#endif	/* CONFIG_CGROUP_WRITEBACK */

struct sock;
bool mem_cgroup_charge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages,
			     gfp_t gfp_mask);
void mem_cgroup_uncharge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages);
#ifdef CONFIG_MEMCG
extern struct static_key_false memcg_sockets_enabled_key;
#define mem_cgroup_sockets_enabled static_branch_unlikely(&memcg_sockets_enabled_key)
void mem_cgroup_sk_alloc(struct sock *sk);
void mem_cgroup_sk_free(struct sock *sk);
static inline bool mem_cgroup_under_socket_pressure(struct mem_cgroup *memcg)
{
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && memcg->tcpmem_pressure)
		return true;
	do {
		if (time_before(jiffies, READ_ONCE(memcg->socket_pressure)))
			return true;
	} while ((memcg = parent_mem_cgroup(memcg)));
	return false;
}

int alloc_shrinker_info(struct mem_cgroup *memcg);
void free_shrinker_info(struct mem_cgroup *memcg);
void set_shrinker_bit(struct mem_cgroup *memcg, int nid, int shrinker_id);
void reparent_shrinker_deferred(struct mem_cgroup *memcg);
#else
#define mem_cgroup_sockets_enabled 0
static inline void mem_cgroup_sk_alloc(struct sock *sk) { };
static inline void mem_cgroup_sk_free(struct sock *sk) { };
static inline bool mem_cgroup_under_socket_pressure(struct mem_cgroup *memcg)
{
	return false;
}

static inline void set_shrinker_bit(struct mem_cgroup *memcg,
				    int nid, int shrinker_id)
{
}
#endif

#ifdef CONFIG_MEMCG_KMEM
bool mem_cgroup_kmem_disabled(void);
int __memcg_kmem_charge_page(struct page *page, gfp_t gfp, int order);
void __memcg_kmem_uncharge_page(struct page *page, int order);

struct obj_cgroup *get_obj_cgroup_from_current(void);
struct obj_cgroup *get_obj_cgroup_from_folio(struct folio *folio);

int obj_cgroup_charge(struct obj_cgroup *objcg, gfp_t gfp, size_t size);
void obj_cgroup_uncharge(struct obj_cgroup *objcg, size_t size);

extern struct static_key_false memcg_bpf_enabled_key;
static inline bool memcg_bpf_enabled(void)
{
	return static_branch_likely(&memcg_bpf_enabled_key);
}

extern struct static_key_false memcg_kmem_online_key;

static inline bool memcg_kmem_online(void)
{
	return static_branch_likely(&memcg_kmem_online_key);
}

static inline int memcg_kmem_charge_page(struct page *page, gfp_t gfp,
					 int order)
{
	if (memcg_kmem_online())
		return __memcg_kmem_charge_page(page, gfp, order);
	return 0;
}

static inline void memcg_kmem_uncharge_page(struct page *page, int order)
{
	if (memcg_kmem_online())
		__memcg_kmem_uncharge_page(page, order);
}

/*
 * A helper for accessing memcg's kmem_id, used for getting
 * corresponding LRU lists.
 */
static inline int memcg_kmem_id(struct mem_cgroup *memcg)
{
	return memcg ? memcg->kmemcg_id : -1;
}

struct mem_cgroup *mem_cgroup_from_obj(void *p);
struct mem_cgroup *mem_cgroup_from_slab_obj(void *p);

static inline void count_objcg_event(struct obj_cgroup *objcg,
				     enum vm_event_item idx)
{
	struct mem_cgroup *memcg;

	if (!memcg_kmem_online())
		return;

	rcu_read_lock();
	memcg = obj_cgroup_memcg(objcg);
	count_memcg_events(memcg, idx, 1);
	rcu_read_unlock();
}

#else
static inline bool mem_cgroup_kmem_disabled(void)
{
	return true;
}

static inline int memcg_kmem_charge_page(struct page *page, gfp_t gfp,
					 int order)
{
	return 0;
}

static inline void memcg_kmem_uncharge_page(struct page *page, int order)
{
}

static inline int __memcg_kmem_charge_page(struct page *page, gfp_t gfp,
					   int order)
{
	return 0;
}

static inline void __memcg_kmem_uncharge_page(struct page *page, int order)
{
}

static inline struct obj_cgroup *get_obj_cgroup_from_folio(struct folio *folio)
{
	return NULL;
}

static inline bool memcg_bpf_enabled(void)
{
	return false;
}

static inline bool memcg_kmem_online(void)
{
	return false;
}

static inline int memcg_kmem_id(struct mem_cgroup *memcg)
{
	return -1;
}

static inline struct mem_cgroup *mem_cgroup_from_obj(void *p)
{
	return NULL;
}

static inline struct mem_cgroup *mem_cgroup_from_slab_obj(void *p)
{
	return NULL;
}

static inline void count_objcg_event(struct obj_cgroup *objcg,
				     enum vm_event_item idx)
{
}

#endif /* CONFIG_MEMCG_KMEM */

#if defined(CONFIG_MEMCG_KMEM) && defined(CONFIG_ZSWAP)
bool obj_cgroup_may_zswap(struct obj_cgroup *objcg);
void obj_cgroup_charge_zswap(struct obj_cgroup *objcg, size_t size);
void obj_cgroup_uncharge_zswap(struct obj_cgroup *objcg, size_t size);
#else
static inline bool obj_cgroup_may_zswap(struct obj_cgroup *objcg)
{
	return true;
}
static inline void obj_cgroup_charge_zswap(struct obj_cgroup *objcg,
					   size_t size)
{
}
static inline void obj_cgroup_uncharge_zswap(struct obj_cgroup *objcg,
					     size_t size)
{
}
#endif

#endif /* _LINUX_MEMCONTROL_H */
