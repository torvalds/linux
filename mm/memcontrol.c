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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/page_counter.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/shmem_fs.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/smp.h>
#include <linux/page-flags.h>
#include <linux/backing-dev.h>
#include <linux/bit_spinlock.h>
#include <linux/rcupdate.h>
#include <linux/limits.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/spinlock.h>
#include <linux/eventfd.h>
#include <linux/poll.h>
#include <linux/sort.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/vmpressure.h>
#include <linux/mm_inline.h>
#include <linux/swap_cgroup.h>
#include <linux/cpu.h>
#include <linux/oom.h>
#include <linux/lockdep.h>
#include <linux/file.h>
#include <linux/tracehook.h>
#include "internal.h"
#include <net/sock.h>
#include <net/ip.h>
#include "slab.h"

#include <linux/uaccess.h>

#include <trace/events/vmscan.h>

struct cgroup_subsys memory_cgrp_subsys __read_mostly;
EXPORT_SYMBOL(memory_cgrp_subsys);

struct mem_cgroup *root_mem_cgroup __read_mostly;

#define MEM_CGROUP_RECLAIM_RETRIES	5

/* Socket memory accounting disabled? */
static bool cgroup_memory_nosocket;

/* Kernel memory accounting disabled? */
static bool cgroup_memory_nokmem;

/* Whether the swap controller is active */
#ifdef CONFIG_MEMCG_SWAP
int do_swap_account __read_mostly;
#else
#define do_swap_account		0
#endif

/* Whether legacy memory+swap accounting is active */
static bool do_memsw_account(void)
{
	return !cgroup_subsys_on_dfl(memory_cgrp_subsys) && do_swap_account;
}

static const char *const mem_cgroup_lru_names[] = {
	"inactive_anon",
	"active_anon",
	"inactive_file",
	"active_file",
	"unevictable",
};

#define THRESHOLDS_EVENTS_TARGET 128
#define SOFTLIMIT_EVENTS_TARGET 1024
#define NUMAINFO_EVENTS_TARGET	1024

/*
 * Cgroups above their limits are maintained in a RB-Tree, independent of
 * their hierarchy representation
 */

struct mem_cgroup_tree_per_node {
	struct rb_root rb_root;
	struct rb_node *rb_rightmost;
	spinlock_t lock;
};

struct mem_cgroup_tree {
	struct mem_cgroup_tree_per_node *rb_tree_per_node[MAX_NUMNODES];
};

static struct mem_cgroup_tree soft_limit_tree __read_mostly;

/* for OOM */
struct mem_cgroup_eventfd_list {
	struct list_head list;
	struct eventfd_ctx *eventfd;
};

/*
 * cgroup_event represents events which userspace want to receive.
 */
struct mem_cgroup_event {
	/*
	 * memcg which the event belongs to.
	 */
	struct mem_cgroup *memcg;
	/*
	 * eventfd to signal userspace about the event.
	 */
	struct eventfd_ctx *eventfd;
	/*
	 * Each of these stored in a list by the cgroup.
	 */
	struct list_head list;
	/*
	 * register_event() callback will be used to add new userspace
	 * waiter for changes related to this event.  Use eventfd_signal()
	 * on eventfd to send notification to userspace.
	 */
	int (*register_event)(struct mem_cgroup *memcg,
			      struct eventfd_ctx *eventfd, const char *args);
	/*
	 * unregister_event() callback will be called when userspace closes
	 * the eventfd or on cgroup removing.  This callback must be set,
	 * if you want provide notification functionality.
	 */
	void (*unregister_event)(struct mem_cgroup *memcg,
				 struct eventfd_ctx *eventfd);
	/*
	 * All fields below needed to unregister event when
	 * userspace closes eventfd.
	 */
	poll_table pt;
	wait_queue_head_t *wqh;
	wait_queue_entry_t wait;
	struct work_struct remove;
};

static void mem_cgroup_threshold(struct mem_cgroup *memcg);
static void mem_cgroup_oom_notify(struct mem_cgroup *memcg);

/* Stuffs for move charges at task migration. */
/*
 * Types of charges to be moved.
 */
#define MOVE_ANON	0x1U
#define MOVE_FILE	0x2U
#define MOVE_MASK	(MOVE_ANON | MOVE_FILE)

/* "mc" and its members are protected by cgroup_mutex */
static struct move_charge_struct {
	spinlock_t	  lock; /* for from, to */
	struct mm_struct  *mm;
	struct mem_cgroup *from;
	struct mem_cgroup *to;
	unsigned long flags;
	unsigned long precharge;
	unsigned long moved_charge;
	unsigned long moved_swap;
	struct task_struct *moving_task;	/* a task moving charges */
	wait_queue_head_t waitq;		/* a waitq for other context */
} mc = {
	.lock = __SPIN_LOCK_UNLOCKED(mc.lock),
	.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(mc.waitq),
};

/*
 * Maximum loops in mem_cgroup_hierarchical_reclaim(), used for soft
 * limit reclaim to prevent infinite loops, if they ever occur.
 */
#define	MEM_CGROUP_MAX_RECLAIM_LOOPS		100
#define	MEM_CGROUP_MAX_SOFT_LIMIT_RECLAIM_LOOPS	2

enum charge_type {
	MEM_CGROUP_CHARGE_TYPE_CACHE = 0,
	MEM_CGROUP_CHARGE_TYPE_ANON,
	MEM_CGROUP_CHARGE_TYPE_SWAPOUT,	/* for accounting swapcache */
	MEM_CGROUP_CHARGE_TYPE_DROP,	/* a page was unused swap cache */
	NR_CHARGE_TYPE,
};

/* for encoding cft->private value on file */
enum res_type {
	_MEM,
	_MEMSWAP,
	_OOM_TYPE,
	_KMEM,
	_TCP,
};

#define MEMFILE_PRIVATE(x, val)	((x) << 16 | (val))
#define MEMFILE_TYPE(val)	((val) >> 16 & 0xffff)
#define MEMFILE_ATTR(val)	((val) & 0xffff)
/* Used for OOM nofiier */
#define OOM_CONTROL		(0)

/* Some nice accessors for the vmpressure. */
struct vmpressure *memcg_to_vmpressure(struct mem_cgroup *memcg)
{
	if (!memcg)
		memcg = root_mem_cgroup;
	return &memcg->vmpressure;
}

struct cgroup_subsys_state *vmpressure_to_css(struct vmpressure *vmpr)
{
	return &container_of(vmpr, struct mem_cgroup, vmpressure)->css;
}

static inline bool mem_cgroup_is_root(struct mem_cgroup *memcg)
{
	return (memcg == root_mem_cgroup);
}

#ifndef CONFIG_SLOB
/*
 * This will be the memcg's index in each cache's ->memcg_params.memcg_caches.
 * The main reason for not using cgroup id for this:
 *  this works better in sparse environments, where we have a lot of memcgs,
 *  but only a few kmem-limited. Or also, if we have, for instance, 200
 *  memcgs, and none but the 200th is kmem-limited, we'd have to have a
 *  200 entry array for that.
 *
 * The current size of the caches array is stored in memcg_nr_cache_ids. It
 * will double each time we have to increase it.
 */
static DEFINE_IDA(memcg_cache_ida);
int memcg_nr_cache_ids;

/* Protects memcg_nr_cache_ids */
static DECLARE_RWSEM(memcg_cache_ids_sem);

void memcg_get_cache_ids(void)
{
	down_read(&memcg_cache_ids_sem);
}

void memcg_put_cache_ids(void)
{
	up_read(&memcg_cache_ids_sem);
}

/*
 * MIN_SIZE is different than 1, because we would like to avoid going through
 * the alloc/free process all the time. In a small machine, 4 kmem-limited
 * cgroups is a reasonable guess. In the future, it could be a parameter or
 * tunable, but that is strictly not necessary.
 *
 * MAX_SIZE should be as large as the number of cgrp_ids. Ideally, we could get
 * this constant directly from cgroup, but it is understandable that this is
 * better kept as an internal representation in cgroup.c. In any case, the
 * cgrp_id space is not getting any smaller, and we don't have to necessarily
 * increase ours as well if it increases.
 */
#define MEMCG_CACHES_MIN_SIZE 4
#define MEMCG_CACHES_MAX_SIZE MEM_CGROUP_ID_MAX

/*
 * A lot of the calls to the cache allocation functions are expected to be
 * inlined by the compiler. Since the calls to memcg_kmem_get_cache are
 * conditional to this static branch, we'll have to allow modules that does
 * kmem_cache_alloc and the such to see this symbol as well
 */
DEFINE_STATIC_KEY_FALSE(memcg_kmem_enabled_key);
EXPORT_SYMBOL(memcg_kmem_enabled_key);

struct workqueue_struct *memcg_kmem_cache_wq;

#endif /* !CONFIG_SLOB */

/**
 * mem_cgroup_css_from_page - css of the memcg associated with a page
 * @page: page of interest
 *
 * If memcg is bound to the default hierarchy, css of the memcg associated
 * with @page is returned.  The returned css remains associated with @page
 * until it is released.
 *
 * If memcg is bound to a traditional hierarchy, the css of root_mem_cgroup
 * is returned.
 */
struct cgroup_subsys_state *mem_cgroup_css_from_page(struct page *page)
{
	struct mem_cgroup *memcg;

	memcg = page->mem_cgroup;

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
	memcg = READ_ONCE(page->mem_cgroup);
	while (memcg && !(memcg->css.flags & CSS_ONLINE))
		memcg = parent_mem_cgroup(memcg);
	if (memcg)
		ino = cgroup_ino(memcg->css.cgroup);
	rcu_read_unlock();
	return ino;
}

static struct mem_cgroup_per_node *
mem_cgroup_page_nodeinfo(struct mem_cgroup *memcg, struct page *page)
{
	int nid = page_to_nid(page);

	return memcg->nodeinfo[nid];
}

static struct mem_cgroup_tree_per_node *
soft_limit_tree_node(int nid)
{
	return soft_limit_tree.rb_tree_per_node[nid];
}

static struct mem_cgroup_tree_per_node *
soft_limit_tree_from_page(struct page *page)
{
	int nid = page_to_nid(page);

	return soft_limit_tree.rb_tree_per_node[nid];
}

static void __mem_cgroup_insert_exceeded(struct mem_cgroup_per_node *mz,
					 struct mem_cgroup_tree_per_node *mctz,
					 unsigned long new_usage_in_excess)
{
	struct rb_node **p = &mctz->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct mem_cgroup_per_node *mz_node;
	bool rightmost = true;

	if (mz->on_tree)
		return;

	mz->usage_in_excess = new_usage_in_excess;
	if (!mz->usage_in_excess)
		return;
	while (*p) {
		parent = *p;
		mz_node = rb_entry(parent, struct mem_cgroup_per_node,
					tree_node);
		if (mz->usage_in_excess < mz_node->usage_in_excess) {
			p = &(*p)->rb_left;
			rightmost = false;
		}

		/*
		 * We can't avoid mem cgroups that are over their soft
		 * limit by the same amount
		 */
		else if (mz->usage_in_excess >= mz_node->usage_in_excess)
			p = &(*p)->rb_right;
	}

	if (rightmost)
		mctz->rb_rightmost = &mz->tree_node;

	rb_link_node(&mz->tree_node, parent, p);
	rb_insert_color(&mz->tree_node, &mctz->rb_root);
	mz->on_tree = true;
}

static void __mem_cgroup_remove_exceeded(struct mem_cgroup_per_node *mz,
					 struct mem_cgroup_tree_per_node *mctz)
{
	if (!mz->on_tree)
		return;

	if (&mz->tree_node == mctz->rb_rightmost)
		mctz->rb_rightmost = rb_prev(&mz->tree_node);

	rb_erase(&mz->tree_node, &mctz->rb_root);
	mz->on_tree = false;
}

static void mem_cgroup_remove_exceeded(struct mem_cgroup_per_node *mz,
				       struct mem_cgroup_tree_per_node *mctz)
{
	unsigned long flags;

	spin_lock_irqsave(&mctz->lock, flags);
	__mem_cgroup_remove_exceeded(mz, mctz);
	spin_unlock_irqrestore(&mctz->lock, flags);
}

static unsigned long soft_limit_excess(struct mem_cgroup *memcg)
{
	unsigned long nr_pages = page_counter_read(&memcg->memory);
	unsigned long soft_limit = READ_ONCE(memcg->soft_limit);
	unsigned long excess = 0;

	if (nr_pages > soft_limit)
		excess = nr_pages - soft_limit;

	return excess;
}

static void mem_cgroup_update_tree(struct mem_cgroup *memcg, struct page *page)
{
	unsigned long excess;
	struct mem_cgroup_per_node *mz;
	struct mem_cgroup_tree_per_node *mctz;

	mctz = soft_limit_tree_from_page(page);
	if (!mctz)
		return;
	/*
	 * Necessary to update all ancestors when hierarchy is used.
	 * because their event counter is not touched.
	 */
	for (; memcg; memcg = parent_mem_cgroup(memcg)) {
		mz = mem_cgroup_page_nodeinfo(memcg, page);
		excess = soft_limit_excess(memcg);
		/*
		 * We have to update the tree if mz is on RB-tree or
		 * mem is over its softlimit.
		 */
		if (excess || mz->on_tree) {
			unsigned long flags;

			spin_lock_irqsave(&mctz->lock, flags);
			/* if on-tree, remove it */
			if (mz->on_tree)
				__mem_cgroup_remove_exceeded(mz, mctz);
			/*
			 * Insert again. mz->usage_in_excess will be updated.
			 * If excess is 0, no tree ops.
			 */
			__mem_cgroup_insert_exceeded(mz, mctz, excess);
			spin_unlock_irqrestore(&mctz->lock, flags);
		}
	}
}

static void mem_cgroup_remove_from_trees(struct mem_cgroup *memcg)
{
	struct mem_cgroup_tree_per_node *mctz;
	struct mem_cgroup_per_node *mz;
	int nid;

	for_each_node(nid) {
		mz = mem_cgroup_nodeinfo(memcg, nid);
		mctz = soft_limit_tree_node(nid);
		if (mctz)
			mem_cgroup_remove_exceeded(mz, mctz);
	}
}

static struct mem_cgroup_per_node *
__mem_cgroup_largest_soft_limit_node(struct mem_cgroup_tree_per_node *mctz)
{
	struct mem_cgroup_per_node *mz;

retry:
	mz = NULL;
	if (!mctz->rb_rightmost)
		goto done;		/* Nothing to reclaim from */

	mz = rb_entry(mctz->rb_rightmost,
		      struct mem_cgroup_per_node, tree_node);
	/*
	 * Remove the node now but someone else can add it back,
	 * we will to add it back at the end of reclaim to its correct
	 * position in the tree.
	 */
	__mem_cgroup_remove_exceeded(mz, mctz);
	if (!soft_limit_excess(mz->memcg) ||
	    !css_tryget_online(&mz->memcg->css))
		goto retry;
done:
	return mz;
}

static struct mem_cgroup_per_node *
mem_cgroup_largest_soft_limit_node(struct mem_cgroup_tree_per_node *mctz)
{
	struct mem_cgroup_per_node *mz;

	spin_lock_irq(&mctz->lock);
	mz = __mem_cgroup_largest_soft_limit_node(mctz);
	spin_unlock_irq(&mctz->lock);
	return mz;
}

/*
 * Return page count for single (non recursive) @memcg.
 *
 * Implementation Note: reading percpu statistics for memcg.
 *
 * Both of vmstat[] and percpu_counter has threshold and do periodic
 * synchronization to implement "quick" read. There are trade-off between
 * reading cost and precision of value. Then, we may have a chance to implement
 * a periodic synchronization of counter in memcg's counter.
 *
 * But this _read() function is used for user interface now. The user accounts
 * memory usage by memory cgroup and he _always_ requires exact value because
 * he accounts memory. Even if we provide quick-and-fuzzy read, we always
 * have to visit all online cpus and make sum. So, for now, unnecessary
 * synchronization is not implemented. (just implemented for cpu hotplug)
 *
 * If there are kernel internal actions which can make use of some not-exact
 * value, and reading all cpu value can be performance bottleneck in some
 * common workload, threshold and synchronization as vmstat[] should be
 * implemented.
 *
 * The parameter idx can be of type enum memcg_event_item or vm_event_item.
 */

static unsigned long memcg_sum_events(struct mem_cgroup *memcg,
				      int event)
{
	unsigned long val = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		val += per_cpu(memcg->stat->events[event], cpu);
	return val;
}

static void mem_cgroup_charge_statistics(struct mem_cgroup *memcg,
					 struct page *page,
					 bool compound, int nr_pages)
{
	/*
	 * Here, RSS means 'mapped anon' and anon's SwapCache. Shmem/tmpfs is
	 * counted as CACHE even if it's on ANON LRU.
	 */
	if (PageAnon(page))
		__this_cpu_add(memcg->stat->count[MEMCG_RSS], nr_pages);
	else {
		__this_cpu_add(memcg->stat->count[MEMCG_CACHE], nr_pages);
		if (PageSwapBacked(page))
			__this_cpu_add(memcg->stat->count[NR_SHMEM], nr_pages);
	}

	if (compound) {
		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
		__this_cpu_add(memcg->stat->count[MEMCG_RSS_HUGE], nr_pages);
	}

	/* pagein of a big page is an event. So, ignore page size */
	if (nr_pages > 0)
		__this_cpu_inc(memcg->stat->events[PGPGIN]);
	else {
		__this_cpu_inc(memcg->stat->events[PGPGOUT]);
		nr_pages = -nr_pages; /* for event */
	}

	__this_cpu_add(memcg->stat->nr_page_events, nr_pages);
}

unsigned long mem_cgroup_node_nr_lru_pages(struct mem_cgroup *memcg,
					   int nid, unsigned int lru_mask)
{
	struct lruvec *lruvec = mem_cgroup_lruvec(NODE_DATA(nid), memcg);
	unsigned long nr = 0;
	enum lru_list lru;

	VM_BUG_ON((unsigned)nid >= nr_node_ids);

	for_each_lru(lru) {
		if (!(BIT(lru) & lru_mask))
			continue;
		nr += mem_cgroup_get_lru_size(lruvec, lru);
	}
	return nr;
}

static unsigned long mem_cgroup_nr_lru_pages(struct mem_cgroup *memcg,
			unsigned int lru_mask)
{
	unsigned long nr = 0;
	int nid;

	for_each_node_state(nid, N_MEMORY)
		nr += mem_cgroup_node_nr_lru_pages(memcg, nid, lru_mask);
	return nr;
}

static bool mem_cgroup_event_ratelimit(struct mem_cgroup *memcg,
				       enum mem_cgroup_events_target target)
{
	unsigned long val, next;

	val = __this_cpu_read(memcg->stat->nr_page_events);
	next = __this_cpu_read(memcg->stat->targets[target]);
	/* from time_after() in jiffies.h */
	if ((long)(next - val) < 0) {
		switch (target) {
		case MEM_CGROUP_TARGET_THRESH:
			next = val + THRESHOLDS_EVENTS_TARGET;
			break;
		case MEM_CGROUP_TARGET_SOFTLIMIT:
			next = val + SOFTLIMIT_EVENTS_TARGET;
			break;
		case MEM_CGROUP_TARGET_NUMAINFO:
			next = val + NUMAINFO_EVENTS_TARGET;
			break;
		default:
			break;
		}
		__this_cpu_write(memcg->stat->targets[target], next);
		return true;
	}
	return false;
}

/*
 * Check events in order.
 *
 */
static void memcg_check_events(struct mem_cgroup *memcg, struct page *page)
{
	/* threshold event is triggered in finer grain than soft limit */
	if (unlikely(mem_cgroup_event_ratelimit(memcg,
						MEM_CGROUP_TARGET_THRESH))) {
		bool do_softlimit;
		bool do_numainfo __maybe_unused;

		do_softlimit = mem_cgroup_event_ratelimit(memcg,
						MEM_CGROUP_TARGET_SOFTLIMIT);
#if MAX_NUMNODES > 1
		do_numainfo = mem_cgroup_event_ratelimit(memcg,
						MEM_CGROUP_TARGET_NUMAINFO);
#endif
		mem_cgroup_threshold(memcg);
		if (unlikely(do_softlimit))
			mem_cgroup_update_tree(memcg, page);
#if MAX_NUMNODES > 1
		if (unlikely(do_numainfo))
			atomic_inc(&memcg->numainfo_events);
#endif
	}
}

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

static struct mem_cgroup *get_mem_cgroup_from_mm(struct mm_struct *mm)
{
	struct mem_cgroup *memcg = NULL;

	rcu_read_lock();
	do {
		/*
		 * Page cache insertions can happen withou an
		 * actual mm context, e.g. during disk probing
		 * on boot, loopback IO, acct() writes etc.
		 */
		if (unlikely(!mm))
			memcg = root_mem_cgroup;
		else {
			memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
			if (unlikely(!memcg))
				memcg = root_mem_cgroup;
		}
	} while (!css_tryget_online(&memcg->css));
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
 * Reclaimers can specify a zone and a priority level in @reclaim to
 * divide up the memcgs in the hierarchy among all concurrent
 * reclaimers operating on the same zone and priority.
 */
struct mem_cgroup *mem_cgroup_iter(struct mem_cgroup *root,
				   struct mem_cgroup *prev,
				   struct mem_cgroup_reclaim_cookie *reclaim)
{
	struct mem_cgroup_reclaim_iter *uninitialized_var(iter);
	struct cgroup_subsys_state *css = NULL;
	struct mem_cgroup *memcg = NULL;
	struct mem_cgroup *pos = NULL;

	if (mem_cgroup_disabled())
		return NULL;

	if (!root)
		root = root_mem_cgroup;

	if (prev && !reclaim)
		pos = prev;

	if (!root->use_hierarchy && root != root_mem_cgroup) {
		if (prev)
			goto out;
		return root;
	}

	rcu_read_lock();

	if (reclaim) {
		struct mem_cgroup_per_node *mz;

		mz = mem_cgroup_nodeinfo(root, reclaim->pgdat->node_id);
		iter = &mz->iter[reclaim->priority];

		if (prev && reclaim->generation != iter->generation)
			goto out_unlock;

		while (1) {
			pos = READ_ONCE(iter->position);
			if (!pos || css_tryget(&pos->css))
				break;
			/*
			 * css reference reached zero, so iter->position will
			 * be cleared by ->css_released. However, we should not
			 * rely on this happening soon, because ->css_released
			 * is called from a work queue, and by busy-waiting we
			 * might block it. So we clear iter->position right
			 * away.
			 */
			(void)cmpxchg(&iter->position, pos, NULL);
		}
	}

	if (pos)
		css = &pos->css;

	for (;;) {
		css = css_next_descendant_pre(css, &root->css);
		if (!css) {
			/*
			 * Reclaimers share the hierarchy walk, and a
			 * new one might jump in right at the end of
			 * the hierarchy - make sure they see at least
			 * one group and restart from the beginning.
			 */
			if (!prev)
				continue;
			break;
		}

		/*
		 * Verify the css and acquire a reference.  The root
		 * is provided by the caller, so we know it's alive
		 * and kicking, and don't take an extra reference.
		 */
		memcg = mem_cgroup_from_css(css);

		if (css == &root->css)
			break;

		if (css_tryget(css))
			break;

		memcg = NULL;
	}

	if (reclaim) {
		/*
		 * The position could have already been updated by a competing
		 * thread, so check that the value hasn't changed since we read
		 * it to avoid reclaiming from the same cgroup twice.
		 */
		(void)cmpxchg(&iter->position, pos, memcg);

		if (pos)
			css_put(&pos->css);

		if (!memcg)
			iter->generation++;
		else if (!prev)
			reclaim->generation = iter->generation;
	}

out_unlock:
	rcu_read_unlock();
out:
	if (prev && prev != root)
		css_put(&prev->css);

	return memcg;
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

static void invalidate_reclaim_iterators(struct mem_cgroup *dead_memcg)
{
	struct mem_cgroup *memcg = dead_memcg;
	struct mem_cgroup_reclaim_iter *iter;
	struct mem_cgroup_per_node *mz;
	int nid;
	int i;

	while ((memcg = parent_mem_cgroup(memcg))) {
		for_each_node(nid) {
			mz = mem_cgroup_nodeinfo(memcg, nid);
			for (i = 0; i <= DEF_PRIORITY; i++) {
				iter = &mz->iter[i];
				cmpxchg(&iter->position,
					dead_memcg, NULL);
			}
		}
	}
}

/*
 * Iteration constructs for visiting all cgroups (under a tree).  If
 * loops are exited prematurely (break), mem_cgroup_iter_break() must
 * be used for reference counting.
 */
#define for_each_mem_cgroup_tree(iter, root)		\
	for (iter = mem_cgroup_iter(root, NULL, NULL);	\
	     iter != NULL;				\
	     iter = mem_cgroup_iter(root, iter, NULL))

#define for_each_mem_cgroup(iter)			\
	for (iter = mem_cgroup_iter(NULL, NULL, NULL);	\
	     iter != NULL;				\
	     iter = mem_cgroup_iter(NULL, iter, NULL))

/**
 * mem_cgroup_scan_tasks - iterate over tasks of a memory cgroup hierarchy
 * @memcg: hierarchy root
 * @fn: function to call for each task
 * @arg: argument passed to @fn
 *
 * This function iterates over tasks attached to @memcg or to any of its
 * descendants and calls @fn for each task. If @fn returns a non-zero
 * value, the function breaks the iteration loop and returns the value.
 * Otherwise, it will iterate over all tasks and return 0.
 *
 * This function must not be called for the root memory cgroup.
 */
int mem_cgroup_scan_tasks(struct mem_cgroup *memcg,
			  int (*fn)(struct task_struct *, void *), void *arg)
{
	struct mem_cgroup *iter;
	int ret = 0;

	BUG_ON(memcg == root_mem_cgroup);

	for_each_mem_cgroup_tree(iter, memcg) {
		struct css_task_iter it;
		struct task_struct *task;

		css_task_iter_start(&iter->css, 0, &it);
		while (!ret && (task = css_task_iter_next(&it)))
			ret = fn(task, arg);
		css_task_iter_end(&it);
		if (ret) {
			mem_cgroup_iter_break(memcg, iter);
			break;
		}
	}
	return ret;
}

/**
 * mem_cgroup_page_lruvec - return lruvec for isolating/putting an LRU page
 * @page: the page
 * @zone: zone of the page
 *
 * This function is only safe when following the LRU page isolation
 * and putback protocol: the LRU lock must be held, and the page must
 * either be PageLRU() or the caller must have isolated/allocated it.
 */
struct lruvec *mem_cgroup_page_lruvec(struct page *page, struct pglist_data *pgdat)
{
	struct mem_cgroup_per_node *mz;
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;

	if (mem_cgroup_disabled()) {
		lruvec = &pgdat->lruvec;
		goto out;
	}

	memcg = page->mem_cgroup;
	/*
	 * Swapcache readahead pages are added to the LRU - and
	 * possibly migrated - before they are charged.
	 */
	if (!memcg)
		memcg = root_mem_cgroup;

	mz = mem_cgroup_page_nodeinfo(memcg, page);
	lruvec = &mz->lruvec;
out:
	/*
	 * Since a node can be onlined after the mem_cgroup was created,
	 * we have to be prepared to initialize lruvec->zone here;
	 * and if offlined then reonlined, we need to reinitialize it.
	 */
	if (unlikely(lruvec->pgdat != pgdat))
		lruvec->pgdat = pgdat;
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
 * to or just after a page is removed from an lru list (that ordering being
 * so as to allow it to check that lru_size 0 is consistent with list_empty).
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

bool task_in_mem_cgroup(struct task_struct *task, struct mem_cgroup *memcg)
{
	struct mem_cgroup *task_memcg;
	struct task_struct *p;
	bool ret;

	p = find_lock_task_mm(task);
	if (p) {
		task_memcg = get_mem_cgroup_from_mm(p->mm);
		task_unlock(p);
	} else {
		/*
		 * All threads may have already detached their mm's, but the oom
		 * killer still needs to detect if they have already been oom
		 * killed to prevent needlessly killing additional tasks.
		 */
		rcu_read_lock();
		task_memcg = mem_cgroup_from_task(task);
		css_get(&task_memcg->css);
		rcu_read_unlock();
	}
	ret = mem_cgroup_is_descendant(task_memcg, memcg);
	css_put(&task_memcg->css);
	return ret;
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
	limit = READ_ONCE(memcg->memory.limit);
	if (count < limit)
		margin = limit - count;

	if (do_memsw_account()) {
		count = page_counter_read(&memcg->memsw);
		limit = READ_ONCE(memcg->memsw.limit);
		if (count <= limit)
			margin = min(margin, limit - count);
		else
			margin = 0;
	}

	return margin;
}

/*
 * A routine for checking "mem" is under move_account() or not.
 *
 * Checking a cgroup is mc.from or mc.to or under hierarchy of
 * moving cgroups. This is for waiting at high-memory pressure
 * caused by "move".
 */
static bool mem_cgroup_under_move(struct mem_cgroup *memcg)
{
	struct mem_cgroup *from;
	struct mem_cgroup *to;
	bool ret = false;
	/*
	 * Unlike task_move routines, we access mc.to, mc.from not under
	 * mutual exclusion by cgroup_mutex. Here, we take spinlock instead.
	 */
	spin_lock(&mc.lock);
	from = mc.from;
	to = mc.to;
	if (!from)
		goto unlock;

	ret = mem_cgroup_is_descendant(from, memcg) ||
		mem_cgroup_is_descendant(to, memcg);
unlock:
	spin_unlock(&mc.lock);
	return ret;
}

static bool mem_cgroup_wait_acct_move(struct mem_cgroup *memcg)
{
	if (mc.moving_task && current != mc.moving_task) {
		if (mem_cgroup_under_move(memcg)) {
			DEFINE_WAIT(wait);
			prepare_to_wait(&mc.waitq, &wait, TASK_INTERRUPTIBLE);
			/* moving charge context might have finished. */
			if (mc.moving_task)
				schedule();
			finish_wait(&mc.waitq, &wait);
			return true;
		}
	}
	return false;
}

unsigned int memcg1_stats[] = {
	MEMCG_CACHE,
	MEMCG_RSS,
	MEMCG_RSS_HUGE,
	NR_SHMEM,
	NR_FILE_MAPPED,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	MEMCG_SWAP,
};

static const char *const memcg1_stat_names[] = {
	"cache",
	"rss",
	"rss_huge",
	"shmem",
	"mapped_file",
	"dirty",
	"writeback",
	"swap",
};

#define K(x) ((x) << (PAGE_SHIFT-10))
/**
 * mem_cgroup_print_oom_info: Print OOM information relevant to memory controller.
 * @memcg: The memory cgroup that went over limit
 * @p: Task that is going to be killed
 *
 * NOTE: @memcg and @p's mem_cgroup can be different when hierarchy is
 * enabled
 */
void mem_cgroup_print_oom_info(struct mem_cgroup *memcg, struct task_struct *p)
{
	struct mem_cgroup *iter;
	unsigned int i;

	rcu_read_lock();

	if (p) {
		pr_info("Task in ");
		pr_cont_cgroup_path(task_cgroup(p, memory_cgrp_id));
		pr_cont(" killed as a result of limit of ");
	} else {
		pr_info("Memory limit reached of cgroup ");
	}

	pr_cont_cgroup_path(memcg->css.cgroup);
	pr_cont("\n");

	rcu_read_unlock();

	pr_info("memory: usage %llukB, limit %llukB, failcnt %lu\n",
		K((u64)page_counter_read(&memcg->memory)),
		K((u64)memcg->memory.limit), memcg->memory.failcnt);
	pr_info("memory+swap: usage %llukB, limit %llukB, failcnt %lu\n",
		K((u64)page_counter_read(&memcg->memsw)),
		K((u64)memcg->memsw.limit), memcg->memsw.failcnt);
	pr_info("kmem: usage %llukB, limit %llukB, failcnt %lu\n",
		K((u64)page_counter_read(&memcg->kmem)),
		K((u64)memcg->kmem.limit), memcg->kmem.failcnt);

	for_each_mem_cgroup_tree(iter, memcg) {
		pr_info("Memory cgroup stats for ");
		pr_cont_cgroup_path(iter->css.cgroup);
		pr_cont(":");

		for (i = 0; i < ARRAY_SIZE(memcg1_stats); i++) {
			if (memcg1_stats[i] == MEMCG_SWAP && !do_swap_account)
				continue;
			pr_cont(" %s:%luKB", memcg1_stat_names[i],
				K(memcg_page_state(iter, memcg1_stats[i])));
		}

		for (i = 0; i < NR_LRU_LISTS; i++)
			pr_cont(" %s:%luKB", mem_cgroup_lru_names[i],
				K(mem_cgroup_nr_lru_pages(iter, BIT(i))));

		pr_cont("\n");
	}
}

/*
 * This function returns the number of memcg under hierarchy tree. Returns
 * 1(self count) if no children.
 */
static int mem_cgroup_count_children(struct mem_cgroup *memcg)
{
	int num = 0;
	struct mem_cgroup *iter;

	for_each_mem_cgroup_tree(iter, memcg)
		num++;
	return num;
}

/*
 * Return the memory (and swap, if configured) limit for a memcg.
 */
unsigned long mem_cgroup_get_limit(struct mem_cgroup *memcg)
{
	unsigned long limit;

	limit = memcg->memory.limit;
	if (mem_cgroup_swappiness(memcg)) {
		unsigned long memsw_limit;
		unsigned long swap_limit;

		memsw_limit = memcg->memsw.limit;
		swap_limit = memcg->swap.limit;
		swap_limit = min(swap_limit, (unsigned long)total_swap_pages);
		limit = min(limit + swap_limit, memsw_limit);
	}
	return limit;
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
	bool ret;

	mutex_lock(&oom_lock);
	ret = out_of_memory(&oc);
	mutex_unlock(&oom_lock);
	return ret;
}

#if MAX_NUMNODES > 1

/**
 * test_mem_cgroup_node_reclaimable
 * @memcg: the target memcg
 * @nid: the node ID to be checked.
 * @noswap : specify true here if the user wants flle only information.
 *
 * This function returns whether the specified memcg contains any
 * reclaimable pages on a node. Returns true if there are any reclaimable
 * pages in the node.
 */
static bool test_mem_cgroup_node_reclaimable(struct mem_cgroup *memcg,
		int nid, bool noswap)
{
	if (mem_cgroup_node_nr_lru_pages(memcg, nid, LRU_ALL_FILE))
		return true;
	if (noswap || !total_swap_pages)
		return false;
	if (mem_cgroup_node_nr_lru_pages(memcg, nid, LRU_ALL_ANON))
		return true;
	return false;

}

/*
 * Always updating the nodemask is not very good - even if we have an empty
 * list or the wrong list here, we can start from some node and traverse all
 * nodes based on the zonelist. So update the list loosely once per 10 secs.
 *
 */
static void mem_cgroup_may_update_nodemask(struct mem_cgroup *memcg)
{
	int nid;
	/*
	 * numainfo_events > 0 means there was at least NUMAINFO_EVENTS_TARGET
	 * pagein/pageout changes since the last update.
	 */
	if (!atomic_read(&memcg->numainfo_events))
		return;
	if (atomic_inc_return(&memcg->numainfo_updating) > 1)
		return;

	/* make a nodemask where this memcg uses memory from */
	memcg->scan_nodes = node_states[N_MEMORY];

	for_each_node_mask(nid, node_states[N_MEMORY]) {

		if (!test_mem_cgroup_node_reclaimable(memcg, nid, false))
			node_clear(nid, memcg->scan_nodes);
	}

	atomic_set(&memcg->numainfo_events, 0);
	atomic_set(&memcg->numainfo_updating, 0);
}

/*
 * Selecting a node where we start reclaim from. Because what we need is just
 * reducing usage counter, start from anywhere is O,K. Considering
 * memory reclaim from current node, there are pros. and cons.
 *
 * Freeing memory from current node means freeing memory from a node which
 * we'll use or we've used. So, it may make LRU bad. And if several threads
 * hit limits, it will see a contention on a node. But freeing from remote
 * node means more costs for memory reclaim because of memory latency.
 *
 * Now, we use round-robin. Better algorithm is welcomed.
 */
int mem_cgroup_select_victim_node(struct mem_cgroup *memcg)
{
	int node;

	mem_cgroup_may_update_nodemask(memcg);
	node = memcg->last_scanned_node;

	node = next_node_in(node, memcg->scan_nodes);
	/*
	 * mem_cgroup_may_update_nodemask might have seen no reclaimmable pages
	 * last time it really checked all the LRUs due to rate limiting.
	 * Fallback to the current node in that case for simplicity.
	 */
	if (unlikely(node == MAX_NUMNODES))
		node = numa_node_id();

	memcg->last_scanned_node = node;
	return node;
}
#else
int mem_cgroup_select_victim_node(struct mem_cgroup *memcg)
{
	return 0;
}
#endif

static int mem_cgroup_soft_reclaim(struct mem_cgroup *root_memcg,
				   pg_data_t *pgdat,
				   gfp_t gfp_mask,
				   unsigned long *total_scanned)
{
	struct mem_cgroup *victim = NULL;
	int total = 0;
	int loop = 0;
	unsigned long excess;
	unsigned long nr_scanned;
	struct mem_cgroup_reclaim_cookie reclaim = {
		.pgdat = pgdat,
		.priority = 0,
	};

	excess = soft_limit_excess(root_memcg);

	while (1) {
		victim = mem_cgroup_iter(root_memcg, victim, &reclaim);
		if (!victim) {
			loop++;
			if (loop >= 2) {
				/*
				 * If we have not been able to reclaim
				 * anything, it might because there are
				 * no reclaimable pages under this hierarchy
				 */
				if (!total)
					break;
				/*
				 * We want to do more targeted reclaim.
				 * excess >> 2 is not to excessive so as to
				 * reclaim too much, nor too less that we keep
				 * coming back to reclaim from this cgroup
				 */
				if (total >= (excess >> 2) ||
					(loop > MEM_CGROUP_MAX_RECLAIM_LOOPS))
					break;
			}
			continue;
		}
		total += mem_cgroup_shrink_node(victim, gfp_mask, false,
					pgdat, &nr_scanned);
		*total_scanned += nr_scanned;
		if (!soft_limit_excess(root_memcg))
			break;
	}
	mem_cgroup_iter_break(root_memcg, victim);
	return total;
}

#ifdef CONFIG_LOCKDEP
static struct lockdep_map memcg_oom_lock_dep_map = {
	.name = "memcg_oom_lock",
};
#endif

static DEFINE_SPINLOCK(memcg_oom_lock);

/*
 * Check OOM-Killer is already running under our hierarchy.
 * If someone is running, return false.
 */
static bool mem_cgroup_oom_trylock(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter, *failed = NULL;

	spin_lock(&memcg_oom_lock);

	for_each_mem_cgroup_tree(iter, memcg) {
		if (iter->oom_lock) {
			/*
			 * this subtree of our hierarchy is already locked
			 * so we cannot give a lock.
			 */
			failed = iter;
			mem_cgroup_iter_break(memcg, iter);
			break;
		} else
			iter->oom_lock = true;
	}

	if (failed) {
		/*
		 * OK, we failed to lock the whole subtree so we have
		 * to clean up what we set up to the failing subtree
		 */
		for_each_mem_cgroup_tree(iter, memcg) {
			if (iter == failed) {
				mem_cgroup_iter_break(memcg, iter);
				break;
			}
			iter->oom_lock = false;
		}
	} else
		mutex_acquire(&memcg_oom_lock_dep_map, 0, 1, _RET_IP_);

	spin_unlock(&memcg_oom_lock);

	return !failed;
}

static void mem_cgroup_oom_unlock(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	spin_lock(&memcg_oom_lock);
	mutex_release(&memcg_oom_lock_dep_map, 1, _RET_IP_);
	for_each_mem_cgroup_tree(iter, memcg)
		iter->oom_lock = false;
	spin_unlock(&memcg_oom_lock);
}

static void mem_cgroup_mark_under_oom(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	spin_lock(&memcg_oom_lock);
	for_each_mem_cgroup_tree(iter, memcg)
		iter->under_oom++;
	spin_unlock(&memcg_oom_lock);
}

static void mem_cgroup_unmark_under_oom(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	/*
	 * When a new child is created while the hierarchy is under oom,
	 * mem_cgroup_oom_lock() may not be called. Watch for underflow.
	 */
	spin_lock(&memcg_oom_lock);
	for_each_mem_cgroup_tree(iter, memcg)
		if (iter->under_oom > 0)
			iter->under_oom--;
	spin_unlock(&memcg_oom_lock);
}

static DECLARE_WAIT_QUEUE_HEAD(memcg_oom_waitq);

struct oom_wait_info {
	struct mem_cgroup *memcg;
	wait_queue_entry_t	wait;
};

static int memcg_oom_wake_function(wait_queue_entry_t *wait,
	unsigned mode, int sync, void *arg)
{
	struct mem_cgroup *wake_memcg = (struct mem_cgroup *)arg;
	struct mem_cgroup *oom_wait_memcg;
	struct oom_wait_info *oom_wait_info;

	oom_wait_info = container_of(wait, struct oom_wait_info, wait);
	oom_wait_memcg = oom_wait_info->memcg;

	if (!mem_cgroup_is_descendant(wake_memcg, oom_wait_memcg) &&
	    !mem_cgroup_is_descendant(oom_wait_memcg, wake_memcg))
		return 0;
	return autoremove_wake_function(wait, mode, sync, arg);
}

static void memcg_oom_recover(struct mem_cgroup *memcg)
{
	/*
	 * For the following lockless ->under_oom test, the only required
	 * guarantee is that it must see the state asserted by an OOM when
	 * this function is called as a result of userland actions
	 * triggered by the notification of the OOM.  This is trivially
	 * achieved by invoking mem_cgroup_mark_under_oom() before
	 * triggering notification.
	 */
	if (memcg && memcg->under_oom)
		__wake_up(&memcg_oom_waitq, TASK_NORMAL, 0, memcg);
}

static void mem_cgroup_oom(struct mem_cgroup *memcg, gfp_t mask, int order)
{
	if (!current->memcg_may_oom)
		return;
	/*
	 * We are in the middle of the charge context here, so we
	 * don't want to block when potentially sitting on a callstack
	 * that holds all kinds of filesystem and mm locks.
	 *
	 * Also, the caller may handle a failed allocation gracefully
	 * (like optional page cache readahead) and so an OOM killer
	 * invocation might not even be necessary.
	 *
	 * That's why we don't do anything here except remember the
	 * OOM context and then deal with it at the end of the page
	 * fault when the stack is unwound, the locks are released,
	 * and when we know whether the fault was overall successful.
	 */
	css_get(&memcg->css);
	current->memcg_in_oom = memcg;
	current->memcg_oom_gfp_mask = mask;
	current->memcg_oom_order = order;
}

/**
 * mem_cgroup_oom_synchronize - complete memcg OOM handling
 * @handle: actually kill/wait or just clean up the OOM state
 *
 * This has to be called at the end of a page fault if the memcg OOM
 * handler was enabled.
 *
 * Memcg supports userspace OOM handling where failed allocations must
 * sleep on a waitqueue until the userspace task resolves the
 * situation.  Sleeping directly in the charge context with all kinds
 * of locks held is not a good idea, instead we remember an OOM state
 * in the task and mem_cgroup_oom_synchronize() has to be called at
 * the end of the page fault to complete the OOM handling.
 *
 * Returns %true if an ongoing memcg OOM situation was detected and
 * completed, %false otherwise.
 */
bool mem_cgroup_oom_synchronize(bool handle)
{
	struct mem_cgroup *memcg = current->memcg_in_oom;
	struct oom_wait_info owait;
	bool locked;

	/* OOM is global, do not handle */
	if (!memcg)
		return false;

	if (!handle)
		goto cleanup;

	owait.memcg = memcg;
	owait.wait.flags = 0;
	owait.wait.func = memcg_oom_wake_function;
	owait.wait.private = current;
	INIT_LIST_HEAD(&owait.wait.entry);

	prepare_to_wait(&memcg_oom_waitq, &owait.wait, TASK_KILLABLE);
	mem_cgroup_mark_under_oom(memcg);

	locked = mem_cgroup_oom_trylock(memcg);

	if (locked)
		mem_cgroup_oom_notify(memcg);

	if (locked && !memcg->oom_kill_disable) {
		mem_cgroup_unmark_under_oom(memcg);
		finish_wait(&memcg_oom_waitq, &owait.wait);
		mem_cgroup_out_of_memory(memcg, current->memcg_oom_gfp_mask,
					 current->memcg_oom_order);
	} else {
		schedule();
		mem_cgroup_unmark_under_oom(memcg);
		finish_wait(&memcg_oom_waitq, &owait.wait);
	}

	if (locked) {
		mem_cgroup_oom_unlock(memcg);
		/*
		 * There is no guarantee that an OOM-lock contender
		 * sees the wakeups triggered by the OOM kill
		 * uncharges.  Wake any sleepers explicitely.
		 */
		memcg_oom_recover(memcg);
	}
cleanup:
	current->memcg_in_oom = NULL;
	css_put(&memcg->css);
	return true;
}

/**
 * lock_page_memcg - lock a page->mem_cgroup binding
 * @page: the page
 *
 * This function protects unlocked LRU pages from being moved to
 * another cgroup.
 *
 * It ensures lifetime of the returned memcg. Caller is responsible
 * for the lifetime of the page; __unlock_page_memcg() is available
 * when @page might get freed inside the locked section.
 */
struct mem_cgroup *lock_page_memcg(struct page *page)
{
	struct mem_cgroup *memcg;
	unsigned long flags;

	/*
	 * The RCU lock is held throughout the transaction.  The fast
	 * path can get away without acquiring the memcg->move_lock
	 * because page moving starts with an RCU grace period.
	 *
	 * The RCU lock also protects the memcg from being freed when
	 * the page state that is going to change is the only thing
	 * preventing the page itself from being freed. E.g. writeback
	 * doesn't hold a page reference and relies on PG_writeback to
	 * keep off truncation, migration and so forth.
         */
	rcu_read_lock();

	if (mem_cgroup_disabled())
		return NULL;
again:
	memcg = page->mem_cgroup;
	if (unlikely(!memcg))
		return NULL;

	if (atomic_read(&memcg->moving_account) <= 0)
		return memcg;

	spin_lock_irqsave(&memcg->move_lock, flags);
	if (memcg != page->mem_cgroup) {
		spin_unlock_irqrestore(&memcg->move_lock, flags);
		goto again;
	}

	/*
	 * When charge migration first begins, we can have locked and
	 * unlocked page stat updates happening concurrently.  Track
	 * the task who has the lock for unlock_page_memcg().
	 */
	memcg->move_lock_task = current;
	memcg->move_lock_flags = flags;

	return memcg;
}
EXPORT_SYMBOL(lock_page_memcg);

/**
 * __unlock_page_memcg - unlock and unpin a memcg
 * @memcg: the memcg
 *
 * Unlock and unpin a memcg returned by lock_page_memcg().
 */
void __unlock_page_memcg(struct mem_cgroup *memcg)
{
	if (memcg && memcg->move_lock_task == current) {
		unsigned long flags = memcg->move_lock_flags;

		memcg->move_lock_task = NULL;
		memcg->move_lock_flags = 0;

		spin_unlock_irqrestore(&memcg->move_lock, flags);
	}

	rcu_read_unlock();
}

/**
 * unlock_page_memcg - unlock a page->mem_cgroup binding
 * @page: the page
 */
void unlock_page_memcg(struct page *page)
{
	__unlock_page_memcg(page->mem_cgroup);
}
EXPORT_SYMBOL(unlock_page_memcg);

/*
 * size of first charge trial. "32" comes from vmscan.c's magic value.
 * TODO: maybe necessary to use big numbers in big irons.
 */
#define CHARGE_BATCH	32U
struct memcg_stock_pcp {
	struct mem_cgroup *cached; /* this never be root cgroup */
	unsigned int nr_pages;
	struct work_struct work;
	unsigned long flags;
#define FLUSHING_CACHED_CHARGE	0
};
static DEFINE_PER_CPU(struct memcg_stock_pcp, memcg_stock);
static DEFINE_MUTEX(percpu_charge_mutex);

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
	unsigned long flags;
	bool ret = false;

	if (nr_pages > CHARGE_BATCH)
		return ret;

	local_irq_save(flags);

	stock = this_cpu_ptr(&memcg_stock);
	if (memcg == stock->cached && stock->nr_pages >= nr_pages) {
		stock->nr_pages -= nr_pages;
		ret = true;
	}

	local_irq_restore(flags);

	return ret;
}

/*
 * Returns stocks cached in percpu and reset cached information.
 */
static void drain_stock(struct memcg_stock_pcp *stock)
{
	struct mem_cgroup *old = stock->cached;

	if (stock->nr_pages) {
		page_counter_uncharge(&old->memory, stock->nr_pages);
		if (do_memsw_account())
			page_counter_uncharge(&old->memsw, stock->nr_pages);
		css_put_many(&old->css, stock->nr_pages);
		stock->nr_pages = 0;
	}
	stock->cached = NULL;
}

static void drain_local_stock(struct work_struct *dummy)
{
	struct memcg_stock_pcp *stock;
	unsigned long flags;

	/*
	 * The only protection from memory hotplug vs. drain_stock races is
	 * that we always operate on local CPU stock here with IRQ disabled
	 */
	local_irq_save(flags);

	stock = this_cpu_ptr(&memcg_stock);
	drain_stock(stock);
	clear_bit(FLUSHING_CACHED_CHARGE, &stock->flags);

	local_irq_restore(flags);
}

/*
 * Cache charges(val) to local per_cpu area.
 * This will be consumed by consume_stock() function, later.
 */
static void refill_stock(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	struct memcg_stock_pcp *stock;
	unsigned long flags;

	local_irq_save(flags);

	stock = this_cpu_ptr(&memcg_stock);
	if (stock->cached != memcg) { /* reset if necessary */
		drain_stock(stock);
		stock->cached = memcg;
	}
	stock->nr_pages += nr_pages;

	if (stock->nr_pages > CHARGE_BATCH)
		drain_stock(stock);

	local_irq_restore(flags);
}

/*
 * Drains all per-CPU charge caches for given root_memcg resp. subtree
 * of the hierarchy under it.
 */
static void drain_all_stock(struct mem_cgroup *root_memcg)
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
	curcpu = get_cpu();
	for_each_online_cpu(cpu) {
		struct memcg_stock_pcp *stock = &per_cpu(memcg_stock, cpu);
		struct mem_cgroup *memcg;

		memcg = stock->cached;
		if (!memcg || !stock->nr_pages || !css_tryget(&memcg->css))
			continue;
		if (!mem_cgroup_is_descendant(memcg, root_memcg)) {
			css_put(&memcg->css);
			continue;
		}
		if (!test_and_set_bit(FLUSHING_CACHED_CHARGE, &stock->flags)) {
			if (cpu == curcpu)
				drain_local_stock(&stock->work);
			else
				schedule_work_on(cpu, &stock->work);
		}
		css_put(&memcg->css);
	}
	put_cpu();
	mutex_unlock(&percpu_charge_mutex);
}

static int memcg_hotplug_cpu_dead(unsigned int cpu)
{
	struct memcg_stock_pcp *stock;

	stock = &per_cpu(memcg_stock, cpu);
	drain_stock(stock);
	return 0;
}

static void reclaim_high(struct mem_cgroup *memcg,
			 unsigned int nr_pages,
			 gfp_t gfp_mask)
{
	do {
		if (page_counter_read(&memcg->memory) <= memcg->high)
			continue;
		mem_cgroup_event(memcg, MEMCG_HIGH);
		try_to_free_mem_cgroup_pages(memcg, nr_pages, gfp_mask, true);
	} while ((memcg = parent_mem_cgroup(memcg)));
}

static void high_work_func(struct work_struct *work)
{
	struct mem_cgroup *memcg;

	memcg = container_of(work, struct mem_cgroup, high_work);
	reclaim_high(memcg, CHARGE_BATCH, GFP_KERNEL);
}

/*
 * Scheduled by try_charge() to be executed from the userland return path
 * and reclaims memory over the high limit.
 */
void mem_cgroup_handle_over_high(void)
{
	unsigned int nr_pages = current->memcg_nr_pages_over_high;
	struct mem_cgroup *memcg;

	if (likely(!nr_pages))
		return;

	memcg = get_mem_cgroup_from_mm(current->mm);
	reclaim_high(memcg, nr_pages, GFP_KERNEL);
	css_put(&memcg->css);
	current->memcg_nr_pages_over_high = 0;
}

static int try_charge(struct mem_cgroup *memcg, gfp_t gfp_mask,
		      unsigned int nr_pages)
{
	unsigned int batch = max(CHARGE_BATCH, nr_pages);
	int nr_retries = MEM_CGROUP_RECLAIM_RETRIES;
	struct mem_cgroup *mem_over_limit;
	struct page_counter *counter;
	unsigned long nr_reclaimed;
	bool may_swap = true;
	bool drained = false;

	if (mem_cgroup_is_root(memcg))
		return 0;
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
		may_swap = false;
	}

	if (batch > nr_pages) {
		batch = nr_pages;
		goto retry;
	}

	/*
	 * Unlike in global OOM situations, memcg is not in a physical
	 * memory shortage.  Allow dying and OOM-killed tasks to
	 * bypass the last charges so that they can exit quickly and
	 * free their memory.
	 */
	if (unlikely(tsk_is_oom_victim(current) ||
		     fatal_signal_pending(current) ||
		     current->flags & PF_EXITING))
		goto force;

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

	mem_cgroup_event(mem_over_limit, MEMCG_MAX);

	nr_reclaimed = try_to_free_mem_cgroup_pages(mem_over_limit, nr_pages,
						    gfp_mask, may_swap);

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
	/*
	 * At task move, charge accounts can be doubly counted. So, it's
	 * better to wait until the end of task_move if something is going on.
	 */
	if (mem_cgroup_wait_acct_move(mem_over_limit))
		goto retry;

	if (nr_retries--)
		goto retry;

	if (gfp_mask & __GFP_NOFAIL)
		goto force;

	if (fatal_signal_pending(current))
		goto force;

	mem_cgroup_event(mem_over_limit, MEMCG_OOM);

	mem_cgroup_oom(mem_over_limit, gfp_mask,
		       get_order(nr_pages * PAGE_SIZE));
nomem:
	if (!(gfp_mask & __GFP_NOFAIL))
		return -ENOMEM;
force:
	/*
	 * The allocation either can't fail or will lead to more memory
	 * being freed very soon.  Allow memory usage go over the limit
	 * temporarily by force charging it.
	 */
	page_counter_charge(&memcg->memory, nr_pages);
	if (do_memsw_account())
		page_counter_charge(&memcg->memsw, nr_pages);
	css_get_many(&memcg->css, nr_pages);

	return 0;

done_restock:
	css_get_many(&memcg->css, batch);
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
		if (page_counter_read(&memcg->memory) > memcg->high) {
			/* Don't bother a random interrupted task */
			if (in_interrupt()) {
				schedule_work(&memcg->high_work);
				break;
			}
			current->memcg_nr_pages_over_high += batch;
			set_notify_resume(current);
			break;
		}
	} while ((memcg = parent_mem_cgroup(memcg)));

	return 0;
}

static void cancel_charge(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	if (mem_cgroup_is_root(memcg))
		return;

	page_counter_uncharge(&memcg->memory, nr_pages);
	if (do_memsw_account())
		page_counter_uncharge(&memcg->memsw, nr_pages);

	css_put_many(&memcg->css, nr_pages);
}

static void lock_page_lru(struct page *page, int *isolated)
{
	struct zone *zone = page_zone(page);

	spin_lock_irq(zone_lru_lock(zone));
	if (PageLRU(page)) {
		struct lruvec *lruvec;

		lruvec = mem_cgroup_page_lruvec(page, zone->zone_pgdat);
		ClearPageLRU(page);
		del_page_from_lru_list(page, lruvec, page_lru(page));
		*isolated = 1;
	} else
		*isolated = 0;
}

static void unlock_page_lru(struct page *page, int isolated)
{
	struct zone *zone = page_zone(page);

	if (isolated) {
		struct lruvec *lruvec;

		lruvec = mem_cgroup_page_lruvec(page, zone->zone_pgdat);
		VM_BUG_ON_PAGE(PageLRU(page), page);
		SetPageLRU(page);
		add_page_to_lru_list(page, lruvec, page_lru(page));
	}
	spin_unlock_irq(zone_lru_lock(zone));
}

static void commit_charge(struct page *page, struct mem_cgroup *memcg,
			  bool lrucare)
{
	int isolated;

	VM_BUG_ON_PAGE(page->mem_cgroup, page);

	/*
	 * In some cases, SwapCache and FUSE(splice_buf->radixtree), the page
	 * may already be on some other mem_cgroup's LRU.  Take care of it.
	 */
	if (lrucare)
		lock_page_lru(page, &isolated);

	/*
	 * Nobody should be changing or seriously looking at
	 * page->mem_cgroup at this point:
	 *
	 * - the page is uncharged
	 *
	 * - the page is off-LRU
	 *
	 * - an anonymous fault has exclusive page access, except for
	 *   a locked page table
	 *
	 * - a page cache insertion, a swapin fault, or a migration
	 *   have the page locked
	 */
	page->mem_cgroup = memcg;

	if (lrucare)
		unlock_page_lru(page, isolated);
}

#ifndef CONFIG_SLOB
static int memcg_alloc_cache_id(void)
{
	int id, size;
	int err;

	id = ida_simple_get(&memcg_cache_ida,
			    0, MEMCG_CACHES_MAX_SIZE, GFP_KERNEL);
	if (id < 0)
		return id;

	if (id < memcg_nr_cache_ids)
		return id;

	/*
	 * There's no space for the new id in memcg_caches arrays,
	 * so we have to grow them.
	 */
	down_write(&memcg_cache_ids_sem);

	size = 2 * (id + 1);
	if (size < MEMCG_CACHES_MIN_SIZE)
		size = MEMCG_CACHES_MIN_SIZE;
	else if (size > MEMCG_CACHES_MAX_SIZE)
		size = MEMCG_CACHES_MAX_SIZE;

	err = memcg_update_all_caches(size);
	if (!err)
		err = memcg_update_all_list_lrus(size);
	if (!err)
		memcg_nr_cache_ids = size;

	up_write(&memcg_cache_ids_sem);

	if (err) {
		ida_simple_remove(&memcg_cache_ida, id);
		return err;
	}
	return id;
}

static void memcg_free_cache_id(int id)
{
	ida_simple_remove(&memcg_cache_ida, id);
}

struct memcg_kmem_cache_create_work {
	struct mem_cgroup *memcg;
	struct kmem_cache *cachep;
	struct work_struct work;
};

static void memcg_kmem_cache_create_func(struct work_struct *w)
{
	struct memcg_kmem_cache_create_work *cw =
		container_of(w, struct memcg_kmem_cache_create_work, work);
	struct mem_cgroup *memcg = cw->memcg;
	struct kmem_cache *cachep = cw->cachep;

	memcg_create_kmem_cache(memcg, cachep);

	css_put(&memcg->css);
	kfree(cw);
}

/*
 * Enqueue the creation of a per-memcg kmem_cache.
 */
static void __memcg_schedule_kmem_cache_create(struct mem_cgroup *memcg,
					       struct kmem_cache *cachep)
{
	struct memcg_kmem_cache_create_work *cw;

	cw = kmalloc(sizeof(*cw), GFP_NOWAIT);
	if (!cw)
		return;

	css_get(&memcg->css);

	cw->memcg = memcg;
	cw->cachep = cachep;
	INIT_WORK(&cw->work, memcg_kmem_cache_create_func);

	queue_work(memcg_kmem_cache_wq, &cw->work);
}

static void memcg_schedule_kmem_cache_create(struct mem_cgroup *memcg,
					     struct kmem_cache *cachep)
{
	/*
	 * We need to stop accounting when we kmalloc, because if the
	 * corresponding kmalloc cache is not yet created, the first allocation
	 * in __memcg_schedule_kmem_cache_create will recurse.
	 *
	 * However, it is better to enclose the whole function. Depending on
	 * the debugging options enabled, INIT_WORK(), for instance, can
	 * trigger an allocation. This too, will make us recurse. Because at
	 * this point we can't allow ourselves back into memcg_kmem_get_cache,
	 * the safest choice is to do it like this, wrapping the whole function.
	 */
	current->memcg_kmem_skip_account = 1;
	__memcg_schedule_kmem_cache_create(memcg, cachep);
	current->memcg_kmem_skip_account = 0;
}

static inline bool memcg_kmem_bypass(void)
{
	if (in_interrupt() || !current->mm || (current->flags & PF_KTHREAD))
		return true;
	return false;
}

/**
 * memcg_kmem_get_cache: select the correct per-memcg cache for allocation
 * @cachep: the original global kmem cache
 *
 * Return the kmem_cache we're supposed to use for a slab allocation.
 * We try to use the current memcg's version of the cache.
 *
 * If the cache does not exist yet, if we are the first user of it, we
 * create it asynchronously in a workqueue and let the current allocation
 * go through with the original cache.
 *
 * This function takes a reference to the cache it returns to assure it
 * won't get destroyed while we are working with it. Once the caller is
 * done with it, memcg_kmem_put_cache() must be called to release the
 * reference.
 */
struct kmem_cache *memcg_kmem_get_cache(struct kmem_cache *cachep)
{
	struct mem_cgroup *memcg;
	struct kmem_cache *memcg_cachep;
	int kmemcg_id;

	VM_BUG_ON(!is_root_cache(cachep));

	if (memcg_kmem_bypass())
		return cachep;

	if (current->memcg_kmem_skip_account)
		return cachep;

	memcg = get_mem_cgroup_from_mm(current->mm);
	kmemcg_id = READ_ONCE(memcg->kmemcg_id);
	if (kmemcg_id < 0)
		goto out;

	memcg_cachep = cache_from_memcg_idx(cachep, kmemcg_id);
	if (likely(memcg_cachep))
		return memcg_cachep;

	/*
	 * If we are in a safe context (can wait, and not in interrupt
	 * context), we could be be predictable and return right away.
	 * This would guarantee that the allocation being performed
	 * already belongs in the new cache.
	 *
	 * However, there are some clashes that can arrive from locking.
	 * For instance, because we acquire the slab_mutex while doing
	 * memcg_create_kmem_cache, this means no further allocation
	 * could happen with the slab_mutex held. So it's better to
	 * defer everything.
	 */
	memcg_schedule_kmem_cache_create(memcg, cachep);
out:
	css_put(&memcg->css);
	return cachep;
}

/**
 * memcg_kmem_put_cache: drop reference taken by memcg_kmem_get_cache
 * @cachep: the cache returned by memcg_kmem_get_cache
 */
void memcg_kmem_put_cache(struct kmem_cache *cachep)
{
	if (!is_root_cache(cachep))
		css_put(&cachep->memcg_params.memcg->css);
}

/**
 * memcg_kmem_charge: charge a kmem page
 * @page: page to charge
 * @gfp: reclaim mode
 * @order: allocation order
 * @memcg: memory cgroup to charge
 *
 * Returns 0 on success, an error code on failure.
 */
int memcg_kmem_charge_memcg(struct page *page, gfp_t gfp, int order,
			    struct mem_cgroup *memcg)
{
	unsigned int nr_pages = 1 << order;
	struct page_counter *counter;
	int ret;

	ret = try_charge(memcg, gfp, nr_pages);
	if (ret)
		return ret;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) &&
	    !page_counter_try_charge(&memcg->kmem, nr_pages, &counter)) {
		cancel_charge(memcg, nr_pages);
		return -ENOMEM;
	}

	page->mem_cgroup = memcg;

	return 0;
}

/**
 * memcg_kmem_charge: charge a kmem page to the current memory cgroup
 * @page: page to charge
 * @gfp: reclaim mode
 * @order: allocation order
 *
 * Returns 0 on success, an error code on failure.
 */
int memcg_kmem_charge(struct page *page, gfp_t gfp, int order)
{
	struct mem_cgroup *memcg;
	int ret = 0;

	if (memcg_kmem_bypass())
		return 0;

	memcg = get_mem_cgroup_from_mm(current->mm);
	if (!mem_cgroup_is_root(memcg)) {
		ret = memcg_kmem_charge_memcg(page, gfp, order, memcg);
		if (!ret)
			__SetPageKmemcg(page);
	}
	css_put(&memcg->css);
	return ret;
}
/**
 * memcg_kmem_uncharge: uncharge a kmem page
 * @page: page to uncharge
 * @order: allocation order
 */
void memcg_kmem_uncharge(struct page *page, int order)
{
	struct mem_cgroup *memcg = page->mem_cgroup;
	unsigned int nr_pages = 1 << order;

	if (!memcg)
		return;

	VM_BUG_ON_PAGE(mem_cgroup_is_root(memcg), page);

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
		page_counter_uncharge(&memcg->kmem, nr_pages);

	page_counter_uncharge(&memcg->memory, nr_pages);
	if (do_memsw_account())
		page_counter_uncharge(&memcg->memsw, nr_pages);

	page->mem_cgroup = NULL;

	/* slab pages do not have PageKmemcg flag set */
	if (PageKmemcg(page))
		__ClearPageKmemcg(page);

	css_put_many(&memcg->css, nr_pages);
}
#endif /* !CONFIG_SLOB */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

/*
 * Because tail pages are not marked as "used", set it. We're under
 * zone_lru_lock and migration entries setup in all page mappings.
 */
void mem_cgroup_split_huge_fixup(struct page *head)
{
	int i;

	if (mem_cgroup_disabled())
		return;

	for (i = 1; i < HPAGE_PMD_NR; i++)
		head[i].mem_cgroup = head->mem_cgroup;

	__this_cpu_sub(head->mem_cgroup->stat->count[MEMCG_RSS_HUGE],
		       HPAGE_PMD_NR);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#ifdef CONFIG_MEMCG_SWAP
static void mem_cgroup_swap_statistics(struct mem_cgroup *memcg,
				       int nr_entries)
{
	this_cpu_add(memcg->stat->count[MEMCG_SWAP], nr_entries);
}

/**
 * mem_cgroup_move_swap_account - move swap charge and swap_cgroup's record.
 * @entry: swap entry to be moved
 * @from:  mem_cgroup which the entry is moved from
 * @to:  mem_cgroup which the entry is moved to
 *
 * It succeeds only when the swap_cgroup's record for this entry is the same
 * as the mem_cgroup's id of @from.
 *
 * Returns 0 on success, -EINVAL on failure.
 *
 * The caller must have charged to @to, IOW, called page_counter_charge() about
 * both res and memsw, and called css_get().
 */
static int mem_cgroup_move_swap_account(swp_entry_t entry,
				struct mem_cgroup *from, struct mem_cgroup *to)
{
	unsigned short old_id, new_id;

	old_id = mem_cgroup_id(from);
	new_id = mem_cgroup_id(to);

	if (swap_cgroup_cmpxchg(entry, old_id, new_id) == old_id) {
		mem_cgroup_swap_statistics(from, -1);
		mem_cgroup_swap_statistics(to, 1);
		return 0;
	}
	return -EINVAL;
}
#else
static inline int mem_cgroup_move_swap_account(swp_entry_t entry,
				struct mem_cgroup *from, struct mem_cgroup *to)
{
	return -EINVAL;
}
#endif

static DEFINE_MUTEX(memcg_limit_mutex);

static int mem_cgroup_resize_limit(struct mem_cgroup *memcg,
				   unsigned long limit)
{
	unsigned long curusage;
	unsigned long oldusage;
	bool enlarge = false;
	int retry_count;
	int ret;

	/*
	 * For keeping hierarchical_reclaim simple, how long we should retry
	 * is depends on callers. We set our retry-count to be function
	 * of # of children which we should visit in this loop.
	 */
	retry_count = MEM_CGROUP_RECLAIM_RETRIES *
		      mem_cgroup_count_children(memcg);

	oldusage = page_counter_read(&memcg->memory);

	do {
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		mutex_lock(&memcg_limit_mutex);
		if (limit > memcg->memsw.limit) {
			mutex_unlock(&memcg_limit_mutex);
			ret = -EINVAL;
			break;
		}
		if (limit > memcg->memory.limit)
			enlarge = true;
		ret = page_counter_limit(&memcg->memory, limit);
		mutex_unlock(&memcg_limit_mutex);

		if (!ret)
			break;

		try_to_free_mem_cgroup_pages(memcg, 1, GFP_KERNEL, true);

		curusage = page_counter_read(&memcg->memory);
		/* Usage is reduced ? */
		if (curusage >= oldusage)
			retry_count--;
		else
			oldusage = curusage;
	} while (retry_count);

	if (!ret && enlarge)
		memcg_oom_recover(memcg);

	return ret;
}

static int mem_cgroup_resize_memsw_limit(struct mem_cgroup *memcg,
					 unsigned long limit)
{
	unsigned long curusage;
	unsigned long oldusage;
	bool enlarge = false;
	int retry_count;
	int ret;

	/* see mem_cgroup_resize_res_limit */
	retry_count = MEM_CGROUP_RECLAIM_RETRIES *
		      mem_cgroup_count_children(memcg);

	oldusage = page_counter_read(&memcg->memsw);

	do {
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		mutex_lock(&memcg_limit_mutex);
		if (limit < memcg->memory.limit) {
			mutex_unlock(&memcg_limit_mutex);
			ret = -EINVAL;
			break;
		}
		if (limit > memcg->memsw.limit)
			enlarge = true;
		ret = page_counter_limit(&memcg->memsw, limit);
		mutex_unlock(&memcg_limit_mutex);

		if (!ret)
			break;

		try_to_free_mem_cgroup_pages(memcg, 1, GFP_KERNEL, false);

		curusage = page_counter_read(&memcg->memsw);
		/* Usage is reduced ? */
		if (curusage >= oldusage)
			retry_count--;
		else
			oldusage = curusage;
	} while (retry_count);

	if (!ret && enlarge)
		memcg_oom_recover(memcg);

	return ret;
}

unsigned long mem_cgroup_soft_limit_reclaim(pg_data_t *pgdat, int order,
					    gfp_t gfp_mask,
					    unsigned long *total_scanned)
{
	unsigned long nr_reclaimed = 0;
	struct mem_cgroup_per_node *mz, *next_mz = NULL;
	unsigned long reclaimed;
	int loop = 0;
	struct mem_cgroup_tree_per_node *mctz;
	unsigned long excess;
	unsigned long nr_scanned;

	if (order > 0)
		return 0;

	mctz = soft_limit_tree_node(pgdat->node_id);

	/*
	 * Do not even bother to check the largest node if the root
	 * is empty. Do it lockless to prevent lock bouncing. Races
	 * are acceptable as soft limit is best effort anyway.
	 */
	if (!mctz || RB_EMPTY_ROOT(&mctz->rb_root))
		return 0;

	/*
	 * This loop can run a while, specially if mem_cgroup's continuously
	 * keep exceeding their soft limit and putting the system under
	 * pressure
	 */
	do {
		if (next_mz)
			mz = next_mz;
		else
			mz = mem_cgroup_largest_soft_limit_node(mctz);
		if (!mz)
			break;

		nr_scanned = 0;
		reclaimed = mem_cgroup_soft_reclaim(mz->memcg, pgdat,
						    gfp_mask, &nr_scanned);
		nr_reclaimed += reclaimed;
		*total_scanned += nr_scanned;
		spin_lock_irq(&mctz->lock);
		__mem_cgroup_remove_exceeded(mz, mctz);

		/*
		 * If we failed to reclaim anything from this memory cgroup
		 * it is time to move on to the next cgroup
		 */
		next_mz = NULL;
		if (!reclaimed)
			next_mz = __mem_cgroup_largest_soft_limit_node(mctz);

		excess = soft_limit_excess(mz->memcg);
		/*
		 * One school of thought says that we should not add
		 * back the node to the tree if reclaim returns 0.
		 * But our reclaim could return 0, simply because due
		 * to priority we are exposing a smaller subset of
		 * memory to reclaim from. Consider this as a longer
		 * term TODO.
		 */
		/* If excess == 0, no tree ops */
		__mem_cgroup_insert_exceeded(mz, mctz, excess);
		spin_unlock_irq(&mctz->lock);
		css_put(&mz->memcg->css);
		loop++;
		/*
		 * Could not reclaim anything and there are no more
		 * mem cgroups to try or we seem to be looping without
		 * reclaiming anything.
		 */
		if (!nr_reclaimed &&
			(next_mz == NULL ||
			loop > MEM_CGROUP_MAX_SOFT_LIMIT_RECLAIM_LOOPS))
			break;
	} while (!nr_reclaimed);
	if (next_mz)
		css_put(&next_mz->memcg->css);
	return nr_reclaimed;
}

/*
 * Test whether @memcg has children, dead or alive.  Note that this
 * function doesn't care whether @memcg has use_hierarchy enabled and
 * returns %true if there are child csses according to the cgroup
 * hierarchy.  Testing use_hierarchy is the caller's responsiblity.
 */
static inline bool memcg_has_children(struct mem_cgroup *memcg)
{
	bool ret;

	rcu_read_lock();
	ret = css_next_child(NULL, &memcg->css);
	rcu_read_unlock();
	return ret;
}

/*
 * Reclaims as many pages from the given memcg as possible.
 *
 * Caller is responsible for holding css reference for memcg.
 */
static int mem_cgroup_force_empty(struct mem_cgroup *memcg)
{
	int nr_retries = MEM_CGROUP_RECLAIM_RETRIES;

	/* we call try-to-free pages for make this cgroup empty */
	lru_add_drain_all();
	/* try to free all pages in this cgroup */
	while (nr_retries && page_counter_read(&memcg->memory)) {
		int progress;

		if (signal_pending(current))
			return -EINTR;

		progress = try_to_free_mem_cgroup_pages(memcg, 1,
							GFP_KERNEL, true);
		if (!progress) {
			nr_retries--;
			/* maybe some writeback is necessary */
			congestion_wait(BLK_RW_ASYNC, HZ/10);
		}

	}

	return 0;
}

static ssize_t mem_cgroup_force_empty_write(struct kernfs_open_file *of,
					    char *buf, size_t nbytes,
					    loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));

	if (mem_cgroup_is_root(memcg))
		return -EINVAL;
	return mem_cgroup_force_empty(memcg) ?: nbytes;
}

static u64 mem_cgroup_hierarchy_read(struct cgroup_subsys_state *css,
				     struct cftype *cft)
{
	return mem_cgroup_from_css(css)->use_hierarchy;
}

static int mem_cgroup_hierarchy_write(struct cgroup_subsys_state *css,
				      struct cftype *cft, u64 val)
{
	int retval = 0;
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct mem_cgroup *parent_memcg = mem_cgroup_from_css(memcg->css.parent);

	if (memcg->use_hierarchy == val)
		return 0;

	/*
	 * If parent's use_hierarchy is set, we can't make any modifications
	 * in the child subtrees. If it is unset, then the change can
	 * occur, provided the current cgroup has no children.
	 *
	 * For the root cgroup, parent_mem is NULL, we allow value to be
	 * set if there are no children.
	 */
	if ((!parent_memcg || !parent_memcg->use_hierarchy) &&
				(val == 1 || val == 0)) {
		if (!memcg_has_children(memcg))
			memcg->use_hierarchy = val;
		else
			retval = -EBUSY;
	} else
		retval = -EINVAL;

	return retval;
}

static void tree_stat(struct mem_cgroup *memcg, unsigned long *stat)
{
	struct mem_cgroup *iter;
	int i;

	memset(stat, 0, sizeof(*stat) * MEMCG_NR_STAT);

	for_each_mem_cgroup_tree(iter, memcg) {
		for (i = 0; i < MEMCG_NR_STAT; i++)
			stat[i] += memcg_page_state(iter, i);
	}
}

static void tree_events(struct mem_cgroup *memcg, unsigned long *events)
{
	struct mem_cgroup *iter;
	int i;

	memset(events, 0, sizeof(*events) * MEMCG_NR_EVENTS);

	for_each_mem_cgroup_tree(iter, memcg) {
		for (i = 0; i < MEMCG_NR_EVENTS; i++)
			events[i] += memcg_sum_events(iter, i);
	}
}

static unsigned long mem_cgroup_usage(struct mem_cgroup *memcg, bool swap)
{
	unsigned long val = 0;

	if (mem_cgroup_is_root(memcg)) {
		struct mem_cgroup *iter;

		for_each_mem_cgroup_tree(iter, memcg) {
			val += memcg_page_state(iter, MEMCG_CACHE);
			val += memcg_page_state(iter, MEMCG_RSS);
			if (swap)
				val += memcg_page_state(iter, MEMCG_SWAP);
		}
	} else {
		if (!swap)
			val = page_counter_read(&memcg->memory);
		else
			val = page_counter_read(&memcg->memsw);
	}
	return val;
}

enum {
	RES_USAGE,
	RES_LIMIT,
	RES_MAX_USAGE,
	RES_FAILCNT,
	RES_SOFT_LIMIT,
};

static u64 mem_cgroup_read_u64(struct cgroup_subsys_state *css,
			       struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct page_counter *counter;

	switch (MEMFILE_TYPE(cft->private)) {
	case _MEM:
		counter = &memcg->memory;
		break;
	case _MEMSWAP:
		counter = &memcg->memsw;
		break;
	case _KMEM:
		counter = &memcg->kmem;
		break;
	case _TCP:
		counter = &memcg->tcpmem;
		break;
	default:
		BUG();
	}

	switch (MEMFILE_ATTR(cft->private)) {
	case RES_USAGE:
		if (counter == &memcg->memory)
			return (u64)mem_cgroup_usage(memcg, false) * PAGE_SIZE;
		if (counter == &memcg->memsw)
			return (u64)mem_cgroup_usage(memcg, true) * PAGE_SIZE;
		return (u64)page_counter_read(counter) * PAGE_SIZE;
	case RES_LIMIT:
		return (u64)counter->limit * PAGE_SIZE;
	case RES_MAX_USAGE:
		return (u64)counter->watermark * PAGE_SIZE;
	case RES_FAILCNT:
		return counter->failcnt;
	case RES_SOFT_LIMIT:
		return (u64)memcg->soft_limit * PAGE_SIZE;
	default:
		BUG();
	}
}

#ifndef CONFIG_SLOB
static int memcg_online_kmem(struct mem_cgroup *memcg)
{
	int memcg_id;

	if (cgroup_memory_nokmem)
		return 0;

	BUG_ON(memcg->kmemcg_id >= 0);
	BUG_ON(memcg->kmem_state);

	memcg_id = memcg_alloc_cache_id();
	if (memcg_id < 0)
		return memcg_id;

	static_branch_inc(&memcg_kmem_enabled_key);
	/*
	 * A memory cgroup is considered kmem-online as soon as it gets
	 * kmemcg_id. Setting the id after enabling static branching will
	 * guarantee no one starts accounting before all call sites are
	 * patched.
	 */
	memcg->kmemcg_id = memcg_id;
	memcg->kmem_state = KMEM_ONLINE;
	INIT_LIST_HEAD(&memcg->kmem_caches);

	return 0;
}

static void memcg_offline_kmem(struct mem_cgroup *memcg)
{
	struct cgroup_subsys_state *css;
	struct mem_cgroup *parent, *child;
	int kmemcg_id;

	if (memcg->kmem_state != KMEM_ONLINE)
		return;
	/*
	 * Clear the online state before clearing memcg_caches array
	 * entries. The slab_mutex in memcg_deactivate_kmem_caches()
	 * guarantees that no cache will be created for this cgroup
	 * after we are done (see memcg_create_kmem_cache()).
	 */
	memcg->kmem_state = KMEM_ALLOCATED;

	memcg_deactivate_kmem_caches(memcg);

	kmemcg_id = memcg->kmemcg_id;
	BUG_ON(kmemcg_id < 0);

	parent = parent_mem_cgroup(memcg);
	if (!parent)
		parent = root_mem_cgroup;

	/*
	 * Change kmemcg_id of this cgroup and all its descendants to the
	 * parent's id, and then move all entries from this cgroup's list_lrus
	 * to ones of the parent. After we have finished, all list_lrus
	 * corresponding to this cgroup are guaranteed to remain empty. The
	 * ordering is imposed by list_lru_node->lock taken by
	 * memcg_drain_all_list_lrus().
	 */
	rcu_read_lock(); /* can be called from css_free w/o cgroup_mutex */
	css_for_each_descendant_pre(css, &memcg->css) {
		child = mem_cgroup_from_css(css);
		BUG_ON(child->kmemcg_id != kmemcg_id);
		child->kmemcg_id = parent->kmemcg_id;
		if (!memcg->use_hierarchy)
			break;
	}
	rcu_read_unlock();

	memcg_drain_all_list_lrus(kmemcg_id, parent->kmemcg_id);

	memcg_free_cache_id(kmemcg_id);
}

static void memcg_free_kmem(struct mem_cgroup *memcg)
{
	/* css_alloc() failed, offlining didn't happen */
	if (unlikely(memcg->kmem_state == KMEM_ONLINE))
		memcg_offline_kmem(memcg);

	if (memcg->kmem_state == KMEM_ALLOCATED) {
		memcg_destroy_kmem_caches(memcg);
		static_branch_dec(&memcg_kmem_enabled_key);
		WARN_ON(page_counter_read(&memcg->kmem));
	}
}
#else
static int memcg_online_kmem(struct mem_cgroup *memcg)
{
	return 0;
}
static void memcg_offline_kmem(struct mem_cgroup *memcg)
{
}
static void memcg_free_kmem(struct mem_cgroup *memcg)
{
}
#endif /* !CONFIG_SLOB */

static int memcg_update_kmem_limit(struct mem_cgroup *memcg,
				   unsigned long limit)
{
	int ret;

	mutex_lock(&memcg_limit_mutex);
	ret = page_counter_limit(&memcg->kmem, limit);
	mutex_unlock(&memcg_limit_mutex);
	return ret;
}

static int memcg_update_tcp_limit(struct mem_cgroup *memcg, unsigned long limit)
{
	int ret;

	mutex_lock(&memcg_limit_mutex);

	ret = page_counter_limit(&memcg->tcpmem, limit);
	if (ret)
		goto out;

	if (!memcg->tcpmem_active) {
		/*
		 * The active flag needs to be written after the static_key
		 * update. This is what guarantees that the socket activation
		 * function is the last one to run. See mem_cgroup_sk_alloc()
		 * for details, and note that we don't mark any socket as
		 * belonging to this memcg until that flag is up.
		 *
		 * We need to do this, because static_keys will span multiple
		 * sites, but we can't control their order. If we mark a socket
		 * as accounted, but the accounting functions are not patched in
		 * yet, we'll lose accounting.
		 *
		 * We never race with the readers in mem_cgroup_sk_alloc(),
		 * because when this value change, the code to process it is not
		 * patched in yet.
		 */
		static_branch_inc(&memcg_sockets_enabled_key);
		memcg->tcpmem_active = true;
	}
out:
	mutex_unlock(&memcg_limit_mutex);
	return ret;
}

/*
 * The user of this function is...
 * RES_LIMIT.
 */
static ssize_t mem_cgroup_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long nr_pages;
	int ret;

	buf = strstrip(buf);
	ret = page_counter_memparse(buf, "-1", &nr_pages);
	if (ret)
		return ret;

	switch (MEMFILE_ATTR(of_cft(of)->private)) {
	case RES_LIMIT:
		if (mem_cgroup_is_root(memcg)) { /* Can't set limit on root */
			ret = -EINVAL;
			break;
		}
		switch (MEMFILE_TYPE(of_cft(of)->private)) {
		case _MEM:
			ret = mem_cgroup_resize_limit(memcg, nr_pages);
			break;
		case _MEMSWAP:
			ret = mem_cgroup_resize_memsw_limit(memcg, nr_pages);
			break;
		case _KMEM:
			ret = memcg_update_kmem_limit(memcg, nr_pages);
			break;
		case _TCP:
			ret = memcg_update_tcp_limit(memcg, nr_pages);
			break;
		}
		break;
	case RES_SOFT_LIMIT:
		memcg->soft_limit = nr_pages;
		ret = 0;
		break;
	}
	return ret ?: nbytes;
}

static ssize_t mem_cgroup_reset(struct kernfs_open_file *of, char *buf,
				size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	struct page_counter *counter;

	switch (MEMFILE_TYPE(of_cft(of)->private)) {
	case _MEM:
		counter = &memcg->memory;
		break;
	case _MEMSWAP:
		counter = &memcg->memsw;
		break;
	case _KMEM:
		counter = &memcg->kmem;
		break;
	case _TCP:
		counter = &memcg->tcpmem;
		break;
	default:
		BUG();
	}

	switch (MEMFILE_ATTR(of_cft(of)->private)) {
	case RES_MAX_USAGE:
		page_counter_reset_watermark(counter);
		break;
	case RES_FAILCNT:
		counter->failcnt = 0;
		break;
	default:
		BUG();
	}

	return nbytes;
}

static u64 mem_cgroup_move_charge_read(struct cgroup_subsys_state *css,
					struct cftype *cft)
{
	return mem_cgroup_from_css(css)->move_charge_at_immigrate;
}

#ifdef CONFIG_MMU
static int mem_cgroup_move_charge_write(struct cgroup_subsys_state *css,
					struct cftype *cft, u64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (val & ~MOVE_MASK)
		return -EINVAL;

	/*
	 * No kind of locking is needed in here, because ->can_attach() will
	 * check this value once in the beginning of the process, and then carry
	 * on with stale data. This means that changes to this value will only
	 * affect task migrations starting after the change.
	 */
	memcg->move_charge_at_immigrate = val;
	return 0;
}
#else
static int mem_cgroup_move_charge_write(struct cgroup_subsys_state *css,
					struct cftype *cft, u64 val)
{
	return -ENOSYS;
}
#endif

#ifdef CONFIG_NUMA
static int memcg_numa_stat_show(struct seq_file *m, void *v)
{
	struct numa_stat {
		const char *name;
		unsigned int lru_mask;
	};

	static const struct numa_stat stats[] = {
		{ "total", LRU_ALL },
		{ "file", LRU_ALL_FILE },
		{ "anon", LRU_ALL_ANON },
		{ "unevictable", BIT(LRU_UNEVICTABLE) },
	};
	const struct numa_stat *stat;
	int nid;
	unsigned long nr;
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));

	for (stat = stats; stat < stats + ARRAY_SIZE(stats); stat++) {
		nr = mem_cgroup_nr_lru_pages(memcg, stat->lru_mask);
		seq_printf(m, "%s=%lu", stat->name, nr);
		for_each_node_state(nid, N_MEMORY) {
			nr = mem_cgroup_node_nr_lru_pages(memcg, nid,
							  stat->lru_mask);
			seq_printf(m, " N%d=%lu", nid, nr);
		}
		seq_putc(m, '\n');
	}

	for (stat = stats; stat < stats + ARRAY_SIZE(stats); stat++) {
		struct mem_cgroup *iter;

		nr = 0;
		for_each_mem_cgroup_tree(iter, memcg)
			nr += mem_cgroup_nr_lru_pages(iter, stat->lru_mask);
		seq_printf(m, "hierarchical_%s=%lu", stat->name, nr);
		for_each_node_state(nid, N_MEMORY) {
			nr = 0;
			for_each_mem_cgroup_tree(iter, memcg)
				nr += mem_cgroup_node_nr_lru_pages(
					iter, nid, stat->lru_mask);
			seq_printf(m, " N%d=%lu", nid, nr);
		}
		seq_putc(m, '\n');
	}

	return 0;
}
#endif /* CONFIG_NUMA */

/* Universal VM events cgroup1 shows, original sort order */
unsigned int memcg1_events[] = {
	PGPGIN,
	PGPGOUT,
	PGFAULT,
	PGMAJFAULT,
};

static const char *const memcg1_event_names[] = {
	"pgpgin",
	"pgpgout",
	"pgfault",
	"pgmajfault",
};

static int memcg_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	unsigned long memory, memsw;
	struct mem_cgroup *mi;
	unsigned int i;

	BUILD_BUG_ON(ARRAY_SIZE(memcg1_stat_names) != ARRAY_SIZE(memcg1_stats));
	BUILD_BUG_ON(ARRAY_SIZE(mem_cgroup_lru_names) != NR_LRU_LISTS);

	for (i = 0; i < ARRAY_SIZE(memcg1_stats); i++) {
		if (memcg1_stats[i] == MEMCG_SWAP && !do_memsw_account())
			continue;
		seq_printf(m, "%s %lu\n", memcg1_stat_names[i],
			   memcg_page_state(memcg, memcg1_stats[i]) *
			   PAGE_SIZE);
	}

	for (i = 0; i < ARRAY_SIZE(memcg1_events); i++)
		seq_printf(m, "%s %lu\n", memcg1_event_names[i],
			   memcg_sum_events(memcg, memcg1_events[i]));

	for (i = 0; i < NR_LRU_LISTS; i++)
		seq_printf(m, "%s %lu\n", mem_cgroup_lru_names[i],
			   mem_cgroup_nr_lru_pages(memcg, BIT(i)) * PAGE_SIZE);

	/* Hierarchical information */
	memory = memsw = PAGE_COUNTER_MAX;
	for (mi = memcg; mi; mi = parent_mem_cgroup(mi)) {
		memory = min(memory, mi->memory.limit);
		memsw = min(memsw, mi->memsw.limit);
	}
	seq_printf(m, "hierarchical_memory_limit %llu\n",
		   (u64)memory * PAGE_SIZE);
	if (do_memsw_account())
		seq_printf(m, "hierarchical_memsw_limit %llu\n",
			   (u64)memsw * PAGE_SIZE);

	for (i = 0; i < ARRAY_SIZE(memcg1_stats); i++) {
		unsigned long long val = 0;

		if (memcg1_stats[i] == MEMCG_SWAP && !do_memsw_account())
			continue;
		for_each_mem_cgroup_tree(mi, memcg)
			val += memcg_page_state(mi, memcg1_stats[i]) *
			PAGE_SIZE;
		seq_printf(m, "total_%s %llu\n", memcg1_stat_names[i], val);
	}

	for (i = 0; i < ARRAY_SIZE(memcg1_events); i++) {
		unsigned long long val = 0;

		for_each_mem_cgroup_tree(mi, memcg)
			val += memcg_sum_events(mi, memcg1_events[i]);
		seq_printf(m, "total_%s %llu\n", memcg1_event_names[i], val);
	}

	for (i = 0; i < NR_LRU_LISTS; i++) {
		unsigned long long val = 0;

		for_each_mem_cgroup_tree(mi, memcg)
			val += mem_cgroup_nr_lru_pages(mi, BIT(i)) * PAGE_SIZE;
		seq_printf(m, "total_%s %llu\n", mem_cgroup_lru_names[i], val);
	}

#ifdef CONFIG_DEBUG_VM
	{
		pg_data_t *pgdat;
		struct mem_cgroup_per_node *mz;
		struct zone_reclaim_stat *rstat;
		unsigned long recent_rotated[2] = {0, 0};
		unsigned long recent_scanned[2] = {0, 0};

		for_each_online_pgdat(pgdat) {
			mz = mem_cgroup_nodeinfo(memcg, pgdat->node_id);
			rstat = &mz->lruvec.reclaim_stat;

			recent_rotated[0] += rstat->recent_rotated[0];
			recent_rotated[1] += rstat->recent_rotated[1];
			recent_scanned[0] += rstat->recent_scanned[0];
			recent_scanned[1] += rstat->recent_scanned[1];
		}
		seq_printf(m, "recent_rotated_anon %lu\n", recent_rotated[0]);
		seq_printf(m, "recent_rotated_file %lu\n", recent_rotated[1]);
		seq_printf(m, "recent_scanned_anon %lu\n", recent_scanned[0]);
		seq_printf(m, "recent_scanned_file %lu\n", recent_scanned[1]);
	}
#endif

	return 0;
}

static u64 mem_cgroup_swappiness_read(struct cgroup_subsys_state *css,
				      struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	return mem_cgroup_swappiness(memcg);
}

static int mem_cgroup_swappiness_write(struct cgroup_subsys_state *css,
				       struct cftype *cft, u64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (val > 100)
		return -EINVAL;

	if (css->parent)
		memcg->swappiness = val;
	else
		vm_swappiness = val;

	return 0;
}

static void __mem_cgroup_threshold(struct mem_cgroup *memcg, bool swap)
{
	struct mem_cgroup_threshold_ary *t;
	unsigned long usage;
	int i;

	rcu_read_lock();
	if (!swap)
		t = rcu_dereference(memcg->thresholds.primary);
	else
		t = rcu_dereference(memcg->memsw_thresholds.primary);

	if (!t)
		goto unlock;

	usage = mem_cgroup_usage(memcg, swap);

	/*
	 * current_threshold points to threshold just below or equal to usage.
	 * If it's not true, a threshold was crossed after last
	 * call of __mem_cgroup_threshold().
	 */
	i = t->current_threshold;

	/*
	 * Iterate backward over array of thresholds starting from
	 * current_threshold and check if a threshold is crossed.
	 * If none of thresholds below usage is crossed, we read
	 * only one element of the array here.
	 */
	for (; i >= 0 && unlikely(t->entries[i].threshold > usage); i--)
		eventfd_signal(t->entries[i].eventfd, 1);

	/* i = current_threshold + 1 */
	i++;

	/*
	 * Iterate forward over array of thresholds starting from
	 * current_threshold+1 and check if a threshold is crossed.
	 * If none of thresholds above usage is crossed, we read
	 * only one element of the array here.
	 */
	for (; i < t->size && unlikely(t->entries[i].threshold <= usage); i++)
		eventfd_signal(t->entries[i].eventfd, 1);

	/* Update current_threshold */
	t->current_threshold = i - 1;
unlock:
	rcu_read_unlock();
}

static void mem_cgroup_threshold(struct mem_cgroup *memcg)
{
	while (memcg) {
		__mem_cgroup_threshold(memcg, false);
		if (do_memsw_account())
			__mem_cgroup_threshold(memcg, true);

		memcg = parent_mem_cgroup(memcg);
	}
}

static int compare_thresholds(const void *a, const void *b)
{
	const struct mem_cgroup_threshold *_a = a;
	const struct mem_cgroup_threshold *_b = b;

	if (_a->threshold > _b->threshold)
		return 1;

	if (_a->threshold < _b->threshold)
		return -1;

	return 0;
}

static int mem_cgroup_oom_notify_cb(struct mem_cgroup *memcg)
{
	struct mem_cgroup_eventfd_list *ev;

	spin_lock(&memcg_oom_lock);

	list_for_each_entry(ev, &memcg->oom_notify, list)
		eventfd_signal(ev->eventfd, 1);

	spin_unlock(&memcg_oom_lock);
	return 0;
}

static void mem_cgroup_oom_notify(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	for_each_mem_cgroup_tree(iter, memcg)
		mem_cgroup_oom_notify_cb(iter);
}

static int __mem_cgroup_usage_register_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, const char *args, enum res_type type)
{
	struct mem_cgroup_thresholds *thresholds;
	struct mem_cgroup_threshold_ary *new;
	unsigned long threshold;
	unsigned long usage;
	int i, size, ret;

	ret = page_counter_memparse(args, "-1", &threshold);
	if (ret)
		return ret;

	mutex_lock(&memcg->thresholds_lock);

	if (type == _MEM) {
		thresholds = &memcg->thresholds;
		usage = mem_cgroup_usage(memcg, false);
	} else if (type == _MEMSWAP) {
		thresholds = &memcg->memsw_thresholds;
		usage = mem_cgroup_usage(memcg, true);
	} else
		BUG();

	/* Check if a threshold crossed before adding a new one */
	if (thresholds->primary)
		__mem_cgroup_threshold(memcg, type == _MEMSWAP);

	size = thresholds->primary ? thresholds->primary->size + 1 : 1;

	/* Allocate memory for new array of thresholds */
	new = kmalloc(sizeof(*new) + size * sizeof(struct mem_cgroup_threshold),
			GFP_KERNEL);
	if (!new) {
		ret = -ENOMEM;
		goto unlock;
	}
	new->size = size;

	/* Copy thresholds (if any) to new array */
	if (thresholds->primary) {
		memcpy(new->entries, thresholds->primary->entries, (size - 1) *
				sizeof(struct mem_cgroup_threshold));
	}

	/* Add new threshold */
	new->entries[size - 1].eventfd = eventfd;
	new->entries[size - 1].threshold = threshold;

	/* Sort thresholds. Registering of new threshold isn't time-critical */
	sort(new->entries, size, sizeof(struct mem_cgroup_threshold),
			compare_thresholds, NULL);

	/* Find current threshold */
	new->current_threshold = -1;
	for (i = 0; i < size; i++) {
		if (new->entries[i].threshold <= usage) {
			/*
			 * new->current_threshold will not be used until
			 * rcu_assign_pointer(), so it's safe to increment
			 * it here.
			 */
			++new->current_threshold;
		} else
			break;
	}

	/* Free old spare buffer and save old primary buffer as spare */
	kfree(thresholds->spare);
	thresholds->spare = thresholds->primary;

	rcu_assign_pointer(thresholds->primary, new);

	/* To be sure that nobody uses thresholds */
	synchronize_rcu();

unlock:
	mutex_unlock(&memcg->thresholds_lock);

	return ret;
}

static int mem_cgroup_usage_register_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, const char *args)
{
	return __mem_cgroup_usage_register_event(memcg, eventfd, args, _MEM);
}

static int memsw_cgroup_usage_register_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, const char *args)
{
	return __mem_cgroup_usage_register_event(memcg, eventfd, args, _MEMSWAP);
}

static void __mem_cgroup_usage_unregister_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, enum res_type type)
{
	struct mem_cgroup_thresholds *thresholds;
	struct mem_cgroup_threshold_ary *new;
	unsigned long usage;
	int i, j, size;

	mutex_lock(&memcg->thresholds_lock);

	if (type == _MEM) {
		thresholds = &memcg->thresholds;
		usage = mem_cgroup_usage(memcg, false);
	} else if (type == _MEMSWAP) {
		thresholds = &memcg->memsw_thresholds;
		usage = mem_cgroup_usage(memcg, true);
	} else
		BUG();

	if (!thresholds->primary)
		goto unlock;

	/* Check if a threshold crossed before removing */
	__mem_cgroup_threshold(memcg, type == _MEMSWAP);

	/* Calculate new number of threshold */
	size = 0;
	for (i = 0; i < thresholds->primary->size; i++) {
		if (thresholds->primary->entries[i].eventfd != eventfd)
			size++;
	}

	new = thresholds->spare;

	/* Set thresholds array to NULL if we don't have thresholds */
	if (!size) {
		kfree(new);
		new = NULL;
		goto swap_buffers;
	}

	new->size = size;

	/* Copy thresholds and find current threshold */
	new->current_threshold = -1;
	for (i = 0, j = 0; i < thresholds->primary->size; i++) {
		if (thresholds->primary->entries[i].eventfd == eventfd)
			continue;

		new->entries[j] = thresholds->primary->entries[i];
		if (new->entries[j].threshold <= usage) {
			/*
			 * new->current_threshold will not be used
			 * until rcu_assign_pointer(), so it's safe to increment
			 * it here.
			 */
			++new->current_threshold;
		}
		j++;
	}

swap_buffers:
	/* Swap primary and spare array */
	thresholds->spare = thresholds->primary;

	rcu_assign_pointer(thresholds->primary, new);

	/* To be sure that nobody uses thresholds */
	synchronize_rcu();

	/* If all events are unregistered, free the spare array */
	if (!new) {
		kfree(thresholds->spare);
		thresholds->spare = NULL;
	}
unlock:
	mutex_unlock(&memcg->thresholds_lock);
}

static void mem_cgroup_usage_unregister_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd)
{
	return __mem_cgroup_usage_unregister_event(memcg, eventfd, _MEM);
}

static void memsw_cgroup_usage_unregister_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd)
{
	return __mem_cgroup_usage_unregister_event(memcg, eventfd, _MEMSWAP);
}

static int mem_cgroup_oom_register_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd, const char *args)
{
	struct mem_cgroup_eventfd_list *event;

	event = kmalloc(sizeof(*event),	GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	spin_lock(&memcg_oom_lock);

	event->eventfd = eventfd;
	list_add(&event->list, &memcg->oom_notify);

	/* already in OOM ? */
	if (memcg->under_oom)
		eventfd_signal(eventfd, 1);
	spin_unlock(&memcg_oom_lock);

	return 0;
}

static void mem_cgroup_oom_unregister_event(struct mem_cgroup *memcg,
	struct eventfd_ctx *eventfd)
{
	struct mem_cgroup_eventfd_list *ev, *tmp;

	spin_lock(&memcg_oom_lock);

	list_for_each_entry_safe(ev, tmp, &memcg->oom_notify, list) {
		if (ev->eventfd == eventfd) {
			list_del(&ev->list);
			kfree(ev);
		}
	}

	spin_unlock(&memcg_oom_lock);
}

static int mem_cgroup_oom_control_read(struct seq_file *sf, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(sf));

	seq_printf(sf, "oom_kill_disable %d\n", memcg->oom_kill_disable);
	seq_printf(sf, "under_oom %d\n", (bool)memcg->under_oom);
	seq_printf(sf, "oom_kill %lu\n", memcg_sum_events(memcg, OOM_KILL));
	return 0;
}

static int mem_cgroup_oom_control_write(struct cgroup_subsys_state *css,
	struct cftype *cft, u64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	/* cannot set to root cgroup and only 0 and 1 are allowed */
	if (!css->parent || !((val == 0) || (val == 1)))
		return -EINVAL;

	memcg->oom_kill_disable = val;
	if (!val)
		memcg_oom_recover(memcg);

	return 0;
}

#ifdef CONFIG_CGROUP_WRITEBACK

struct list_head *mem_cgroup_cgwb_list(struct mem_cgroup *memcg)
{
	return &memcg->cgwb_list;
}

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

	*pdirty = memcg_page_state(memcg, NR_FILE_DIRTY);

	/* this should eventually include NR_UNSTABLE_NFS */
	*pwriteback = memcg_page_state(memcg, NR_WRITEBACK);
	*pfilepages = mem_cgroup_nr_lru_pages(memcg, (1 << LRU_INACTIVE_FILE) |
						     (1 << LRU_ACTIVE_FILE));
	*pheadroom = PAGE_COUNTER_MAX;

	while ((parent = parent_mem_cgroup(memcg))) {
		unsigned long ceiling = min(memcg->memory.limit, memcg->high);
		unsigned long used = page_counter_read(&memcg->memory);

		*pheadroom = min(*pheadroom, ceiling - min(ceiling, used));
		memcg = parent;
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
 * DO NOT USE IN NEW FILES.
 *
 * "cgroup.event_control" implementation.
 *
 * This is way over-engineered.  It tries to support fully configurable
 * events for each user.  Such level of flexibility is completely
 * unnecessary especially in the light of the planned unified hierarchy.
 *
 * Please deprecate this and replace with something simpler if at all
 * possible.
 */

/*
 * Unregister event and free resources.
 *
 * Gets called from workqueue.
 */
static void memcg_event_remove(struct work_struct *work)
{
	struct mem_cgroup_event *event =
		container_of(work, struct mem_cgroup_event, remove);
	struct mem_cgroup *memcg = event->memcg;

	remove_wait_queue(event->wqh, &event->wait);

	event->unregister_event(memcg, event->eventfd);

	/* Notify userspace the event is going away. */
	eventfd_signal(event->eventfd, 1);

	eventfd_ctx_put(event->eventfd);
	kfree(event);
	css_put(&memcg->css);
}

/*
 * Gets called on POLLHUP on eventfd when user closes it.
 *
 * Called with wqh->lock held and interrupts disabled.
 */
static int memcg_event_wake(wait_queue_entry_t *wait, unsigned mode,
			    int sync, void *key)
{
	struct mem_cgroup_event *event =
		container_of(wait, struct mem_cgroup_event, wait);
	struct mem_cgroup *memcg = event->memcg;
	unsigned long flags = (unsigned long)key;

	if (flags & POLLHUP) {
		/*
		 * If the event has been detached at cgroup removal, we
		 * can simply return knowing the other side will cleanup
		 * for us.
		 *
		 * We can't race against event freeing since the other
		 * side will require wqh->lock via remove_wait_queue(),
		 * which we hold.
		 */
		spin_lock(&memcg->event_list_lock);
		if (!list_empty(&event->list)) {
			list_del_init(&event->list);
			/*
			 * We are in atomic context, but cgroup_event_remove()
			 * may sleep, so we have to call it in workqueue.
			 */
			schedule_work(&event->remove);
		}
		spin_unlock(&memcg->event_list_lock);
	}

	return 0;
}

static void memcg_event_ptable_queue_proc(struct file *file,
		wait_queue_head_t *wqh, poll_table *pt)
{
	struct mem_cgroup_event *event =
		container_of(pt, struct mem_cgroup_event, pt);

	event->wqh = wqh;
	add_wait_queue(wqh, &event->wait);
}

/*
 * DO NOT USE IN NEW FILES.
 *
 * Parse input and register new cgroup event handler.
 *
 * Input must be in format '<event_fd> <control_fd> <args>'.
 * Interpretation of args is defined by control file implementation.
 */
static ssize_t memcg_write_event_control(struct kernfs_open_file *of,
					 char *buf, size_t nbytes, loff_t off)
{
	struct cgroup_subsys_state *css = of_css(of);
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct mem_cgroup_event *event;
	struct cgroup_subsys_state *cfile_css;
	unsigned int efd, cfd;
	struct fd efile;
	struct fd cfile;
	const char *name;
	char *endp;
	int ret;

	buf = strstrip(buf);

	efd = simple_strtoul(buf, &endp, 10);
	if (*endp != ' ')
		return -EINVAL;
	buf = endp + 1;

	cfd = simple_strtoul(buf, &endp, 10);
	if ((*endp != ' ') && (*endp != '\0'))
		return -EINVAL;
	buf = endp + 1;

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	event->memcg = memcg;
	INIT_LIST_HEAD(&event->list);
	init_poll_funcptr(&event->pt, memcg_event_ptable_queue_proc);
	init_waitqueue_func_entry(&event->wait, memcg_event_wake);
	INIT_WORK(&event->remove, memcg_event_remove);

	efile = fdget(efd);
	if (!efile.file) {
		ret = -EBADF;
		goto out_kfree;
	}

	event->eventfd = eventfd_ctx_fileget(efile.file);
	if (IS_ERR(event->eventfd)) {
		ret = PTR_ERR(event->eventfd);
		goto out_put_efile;
	}

	cfile = fdget(cfd);
	if (!cfile.file) {
		ret = -EBADF;
		goto out_put_eventfd;
	}

	/* the process need read permission on control file */
	/* AV: shouldn't we check that it's been opened for read instead? */
	ret = inode_permission(file_inode(cfile.file), MAY_READ);
	if (ret < 0)
		goto out_put_cfile;

	/*
	 * Determine the event callbacks and set them in @event.  This used
	 * to be done via struct cftype but cgroup core no longer knows
	 * about these events.  The following is crude but the whole thing
	 * is for compatibility anyway.
	 *
	 * DO NOT ADD NEW FILES.
	 */
	name = cfile.file->f_path.dentry->d_name.name;

	if (!strcmp(name, "memory.usage_in_bytes")) {
		event->register_event = mem_cgroup_usage_register_event;
		event->unregister_event = mem_cgroup_usage_unregister_event;
	} else if (!strcmp(name, "memory.oom_control")) {
		event->register_event = mem_cgroup_oom_register_event;
		event->unregister_event = mem_cgroup_oom_unregister_event;
	} else if (!strcmp(name, "memory.pressure_level")) {
		event->register_event = vmpressure_register_event;
		event->unregister_event = vmpressure_unregister_event;
	} else if (!strcmp(name, "memory.memsw.usage_in_bytes")) {
		event->register_event = memsw_cgroup_usage_register_event;
		event->unregister_event = memsw_cgroup_usage_unregister_event;
	} else {
		ret = -EINVAL;
		goto out_put_cfile;
	}

	/*
	 * Verify @cfile should belong to @css.  Also, remaining events are
	 * automatically removed on cgroup destruction but the removal is
	 * asynchronous, so take an extra ref on @css.
	 */
	cfile_css = css_tryget_online_from_dir(cfile.file->f_path.dentry->d_parent,
					       &memory_cgrp_subsys);
	ret = -EINVAL;
	if (IS_ERR(cfile_css))
		goto out_put_cfile;
	if (cfile_css != css) {
		css_put(cfile_css);
		goto out_put_cfile;
	}

	ret = event->register_event(memcg, event->eventfd, buf);
	if (ret)
		goto out_put_css;

	efile.file->f_op->poll(efile.file, &event->pt);

	spin_lock(&memcg->event_list_lock);
	list_add(&event->list, &memcg->event_list);
	spin_unlock(&memcg->event_list_lock);

	fdput(cfile);
	fdput(efile);

	return nbytes;

out_put_css:
	css_put(css);
out_put_cfile:
	fdput(cfile);
out_put_eventfd:
	eventfd_ctx_put(event->eventfd);
out_put_efile:
	fdput(efile);
out_kfree:
	kfree(event);

	return ret;
}

static struct cftype mem_cgroup_legacy_files[] = {
	{
		.name = "usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_USAGE),
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_MAX_USAGE),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "limit_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "soft_limit_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_SOFT_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "failcnt",
		.private = MEMFILE_PRIVATE(_MEM, RES_FAILCNT),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "stat",
		.seq_show = memcg_stat_show,
	},
	{
		.name = "force_empty",
		.write = mem_cgroup_force_empty_write,
	},
	{
		.name = "use_hierarchy",
		.write_u64 = mem_cgroup_hierarchy_write,
		.read_u64 = mem_cgroup_hierarchy_read,
	},
	{
		.name = "cgroup.event_control",		/* XXX: for compat */
		.write = memcg_write_event_control,
		.flags = CFTYPE_NO_PREFIX | CFTYPE_WORLD_WRITABLE,
	},
	{
		.name = "swappiness",
		.read_u64 = mem_cgroup_swappiness_read,
		.write_u64 = mem_cgroup_swappiness_write,
	},
	{
		.name = "move_charge_at_immigrate",
		.read_u64 = mem_cgroup_move_charge_read,
		.write_u64 = mem_cgroup_move_charge_write,
	},
	{
		.name = "oom_control",
		.seq_show = mem_cgroup_oom_control_read,
		.write_u64 = mem_cgroup_oom_control_write,
		.private = MEMFILE_PRIVATE(_OOM_TYPE, OOM_CONTROL),
	},
	{
		.name = "pressure_level",
	},
#ifdef CONFIG_NUMA
	{
		.name = "numa_stat",
		.seq_show = memcg_numa_stat_show,
	},
#endif
	{
		.name = "kmem.limit_in_bytes",
		.private = MEMFILE_PRIVATE(_KMEM, RES_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.usage_in_bytes",
		.private = MEMFILE_PRIVATE(_KMEM, RES_USAGE),
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.failcnt",
		.private = MEMFILE_PRIVATE(_KMEM, RES_FAILCNT),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_KMEM, RES_MAX_USAGE),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
#ifdef CONFIG_SLABINFO
	{
		.name = "kmem.slabinfo",
		.seq_start = memcg_slab_start,
		.seq_next = memcg_slab_next,
		.seq_stop = memcg_slab_stop,
		.seq_show = memcg_slab_show,
	},
#endif
	{
		.name = "kmem.tcp.limit_in_bytes",
		.private = MEMFILE_PRIVATE(_TCP, RES_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.tcp.usage_in_bytes",
		.private = MEMFILE_PRIVATE(_TCP, RES_USAGE),
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.tcp.failcnt",
		.private = MEMFILE_PRIVATE(_TCP, RES_FAILCNT),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "kmem.tcp.max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_TCP, RES_MAX_USAGE),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{ },	/* terminate */
};

/*
 * Private memory cgroup IDR
 *
 * Swap-out records and page cache shadow entries need to store memcg
 * references in constrained space, so we maintain an ID space that is
 * limited to 16 bit (MEM_CGROUP_ID_MAX), limiting the total number of
 * memory-controlled cgroups to 64k.
 *
 * However, there usually are many references to the oflline CSS after
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

static DEFINE_IDR(mem_cgroup_idr);

static void mem_cgroup_id_get_many(struct mem_cgroup *memcg, unsigned int n)
{
	VM_BUG_ON(atomic_read(&memcg->id.ref) <= 0);
	atomic_add(n, &memcg->id.ref);
}

static void mem_cgroup_id_put_many(struct mem_cgroup *memcg, unsigned int n)
{
	VM_BUG_ON(atomic_read(&memcg->id.ref) < n);
	if (atomic_sub_and_test(n, &memcg->id.ref)) {
		idr_remove(&mem_cgroup_idr, memcg->id.id);
		memcg->id.id = 0;

		/* Memcg ID pins CSS */
		css_put(&memcg->css);
	}
}

static inline void mem_cgroup_id_get(struct mem_cgroup *memcg)
{
	mem_cgroup_id_get_many(memcg, 1);
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
	return idr_find(&mem_cgroup_idr, id);
}

static int alloc_mem_cgroup_per_node_info(struct mem_cgroup *memcg, int node)
{
	struct mem_cgroup_per_node *pn;
	int tmp = node;
	/*
	 * This routine is called against possible nodes.
	 * But it's BUG to call kmalloc() against offline node.
	 *
	 * TODO: this routine can waste much memory for nodes which will
	 *       never be onlined. It's better to use memory hotplug callback
	 *       function.
	 */
	if (!node_state(node, N_NORMAL_MEMORY))
		tmp = -1;
	pn = kzalloc_node(sizeof(*pn), GFP_KERNEL, tmp);
	if (!pn)
		return 1;

	pn->lruvec_stat = alloc_percpu(struct lruvec_stat);
	if (!pn->lruvec_stat) {
		kfree(pn);
		return 1;
	}

	lruvec_init(&pn->lruvec);
	pn->usage_in_excess = 0;
	pn->on_tree = false;
	pn->memcg = memcg;

	memcg->nodeinfo[node] = pn;
	return 0;
}

static void free_mem_cgroup_per_node_info(struct mem_cgroup *memcg, int node)
{
	struct mem_cgroup_per_node *pn = memcg->nodeinfo[node];

	free_percpu(pn->lruvec_stat);
	kfree(pn);
}

static void __mem_cgroup_free(struct mem_cgroup *memcg)
{
	int node;

	for_each_node(node)
		free_mem_cgroup_per_node_info(memcg, node);
	free_percpu(memcg->stat);
	kfree(memcg);
}

static void mem_cgroup_free(struct mem_cgroup *memcg)
{
	memcg_wb_domain_exit(memcg);
	__mem_cgroup_free(memcg);
}

static struct mem_cgroup *mem_cgroup_alloc(void)
{
	struct mem_cgroup *memcg;
	size_t size;
	int node;

	size = sizeof(struct mem_cgroup);
	size += nr_node_ids * sizeof(struct mem_cgroup_per_node *);

	memcg = kzalloc(size, GFP_KERNEL);
	if (!memcg)
		return NULL;

	memcg->id.id = idr_alloc(&mem_cgroup_idr, NULL,
				 1, MEM_CGROUP_ID_MAX,
				 GFP_KERNEL);
	if (memcg->id.id < 0)
		goto fail;

	memcg->stat = alloc_percpu(struct mem_cgroup_stat_cpu);
	if (!memcg->stat)
		goto fail;

	for_each_node(node)
		if (alloc_mem_cgroup_per_node_info(memcg, node))
			goto fail;

	if (memcg_wb_domain_init(memcg, GFP_KERNEL))
		goto fail;

	INIT_WORK(&memcg->high_work, high_work_func);
	memcg->last_scanned_node = MAX_NUMNODES;
	INIT_LIST_HEAD(&memcg->oom_notify);
	mutex_init(&memcg->thresholds_lock);
	spin_lock_init(&memcg->move_lock);
	vmpressure_init(&memcg->vmpressure);
	INIT_LIST_HEAD(&memcg->event_list);
	spin_lock_init(&memcg->event_list_lock);
	memcg->socket_pressure = jiffies;
#ifndef CONFIG_SLOB
	memcg->kmemcg_id = -1;
#endif
#ifdef CONFIG_CGROUP_WRITEBACK
	INIT_LIST_HEAD(&memcg->cgwb_list);
#endif
	idr_replace(&mem_cgroup_idr, memcg, memcg->id.id);
	return memcg;
fail:
	if (memcg->id.id > 0)
		idr_remove(&mem_cgroup_idr, memcg->id.id);
	__mem_cgroup_free(memcg);
	return NULL;
}

static struct cgroup_subsys_state * __ref
mem_cgroup_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct mem_cgroup *parent = mem_cgroup_from_css(parent_css);
	struct mem_cgroup *memcg;
	long error = -ENOMEM;

	memcg = mem_cgroup_alloc();
	if (!memcg)
		return ERR_PTR(error);

	memcg->high = PAGE_COUNTER_MAX;
	memcg->soft_limit = PAGE_COUNTER_MAX;
	if (parent) {
		memcg->swappiness = mem_cgroup_swappiness(parent);
		memcg->oom_kill_disable = parent->oom_kill_disable;
	}
	if (parent && parent->use_hierarchy) {
		memcg->use_hierarchy = true;
		page_counter_init(&memcg->memory, &parent->memory);
		page_counter_init(&memcg->swap, &parent->swap);
		page_counter_init(&memcg->memsw, &parent->memsw);
		page_counter_init(&memcg->kmem, &parent->kmem);
		page_counter_init(&memcg->tcpmem, &parent->tcpmem);
	} else {
		page_counter_init(&memcg->memory, NULL);
		page_counter_init(&memcg->swap, NULL);
		page_counter_init(&memcg->memsw, NULL);
		page_counter_init(&memcg->kmem, NULL);
		page_counter_init(&memcg->tcpmem, NULL);
		/*
		 * Deeper hierachy with use_hierarchy == false doesn't make
		 * much sense so let cgroup subsystem know about this
		 * unfortunate state in our controller.
		 */
		if (parent != root_mem_cgroup)
			memory_cgrp_subsys.broken_hierarchy = true;
	}

	/* The following stuff does not apply to the root */
	if (!parent) {
		root_mem_cgroup = memcg;
		return &memcg->css;
	}

	error = memcg_online_kmem(memcg);
	if (error)
		goto fail;

	if (cgroup_subsys_on_dfl(memory_cgrp_subsys) && !cgroup_memory_nosocket)
		static_branch_inc(&memcg_sockets_enabled_key);

	return &memcg->css;
fail:
	mem_cgroup_free(memcg);
	return ERR_PTR(-ENOMEM);
}

static int mem_cgroup_css_online(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	/* Online state pins memcg ID, memcg ID pins CSS */
	atomic_set(&memcg->id.ref, 1);
	css_get(css);
	return 0;
}

static void mem_cgroup_css_offline(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct mem_cgroup_event *event, *tmp;

	/*
	 * Unregister events and notify userspace.
	 * Notify userspace about cgroup removing only after rmdir of cgroup
	 * directory to avoid race between userspace and kernelspace.
	 */
	spin_lock(&memcg->event_list_lock);
	list_for_each_entry_safe(event, tmp, &memcg->event_list, list) {
		list_del_init(&event->list);
		schedule_work(&event->remove);
	}
	spin_unlock(&memcg->event_list_lock);

	memcg->low = 0;

	memcg_offline_kmem(memcg);
	wb_memcg_offline(memcg);

	mem_cgroup_id_put(memcg);
}

static void mem_cgroup_css_released(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	invalidate_reclaim_iterators(memcg);
}

static void mem_cgroup_css_free(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	if (cgroup_subsys_on_dfl(memory_cgrp_subsys) && !cgroup_memory_nosocket)
		static_branch_dec(&memcg_sockets_enabled_key);

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && memcg->tcpmem_active)
		static_branch_dec(&memcg_sockets_enabled_key);

	vmpressure_cleanup(&memcg->vmpressure);
	cancel_work_sync(&memcg->high_work);
	mem_cgroup_remove_from_trees(memcg);
	memcg_free_kmem(memcg);
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

	page_counter_limit(&memcg->memory, PAGE_COUNTER_MAX);
	page_counter_limit(&memcg->swap, PAGE_COUNTER_MAX);
	page_counter_limit(&memcg->memsw, PAGE_COUNTER_MAX);
	page_counter_limit(&memcg->kmem, PAGE_COUNTER_MAX);
	page_counter_limit(&memcg->tcpmem, PAGE_COUNTER_MAX);
	memcg->low = 0;
	memcg->high = PAGE_COUNTER_MAX;
	memcg->soft_limit = PAGE_COUNTER_MAX;
	memcg_wb_domain_size_changed(memcg);
}

#ifdef CONFIG_MMU
/* Handlers for move charge at task migration. */
static int mem_cgroup_do_precharge(unsigned long count)
{
	int ret;

	/* Try a single bulk charge without reclaim first, kswapd may wake */
	ret = try_charge(mc.to, GFP_KERNEL & ~__GFP_DIRECT_RECLAIM, count);
	if (!ret) {
		mc.precharge += count;
		return ret;
	}

	/* Try charges one by one with reclaim, but do not retry */
	while (count--) {
		ret = try_charge(mc.to, GFP_KERNEL | __GFP_NORETRY, 1);
		if (ret)
			return ret;
		mc.precharge++;
		cond_resched();
	}
	return 0;
}

union mc_target {
	struct page	*page;
	swp_entry_t	ent;
};

enum mc_target_type {
	MC_TARGET_NONE = 0,
	MC_TARGET_PAGE,
	MC_TARGET_SWAP,
	MC_TARGET_DEVICE,
};

static struct page *mc_handle_present_pte(struct vm_area_struct *vma,
						unsigned long addr, pte_t ptent)
{
	struct page *page = _vm_normal_page(vma, addr, ptent, true);

	if (!page || !page_mapped(page))
		return NULL;
	if (PageAnon(page)) {
		if (!(mc.flags & MOVE_ANON))
			return NULL;
	} else {
		if (!(mc.flags & MOVE_FILE))
			return NULL;
	}
	if (!get_page_unless_zero(page))
		return NULL;

	return page;
}

#if defined(CONFIG_SWAP) || defined(CONFIG_DEVICE_PRIVATE)
static struct page *mc_handle_swap_pte(struct vm_area_struct *vma,
			pte_t ptent, swp_entry_t *entry)
{
	struct page *page = NULL;
	swp_entry_t ent = pte_to_swp_entry(ptent);

	if (!(mc.flags & MOVE_ANON) || non_swap_entry(ent))
		return NULL;

	/*
	 * Handle MEMORY_DEVICE_PRIVATE which are ZONE_DEVICE page belonging to
	 * a device and because they are not accessible by CPU they are store
	 * as special swap entry in the CPU page table.
	 */
	if (is_device_private_entry(ent)) {
		page = device_private_entry_to_page(ent);
		/*
		 * MEMORY_DEVICE_PRIVATE means ZONE_DEVICE page and which have
		 * a refcount of 1 when free (unlike normal page)
		 */
		if (!page_ref_add_unless(page, 1, 1))
			return NULL;
		return page;
	}

	/*
	 * Because lookup_swap_cache() updates some statistics counter,
	 * we call find_get_page() with swapper_space directly.
	 */
	page = find_get_page(swap_address_space(ent), swp_offset(ent));
	if (do_memsw_account())
		entry->val = ent.val;

	return page;
}
#else
static struct page *mc_handle_swap_pte(struct vm_area_struct *vma,
			pte_t ptent, swp_entry_t *entry)
{
	return NULL;
}
#endif

static struct page *mc_handle_file_pte(struct vm_area_struct *vma,
			unsigned long addr, pte_t ptent, swp_entry_t *entry)
{
	struct page *page = NULL;
	struct address_space *mapping;
	pgoff_t pgoff;

	if (!vma->vm_file) /* anonymous vma */
		return NULL;
	if (!(mc.flags & MOVE_FILE))
		return NULL;

	mapping = vma->vm_file->f_mapping;
	pgoff = linear_page_index(vma, addr);

	/* page is moved even if it's not RSS of this task(page-faulted). */
#ifdef CONFIG_SWAP
	/* shmem/tmpfs may report page out on swap: account for that too. */
	if (shmem_mapping(mapping)) {
		page = find_get_entry(mapping, pgoff);
		if (radix_tree_exceptional_entry(page)) {
			swp_entry_t swp = radix_to_swp_entry(page);
			if (do_memsw_account())
				*entry = swp;
			page = find_get_page(swap_address_space(swp),
					     swp_offset(swp));
		}
	} else
		page = find_get_page(mapping, pgoff);
#else
	page = find_get_page(mapping, pgoff);
#endif
	return page;
}

/**
 * mem_cgroup_move_account - move account of the page
 * @page: the page
 * @compound: charge the page as compound or small page
 * @from: mem_cgroup which the page is moved from.
 * @to:	mem_cgroup which the page is moved to. @from != @to.
 *
 * The caller must make sure the page is not on LRU (isolate_page() is useful.)
 *
 * This function doesn't do "charge" to new cgroup and doesn't do "uncharge"
 * from old cgroup.
 */
static int mem_cgroup_move_account(struct page *page,
				   bool compound,
				   struct mem_cgroup *from,
				   struct mem_cgroup *to)
{
	unsigned long flags;
	unsigned int nr_pages = compound ? hpage_nr_pages(page) : 1;
	int ret;
	bool anon;

	VM_BUG_ON(from == to);
	VM_BUG_ON_PAGE(PageLRU(page), page);
	VM_BUG_ON(compound && !PageTransHuge(page));

	/*
	 * Prevent mem_cgroup_migrate() from looking at
	 * page->mem_cgroup of its source page while we change it.
	 */
	ret = -EBUSY;
	if (!trylock_page(page))
		goto out;

	ret = -EINVAL;
	if (page->mem_cgroup != from)
		goto out_unlock;

	anon = PageAnon(page);

	spin_lock_irqsave(&from->move_lock, flags);

	if (!anon && page_mapped(page)) {
		__this_cpu_sub(from->stat->count[NR_FILE_MAPPED], nr_pages);
		__this_cpu_add(to->stat->count[NR_FILE_MAPPED], nr_pages);
	}

	/*
	 * move_lock grabbed above and caller set from->moving_account, so
	 * mod_memcg_page_state will serialize updates to PageDirty.
	 * So mapping should be stable for dirty pages.
	 */
	if (!anon && PageDirty(page)) {
		struct address_space *mapping = page_mapping(page);

		if (mapping_cap_account_dirty(mapping)) {
			__this_cpu_sub(from->stat->count[NR_FILE_DIRTY],
				       nr_pages);
			__this_cpu_add(to->stat->count[NR_FILE_DIRTY],
				       nr_pages);
		}
	}

	if (PageWriteback(page)) {
		__this_cpu_sub(from->stat->count[NR_WRITEBACK], nr_pages);
		__this_cpu_add(to->stat->count[NR_WRITEBACK], nr_pages);
	}

	/*
	 * It is safe to change page->mem_cgroup here because the page
	 * is referenced, charged, and isolated - we can't race with
	 * uncharging, charging, migration, or LRU putback.
	 */

	/* caller should have done css_get */
	page->mem_cgroup = to;
	spin_unlock_irqrestore(&from->move_lock, flags);

	ret = 0;

	local_irq_disable();
	mem_cgroup_charge_statistics(to, page, compound, nr_pages);
	memcg_check_events(to, page);
	mem_cgroup_charge_statistics(from, page, compound, -nr_pages);
	memcg_check_events(from, page);
	local_irq_enable();
out_unlock:
	unlock_page(page);
out:
	return ret;
}

/**
 * get_mctgt_type - get target type of moving charge
 * @vma: the vma the pte to be checked belongs
 * @addr: the address corresponding to the pte to be checked
 * @ptent: the pte to be checked
 * @target: the pointer the target page or swap ent will be stored(can be NULL)
 *
 * Returns
 *   0(MC_TARGET_NONE): if the pte is not a target for move charge.
 *   1(MC_TARGET_PAGE): if the page corresponding to this pte is a target for
 *     move charge. if @target is not NULL, the page is stored in target->page
 *     with extra refcnt got(Callers should handle it).
 *   2(MC_TARGET_SWAP): if the swap entry corresponding to this pte is a
 *     target for charge migration. if @target is not NULL, the entry is stored
 *     in target->ent.
 *   3(MC_TARGET_DEVICE): like MC_TARGET_PAGE  but page is MEMORY_DEVICE_PUBLIC
 *     or MEMORY_DEVICE_PRIVATE (so ZONE_DEVICE page and thus not on the lru).
 *     For now we such page is charge like a regular page would be as for all
 *     intent and purposes it is just special memory taking the place of a
 *     regular page.
 *
 *     See Documentations/vm/hmm.txt and include/linux/hmm.h
 *
 * Called with pte lock held.
 */

static enum mc_target_type get_mctgt_type(struct vm_area_struct *vma,
		unsigned long addr, pte_t ptent, union mc_target *target)
{
	struct page *page = NULL;
	enum mc_target_type ret = MC_TARGET_NONE;
	swp_entry_t ent = { .val = 0 };

	if (pte_present(ptent))
		page = mc_handle_present_pte(vma, addr, ptent);
	else if (is_swap_pte(ptent))
		page = mc_handle_swap_pte(vma, ptent, &ent);
	else if (pte_none(ptent))
		page = mc_handle_file_pte(vma, addr, ptent, &ent);

	if (!page && !ent.val)
		return ret;
	if (page) {
		/*
		 * Do only loose check w/o serialization.
		 * mem_cgroup_move_account() checks the page is valid or
		 * not under LRU exclusion.
		 */
		if (page->mem_cgroup == mc.from) {
			ret = MC_TARGET_PAGE;
			if (is_device_private_page(page) ||
			    is_device_public_page(page))
				ret = MC_TARGET_DEVICE;
			if (target)
				target->page = page;
		}
		if (!ret || !target)
			put_page(page);
	}
	/*
	 * There is a swap entry and a page doesn't exist or isn't charged.
	 * But we cannot move a tail-page in a THP.
	 */
	if (ent.val && !ret && (!page || !PageTransCompound(page)) &&
	    mem_cgroup_id(mc.from) == lookup_swap_cgroup_id(ent)) {
		ret = MC_TARGET_SWAP;
		if (target)
			target->ent = ent;
	}
	return ret;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 * We don't consider PMD mapped swapping or file mapped pages because THP does
 * not support them for now.
 * Caller should make sure that pmd_trans_huge(pmd) is true.
 */
static enum mc_target_type get_mctgt_type_thp(struct vm_area_struct *vma,
		unsigned long addr, pmd_t pmd, union mc_target *target)
{
	struct page *page = NULL;
	enum mc_target_type ret = MC_TARGET_NONE;

	if (unlikely(is_swap_pmd(pmd))) {
		VM_BUG_ON(thp_migration_supported() &&
				  !is_pmd_migration_entry(pmd));
		return ret;
	}
	page = pmd_page(pmd);
	VM_BUG_ON_PAGE(!page || !PageHead(page), page);
	if (!(mc.flags & MOVE_ANON))
		return ret;
	if (page->mem_cgroup == mc.from) {
		ret = MC_TARGET_PAGE;
		if (target) {
			get_page(page);
			target->page = page;
		}
	}
	return ret;
}
#else
static inline enum mc_target_type get_mctgt_type_thp(struct vm_area_struct *vma,
		unsigned long addr, pmd_t pmd, union mc_target *target)
{
	return MC_TARGET_NONE;
}
#endif

static int mem_cgroup_count_precharge_pte_range(pmd_t *pmd,
					unsigned long addr, unsigned long end,
					struct mm_walk *walk)
{
	struct vm_area_struct *vma = walk->vma;
	pte_t *pte;
	spinlock_t *ptl;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		/*
		 * Note their can not be MC_TARGET_DEVICE for now as we do not
		 * support transparent huge page with MEMORY_DEVICE_PUBLIC or
		 * MEMORY_DEVICE_PRIVATE but this might change.
		 */
		if (get_mctgt_type_thp(vma, addr, *pmd, NULL) == MC_TARGET_PAGE)
			mc.precharge += HPAGE_PMD_NR;
		spin_unlock(ptl);
		return 0;
	}

	if (pmd_trans_unstable(pmd))
		return 0;
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; pte++, addr += PAGE_SIZE)
		if (get_mctgt_type(vma, addr, *pte, NULL))
			mc.precharge++;	/* increment precharge temporarily */
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();

	return 0;
}

static unsigned long mem_cgroup_count_precharge(struct mm_struct *mm)
{
	unsigned long precharge;

	struct mm_walk mem_cgroup_count_precharge_walk = {
		.pmd_entry = mem_cgroup_count_precharge_pte_range,
		.mm = mm,
	};
	down_read(&mm->mmap_sem);
	walk_page_range(0, mm->highest_vm_end,
			&mem_cgroup_count_precharge_walk);
	up_read(&mm->mmap_sem);

	precharge = mc.precharge;
	mc.precharge = 0;

	return precharge;
}

static int mem_cgroup_precharge_mc(struct mm_struct *mm)
{
	unsigned long precharge = mem_cgroup_count_precharge(mm);

	VM_BUG_ON(mc.moving_task);
	mc.moving_task = current;
	return mem_cgroup_do_precharge(precharge);
}

/* cancels all extra charges on mc.from and mc.to, and wakes up all waiters. */
static void __mem_cgroup_clear_mc(void)
{
	struct mem_cgroup *from = mc.from;
	struct mem_cgroup *to = mc.to;

	/* we must uncharge all the leftover precharges from mc.to */
	if (mc.precharge) {
		cancel_charge(mc.to, mc.precharge);
		mc.precharge = 0;
	}
	/*
	 * we didn't uncharge from mc.from at mem_cgroup_move_account(), so
	 * we must uncharge here.
	 */
	if (mc.moved_charge) {
		cancel_charge(mc.from, mc.moved_charge);
		mc.moved_charge = 0;
	}
	/* we must fixup refcnts and charges */
	if (mc.moved_swap) {
		/* uncharge swap account from the old cgroup */
		if (!mem_cgroup_is_root(mc.from))
			page_counter_uncharge(&mc.from->memsw, mc.moved_swap);

		mem_cgroup_id_put_many(mc.from, mc.moved_swap);

		/*
		 * we charged both to->memory and to->memsw, so we
		 * should uncharge to->memory.
		 */
		if (!mem_cgroup_is_root(mc.to))
			page_counter_uncharge(&mc.to->memory, mc.moved_swap);

		mem_cgroup_id_get_many(mc.to, mc.moved_swap);
		css_put_many(&mc.to->css, mc.moved_swap);

		mc.moved_swap = 0;
	}
	memcg_oom_recover(from);
	memcg_oom_recover(to);
	wake_up_all(&mc.waitq);
}

static void mem_cgroup_clear_mc(void)
{
	struct mm_struct *mm = mc.mm;

	/*
	 * we must clear moving_task before waking up waiters at the end of
	 * task migration.
	 */
	mc.moving_task = NULL;
	__mem_cgroup_clear_mc();
	spin_lock(&mc.lock);
	mc.from = NULL;
	mc.to = NULL;
	mc.mm = NULL;
	spin_unlock(&mc.lock);

	mmput(mm);
}

static int mem_cgroup_can_attach(struct cgroup_taskset *tset)
{
	struct cgroup_subsys_state *css;
	struct mem_cgroup *memcg = NULL; /* unneeded init to make gcc happy */
	struct mem_cgroup *from;
	struct task_struct *leader, *p;
	struct mm_struct *mm;
	unsigned long move_flags;
	int ret = 0;

	/* charge immigration isn't supported on the default hierarchy */
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return 0;

	/*
	 * Multi-process migrations only happen on the default hierarchy
	 * where charge immigration is not used.  Perform charge
	 * immigration if @tset contains a leader and whine if there are
	 * multiple.
	 */
	p = NULL;
	cgroup_taskset_for_each_leader(leader, css, tset) {
		WARN_ON_ONCE(p);
		p = leader;
		memcg = mem_cgroup_from_css(css);
	}
	if (!p)
		return 0;

	/*
	 * We are now commited to this value whatever it is. Changes in this
	 * tunable will only affect upcoming migrations, not the current one.
	 * So we need to save it, and keep it going.
	 */
	move_flags = READ_ONCE(memcg->move_charge_at_immigrate);
	if (!move_flags)
		return 0;

	from = mem_cgroup_from_task(p);

	VM_BUG_ON(from == memcg);

	mm = get_task_mm(p);
	if (!mm)
		return 0;
	/* We move charges only when we move a owner of the mm */
	if (mm->owner == p) {
		VM_BUG_ON(mc.from);
		VM_BUG_ON(mc.to);
		VM_BUG_ON(mc.precharge);
		VM_BUG_ON(mc.moved_charge);
		VM_BUG_ON(mc.moved_swap);

		spin_lock(&mc.lock);
		mc.mm = mm;
		mc.from = from;
		mc.to = memcg;
		mc.flags = move_flags;
		spin_unlock(&mc.lock);
		/* We set mc.moving_task later */

		ret = mem_cgroup_precharge_mc(mm);
		if (ret)
			mem_cgroup_clear_mc();
	} else {
		mmput(mm);
	}
	return ret;
}

static void mem_cgroup_cancel_attach(struct cgroup_taskset *tset)
{
	if (mc.to)
		mem_cgroup_clear_mc();
}

static int mem_cgroup_move_charge_pte_range(pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct mm_walk *walk)
{
	int ret = 0;
	struct vm_area_struct *vma = walk->vma;
	pte_t *pte;
	spinlock_t *ptl;
	enum mc_target_type target_type;
	union mc_target target;
	struct page *page;

	ptl = pmd_trans_huge_lock(pmd, vma);
	if (ptl) {
		if (mc.precharge < HPAGE_PMD_NR) {
			spin_unlock(ptl);
			return 0;
		}
		target_type = get_mctgt_type_thp(vma, addr, *pmd, &target);
		if (target_type == MC_TARGET_PAGE) {
			page = target.page;
			if (!isolate_lru_page(page)) {
				if (!mem_cgroup_move_account(page, true,
							     mc.from, mc.to)) {
					mc.precharge -= HPAGE_PMD_NR;
					mc.moved_charge += HPAGE_PMD_NR;
				}
				putback_lru_page(page);
			}
			put_page(page);
		} else if (target_type == MC_TARGET_DEVICE) {
			page = target.page;
			if (!mem_cgroup_move_account(page, true,
						     mc.from, mc.to)) {
				mc.precharge -= HPAGE_PMD_NR;
				mc.moved_charge += HPAGE_PMD_NR;
			}
			put_page(page);
		}
		spin_unlock(ptl);
		return 0;
	}

	if (pmd_trans_unstable(pmd))
		return 0;
retry:
	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	for (; addr != end; addr += PAGE_SIZE) {
		pte_t ptent = *(pte++);
		bool device = false;
		swp_entry_t ent;

		if (!mc.precharge)
			break;

		switch (get_mctgt_type(vma, addr, ptent, &target)) {
		case MC_TARGET_DEVICE:
			device = true;
			/* fall through */
		case MC_TARGET_PAGE:
			page = target.page;
			/*
			 * We can have a part of the split pmd here. Moving it
			 * can be done but it would be too convoluted so simply
			 * ignore such a partial THP and keep it in original
			 * memcg. There should be somebody mapping the head.
			 */
			if (PageTransCompound(page))
				goto put;
			if (!device && isolate_lru_page(page))
				goto put;
			if (!mem_cgroup_move_account(page, false,
						mc.from, mc.to)) {
				mc.precharge--;
				/* we uncharge from mc.from later. */
				mc.moved_charge++;
			}
			if (!device)
				putback_lru_page(page);
put:			/* get_mctgt_type() gets the page */
			put_page(page);
			break;
		case MC_TARGET_SWAP:
			ent = target.ent;
			if (!mem_cgroup_move_swap_account(ent, mc.from, mc.to)) {
				mc.precharge--;
				/* we fixup refcnts and charges later. */
				mc.moved_swap++;
			}
			break;
		default:
			break;
		}
	}
	pte_unmap_unlock(pte - 1, ptl);
	cond_resched();

	if (addr != end) {
		/*
		 * We have consumed all precharges we got in can_attach().
		 * We try charge one by one, but don't do any additional
		 * charges to mc.to if we have failed in charge once in attach()
		 * phase.
		 */
		ret = mem_cgroup_do_precharge(1);
		if (!ret)
			goto retry;
	}

	return ret;
}

static void mem_cgroup_move_charge(void)
{
	struct mm_walk mem_cgroup_move_charge_walk = {
		.pmd_entry = mem_cgroup_move_charge_pte_range,
		.mm = mc.mm,
	};

	lru_add_drain_all();
	/*
	 * Signal lock_page_memcg() to take the memcg's move_lock
	 * while we're moving its pages to another memcg. Then wait
	 * for already started RCU-only updates to finish.
	 */
	atomic_inc(&mc.from->moving_account);
	synchronize_rcu();
retry:
	if (unlikely(!down_read_trylock(&mc.mm->mmap_sem))) {
		/*
		 * Someone who are holding the mmap_sem might be waiting in
		 * waitq. So we cancel all extra charges, wake up all waiters,
		 * and retry. Because we cancel precharges, we might not be able
		 * to move enough charges, but moving charge is a best-effort
		 * feature anyway, so it wouldn't be a big problem.
		 */
		__mem_cgroup_clear_mc();
		cond_resched();
		goto retry;
	}
	/*
	 * When we have consumed all precharges and failed in doing
	 * additional charge, the page walk just aborts.
	 */
	walk_page_range(0, mc.mm->highest_vm_end, &mem_cgroup_move_charge_walk);

	up_read(&mc.mm->mmap_sem);
	atomic_dec(&mc.from->moving_account);
}

static void mem_cgroup_move_task(void)
{
	if (mc.to) {
		mem_cgroup_move_charge();
		mem_cgroup_clear_mc();
	}
}
#else	/* !CONFIG_MMU */
static int mem_cgroup_can_attach(struct cgroup_taskset *tset)
{
	return 0;
}
static void mem_cgroup_cancel_attach(struct cgroup_taskset *tset)
{
}
static void mem_cgroup_move_task(void)
{
}
#endif

/*
 * Cgroup retains root cgroups across [un]mount cycles making it necessary
 * to verify whether we're attached to the default hierarchy on each mount
 * attempt.
 */
static void mem_cgroup_bind(struct cgroup_subsys_state *root_css)
{
	/*
	 * use_hierarchy is forced on the default hierarchy.  cgroup core
	 * guarantees that @root doesn't have any children, so turning it
	 * on for the root memcg is enough.
	 */
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		root_mem_cgroup->use_hierarchy = true;
	else
		root_mem_cgroup->use_hierarchy = false;
}

static u64 memory_current_read(struct cgroup_subsys_state *css,
			       struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	return (u64)page_counter_read(&memcg->memory) * PAGE_SIZE;
}

static int memory_low_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	unsigned long low = READ_ONCE(memcg->low);

	if (low == PAGE_COUNTER_MAX)
		seq_puts(m, "max\n");
	else
		seq_printf(m, "%llu\n", (u64)low * PAGE_SIZE);

	return 0;
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

	memcg->low = low;

	return nbytes;
}

static int memory_high_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	unsigned long high = READ_ONCE(memcg->high);

	if (high == PAGE_COUNTER_MAX)
		seq_puts(m, "max\n");
	else
		seq_printf(m, "%llu\n", (u64)high * PAGE_SIZE);

	return 0;
}

static ssize_t memory_high_write(struct kernfs_open_file *of,
				 char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long nr_pages;
	unsigned long high;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &high);
	if (err)
		return err;

	memcg->high = high;

	nr_pages = page_counter_read(&memcg->memory);
	if (nr_pages > high)
		try_to_free_mem_cgroup_pages(memcg, nr_pages - high,
					     GFP_KERNEL, true);

	memcg_wb_domain_size_changed(memcg);
	return nbytes;
}

static int memory_max_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	unsigned long max = READ_ONCE(memcg->memory.limit);

	if (max == PAGE_COUNTER_MAX)
		seq_puts(m, "max\n");
	else
		seq_printf(m, "%llu\n", (u64)max * PAGE_SIZE);

	return 0;
}

static ssize_t memory_max_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned int nr_reclaims = MEM_CGROUP_RECLAIM_RETRIES;
	bool drained = false;
	unsigned long max;
	int err;

	buf = strstrip(buf);
	err = page_counter_memparse(buf, "max", &max);
	if (err)
		return err;

	xchg(&memcg->memory.limit, max);

	for (;;) {
		unsigned long nr_pages = page_counter_read(&memcg->memory);

		if (nr_pages <= max)
			break;

		if (signal_pending(current)) {
			err = -EINTR;
			break;
		}

		if (!drained) {
			drain_all_stock(memcg);
			drained = true;
			continue;
		}

		if (nr_reclaims) {
			if (!try_to_free_mem_cgroup_pages(memcg, nr_pages - max,
							  GFP_KERNEL, true))
				nr_reclaims--;
			continue;
		}

		mem_cgroup_event(memcg, MEMCG_OOM);
		if (!mem_cgroup_out_of_memory(memcg, GFP_KERNEL, 0))
			break;
	}

	memcg_wb_domain_size_changed(memcg);
	return nbytes;
}

static int memory_events_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));

	seq_printf(m, "low %lu\n", memcg_sum_events(memcg, MEMCG_LOW));
	seq_printf(m, "high %lu\n", memcg_sum_events(memcg, MEMCG_HIGH));
	seq_printf(m, "max %lu\n", memcg_sum_events(memcg, MEMCG_MAX));
	seq_printf(m, "oom %lu\n", memcg_sum_events(memcg, MEMCG_OOM));
	seq_printf(m, "oom_kill %lu\n", memcg_sum_events(memcg, OOM_KILL));

	return 0;
}

static int memory_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	unsigned long stat[MEMCG_NR_STAT];
	unsigned long events[MEMCG_NR_EVENTS];
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

	tree_stat(memcg, stat);
	tree_events(memcg, events);

	seq_printf(m, "anon %llu\n",
		   (u64)stat[MEMCG_RSS] * PAGE_SIZE);
	seq_printf(m, "file %llu\n",
		   (u64)stat[MEMCG_CACHE] * PAGE_SIZE);
	seq_printf(m, "kernel_stack %llu\n",
		   (u64)stat[MEMCG_KERNEL_STACK_KB] * 1024);
	seq_printf(m, "slab %llu\n",
		   (u64)(stat[NR_SLAB_RECLAIMABLE] +
			 stat[NR_SLAB_UNRECLAIMABLE]) * PAGE_SIZE);
	seq_printf(m, "sock %llu\n",
		   (u64)stat[MEMCG_SOCK] * PAGE_SIZE);

	seq_printf(m, "shmem %llu\n",
		   (u64)stat[NR_SHMEM] * PAGE_SIZE);
	seq_printf(m, "file_mapped %llu\n",
		   (u64)stat[NR_FILE_MAPPED] * PAGE_SIZE);
	seq_printf(m, "file_dirty %llu\n",
		   (u64)stat[NR_FILE_DIRTY] * PAGE_SIZE);
	seq_printf(m, "file_writeback %llu\n",
		   (u64)stat[NR_WRITEBACK] * PAGE_SIZE);

	for (i = 0; i < NR_LRU_LISTS; i++) {
		struct mem_cgroup *mi;
		unsigned long val = 0;

		for_each_mem_cgroup_tree(mi, memcg)
			val += mem_cgroup_nr_lru_pages(mi, BIT(i));
		seq_printf(m, "%s %llu\n",
			   mem_cgroup_lru_names[i], (u64)val * PAGE_SIZE);
	}

	seq_printf(m, "slab_reclaimable %llu\n",
		   (u64)stat[NR_SLAB_RECLAIMABLE] * PAGE_SIZE);
	seq_printf(m, "slab_unreclaimable %llu\n",
		   (u64)stat[NR_SLAB_UNRECLAIMABLE] * PAGE_SIZE);

	/* Accumulated memory events */

	seq_printf(m, "pgfault %lu\n", events[PGFAULT]);
	seq_printf(m, "pgmajfault %lu\n", events[PGMAJFAULT]);

	seq_printf(m, "pgrefill %lu\n", events[PGREFILL]);
	seq_printf(m, "pgscan %lu\n", events[PGSCAN_KSWAPD] +
		   events[PGSCAN_DIRECT]);
	seq_printf(m, "pgsteal %lu\n", events[PGSTEAL_KSWAPD] +
		   events[PGSTEAL_DIRECT]);
	seq_printf(m, "pgactivate %lu\n", events[PGACTIVATE]);
	seq_printf(m, "pgdeactivate %lu\n", events[PGDEACTIVATE]);
	seq_printf(m, "pglazyfree %lu\n", events[PGLAZYFREE]);
	seq_printf(m, "pglazyfreed %lu\n", events[PGLAZYFREED]);

	seq_printf(m, "workingset_refault %lu\n",
		   stat[WORKINGSET_REFAULT]);
	seq_printf(m, "workingset_activate %lu\n",
		   stat[WORKINGSET_ACTIVATE]);
	seq_printf(m, "workingset_nodereclaim %lu\n",
		   stat[WORKINGSET_NODERECLAIM]);

	return 0;
}

static struct cftype memory_files[] = {
	{
		.name = "current",
		.flags = CFTYPE_NOT_ON_ROOT,
		.read_u64 = memory_current_read,
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
		.name = "stat",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = memory_stat_show,
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
	.can_attach = mem_cgroup_can_attach,
	.cancel_attach = mem_cgroup_cancel_attach,
	.post_attach = mem_cgroup_move_task,
	.bind = mem_cgroup_bind,
	.dfl_cftypes = memory_files,
	.legacy_cftypes = mem_cgroup_legacy_files,
	.early_init = 0,
};

/**
 * mem_cgroup_low - check if memory consumption is below the normal range
 * @root: the top ancestor of the sub-tree being checked
 * @memcg: the memory cgroup to check
 *
 * Returns %true if memory consumption of @memcg, and that of all
 * ancestors up to (but not including) @root, is below the normal range.
 *
 * @root is exclusive; it is never low when looked at directly and isn't
 * checked when traversing the hierarchy.
 *
 * Excluding @root enables using memory.low to prioritize memory usage
 * between cgroups within a subtree of the hierarchy that is limited by
 * memory.high or memory.max.
 *
 * For example, given cgroup A with children B and C:
 *
 *    A
 *   / \
 *  B   C
 *
 * and
 *
 *  1. A/memory.current > A/memory.high
 *  2. A/B/memory.current < A/B/memory.low
 *  3. A/C/memory.current >= A/C/memory.low
 *
 * As 'A' is high, i.e. triggers reclaim from 'A', and 'B' is low, we
 * should reclaim from 'C' until 'A' is no longer high or until we can
 * no longer reclaim from 'C'.  If 'A', i.e. @root, isn't excluded by
 * mem_cgroup_low when reclaming from 'A', then 'B' won't be considered
 * low and we will reclaim indiscriminately from both 'B' and 'C'.
 */
bool mem_cgroup_low(struct mem_cgroup *root, struct mem_cgroup *memcg)
{
	if (mem_cgroup_disabled())
		return false;

	if (!root)
		root = root_mem_cgroup;
	if (memcg == root)
		return false;

	for (; memcg != root; memcg = parent_mem_cgroup(memcg)) {
		if (page_counter_read(&memcg->memory) >= memcg->low)
			return false;
	}

	return true;
}

/**
 * mem_cgroup_try_charge - try charging a page
 * @page: page to charge
 * @mm: mm context of the victim
 * @gfp_mask: reclaim mode
 * @memcgp: charged memcg return
 * @compound: charge the page as compound or small page
 *
 * Try to charge @page to the memcg that @mm belongs to, reclaiming
 * pages according to @gfp_mask if necessary.
 *
 * Returns 0 on success, with *@memcgp pointing to the charged memcg.
 * Otherwise, an error code is returned.
 *
 * After page->mapping has been set up, the caller must finalize the
 * charge with mem_cgroup_commit_charge().  Or abort the transaction
 * with mem_cgroup_cancel_charge() in case page instantiation fails.
 */
int mem_cgroup_try_charge(struct page *page, struct mm_struct *mm,
			  gfp_t gfp_mask, struct mem_cgroup **memcgp,
			  bool compound)
{
	struct mem_cgroup *memcg = NULL;
	unsigned int nr_pages = compound ? hpage_nr_pages(page) : 1;
	int ret = 0;

	if (mem_cgroup_disabled())
		goto out;

	if (PageSwapCache(page)) {
		/*
		 * Every swap fault against a single page tries to charge the
		 * page, bail as early as possible.  shmem_unuse() encounters
		 * already charged pages, too.  The USED bit is protected by
		 * the page lock, which serializes swap cache removal, which
		 * in turn serializes uncharging.
		 */
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		if (compound_head(page)->mem_cgroup)
			goto out;

		if (do_swap_account) {
			swp_entry_t ent = { .val = page_private(page), };
			unsigned short id = lookup_swap_cgroup_id(ent);

			rcu_read_lock();
			memcg = mem_cgroup_from_id(id);
			if (memcg && !css_tryget_online(&memcg->css))
				memcg = NULL;
			rcu_read_unlock();
		}
	}

	if (!memcg)
		memcg = get_mem_cgroup_from_mm(mm);

	ret = try_charge(memcg, gfp_mask, nr_pages);

	css_put(&memcg->css);
out:
	*memcgp = memcg;
	return ret;
}

/**
 * mem_cgroup_commit_charge - commit a page charge
 * @page: page to charge
 * @memcg: memcg to charge the page to
 * @lrucare: page might be on LRU already
 * @compound: charge the page as compound or small page
 *
 * Finalize a charge transaction started by mem_cgroup_try_charge(),
 * after page->mapping has been set up.  This must happen atomically
 * as part of the page instantiation, i.e. under the page table lock
 * for anonymous pages, under the page lock for page and swap cache.
 *
 * In addition, the page must not be on the LRU during the commit, to
 * prevent racing with task migration.  If it might be, use @lrucare.
 *
 * Use mem_cgroup_cancel_charge() to cancel the transaction instead.
 */
void mem_cgroup_commit_charge(struct page *page, struct mem_cgroup *memcg,
			      bool lrucare, bool compound)
{
	unsigned int nr_pages = compound ? hpage_nr_pages(page) : 1;

	VM_BUG_ON_PAGE(!page->mapping, page);
	VM_BUG_ON_PAGE(PageLRU(page) && !lrucare, page);

	if (mem_cgroup_disabled())
		return;
	/*
	 * Swap faults will attempt to charge the same page multiple
	 * times.  But reuse_swap_page() might have removed the page
	 * from swapcache already, so we can't check PageSwapCache().
	 */
	if (!memcg)
		return;

	commit_charge(page, memcg, lrucare);

	local_irq_disable();
	mem_cgroup_charge_statistics(memcg, page, compound, nr_pages);
	memcg_check_events(memcg, page);
	local_irq_enable();

	if (do_memsw_account() && PageSwapCache(page)) {
		swp_entry_t entry = { .val = page_private(page) };
		/*
		 * The swap entry might not get freed for a long time,
		 * let's not wait for it.  The page already received a
		 * memory+swap charge, drop the swap entry duplicate.
		 */
		mem_cgroup_uncharge_swap(entry, nr_pages);
	}
}

/**
 * mem_cgroup_cancel_charge - cancel a page charge
 * @page: page to charge
 * @memcg: memcg to charge the page to
 * @compound: charge the page as compound or small page
 *
 * Cancel a charge transaction started by mem_cgroup_try_charge().
 */
void mem_cgroup_cancel_charge(struct page *page, struct mem_cgroup *memcg,
		bool compound)
{
	unsigned int nr_pages = compound ? hpage_nr_pages(page) : 1;

	if (mem_cgroup_disabled())
		return;
	/*
	 * Swap faults will attempt to charge the same page multiple
	 * times.  But reuse_swap_page() might have removed the page
	 * from swapcache already, so we can't check PageSwapCache().
	 */
	if (!memcg)
		return;

	cancel_charge(memcg, nr_pages);
}

struct uncharge_gather {
	struct mem_cgroup *memcg;
	unsigned long pgpgout;
	unsigned long nr_anon;
	unsigned long nr_file;
	unsigned long nr_kmem;
	unsigned long nr_huge;
	unsigned long nr_shmem;
	struct page *dummy_page;
};

static inline void uncharge_gather_clear(struct uncharge_gather *ug)
{
	memset(ug, 0, sizeof(*ug));
}

static void uncharge_batch(const struct uncharge_gather *ug)
{
	unsigned long nr_pages = ug->nr_anon + ug->nr_file + ug->nr_kmem;
	unsigned long flags;

	if (!mem_cgroup_is_root(ug->memcg)) {
		page_counter_uncharge(&ug->memcg->memory, nr_pages);
		if (do_memsw_account())
			page_counter_uncharge(&ug->memcg->memsw, nr_pages);
		if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && ug->nr_kmem)
			page_counter_uncharge(&ug->memcg->kmem, ug->nr_kmem);
		memcg_oom_recover(ug->memcg);
	}

	local_irq_save(flags);
	__this_cpu_sub(ug->memcg->stat->count[MEMCG_RSS], ug->nr_anon);
	__this_cpu_sub(ug->memcg->stat->count[MEMCG_CACHE], ug->nr_file);
	__this_cpu_sub(ug->memcg->stat->count[MEMCG_RSS_HUGE], ug->nr_huge);
	__this_cpu_sub(ug->memcg->stat->count[NR_SHMEM], ug->nr_shmem);
	__this_cpu_add(ug->memcg->stat->events[PGPGOUT], ug->pgpgout);
	__this_cpu_add(ug->memcg->stat->nr_page_events, nr_pages);
	memcg_check_events(ug->memcg, ug->dummy_page);
	local_irq_restore(flags);

	if (!mem_cgroup_is_root(ug->memcg))
		css_put_many(&ug->memcg->css, nr_pages);
}

static void uncharge_page(struct page *page, struct uncharge_gather *ug)
{
	VM_BUG_ON_PAGE(PageLRU(page), page);
	VM_BUG_ON_PAGE(!PageHWPoison(page) && page_count(page), page);

	if (!page->mem_cgroup)
		return;

	/*
	 * Nobody should be changing or seriously looking at
	 * page->mem_cgroup at this point, we have fully
	 * exclusive access to the page.
	 */

	if (ug->memcg != page->mem_cgroup) {
		if (ug->memcg) {
			uncharge_batch(ug);
			uncharge_gather_clear(ug);
		}
		ug->memcg = page->mem_cgroup;
	}

	if (!PageKmemcg(page)) {
		unsigned int nr_pages = 1;

		if (PageTransHuge(page)) {
			nr_pages <<= compound_order(page);
			ug->nr_huge += nr_pages;
		}
		if (PageAnon(page))
			ug->nr_anon += nr_pages;
		else {
			ug->nr_file += nr_pages;
			if (PageSwapBacked(page))
				ug->nr_shmem += nr_pages;
		}
		ug->pgpgout++;
	} else {
		ug->nr_kmem += 1 << compound_order(page);
		__ClearPageKmemcg(page);
	}

	ug->dummy_page = page;
	page->mem_cgroup = NULL;
}

static void uncharge_list(struct list_head *page_list)
{
	struct uncharge_gather ug;
	struct list_head *next;

	uncharge_gather_clear(&ug);

	/*
	 * Note that the list can be a single page->lru; hence the
	 * do-while loop instead of a simple list_for_each_entry().
	 */
	next = page_list->next;
	do {
		struct page *page;

		page = list_entry(next, struct page, lru);
		next = page->lru.next;

		uncharge_page(page, &ug);
	} while (next != page_list);

	if (ug.memcg)
		uncharge_batch(&ug);
}

/**
 * mem_cgroup_uncharge - uncharge a page
 * @page: page to uncharge
 *
 * Uncharge a page previously charged with mem_cgroup_try_charge() and
 * mem_cgroup_commit_charge().
 */
void mem_cgroup_uncharge(struct page *page)
{
	struct uncharge_gather ug;

	if (mem_cgroup_disabled())
		return;

	/* Don't touch page->lru of any random page, pre-check: */
	if (!page->mem_cgroup)
		return;

	uncharge_gather_clear(&ug);
	uncharge_page(page, &ug);
	uncharge_batch(&ug);
}

/**
 * mem_cgroup_uncharge_list - uncharge a list of page
 * @page_list: list of pages to uncharge
 *
 * Uncharge a list of pages previously charged with
 * mem_cgroup_try_charge() and mem_cgroup_commit_charge().
 */
void mem_cgroup_uncharge_list(struct list_head *page_list)
{
	if (mem_cgroup_disabled())
		return;

	if (!list_empty(page_list))
		uncharge_list(page_list);
}

/**
 * mem_cgroup_migrate - charge a page's replacement
 * @oldpage: currently circulating page
 * @newpage: replacement page
 *
 * Charge @newpage as a replacement page for @oldpage. @oldpage will
 * be uncharged upon free.
 *
 * Both pages must be locked, @newpage->mapping must be set up.
 */
void mem_cgroup_migrate(struct page *oldpage, struct page *newpage)
{
	struct mem_cgroup *memcg;
	unsigned int nr_pages;
	bool compound;
	unsigned long flags;

	VM_BUG_ON_PAGE(!PageLocked(oldpage), oldpage);
	VM_BUG_ON_PAGE(!PageLocked(newpage), newpage);
	VM_BUG_ON_PAGE(PageAnon(oldpage) != PageAnon(newpage), newpage);
	VM_BUG_ON_PAGE(PageTransHuge(oldpage) != PageTransHuge(newpage),
		       newpage);

	if (mem_cgroup_disabled())
		return;

	/* Page cache replacement: new page already charged? */
	if (newpage->mem_cgroup)
		return;

	/* Swapcache readahead pages can get replaced before being charged */
	memcg = oldpage->mem_cgroup;
	if (!memcg)
		return;

	/* Force-charge the new page. The old one will be freed soon */
	compound = PageTransHuge(newpage);
	nr_pages = compound ? hpage_nr_pages(newpage) : 1;

	page_counter_charge(&memcg->memory, nr_pages);
	if (do_memsw_account())
		page_counter_charge(&memcg->memsw, nr_pages);
	css_get_many(&memcg->css, nr_pages);

	commit_charge(newpage, memcg, false);

	local_irq_save(flags);
	mem_cgroup_charge_statistics(memcg, newpage, compound, nr_pages);
	memcg_check_events(memcg, newpage);
	local_irq_restore(flags);
}

DEFINE_STATIC_KEY_FALSE(memcg_sockets_enabled_key);
EXPORT_SYMBOL(memcg_sockets_enabled_key);

void mem_cgroup_sk_alloc(struct sock *sk)
{
	struct mem_cgroup *memcg;

	if (!mem_cgroup_sockets_enabled)
		return;

	/*
	 * Socket cloning can throw us here with sk_memcg already
	 * filled. It won't however, necessarily happen from
	 * process context. So the test for root memcg given
	 * the current task's memcg won't help us in this case.
	 *
	 * Respecting the original socket's memcg is a better
	 * decision in this case.
	 */
	if (sk->sk_memcg) {
		BUG_ON(mem_cgroup_is_root(sk->sk_memcg));
		css_get(&sk->sk_memcg->css);
		return;
	}

	rcu_read_lock();
	memcg = mem_cgroup_from_task(current);
	if (memcg == root_mem_cgroup)
		goto out;
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && !memcg->tcpmem_active)
		goto out;
	if (css_tryget_online(&memcg->css))
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
 *
 * Charges @nr_pages to @memcg. Returns %true if the charge fit within
 * @memcg's configured limit, %false if the charge had to be forced.
 */
bool mem_cgroup_charge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	gfp_t gfp_mask = GFP_KERNEL;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys)) {
		struct page_counter *fail;

		if (page_counter_try_charge(&memcg->tcpmem, nr_pages, &fail)) {
			memcg->tcpmem_pressure = 0;
			return true;
		}
		page_counter_charge(&memcg->tcpmem, nr_pages);
		memcg->tcpmem_pressure = 1;
		return false;
	}

	/* Don't block in the packet receive path */
	if (in_softirq())
		gfp_mask = GFP_NOWAIT;

	this_cpu_add(memcg->stat->count[MEMCG_SOCK], nr_pages);

	if (try_charge(memcg, gfp_mask, nr_pages) == 0)
		return true;

	try_charge(memcg, gfp_mask|__GFP_NOFAIL, nr_pages);
	return false;
}

/**
 * mem_cgroup_uncharge_skmem - uncharge socket memory
 * @memcg - memcg to uncharge
 * @nr_pages - number of pages to uncharge
 */
void mem_cgroup_uncharge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys)) {
		page_counter_uncharge(&memcg->tcpmem, nr_pages);
		return;
	}

	this_cpu_sub(memcg->stat->count[MEMCG_SOCK], nr_pages);

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
	}
	return 0;
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
	int cpu, node;

#ifndef CONFIG_SLOB
	/*
	 * Kmem cache creation is mostly done with the slab_mutex held,
	 * so use a workqueue with limited concurrency to avoid stalling
	 * all worker threads in case lots of cgroups are created and
	 * destroyed simultaneously.
	 */
	memcg_kmem_cache_wq = alloc_workqueue("memcg_kmem_cache", 0, 1);
	BUG_ON(!memcg_kmem_cache_wq);
#endif

	cpuhp_setup_state_nocalls(CPUHP_MM_MEMCQ_DEAD, "mm/memctrl:dead", NULL,
				  memcg_hotplug_cpu_dead);

	for_each_possible_cpu(cpu)
		INIT_WORK(&per_cpu_ptr(&memcg_stock, cpu)->work,
			  drain_local_stock);

	for_each_node(node) {
		struct mem_cgroup_tree_per_node *rtpn;

		rtpn = kzalloc_node(sizeof(*rtpn), GFP_KERNEL,
				    node_online(node) ? node : NUMA_NO_NODE);

		rtpn->rb_root = RB_ROOT;
		rtpn->rb_rightmost = NULL;
		spin_lock_init(&rtpn->lock);
		soft_limit_tree.rb_tree_per_node[node] = rtpn;
	}

	return 0;
}
subsys_initcall(mem_cgroup_init);

#ifdef CONFIG_MEMCG_SWAP
static struct mem_cgroup *mem_cgroup_id_get_online(struct mem_cgroup *memcg)
{
	while (!atomic_inc_not_zero(&memcg->id.ref)) {
		/*
		 * The root cgroup cannot be destroyed, so it's refcount must
		 * always be >= 1.
		 */
		if (WARN_ON_ONCE(memcg == root_mem_cgroup)) {
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
 * @page: page whose memsw charge to transfer
 * @entry: swap entry to move the charge to
 *
 * Transfer the memsw charge of @page to @entry.
 */
void mem_cgroup_swapout(struct page *page, swp_entry_t entry)
{
	struct mem_cgroup *memcg, *swap_memcg;
	unsigned int nr_entries;
	unsigned short oldid;

	VM_BUG_ON_PAGE(PageLRU(page), page);
	VM_BUG_ON_PAGE(page_count(page), page);

	if (!do_memsw_account())
		return;

	memcg = page->mem_cgroup;

	/* Readahead page, never charged */
	if (!memcg)
		return;

	/*
	 * In case the memcg owning these pages has been offlined and doesn't
	 * have an ID allocated to it anymore, charge the closest online
	 * ancestor for the swap instead and transfer the memory+swap charge.
	 */
	swap_memcg = mem_cgroup_id_get_online(memcg);
	nr_entries = hpage_nr_pages(page);
	/* Get references for the tail pages, too */
	if (nr_entries > 1)
		mem_cgroup_id_get_many(swap_memcg, nr_entries - 1);
	oldid = swap_cgroup_record(entry, mem_cgroup_id(swap_memcg),
				   nr_entries);
	VM_BUG_ON_PAGE(oldid, page);
	mem_cgroup_swap_statistics(swap_memcg, nr_entries);

	page->mem_cgroup = NULL;

	if (!mem_cgroup_is_root(memcg))
		page_counter_uncharge(&memcg->memory, nr_entries);

	if (memcg != swap_memcg) {
		if (!mem_cgroup_is_root(swap_memcg))
			page_counter_charge(&swap_memcg->memsw, nr_entries);
		page_counter_uncharge(&memcg->memsw, nr_entries);
	}

	/*
	 * Interrupts should be disabled here because the caller holds the
	 * mapping->tree_lock lock which is taken with interrupts-off. It is
	 * important here to have the interrupts disabled because it is the
	 * only synchronisation we have for udpating the per-CPU variables.
	 */
	VM_BUG_ON(!irqs_disabled());
	mem_cgroup_charge_statistics(memcg, page, PageTransHuge(page),
				     -nr_entries);
	memcg_check_events(memcg, page);

	if (!mem_cgroup_is_root(memcg))
		css_put(&memcg->css);
}

/**
 * mem_cgroup_try_charge_swap - try charging swap space for a page
 * @page: page being added to swap
 * @entry: swap entry to charge
 *
 * Try to charge @page's memcg for the swap space at @entry.
 *
 * Returns 0 on success, -ENOMEM on failure.
 */
int mem_cgroup_try_charge_swap(struct page *page, swp_entry_t entry)
{
	unsigned int nr_pages = hpage_nr_pages(page);
	struct page_counter *counter;
	struct mem_cgroup *memcg;
	unsigned short oldid;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) || !do_swap_account)
		return 0;

	memcg = page->mem_cgroup;

	/* Readahead page, never charged */
	if (!memcg)
		return 0;

	memcg = mem_cgroup_id_get_online(memcg);

	if (!mem_cgroup_is_root(memcg) &&
	    !page_counter_try_charge(&memcg->swap, nr_pages, &counter)) {
		mem_cgroup_id_put(memcg);
		return -ENOMEM;
	}

	/* Get references for the tail pages, too */
	if (nr_pages > 1)
		mem_cgroup_id_get_many(memcg, nr_pages - 1);
	oldid = swap_cgroup_record(entry, mem_cgroup_id(memcg), nr_pages);
	VM_BUG_ON_PAGE(oldid, page);
	mem_cgroup_swap_statistics(memcg, nr_pages);

	return 0;
}

/**
 * mem_cgroup_uncharge_swap - uncharge swap space
 * @entry: swap entry to uncharge
 * @nr_pages: the amount of swap space to uncharge
 */
void mem_cgroup_uncharge_swap(swp_entry_t entry, unsigned int nr_pages)
{
	struct mem_cgroup *memcg;
	unsigned short id;

	if (!do_swap_account)
		return;

	id = swap_cgroup_record(entry, 0, nr_pages);
	rcu_read_lock();
	memcg = mem_cgroup_from_id(id);
	if (memcg) {
		if (!mem_cgroup_is_root(memcg)) {
			if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
				page_counter_uncharge(&memcg->swap, nr_pages);
			else
				page_counter_uncharge(&memcg->memsw, nr_pages);
		}
		mem_cgroup_swap_statistics(memcg, -nr_pages);
		mem_cgroup_id_put_many(memcg, nr_pages);
	}
	rcu_read_unlock();
}

long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg)
{
	long nr_swap_pages = get_nr_swap_pages();

	if (!do_swap_account || !cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return nr_swap_pages;
	for (; memcg != root_mem_cgroup; memcg = parent_mem_cgroup(memcg))
		nr_swap_pages = min_t(long, nr_swap_pages,
				      READ_ONCE(memcg->swap.limit) -
				      page_counter_read(&memcg->swap));
	return nr_swap_pages;
}

bool mem_cgroup_swap_full(struct page *page)
{
	struct mem_cgroup *memcg;

	VM_BUG_ON_PAGE(!PageLocked(page), page);

	if (vm_swap_full())
		return true;
	if (!do_swap_account || !cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return false;

	memcg = page->mem_cgroup;
	if (!memcg)
		return false;

	for (; memcg != root_mem_cgroup; memcg = parent_mem_cgroup(memcg))
		if (page_counter_read(&memcg->swap) * 2 >= memcg->swap.limit)
			return true;

	return false;
}

/* for remember boot option*/
#ifdef CONFIG_MEMCG_SWAP_ENABLED
static int really_do_swap_account __initdata = 1;
#else
static int really_do_swap_account __initdata;
#endif

static int __init enable_swap_account(char *s)
{
	if (!strcmp(s, "1"))
		really_do_swap_account = 1;
	else if (!strcmp(s, "0"))
		really_do_swap_account = 0;
	return 1;
}
__setup("swapaccount=", enable_swap_account);

static u64 swap_current_read(struct cgroup_subsys_state *css,
			     struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	return (u64)page_counter_read(&memcg->swap) * PAGE_SIZE;
}

static int swap_max_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	unsigned long max = READ_ONCE(memcg->swap.limit);

	if (max == PAGE_COUNTER_MAX)
		seq_puts(m, "max\n");
	else
		seq_printf(m, "%llu\n", (u64)max * PAGE_SIZE);

	return 0;
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

	mutex_lock(&memcg_limit_mutex);
	err = page_counter_limit(&memcg->swap, max);
	mutex_unlock(&memcg_limit_mutex);
	if (err)
		return err;

	return nbytes;
}

static struct cftype swap_files[] = {
	{
		.name = "swap.current",
		.flags = CFTYPE_NOT_ON_ROOT,
		.read_u64 = swap_current_read,
	},
	{
		.name = "swap.max",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = swap_max_show,
		.write = swap_max_write,
	},
	{ }	/* terminate */
};

static struct cftype memsw_cgroup_files[] = {
	{
		.name = "memsw.usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_USAGE),
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "memsw.max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_MAX_USAGE),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "memsw.limit_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_LIMIT),
		.write = mem_cgroup_write,
		.read_u64 = mem_cgroup_read_u64,
	},
	{
		.name = "memsw.failcnt",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_FAILCNT),
		.write = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read_u64,
	},
	{ },	/* terminate */
};

static int __init mem_cgroup_swap_init(void)
{
	if (!mem_cgroup_disabled() && really_do_swap_account) {
		do_swap_account = 1;
		WARN_ON(cgroup_add_dfl_cftypes(&memory_cgrp_subsys,
					       swap_files));
		WARN_ON(cgroup_add_legacy_cftypes(&memory_cgrp_subsys,
						  memsw_cgroup_files));
	}
	return 0;
}
subsys_initcall(mem_cgroup_swap_init);

#endif /* CONFIG_MEMCG_SWAP */
