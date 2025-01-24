// SPDX-License-Identifier: GPL-2.0-or-later
/* memcontrol.c - Memory Controller
 *
 * Copyright IBM Corporation, 2007
 * Author Balbir Singh <balbir@linux.vnet.ibm.com>
 *
 * Copyright 2007 OpenVZ SWsoft Inc
 * Author: Pavel Emelianov <xemul@openvz.org>
 *
 * Memory thresholds
 * Copyright (C) 2009 Nokia Corporation
 * Author: Kirill A. Shutemov
 *
 * Kernel Memory Controller
 * Copyright (C) 2012 Parallels Inc. and Google Inc.
 * Authors: Glauber Costa and Suleiman Souhlal
 *
 * Native page reclaim
 * Charge lifetime sanitation
 * Lockless page tracking & accounting
 * Unified hierarchy configuration model
 * Copyright (C) 2015 Red Hat, Inc., Johannes Weiner
 *
 * Per memcg lru locking
 * Copyright (C) 2020 Alibaba, Inc, Alex Shi
 */

#include <linux/cgroup-defs.h>
#include <linux/page_counter.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>
#include <linux/sched/mm.h>
#include <linux/shmem_fs.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/vm_event_item.h>
#include <linux/smp.h>
#include <linux/page-flags.h>
#include <linux/backing-dev.h>
#include <linux/bit_spinlock.h>
#include <linux/rcupdate.h>
#include <linux/limits.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/vmpressure.h>
#include <linux/memremap.h>
#include <linux/mm_inline.h>
#include <linux/swap_cgroup.h>
#include <linux/cpu.h>
#include <linux/oom.h>
#include <linux/lockdep.h>
#include <linux/resume_user_mode.h>
#include <linux/psi.h>
#include <linux/seq_buf.h>
#include <linux/sched/isolation.h>
#include <linux/kmemleak.h>
#include "internal.h"
#include <net/sock.h>
#include <net/ip.h>
#include "slab.h"
#include "memcontrol-v1.h"

#include <linux/uaccess.h>

#define CREATE_TRACE_POINTS
#include <trace/events/memcg.h>
#undef CREATE_TRACE_POINTS

#include <trace/events/vmscan.h>

struct cgroup_subsys memory_cgrp_subsys __read_mostly;
EXPORT_SYMBOL(memory_cgrp_subsys);

struct mem_cgroup *root_mem_cgroup __read_mostly;

/* Active memory cgroup to use from an interrupt context */
DEFINE_PER_CPU(struct mem_cgroup *, int_active_memcg);
EXPORT_PER_CPU_SYMBOL_GPL(int_active_memcg);

/* Socket memory accounting disabled? */
static bool cgroup_memory_nosocket __ro_after_init;

/* Kernel memory accounting disabled? */
static bool cgroup_memory_nokmem __ro_after_init;

/* BPF memory accounting disabled? */
static bool cgroup_memory_nobpf __ro_after_init;

#ifdef CONFIG_CGROUP_WRITEBACK
static DECLARE_WAIT_QUEUE_HEAD(memcg_cgwb_frn_waitq);
#endif

static inline bool task_is_dying(void)
{
	return tsk_is_oom_victim(current) || fatal_signal_pending(current) ||
		(current->flags & PF_EXITING);
}

/* Some nice accessors for the vmpressure. */
struct vmpressure *memcg_to_vmpressure(struct mem_cgroup *memcg)
{
	if (!memcg)
		memcg = root_mem_cgroup;
	return &memcg->vmpressure;
}

struct mem_cgroup *vmpressure_to_memcg(struct vmpressure *vmpr)
{
	return container_of(vmpr, struct mem_cgroup, vmpressure);
}

#define SEQ_BUF_SIZE SZ_4K
#define CURRENT_OBJCG_UPDATE_BIT 0
#define CURRENT_OBJCG_UPDATE_FLAG (1UL << CURRENT_OBJCG_UPDATE_BIT)

static DEFINE_SPINLOCK(objcg_lock);

bool mem_cgroup_kmem_disabled(void)
{
	return cgroup_memory_nokmem;
}

static void obj_cgroup_uncharge_pages(struct obj_cgroup *objcg,
				      unsigned int nr_pages);

static void obj_cgroup_release(struct percpu_ref *ref)
{
	struct obj_cgroup *objcg = container_of(ref, struct obj_cgroup, refcnt);
	unsigned int nr_bytes;
	unsigned int nr_pages;
	unsigned long flags;

	/*
	 * At this point all allocated objects are freed, and
	 * objcg->nr_charged_bytes can't have an arbitrary byte value.
	 * However, it can be PAGE_SIZE or (x * PAGE_SIZE).
	 *
	 * The following sequence can lead to it:
	 * 1) CPU0: objcg == stock->cached_objcg
	 * 2) CPU1: we do a small allocation (e.g. 92 bytes),
	 *          PAGE_SIZE bytes are charged
	 * 3) CPU1: a process from another memcg is allocating something,
	 *          the stock if flushed,
	 *          objcg->nr_charged_bytes = PAGE_SIZE - 92
	 * 5) CPU0: we do release this object,
	 *          92 bytes are added to stock->nr_bytes
	 * 6) CPU0: stock is flushed,
	 *          92 bytes are added to objcg->nr_charged_bytes
	 *
	 * In the result, nr_charged_bytes == PAGE_SIZE.
	 * This page will be uncharged in obj_cgroup_release().
	 */
	nr_bytes = atomic_read(&objcg->nr_charged_bytes);
	WARN_ON_ONCE(nr_bytes & (PAGE_SIZE - 1));
	nr_pages = nr_bytes >> PAGE_SHIFT;

	if (nr_pages)
		obj_cgroup_uncharge_pages(objcg, nr_pages);

	spin_lock_irqsave(&objcg_lock, flags);
	list_del(&objcg->list);
	spin_unlock_irqrestore(&objcg_lock, flags);

	percpu_ref_exit(ref);
	kfree_rcu(objcg, rcu);
}

static struct obj_cgroup *obj_cgroup_alloc(void)
{
	struct obj_cgroup *objcg;
	int ret;

	objcg = kzalloc(sizeof(struct obj_cgroup), GFP_KERNEL);
	if (!objcg)
		return NULL;

	ret = percpu_ref_init(&objcg->refcnt, obj_cgroup_release, 0,
			      GFP_KERNEL);
	if (ret) {
		kfree(objcg);
		return NULL;
	}
	INIT_LIST_HEAD(&objcg->list);
	return objcg;
}

static void memcg_reparent_objcgs(struct mem_cgroup *memcg,
				  struct mem_cgroup *parent)
{
	struct obj_cgroup *objcg, *iter;

	objcg = rcu_replace_pointer(memcg->objcg, NULL, true);

	spin_lock_irq(&objcg_lock);

	/* 1) Ready to reparent active objcg. */
	list_add(&objcg->list, &memcg->objcg_list);
	/* 2) Reparent active objcg and already reparented objcgs to parent. */
	list_for_each_entry(iter, &memcg->objcg_list, list)
		WRITE_ONCE(iter->memcg, parent);
	/* 3) Move already reparented objcgs to the parent's list */
	list_splice(&memcg->objcg_list, &parent->objcg_list);

	spin_unlock_irq(&objcg_lock);

	percpu_ref_kill(&objcg->refcnt);
}

/*
 * A lot of the calls to the cache allocation functions are expected to be
 * inlined by the compiler. Since the calls to memcg_slab_post_alloc_hook() are
 * conditional to this static branch, we'll have to allow modules that does
 * kmem_cache_alloc and the such to see this symbol as well
 */
DEFINE_STATIC_KEY_FALSE(memcg_kmem_online_key);
EXPORT_SYMBOL(memcg_kmem_online_key);

DEFINE_STATIC_KEY_FALSE(memcg_bpf_enabled_key);
EXPORT_SYMBOL(memcg_bpf_enabled_key);

/**
 * mem_cgroup_css_from_folio - css of the memcg associated with a folio
 * @folio: folio of interest
 *
 * If memcg is bound to the default hierarchy, css of the memcg associated
 * with @folio is returned.  The returned css remains associated with @folio
 * until it is released.
 *
 * If memcg is bound to a traditional hierarchy, the css of root_mem_cgroup
 * is returned.
 */
struct cgroup_subsys_state *mem_cgroup_css_from_folio(struct folio *folio)
{
	struct mem_cgroup *memcg = folio_memcg(folio);

	if (!memcg || !cgroup_subsys_on_dfl(memory_cgrp_subsys))
		memcg = root_mem_cgroup;

	return &memcg->css;
}

/**
 * page_cgroup_ino - return inode number of the memcg a page is charged to
 * @page: the page
 *
 * Look up the closest online ancestor of the memory cgroup @page is charged to
 * and return its inode number or 0 if @page is not charged to any cgroup. It
 * is safe to call this function without holding a reference to @page.
 *
 * Note, this function is inherently racy, because there is nothing to prevent
 * the cgroup inode from getting torn down and potentially reallocated a moment
 * after page_cgroup_ino() returns, so it only should be used by callers that
 * do not care (such as procfs interfaces).
 */
ino_t page_cgroup_ino(struct page *page)
{
	struct mem_cgroup *memcg;
	unsigned long ino = 0;

	rcu_read_lock();
	/* page_folio() is racy here, but the entire function is racy anyway */
	memcg = folio_memcg_check(page_folio(page));

	while (memcg && !(memcg->css.flags & CSS_ONLINE))
		memcg = parent_mem_cgroup(memcg);
	if (memcg)
		ino = cgroup_ino(memcg->css.cgroup);
	rcu_read_unlock();
	return ino;
}

/* Subset of node_stat_item for memcg stats */
static const unsigned int memcg_node_stat_items[] = {
	NR_INACTIVE_ANON,
	NR_ACTIVE_ANON,
	NR_INACTIVE_FILE,
	NR_ACTIVE_FILE,
	NR_UNEVICTABLE,
	NR_SLAB_RECLAIMABLE_B,
	NR_SLAB_UNRECLAIMABLE_B,
	WORKINGSET_REFAULT_ANON,
	WORKINGSET_REFAULT_FILE,
	WORKINGSET_ACTIVATE_ANON,
	WORKINGSET_ACTIVATE_FILE,
	WORKINGSET_RESTORE_ANON,
	WORKINGSET_RESTORE_FILE,
	WORKINGSET_NODERECLAIM,
	NR_ANON_MAPPED,
	NR_FILE_MAPPED,
	NR_FILE_PAGES,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	NR_SHMEM,
	NR_SHMEM_THPS,
	NR_FILE_THPS,
	NR_ANON_THPS,
	NR_KERNEL_STACK_KB,
	NR_PAGETABLE,
	NR_SECONDARY_PAGETABLE,
#ifdef CONFIG_SWAP
	NR_SWAPCACHE,
#endif
#ifdef CONFIG_NUMA_BALANCING
	PGPROMOTE_SUCCESS,
#endif
	PGDEMOTE_KSWAPD,
	PGDEMOTE_DIRECT,
	PGDEMOTE_KHUGEPAGED,
#ifdef CONFIG_HUGETLB_PAGE
	NR_HUGETLB,
#endif
};

static const unsigned int memcg_stat_items[] = {
	MEMCG_SWAP,
	MEMCG_SOCK,
	MEMCG_PERCPU_B,
	MEMCG_VMALLOC,
	MEMCG_KMEM,
	MEMCG_ZSWAP_B,
	MEMCG_ZSWAPPED,
};

#define NR_MEMCG_NODE_STAT_ITEMS ARRAY_SIZE(memcg_node_stat_items)
#define MEMCG_VMSTAT_SIZE (NR_MEMCG_NODE_STAT_ITEMS + \
			   ARRAY_SIZE(memcg_stat_items))
#define BAD_STAT_IDX(index) ((u32)(index) >= U8_MAX)
static u8 mem_cgroup_stats_index[MEMCG_NR_STAT] __read_mostly;

static void init_memcg_stats(void)
{
	u8 i, j = 0;

	BUILD_BUG_ON(MEMCG_NR_STAT >= U8_MAX);

	memset(mem_cgroup_stats_index, U8_MAX, sizeof(mem_cgroup_stats_index));

	for (i = 0; i < NR_MEMCG_NODE_STAT_ITEMS; ++i, ++j)
		mem_cgroup_stats_index[memcg_node_stat_items[i]] = j;

	for (i = 0; i < ARRAY_SIZE(memcg_stat_items); ++i, ++j)
		mem_cgroup_stats_index[memcg_stat_items[i]] = j;
}

static inline int memcg_stats_index(int idx)
{
	return mem_cgroup_stats_index[idx];
}

struct lruvec_stats_percpu {
	/* Local (CPU and cgroup) state */
	long state[NR_MEMCG_NODE_STAT_ITEMS];

	/* Delta calculation for lockless upward propagation */
	long state_prev[NR_MEMCG_NODE_STAT_ITEMS];
};

struct lruvec_stats {
	/* Aggregated (CPU and subtree) state */
	long state[NR_MEMCG_NODE_STAT_ITEMS];

	/* Non-hierarchical (CPU aggregated) state */
	long state_local[NR_MEMCG_NODE_STAT_ITEMS];

	/* Pending child counts during tree propagation */
	long state_pending[NR_MEMCG_NODE_STAT_ITEMS];
};

unsigned long lruvec_page_state(struct lruvec *lruvec, enum node_stat_item idx)
{
	struct mem_cgroup_per_node *pn;
	long x;
	int i;

	if (mem_cgroup_disabled())
		return node_page_state(lruvec_pgdat(lruvec), idx);

	i = memcg_stats_index(idx);
	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, idx))
		return 0;

	pn = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	x = READ_ONCE(pn->lruvec_stats->state[i]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

unsigned long lruvec_page_state_local(struct lruvec *lruvec,
				      enum node_stat_item idx)
{
	struct mem_cgroup_per_node *pn;
	long x;
	int i;

	if (mem_cgroup_disabled())
		return node_page_state(lruvec_pgdat(lruvec), idx);

	i = memcg_stats_index(idx);
	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, idx))
		return 0;

	pn = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	x = READ_ONCE(pn->lruvec_stats->state_local[i]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

/* Subset of vm_event_item to report for memcg event stats */
static const unsigned int memcg_vm_event_stat[] = {
#ifdef CONFIG_MEMCG_V1
	PGPGIN,
	PGPGOUT,
#endif
	PSWPIN,
	PSWPOUT,
	PGSCAN_KSWAPD,
	PGSCAN_DIRECT,
	PGSCAN_KHUGEPAGED,
	PGSTEAL_KSWAPD,
	PGSTEAL_DIRECT,
	PGSTEAL_KHUGEPAGED,
	PGFAULT,
	PGMAJFAULT,
	PGREFILL,
	PGACTIVATE,
	PGDEACTIVATE,
	PGLAZYFREE,
	PGLAZYFREED,
#ifdef CONFIG_SWAP
	SWPIN_ZERO,
	SWPOUT_ZERO,
#endif
#ifdef CONFIG_ZSWAP
	ZSWPIN,
	ZSWPOUT,
	ZSWPWB,
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	THP_FAULT_ALLOC,
	THP_COLLAPSE_ALLOC,
	THP_SWPOUT,
	THP_SWPOUT_FALLBACK,
#endif
#ifdef CONFIG_NUMA_BALANCING
	NUMA_PAGE_MIGRATE,
	NUMA_PTE_UPDATES,
	NUMA_HINT_FAULTS,
#endif
};

#define NR_MEMCG_EVENTS ARRAY_SIZE(memcg_vm_event_stat)
static u8 mem_cgroup_events_index[NR_VM_EVENT_ITEMS] __read_mostly;

static void init_memcg_events(void)
{
	u8 i;

	BUILD_BUG_ON(NR_VM_EVENT_ITEMS >= U8_MAX);

	memset(mem_cgroup_events_index, U8_MAX,
	       sizeof(mem_cgroup_events_index));

	for (i = 0; i < NR_MEMCG_EVENTS; ++i)
		mem_cgroup_events_index[memcg_vm_event_stat[i]] = i;
}

static inline int memcg_events_index(enum vm_event_item idx)
{
	return mem_cgroup_events_index[idx];
}

struct memcg_vmstats_percpu {
	/* Stats updates since the last flush */
	unsigned int			stats_updates;

	/* Cached pointers for fast iteration in memcg_rstat_updated() */
	struct memcg_vmstats_percpu	*parent;
	struct memcg_vmstats		*vmstats;

	/* The above should fit a single cacheline for memcg_rstat_updated() */

	/* Local (CPU and cgroup) page state & events */
	long			state[MEMCG_VMSTAT_SIZE];
	unsigned long		events[NR_MEMCG_EVENTS];

	/* Delta calculation for lockless upward propagation */
	long			state_prev[MEMCG_VMSTAT_SIZE];
	unsigned long		events_prev[NR_MEMCG_EVENTS];
} ____cacheline_aligned;

struct memcg_vmstats {
	/* Aggregated (CPU and subtree) page state & events */
	long			state[MEMCG_VMSTAT_SIZE];
	unsigned long		events[NR_MEMCG_EVENTS];

	/* Non-hierarchical (CPU aggregated) page state & events */
	long			state_local[MEMCG_VMSTAT_SIZE];
	unsigned long		events_local[NR_MEMCG_EVENTS];

	/* Pending child counts during tree propagation */
	long			state_pending[MEMCG_VMSTAT_SIZE];
	unsigned long		events_pending[NR_MEMCG_EVENTS];

	/* Stats updates since the last flush */
	atomic64_t		stats_updates;
};

/*
 * memcg and lruvec stats flushing
 *
 * Many codepaths leading to stats update or read are performance sensitive and
 * adding stats flushing in such codepaths is not desirable. So, to optimize the
 * flushing the kernel does:
 *
 * 1) Periodically and asynchronously flush the stats every 2 seconds to not let
 *    rstat update tree grow unbounded.
 *
 * 2) Flush the stats synchronously on reader side only when there are more than
 *    (MEMCG_CHARGE_BATCH * nr_cpus) update events. Though this optimization
 *    will let stats be out of sync by atmost (MEMCG_CHARGE_BATCH * nr_cpus) but
 *    only for 2 seconds due to (1).
 */
static void flush_memcg_stats_dwork(struct work_struct *w);
static DECLARE_DEFERRABLE_WORK(stats_flush_dwork, flush_memcg_stats_dwork);
static u64 flush_last_time;

#define FLUSH_TIME (2UL*HZ)

/*
 * Accessors to ensure that preemption is disabled on PREEMPT_RT because it can
 * not rely on this as part of an acquired spinlock_t lock. These functions are
 * never used in hardirq context on PREEMPT_RT and therefore disabling preemtion
 * is sufficient.
 */
static void memcg_stats_lock(void)
{
	preempt_disable_nested();
	VM_WARN_ON_IRQS_ENABLED();
}

static void __memcg_stats_lock(void)
{
	preempt_disable_nested();
}

static void memcg_stats_unlock(void)
{
	preempt_enable_nested();
}


static bool memcg_vmstats_needs_flush(struct memcg_vmstats *vmstats)
{
	return atomic64_read(&vmstats->stats_updates) >
		MEMCG_CHARGE_BATCH * num_online_cpus();
}

static inline void memcg_rstat_updated(struct mem_cgroup *memcg, int val)
{
	struct memcg_vmstats_percpu *statc;
	int cpu = smp_processor_id();
	unsigned int stats_updates;

	if (!val)
		return;

	cgroup_rstat_updated(memcg->css.cgroup, cpu);
	statc = this_cpu_ptr(memcg->vmstats_percpu);
	for (; statc; statc = statc->parent) {
		stats_updates = READ_ONCE(statc->stats_updates) + abs(val);
		WRITE_ONCE(statc->stats_updates, stats_updates);
		if (stats_updates < MEMCG_CHARGE_BATCH)
			continue;

		/*
		 * If @memcg is already flush-able, increasing stats_updates is
		 * redundant. Avoid the overhead of the atomic update.
		 */
		if (!memcg_vmstats_needs_flush(statc->vmstats))
			atomic64_add(stats_updates,
				     &statc->vmstats->stats_updates);
		WRITE_ONCE(statc->stats_updates, 0);
	}
}

static void __mem_cgroup_flush_stats(struct mem_cgroup *memcg, bool force)
{
	bool needs_flush = memcg_vmstats_needs_flush(memcg->vmstats);

	trace_memcg_flush_stats(memcg, atomic64_read(&memcg->vmstats->stats_updates),
		force, needs_flush);

	if (!force && !needs_flush)
		return;

	if (mem_cgroup_is_root(memcg))
		WRITE_ONCE(flush_last_time, jiffies_64);

	cgroup_rstat_flush(memcg->css.cgroup);
}

/*
 * mem_cgroup_flush_stats - flush the stats of a memory cgroup subtree
 * @memcg: root of the subtree to flush
 *
 * Flushing is serialized by the underlying global rstat lock. There is also a
 * minimum amount of work to be done even if there are no stat updates to flush.
 * Hence, we only flush the stats if the updates delta exceeds a threshold. This
 * avoids unnecessary work and contention on the underlying lock.
 */
void mem_cgroup_flush_stats(struct mem_cgroup *memcg)
{
	if (mem_cgroup_disabled())
		return;

	if (!memcg)
		memcg = root_mem_cgroup;

	__mem_cgroup_flush_stats(memcg, false);
}

void mem_cgroup_flush_stats_ratelimited(struct mem_cgroup *memcg)
{
	/* Only flush if the periodic flusher is one full cycle late */
	if (time_after64(jiffies_64, READ_ONCE(flush_last_time) + 2*FLUSH_TIME))
		mem_cgroup_flush_stats(memcg);
}

static void flush_memcg_stats_dwork(struct work_struct *w)
{
	/*
	 * Deliberately ignore memcg_vmstats_needs_flush() here so that flushing
	 * in latency-sensitive paths is as cheap as possible.
	 */
	__mem_cgroup_flush_stats(root_mem_cgroup, true);
	queue_delayed_work(system_unbound_wq, &stats_flush_dwork, FLUSH_TIME);
}

unsigned long memcg_page_state(struct mem_cgroup *memcg, int idx)
{
	long x;
	int i = memcg_stats_index(idx);

	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, idx))
		return 0;

	x = READ_ONCE(memcg->vmstats->state[i]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

static int memcg_page_state_unit(int item);

/*
 * Normalize the value passed into memcg_rstat_updated() to be in pages. Round
 * up non-zero sub-page updates to 1 page as zero page updates are ignored.
 */
static int memcg_state_val_in_pages(int idx, int val)
{
	int unit = memcg_page_state_unit(idx);

	if (!val || unit == PAGE_SIZE)
		return val;
	else
		return max(val * unit / PAGE_SIZE, 1UL);
}

/**
 * __mod_memcg_state - update cgroup memory statistics
 * @memcg: the memory cgroup
 * @idx: the stat item - can be enum memcg_stat_item or enum node_stat_item
 * @val: delta to add to the counter, can be negative
 */
void __mod_memcg_state(struct mem_cgroup *memcg, enum memcg_stat_item idx,
		       int val)
{
	int i = memcg_stats_index(idx);

	if (mem_cgroup_disabled())
		return;

	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, idx))
		return;

	__this_cpu_add(memcg->vmstats_percpu->state[i], val);
	val = memcg_state_val_in_pages(idx, val);
	memcg_rstat_updated(memcg, val);
	trace_mod_memcg_state(memcg, idx, val);
}

#ifdef CONFIG_MEMCG_V1
/* idx can be of type enum memcg_stat_item or node_stat_item. */
unsigned long memcg_page_state_local(struct mem_cgroup *memcg, int idx)
{
	long x;
	int i = memcg_stats_index(idx);

	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, idx))
		return 0;

	x = READ_ONCE(memcg->vmstats->state_local[i]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}
#endif

static void __mod_memcg_lruvec_state(struct lruvec *lruvec,
				     enum node_stat_item idx,
				     int val)
{
	struct mem_cgroup_per_node *pn;
	struct mem_cgroup *memcg;
	int i = memcg_stats_index(idx);

	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, idx))
		return;

	pn = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	memcg = pn->memcg;

	/*
	 * The caller from rmap relies on disabled preemption because they never
	 * update their counter from in-interrupt context. For these two
	 * counters we check that the update is never performed from an
	 * interrupt context while other caller need to have disabled interrupt.
	 */
	__memcg_stats_lock();
	if (IS_ENABLED(CONFIG_DEBUG_VM)) {
		switch (idx) {
		case NR_ANON_MAPPED:
		case NR_FILE_MAPPED:
		case NR_ANON_THPS:
			WARN_ON_ONCE(!in_task());
			break;
		default:
			VM_WARN_ON_IRQS_ENABLED();
		}
	}

	/* Update memcg */
	__this_cpu_add(memcg->vmstats_percpu->state[i], val);

	/* Update lruvec */
	__this_cpu_add(pn->lruvec_stats_percpu->state[i], val);

	val = memcg_state_val_in_pages(idx, val);
	memcg_rstat_updated(memcg, val);
	trace_mod_memcg_lruvec_state(memcg, idx, val);
	memcg_stats_unlock();
}

/**
 * __mod_lruvec_state - update lruvec memory statistics
 * @lruvec: the lruvec
 * @idx: the stat item
 * @val: delta to add to the counter, can be negative
 *
 * The lruvec is the intersection of the NUMA node and a cgroup. This
 * function updates the all three counters that are affected by a
 * change of state at this level: per-node, per-cgroup, per-lruvec.
 */
void __mod_lruvec_state(struct lruvec *lruvec, enum node_stat_item idx,
			int val)
{
	/* Update node */
	__mod_node_page_state(lruvec_pgdat(lruvec), idx, val);

	/* Update memcg and lruvec */
	if (!mem_cgroup_disabled())
		__mod_memcg_lruvec_state(lruvec, idx, val);
}

void __lruvec_stat_mod_folio(struct folio *folio, enum node_stat_item idx,
			     int val)
{
	struct mem_cgroup *memcg;
	pg_data_t *pgdat = folio_pgdat(folio);
	struct lruvec *lruvec;

	rcu_read_lock();
	memcg = folio_memcg(folio);
	/* Untracked pages have no memcg, no lruvec. Update only the node */
	if (!memcg) {
		rcu_read_unlock();
		__mod_node_page_state(pgdat, idx, val);
		return;
	}

	lruvec = mem_cgroup_lruvec(memcg, pgdat);
	__mod_lruvec_state(lruvec, idx, val);
	rcu_read_unlock();
}
EXPORT_SYMBOL(__lruvec_stat_mod_folio);

void __mod_lruvec_kmem_state(void *p, enum node_stat_item idx, int val)
{
	pg_data_t *pgdat = page_pgdat(virt_to_page(p));
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;

	rcu_read_lock();
	memcg = mem_cgroup_from_slab_obj(p);

	/*
	 * Untracked pages have no memcg, no lruvec. Update only the
	 * node. If we reparent the slab objects to the root memcg,
	 * when we free the slab object, we need to update the per-memcg
	 * vmstats to keep it correct for the root memcg.
	 */
	if (!memcg) {
		__mod_node_page_state(pgdat, idx, val);
	} else {
		lruvec = mem_cgroup_lruvec(memcg, pgdat);
		__mod_lruvec_state(lruvec, idx, val);
	}
	rcu_read_unlock();
}

/**
 * __count_memcg_events - account VM events in a cgroup
 * @memcg: the memory cgroup
 * @idx: the event item
 * @count: the number of events that occurred
 */
void __count_memcg_events(struct mem_cgroup *memcg, enum vm_event_item idx,
			  unsigned long count)
{
	int i = memcg_events_index(idx);

	if (mem_cgroup_disabled())
		return;

	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, idx))
		return;

	memcg_stats_lock();
	__this_cpu_add(memcg->vmstats_percpu->events[i], count);
	memcg_rstat_updated(memcg, count);
	trace_count_memcg_events(memcg, idx, count);
	memcg_stats_unlock();
}

unsigned long memcg_events(struct mem_cgroup *memcg, int event)
{
	int i = memcg_events_index(event);

	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, event))
		return 0;

	return READ_ONCE(memcg->vmstats->events[i]);
}

#ifdef CONFIG_MEMCG_V1
unsigned long memcg_events_local(struct mem_cgroup *memcg, int event)
{
	int i = memcg_events_index(event);

	if (WARN_ONCE(BAD_STAT_IDX(i), "%s: missing stat item %d\n", __func__, event))
		return 0;

	return READ_ONCE(memcg->vmstats->events_local[i]);
}
#endif

struct mem_cgroup *mem_cgroup_from_task(struct task_struct *p)
{
	/*
	 * mm_update_next_owner() may clear mm->owner to NULL
	 * if it races with swapoff, page migration, etc.
	 * So this can be called with p == NULL.
	 */
	if (unlikely(!p))
		return NULL;

	return mem_cgroup_from_css(task_css(p, memory_cgrp_id));
}
EXPORT_SYMBOL(mem_cgroup_from_task);

static __always_inline struct mem_cgroup *active_memcg(void)
{
	if (!in_task())
		return this_cpu_read(int_active_memcg);
	else
		return current->active_memcg;
}

/**
 * get_mem_cgroup_from_mm: Obtain a reference on given mm_struct's memcg.
 * @mm: mm from which memcg should be extracted. It can be NULL.
 *
 * Obtain a reference on mm->memcg and returns it if successful. If mm
 * is NULL, then the memcg is chosen as follows:
 * 1) The active memcg, if set.
 * 2) current->mm->memcg, if available
 * 3) root memcg
 * If mem_cgroup is disabled, NULL is returned.
 */
struct mem_cgroup *get_mem_cgroup_from_mm(struct mm_struct *mm)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return NULL;

	/*
	 * Page cache insertions can happen without an
	 * actual mm context, e.g. during disk probing
	 * on boot, loopback IO, acct() writes etc.
	 *
	 * No need to css_get on root memcg as the reference
	 * counting is disabled on the root level in the
	 * cgroup core. See CSS_NO_REF.
	 */
	if (unlikely(!mm)) {
		memcg = active_memcg();
		if (unlikely(memcg)) {
			/* remote memcg must hold a ref */
			css_get(&memcg->css);
			return memcg;
		}
		mm = current->mm;
		if (unlikely(!mm))
			return root_mem_cgroup;
	}

	rcu_read_lock();
	do {
		memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
		if (unlikely(!memcg))
			memcg = root_mem_cgroup;
	} while (!css_tryget(&memcg->css));
	rcu_read_unlock();
	return memcg;
}
EXPORT_SYMBOL(get_mem_cgroup_from_mm);

/**
 * get_mem_cgroup_from_current - Obtain a reference on current task's memcg.
 */
struct mem_cgroup *get_mem_cgroup_from_current(void)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return NULL;

again:
	rcu_read_lock();
	memcg = mem_cgroup_from_task(current);
	if (!css_tryget(&memcg->css)) {
		rcu_read_unlock();
		goto again;
	}
	rcu_read_unlock();
	return memcg;
}

/**
 * get_mem_cgroup_from_folio - Obtain a reference on a given folio's memcg.
 * @folio: folio from which memcg should be extracted.
 */
struct mem_cgroup *get_mem_cgroup_from_folio(struct folio *folio)
{
	struct mem_cgroup *memcg = folio_memcg(folio);

	if (mem_cgroup_disabled())
		return NULL;

	rcu_read_lock();
	if (!memcg || WARN_ON_ONCE(!css_tryget(&memcg->css)))
		memcg = root_mem_cgroup;
	rcu_read_unlock();
	return memcg;
}

/**
 * mem_cgroup_iter - iterate over memory cgroup hierarchy
 * @root: hierarchy root
 * @prev: previously returned memcg, NULL on first invocation
 * @reclaim: cookie for shared reclaim walks, NULL for full walks
 *
 * Returns references to children of the hierarchy below @root, or
 * @root itself, or %NULL after a full round-trip.
 *
 * Caller must pass the return value in @prev on subsequent
 * invocations for reference counting, or use mem_cgroup_iter_break()
 * to cancel a hierarchy walk before the round-trip is complete.
 *
 * Reclaimers can specify a node in @reclaim to divide up the memcgs
 * in the hierarchy among all concurrent reclaimers operating on the
 * same node.
 */
struct mem_cgroup *mem_cgroup_iter(struct mem_cgroup *root,
				   struct mem_cgroup *prev,
				   struct mem_cgroup_reclaim_cookie *reclaim)
{
	struct mem_cgroup_reclaim_iter *iter;
	struct cgroup_subsys_state *css;
	struct mem_cgroup *pos;
	struct mem_cgroup *next;

	if (mem_cgroup_disabled())
		return NULL;

	if (!root)
		root = root_mem_cgroup;

	rcu_read_lock();
restart:
	next = NULL;

	if (reclaim) {
		int gen;
		int nid = reclaim->pgdat->node_id;

		iter = &root->nodeinfo[nid]->iter;
		gen = atomic_read(&iter->generation);

		/*
		 * On start, join the current reclaim iteration cycle.
		 * Exit when a concurrent walker completes it.
		 */
		if (!prev)
			reclaim->generation = gen;
		else if (reclaim->generation != gen)
			goto out_unlock;

		pos = READ_ONCE(iter->position);
	} else
		pos = prev;

	css = pos ? &pos->css : NULL;

	while ((css = css_next_descendant_pre(css, &root->css))) {
		/*
		 * Verify the css and acquire a reference.  The root
		 * is provided by the caller, so we know it's alive
		 * and kicking, and don't take an extra reference.
		 */
		if (css == &root->css || css_tryget(css))
			break;
	}

	next = mem_cgroup_from_css(css);

	if (reclaim) {
		/*
		 * The position could have already been updated by a competing
		 * thread, so check that the value hasn't changed since we read
		 * it to avoid reclaiming from the same cgroup twice.
		 */
		if (cmpxchg(&iter->position, pos, next) != pos) {
			if (css && css != &root->css)
				css_put(css);
			goto restart;
		}

		if (!next) {
			atomic_inc(&iter->generation);

			/*
			 * Reclaimers share the hierarchy walk, and a
			 * new one might jump in right at the end of
			 * the hierarchy - make sure they see at least
			 * one group and restart from the beginning.
			 */
			if (!prev)
				goto restart;
		}
	}

out_unlock:
	rcu_read_unlock();
	if (prev && prev != root)
		css_put(&prev->css);

	return next;
}

/**
 * mem_cgroup_iter_break - abort a hierarchy walk prematurely
 * @root: hierarchy root
 * @prev: last visited hierarchy member as returned by mem_cgroup_iter()
 */
void mem_cgroup_iter_break(struct mem_cgroup *root,
			   struct mem_cgroup *prev)
{
	if (!root)
		root = root_mem_cgroup;
	if (prev && prev != root)
		css_put(&prev->css);
}

static void __invalidate_reclaim_iterators(struct mem_cgroup *from,
					struct mem_cgroup *dead_memcg)
{
	struct mem_cgroup_reclaim_iter *iter;
	struct mem_cgroup_per_node *mz;
	int nid;

	for_each_node(nid) {
		mz = from->nodeinfo[nid];
		iter = &mz->iter;
		cmpxchg(&iter->position, dead_memcg, NULL);
	}
}

static void invalidate_reclaim_iterators(struct mem_cgroup *dead_memcg)
{
	struct mem_cgroup *memcg = dead_memcg;
	struct mem_cgroup *last;

	do {
		__invalidate_reclaim_iterators(memcg, dead_memcg);
		last = memcg;
	} while ((memcg = parent_mem_cgroup(memcg)));

	/*
	 * When cgroup1 non-hierarchy mode is used,
	 * parent_mem_cgroup() does not walk all the way up to the
	 * cgroup root (root_mem_cgroup). So we have to handle
	 * dead_memcg from cgroup root separately.
	 */
	if (!mem_cgroup_is_root(last))
		__invalidate_reclaim_iterators(root_mem_cgroup,
						dead_memcg);
}

/**
 * mem_cgroup_scan_tasks - iterate over tasks of a memory cgroup hierarchy
 * @memcg: hierarchy root
 * @fn: function to call for each task
 * @arg: argument passed to @fn
 *
 * This function iterates over tasks attached to @memcg or to any of its
 * descendants and calls @fn for each task. If @fn returns a non-zero
 * value, the function breaks the iteration loop. Otherwise, it will iterate
 * over all tasks and return 0.
 *
 * This function must not be called for the root memory cgroup.
 */
void mem_cgroup_scan_tasks(struct mem_cgroup *memcg,
			   int (*fn)(struct task_struct *, void *), void *arg)
{
	struct mem_cgroup *iter;
	int ret = 0;
	int i = 0;

	BUG_ON(mem_cgroup_is_root(memcg));

	for_each_mem_cgroup_tree(iter, memcg) {
		struct css_task_iter it;
		struct task_struct *task;

		css_task_iter_start(&iter->css, CSS_TASK_ITER_PROCS, &it);
		while (!ret && (task = css_task_iter_next(&it))) {
			/* Avoid potential softlockup warning */
			if ((++i & 1023) == 0)
				cond_resched();
			ret = fn(task, arg);
		}
		css_task_iter_end(&it);
		if (ret) {
			mem_cgroup_iter_break(memcg, iter);
			break;
		}
	}
}

#ifdef CONFIG_DEBUG_VM
void lruvec_memcg_debug(struct lruvec *lruvec, struct folio *folio)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return;

	memcg = folio_memcg(folio);

	if (!memcg)
		VM_BUG_ON_FOLIO(!mem_cgroup_is_root(lruvec_memcg(lruvec)), folio);
	else
		VM_BUG_ON_FOLIO(lruvec_memcg(lruvec) != memcg, folio);
}
#endif

/**
 * folio_lruvec_lock - Lock the lruvec for a folio.
 * @folio: Pointer to the folio.
 *
 * These functions are safe to use under any of the following conditions:
 * - folio locked
 * - folio_test_lru false
 * - folio frozen (refcount of 0)
 *
 * Return: The lruvec this folio is on with its lock held.
 */
struct lruvec *folio_lruvec_lock(struct folio *folio)
{
	struct lruvec *lruvec = folio_lruvec(folio);

	spin_lock(&lruvec->lru_lock);
	lruvec_memcg_debug(lruvec, folio);

	return lruvec;
}

/**
 * folio_lruvec_lock_irq - Lock the lruvec for a folio.
 * @folio: Pointer to the folio.
 *
 * These functions are safe to use under any of the following conditions:
 * - folio locked
 * - folio_test_lru false
 * - folio frozen (refcount of 0)
 *
 * Return: The lruvec this folio is on with its lock held and interrupts
 * disabled.
 */
struct lruvec *folio_lruvec_lock_irq(struct folio *folio)
{
	struct lruvec *lruvec = folio_lruvec(folio);

	spin_lock_irq(&lruvec->lru_lock);
	lruvec_memcg_debug(lruvec, folio);

	return lruvec;
}

/**
 * folio_lruvec_lock_irqsave - Lock the lruvec for a folio.
 * @folio: Pointer to the folio.
 * @flags: Pointer to irqsave flags.
 *
 * These functions are safe to use under any of the following conditions:
 * - folio locked
 * - folio_test_lru false
 * - folio frozen (refcount of 0)
 *
 * Return: The lruvec this folio is on with its lock held and interrupts
 * disabled.
 */
struct lruvec *folio_lruvec_lock_irqsave(struct folio *folio,
		unsigned long *flags)
{
	struct lruvec *lruvec = folio_lruvec(folio);

	spin_lock_irqsave(&lruvec->lru_lock, *flags);
	lruvec_memcg_debug(lruvec, folio);

	return lruvec;
}

/**
 * mem_cgroup_update_lru_size - account for adding or removing an lru page
 * @lruvec: mem_cgroup per zone lru vector
 * @lru: index of lru list the page is sitting on
 * @zid: zone id of the accounted pages
 * @nr_pages: positive when adding or negative when removing
 *
 * This function must be called under lru_lock, just before a page is added
 * to or just after a page is removed from an lru list.
 */
void mem_cgroup_update_lru_size(struct lruvec *lruvec, enum lru_list lru,
				int zid, int nr_pages)
{
	struct mem_cgroup_per_node *mz;
	unsigned long *lru_size;
	long size;

	if (mem_cgroup_disabled())
		return;

	mz = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	lru_size = &mz->lru_zone_size[zid][lru];

	if (nr_pages < 0)
		*lru_size += nr_pages;

	size = *lru_size;
	if (WARN_ONCE(size < 0,
		"%s(%p, %d, %d): lru_size %ld\n",
		__func__, lruvec, lru, nr_pages, size)) {
		VM_BUG_ON(1);
		*lru_size = 0;
	}

	if (nr_pages > 0)
		*lru_size += nr_pages;
}

/**
 * mem_cgroup_margin - calculate chargeable space of a memory cgroup
 * @memcg: the memory cgroup
 *
 * Returns the maximum amount of memory @mem can be charged with, in
 * pages.
 */
static unsigned long mem_cgroup_margin(struct mem_cgroup *memcg)
{
	unsigned long margin = 0;
	unsigned long count;
	unsigned long limit;

	count = page_counter_read(&memcg->memory);
	limit = READ_ONCE(memcg->memory.max);
	if (count < limit)
		margin = limit - count;

	if (do_memsw_account()) {
		count = page_counter_read(&memcg->memsw);
		limit = READ_ONCE(memcg->memsw.max);
		if (count < limit)
			margin = min(margin, limit - count);
		else
			margin = 0;
	}

	return margin;
}

struct memory_stat {
	const char *name;
	unsigned int idx;
};

static const struct memory_stat memory_stats[] = {
	{ "anon",			NR_ANON_MAPPED			},
	{ "file",			NR_FILE_PAGES			},
	{ "kernel",			MEMCG_KMEM			},
	{ "kernel_stack",		NR_KERNEL_STACK_KB		},
	{ "pagetables",			NR_PAGETABLE			},
	{ "sec_pagetables",		NR_SECONDARY_PAGETABLE		},
	{ "percpu",			MEMCG_PERCPU_B			},
	{ "sock",			MEMCG_SOCK			},
	{ "vmalloc",			MEMCG_VMALLOC			},
	{ "shmem",			NR_SHMEM			},
#ifdef CONFIG_ZSWAP
	{ "zswap",			MEMCG_ZSWAP_B			},
	{ "zswapped",			MEMCG_ZSWAPPED			},
#endif
	{ "file_mapped",		NR_FILE_MAPPED			},
	{ "file_dirty",			NR_FILE_DIRTY			},
	{ "file_writeback",		NR_WRITEBACK			},
#ifdef CONFIG_SWAP
	{ "swapcached",			NR_SWAPCACHE			},
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	{ "anon_thp",			NR_ANON_THPS			},
	{ "file_thp",			NR_FILE_THPS			},
	{ "shmem_thp",			NR_SHMEM_THPS			},
#endif
	{ "inactive_anon",		NR_INACTIVE_ANON		},
	{ "active_anon",		NR_ACTIVE_ANON			},
	{ "inactive_file",		NR_INACTIVE_FILE		},
	{ "active_file",		NR_ACTIVE_FILE			},
	{ "unevictable",		NR_UNEVICTABLE			},
	{ "slab_reclaimable",		NR_SLAB_RECLAIMABLE_B		},
	{ "slab_unreclaimable",		NR_SLAB_UNRECLAIMABLE_B		},
#ifdef CONFIG_HUGETLB_PAGE
	{ "hugetlb",			NR_HUGETLB			},
#endif

	/* The memory events */
	{ "workingset_refault_anon",	WORKINGSET_REFAULT_ANON		},
	{ "workingset_refault_file",	WORKINGSET_REFAULT_FILE		},
	{ "workingset_activate_anon",	WORKINGSET_ACTIVATE_ANON	},
	{ "workingset_activate_file",	WORKINGSET_ACTIVATE_FILE	},
	{ "workingset_restore_anon",	WORKINGSET_RESTORE_ANON		},
	{ "workingset_restore_file",	WORKINGSET_RESTORE_FILE		},
	{ "workingset_nodereclaim",	WORKINGSET_NODERECLAIM		},

	{ "pgdemote_kswapd",		PGDEMOTE_KSWAPD		},
	{ "pgdemote_direct",		PGDEMOTE_DIRECT		},
	{ "pgdemote_khugepaged",	PGDEMOTE_KHUGEPAGED	},
#ifdef CONFIG_NUMA_BALANCING
	{ "pgpromote_success",		PGPROMOTE_SUCCESS	},
#endif
};

/* The actual unit of the state item, not the same as the output unit */
static int memcg_page_state_unit(int item)
{
	switch (item) {
	case MEMCG_PERCPU_B:
	case MEMCG_ZSWAP_B:
	case NR_SLAB_RECLAIMABLE_B:
	case NR_SLAB_UNRECLAIMABLE_B:
		return 1;
	case NR_KERNEL_STACK_KB:
		return SZ_1K;
	default:
		return PAGE_SIZE;
	}
}

/* Translate stat items to the correct unit for memory.stat output */
static int memcg_page_state_output_unit(int item)
{
	/*
	 * Workingset state is actually in pages, but we export it to userspace
	 * as a scalar count of events, so special case it here.
	 *
	 * Demotion and promotion activities are exported in pages, consistent
	 * with their global counterparts.
	 */
	switch (item) {
	case WORKINGSET_REFAULT_ANON:
	case WORKINGSET_REFAULT_FILE:
	case WORKINGSET_ACTIVATE_ANON:
	case WORKINGSET_ACTIVATE_FILE:
	case WORKINGSET_RESTORE_ANON:
	case WORKINGSET_RESTORE_FILE:
	case WORKINGSET_NODERECLAIM:
	case PGDEMOTE_KSWAPD:
	case PGDEMOTE_DIRECT:
	case PGDEMOTE_KHUGEPAGED:
#ifdef CONFIG_NUMA_BALANCING
	case PGPROMOTE_SUCCESS:
#endif
		return 1;
	default:
		return memcg_page_state_unit(item);
	}
}

unsigned long memcg_page_state_output(struct mem_cgroup *memcg, int item)
{
	return memcg_page_state(memcg, item) *
		memcg_page_state_output_unit(item);
}

#ifdef CONFIG_MEMCG_V1
unsigned long memcg_page_state_local_output(struct mem_cgroup *memcg, int item)
{
	return memcg_page_state_local(memcg, item) *
		memcg_page_state_output_unit(item);
}
#endif

#ifdef CONFIG_HUGETLB_PAGE
static bool memcg_accounts_hugetlb(void)
{
	return cgrp_dfl_root.flags & CGRP_ROOT_MEMORY_HUGETLB_ACCOUNTING;
}
#else /* CONFIG_HUGETLB_PAGE */
static bool memcg_accounts_hugetlb(void)
{
	return false;
}
#endif /* CONFIG_HUGETLB_PAGE */

static void memcg_stat_format(struct mem_cgroup *memcg, struct seq_buf *s)
{
	int i;

	/*
	 * Provide statistics on the state of the memory subsystem as
	 * well as cumulative event counters that show past behavior.
	 *
	 * This list is ordered following a combination of these gradients:
	 * 1) generic big picture -> specifics and details
	 * 2) reflecting userspace activity -> reflecting kernel heuristics
	 *
	 * Current memory state:
	 */
	mem_cgroup_flush_stats(memcg);

	for (i = 0; i < ARRAY_SIZE(memory_stats); i++) {
		u64 size;

#ifdef CONFIG_HUGETLB_PAGE
		if (unlikely(memory_stats[i].idx == NR_HUGETLB) &&
			!memcg_accounts_hugetlb())
			continue;
#endif
		size = memcg_page_state_output(memcg, memory_stats[i].idx);
		seq_buf_printf(s, "%s %llu\n", memory_stats[i].name, size);

		if (unlikely(memory_stats[i].idx == NR_SLAB_UNRECLAIMABLE_B)) {
			size += memcg_page_state_output(memcg,
							NR_SLAB_RECLAIMABLE_B);
			seq_buf_printf(s, "slab %llu\n", size);
		}
	}

	/* Accumulated memory events */
	seq_buf_printf(s, "pgscan %lu\n",
		       memcg_events(memcg, PGSCAN_KSWAPD) +
		       memcg_events(memcg, PGSCAN_DIRECT) +
		       memcg_events(memcg, PGSCAN_KHUGEPAGED));
	seq_buf_printf(s, "pgsteal %lu\n",
		       memcg_events(memcg, PGSTEAL_KSWAPD) +
		       memcg_events(memcg, PGSTEAL_DIRECT) +
		       memcg_events(memcg, PGSTEAL_KHUGEPAGED));

	for (i = 0; i < ARRAY_SIZE(memcg_vm_event_stat); i++) {
#ifdef CONFIG_MEMCG_V1
		if (memcg_vm_event_stat[i] == PGPGIN ||
		    memcg_vm_event_stat[i] == PGPGOUT)
			continue;
#endif
		seq_buf_printf(s, "%s %lu\n",
			       vm_event_name(memcg_vm_event_stat[i]),
			       memcg_events(memcg, memcg_vm_event_stat[i]));
	}
}

static void memory_stat_format(struct mem_cgroup *memcg, struct seq_buf *s)
{
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		memcg_stat_format(memcg, s);
	else
		memcg1_stat_format(memcg, s);
	if (seq_buf_has_overflowed(s))
		pr_warn("%s: Warning, stat buffer overflow, please report\n", __func__);
}

/**
 * mem_cgroup_print_oom_context: Print OOM information relevant to
 * memory controller.
 * @memcg: The memory cgroup that went over limit
 * @p: Task that is going to be killed
 *
 * NOTE: @memcg and @p's mem_cgroup can be different when hierarchy is
 * enabled
 */
void mem_cgroup_print_oom_context(struct mem_cgroup *memcg, struct task_struct *p)
{
	rcu_read_lock();

	if (memcg) {
		pr_cont(",oom_memcg=");
		pr_cont_cgroup_path(memcg->css.cgroup);
	} else
		pr_cont(",global_oom");
	if (p) {
		pr_cont(",task_memcg=");
		pr_cont_cgroup_path(task_cgroup(p, memory_cgrp_id));
	}
	rcu_read_unlock();
}

/**
 * mem_cgroup_print_oom_meminfo: Print OOM memory information relevant to
 * memory controller.
 * @memcg: The memory cgroup that went over limit
 */
void mem_cgroup_print_oom_meminfo(struct mem_cgroup *memcg)
{
	/* Use static buffer, for the caller is holding oom_lock. */
	static char buf[SEQ_BUF_SIZE];
	struct seq_buf s;

	lockdep_assert_held(&oom_lock);

	pr_info("memory: usage %llukB, limit %llukB, failcnt %lu\n",
		K((u64)page_counter_read(&memcg->memory)),
		K((u64)READ_ONCE(memcg->memory.max)), memcg->memory.failcnt);
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		pr_info("swap: usage %llukB, limit %llukB, failcnt %lu\n",
			K((u64)page_counter_read(&memcg->swap)),
			K((u64)READ_ONCE(memcg->swap.max)), memcg->swap.failcnt);
#ifdef CONFIG_MEMCG_V1
	else {
		pr_info("memory+swap: usage %llukB, limit %llukB, failcnt %lu\n",
			K((u64)page_counter_read(&memcg->memsw)),
			K((u64)memcg->memsw.max), memcg->memsw.failcnt);
		pr_info("kmem: usage %llukB, limit %llukB, failcnt %lu\n",
			K((u64)page_counter_read(&memcg->kmem)),
			K((u64)memcg->kmem.max), memcg->kmem.failcnt);
	}
#endif

	pr_info("Memory cgroup stats for ");
	pr_cont_cgroup_path(memcg->css.cgroup);
	pr_cont(":");
	seq_buf_init(&s, buf, SEQ_BUF_SIZE);
	memory_stat_format(memcg, &s);
	seq_buf_do_printk(&s, KERN_INFO);
}

/*
 * Return the memory (and swap, if configured) limit for a memcg.
 */
unsigned long mem_cgroup_get_max(struct mem_cgroup *memcg)
{
	unsigned long max = READ_ONCE(memcg->memory.max);

	if (do_memsw_account()) {
		if (mem_cgroup_swappiness(memcg)) {
			/* Calculate swap excess capacity from memsw limit */
			unsigned long swap = READ_ONCE(memcg->memsw.max) - max;

			max += min(swap, (unsigned long)total_swap_pages);
		}
	} else {
		if (mem_cgroup_swappiness(memcg))
			max += min(READ_ONCE(memcg->swap.max),
				   (unsigned long)total_swap_pages);
	}
	return max;
}

unsigned long mem_cgroup_size(struct mem_cgroup *memcg)
{
	return page_counter_read(&memcg->memory);
}

static bool mem_cgroup_out_of_memory(struct mem_cgroup *memcg, gfp_t gfp_mask,
				     int order)
{
	struct oom_control oc = {
		.zonelist = NULL,
		.nodemask = NULL,
		.memcg = memcg,
		.gfp_mask = gfp_mask,
		.order = order,
	};
	bool ret = true;

	if (mutex_lock_killable(&oom_lock))
		return true;

	if (mem_cgroup_margin(memcg) >= (1 << order))
		goto unlock;

	/*
	 * A few threads which were not waiting at mutex_lock_killable() can
	 * fail to bail out. Therefore, check again after holding oom_lock.
	 */
	ret = task_is_dying() || out_of_memory(&oc);

unlock:
	mutex_unlock(&oom_lock);
	return ret;
}

/*
 * Returns true if successfully killed one or more processes. Though in some
 * corner cases it can return true even without killing any process.
 */
static bool mem_cgroup_oom(struct mem_cgroup *memcg, gfp_t mask, int order)
{
	bool locked, ret;

	if (order > PAGE_ALLOC_COSTLY_ORDER)
		return false;

	memcg_memory_event(memcg, MEMCG_OOM);

	if (!memcg1_oom_prepare(memcg, &locked))
		return false;

	ret = mem_cgroup_out_of_memory(memcg, mask, order);

	memcg1_oom_finish(memcg, locked);

	return ret;
}

/**
 * mem_cgroup_get_oom_group - get a memory cgroup to clean up after OOM
 * @victim: task to be killed by the OOM killer
 * @oom_domain: memcg in case of memcg OOM, NULL in case of system-wide OOM
 *
 * Returns a pointer to a memory cgroup, which has to be cleaned up
 * by killing all belonging OOM-killable tasks.
 *
 * Caller has to call mem_cgroup_put() on the returned non-NULL memcg.
 */
struct mem_cgroup *mem_cgroup_get_oom_group(struct task_struct *victim,
					    struct mem_cgroup *oom_domain)
{
	struct mem_cgroup *oom_group = NULL;
	struct mem_cgroup *memcg;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return NULL;

	if (!oom_domain)
		oom_domain = root_mem_cgroup;

	rcu_read_lock();

	memcg = mem_cgroup_from_task(victim);
	if (mem_cgroup_is_root(memcg))
		goto out;

	/*
	 * If the victim task has been asynchronously moved to a different
	 * memory cgroup, we might end up killing tasks outside oom_domain.
	 * In this case it's better to ignore memory.group.oom.
	 */
	if (unlikely(!mem_cgroup_is_descendant(memcg, oom_domain)))
		goto out;

	/*
	 * Traverse the memory cgroup hierarchy from the victim task's
	 * cgroup up to the OOMing cgroup (or root) to find the
	 * highest-level memory cgroup with oom.group set.
	 */
	for (; memcg; memcg = parent_mem_cgroup(memcg)) {
		if (READ_ONCE(memcg->oom_group))
			oom_group = memcg;

		if (memcg == oom_domain)
			break;
	}

	if (oom_group)
		css_get(&oom_group->css);
out:
	rcu_read_unlock();

	return oom_group;
}

void mem_cgroup_print_oom_group(struct mem_cgroup *memcg)
{
	pr_info("Tasks in ");
	pr_cont_cgroup_path(memcg->css.cgroup);
	pr_cont(" are going to be killed due to memory.oom.group set\n");
}

struct memcg_stock_pcp {
	local_lock_t stock_lock;
	struct mem_cgroup *cached; /* this never be root cgroup */
	unsigned int nr_pages;

	struct obj_cgroup *cached_objcg;
	struct pglist_data *cached_pgdat;
	unsigned int nr_bytes;
	int nr_slab_reclaimable_b;
	int nr_slab_unreclaimable_b;

	struct work_struct work;
	unsigned long flags;
#define FLUSHING_CACHED_CHARGE	0
};
static DEFINE_PER_CPU(struct memcg_stock_pcp, memcg_stock) = {
	.stock_lock = INIT_LOCAL_LOCK(stock_lock),
};
static DEFINE_MUTEX(percpu_charge_mutex);

static struct obj_cgroup *drain_obj_stock(struct memcg_stock_pcp *stock);
static bool obj_stock_flush_required(struct memcg_stock_pcp *stock,
				     struct mem_cgroup *root_memcg);

/**
 * consume_stock: Try to consume stocked charge on this cpu.
 * @memcg: memcg to consume from.
 * @nr_pages: how many pages to charge.
 *
 * The charges will only happen if @memcg matches the current cpu's memcg
 * stock, and at least @nr_pages are available in that stock.  Failure to
 * service an allocation will refill the stock.
 *
 * returns true if successful, false otherwise.
 */
static bool consume_stock(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	struct memcg_stock_pcp *stock;
	unsigned int stock_pages;
	unsigned long flags;
	bool ret = false;

	if (nr_pages > MEMCG_CHARGE_BATCH)
		return ret;

	local_lock_irqsave(&memcg_stock.stock_lock, flags);

	stock = this_cpu_ptr(&memcg_stock);
	stock_pages = READ_ONCE(stock->nr_pages);
	if (memcg == READ_ONCE(stock->cached) && stock_pages >= nr_pages) {
		WRITE_ONCE(stock->nr_pages, stock_pages - nr_pages);
		ret = true;
	}

	local_unlock_irqrestore(&memcg_stock.stock_lock, flags);

	return ret;
}

/*
 * Returns stocks cached in percpu and reset cached information.
 */
static void drain_stock(struct memcg_stock_pcp *stock)
{
	unsigned int stock_pages = READ_ONCE(stock->nr_pages);
	struct mem_cgroup *old = READ_ONCE(stock->cached);

	if (!old)
		return;

	if (stock_pages) {
		page_counter_uncharge(&old->memory, stock_pages);
		if (do_memsw_account())
			page_counter_uncharge(&old->memsw, stock_pages);

		WRITE_ONCE(stock->nr_pages, 0);
	}

	css_put(&old->css);
	WRITE_ONCE(stock->cached, NULL);
}

static void drain_local_stock(struct work_struct *dummy)
{
	struct memcg_stock_pcp *stock;
	struct obj_cgroup *old = NULL;
	unsigned long flags;

	/*
	 * The only protection from cpu hotplug (memcg_hotplug_cpu_dead) vs.
	 * drain_stock races is that we always operate on local CPU stock
	 * here with IRQ disabled
	 */
	local_lock_irqsave(&memcg_stock.stock_lock, flags);

	stock = this_cpu_ptr(&memcg_stock);
	old = drain_obj_stock(stock);
	drain_stock(stock);
	clear_bit(FLUSHING_CACHED_CHARGE, &stock->flags);

	local_unlock_irqrestore(&memcg_stock.stock_lock, flags);
	obj_cgroup_put(old);
}

/*
 * Cache charges(val) to local per_cpu area.
 * This will be consumed by consume_stock() function, later.
 */
static void __refill_stock(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	struct memcg_stock_pcp *stock;
	unsigned int stock_pages;

	stock = this_cpu_ptr(&memcg_stock);
	if (READ_ONCE(stock->cached) != memcg) { /* reset if necessary */
		drain_stock(stock);
		css_get(&memcg->css);
		WRITE_ONCE(stock->cached, memcg);
	}
	stock_pages = READ_ONCE(stock->nr_pages) + nr_pages;
	WRITE_ONCE(stock->nr_pages, stock_pages);

	if (stock_pages > MEMCG_CHARGE_BATCH)
		drain_stock(stock);
}

static void refill_stock(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	unsigned long flags;

	local_lock_irqsave(&memcg_stock.stock_lock, flags);
	__refill_stock(memcg, nr_pages);
	local_unlock_irqrestore(&memcg_stock.stock_lock, flags);
}

/*
 * Drains all per-CPU charge caches for given root_memcg resp. subtree
 * of the hierarchy under it.
 */
void drain_all_stock(struct mem_cgroup *root_memcg)
{
	int cpu, curcpu;

	/* If someone's already draining, avoid adding running more workers. */
	if (!mutex_trylock(&percpu_charge_mutex))
		return;
	/*
	 * Notify other cpus that system-wide "drain" is running
	 * We do not care about races with the cpu hotplug because cpu down
	 * as well as workers from this path always operate on the local
	 * per-cpu data. CPU up doesn't touch memcg_stock at all.
	 */
	migrate_disable();
	curcpu = smp_processor_id();
	for_each_online_cpu(cpu) {
		struct memcg_stock_pcp *stock = &per_cpu(memcg_stock, cpu);
		struct mem_cgroup *memcg;
		bool flush = false;

		rcu_read_lock();
		memcg = READ_ONCE(stock->cached);
		if (memcg && READ_ONCE(stock->nr_pages) &&
		    mem_cgroup_is_descendant(memcg, root_memcg))
			flush = true;
		else if (obj_stock_flush_required(stock, root_memcg))
			flush = true;
		rcu_read_unlock();

		if (flush &&
		    !test_and_set_bit(FLUSHING_CACHED_CHARGE, &stock->flags)) {
			if (cpu == curcpu)
				drain_local_stock(&stock->work);
			else if (!cpu_is_isolated(cpu))
				schedule_work_on(cpu, &stock->work);
		}
	}
	migrate_enable();
	mutex_unlock(&percpu_charge_mutex);
}

static int memcg_hotplug_cpu_dead(unsigned int cpu)
{
	struct memcg_stock_pcp *stock;
	struct obj_cgroup *old;
	unsigned long flags;

	stock = &per_cpu(memcg_stock, cpu);

	/* drain_obj_stock requires stock_lock */
	local_lock_irqsave(&memcg_stock.stock_lock, flags);
	old = drain_obj_stock(stock);
	local_unlock_irqrestore(&memcg_stock.stock_lock, flags);

	drain_stock(stock);
	obj_cgroup_put(old);

	return 0;
}

static unsigned long reclaim_high(struct mem_cgroup *memcg,
				  unsigned int nr_pages,
				  gfp_t gfp_mask)
{
	unsigned long nr_reclaimed = 0;

	do {
		unsigned long pflags;

		if (page_counter_read(&memcg->memory) <=
		    READ_ONCE(memcg->memory.high))
			continue;

		memcg_memory_event(memcg, MEMCG_HIGH);

		psi_memstall_enter(&pflags);
		nr_reclaimed += try_to_free_mem_cgroup_pages(memcg, nr_pages,
							gfp_mask,
							MEMCG_RECLAIM_MAY_SWAP,
							NULL);
		psi_memstall_leave(&pflags);
	} while ((memcg = parent_mem_cgroup(memcg)) &&
		 !mem_cgroup_is_root(memcg));

	return nr_reclaimed;
}

static void high_work_func(struct work_struct *work)
{
	struct mem_cgroup *memcg;

	memcg = container_of(work, struct mem_cgroup, high_work);
	reclaim_high(memcg, MEMCG_CHARGE_BATCH, GFP_KERNEL);
}

/*
 * Clamp the maximum sleep time per allocation batch to 2 seconds. This is
 * enough to still cause a significant slowdown in most cases, while still
 * allowing diagnostics and tracing to proceed without becoming stuck.
 */
#define MEMCG_MAX_HIGH_DELAY_JIFFIES (2UL*HZ)

/*
 * When calculating the delay, we use these either side of the exponentiation to
 * maintain precision and scale to a reasonable number of jiffies (see the table
 * below.
 *
 * - MEMCG_DELAY_PRECISION_SHIFT: Extra precision bits while translating the
 *   overage ratio to a delay.
 * - MEMCG_DELAY_SCALING_SHIFT: The number of bits to scale down the
 *   proposed penalty in order to reduce to a reasonable number of jiffies, and
 *   to produce a reasonable delay curve.
 *
 * MEMCG_DELAY_SCALING_SHIFT just happens to be a number that produces a
 * reasonable delay curve compared to precision-adjusted overage, not
 * penalising heavily at first, but still making sure that growth beyond the
 * limit penalises misbehaviour cgroups by slowing them down exponentially. For
 * example, with a high of 100 megabytes:
 *
 *  +-------+------------------------+
 *  | usage | time to allocate in ms |
 *  +-------+------------------------+
 *  | 100M  |                      0 |
 *  | 101M  |                      6 |
 *  | 102M  |                     25 |
 *  | 103M  |                     57 |
 *  | 104M  |                    102 |
 *  | 105M  |                    159 |
 *  | 106M  |                    230 |
 *  | 107M  |                    313 |
 *  | 108M  |                    409 |
 *  | 109M  |                    518 |
 *  | 110M  |                    639 |
 *  | 111M  |                    774 |
 *  | 112M  |                    921 |
 *  | 113M  |                   1081 |
 *  | 114M  |                   1254 |
 *  | 115M  |                   1439 |
 *  | 116M  |                   1638 |
 *  | 117M  |                   1849 |
 *  | 118M  |                   2000 |
 *  | 119M  |                   2000 |
 *  | 120M  |                   2000 |
 *  +-------+------------------------+
 */
 #define MEMCG_DELAY_PRECISION_SHIFT 20
 #define MEMCG_DELAY_SCALING_SHIFT 14

static u64 calculate_overage(unsigned long usage, unsigned long high)
{
	u64 overage;

	if (usage <= high)
		return 0;

	/*
	 * Prevent division by 0 in overage calculation by acting as if
	 * it was a threshold of 1 page
	 */
	high = max(high, 1UL);

	overage = usage - high;
	overage <<= MEMCG_DELAY_PRECISION_SHIFT;
	return div64_u64(overage, high);
}

static u64 mem_find_max_overage(struct mem_cgroup *memcg)
{
	u64 overage, max_overage = 0;

	do {
		overage = calculate_overage(page_counter_read(&memcg->memory),
					    READ_ONCE(memcg->memory.high));
		max_overage = max(overage, max_overage);
	} while ((memcg = parent_mem_cgroup(memcg)) &&
		 !mem_cgroup_is_root(memcg));

	return max_overage;
}

static u64 swap_find_max_overage(struct mem_cgroup *memcg)
{
	u64 overage, max_overage = 0;

	do {
		overage = calculate_overage(page_counter_read(&memcg->swap),
					    READ_ONCE(memcg->swap.high));
		if (overage)
			memcg_memory_event(memcg, MEMCG_SWAP_HIGH);
		max_overage = max(overage, max_overage);
	} while ((memcg = parent_mem_cgroup(memcg)) &&
		 !mem_cgroup_is_root(memcg));

	return max_overage;
}

/*
 * Get the number of jiffies that we should penalise a mischievous cgroup which
 * is exceeding its memory.high by checking both it and its ancestors.
 */
static unsigned long calculate_high_delay(struct mem_cgroup *memcg,
					  unsigned int nr_pages,
					  u64 max_overage)
{
	unsigned long penalty_jiffies;

	if (!max_overage)
		return 0;

	/*
	 * We use overage compared to memory.high to calculate the number of
	 * jiffies to sleep (penalty_jiffies). Ideally this value should be
	 * fairly lenient on small overages, and increasingly harsh when the
	 * memcg in question makes it clear that it has no intention of stopping
	 * its crazy behaviour, so we exponentially increase the delay based on
	 * overage amount.
	 */
	penalty_jiffies = max_overage * max_overage * HZ;
	penalty_jiffies >>= MEMCG_DELAY_PRECISION_SHIFT;
	penalty_jiffies >>= MEMCG_DELAY_SCALING_SHIFT;

	/*
	 * Factor in the task's own contribution to the overage, such that four
	 * N-sized allocations are throttled approximately the same as one
	 * 4N-sized allocation.
	 *
	 * MEMCG_CHARGE_BATCH pages is nominal, so work out how much smaller or
	 * larger the current charge patch is than that.
	 */
	return penalty_jiffies * nr_pages / MEMCG_CHARGE_BATCH;
}

/*
 * Reclaims memory over the high limit. Called directly from
 * try_charge() (context permitting), as well as from the userland
 * return path where reclaim is always able to block.
 */
void mem_cgroup_handle_over_high(gfp_t gfp_mask)
{
	unsigned long penalty_jiffies;
	unsigned long pflags;
	unsigned long nr_reclaimed;
	unsigned int nr_pages = current->memcg_nr_pages_over_high;
	int nr_retries = MAX_RECLAIM_RETRIES;
	struct mem_cgroup *memcg;
	bool in_retry = false;

	if (likely(!nr_pages))
		return;

	memcg = get_mem_cgroup_from_mm(current->mm);
	current->memcg_nr_pages_over_high = 0;

retry_reclaim:
	/*
	 * Bail if the task is already exiting. Unlike memory.max,
	 * memory.high enforcement isn't as strict, and there is no
	 * OOM killer involved, which means the excess could already
	 * be much bigger (and still growing) than it could for
	 * memory.max; the dying task could get stuck in fruitless
	 * reclaim for a long time, which isn't desirable.
	 */
	if (task_is_dying())
		goto out;

	/*
	 * The allocating task should reclaim at least the batch size, but for
	 * subsequent retries we only want to do what's necessary to prevent oom
	 * or breaching resource isolation.
	 *
	 * This is distinct from memory.max or page allocator behaviour because
	 * memory.high is currently batched, whereas memory.max and the page
	 * allocator run every time an allocation is made.
	 */
	nr_reclaimed = reclaim_high(memcg,
				    in_retry ? SWAP_CLUSTER_MAX : nr_pages,
				    gfp_mask);

	/*
	 * memory.high is breached and reclaim is unable to keep up. Throttle
	 * allocators proactively to slow down excessive growth.
	 */
	penalty_jiffies = calculate_high_delay(memcg, nr_pages,
					       mem_find_max_overage(memcg));

	penalty_jiffies += calculate_high_delay(memcg, nr_pages,
						swap_find_max_overage(memcg));

	/*
	 * Clamp the max delay per usermode return so as to still keep the
	 * application moving forwards and also permit diagnostics, albeit
	 * extremely slowly.
	 */
	penalty_jiffies = min(penalty_jiffies, MEMCG_MAX_HIGH_DELAY_JIFFIES);

	/*
	 * Don't sleep if the amount of jiffies this memcg owes us is so low
	 * that it's not even worth doing, in an attempt to be nice to those who
	 * go only a small amount over their memory.high value and maybe haven't
	 * been aggressively reclaimed enough yet.
	 */
	if (penalty_jiffies <= HZ / 100)
		goto out;

	/*
	 * If reclaim is making forward progress but we're still over
	 * memory.high, we want to encourage that rather than doing allocator
	 * throttling.
	 */
	if (nr_reclaimed || nr_retries--) {
		in_retry = true;
		goto retry_reclaim;
	}

	/*
	 * Reclaim didn't manage to push usage below the limit, slow
	 * this allocating task down.
	 *
	 * If we exit early, we're guaranteed to die (since
	 * schedule_timeout_killable sets TASK_KILLABLE). This means we don't
	 * need to account for any ill-begotten jiffies to pay them off later.
	 */
	psi_memstall_enter(&pflags);
	schedule_timeout_killable(penalty_jiffies);
	psi_memstall_leave(&pflags);

out:
	css_put(&memcg->css);
}

int try_charge_memcg(struct mem_cgroup *memcg, gfp_t gfp_mask,
		     unsigned int nr_pages)
{
	unsigned int batch = max(MEMCG_CHARGE_BATCH, nr_pages);
	int nr_retries = MAX_RECLAIM_RETRIES;
	struct mem_cgroup *mem_over_limit;
	struct page_counter *counter;
	unsigned long nr_reclaimed;
	bool passed_oom = false;
	unsigned int reclaim_options = MEMCG_RECLAIM_MAY_SWAP;
	bool drained = false;
	bool raised_max_event = false;
	unsigned long pflags;

retry:
	if (consume_stock(memcg, nr_pages))
		return 0;

	if (!do_memsw_account() ||
	    page_counter_try_charge(&memcg->memsw, batch, &counter)) {
		if (page_counter_try_charge(&memcg->memory, batch, &counter))
			goto done_restock;
		if (do_memsw_account())
			page_counter_uncharge(&memcg->memsw, batch);
		mem_over_limit = mem_cgroup_from_counter(counter, memory);
	} else {
		mem_over_limit = mem_cgroup_from_counter(counter, memsw);
		reclaim_options &= ~MEMCG_RECLAIM_MAY_SWAP;
	}

	if (batch > nr_pages) {
		batch = nr_pages;
		goto retry;
	}

	/*
	 * Prevent unbounded recursion when reclaim operations need to
	 * allocate memory. This might exceed the limits temporarily,
	 * but we prefer facilitating memory reclaim and getting back
	 * under the limit over triggering OOM kills in these cases.
	 */
	if (unlikely(current->flags & PF_MEMALLOC))
		goto force;

	if (unlikely(task_in_memcg_oom(current)))
		goto nomem;

	if (!gfpflags_allow_blocking(gfp_mask))
		goto nomem;

	memcg_memory_event(mem_over_limit, MEMCG_MAX);
	raised_max_event = true;

	psi_memstall_enter(&pflags);
	nr_reclaimed = try_to_free_mem_cgroup_pages(mem_over_limit, nr_pages,
						    gfp_mask, reclaim_options, NULL);
	psi_memstall_leave(&pflags);

	if (mem_cgroup_margin(mem_over_limit) >= nr_pages)
		goto retry;

	if (!drained) {
		drain_all_stock(mem_over_limit);
		drained = true;
		goto retry;
	}

	if (gfp_mask & __GFP_NORETRY)
		goto nomem;
	/*
	 * Even though the limit is exceeded at this point, reclaim
	 * may have been able to free some pages.  Retry the charge
	 * before killing the task.
	 *
	 * Only for regular pages, though: huge pages are rather
	 * unlikely to succeed so close to the limit, and we fall back
	 * to regular pages anyway in case of failure.
	 */
	if (nr_reclaimed && nr_pages <= (1 << PAGE_ALLOC_COSTLY_ORDER))
		goto retry;

	if (nr_retries--)
		goto retry;

	if (gfp_mask & __GFP_RETRY_MAYFAIL)
		goto nomem;

	/* Avoid endless loop for tasks bypassed by the oom killer */
	if (passed_oom && task_is_dying())
		goto nomem;

	/*
	 * keep retrying as long as the memcg oom killer is able to make
	 * a forward progress or bypass the charge if the oom killer
	 * couldn't make any progress.
	 */
	if (mem_cgroup_oom(mem_over_limit, gfp_mask,
			   get_order(nr_pages * PAGE_SIZE))) {
		passed_oom = true;
		nr_retries = MAX_RECLAIM_RETRIES;
		goto retry;
	}
nomem:
	/*
	 * Memcg doesn't have a dedicated reserve for atomic
	 * allocations. But like the global atomic pool, we need to
	 * put the burden of reclaim on regular allocation requests
	 * and let these go through as privileged allocations.
	 */
	if (!(gfp_mask & (__GFP_NOFAIL | __GFP_HIGH)))
		return -ENOMEM;
force:
	/*
	 * If the allocation has to be enforced, don't forget to raise
	 * a MEMCG_MAX event.
	 */
	if (!raised_max_event)
		memcg_memory_event(mem_over_limit, MEMCG_MAX);

	/*
	 * The allocation either can't fail or will lead to more memory
	 * being freed very soon.  Allow memory usage go over the limit
	 * temporarily by force charging it.
	 */
	page_counter_charge(&memcg->memory, nr_pages);
	if (do_memsw_account())
		page_counter_charge(&memcg->memsw, nr_pages);

	return 0;

done_restock:
	if (batch > nr_pages)
		refill_stock(memcg, batch - nr_pages);

	/*
	 * If the hierarchy is above the normal consumption range, schedule
	 * reclaim on returning to userland.  We can perform reclaim here
	 * if __GFP_RECLAIM but let's always punt for simplicity and so that
	 * GFP_KERNEL can consistently be used during reclaim.  @memcg is
	 * not recorded as it most likely matches current's and won't
	 * change in the meantime.  As high limit is checked again before
	 * reclaim, the cost of mismatch is negligible.
	 */
	do {
		bool mem_high, swap_high;

		mem_high = page_counter_read(&memcg->memory) >
			READ_ONCE(memcg->memory.high);
		swap_high = page_counter_read(&memcg->swap) >
			READ_ONCE(memcg->swap.high);

		/* Don't bother a random interrupted task */
		if (!in_task()) {
			if (mem_high) {
				schedule_work(&memcg->high_work);
				break;
			}
			continue;
		}

		if (mem_high || swap_high) {
			/*
			 * The allocating tasks in this cgroup will need to do
			 * reclaim or be throttled to prevent further growth
			 * of the memory or swap footprints.
			 *
			 * Target some best-effort fairness between the tasks,
			 * and distribute reclaim work and delay penalties
			 * based on how much each task is actually allocating.
			 */
			current->memcg_nr_pages_over_high += batch;
			set_notify_resume(current);
			break;
		}
	} while ((memcg = parent_mem_cgroup(memcg)));

	/*
	 * Reclaim is set up above to be called from the userland
	 * return path. But also attempt synchronous reclaim to avoid
	 * excessive overrun while the task is still inside the
	 * kernel. If this is successful, the return path will see it
	 * when it rechecks the overage and simply bail out.
	 */
	if (current->memcg_nr_pages_over_high > MEMCG_CHARGE_BATCH &&
	    !(current->flags & PF_MEMALLOC) &&
	    gfpflags_allow_blocking(gfp_mask))
		mem_cgroup_handle_over_high(gfp_mask);
	return 0;
}

static void commit_charge(struct folio *folio, struct mem_cgroup *memcg)
{
	VM_BUG_ON_FOLIO(folio_memcg_charged(folio), folio);
	/*
	 * Any of the following ensures page's memcg stability:
	 *
	 * - the page lock
	 * - LRU isolation
	 * - exclusive reference
	 */
	folio->memcg_data = (unsigned long)memcg;
}

static inline void __mod_objcg_mlstate(struct obj_cgroup *objcg,
				       struct pglist_data *pgdat,
				       enum node_stat_item idx, int nr)
{
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;

	rcu_read_lock();
	memcg = obj_cgroup_memcg(objcg);
	lruvec = mem_cgroup_lruvec(memcg, pgdat);
	__mod_memcg_lruvec_state(lruvec, idx, nr);
	rcu_read_unlock();
}

static __always_inline
struct mem_cgroup *mem_cgroup_from_obj_folio(struct folio *folio, void *p)
{
	/*
	 * Slab objects are accounted individually, not per-page.
	 * Memcg membership data for each individual object is saved in
	 * slab->obj_exts.
	 */
	if (folio_test_slab(folio)) {
		struct slabobj_ext *obj_exts;
		struct slab *slab;
		unsigned int off;

		slab = folio_slab(folio);
		obj_exts = slab_obj_exts(slab);
		if (!obj_exts)
			return NULL;

		off = obj_to_index(slab->slab_cache, slab, p);
		if (obj_exts[off].objcg)
			return obj_cgroup_memcg(obj_exts[off].objcg);

		return NULL;
	}

	/*
	 * folio_memcg_check() is used here, because in theory we can encounter
	 * a folio where the slab flag has been cleared already, but
	 * slab->obj_exts has not been freed yet
	 * folio_memcg_check() will guarantee that a proper memory
	 * cgroup pointer or NULL will be returned.
	 */
	return folio_memcg_check(folio);
}

/*
 * Returns a pointer to the memory cgroup to which the kernel object is charged.
 * It is not suitable for objects allocated using vmalloc().
 *
 * A passed kernel object must be a slab object or a generic kernel page.
 *
 * The caller must ensure the memcg lifetime, e.g. by taking rcu_read_lock(),
 * cgroup_mutex, etc.
 */
struct mem_cgroup *mem_cgroup_from_slab_obj(void *p)
{
	if (mem_cgroup_disabled())
		return NULL;

	return mem_cgroup_from_obj_folio(virt_to_folio(p), p);
}

static struct obj_cgroup *__get_obj_cgroup_from_memcg(struct mem_cgroup *memcg)
{
	struct obj_cgroup *objcg = NULL;

	for (; !mem_cgroup_is_root(memcg); memcg = parent_mem_cgroup(memcg)) {
		objcg = rcu_dereference(memcg->objcg);
		if (likely(objcg && obj_cgroup_tryget(objcg)))
			break;
		objcg = NULL;
	}
	return objcg;
}

static struct obj_cgroup *current_objcg_update(void)
{
	struct mem_cgroup *memcg;
	struct obj_cgroup *old, *objcg = NULL;

	do {
		/* Atomically drop the update bit. */
		old = xchg(&current->objcg, NULL);
		if (old) {
			old = (struct obj_cgroup *)
				((unsigned long)old & ~CURRENT_OBJCG_UPDATE_FLAG);
			obj_cgroup_put(old);

			old = NULL;
		}

		/* If new objcg is NULL, no reason for the second atomic update. */
		if (!current->mm || (current->flags & PF_KTHREAD))
			return NULL;

		/*
		 * Release the objcg pointer from the previous iteration,
		 * if try_cmpxcg() below fails.
		 */
		if (unlikely(objcg)) {
			obj_cgroup_put(objcg);
			objcg = NULL;
		}

		/*
		 * Obtain the new objcg pointer. The current task can be
		 * asynchronously moved to another memcg and the previous
		 * memcg can be offlined. So let's get the memcg pointer
		 * and try get a reference to objcg under a rcu read lock.
		 */

		rcu_read_lock();
		memcg = mem_cgroup_from_task(current);
		objcg = __get_obj_cgroup_from_memcg(memcg);
		rcu_read_unlock();

		/*
		 * Try set up a new objcg pointer atomically. If it
		 * fails, it means the update flag was set concurrently, so
		 * the whole procedure should be repeated.
		 */
	} while (!try_cmpxchg(&current->objcg, &old, objcg));

	return objcg;
}

__always_inline struct obj_cgroup *current_obj_cgroup(void)
{
	struct mem_cgroup *memcg;
	struct obj_cgroup *objcg;

	if (in_task()) {
		memcg = current->active_memcg;
		if (unlikely(memcg))
			goto from_memcg;

		objcg = READ_ONCE(current->objcg);
		if (unlikely((unsigned long)objcg & CURRENT_OBJCG_UPDATE_FLAG))
			objcg = current_objcg_update();
		/*
		 * Objcg reference is kept by the task, so it's safe
		 * to use the objcg by the current task.
		 */
		return objcg;
	}

	memcg = this_cpu_read(int_active_memcg);
	if (unlikely(memcg))
		goto from_memcg;

	return NULL;

from_memcg:
	objcg = NULL;
	for (; !mem_cgroup_is_root(memcg); memcg = parent_mem_cgroup(memcg)) {
		/*
		 * Memcg pointer is protected by scope (see set_active_memcg())
		 * and is pinning the corresponding objcg, so objcg can't go
		 * away and can be used within the scope without any additional
		 * protection.
		 */
		objcg = rcu_dereference_check(memcg->objcg, 1);
		if (likely(objcg))
			break;
	}

	return objcg;
}

struct obj_cgroup *get_obj_cgroup_from_folio(struct folio *folio)
{
	struct obj_cgroup *objcg;

	if (!memcg_kmem_online())
		return NULL;

	if (folio_memcg_kmem(folio)) {
		objcg = __folio_objcg(folio);
		obj_cgroup_get(objcg);
	} else {
		struct mem_cgroup *memcg;

		rcu_read_lock();
		memcg = __folio_memcg(folio);
		if (memcg)
			objcg = __get_obj_cgroup_from_memcg(memcg);
		else
			objcg = NULL;
		rcu_read_unlock();
	}
	return objcg;
}

/*
 * obj_cgroup_uncharge_pages: uncharge a number of kernel pages from a objcg
 * @objcg: object cgroup to uncharge
 * @nr_pages: number of pages to uncharge
 */
static void obj_cgroup_uncharge_pages(struct obj_cgroup *objcg,
				      unsigned int nr_pages)
{
	struct mem_cgroup *memcg;

	memcg = get_mem_cgroup_from_objcg(objcg);

	mod_memcg_state(memcg, MEMCG_KMEM, -nr_pages);
	memcg1_account_kmem(memcg, -nr_pages);
	refill_stock(memcg, nr_pages);

	css_put(&memcg->css);
}

/*
 * obj_cgroup_charge_pages: charge a number of kernel pages to a objcg
 * @objcg: object cgroup to charge
 * @gfp: reclaim mode
 * @nr_pages: number of pages to charge
 *
 * Returns 0 on success, an error code on failure.
 */
static int obj_cgroup_charge_pages(struct obj_cgroup *objcg, gfp_t gfp,
				   unsigned int nr_pages)
{
	struct mem_cgroup *memcg;
	int ret;

	memcg = get_mem_cgroup_from_objcg(objcg);

	ret = try_charge_memcg(memcg, gfp, nr_pages);
	if (ret)
		goto out;

	mod_memcg_state(memcg, MEMCG_KMEM, nr_pages);
	memcg1_account_kmem(memcg, nr_pages);
out:
	css_put(&memcg->css);

	return ret;
}

/**
 * __memcg_kmem_charge_page: charge a kmem page to the current memory cgroup
 * @page: page to charge
 * @gfp: reclaim mode
 * @order: allocation order
 *
 * Returns 0 on success, an error code on failure.
 */
int __memcg_kmem_charge_page(struct page *page, gfp_t gfp, int order)
{
	struct obj_cgroup *objcg;
	int ret = 0;

	objcg = current_obj_cgroup();
	if (objcg) {
		ret = obj_cgroup_charge_pages(objcg, gfp, 1 << order);
		if (!ret) {
			obj_cgroup_get(objcg);
			page->memcg_data = (unsigned long)objcg |
				MEMCG_DATA_KMEM;
			return 0;
		}
	}
	return ret;
}

/**
 * __memcg_kmem_uncharge_page: uncharge a kmem page
 * @page: page to uncharge
 * @order: allocation order
 */
void __memcg_kmem_uncharge_page(struct page *page, int order)
{
	struct folio *folio = page_folio(page);
	struct obj_cgroup *objcg;
	unsigned int nr_pages = 1 << order;

	if (!folio_memcg_kmem(folio))
		return;

	objcg = __folio_objcg(folio);
	obj_cgroup_uncharge_pages(objcg, nr_pages);
	folio->memcg_data = 0;
	obj_cgroup_put(objcg);
}

/* Replace the stock objcg with objcg, return the old objcg */
static struct obj_cgroup *replace_stock_objcg(struct memcg_stock_pcp *stock,
					     struct obj_cgroup *objcg)
{
	struct obj_cgroup *old = NULL;

	old = drain_obj_stock(stock);
	obj_cgroup_get(objcg);
	stock->nr_bytes = atomic_read(&objcg->nr_charged_bytes)
			? atomic_xchg(&objcg->nr_charged_bytes, 0) : 0;
	WRITE_ONCE(stock->cached_objcg, objcg);
	return old;
}

static void mod_objcg_state(struct obj_cgroup *objcg, struct pglist_data *pgdat,
		     enum node_stat_item idx, int nr)
{
	struct memcg_stock_pcp *stock;
	struct obj_cgroup *old = NULL;
	unsigned long flags;
	int *bytes;

	local_lock_irqsave(&memcg_stock.stock_lock, flags);
	stock = this_cpu_ptr(&memcg_stock);

	/*
	 * Save vmstat data in stock and skip vmstat array update unless
	 * accumulating over a page of vmstat data or when pgdat or idx
	 * changes.
	 */
	if (READ_ONCE(stock->cached_objcg) != objcg) {
		old = replace_stock_objcg(stock, objcg);
		stock->cached_pgdat = pgdat;
	} else if (stock->cached_pgdat != pgdat) {
		/* Flush the existing cached vmstat data */
		struct pglist_data *oldpg = stock->cached_pgdat;

		if (stock->nr_slab_reclaimable_b) {
			__mod_objcg_mlstate(objcg, oldpg, NR_SLAB_RECLAIMABLE_B,
					  stock->nr_slab_reclaimable_b);
			stock->nr_slab_reclaimable_b = 0;
		}
		if (stock->nr_slab_unreclaimable_b) {
			__mod_objcg_mlstate(objcg, oldpg, NR_SLAB_UNRECLAIMABLE_B,
					  stock->nr_slab_unreclaimable_b);
			stock->nr_slab_unreclaimable_b = 0;
		}
		stock->cached_pgdat = pgdat;
	}

	bytes = (idx == NR_SLAB_RECLAIMABLE_B) ? &stock->nr_slab_reclaimable_b
					       : &stock->nr_slab_unreclaimable_b;
	/*
	 * Even for large object >= PAGE_SIZE, the vmstat data will still be
	 * cached locally at least once before pushing it out.
	 */
	if (!*bytes) {
		*bytes = nr;
		nr = 0;
	} else {
		*bytes += nr;
		if (abs(*bytes) > PAGE_SIZE) {
			nr = *bytes;
			*bytes = 0;
		} else {
			nr = 0;
		}
	}
	if (nr)
		__mod_objcg_mlstate(objcg, pgdat, idx, nr);

	local_unlock_irqrestore(&memcg_stock.stock_lock, flags);
	obj_cgroup_put(old);
}

static bool consume_obj_stock(struct obj_cgroup *objcg, unsigned int nr_bytes)
{
	struct memcg_stock_pcp *stock;
	unsigned long flags;
	bool ret = false;

	local_lock_irqsave(&memcg_stock.stock_lock, flags);

	stock = this_cpu_ptr(&memcg_stock);
	if (objcg == READ_ONCE(stock->cached_objcg) && stock->nr_bytes >= nr_bytes) {
		stock->nr_bytes -= nr_bytes;
		ret = true;
	}

	local_unlock_irqrestore(&memcg_stock.stock_lock, flags);

	return ret;
}

static struct obj_cgroup *drain_obj_stock(struct memcg_stock_pcp *stock)
{
	struct obj_cgroup *old = READ_ONCE(stock->cached_objcg);

	if (!old)
		return NULL;

	if (stock->nr_bytes) {
		unsigned int nr_pages = stock->nr_bytes >> PAGE_SHIFT;
		unsigned int nr_bytes = stock->nr_bytes & (PAGE_SIZE - 1);

		if (nr_pages) {
			struct mem_cgroup *memcg;

			memcg = get_mem_cgroup_from_objcg(old);

			mod_memcg_state(memcg, MEMCG_KMEM, -nr_pages);
			memcg1_account_kmem(memcg, -nr_pages);
			__refill_stock(memcg, nr_pages);

			css_put(&memcg->css);
		}

		/*
		 * The leftover is flushed to the centralized per-memcg value.
		 * On the next attempt to refill obj stock it will be moved
		 * to a per-cpu stock (probably, on an other CPU), see
		 * refill_obj_stock().
		 *
		 * How often it's flushed is a trade-off between the memory
		 * limit enforcement accuracy and potential CPU contention,
		 * so it might be changed in the future.
		 */
		atomic_add(nr_bytes, &old->nr_charged_bytes);
		stock->nr_bytes = 0;
	}

	/*
	 * Flush the vmstat data in current stock
	 */
	if (stock->nr_slab_reclaimable_b || stock->nr_slab_unreclaimable_b) {
		if (stock->nr_slab_reclaimable_b) {
			__mod_objcg_mlstate(old, stock->cached_pgdat,
					  NR_SLAB_RECLAIMABLE_B,
					  stock->nr_slab_reclaimable_b);
			stock->nr_slab_reclaimable_b = 0;
		}
		if (stock->nr_slab_unreclaimable_b) {
			__mod_objcg_mlstate(old, stock->cached_pgdat,
					  NR_SLAB_UNRECLAIMABLE_B,
					  stock->nr_slab_unreclaimable_b);
			stock->nr_slab_unreclaimable_b = 0;
		}
		stock->cached_pgdat = NULL;
	}

	WRITE_ONCE(stock->cached_objcg, NULL);
	/*
	 * The `old' objects needs to be released by the caller via
	 * obj_cgroup_put() outside of memcg_stock_pcp::stock_lock.
	 */
	return old;
}

static bool obj_stock_flush_required(struct memcg_stock_pcp *stock,
				     struct mem_cgroup *root_memcg)
{
	struct obj_cgroup *objcg = READ_ONCE(stock->cached_objcg);
	struct mem_cgroup *memcg;

	if (objcg) {
		memcg = obj_cgroup_memcg(objcg);
		if (memcg && mem_cgroup_is_descendant(memcg, root_memcg))
			return true;
	}

	return false;
}

static void refill_obj_stock(struct obj_cgroup *objcg, unsigned int nr_bytes,
			     bool allow_uncharge)
{
	struct memcg_stock_pcp *stock;
	struct obj_cgroup *old = NULL;
	unsigned long flags;
	unsigned int nr_pages = 0;

	local_lock_irqsave(&memcg_stock.stock_lock, flags);

	stock = this_cpu_ptr(&memcg_stock);
	if (READ_ONCE(stock->cached_objcg) != objcg) { /* reset if necessary */
		old = replace_stock_objcg(stock, objcg);
		allow_uncharge = true;	/* Allow uncharge when objcg changes */
	}
	stock->nr_bytes += nr_bytes;

	if (allow_uncharge && (stock->nr_bytes > PAGE_SIZE)) {
		nr_pages = stock->nr_bytes >> PAGE_SHIFT;
		stock->nr_bytes &= (PAGE_SIZE - 1);
	}

	local_unlock_irqrestore(&memcg_stock.stock_lock, flags);
	obj_cgroup_put(old);

	if (nr_pages)
		obj_cgroup_uncharge_pages(objcg, nr_pages);
}

int obj_cgroup_charge(struct obj_cgroup *objcg, gfp_t gfp, size_t size)
{
	unsigned int nr_pages, nr_bytes;
	int ret;

	if (consume_obj_stock(objcg, size))
		return 0;

	/*
	 * In theory, objcg->nr_charged_bytes can have enough
	 * pre-charged bytes to satisfy the allocation. However,
	 * flushing objcg->nr_charged_bytes requires two atomic
	 * operations, and objcg->nr_charged_bytes can't be big.
	 * The shared objcg->nr_charged_bytes can also become a
	 * performance bottleneck if all tasks of the same memcg are
	 * trying to update it. So it's better to ignore it and try
	 * grab some new pages. The stock's nr_bytes will be flushed to
	 * objcg->nr_charged_bytes later on when objcg changes.
	 *
	 * The stock's nr_bytes may contain enough pre-charged bytes
	 * to allow one less page from being charged, but we can't rely
	 * on the pre-charged bytes not being changed outside of
	 * consume_obj_stock() or refill_obj_stock(). So ignore those
	 * pre-charged bytes as well when charging pages. To avoid a
	 * page uncharge right after a page charge, we set the
	 * allow_uncharge flag to false when calling refill_obj_stock()
	 * to temporarily allow the pre-charged bytes to exceed the page
	 * size limit. The maximum reachable value of the pre-charged
	 * bytes is (sizeof(object) + PAGE_SIZE - 2) if there is no data
	 * race.
	 */
	nr_pages = size >> PAGE_SHIFT;
	nr_bytes = size & (PAGE_SIZE - 1);

	if (nr_bytes)
		nr_pages += 1;

	ret = obj_cgroup_charge_pages(objcg, gfp, nr_pages);
	if (!ret && nr_bytes)
		refill_obj_stock(objcg, PAGE_SIZE - nr_bytes, false);

	return ret;
}

void obj_cgroup_uncharge(struct obj_cgroup *objcg, size_t size)
{
	refill_obj_stock(objcg, size, true);
}

static inline size_t obj_full_size(struct kmem_cache *s)
{
	/*
	 * For each accounted object there is an extra space which is used
	 * to store obj_cgroup membership. Charge it too.
	 */
	return s->size + sizeof(struct obj_cgroup *);
}

bool __memcg_slab_post_alloc_hook(struct kmem_cache *s, struct list_lru *lru,
				  gfp_t flags, size_t size, void **p)
{
	struct obj_cgroup *objcg;
	struct slab *slab;
	unsigned long off;
	size_t i;

	/*
	 * The obtained objcg pointer is safe to use within the current scope,
	 * defined by current task or set_active_memcg() pair.
	 * obj_cgroup_get() is used to get a permanent reference.
	 */
	objcg = current_obj_cgroup();
	if (!objcg)
		return true;

	/*
	 * slab_alloc_node() avoids the NULL check, so we might be called with a
	 * single NULL object. kmem_cache_alloc_bulk() aborts if it can't fill
	 * the whole requested size.
	 * return success as there's nothing to free back
	 */
	if (unlikely(*p == NULL))
		return true;

	flags &= gfp_allowed_mask;

	if (lru) {
		int ret;
		struct mem_cgroup *memcg;

		memcg = get_mem_cgroup_from_objcg(objcg);
		ret = memcg_list_lru_alloc(memcg, lru, flags);
		css_put(&memcg->css);

		if (ret)
			return false;
	}

	if (obj_cgroup_charge(objcg, flags, size * obj_full_size(s)))
		return false;

	for (i = 0; i < size; i++) {
		slab = virt_to_slab(p[i]);

		if (!slab_obj_exts(slab) &&
		    alloc_slab_obj_exts(slab, s, flags, false)) {
			obj_cgroup_uncharge(objcg, obj_full_size(s));
			continue;
		}

		off = obj_to_index(s, slab, p[i]);
		obj_cgroup_get(objcg);
		slab_obj_exts(slab)[off].objcg = objcg;
		mod_objcg_state(objcg, slab_pgdat(slab),
				cache_vmstat_idx(s), obj_full_size(s));
	}

	return true;
}

void __memcg_slab_free_hook(struct kmem_cache *s, struct slab *slab,
			    void **p, int objects, struct slabobj_ext *obj_exts)
{
	for (int i = 0; i < objects; i++) {
		struct obj_cgroup *objcg;
		unsigned int off;

		off = obj_to_index(s, slab, p[i]);
		objcg = obj_exts[off].objcg;
		if (!objcg)
			continue;

		obj_exts[off].objcg = NULL;
		obj_cgroup_uncharge(objcg, obj_full_size(s));
		mod_objcg_state(objcg, slab_pgdat(slab), cache_vmstat_idx(s),
				-obj_full_size(s));
		obj_cgroup_put(objcg);
	}
}

/*
 * Because folio_memcg(head) is not set on tails, set it now.
 */
void split_page_memcg(struct page *head, int old_order, int new_order)
{
	struct folio *folio = page_folio(head);
	int i;
	unsigned int old_nr = 1 << old_order;
	unsigned int new_nr = 1 << new_order;

	if (mem_cgroup_disabled() || !folio_memcg_charged(folio))
		return;

	for (i = new_nr; i < old_nr; i += new_nr)
		folio_page(folio, i)->memcg_data = folio->memcg_data;

	if (folio_memcg_kmem(folio))
		obj_cgroup_get_many(__folio_objcg(folio), old_nr / new_nr - 1);
	else
		css_get_many(&folio_memcg(folio)->css, old_nr / new_nr - 1);
}

unsigned long mem_cgroup_usage(struct mem_cgroup *memcg, bool swap)
{
	unsigned long val;

	if (mem_cgroup_is_root(memcg)) {
		/*
		 * Approximate root's usage from global state. This isn't
		 * perfect, but the root usage was always an approximation.
		 */
		val = global_node_page_state(NR_FILE_PAGES) +
			global_node_page_state(NR_ANON_MAPPED);
		if (swap)
			val += total_swap_pages - get_nr_swap_pages();
	} else {
		if (!swap)
			val = page_counter_read(&memcg->memory);
		else
			val = page_counter_read(&memcg->memsw);
	}
	return val;
}

static int memcg_online_kmem(struct mem_cgroup *memcg)
{
	struct obj_cgroup *objcg;

	if (mem_cgroup_kmem_disabled())
		return 0;

	if (unlikely(mem_cgroup_is_root(memcg)))
		return 0;

	objcg = obj_cgroup_alloc();
	if (!objcg)
		return -ENOMEM;

	objcg->memcg = memcg;
	rcu_assign_pointer(memcg->objcg, objcg);
	obj_cgroup_get(objcg);
	memcg->orig_objcg = objcg;

	static_branch_enable(&memcg_kmem_online_key);

	memcg->kmemcg_id = memcg->id.id;

	return 0;
}

static void memcg_offline_kmem(struct mem_cgroup *memcg)
{
	struct mem_cgroup *parent;

	if (mem_cgroup_kmem_disabled())
		return;

	if (unlikely(mem_cgroup_is_root(memcg)))
		return;

	parent = parent_mem_cgroup(memcg);
	if (!parent)
		parent = root_mem_cgroup;

	memcg_reparent_list_lrus(memcg, parent);

	/*
	 * Objcg's reparenting must be after list_lru's, make sure list_lru
	 * helpers won't use parent's list_lru until child is drained.
	 */
	memcg_reparent_objcgs(memcg, parent);
}

#ifdef CONFIG_CGROUP_WRITEBACK

#include <trace/events/writeback.h>

static int memcg_wb_domain_init(struct mem_cgroup *memcg, gfp_t gfp)
{
	return wb_domain_init(&memcg->cgwb_domain, gfp);
}

static void memcg_wb_domain_exit(struct mem_cgroup *memcg)
{
	wb_domain_exit(&memcg->cgwb_domain);
}

static void memcg_wb_domain_size_changed(struct mem_cgroup *memcg)
{
	wb_domain_size_changed(&memcg->cgwb_domain);
}

struct wb_domain *mem_cgroup_wb_domain(struct bdi_writeback *wb)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(wb->memcg_css);

	if (!memcg->css.parent)
		return NULL;

	return &memcg->cgwb_domain;
}

/**
 * mem_cgroup_wb_stats - retrieve writeback related stats from its memcg
 * @wb: bdi_writeback in question
 * @pfilepages: out parameter for number of file pages
 * @pheadroom: out parameter for number of allocatable pages according to memcg
 * @pdirty: out parameter for number of dirty pages
 * @pwriteback: out parameter for number of pages under writeback
 *
 * Determine the numbers of file, headroom, dirty, and writeback pages in
 * @wb's memcg.  File, dirty and writeback are self-explanatory.  Headroom
 * is a bit more involved.
 *
 * A memcg's headroom is "min(max, high) - used".  In the hierarchy, the
 * headroom is calculated as the lowest headroom of itself and the
 * ancestors.  Note that this doesn't consider the actual amount of
 * available memory in the system.  The caller should further cap
 * *@pheadroom accordingly.
 */
void mem_cgroup_wb_stats(struct bdi_writeback *wb, unsigned long *pfilepages,
			 unsigned long *pheadroom, unsigned long *pdirty,
			 unsigned long *pwriteback)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(wb->memcg_css);
	struct mem_cgroup *parent;

	mem_cgroup_flush_stats_ratelimited(memcg);

	*pdirty = memcg_page_state(memcg, NR_FILE_DIRTY);
	*pwriteback = memcg_page_state(memcg, NR_WRITEBACK);
	*pfilepages = memcg_page_state(memcg, NR_INACTIVE_FILE) +
			memcg_page_state(memcg, NR_ACTIVE_FILE);

	*pheadroom = PAGE_COUNTER_MAX;
	while ((parent = parent_mem_cgroup(memcg))) {
		unsigned long ceiling = min(READ_ONCE(memcg->memory.max),
					    READ_ONCE(memcg->memory.high));
		unsigned long used = page_counter_read(&memcg->memory);

		*pheadroom = min(*pheadroom, ceiling - min(ceiling, used));
		memcg = parent;
	}
}

/*
 * Foreign dirty flushing
 *
 * There's an inherent mismatch between memcg and writeback.  The former
 * tracks ownership per-page while the latter per-inode.  This was a
 * deliberate design decision because honoring per-page ownership in the
 * writeback path is complicated, may lead to higher CPU and IO overheads
 * and deemed unnecessary given that write-sharing an inode across
 * different cgroups isn't a common use-case.
 *
 * Combined with inode majority-writer ownership switching, this works well
 * enough in most cases but there are some pathological cases.  For
 * example, let's say there are two cgroups A and B which keep writing to
 * different but confined parts of the same inode.  B owns the inode and
 * A's memory is limited far below B's.  A's dirty ratio can rise enough to
 * trigger balance_dirty_pages() sleeps but B's can be low enough to avoid
 * triggering background writeback.  A will be slowed down without a way to
 * make writeback of the dirty pages happen.
 *
 * Conditions like the above can lead to a cgroup getting repeatedly and
 * severely throttled after making some progress after each
 * dirty_expire_interval while the underlying IO device is almost
 * completely idle.
 *
 * Solving this problem completely requires matching the ownership tracking
 * granularities between memcg and writeback in either direction.  However,
 * the more egregious behaviors can be avoided by simply remembering the
 * most recent foreign dirtying events and initiating remote flushes on
 * them when local writeback isn't enough to keep the memory clean enough.
 *
 * The following two functions implement such mechanism.  When a foreign
 * page - a page whose memcg and writeback ownerships don't match - is
 * dirtied, mem_cgroup_track_foreign_dirty() records the inode owning
 * bdi_writeback on the page owning memcg.  When balance_dirty_pages()
 * decides that the memcg needs to sleep due to high dirty ratio, it calls
 * mem_cgroup_flush_foreign() which queues writeback on the recorded
 * foreign bdi_writebacks which haven't expired.  Both the numbers of
 * recorded bdi_writebacks and concurrent in-flight foreign writebacks are
 * limited to MEMCG_CGWB_FRN_CNT.
 *
 * The mechanism only remembers IDs and doesn't hold any object references.
 * As being wrong occasionally doesn't matter, updates and accesses to the
 * records are lockless and racy.
 */
void mem_cgroup_track_foreign_dirty_slowpath(struct folio *folio,
					     struct bdi_writeback *wb)
{
	struct mem_cgroup *memcg = folio_memcg(folio);
	struct memcg_cgwb_frn *frn;
	u64 now = get_jiffies_64();
	u64 oldest_at = now;
	int oldest = -1;
	int i;

	trace_track_foreign_dirty(folio, wb);

	/*
	 * Pick the slot to use.  If there is already a slot for @wb, keep
	 * using it.  If not replace the oldest one which isn't being
	 * written out.
	 */
	for (i = 0; i < MEMCG_CGWB_FRN_CNT; i++) {
		frn = &memcg->cgwb_frn[i];
		if (frn->bdi_id == wb->bdi->id &&
		    frn->memcg_id == wb->memcg_css->id)
			break;
		if (time_before64(frn->at, oldest_at) &&
		    atomic_read(&frn->done.cnt) == 1) {
			oldest = i;
			oldest_at = frn->at;
		}
	}

	if (i < MEMCG_CGWB_FRN_CNT) {
		/*
		 * Re-using an existing one.  Update timestamp lazily to
		 * avoid making the cacheline hot.  We want them to be
		 * reasonably up-to-date and significantly shorter than
		 * dirty_expire_interval as that's what expires the record.
		 * Use the shorter of 1s and dirty_expire_interval / 8.
		 */
		unsigned long update_intv =
			min_t(unsigned long, HZ,
			      msecs_to_jiffies(dirty_expire_interval * 10) / 8);

		if (time_before64(frn->at, now - update_intv))
			frn->at = now;
	} else if (oldest >= 0) {
		/* replace the oldest free one */
		frn = &memcg->cgwb_frn[oldest];
		frn->bdi_id = wb->bdi->id;
		frn->memcg_id = wb->memcg_css->id;
		frn->at = now;
	}
}

/* issue foreign writeback flushes for recorded foreign dirtying events */
void mem_cgroup_flush_foreign(struct bdi_writeback *wb)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(wb->memcg_css);
	unsigned long intv = msecs_to_jiffies(dirty_expire_interval * 10);
	u64 now = jiffies_64;
	int i;

	for (i = 0; i < MEMCG_CGWB_FRN_CNT; i++) {
		struct memcg_cgwb_frn *frn = &memcg->cgwb_frn[i];

		/*
		 * If the record is older than dirty_expire_interval,
		 * writeback on it has already started.  No need to kick it
		 * off again.  Also, don't start a new one if there's
		 * already one in flight.
		 */
		if (time_after64(frn->at, now - intv) &&
		    atomic_read(&frn->done.cnt) == 1) {
			frn->at = 0;
			trace_flush_foreign(wb, frn->bdi_id, frn->memcg_id);
			cgroup_writeback_by_id(frn->bdi_id, frn->memcg_id,
					       WB_REASON_FOREIGN_FLUSH,
					       &frn->done);
		}
	}
}

#else	/* CONFIG_CGROUP_WRITEBACK */

static int memcg_wb_domain_init(struct mem_cgroup *memcg, gfp_t gfp)
{
	return 0;
}

static void memcg_wb_domain_exit(struct mem_cgroup *memcg)
{
}

static void memcg_wb_domain_size_changed(struct mem_cgroup *memcg)
{
}

#endif	/* CONFIG_CGROUP_WRITEBACK */

/*
 * Private memory cgroup IDR
 *
 * Swap-out records and page cache shadow entries need to store memcg
 * references in constrained space, so we maintain an ID space that is
 * limited to 16 bit (MEM_CGROUP_ID_MAX), limiting the total number of
 * memory-controlled cgroups to 64k.
 *
 * However, there usually are many references to the offline CSS after
 * the cgroup has been destroyed, such as page cache or reclaimable
 * slab objects, that don't need to hang on to the ID. We want to keep
 * those dead CSS from occupying IDs, or we might quickly exhaust the
 * relatively small ID space and prevent the creation of new cgroups
 * even when there are much fewer than 64k cgroups - possibly none.
 *
 * Maintain a private 16-bit ID space for memcg, and allow the ID to
 * be freed and recycled when it's no longer needed, which is usually
 * when the CSS is offlined.
 *
 * The only exception to that are records of swapped out tmpfs/shmem
 * pages that need to be attributed to live ancestors on swapin. But
 * those references are manageable from userspace.
 */

#define MEM_CGROUP_ID_MAX	((1UL << MEM_CGROUP_ID_SHIFT) - 1)
static DEFINE_XARRAY_ALLOC1(mem_cgroup_ids);

static void mem_cgroup_id_remove(struct mem_cgroup *memcg)
{
	if (memcg->id.id > 0) {
		xa_erase(&mem_cgroup_ids, memcg->id.id);
		memcg->id.id = 0;
	}
}

void __maybe_unused mem_cgroup_id_get_many(struct mem_cgroup *memcg,
					   unsigned int n)
{
	refcount_add(n, &memcg->id.ref);
}

void mem_cgroup_id_put_many(struct mem_cgroup *memcg, unsigned int n)
{
	if (refcount_sub_and_test(n, &memcg->id.ref)) {
		mem_cgroup_id_remove(memcg);

		/* Memcg ID pins CSS */
		css_put(&memcg->css);
	}
}

static inline void mem_cgroup_id_put(struct mem_cgroup *memcg)
{
	mem_cgroup_id_put_many(memcg, 1);
}

/**
 * mem_cgroup_from_id - look up a memcg from a memcg id
 * @id: the memcg id to look up
 *
 * Caller must hold rcu_read_lock().
 */
struct mem_cgroup *mem_cgroup_from_id(unsigned short id)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return xa_load(&mem_cgroup_ids, id);
}

#ifdef CONFIG_SHRINKER_DEBUG
struct mem_cgroup *mem_cgroup_get_from_ino(unsigned long ino)
{
	struct cgroup *cgrp;
	struct cgroup_subsys_state *css;
	struct mem_cgroup *memcg;

	cgrp = cgroup_get_from_id(ino);
	if (IS_ERR(cgrp))
		return ERR_CAST(cgrp);

	css = cgroup_get_e_css(cgrp, &memory_cgrp_subsys);
	if (css)
		memcg = container_of(css, struct mem_cgroup, css);
	else
		memcg = ERR_PTR(-ENOENT);

	cgroup_put(cgrp);

	return memcg;
}
#endif

static void free_mem_cgroup_per_node_info(struct mem_cgroup_per_node *pn)
{
	if (!pn)
		return;

	free_percpu(pn->lruvec_stats_percpu);
	kfree(pn->lruvec_stats);
	kfree(pn);
}

static bool alloc_mem_cgroup_per_node_info(struct mem_cgroup *memcg, int node)
{
	struct mem_cgroup_per_node *pn;

	pn = kzalloc_node(sizeof(*pn), GFP_KERNEL, node);
	if (!pn)
		return false;

	pn->lruvec_stats = kzalloc_node(sizeof(struct lruvec_stats),
					GFP_KERNEL_ACCOUNT, node);
	if (!pn->lruvec_stats)
		goto fail;

	pn->lruvec_stats_percpu = alloc_percpu_gfp(struct lruvec_stats_percpu,
						   GFP_KERNEL_ACCOUNT);
	if (!pn->lruvec_stats_percpu)
		goto fail;

	lruvec_init(&pn->lruvec);
	pn->memcg = memcg;

	memcg->nodeinfo[node] = pn;
	return true;
fail:
	free_mem_cgroup_per_node_info(pn);
	return false;
}

static void __mem_cgroup_free(struct mem_cgroup *memcg)
{
	int node;

	obj_cgroup_put(memcg->orig_objcg);

	for_each_node(node)
		free_mem_cgroup_per_node_info(memcg->nodeinfo[node]);
	memcg1_free_events(memcg);
	kfree(memcg->vmstats);
	free_percpu(memcg->vmstats_percpu);
	kfree(memcg);
}

static void mem_cgroup_free(struct mem_cgroup *memcg)
{
	lru_gen_exit_memcg(memcg);
	memcg_wb_domain_exit(memcg);
	__mem_cgroup_free(memcg);
}

static struct mem_cgroup *mem_cgroup_alloc(struct mem_cgroup *parent)
{
	struct memcg_vmstats_percpu *statc, *pstatc;
	struct mem_cgroup *memcg;
	int node, cpu;
	int __maybe_unused i;
	long error;

	memcg = kzalloc(struct_size(memcg, nodeinfo, nr_node_ids), GFP_KERNEL);
	if (!memcg)
		return ERR_PTR(-ENOMEM);

	error = xa_alloc(&mem_cgroup_ids, &memcg->id.id, NULL,
			 XA_LIMIT(1, MEM_CGROUP_ID_MAX), GFP_KERNEL);
	if (error)
		goto fail;
	error = -ENOMEM;

	memcg->vmstats = kzalloc(sizeof(struct memcg_vmstats),
				 GFP_KERNEL_ACCOUNT);
	if (!memcg->vmstats)
		goto fail;

	memcg->vmstats_percpu = alloc_percpu_gfp(struct memcg_vmstats_percpu,
						 GFP_KERNEL_ACCOUNT);
	if (!memcg->vmstats_percpu)
		goto fail;

	if (!memcg1_alloc_events(memcg))
		goto fail;

	for_each_possible_cpu(cpu) {
		if (parent)
			pstatc = per_cpu_ptr(parent->vmstats_percpu, cpu);
		statc = per_cpu_ptr(memcg->vmstats_percpu, cpu);
		statc->parent = parent ? pstatc : NULL;
		statc->vmstats = memcg->vmstats;
	}

	for_each_node(node)
		if (!alloc_mem_cgroup_per_node_info(memcg, node))
			goto fail;

	if (memcg_wb_domain_init(memcg, GFP_KERNEL))
		goto fail;

	INIT_WORK(&memcg->high_work, high_work_func);
	vmpressure_init(&memcg->vmpressure);
	INIT_LIST_HEAD(&memcg->memory_peaks);
	INIT_LIST_HEAD(&memcg->swap_peaks);
	spin_lock_init(&memcg->peaks_lock);
	memcg->socket_pressure = jiffies;
	memcg1_memcg_init(memcg);
	memcg->kmemcg_id = -1;
	INIT_LIST_HEAD(&memcg->objcg_list);
#ifdef CONFIG_CGROUP_WRITEBACK
	INIT_LIST_HEAD(&memcg->cgwb_list);
	for (i = 0; i < MEMCG_CGWB_FRN_CNT; i++)
		memcg->cgwb_frn[i].done =
			__WB_COMPLETION_INIT(&memcg_cgwb_frn_waitq);
#endif
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	spin_lock_init(&memcg->deferred_split_queue.split_queue_lock);
	INIT_LIST_HEAD(&memcg->deferred_split_queue.split_queue);
	memcg->deferred_split_queue.split_queue_len = 0;
#endif
	lru_gen_init_memcg(memcg);
	return memcg;
fail:
	mem_cgroup_id_remove(memcg);
	__mem_cgroup_free(memcg);
	return ERR_PTR(error);
}

static struct cgroup_subsys_state * __ref
mem_cgroup_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct mem_cgroup *parent = mem_cgroup_from_css(parent_css);
	struct mem_cgroup *memcg, *old_memcg;

	old_memcg = set_active_memcg(parent);
	memcg = mem_cgroup_alloc(parent);
	set_active_memcg(old_memcg);
	if (IS_ERR(memcg))
		return ERR_CAST(memcg);

	page_counter_set_high(&memcg->memory, PAGE_COUNTER_MAX);
	memcg1_soft_limit_reset(memcg);
#ifdef CONFIG_ZSWAP
	memcg->zswap_max = PAGE_COUNTER_MAX;
	WRITE_ONCE(memcg->zswap_writeback, true);
#endif
	page_counter_set_high(&memcg->swap, PAGE_COUNTER_MAX);
	if (parent) {
		WRITE_ONCE(memcg->swappiness, mem_cgroup_swappiness(parent));

		page_counter_init(&memcg->memory, &parent->memory, true);
		page_counter_init(&memcg->swap, &parent->swap, false);
#ifdef CONFIG_MEMCG_V1
		WRITE_ONCE(memcg->oom_kill_disable, READ_ONCE(parent->oom_kill_disable));
		page_counter_init(&memcg->kmem, &parent->kmem, false);
		page_counter_init(&memcg->tcpmem, &parent->tcpmem, false);
#endif
	} else {
		init_memcg_stats();
		init_memcg_events();
		page_counter_init(&memcg->memory, NULL, true);
		page_counter_init(&memcg->swap, NULL, false);
#ifdef CONFIG_MEMCG_V1
		page_counter_init(&memcg->kmem, NULL, false);
		page_counter_init(&memcg->tcpmem, NULL, false);
#endif
		root_mem_cgroup = memcg;
		return &memcg->css;
	}

	if (cgroup_subsys_on_dfl(memory_cgrp_subsys) && !cgroup_memory_nosocket)
		static_branch_inc(&memcg_sockets_enabled_key);

	if (!cgroup_memory_nobpf)
		static_branch_inc(&memcg_bpf_enabled_key);

	return &memcg->css;
}

static int mem_cgroup_css_online(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (memcg_online_kmem(memcg))
		goto remove_id;

	/*
	 * A memcg must be visible for expand_shrinker_info()
	 * by the time the maps are allocated. So, we allocate maps
	 * here, when for_each_mem_cgroup() can't skip it.
	 */
	if (alloc_shrinker_info(memcg))
		goto offline_kmem;

	if (unlikely(mem_cgroup_is_root(memcg)) && !mem_cgroup_disabled())
		queue_delayed_work(system_unbound_wq, &stats_flush_dwork,
				   FLUSH_TIME);
	lru_gen_online_memcg(memcg);

	/* Online state pins memcg ID, memcg ID pins CSS */
	refcount_set(&memcg->id.ref, 1);
	css_get(css);

	/*
	 * Ensure mem_cgroup_from_id() works once we're fully online.
	 *
	 * We could do this earlier and require callers to filter with
	 * css_tryget_online(). But right now there are no users that
	 * need earlier access, and the workingset code relies on the
	 * cgroup tree linkage (mem_cgroup_get_nr_swap_pages()). So
	 * publish it here at the end of onlining. This matches the
	 * regular ID destruction during offlining.
	 */
	xa_store(&mem_cgroup_ids, memcg->id.id, memcg, GFP_KERNEL);

	return 0;
offline_kmem:
	memcg_offline_kmem(memcg);
remove_id:
	mem_cgroup_id_remove(memcg);
	return -ENOMEM;
}

static void mem_cgroup_css_offline(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	memcg1_css_offline(memcg);

	page_counter_set_min(&memcg->memory, 0);
	page_counter_set_low(&memcg->memory, 0);

	zswap_memcg_offline_cleanup(memcg);

	memcg_offline_kmem(memcg);
	reparent_shrinker_deferred(memcg);
	wb_memcg_offline(memcg);
	lru_gen_offline_memcg(memcg);

	drain_all_stock(memcg);

	mem_cgroup_id_put(memcg);
}

static void mem_cgroup_css_released(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	invalidate_reclaim_iterators(memcg);
	lru_gen_release_memcg(memcg);
}

static void mem_cgroup_css_free(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	int __maybe_unused i;

#ifdef CONFIG_CGROUP_WRITEBACK
	for (i = 0; i < MEMCG_CGWB_FRN_CNT; i++)
		wb_wait_for_completion(&memcg->cgwb_frn[i].done);
#endif
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys) && !cgroup_memory_nosocket)
		static_branch_dec(&memcg_sockets_enabled_key);

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && memcg1_tcpmem_active(memcg))
		static_branch_dec(&memcg_sockets_enabled_key);

	if (!cgroup_memory_nobpf)
		static_branch_dec(&memcg_bpf_enabled_key);

	vmpressure_cleanup(&memcg->vmpressure);
	cancel_work_sync(&memcg->high_work);
	memcg1_remove_from_trees(memcg);
	free_shrinker_info(memcg);
	mem_cgroup_free(memcg);
}

/**
 * mem_cgroup_css_reset - reset the states of a mem_cgroup
 * @css: the target css
 *
 * Reset the states of the mem_cgroup associated with @css.  This is
 * invoked when the userland requests disabling on the default hierarchy
 * but the memcg is pinned through dependency.  The memcg should stop
 * applying policies and should revert to the vanilla state as it may be
 * made visible again.
 *
 * The current implementation only resets the essential configurations.
 * This needs to be expanded to cover all the visible parts.
 */
static void mem_cgroup_css_reset(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	page_counter_set_max(&memcg->memory, PAGE_COUNTER_MAX);
	page_counter_set_max(&memcg->swap, PAGE_COUNTER_MAX);
#ifdef CONFIG_MEMCG_V1
	page_counter_set_max(&memcg->kmem, PAGE_COUNTER_MAX);
	page_counter_set_max(&memcg->tcpmem, PAGE_COUNTER_MAX);
#endif
	page_counter_set_min(&memcg->memory, 0);
	page_counter_set_low(&memcg->memory, 0);
	page_counter_set_high(&memcg->memory, PAGE_COUNTER_MAX);
	memcg1_soft_limit_reset(memcg);
	page_counter_set_high(&memcg->swap, PAGE_COUNTER_MAX);
	memcg_wb_domain_size_changed(memcg);
}

struct aggregate_control {
	/* pointer to the aggregated (CPU and subtree aggregated) counters */
	long *aggregate;
	/* pointer to the non-hierarchichal (CPU aggregated) counters */
	long *local;
	/* pointer to the pending child counters during tree propagation */
	long *pending;
	/* pointer to the parent's pending counters, could be NULL */
	long *ppending;
	/* pointer to the percpu counters to be aggregated */
	long *cstat;
	/* pointer to the percpu counters of the last aggregation*/
	long *cstat_prev;
	/* size of the above counters */
	int size;
};

static void mem_cgroup_stat_aggregate(struct aggregate_control *ac)
{
	int i;
	long delta, delta_cpu, v;

	for (i = 0; i < ac->size; i++) {
		/*
		 * Collect the aggregated propagation counts of groups
		 * below us. We're in a per-cpu loop here and this is
		 * a global counter, so the first cycle will get them.
		 */
		delta = ac->pending[i];
		if (delta)
			ac->pending[i] = 0;

		/* Add CPU changes on this level since the last flush */
		delta_cpu = 0;
		v = READ_ONCE(ac->cstat[i]);
		if (v != ac->cstat_prev[i]) {
			delta_cpu = v - ac->cstat_prev[i];
			delta += delta_cpu;
			ac->cstat_prev[i] = v;
		}

		/* Aggregate counts on this level and propagate upwards */
		if (delta_cpu)
			ac->local[i] += delta_cpu;

		if (delta) {
			ac->aggregate[i] += delta;
			if (ac->ppending)
				ac->ppending[i] += delta;
		}
	}
}

static void mem_cgroup_css_rstat_flush(struct cgroup_subsys_state *css, int cpu)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct mem_cgroup *parent = parent_mem_cgroup(memcg);
	struct memcg_vmstats_percpu *statc;
	struct aggregate_control ac;
	int nid;

	statc = per_cpu_ptr(memcg->vmstats_percpu, cpu);

	ac = (struct aggregate_control) {
		.aggregate = memcg->vmstats->state,
		.local = memcg->vmstats->state_local,
		.pending = memcg->vmstats->state_pending,
		.ppending = parent ? parent->vmstats->state_pending : NULL,
		.cstat = statc->state,
		.cstat_prev = statc->state_prev,
		.size = MEMCG_VMSTAT_SIZE,
	};
	mem_cgroup_stat_aggregate(&ac);

	ac = (struct aggregate_control) {
		.aggregate = memcg->vmstats->events,
		.local = memcg->vmstats->events_local,
		.pending = memcg->vmstats->events_pending,
		.ppending = parent ? parent->vmstats->events_pending : NULL,
		.cstat = statc->events,
		.cstat_prev = statc->events_prev,
		.size = NR_MEMCG_EVENTS,
	};
	mem_cgroup_stat_aggregate(&ac);

	for_each_node_state(nid, N_MEMORY) {
		struct mem_cgroup_per_node *pn = memcg->nodeinfo[nid];
		struct lruvec_stats *lstats = pn->lruvec_stats;
		struct lruvec_stats *plstats = NULL;
		struct lruvec_stats_percpu *lstatc;

		if (parent)
			plstats = parent->nodeinfo[nid]->lruvec_stats;

		lstatc = per_cpu_ptr(pn->lruvec_stats_percpu, cpu);

		ac = (struct aggregate_control) {
			.aggregate = lstats->state,
			.local = lstats->state_local,
			.pending = lstats->state_pending,
			.ppending = plstats ? plstats->state_pending : NULL,
			.cstat = lstatc->state,
			.cstat_prev = lstatc->state_prev,
			.size = NR_MEMCG_NODE_STAT_ITEMS,
		};
		mem_cgroup_stat_aggregate(&ac);

	}
	WRITE_ONCE(statc->stats_updates, 0);
	/* We are in a per-cpu loop here, only do the atomic write once */
	if (atomic64_read(&memcg->vmstats->stats_updates))
		atomic64_set(&memcg->vmstats->stats_updates, 0);
}

static void mem_cgroup_fork(struct task_struct *task)
{
	/*
	 * Set the update flag to cause task->objcg to be initialized lazily
	 * on the first allocation. It can be done without any synchronization
	 * because it's always performed on the current task, so does
	 * current_objcg_update().
	 */
	task->objcg = (struct obj_cgroup *)CURRENT_OBJCG_UPDATE_FLAG;
}

static void mem_cgroup_exit(struct task_struct *task)
{
	struct obj_cgroup *objcg = task->objcg;

	objcg = (struct obj_cgroup *)
		((unsigned long)objcg & ~CURRENT_OBJCG_UPDATE_FLAG);
	obj_cgroup_put(objcg);

	/*
	 * Some kernel allocations can happen after this point,
	 * but let's ignore them. It can be done without any synchronization
	 * because it's always performed on the current task, so does
	 * current_objcg_update().
	 */
	task->objcg = NULL;
}

#ifdef CONFIG_LRU_GEN
static void mem_cgroup_lru_gen_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct cgroup_subsys_state *css;

	/* find the first leader if there is any */
	cgroup_taskset_for_each_leader(task, css, tset)
		break;

	if (!task)
		return;

	task_lock(task);
	if (task->mm && READ_ONCE(task->mm->owner) == task)
		lru_gen_migrate_mm(task->mm);
	task_unlock(task);
}
#else
static void mem_cgroup_lru_gen_attach(struct cgroup_taskset *tset) {}
#endif /* CONFIG_LRU_GEN */

static void mem_cgroup_kmem_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct cgroup_subsys_state *css;

	cgroup_taskset_for_each(task, css, tset) {
		/* atomically set the update bit */
		set_bit(CURRENT_OBJCG_UPDATE_BIT, (unsigned long *)&task->objcg);
	}
}

static void mem_cgroup_attach(struct cgroup_taskset *tset)
{
	mem_cgroup_lru_gen_attach(tset);
	mem_cgroup_kmem_attach(tset);
}

static int seq_puts_memcg_tunable(struct seq_file *m, unsigned long value)
{
	if (value == PAGE_COUNTER_MAX)
		seq_puts(m, "max\n");
	else
		seq_printf(m, "%llu\n", (u64)value * PAGE_SIZE);

	return 0;
}

static u64 memory_current_read(struct cgroup_subsys_state *css,
			       struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	return (u64)page_counter_read(&memcg->memory) * PAGE_SIZE;
}

#define OFP_PEAK_UNSET (((-1UL)))

static int peak_show(struct seq_file *sf, void *v, struct page_counter *pc)
{
	struct cgroup_of_peak *ofp = of_peak(sf->private);
	u64 fd_peak = READ_ONCE(ofp->value), peak;

	/* User wants global or local peak? */
	if (fd_peak == OFP_PEAK_UNSET)
		peak = pc->watermark;
	else
		peak = max(fd_peak, READ_ONCE(pc->local_watermark));

	seq_printf(sf, "%llu\n", peak * PAGE_SIZE);
	return 0;
}

static int memory_peak_show(struct seq_file *sf, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(sf));

	return peak_show(sf, v, &memcg->memory);
}

static int peak_open(struct kernfs_open_file *of)
{
	struct cgroup_of_peak *ofp = of_peak(of);

	ofp->value = OFP_PEAK_UNSET;
	return 0;
}

static void peak_release(struct kernfs_open_file *of)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	struct cgroup_of_peak *ofp = of_peak(of);

	if (ofp->value == OFP_PEAK_UNSET) {
		/* fast path (no writes on this fd) */
		return;
	}
	spin_lock(&memcg->peaks_lock);
	list_del(&ofp->list);
	spin_unlock(&memcg->peaks_lock);
}

static ssize_t peak_write(struct kernfs_open_file *of, char *buf, size_t nbytes,
			  loff_t off, struct page_counter *pc,
			  struct list_head *watchers)
{
	unsigned long usage;
	struct cgroup_of_peak *peer_ctx;
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	struct cgroup_of_peak *ofp = of_peak(of);

	spin_lock(&memcg->peaks_lock);

	usage = page_counter_read(pc);
	WRITE_ONCE(pc->local_watermark, usage);

	list_for_each_entry(peer_ctx, watchers, list)
		if (usage > peer_ctx->value)
			WRITE_ONCE(peer_ctx->value, usage);

	/* initial write, register watcher */
	if (ofp->value == OFP_PEAK_UNSET)
		list_add(&ofp->list, watchers);

	WRITE_ONCE(ofp->value, usage);
	spin_unlock(&memcg->peaks_lock);

	return nbytes;
}

static ssize_t memory_peak_write(struct kernfs_open_file *of, char *buf,
				 size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));

	return peak_write(of, buf, nbytes, off, &memcg->memory,
			  &memcg->memory_peaks);
}

#undef OFP_PEAK_UNSET

static int memory_min_show(struct seq_file *m, void *v)
{
	return seq_puts_memcg_tunable(m,
		READ_ONCE(mem_cgroup_from_seq(m)->memory.min));
}

static ssize_t memory_min_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long min;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &min);
	if (err)
		return err;

	page_counter_set_min(&memcg->memory, min);

	return nbytes;
}

static int memory_low_show(struct seq_file *m, void *v)
{
	return seq_puts_memcg_tunable(m,
		READ_ONCE(mem_cgroup_from_seq(m)->memory.low));
}

static ssize_t memory_low_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long low;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &low);
	if (err)
		return err;

	page_counter_set_low(&memcg->memory, low);

	return nbytes;
}

static int memory_high_show(struct seq_file *m, void *v)
{
	return seq_puts_memcg_tunable(m,
		READ_ONCE(mem_cgroup_from_seq(m)->memory.high));
}

static ssize_t memory_high_write(struct kernfs_open_file *of,
				 char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned int nr_retries = MAX_RECLAIM_RETRIES;
	bool drained = false;
	unsigned long high;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &high);
	if (err)
		return err;

	page_counter_set_high(&memcg->memory, high);

	for (;;) {
		unsigned long nr_pages = page_counter_read(&memcg->memory);
		unsigned long reclaimed;

		if (nr_pages <= high)
			break;

		if (signal_pending(current))
			break;

		if (!drained) {
			drain_all_stock(memcg);
			drained = true;
			continue;
		}

		reclaimed = try_to_free_mem_cgroup_pages(memcg, nr_pages - high,
					GFP_KERNEL, MEMCG_RECLAIM_MAY_SWAP, NULL);

		if (!reclaimed && !nr_retries--)
			break;
	}

	memcg_wb_domain_size_changed(memcg);
	return nbytes;
}

static int memory_max_show(struct seq_file *m, void *v)
{
	return seq_puts_memcg_tunable(m,
		READ_ONCE(mem_cgroup_from_seq(m)->memory.max));
}

static ssize_t memory_max_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned int nr_reclaims = MAX_RECLAIM_RETRIES;
	bool drained = false;
	unsigned long max;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &max);
	if (err)
		return err;

	xchg(&memcg->memory.max, max);

	for (;;) {
		unsigned long nr_pages = page_counter_read(&memcg->memory);

		if (nr_pages <= max)
			break;

		if (signal_pending(current))
			break;

		if (!drained) {
			drain_all_stock(memcg);
			drained = true;
			continue;
		}

		if (nr_reclaims) {
			if (!try_to_free_mem_cgroup_pages(memcg, nr_pages - max,
					GFP_KERNEL, MEMCG_RECLAIM_MAY_SWAP, NULL))
				nr_reclaims--;
			continue;
		}

		memcg_memory_event(memcg, MEMCG_OOM);
		if (!mem_cgroup_out_of_memory(memcg, GFP_KERNEL, 0))
			break;
		cond_resched();
	}

	memcg_wb_domain_size_changed(memcg);
	return nbytes;
}

/*
 * Note: don't forget to update the 'samples/cgroup/memcg_event_listener'
 * if any new events become available.
 */
static void __memory_events_show(struct seq_file *m, atomic_long_t *events)
{
	seq_printf(m, "low %lu\n", atomic_long_read(&events[MEMCG_LOW]));
	seq_printf(m, "high %lu\n", atomic_long_read(&events[MEMCG_HIGH]));
	seq_printf(m, "max %lu\n", atomic_long_read(&events[MEMCG_MAX]));
	seq_printf(m, "oom %lu\n", atomic_long_read(&events[MEMCG_OOM]));
	seq_printf(m, "oom_kill %lu\n",
		   atomic_long_read(&events[MEMCG_OOM_KILL]));
	seq_printf(m, "oom_group_kill %lu\n",
		   atomic_long_read(&events[MEMCG_OOM_GROUP_KILL]));
}

static int memory_events_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	__memory_events_show(m, memcg->memory_events);
	return 0;
}

static int memory_events_local_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	__memory_events_show(m, memcg->memory_events_local);
	return 0;
}

int memory_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);
	char *buf = kmalloc(SEQ_BUF_SIZE, GFP_KERNEL);
	struct seq_buf s;

	if (!buf)
		return -ENOMEM;
	seq_buf_init(&s, buf, SEQ_BUF_SIZE);
	memory_stat_format(memcg, &s);
	seq_puts(m, buf);
	kfree(buf);
	return 0;
}

#ifdef CONFIG_NUMA
static inline unsigned long lruvec_page_state_output(struct lruvec *lruvec,
						     int item)
{
	return lruvec_page_state(lruvec, item) *
		memcg_page_state_output_unit(item);
}

static int memory_numa_stat_show(struct seq_file *m, void *v)
{
	int i;
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	mem_cgroup_flush_stats(memcg);

	for (i = 0; i < ARRAY_SIZE(memory_stats); i++) {
		int nid;

		if (memory_stats[i].idx >= NR_VM_NODE_STAT_ITEMS)
			continue;

		seq_printf(m, "%s", memory_stats[i].name);
		for_each_node_state(nid, N_MEMORY) {
			u64 size;
			struct lruvec *lruvec;

			lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(nid));
			size = lruvec_page_state_output(lruvec,
							memory_stats[i].idx);
			seq_printf(m, " N%d=%llu", nid, size);
		}
		seq_putc(m, '\n');
	}

	return 0;
}
#endif

static int memory_oom_group_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	seq_printf(m, "%d\n", READ_ONCE(memcg->oom_group));

	return 0;
}

static ssize_t memory_oom_group_write(struct kernfs_open_file *of,
				      char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	int ret, oom_group;

	buf = strstrip(buf);
	if (!buf)
		return -EINVAL;

	ret = kstrtoint(buf, 0, &oom_group);
	if (ret)
		return ret;

	if (oom_group != 0 && oom_group != 1)
		return -EINVAL;

	WRITE_ONCE(memcg->oom_group, oom_group);

	return nbytes;
}

enum {
	MEMORY_RECLAIM_SWAPPINESS = 0,
	MEMORY_RECLAIM_NULL,
};

static const match_table_t tokens = {
	{ MEMORY_RECLAIM_SWAPPINESS, "swappiness=%d"},
	{ MEMORY_RECLAIM_NULL, NULL },
};

static ssize_t memory_reclaim(struct kernfs_open_file *of, char *buf,
			      size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned int nr_retries = MAX_RECLAIM_RETRIES;
	unsigned long nr_to_reclaim, nr_reclaimed = 0;
	int swappiness = -1;
	unsigned int reclaim_options;
	char *old_buf, *start;
	substring_t args[MAX_OPT_ARGS];

	buf = strstrip(buf);

	old_buf = buf;
	nr_to_reclaim = memparse(buf, &buf) / PAGE_SIZE;
	if (buf == old_buf)
		return -EINVAL;

	buf = strstrip(buf);

	while ((start = strsep(&buf, " ")) != NULL) {
		if (!strlen(start))
			continue;
		switch (match_token(start, tokens, args)) {
		case MEMORY_RECLAIM_SWAPPINESS:
			if (match_int(&args[0], &swappiness))
				return -EINVAL;
			if (swappiness < MIN_SWAPPINESS || swappiness > MAX_SWAPPINESS)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
	}

	reclaim_options	= MEMCG_RECLAIM_MAY_SWAP | MEMCG_RECLAIM_PROACTIVE;
	while (nr_reclaimed < nr_to_reclaim) {
		/* Will converge on zero, but reclaim enforces a minimum */
		unsigned long batch_size = (nr_to_reclaim - nr_reclaimed) / 4;
		unsigned long reclaimed;

		if (signal_pending(current))
			return -EINTR;

		/*
		 * This is the final attempt, drain percpu lru caches in the
		 * hope of introducing more evictable pages for
		 * try_to_free_mem_cgroup_pages().
		 */
		if (!nr_retries)
			lru_add_drain_all();

		reclaimed = try_to_free_mem_cgroup_pages(memcg,
					batch_size, GFP_KERNEL,
					reclaim_options,
					swappiness == -1 ? NULL : &swappiness);

		if (!reclaimed && !nr_retries--)
			return -EAGAIN;

		nr_reclaimed += reclaimed;
	}

	return nbytes;
}

static struct cftype memory_files[] = {
	{
		.name = "current",
		.flags = CFTYPE_NOT_ON_ROOT,
		.read_u64 = memory_current_read,
	},
	{
		.name = "peak",
		.flags = CFTYPE_NOT_ON_ROOT,
		.open = peak_open,
		.release = peak_release,
		.seq_show = memory_peak_show,
		.write = memory_peak_write,
	},
	{
		.name = "min",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = memory_min_show,
		.write = memory_min_write,
	},
	{
		.name = "low",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = memory_low_show,
		.write = memory_low_write,
	},
	{
		.name = "high",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = memory_high_show,
		.write = memory_high_write,
	},
	{
		.name = "max",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = memory_max_show,
		.write = memory_max_write,
	},
	{
		.name = "events",
		.flags = CFTYPE_NOT_ON_ROOT,
		.file_offset = offsetof(struct mem_cgroup, events_file),
		.seq_show = memory_events_show,
	},
	{
		.name = "events.local",
		.flags = CFTYPE_NOT_ON_ROOT,
		.file_offset = offsetof(struct mem_cgroup, events_local_file),
		.seq_show = memory_events_local_show,
	},
	{
		.name = "stat",
		.seq_show = memory_stat_show,
	},
#ifdef CONFIG_NUMA
	{
		.name = "numa_stat",
		.seq_show = memory_numa_stat_show,
	},
#endif
	{
		.name = "oom.group",
		.flags = CFTYPE_NOT_ON_ROOT | CFTYPE_NS_DELEGATABLE,
		.seq_show = memory_oom_group_show,
		.write = memory_oom_group_write,
	},
	{
		.name = "reclaim",
		.flags = CFTYPE_NS_DELEGATABLE,
		.write = memory_reclaim,
	},
	{ }	/* terminate */
};

struct cgroup_subsys memory_cgrp_subsys = {
	.css_alloc = mem_cgroup_css_alloc,
	.css_online = mem_cgroup_css_online,
	.css_offline = mem_cgroup_css_offline,
	.css_released = mem_cgroup_css_released,
	.css_free = mem_cgroup_css_free,
	.css_reset = mem_cgroup_css_reset,
	.css_rstat_flush = mem_cgroup_css_rstat_flush,
	.attach = mem_cgroup_attach,
	.fork = mem_cgroup_fork,
	.exit = mem_cgroup_exit,
	.dfl_cftypes = memory_files,
#ifdef CONFIG_MEMCG_V1
	.legacy_cftypes = mem_cgroup_legacy_files,
#endif
	.early_init = 0,
};

/**
 * mem_cgroup_calculate_protection - check if memory consumption is in the normal range
 * @root: the top ancestor of the sub-tree being checked
 * @memcg: the memory cgroup to check
 *
 * WARNING: This function is not stateless! It can only be used as part
 *          of a top-down tree iteration, not for isolated queries.
 */
void mem_cgroup_calculate_protection(struct mem_cgroup *root,
				     struct mem_cgroup *memcg)
{
	bool recursive_protection =
		cgrp_dfl_root.flags & CGRP_ROOT_MEMORY_RECURSIVE_PROT;

	if (mem_cgroup_disabled())
		return;

	if (!root)
		root = root_mem_cgroup;

	page_counter_calculate_protection(&root->memory, &memcg->memory, recursive_protection);
}

static int charge_memcg(struct folio *folio, struct mem_cgroup *memcg,
			gfp_t gfp)
{
	int ret;

	ret = try_charge(memcg, gfp, folio_nr_pages(folio));
	if (ret)
		goto out;

	css_get(&memcg->css);
	commit_charge(folio, memcg);
	memcg1_commit_charge(folio, memcg);
out:
	return ret;
}

int __mem_cgroup_charge(struct folio *folio, struct mm_struct *mm, gfp_t gfp)
{
	struct mem_cgroup *memcg;
	int ret;

	memcg = get_mem_cgroup_from_mm(mm);
	ret = charge_memcg(folio, memcg, gfp);
	css_put(&memcg->css);

	return ret;
}

/**
 * mem_cgroup_charge_hugetlb - charge the memcg for a hugetlb folio
 * @folio: folio being charged
 * @gfp: reclaim mode
 *
 * This function is called when allocating a huge page folio, after the page has
 * already been obtained and charged to the appropriate hugetlb cgroup
 * controller (if it is enabled).
 *
 * Returns ENOMEM if the memcg is already full.
 * Returns 0 if either the charge was successful, or if we skip the charging.
 */
int mem_cgroup_charge_hugetlb(struct folio *folio, gfp_t gfp)
{
	struct mem_cgroup *memcg = get_mem_cgroup_from_current();
	int ret = 0;

	/*
	 * Even memcg does not account for hugetlb, we still want to update
	 * system-level stats via lruvec_stat_mod_folio. Return 0, and skip
	 * charging the memcg.
	 */
	if (mem_cgroup_disabled() || !memcg_accounts_hugetlb() ||
		!memcg || !cgroup_subsys_on_dfl(memory_cgrp_subsys))
		goto out;

	if (charge_memcg(folio, memcg, gfp))
		ret = -ENOMEM;

out:
	mem_cgroup_put(memcg);
	return ret;
}

/**
 * mem_cgroup_swapin_charge_folio - Charge a newly allocated folio for swapin.
 * @folio: folio to charge.
 * @mm: mm context of the victim
 * @gfp: reclaim mode
 * @entry: swap entry for which the folio is allocated
 *
 * This function charges a folio allocated for swapin. Please call this before
 * adding the folio to the swapcache.
 *
 * Returns 0 on success. Otherwise, an error code is returned.
 */
int mem_cgroup_swapin_charge_folio(struct folio *folio, struct mm_struct *mm,
				  gfp_t gfp, swp_entry_t entry)
{
	struct mem_cgroup *memcg;
	unsigned short id;
	int ret;

	if (mem_cgroup_disabled())
		return 0;

	id = lookup_swap_cgroup_id(entry);
	rcu_read_lock();
	memcg = mem_cgroup_from_id(id);
	if (!memcg || !css_tryget_online(&memcg->css))
		memcg = get_mem_cgroup_from_mm(mm);
	rcu_read_unlock();

	ret = charge_memcg(folio, memcg, gfp);

	css_put(&memcg->css);
	return ret;
}

/*
 * mem_cgroup_swapin_uncharge_swap - uncharge swap slot
 * @entry: the first swap entry for which the pages are charged
 * @nr_pages: number of pages which will be uncharged
 *
 * Call this function after successfully adding the charged page to swapcache.
 *
 * Note: This function assumes the page for which swap slot is being uncharged
 * is order 0 page.
 */
void mem_cgroup_swapin_uncharge_swap(swp_entry_t entry, unsigned int nr_pages)
{
	/*
	 * Cgroup1's unified memory+swap counter has been charged with the
	 * new swapcache page, finish the transfer by uncharging the swap
	 * slot. The swap slot would also get uncharged when it dies, but
	 * it can stick around indefinitely and we'd count the page twice
	 * the entire time.
	 *
	 * Cgroup2 has separate resource counters for memory and swap,
	 * so this is a non-issue here. Memory and swap charge lifetimes
	 * correspond 1:1 to page and swap slot lifetimes: we charge the
	 * page to memory here, and uncharge swap when the slot is freed.
	 */
	if (do_memsw_account()) {
		/*
		 * The swap entry might not get freed for a long time,
		 * let's not wait for it.  The page already received a
		 * memory+swap charge, drop the swap entry duplicate.
		 */
		mem_cgroup_uncharge_swap(entry, nr_pages);
	}
}

struct uncharge_gather {
	struct mem_cgroup *memcg;
	unsigned long nr_memory;
	unsigned long pgpgout;
	unsigned long nr_kmem;
	int nid;
};

static inline void uncharge_gather_clear(struct uncharge_gather *ug)
{
	memset(ug, 0, sizeof(*ug));
}

static void uncharge_batch(const struct uncharge_gather *ug)
{
	if (ug->nr_memory) {
		page_counter_uncharge(&ug->memcg->memory, ug->nr_memory);
		if (do_memsw_account())
			page_counter_uncharge(&ug->memcg->memsw, ug->nr_memory);
		if (ug->nr_kmem) {
			mod_memcg_state(ug->memcg, MEMCG_KMEM, -ug->nr_kmem);
			memcg1_account_kmem(ug->memcg, -ug->nr_kmem);
		}
		memcg1_oom_recover(ug->memcg);
	}

	memcg1_uncharge_batch(ug->memcg, ug->pgpgout, ug->nr_memory, ug->nid);

	/* drop reference from uncharge_folio */
	css_put(&ug->memcg->css);
}

static void uncharge_folio(struct folio *folio, struct uncharge_gather *ug)
{
	long nr_pages;
	struct mem_cgroup *memcg;
	struct obj_cgroup *objcg;

	VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);

	/*
	 * Nobody should be changing or seriously looking at
	 * folio memcg or objcg at this point, we have fully
	 * exclusive access to the folio.
	 */
	if (folio_memcg_kmem(folio)) {
		objcg = __folio_objcg(folio);
		/*
		 * This get matches the put at the end of the function and
		 * kmem pages do not hold memcg references anymore.
		 */
		memcg = get_mem_cgroup_from_objcg(objcg);
	} else {
		memcg = __folio_memcg(folio);
	}

	if (!memcg)
		return;

	if (ug->memcg != memcg) {
		if (ug->memcg) {
			uncharge_batch(ug);
			uncharge_gather_clear(ug);
		}
		ug->memcg = memcg;
		ug->nid = folio_nid(folio);

		/* pairs with css_put in uncharge_batch */
		css_get(&memcg->css);
	}

	nr_pages = folio_nr_pages(folio);

	if (folio_memcg_kmem(folio)) {
		ug->nr_memory += nr_pages;
		ug->nr_kmem += nr_pages;

		folio->memcg_data = 0;
		obj_cgroup_put(objcg);
	} else {
		/* LRU pages aren't accounted at the root level */
		if (!mem_cgroup_is_root(memcg))
			ug->nr_memory += nr_pages;
		ug->pgpgout++;

		WARN_ON_ONCE(folio_unqueue_deferred_split(folio));
		folio->memcg_data = 0;
	}

	css_put(&memcg->css);
}

void __mem_cgroup_uncharge(struct folio *folio)
{
	struct uncharge_gather ug;

	/* Don't touch folio->lru of any random page, pre-check: */
	if (!folio_memcg_charged(folio))
		return;

	uncharge_gather_clear(&ug);
	uncharge_folio(folio, &ug);
	uncharge_batch(&ug);
}

void __mem_cgroup_uncharge_folios(struct folio_batch *folios)
{
	struct uncharge_gather ug;
	unsigned int i;

	uncharge_gather_clear(&ug);
	for (i = 0; i < folios->nr; i++)
		uncharge_folio(folios->folios[i], &ug);
	if (ug.memcg)
		uncharge_batch(&ug);
}

/**
 * mem_cgroup_replace_folio - Charge a folio's replacement.
 * @old: Currently circulating folio.
 * @new: Replacement folio.
 *
 * Charge @new as a replacement folio for @old. @old will
 * be uncharged upon free.
 *
 * Both folios must be locked, @new->mapping must be set up.
 */
void mem_cgroup_replace_folio(struct folio *old, struct folio *new)
{
	struct mem_cgroup *memcg;
	long nr_pages = folio_nr_pages(new);

	VM_BUG_ON_FOLIO(!folio_test_locked(old), old);
	VM_BUG_ON_FOLIO(!folio_test_locked(new), new);
	VM_BUG_ON_FOLIO(folio_test_anon(old) != folio_test_anon(new), new);
	VM_BUG_ON_FOLIO(folio_nr_pages(old) != nr_pages, new);

	if (mem_cgroup_disabled())
		return;

	/* Page cache replacement: new folio already charged? */
	if (folio_memcg_charged(new))
		return;

	memcg = folio_memcg(old);
	VM_WARN_ON_ONCE_FOLIO(!memcg, old);
	if (!memcg)
		return;

	/* Force-charge the new page. The old one will be freed soon */
	if (!mem_cgroup_is_root(memcg)) {
		page_counter_charge(&memcg->memory, nr_pages);
		if (do_memsw_account())
			page_counter_charge(&memcg->memsw, nr_pages);
	}

	css_get(&memcg->css);
	commit_charge(new, memcg);
	memcg1_commit_charge(new, memcg);
}

/**
 * mem_cgroup_migrate - Transfer the memcg data from the old to the new folio.
 * @old: Currently circulating folio.
 * @new: Replacement folio.
 *
 * Transfer the memcg data from the old folio to the new folio for migration.
 * The old folio's data info will be cleared. Note that the memory counters
 * will remain unchanged throughout the process.
 *
 * Both folios must be locked, @new->mapping must be set up.
 */
void mem_cgroup_migrate(struct folio *old, struct folio *new)
{
	struct mem_cgroup *memcg;

	VM_BUG_ON_FOLIO(!folio_test_locked(old), old);
	VM_BUG_ON_FOLIO(!folio_test_locked(new), new);
	VM_BUG_ON_FOLIO(folio_test_anon(old) != folio_test_anon(new), new);
	VM_BUG_ON_FOLIO(folio_nr_pages(old) != folio_nr_pages(new), new);
	VM_BUG_ON_FOLIO(folio_test_lru(old), old);

	if (mem_cgroup_disabled())
		return;

	memcg = folio_memcg(old);
	/*
	 * Note that it is normal to see !memcg for a hugetlb folio.
	 * For e.g, itt could have been allocated when memory_hugetlb_accounting
	 * was not selected.
	 */
	VM_WARN_ON_ONCE_FOLIO(!folio_test_hugetlb(old) && !memcg, old);
	if (!memcg)
		return;

	/* Transfer the charge and the css ref */
	commit_charge(new, memcg);

	/* Warning should never happen, so don't worry about refcount non-0 */
	WARN_ON_ONCE(folio_unqueue_deferred_split(old));
	old->memcg_data = 0;
}

DEFINE_STATIC_KEY_FALSE(memcg_sockets_enabled_key);
EXPORT_SYMBOL(memcg_sockets_enabled_key);

void mem_cgroup_sk_alloc(struct sock *sk)
{
	struct mem_cgroup *memcg;

	if (!mem_cgroup_sockets_enabled)
		return;

	/* Do not associate the sock with unrelated interrupted task's memcg. */
	if (!in_task())
		return;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(current);
	if (mem_cgroup_is_root(memcg))
		goto out;
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && !memcg1_tcpmem_active(memcg))
		goto out;
	if (css_tryget(&memcg->css))
		sk->sk_memcg = memcg;
out:
	rcu_read_unlock();
}

void mem_cgroup_sk_free(struct sock *sk)
{
	if (sk->sk_memcg)
		css_put(&sk->sk_memcg->css);
}

/**
 * mem_cgroup_charge_skmem - charge socket memory
 * @memcg: memcg to charge
 * @nr_pages: number of pages to charge
 * @gfp_mask: reclaim mode
 *
 * Charges @nr_pages to @memcg. Returns %true if the charge fit within
 * @memcg's configured limit, %false if it doesn't.
 */
bool mem_cgroup_charge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages,
			     gfp_t gfp_mask)
{
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return memcg1_charge_skmem(memcg, nr_pages, gfp_mask);

	if (try_charge(memcg, gfp_mask, nr_pages) == 0) {
		mod_memcg_state(memcg, MEMCG_SOCK, nr_pages);
		return true;
	}

	return false;
}

/**
 * mem_cgroup_uncharge_skmem - uncharge socket memory
 * @memcg: memcg to uncharge
 * @nr_pages: number of pages to uncharge
 */
void mem_cgroup_uncharge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys)) {
		memcg1_uncharge_skmem(memcg, nr_pages);
		return;
	}

	mod_memcg_state(memcg, MEMCG_SOCK, -nr_pages);

	refill_stock(memcg, nr_pages);
}

static int __init cgroup_memory(char *s)
{
	char *token;

	while ((token = strsep(&s, ",")) != NULL) {
		if (!*token)
			continue;
		if (!strcmp(token, "nosocket"))
			cgroup_memory_nosocket = true;
		if (!strcmp(token, "nokmem"))
			cgroup_memory_nokmem = true;
		if (!strcmp(token, "nobpf"))
			cgroup_memory_nobpf = true;
	}
	return 1;
}
__setup("cgroup.memory=", cgroup_memory);

/*
 * subsys_initcall() for memory controller.
 *
 * Some parts like memcg_hotplug_cpu_dead() have to be initialized from this
 * context because of lock dependencies (cgroup_lock -> cpu hotplug) but
 * basically everything that doesn't depend on a specific mem_cgroup structure
 * should be initialized from here.
 */
static int __init mem_cgroup_init(void)
{
	int cpu;

	/*
	 * Currently s32 type (can refer to struct batched_lruvec_stat) is
	 * used for per-memcg-per-cpu caching of per-node statistics. In order
	 * to work fine, we should make sure that the overfill threshold can't
	 * exceed S32_MAX / PAGE_SIZE.
	 */
	BUILD_BUG_ON(MEMCG_CHARGE_BATCH > S32_MAX / PAGE_SIZE);

	cpuhp_setup_state_nocalls(CPUHP_MM_MEMCQ_DEAD, "mm/memctrl:dead", NULL,
				  memcg_hotplug_cpu_dead);

	for_each_possible_cpu(cpu)
		INIT_WORK(&per_cpu_ptr(&memcg_stock, cpu)->work,
			  drain_local_stock);

	return 0;
}
subsys_initcall(mem_cgroup_init);

#ifdef CONFIG_SWAP
static struct mem_cgroup *mem_cgroup_id_get_online(struct mem_cgroup *memcg)
{
	while (!refcount_inc_not_zero(&memcg->id.ref)) {
		/*
		 * The root cgroup cannot be destroyed, so it's refcount must
		 * always be >= 1.
		 */
		if (WARN_ON_ONCE(mem_cgroup_is_root(memcg))) {
			VM_BUG_ON(1);
			break;
		}
		memcg = parent_mem_cgroup(memcg);
		if (!memcg)
			memcg = root_mem_cgroup;
	}
	return memcg;
}

/**
 * mem_cgroup_swapout - transfer a memsw charge to swap
 * @folio: folio whose memsw charge to transfer
 * @entry: swap entry to move the charge to
 *
 * Transfer the memsw charge of @folio to @entry.
 */
void mem_cgroup_swapout(struct folio *folio, swp_entry_t entry)
{
	struct mem_cgroup *memcg, *swap_memcg;
	unsigned int nr_entries;

	VM_BUG_ON_FOLIO(folio_test_lru(folio), folio);
	VM_BUG_ON_FOLIO(folio_ref_count(folio), folio);

	if (mem_cgroup_disabled())
		return;

	if (!do_memsw_account())
		return;

	memcg = folio_memcg(folio);

	VM_WARN_ON_ONCE_FOLIO(!memcg, folio);
	if (!memcg)
		return;

	/*
	 * In case the memcg owning these pages has been offlined and doesn't
	 * have an ID allocated to it anymore, charge the closest online
	 * ancestor for the swap instead and transfer the memory+swap charge.
	 */
	swap_memcg = mem_cgroup_id_get_online(memcg);
	nr_entries = folio_nr_pages(folio);
	/* Get references for the tail pages, too */
	if (nr_entries > 1)
		mem_cgroup_id_get_many(swap_memcg, nr_entries - 1);
	mod_memcg_state(swap_memcg, MEMCG_SWAP, nr_entries);

	swap_cgroup_record(folio, mem_cgroup_id(swap_memcg), entry);

	folio_unqueue_deferred_split(folio);
	folio->memcg_data = 0;

	if (!mem_cgroup_is_root(memcg))
		page_counter_uncharge(&memcg->memory, nr_entries);

	if (memcg != swap_memcg) {
		if (!mem_cgroup_is_root(swap_memcg))
			page_counter_charge(&swap_memcg->memsw, nr_entries);
		page_counter_uncharge(&memcg->memsw, nr_entries);
	}

	memcg1_swapout(folio, memcg);
	css_put(&memcg->css);
}

/**
 * __mem_cgroup_try_charge_swap - try charging swap space for a folio
 * @folio: folio being added to swap
 * @entry: swap entry to charge
 *
 * Try to charge @folio's memcg for the swap space at @entry.
 *
 * Returns 0 on success, -ENOMEM on failure.
 */
int __mem_cgroup_try_charge_swap(struct folio *folio, swp_entry_t entry)
{
	unsigned int nr_pages = folio_nr_pages(folio);
	struct page_counter *counter;
	struct mem_cgroup *memcg;

	if (do_memsw_account())
		return 0;

	memcg = folio_memcg(folio);

	VM_WARN_ON_ONCE_FOLIO(!memcg, folio);
	if (!memcg)
		return 0;

	if (!entry.val) {
		memcg_memory_event(memcg, MEMCG_SWAP_FAIL);
		return 0;
	}

	memcg = mem_cgroup_id_get_online(memcg);

	if (!mem_cgroup_is_root(memcg) &&
	    !page_counter_try_charge(&memcg->swap, nr_pages, &counter)) {
		memcg_memory_event(memcg, MEMCG_SWAP_MAX);
		memcg_memory_event(memcg, MEMCG_SWAP_FAIL);
		mem_cgroup_id_put(memcg);
		return -ENOMEM;
	}

	/* Get references for the tail pages, too */
	if (nr_pages > 1)
		mem_cgroup_id_get_many(memcg, nr_pages - 1);
	mod_memcg_state(memcg, MEMCG_SWAP, nr_pages);

	swap_cgroup_record(folio, mem_cgroup_id(memcg), entry);

	return 0;
}

/**
 * __mem_cgroup_uncharge_swap - uncharge swap space
 * @entry: swap entry to uncharge
 * @nr_pages: the amount of swap space to uncharge
 */
void __mem_cgroup_uncharge_swap(swp_entry_t entry, unsigned int nr_pages)
{
	struct mem_cgroup *memcg;
	unsigned short id;

	id = swap_cgroup_clear(entry, nr_pages);
	rcu_read_lock();
	memcg = mem_cgroup_from_id(id);
	if (memcg) {
		if (!mem_cgroup_is_root(memcg)) {
			if (do_memsw_account())
				page_counter_uncharge(&memcg->memsw, nr_pages);
			else
				page_counter_uncharge(&memcg->swap, nr_pages);
		}
		mod_memcg_state(memcg, MEMCG_SWAP, -nr_pages);
		mem_cgroup_id_put_many(memcg, nr_pages);
	}
	rcu_read_unlock();
}

long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg)
{
	long nr_swap_pages = get_nr_swap_pages();

	if (mem_cgroup_disabled() || do_memsw_account())
		return nr_swap_pages;
	for (; !mem_cgroup_is_root(memcg); memcg = parent_mem_cgroup(memcg))
		nr_swap_pages = min_t(long, nr_swap_pages,
				      READ_ONCE(memcg->swap.max) -
				      page_counter_read(&memcg->swap));
	return nr_swap_pages;
}

bool mem_cgroup_swap_full(struct folio *folio)
{
	struct mem_cgroup *memcg;

	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);

	if (vm_swap_full())
		return true;
	if (do_memsw_account())
		return false;

	memcg = folio_memcg(folio);
	if (!memcg)
		return false;

	for (; !mem_cgroup_is_root(memcg); memcg = parent_mem_cgroup(memcg)) {
		unsigned long usage = page_counter_read(&memcg->swap);

		if (usage * 2 >= READ_ONCE(memcg->swap.high) ||
		    usage * 2 >= READ_ONCE(memcg->swap.max))
			return true;
	}

	return false;
}

static int __init setup_swap_account(char *s)
{
	bool res;

	if (!kstrtobool(s, &res) && !res)
		pr_warn_once("The swapaccount=0 commandline option is deprecated "
			     "in favor of configuring swap control via cgroupfs. "
			     "Please report your usecase to linux-mm@kvack.org if you "
			     "depend on this functionality.\n");
	return 1;
}
__setup("swapaccount=", setup_swap_account);

static u64 swap_current_read(struct cgroup_subsys_state *css,
			     struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	return (u64)page_counter_read(&memcg->swap) * PAGE_SIZE;
}

static int swap_peak_show(struct seq_file *sf, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(sf));

	return peak_show(sf, v, &memcg->swap);
}

static ssize_t swap_peak_write(struct kernfs_open_file *of, char *buf,
			       size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));

	return peak_write(of, buf, nbytes, off, &memcg->swap,
			  &memcg->swap_peaks);
}

static int swap_high_show(struct seq_file *m, void *v)
{
	return seq_puts_memcg_tunable(m,
		READ_ONCE(mem_cgroup_from_seq(m)->swap.high));
}

static ssize_t swap_high_write(struct kernfs_open_file *of,
			       char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long high;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &high);
	if (err)
		return err;

	page_counter_set_high(&memcg->swap, high);

	return nbytes;
}

static int swap_max_show(struct seq_file *m, void *v)
{
	return seq_puts_memcg_tunable(m,
		READ_ONCE(mem_cgroup_from_seq(m)->swap.max));
}

static ssize_t swap_max_write(struct kernfs_open_file *of,
			      char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long max;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &max);
	if (err)
		return err;

	xchg(&memcg->swap.max, max);

	return nbytes;
}

static int swap_events_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	seq_printf(m, "high %lu\n",
		   atomic_long_read(&memcg->memory_events[MEMCG_SWAP_HIGH]));
	seq_printf(m, "max %lu\n",
		   atomic_long_read(&memcg->memory_events[MEMCG_SWAP_MAX]));
	seq_printf(m, "fail %lu\n",
		   atomic_long_read(&memcg->memory_events[MEMCG_SWAP_FAIL]));

	return 0;
}

static struct cftype swap_files[] = {
	{
		.name = "swap.current",
		.flags = CFTYPE_NOT_ON_ROOT,
		.read_u64 = swap_current_read,
	},
	{
		.name = "swap.high",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = swap_high_show,
		.write = swap_high_write,
	},
	{
		.name = "swap.max",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = swap_max_show,
		.write = swap_max_write,
	},
	{
		.name = "swap.peak",
		.flags = CFTYPE_NOT_ON_ROOT,
		.open = peak_open,
		.release = peak_release,
		.seq_show = swap_peak_show,
		.write = swap_peak_write,
	},
	{
		.name = "swap.events",
		.flags = CFTYPE_NOT_ON_ROOT,
		.file_offset = offsetof(struct mem_cgroup, swap_events_file),
		.seq_show = swap_events_show,
	},
	{ }	/* terminate */
};

#ifdef CONFIG_ZSWAP
/**
 * obj_cgroup_may_zswap - check if this cgroup can zswap
 * @objcg: the object cgroup
 *
 * Check if the hierarchical zswap limit has been reached.
 *
 * This doesn't check for specific headroom, and it is not atomic
 * either. But with zswap, the size of the allocation is only known
 * once compression has occurred, and this optimistic pre-check avoids
 * spending cycles on compression when there is already no room left
 * or zswap is disabled altogether somewhere in the hierarchy.
 */
bool obj_cgroup_may_zswap(struct obj_cgroup *objcg)
{
	struct mem_cgroup *memcg, *original_memcg;
	bool ret = true;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return true;

	original_memcg = get_mem_cgroup_from_objcg(objcg);
	for (memcg = original_memcg; !mem_cgroup_is_root(memcg);
	     memcg = parent_mem_cgroup(memcg)) {
		unsigned long max = READ_ONCE(memcg->zswap_max);
		unsigned long pages;

		if (max == PAGE_COUNTER_MAX)
			continue;
		if (max == 0) {
			ret = false;
			break;
		}

		/* Force flush to get accurate stats for charging */
		__mem_cgroup_flush_stats(memcg, true);
		pages = memcg_page_state(memcg, MEMCG_ZSWAP_B) / PAGE_SIZE;
		if (pages < max)
			continue;
		ret = false;
		break;
	}
	mem_cgroup_put(original_memcg);
	return ret;
}

/**
 * obj_cgroup_charge_zswap - charge compression backend memory
 * @objcg: the object cgroup
 * @size: size of compressed object
 *
 * This forces the charge after obj_cgroup_may_zswap() allowed
 * compression and storage in zwap for this cgroup to go ahead.
 */
void obj_cgroup_charge_zswap(struct obj_cgroup *objcg, size_t size)
{
	struct mem_cgroup *memcg;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return;

	VM_WARN_ON_ONCE(!(current->flags & PF_MEMALLOC));

	/* PF_MEMALLOC context, charging must succeed */
	if (obj_cgroup_charge(objcg, GFP_KERNEL, size))
		VM_WARN_ON_ONCE(1);

	rcu_read_lock();
	memcg = obj_cgroup_memcg(objcg);
	mod_memcg_state(memcg, MEMCG_ZSWAP_B, size);
	mod_memcg_state(memcg, MEMCG_ZSWAPPED, 1);
	rcu_read_unlock();
}

/**
 * obj_cgroup_uncharge_zswap - uncharge compression backend memory
 * @objcg: the object cgroup
 * @size: size of compressed object
 *
 * Uncharges zswap memory on page in.
 */
void obj_cgroup_uncharge_zswap(struct obj_cgroup *objcg, size_t size)
{
	struct mem_cgroup *memcg;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return;

	obj_cgroup_uncharge(objcg, size);

	rcu_read_lock();
	memcg = obj_cgroup_memcg(objcg);
	mod_memcg_state(memcg, MEMCG_ZSWAP_B, -size);
	mod_memcg_state(memcg, MEMCG_ZSWAPPED, -1);
	rcu_read_unlock();
}

bool mem_cgroup_zswap_writeback_enabled(struct mem_cgroup *memcg)
{
	/* if zswap is disabled, do not block pages going to the swapping device */
	if (!zswap_is_enabled())
		return true;

	for (; memcg; memcg = parent_mem_cgroup(memcg))
		if (!READ_ONCE(memcg->zswap_writeback))
			return false;

	return true;
}

static u64 zswap_current_read(struct cgroup_subsys_state *css,
			      struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	mem_cgroup_flush_stats(memcg);
	return memcg_page_state(memcg, MEMCG_ZSWAP_B);
}

static int zswap_max_show(struct seq_file *m, void *v)
{
	return seq_puts_memcg_tunable(m,
		READ_ONCE(mem_cgroup_from_seq(m)->zswap_max));
}

static ssize_t zswap_max_write(struct kernfs_open_file *of,
			       char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long max;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &max);
	if (err)
		return err;

	xchg(&memcg->zswap_max, max);

	return nbytes;
}

static int zswap_writeback_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	seq_printf(m, "%d\n", READ_ONCE(memcg->zswap_writeback));
	return 0;
}

static ssize_t zswap_writeback_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	int zswap_writeback;
	ssize_t parse_ret = kstrtoint(strstrip(buf), 0, &zswap_writeback);

	if (parse_ret)
		return parse_ret;

	if (zswap_writeback != 0 && zswap_writeback != 1)
		return -EINVAL;

	WRITE_ONCE(memcg->zswap_writeback, zswap_writeback);
	return nbytes;
}

static struct cftype zswap_files[] = {
	{
		.name = "zswap.current",
		.flags = CFTYPE_NOT_ON_ROOT,
		.read_u64 = zswap_current_read,
	},
	{
		.name = "zswap.max",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = zswap_max_show,
		.write = zswap_max_write,
	},
	{
		.name = "zswap.writeback",
		.seq_show = zswap_writeback_show,
		.write = zswap_writeback_write,
	},
	{ }	/* terminate */
};
#endif /* CONFIG_ZSWAP */

static int __init mem_cgroup_swap_init(void)
{
	if (mem_cgroup_disabled())
		return 0;

	WARN_ON(cgroup_add_dfl_cftypes(&memory_cgrp_subsys, swap_files));
#ifdef CONFIG_MEMCG_V1
	WARN_ON(cgroup_add_legacy_cftypes(&memory_cgrp_subsys, memsw_files));
#endif
#ifdef CONFIG_ZSWAP
	WARN_ON(cgroup_add_dfl_cftypes(&memory_cgrp_subsys, zswap_files));
#endif
	return 0;
}
subsys_initcall(mem_cgroup_swap_init);

#endif /* CONFIG_SWAP */
