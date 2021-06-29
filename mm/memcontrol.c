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
 */

#include <linux/page_counter.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>
#include <linux/pagewalk.h>
#include <linux/sched/mm.h>
#include <linux/shmem_fs.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/vm_event_item.h>
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
#include <linux/psi.h>
#include <linux/seq_buf.h>
#include "internal.h"
#include <net/sock.h>
#include <net/ip.h>
#include "slab.h"

#include <linux/uaccess.h>

#include <trace/events/vmscan.h>

struct cgroup_subsys memory_cgrp_subsys __read_mostly;
EXPORT_SYMBOL(memory_cgrp_subsys);

struct mem_cgroup *root_mem_cgroup __read_mostly;

/* Active memory cgroup to use from an interrupt context */
DEFINE_PER_CPU(struct mem_cgroup *, int_active_memcg);

/* Socket memory accounting disabled? */
static bool cgroup_memory_nosocket;

/* Kernel memory accounting disabled? */
static bool cgroup_memory_nokmem;

/* Whether the swap controller is active */
#ifdef CONFIG_MEMCG_SWAP
bool cgroup_memory_noswap __read_mostly;
#else
#define cgroup_memory_noswap		1
#endif

#ifdef CONFIG_CGROUP_WRITEBACK
static DECLARE_WAIT_QUEUE_HEAD(memcg_cgwb_frn_waitq);
#endif

/* Whether legacy memory+swap accounting is active */
static bool do_memsw_account(void)
{
	return !cgroup_subsys_on_dfl(memory_cgrp_subsys) && !cgroup_memory_noswap;
}

#define THRESHOLDS_EVENTS_TARGET 128
#define SOFTLIMIT_EVENTS_TARGET 1024

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

static inline bool should_force_charge(void)
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

struct cgroup_subsys_state *vmpressure_to_css(struct vmpressure *vmpr)
{
	return &container_of(vmpr, struct mem_cgroup, vmpressure)->css;
}

#ifdef CONFIG_MEMCG_KMEM
extern spinlock_t css_set_lock;

static void obj_cgroup_release(struct percpu_ref *ref)
{
	struct obj_cgroup *objcg = container_of(ref, struct obj_cgroup, refcnt);
	struct mem_cgroup *memcg;
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

	spin_lock_irqsave(&css_set_lock, flags);
	memcg = obj_cgroup_memcg(objcg);
	if (nr_pages)
		__memcg_kmem_uncharge(memcg, nr_pages);
	list_del(&objcg->list);
	mem_cgroup_put(memcg);
	spin_unlock_irqrestore(&css_set_lock, flags);

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

	spin_lock_irq(&css_set_lock);

	/* Move active objcg to the parent's list */
	xchg(&objcg->memcg, parent);
	css_get(&parent->css);
	list_add(&objcg->list, &parent->objcg_list);

	/* Move already reparented objcgs to the parent's list */
	list_for_each_entry(iter, &memcg->objcg_list, list) {
		css_get(&parent->css);
		xchg(&iter->memcg, parent);
		css_put(&memcg->css);
	}
	list_splice(&memcg->objcg_list, &parent->objcg_list);

	spin_unlock_irq(&css_set_lock);

	percpu_ref_kill(&objcg->refcnt);
}

/*
 * This will be used as a shrinker list's index.
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
 * inlined by the compiler. Since the calls to memcg_slab_pre_alloc_hook() are
 * conditional to this static branch, we'll have to allow modules that does
 * kmem_cache_alloc and the such to see this symbol as well
 */
DEFINE_STATIC_KEY_FALSE(memcg_kmem_enabled_key);
EXPORT_SYMBOL(memcg_kmem_enabled_key);
#endif

static int memcg_shrinker_map_size;
static DEFINE_MUTEX(memcg_shrinker_map_mutex);

static void memcg_free_shrinker_map_rcu(struct rcu_head *head)
{
	kvfree(container_of(head, struct memcg_shrinker_map, rcu));
}

static int memcg_expand_one_shrinker_map(struct mem_cgroup *memcg,
					 int size, int old_size)
{
	struct memcg_shrinker_map *new, *old;
	int nid;

	lockdep_assert_held(&memcg_shrinker_map_mutex);

	for_each_node(nid) {
		old = rcu_dereference_protected(
			mem_cgroup_nodeinfo(memcg, nid)->shrinker_map, true);
		/* Not yet online memcg */
		if (!old)
			return 0;

		new = kvmalloc_node(sizeof(*new) + size, GFP_KERNEL, nid);
		if (!new)
			return -ENOMEM;

		/* Set all old bits, clear all new bits */
		memset(new->map, (int)0xff, old_size);
		memset((void *)new->map + old_size, 0, size - old_size);

		rcu_assign_pointer(memcg->nodeinfo[nid]->shrinker_map, new);
		call_rcu(&old->rcu, memcg_free_shrinker_map_rcu);
	}

	return 0;
}

static void memcg_free_shrinker_maps(struct mem_cgroup *memcg)
{
	struct mem_cgroup_per_node *pn;
	struct memcg_shrinker_map *map;
	int nid;

	if (mem_cgroup_is_root(memcg))
		return;

	for_each_node(nid) {
		pn = mem_cgroup_nodeinfo(memcg, nid);
		map = rcu_dereference_protected(pn->shrinker_map, true);
		if (map)
			kvfree(map);
		rcu_assign_pointer(pn->shrinker_map, NULL);
	}
}

static int memcg_alloc_shrinker_maps(struct mem_cgroup *memcg)
{
	struct memcg_shrinker_map *map;
	int nid, size, ret = 0;

	if (mem_cgroup_is_root(memcg))
		return 0;

	mutex_lock(&memcg_shrinker_map_mutex);
	size = memcg_shrinker_map_size;
	for_each_node(nid) {
		map = kvzalloc_node(sizeof(*map) + size, GFP_KERNEL, nid);
		if (!map) {
			memcg_free_shrinker_maps(memcg);
			ret = -ENOMEM;
			break;
		}
		rcu_assign_pointer(memcg->nodeinfo[nid]->shrinker_map, map);
	}
	mutex_unlock(&memcg_shrinker_map_mutex);

	return ret;
}

int memcg_expand_shrinker_maps(int new_id)
{
	int size, old_size, ret = 0;
	struct mem_cgroup *memcg;

	size = DIV_ROUND_UP(new_id + 1, BITS_PER_LONG) * sizeof(unsigned long);
	old_size = memcg_shrinker_map_size;
	if (size <= old_size)
		return 0;

	mutex_lock(&memcg_shrinker_map_mutex);
	if (!root_mem_cgroup)
		goto unlock;

	for_each_mem_cgroup(memcg) {
		if (mem_cgroup_is_root(memcg))
			continue;
		ret = memcg_expand_one_shrinker_map(memcg, size, old_size);
		if (ret) {
			mem_cgroup_iter_break(NULL, memcg);
			goto unlock;
		}
	}
unlock:
	if (!ret)
		memcg_shrinker_map_size = size;
	mutex_unlock(&memcg_shrinker_map_mutex);
	return ret;
}

void memcg_set_shrinker_bit(struct mem_cgroup *memcg, int nid, int shrinker_id)
{
	if (shrinker_id >= 0 && memcg && !mem_cgroup_is_root(memcg)) {
		struct memcg_shrinker_map *map;

		rcu_read_lock();
		map = rcu_dereference(memcg->nodeinfo[nid]->shrinker_map);
		/* Pairs with smp mb in shrink_slab() */
		smp_mb__before_atomic();
		set_bit(shrinker_id, map->map);
		rcu_read_unlock();
	}
}

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
	memcg = page->mem_cgroup;

	/*
	 * The lowest bit set means that memcg isn't a valid
	 * memcg pointer, but a obj_cgroups pointer.
	 * In this case the page is shared and doesn't belong
	 * to any specific memory cgroup.
	 */
	if ((unsigned long) memcg & 0x1UL)
		memcg = NULL;

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
	    !css_tryget(&mz->memcg->css))
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

/**
 * __mod_memcg_state - update cgroup memory statistics
 * @memcg: the memory cgroup
 * @idx: the stat item - can be enum memcg_stat_item or enum node_stat_item
 * @val: delta to add to the counter, can be negative
 */
void __mod_memcg_state(struct mem_cgroup *memcg, int idx, int val)
{
	long x, threshold = MEMCG_CHARGE_BATCH;

	if (mem_cgroup_disabled())
		return;

	if (memcg_stat_item_in_bytes(idx))
		threshold <<= PAGE_SHIFT;

	x = val + __this_cpu_read(memcg->vmstats_percpu->stat[idx]);
	if (unlikely(abs(x) > threshold)) {
		struct mem_cgroup *mi;

		/*
		 * Batch local counters to keep them in sync with
		 * the hierarchical ones.
		 */
		__this_cpu_add(memcg->vmstats_local->stat[idx], x);
		for (mi = memcg; mi; mi = parent_mem_cgroup(mi))
			atomic_long_add(x, &mi->vmstats[idx]);
		x = 0;
	}
	__this_cpu_write(memcg->vmstats_percpu->stat[idx], x);
}

static struct mem_cgroup_per_node *
parent_nodeinfo(struct mem_cgroup_per_node *pn, int nid)
{
	struct mem_cgroup *parent;

	parent = parent_mem_cgroup(pn->memcg);
	if (!parent)
		return NULL;
	return mem_cgroup_nodeinfo(parent, nid);
}

void __mod_memcg_lruvec_state(struct lruvec *lruvec, enum node_stat_item idx,
			      int val)
{
	struct mem_cgroup_per_node *pn;
	struct mem_cgroup *memcg;
	long x, threshold = MEMCG_CHARGE_BATCH;

	pn = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	memcg = pn->memcg;

	/* Update memcg */
	__mod_memcg_state(memcg, idx, val);

	/* Update lruvec */
	__this_cpu_add(pn->lruvec_stat_local->count[idx], val);

	if (vmstat_item_in_bytes(idx))
		threshold <<= PAGE_SHIFT;

	x = val + __this_cpu_read(pn->lruvec_stat_cpu->count[idx]);
	if (unlikely(abs(x) > threshold)) {
		pg_data_t *pgdat = lruvec_pgdat(lruvec);
		struct mem_cgroup_per_node *pi;

		for (pi = pn; pi; pi = parent_nodeinfo(pi, pgdat->node_id))
			atomic_long_add(x, &pi->lruvec_stat[idx]);
		x = 0;
	}
	__this_cpu_write(pn->lruvec_stat_cpu->count[idx], x);
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

void __mod_lruvec_slab_state(void *p, enum node_stat_item idx, int val)
{
	pg_data_t *pgdat = page_pgdat(virt_to_page(p));
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;

	rcu_read_lock();
	memcg = mem_cgroup_from_obj(p);

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

void mod_memcg_obj_state(void *p, int idx, int val)
{
	struct mem_cgroup *memcg;

	rcu_read_lock();
	memcg = mem_cgroup_from_obj(p);
	if (memcg)
		mod_memcg_state(memcg, idx, val);
	rcu_read_unlock();
}

/**
 * __count_memcg_events - account VM events in a cgroup
 * @memcg: the memory cgroup
 * @idx: the event item
 * @count: the number of events that occured
 */
void __count_memcg_events(struct mem_cgroup *memcg, enum vm_event_item idx,
			  unsigned long count)
{
	unsigned long x;

	if (mem_cgroup_disabled())
		return;

	x = count + __this_cpu_read(memcg->vmstats_percpu->events[idx]);
	if (unlikely(x > MEMCG_CHARGE_BATCH)) {
		struct mem_cgroup *mi;

		/*
		 * Batch local counters to keep them in sync with
		 * the hierarchical ones.
		 */
		__this_cpu_add(memcg->vmstats_local->events[idx], x);
		for (mi = memcg; mi; mi = parent_mem_cgroup(mi))
			atomic_long_add(x, &mi->vmevents[idx]);
		x = 0;
	}
	__this_cpu_write(memcg->vmstats_percpu->events[idx], x);
}

static unsigned long memcg_events(struct mem_cgroup *memcg, int event)
{
	return atomic_long_read(&memcg->vmevents[event]);
}

static unsigned long memcg_events_local(struct mem_cgroup *memcg, int event)
{
	long x = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		x += per_cpu(memcg->vmstats_local->events[event], cpu);
	return x;
}

static void mem_cgroup_charge_statistics(struct mem_cgroup *memcg,
					 struct page *page,
					 int nr_pages)
{
	/* pagein of a big page is an event. So, ignore page size */
	if (nr_pages > 0)
		__count_memcg_events(memcg, PGPGIN, 1);
	else {
		__count_memcg_events(memcg, PGPGOUT, 1);
		nr_pages = -nr_pages; /* for event */
	}

	__this_cpu_add(memcg->vmstats_percpu->nr_page_events, nr_pages);
}

static bool mem_cgroup_event_ratelimit(struct mem_cgroup *memcg,
				       enum mem_cgroup_events_target target)
{
	unsigned long val, next;

	val = __this_cpu_read(memcg->vmstats_percpu->nr_page_events);
	next = __this_cpu_read(memcg->vmstats_percpu->targets[target]);
	/* from time_after() in jiffies.h */
	if ((long)(next - val) < 0) {
		switch (target) {
		case MEM_CGROUP_TARGET_THRESH:
			next = val + THRESHOLDS_EVENTS_TARGET;
			break;
		case MEM_CGROUP_TARGET_SOFTLIMIT:
			next = val + SOFTLIMIT_EVENTS_TARGET;
			break;
		default:
			break;
		}
		__this_cpu_write(memcg->vmstats_percpu->targets[target], next);
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

		do_softlimit = mem_cgroup_event_ratelimit(memcg,
						MEM_CGROUP_TARGET_SOFTLIMIT);
		mem_cgroup_threshold(memcg);
		if (unlikely(do_softlimit))
			mem_cgroup_update_tree(memcg, page);
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

/**
 * get_mem_cgroup_from_mm: Obtain a reference on given mm_struct's memcg.
 * @mm: mm from which memcg should be extracted. It can be NULL.
 *
 * Obtain a reference on mm->memcg and returns it if successful. Otherwise
 * root_mem_cgroup is returned. However if mem_cgroup is disabled, NULL is
 * returned.
 */
struct mem_cgroup *get_mem_cgroup_from_mm(struct mm_struct *mm)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return NULL;

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
	} while (!css_tryget(&memcg->css));
	rcu_read_unlock();
	return memcg;
}
EXPORT_SYMBOL(get_mem_cgroup_from_mm);

/**
 * get_mem_cgroup_from_page: Obtain a reference on given page's memcg.
 * @page: page from which memcg should be extracted.
 *
 * Obtain a reference on page->memcg and returns it if successful. Otherwise
 * root_mem_cgroup is returned.
 */
struct mem_cgroup *get_mem_cgroup_from_page(struct page *page)
{
	struct mem_cgroup *memcg = page->mem_cgroup;

	if (mem_cgroup_disabled())
		return NULL;

	rcu_read_lock();
	/* Page should not get uncharged and freed memcg under us. */
	if (!memcg || WARN_ON_ONCE(!css_tryget(&memcg->css)))
		memcg = root_mem_cgroup;
	rcu_read_unlock();
	return memcg;
}
EXPORT_SYMBOL(get_mem_cgroup_from_page);

static __always_inline struct mem_cgroup *active_memcg(void)
{
	if (in_interrupt())
		return this_cpu_read(int_active_memcg);
	else
		return current->active_memcg;
}

static __always_inline struct mem_cgroup *get_active_memcg(void)
{
	struct mem_cgroup *memcg;

	rcu_read_lock();
	memcg = active_memcg();
	/* remote memcg must hold a ref. */
	if (memcg && WARN_ON_ONCE(!css_tryget(&memcg->css)))
		memcg = root_mem_cgroup;
	rcu_read_unlock();

	return memcg;
}

static __always_inline bool memcg_kmem_bypass(void)
{
	/* Allow remote memcg charging from any context. */
	if (unlikely(active_memcg()))
		return false;

	/* Memcg to charge can't be determined. */
	if (in_interrupt() || !current->mm || (current->flags & PF_KTHREAD))
		return true;

	return false;
}

/**
 * If active memcg is set, do not fallback to current->mm->memcg.
 */
static __always_inline struct mem_cgroup *get_mem_cgroup_from_current(void)
{
	if (memcg_kmem_bypass())
		return NULL;

	if (unlikely(active_memcg()))
		return get_active_memcg();

	return get_mem_cgroup_from_mm(current->mm);
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
		iter = &mz->iter;

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

static void __invalidate_reclaim_iterators(struct mem_cgroup *from,
					struct mem_cgroup *dead_memcg)
{
	struct mem_cgroup_reclaim_iter *iter;
	struct mem_cgroup_per_node *mz;
	int nid;

	for_each_node(nid) {
		mz = mem_cgroup_nodeinfo(from, nid);
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
	 * When cgruop1 non-hierarchy mode is used,
	 * parent_mem_cgroup() does not walk all the way up to the
	 * cgroup root (root_mem_cgroup). So we have to handle
	 * dead_memcg from cgroup root separately.
	 */
	if (last != root_mem_cgroup)
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

		css_task_iter_start(&iter->css, CSS_TASK_ITER_PROCS, &it);
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
 * @pgdat: pgdat of the page
 *
 * This function relies on page->mem_cgroup being stable - see the
 * access rules in commit_charge().
 */
struct lruvec *mem_cgroup_page_lruvec(struct page *page, struct pglist_data *pgdat)
{
	struct mem_cgroup_per_node *mz;
	struct mem_cgroup *memcg;
	struct lruvec *lruvec;

	if (mem_cgroup_disabled()) {
		lruvec = &pgdat->__lruvec;
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

struct memory_stat {
	const char *name;
	unsigned int ratio;
	unsigned int idx;
};

static struct memory_stat memory_stats[] = {
	{ "anon", PAGE_SIZE, NR_ANON_MAPPED },
	{ "file", PAGE_SIZE, NR_FILE_PAGES },
	{ "kernel_stack", 1024, NR_KERNEL_STACK_KB },
	{ "percpu", 1, MEMCG_PERCPU_B },
	{ "sock", PAGE_SIZE, MEMCG_SOCK },
	{ "shmem", PAGE_SIZE, NR_SHMEM },
	{ "file_mapped", PAGE_SIZE, NR_FILE_MAPPED },
	{ "file_dirty", PAGE_SIZE, NR_FILE_DIRTY },
	{ "file_writeback", PAGE_SIZE, NR_WRITEBACK },
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	/*
	 * The ratio will be initialized in memory_stats_init(). Because
	 * on some architectures, the macro of HPAGE_PMD_SIZE is not
	 * constant(e.g. powerpc).
	 */
	{ "anon_thp", 0, NR_ANON_THPS },
#endif
	{ "inactive_anon", PAGE_SIZE, NR_INACTIVE_ANON },
	{ "active_anon", PAGE_SIZE, NR_ACTIVE_ANON },
	{ "inactive_file", PAGE_SIZE, NR_INACTIVE_FILE },
	{ "active_file", PAGE_SIZE, NR_ACTIVE_FILE },
	{ "unevictable", PAGE_SIZE, NR_UNEVICTABLE },

	/*
	 * Note: The slab_reclaimable and slab_unreclaimable must be
	 * together and slab_reclaimable must be in front.
	 */
	{ "slab_reclaimable", 1, NR_SLAB_RECLAIMABLE_B },
	{ "slab_unreclaimable", 1, NR_SLAB_UNRECLAIMABLE_B },

	/* The memory events */
	{ "workingset_refault_anon", 1, WORKINGSET_REFAULT_ANON },
	{ "workingset_refault_file", 1, WORKINGSET_REFAULT_FILE },
	{ "workingset_activate_anon", 1, WORKINGSET_ACTIVATE_ANON },
	{ "workingset_activate_file", 1, WORKINGSET_ACTIVATE_FILE },
	{ "workingset_restore_anon", 1, WORKINGSET_RESTORE_ANON },
	{ "workingset_restore_file", 1, WORKINGSET_RESTORE_FILE },
	{ "workingset_nodereclaim", 1, WORKINGSET_NODERECLAIM },
};

static int __init memory_stats_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(memory_stats); i++) {
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		if (memory_stats[i].idx == NR_ANON_THPS)
			memory_stats[i].ratio = HPAGE_PMD_SIZE;
#endif
		VM_BUG_ON(!memory_stats[i].ratio);
		VM_BUG_ON(memory_stats[i].idx >= MEMCG_NR_STAT);
	}

	return 0;
}
pure_initcall(memory_stats_init);

static char *memory_stat_format(struct mem_cgroup *memcg)
{
	struct seq_buf s;
	int i;

	seq_buf_init(&s, kmalloc(PAGE_SIZE, GFP_KERNEL), PAGE_SIZE);
	if (!s.buffer)
		return NULL;

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

	for (i = 0; i < ARRAY_SIZE(memory_stats); i++) {
		u64 size;

		size = memcg_page_state(memcg, memory_stats[i].idx);
		size *= memory_stats[i].ratio;
		seq_buf_printf(&s, "%s %llu\n", memory_stats[i].name, size);

		if (unlikely(memory_stats[i].idx == NR_SLAB_UNRECLAIMABLE_B)) {
			size = memcg_page_state(memcg, NR_SLAB_RECLAIMABLE_B) +
			       memcg_page_state(memcg, NR_SLAB_UNRECLAIMABLE_B);
			seq_buf_printf(&s, "slab %llu\n", size);
		}
	}

	/* Accumulated memory events */

	seq_buf_printf(&s, "%s %lu\n", vm_event_name(PGFAULT),
		       memcg_events(memcg, PGFAULT));
	seq_buf_printf(&s, "%s %lu\n", vm_event_name(PGMAJFAULT),
		       memcg_events(memcg, PGMAJFAULT));
	seq_buf_printf(&s, "%s %lu\n",  vm_event_name(PGREFILL),
		       memcg_events(memcg, PGREFILL));
	seq_buf_printf(&s, "pgscan %lu\n",
		       memcg_events(memcg, PGSCAN_KSWAPD) +
		       memcg_events(memcg, PGSCAN_DIRECT));
	seq_buf_printf(&s, "pgsteal %lu\n",
		       memcg_events(memcg, PGSTEAL_KSWAPD) +
		       memcg_events(memcg, PGSTEAL_DIRECT));
	seq_buf_printf(&s, "%s %lu\n", vm_event_name(PGACTIVATE),
		       memcg_events(memcg, PGACTIVATE));
	seq_buf_printf(&s, "%s %lu\n", vm_event_name(PGDEACTIVATE),
		       memcg_events(memcg, PGDEACTIVATE));
	seq_buf_printf(&s, "%s %lu\n", vm_event_name(PGLAZYFREE),
		       memcg_events(memcg, PGLAZYFREE));
	seq_buf_printf(&s, "%s %lu\n", vm_event_name(PGLAZYFREED),
		       memcg_events(memcg, PGLAZYFREED));

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	seq_buf_printf(&s, "%s %lu\n", vm_event_name(THP_FAULT_ALLOC),
		       memcg_events(memcg, THP_FAULT_ALLOC));
	seq_buf_printf(&s, "%s %lu\n", vm_event_name(THP_COLLAPSE_ALLOC),
		       memcg_events(memcg, THP_COLLAPSE_ALLOC));
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

	/* The above should easily fit into one page */
	WARN_ON_ONCE(seq_buf_has_overflowed(&s));

	return s.buffer;
}

#define K(x) ((x) << (PAGE_SHIFT-10))
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
	char *buf;

	pr_info("memory: usage %llukB, limit %llukB, failcnt %lu\n",
		K((u64)page_counter_read(&memcg->memory)),
		K((u64)READ_ONCE(memcg->memory.max)), memcg->memory.failcnt);
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
		pr_info("swap: usage %llukB, limit %llukB, failcnt %lu\n",
			K((u64)page_counter_read(&memcg->swap)),
			K((u64)READ_ONCE(memcg->swap.max)), memcg->swap.failcnt);
	else {
		pr_info("memory+swap: usage %llukB, limit %llukB, failcnt %lu\n",
			K((u64)page_counter_read(&memcg->memsw)),
			K((u64)memcg->memsw.max), memcg->memsw.failcnt);
		pr_info("kmem: usage %llukB, limit %llukB, failcnt %lu\n",
			K((u64)page_counter_read(&memcg->kmem)),
			K((u64)memcg->kmem.max), memcg->kmem.failcnt);
	}

	pr_info("Memory cgroup stats for ");
	pr_cont_cgroup_path(memcg->css.cgroup);
	pr_cont(":");
	buf = memory_stat_format(memcg);
	if (!buf)
		return;
	pr_info("%s", buf);
	kfree(buf);
}

/*
 * Return the memory (and swap, if configured) limit for a memcg.
 */
unsigned long mem_cgroup_get_max(struct mem_cgroup *memcg)
{
	unsigned long max = READ_ONCE(memcg->memory.max);

	if (cgroup_subsys_on_dfl(memory_cgrp_subsys)) {
		if (mem_cgroup_swappiness(memcg))
			max += min(READ_ONCE(memcg->swap.max),
				   (unsigned long)total_swap_pages);
	} else { /* v1 */
		if (mem_cgroup_swappiness(memcg)) {
			/* Calculate swap excess capacity from memsw limit */
			unsigned long swap = READ_ONCE(memcg->memsw.max) - max;

			max += min(swap, (unsigned long)total_swap_pages);
		}
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
	ret = should_force_charge() || out_of_memory(&oc);

unlock:
	mutex_unlock(&oom_lock);
	return ret;
}

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
	mutex_release(&memcg_oom_lock_dep_map, _RET_IP_);
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
	 * Be careful about under_oom underflows becase a child memcg
	 * could have been added after mem_cgroup_mark_under_oom.
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

enum oom_status {
	OOM_SUCCESS,
	OOM_FAILED,
	OOM_ASYNC,
	OOM_SKIPPED
};

static enum oom_status mem_cgroup_oom(struct mem_cgroup *memcg, gfp_t mask, int order)
{
	enum oom_status ret;
	bool locked;

	if (order > PAGE_ALLOC_COSTLY_ORDER)
		return OOM_SKIPPED;

	memcg_memory_event(memcg, MEMCG_OOM);

	/*
	 * We are in the middle of the charge context here, so we
	 * don't want to block when potentially sitting on a callstack
	 * that holds all kinds of filesystem and mm locks.
	 *
	 * cgroup1 allows disabling the OOM killer and waiting for outside
	 * handling until the charge can succeed; remember the context and put
	 * the task to sleep at the end of the page fault when all locks are
	 * released.
	 *
	 * On the other hand, in-kernel OOM killer allows for an async victim
	 * memory reclaim (oom_reaper) and that means that we are not solely
	 * relying on the oom victim to make a forward progress and we can
	 * invoke the oom killer here.
	 *
	 * Please note that mem_cgroup_out_of_memory might fail to find a
	 * victim and then we have to bail out from the charge path.
	 */
	if (memcg->oom_kill_disable) {
		if (!current->in_user_fault)
			return OOM_SKIPPED;
		css_get(&memcg->css);
		current->memcg_in_oom = memcg;
		current->memcg_oom_gfp_mask = mask;
		current->memcg_oom_order = order;

		return OOM_ASYNC;
	}

	mem_cgroup_mark_under_oom(memcg);

	locked = mem_cgroup_oom_trylock(memcg);

	if (locked)
		mem_cgroup_oom_notify(memcg);

	mem_cgroup_unmark_under_oom(memcg);
	if (mem_cgroup_out_of_memory(memcg, mask, order))
		ret = OOM_SUCCESS;
	else
		ret = OOM_FAILED;

	if (locked)
		mem_cgroup_oom_unlock(memcg);

	return ret;
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
	if (memcg == root_mem_cgroup)
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
		if (memcg->oom_group)
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
	struct page *head = compound_head(page); /* rmap on tail pages */
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
	memcg = head->mem_cgroup;
	if (unlikely(!memcg))
		return NULL;

	if (atomic_read(&memcg->moving_account) <= 0)
		return memcg;

	spin_lock_irqsave(&memcg->move_lock, flags);
	if (memcg != head->mem_cgroup) {
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
	struct page *head = compound_head(page);

	__unlock_page_memcg(head->mem_cgroup);
}
EXPORT_SYMBOL(unlock_page_memcg);

struct memcg_stock_pcp {
	struct mem_cgroup *cached; /* this never be root cgroup */
	unsigned int nr_pages;

#ifdef CONFIG_MEMCG_KMEM
	struct obj_cgroup *cached_objcg;
	unsigned int nr_bytes;
#endif

	struct work_struct work;
	unsigned long flags;
#define FLUSHING_CACHED_CHARGE	0
};
static DEFINE_PER_CPU(struct memcg_stock_pcp, memcg_stock);
static DEFINE_MUTEX(percpu_charge_mutex);

#ifdef CONFIG_MEMCG_KMEM
static void drain_obj_stock(struct memcg_stock_pcp *stock);
static bool obj_stock_flush_required(struct memcg_stock_pcp *stock,
				     struct mem_cgroup *root_memcg);

#else
static inline void drain_obj_stock(struct memcg_stock_pcp *stock)
{
}
static bool obj_stock_flush_required(struct memcg_stock_pcp *stock,
				     struct mem_cgroup *root_memcg)
{
	return false;
}
#endif

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

	if (nr_pages > MEMCG_CHARGE_BATCH)
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

	if (!old)
		return;

	if (stock->nr_pages) {
		page_counter_uncharge(&old->memory, stock->nr_pages);
		if (do_memsw_account())
			page_counter_uncharge(&old->memsw, stock->nr_pages);
		stock->nr_pages = 0;
	}

	css_put(&old->css);
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
	drain_obj_stock(stock);
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
		css_get(&memcg->css);
		stock->cached = memcg;
	}
	stock->nr_pages += nr_pages;

	if (stock->nr_pages > MEMCG_CHARGE_BATCH)
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
		bool flush = false;

		rcu_read_lock();
		memcg = stock->cached;
		if (memcg && stock->nr_pages &&
		    mem_cgroup_is_descendant(memcg, root_memcg))
			flush = true;
		if (obj_stock_flush_required(stock, root_memcg))
			flush = true;
		rcu_read_unlock();

		if (flush &&
		    !test_and_set_bit(FLUSHING_CACHED_CHARGE, &stock->flags)) {
			if (cpu == curcpu)
				drain_local_stock(&stock->work);
			else
				schedule_work_on(cpu, &stock->work);
		}
	}
	put_cpu();
	mutex_unlock(&percpu_charge_mutex);
}

static int memcg_hotplug_cpu_dead(unsigned int cpu)
{
	struct memcg_stock_pcp *stock;
	struct mem_cgroup *memcg, *mi;

	stock = &per_cpu(memcg_stock, cpu);
	drain_stock(stock);

	for_each_mem_cgroup(memcg) {
		int i;

		for (i = 0; i < MEMCG_NR_STAT; i++) {
			int nid;
			long x;

			x = this_cpu_xchg(memcg->vmstats_percpu->stat[i], 0);
			if (x)
				for (mi = memcg; mi; mi = parent_mem_cgroup(mi))
					atomic_long_add(x, &memcg->vmstats[i]);

			if (i >= NR_VM_NODE_STAT_ITEMS)
				continue;

			for_each_node(nid) {
				struct mem_cgroup_per_node *pn;

				pn = mem_cgroup_nodeinfo(memcg, nid);
				x = this_cpu_xchg(pn->lruvec_stat_cpu->count[i], 0);
				if (x)
					do {
						atomic_long_add(x, &pn->lruvec_stat[i]);
					} while ((pn = parent_nodeinfo(pn, nid)));
			}
		}

		for (i = 0; i < NR_VM_EVENT_ITEMS; i++) {
			long x;

			x = this_cpu_xchg(memcg->vmstats_percpu->events[i], 0);
			if (x)
				for (mi = memcg; mi; mi = parent_mem_cgroup(mi))
					atomic_long_add(x, &memcg->vmevents[i]);
		}
	}

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
							     gfp_mask, true);
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
 * Scheduled by try_charge() to be executed from the userland return path
 * and reclaims memory over the high limit.
 */
void mem_cgroup_handle_over_high(void)
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
				    GFP_KERNEL);

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

static int try_charge(struct mem_cgroup *memcg, gfp_t gfp_mask,
		      unsigned int nr_pages)
{
	unsigned int batch = max(MEMCG_CHARGE_BATCH, nr_pages);
	int nr_retries = MAX_RECLAIM_RETRIES;
	struct mem_cgroup *mem_over_limit;
	struct page_counter *counter;
	enum oom_status oom_status;
	unsigned long nr_reclaimed;
	bool may_swap = true;
	bool drained = false;
	unsigned long pflags;

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
	 * Memcg doesn't have a dedicated reserve for atomic
	 * allocations. But like the global atomic pool, we need to
	 * put the burden of reclaim on regular allocation requests
	 * and let these go through as privileged allocations.
	 */
	if (gfp_mask & __GFP_ATOMIC)
		goto force;

	/*
	 * Unlike in global OOM situations, memcg is not in a physical
	 * memory shortage.  Allow dying and OOM-killed tasks to
	 * bypass the last charges so that they can exit quickly and
	 * free their memory.
	 */
	if (unlikely(should_force_charge()))
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

	memcg_memory_event(mem_over_limit, MEMCG_MAX);

	psi_memstall_enter(&pflags);
	nr_reclaimed = try_to_free_mem_cgroup_pages(mem_over_limit, nr_pages,
						    gfp_mask, may_swap);
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
	/*
	 * At task move, charge accounts can be doubly counted. So, it's
	 * better to wait until the end of task_move if something is going on.
	 */
	if (mem_cgroup_wait_acct_move(mem_over_limit))
		goto retry;

	if (nr_retries--)
		goto retry;

	if (gfp_mask & __GFP_RETRY_MAYFAIL)
		goto nomem;

	if (gfp_mask & __GFP_NOFAIL)
		goto force;

	if (fatal_signal_pending(current))
		goto force;

	/*
	 * keep retrying as long as the memcg oom killer is able to make
	 * a forward progress or bypass the charge if the oom killer
	 * couldn't make any progress.
	 */
	oom_status = mem_cgroup_oom(mem_over_limit, gfp_mask,
		       get_order(nr_pages * PAGE_SIZE));
	switch (oom_status) {
	case OOM_SUCCESS:
		nr_retries = MAX_RECLAIM_RETRIES;
		goto retry;
	case OOM_FAILED:
		goto force;
	default:
		goto nomem;
	}
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
		if (in_interrupt()) {
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

	return 0;
}

#if defined(CONFIG_MEMCG_KMEM) || defined(CONFIG_MMU)
static void cancel_charge(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	if (mem_cgroup_is_root(memcg))
		return;

	page_counter_uncharge(&memcg->memory, nr_pages);
	if (do_memsw_account())
		page_counter_uncharge(&memcg->memsw, nr_pages);
}
#endif

static void commit_charge(struct page *page, struct mem_cgroup *memcg)
{
	VM_BUG_ON_PAGE(page->mem_cgroup, page);
	/*
	 * Any of the following ensures page->mem_cgroup stability:
	 *
	 * - the page lock
	 * - LRU isolation
	 * - lock_page_memcg()
	 * - exclusive reference
	 */
	page->mem_cgroup = memcg;
}

#ifdef CONFIG_MEMCG_KMEM
/*
 * The allocated objcg pointers array is not accounted directly.
 * Moreover, it should not come from DMA buffer and is not readily
 * reclaimable. So those GFP bits should be masked off.
 */
#define OBJCGS_CLEAR_MASK	(__GFP_DMA | __GFP_RECLAIMABLE | __GFP_ACCOUNT)

int memcg_alloc_page_obj_cgroups(struct page *page, struct kmem_cache *s,
				 gfp_t gfp)
{
	unsigned int objects = objs_per_slab_page(s, page);
	void *vec;

	gfp &= ~OBJCGS_CLEAR_MASK;
	vec = kcalloc_node(objects, sizeof(struct obj_cgroup *), gfp,
			   page_to_nid(page));
	if (!vec)
		return -ENOMEM;

	if (cmpxchg(&page->obj_cgroups, NULL,
		    (struct obj_cgroup **) ((unsigned long)vec | 0x1UL)))
		kfree(vec);
	else
		kmemleak_not_leak(vec);

	return 0;
}

/*
 * Returns a pointer to the memory cgroup to which the kernel object is charged.
 *
 * The caller must ensure the memcg lifetime, e.g. by taking rcu_read_lock(),
 * cgroup_mutex, etc.
 */
struct mem_cgroup *mem_cgroup_from_obj(void *p)
{
	struct page *page;

	if (mem_cgroup_disabled())
		return NULL;

	page = virt_to_head_page(p);

	/*
	 * If page->mem_cgroup is set, it's either a simple mem_cgroup pointer
	 * or a pointer to obj_cgroup vector. In the latter case the lowest
	 * bit of the pointer is set.
	 * The page->mem_cgroup pointer can be asynchronously changed
	 * from NULL to (obj_cgroup_vec | 0x1UL), but can't be changed
	 * from a valid memcg pointer to objcg vector or back.
	 */
	if (!page->mem_cgroup)
		return NULL;

	/*
	 * Slab objects are accounted individually, not per-page.
	 * Memcg membership data for each individual object is saved in
	 * the page->obj_cgroups.
	 */
	if (page_has_obj_cgroups(page)) {
		struct obj_cgroup *objcg;
		unsigned int off;

		off = obj_to_index(page->slab_cache, page, p);
		objcg = page_obj_cgroups(page)[off];
		if (objcg)
			return obj_cgroup_memcg(objcg);

		return NULL;
	}

	/* All other pages use page->mem_cgroup */
	return page->mem_cgroup;
}

__always_inline struct obj_cgroup *get_obj_cgroup_from_current(void)
{
	struct obj_cgroup *objcg = NULL;
	struct mem_cgroup *memcg;

	if (memcg_kmem_bypass())
		return NULL;

	rcu_read_lock();
	if (unlikely(active_memcg()))
		memcg = active_memcg();
	else
		memcg = mem_cgroup_from_task(current);

	for (; memcg != root_mem_cgroup; memcg = parent_mem_cgroup(memcg)) {
		objcg = rcu_dereference(memcg->objcg);
		if (objcg && obj_cgroup_tryget(objcg))
			break;
		objcg = NULL;
	}
	rcu_read_unlock();

	return objcg;
}

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

/**
 * __memcg_kmem_charge: charge a number of kernel pages to a memcg
 * @memcg: memory cgroup to charge
 * @gfp: reclaim mode
 * @nr_pages: number of pages to charge
 *
 * Returns 0 on success, an error code on failure.
 */
int __memcg_kmem_charge(struct mem_cgroup *memcg, gfp_t gfp,
			unsigned int nr_pages)
{
	struct page_counter *counter;
	int ret;

	ret = try_charge(memcg, gfp, nr_pages);
	if (ret)
		return ret;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) &&
	    !page_counter_try_charge(&memcg->kmem, nr_pages, &counter)) {

		/*
		 * Enforce __GFP_NOFAIL allocation because callers are not
		 * prepared to see failures and likely do not have any failure
		 * handling code.
		 */
		if (gfp & __GFP_NOFAIL) {
			page_counter_charge(&memcg->kmem, nr_pages);
			return 0;
		}
		cancel_charge(memcg, nr_pages);
		return -ENOMEM;
	}
	return 0;
}

/**
 * __memcg_kmem_uncharge: uncharge a number of kernel pages from a memcg
 * @memcg: memcg to uncharge
 * @nr_pages: number of pages to uncharge
 */
void __memcg_kmem_uncharge(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
		page_counter_uncharge(&memcg->kmem, nr_pages);

	refill_stock(memcg, nr_pages);
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
	struct mem_cgroup *memcg;
	int ret = 0;

	memcg = get_mem_cgroup_from_current();
	if (memcg && !mem_cgroup_is_root(memcg)) {
		ret = __memcg_kmem_charge(memcg, gfp, 1 << order);
		if (!ret) {
			page->mem_cgroup = memcg;
			__SetPageKmemcg(page);
			return 0;
		}
		css_put(&memcg->css);
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
	struct mem_cgroup *memcg = page->mem_cgroup;
	unsigned int nr_pages = 1 << order;

	if (!memcg)
		return;

	VM_BUG_ON_PAGE(mem_cgroup_is_root(memcg), page);
	__memcg_kmem_uncharge(memcg, nr_pages);
	page->mem_cgroup = NULL;
	css_put(&memcg->css);

	/* slab pages do not have PageKmemcg flag set */
	if (PageKmemcg(page))
		__ClearPageKmemcg(page);
}

static bool consume_obj_stock(struct obj_cgroup *objcg, unsigned int nr_bytes)
{
	struct memcg_stock_pcp *stock;
	unsigned long flags;
	bool ret = false;

	local_irq_save(flags);

	stock = this_cpu_ptr(&memcg_stock);
	if (objcg == stock->cached_objcg && stock->nr_bytes >= nr_bytes) {
		stock->nr_bytes -= nr_bytes;
		ret = true;
	}

	local_irq_restore(flags);

	return ret;
}

static void drain_obj_stock(struct memcg_stock_pcp *stock)
{
	struct obj_cgroup *old = stock->cached_objcg;

	if (!old)
		return;

	if (stock->nr_bytes) {
		unsigned int nr_pages = stock->nr_bytes >> PAGE_SHIFT;
		unsigned int nr_bytes = stock->nr_bytes & (PAGE_SIZE - 1);

		if (nr_pages) {
			struct mem_cgroup *memcg;

			rcu_read_lock();
retry:
			memcg = obj_cgroup_memcg(old);
			if (unlikely(!css_tryget(&memcg->css)))
				goto retry;
			rcu_read_unlock();

			__memcg_kmem_uncharge(memcg, nr_pages);
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

	obj_cgroup_put(old);
	stock->cached_objcg = NULL;
}

static bool obj_stock_flush_required(struct memcg_stock_pcp *stock,
				     struct mem_cgroup *root_memcg)
{
	struct mem_cgroup *memcg;

	if (stock->cached_objcg) {
		memcg = obj_cgroup_memcg(stock->cached_objcg);
		if (memcg && mem_cgroup_is_descendant(memcg, root_memcg))
			return true;
	}

	return false;
}

static void refill_obj_stock(struct obj_cgroup *objcg, unsigned int nr_bytes)
{
	struct memcg_stock_pcp *stock;
	unsigned long flags;

	local_irq_save(flags);

	stock = this_cpu_ptr(&memcg_stock);
	if (stock->cached_objcg != objcg) { /* reset if necessary */
		drain_obj_stock(stock);
		obj_cgroup_get(objcg);
		stock->cached_objcg = objcg;
		stock->nr_bytes = atomic_xchg(&objcg->nr_charged_bytes, 0);
	}
	stock->nr_bytes += nr_bytes;

	if (stock->nr_bytes > PAGE_SIZE)
		drain_obj_stock(stock);

	local_irq_restore(flags);
}

int obj_cgroup_charge(struct obj_cgroup *objcg, gfp_t gfp, size_t size)
{
	struct mem_cgroup *memcg;
	unsigned int nr_pages, nr_bytes;
	int ret;

	if (consume_obj_stock(objcg, size))
		return 0;

	/*
	 * In theory, memcg->nr_charged_bytes can have enough
	 * pre-charged bytes to satisfy the allocation. However,
	 * flushing memcg->nr_charged_bytes requires two atomic
	 * operations, and memcg->nr_charged_bytes can't be big,
	 * so it's better to ignore it and try grab some new pages.
	 * memcg->nr_charged_bytes will be flushed in
	 * refill_obj_stock(), called from this function or
	 * independently later.
	 */
	rcu_read_lock();
retry:
	memcg = obj_cgroup_memcg(objcg);
	if (unlikely(!css_tryget(&memcg->css)))
		goto retry;
	rcu_read_unlock();

	nr_pages = size >> PAGE_SHIFT;
	nr_bytes = size & (PAGE_SIZE - 1);

	if (nr_bytes)
		nr_pages += 1;

	ret = __memcg_kmem_charge(memcg, gfp, nr_pages);
	if (!ret && nr_bytes)
		refill_obj_stock(objcg, PAGE_SIZE - nr_bytes);

	css_put(&memcg->css);
	return ret;
}

void obj_cgroup_uncharge(struct obj_cgroup *objcg, size_t size)
{
	refill_obj_stock(objcg, size);
}

#endif /* CONFIG_MEMCG_KMEM */

/*
 * Because head->mem_cgroup is not set on tails, set it now.
 */
void split_page_memcg(struct page *head, unsigned int nr)
{
	struct mem_cgroup *memcg = head->mem_cgroup;
	int kmemcg = PageKmemcg(head);
	int i;

	if (mem_cgroup_disabled() || !memcg)
		return;

	for (i = 1; i < nr; i++) {
		head[i].mem_cgroup = memcg;
		if (kmemcg)
			__SetPageKmemcg(head + i);
	}
	css_get_many(&memcg->css, nr - 1);
}

#ifdef CONFIG_MEMCG_SWAP
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
		mod_memcg_state(from, MEMCG_SWAP, -1);
		mod_memcg_state(to, MEMCG_SWAP, 1);
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

static DEFINE_MUTEX(memcg_max_mutex);

static int mem_cgroup_resize_max(struct mem_cgroup *memcg,
				 unsigned long max, bool memsw)
{
	bool enlarge = false;
	bool drained = false;
	int ret;
	bool limits_invariant;
	struct page_counter *counter = memsw ? &memcg->memsw : &memcg->memory;

	do {
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		mutex_lock(&memcg_max_mutex);
		/*
		 * Make sure that the new limit (memsw or memory limit) doesn't
		 * break our basic invariant rule memory.max <= memsw.max.
		 */
		limits_invariant = memsw ? max >= READ_ONCE(memcg->memory.max) :
					   max <= memcg->memsw.max;
		if (!limits_invariant) {
			mutex_unlock(&memcg_max_mutex);
			ret = -EINVAL;
			break;
		}
		if (max > counter->max)
			enlarge = true;
		ret = page_counter_set_max(counter, max);
		mutex_unlock(&memcg_max_mutex);

		if (!ret)
			break;

		if (!drained) {
			drain_all_stock(memcg);
			drained = true;
			continue;
		}

		if (!try_to_free_mem_cgroup_pages(memcg, 1,
					GFP_KERNEL, !memsw)) {
			ret = -EBUSY;
			break;
		}
	} while (true);

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
 * hierarchy.  Testing use_hierarchy is the caller's responsibility.
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
	int nr_retries = MAX_RECLAIM_RETRIES;

	/* we call try-to-free pages for make this cgroup empty */
	lru_add_drain_all();

	drain_all_stock(memcg);

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

static unsigned long mem_cgroup_usage(struct mem_cgroup *memcg, bool swap)
{
	unsigned long val;

	if (mem_cgroup_is_root(memcg)) {
		val = memcg_page_state(memcg, NR_FILE_PAGES) +
			memcg_page_state(memcg, NR_ANON_MAPPED);
		if (swap)
			val += memcg_page_state(memcg, MEMCG_SWAP);
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
		return (u64)counter->max * PAGE_SIZE;
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

static void memcg_flush_percpu_vmstats(struct mem_cgroup *memcg)
{
	unsigned long stat[MEMCG_NR_STAT] = {0};
	struct mem_cgroup *mi;
	int node, cpu, i;

	for_each_online_cpu(cpu)
		for (i = 0; i < MEMCG_NR_STAT; i++)
			stat[i] += per_cpu(memcg->vmstats_percpu->stat[i], cpu);

	for (mi = memcg; mi; mi = parent_mem_cgroup(mi))
		for (i = 0; i < MEMCG_NR_STAT; i++)
			atomic_long_add(stat[i], &mi->vmstats[i]);

	for_each_node(node) {
		struct mem_cgroup_per_node *pn = memcg->nodeinfo[node];
		struct mem_cgroup_per_node *pi;

		for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++)
			stat[i] = 0;

		for_each_online_cpu(cpu)
			for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++)
				stat[i] += per_cpu(
					pn->lruvec_stat_cpu->count[i], cpu);

		for (pi = pn; pi; pi = parent_nodeinfo(pi, node))
			for (i = 0; i < NR_VM_NODE_STAT_ITEMS; i++)
				atomic_long_add(stat[i], &pi->lruvec_stat[i]);
	}
}

static void memcg_flush_percpu_vmevents(struct mem_cgroup *memcg)
{
	unsigned long events[NR_VM_EVENT_ITEMS];
	struct mem_cgroup *mi;
	int cpu, i;

	for (i = 0; i < NR_VM_EVENT_ITEMS; i++)
		events[i] = 0;

	for_each_online_cpu(cpu)
		for (i = 0; i < NR_VM_EVENT_ITEMS; i++)
			events[i] += per_cpu(memcg->vmstats_percpu->events[i],
					     cpu);

	for (mi = memcg; mi; mi = parent_mem_cgroup(mi))
		for (i = 0; i < NR_VM_EVENT_ITEMS; i++)
			atomic_long_add(events[i], &mi->vmevents[i]);
}

#ifdef CONFIG_MEMCG_KMEM
static int memcg_online_kmem(struct mem_cgroup *memcg)
{
	struct obj_cgroup *objcg;
	int memcg_id;

	if (cgroup_memory_nokmem)
		return 0;

	BUG_ON(memcg->kmemcg_id >= 0);
	BUG_ON(memcg->kmem_state);

	memcg_id = memcg_alloc_cache_id();
	if (memcg_id < 0)
		return memcg_id;

	objcg = obj_cgroup_alloc();
	if (!objcg) {
		memcg_free_cache_id(memcg_id);
		return -ENOMEM;
	}
	objcg->memcg = memcg;
	rcu_assign_pointer(memcg->objcg, objcg);

	static_branch_enable(&memcg_kmem_enabled_key);

	/*
	 * A memory cgroup is considered kmem-online as soon as it gets
	 * kmemcg_id. Setting the id after enabling static branching will
	 * guarantee no one starts accounting before all call sites are
	 * patched.
	 */
	memcg->kmemcg_id = memcg_id;
	memcg->kmem_state = KMEM_ONLINE;

	return 0;
}

static void memcg_offline_kmem(struct mem_cgroup *memcg)
{
	struct cgroup_subsys_state *css;
	struct mem_cgroup *parent, *child;
	int kmemcg_id;

	if (memcg->kmem_state != KMEM_ONLINE)
		return;

	memcg->kmem_state = KMEM_ALLOCATED;

	parent = parent_mem_cgroup(memcg);
	if (!parent)
		parent = root_mem_cgroup;

	memcg_reparent_objcgs(memcg, parent);

	kmemcg_id = memcg->kmemcg_id;
	BUG_ON(kmemcg_id < 0);

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

	memcg_drain_all_list_lrus(kmemcg_id, parent);

	memcg_free_cache_id(kmemcg_id);
}

static void memcg_free_kmem(struct mem_cgroup *memcg)
{
	/* css_alloc() failed, offlining didn't happen */
	if (unlikely(memcg->kmem_state == KMEM_ONLINE))
		memcg_offline_kmem(memcg);
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
#endif /* CONFIG_MEMCG_KMEM */

static int memcg_update_kmem_max(struct mem_cgroup *memcg,
				 unsigned long max)
{
	int ret;

	mutex_lock(&memcg_max_mutex);
	ret = page_counter_set_max(&memcg->kmem, max);
	mutex_unlock(&memcg_max_mutex);
	return ret;
}

static int memcg_update_tcp_max(struct mem_cgroup *memcg, unsigned long max)
{
	int ret;

	mutex_lock(&memcg_max_mutex);

	ret = page_counter_set_max(&memcg->tcpmem, max);
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
	mutex_unlock(&memcg_max_mutex);
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
			ret = mem_cgroup_resize_max(memcg, nr_pages, false);
			break;
		case _MEMSWAP:
			ret = mem_cgroup_resize_max(memcg, nr_pages, true);
			break;
		case _KMEM:
			pr_warn_once("kmem.limit_in_bytes is deprecated and will be removed. "
				     "Please report your usecase to linux-mm@kvack.org if you "
				     "depend on this functionality.\n");
			ret = memcg_update_kmem_max(memcg, nr_pages);
			break;
		case _TCP:
			ret = memcg_update_tcp_max(memcg, nr_pages);
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

#define LRU_ALL_FILE (BIT(LRU_INACTIVE_FILE) | BIT(LRU_ACTIVE_FILE))
#define LRU_ALL_ANON (BIT(LRU_INACTIVE_ANON) | BIT(LRU_ACTIVE_ANON))
#define LRU_ALL	     ((1 << NR_LRU_LISTS) - 1)

static unsigned long mem_cgroup_node_nr_lru_pages(struct mem_cgroup *memcg,
				int nid, unsigned int lru_mask, bool tree)
{
	struct lruvec *lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(nid));
	unsigned long nr = 0;
	enum lru_list lru;

	VM_BUG_ON((unsigned)nid >= nr_node_ids);

	for_each_lru(lru) {
		if (!(BIT(lru) & lru_mask))
			continue;
		if (tree)
			nr += lruvec_page_state(lruvec, NR_LRU_BASE + lru);
		else
			nr += lruvec_page_state_local(lruvec, NR_LRU_BASE + lru);
	}
	return nr;
}

static unsigned long mem_cgroup_nr_lru_pages(struct mem_cgroup *memcg,
					     unsigned int lru_mask,
					     bool tree)
{
	unsigned long nr = 0;
	enum lru_list lru;

	for_each_lru(lru) {
		if (!(BIT(lru) & lru_mask))
			continue;
		if (tree)
			nr += memcg_page_state(memcg, NR_LRU_BASE + lru);
		else
			nr += memcg_page_state_local(memcg, NR_LRU_BASE + lru);
	}
	return nr;
}

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
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	for (stat = stats; stat < stats + ARRAY_SIZE(stats); stat++) {
		seq_printf(m, "%s=%lu", stat->name,
			   mem_cgroup_nr_lru_pages(memcg, stat->lru_mask,
						   false));
		for_each_node_state(nid, N_MEMORY)
			seq_printf(m, " N%d=%lu", nid,
				   mem_cgroup_node_nr_lru_pages(memcg, nid,
							stat->lru_mask, false));
		seq_putc(m, '\n');
	}

	for (stat = stats; stat < stats + ARRAY_SIZE(stats); stat++) {

		seq_printf(m, "hierarchical_%s=%lu", stat->name,
			   mem_cgroup_nr_lru_pages(memcg, stat->lru_mask,
						   true));
		for_each_node_state(nid, N_MEMORY)
			seq_printf(m, " N%d=%lu", nid,
				   mem_cgroup_node_nr_lru_pages(memcg, nid,
							stat->lru_mask, true));
		seq_putc(m, '\n');
	}

	return 0;
}
#endif /* CONFIG_NUMA */

static const unsigned int memcg1_stats[] = {
	NR_FILE_PAGES,
	NR_ANON_MAPPED,
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	NR_ANON_THPS,
#endif
	NR_SHMEM,
	NR_FILE_MAPPED,
	NR_FILE_DIRTY,
	NR_WRITEBACK,
	MEMCG_SWAP,
};

static const char *const memcg1_stat_names[] = {
	"cache",
	"rss",
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	"rss_huge",
#endif
	"shmem",
	"mapped_file",
	"dirty",
	"writeback",
	"swap",
};

/* Universal VM events cgroup1 shows, original sort order */
static const unsigned int memcg1_events[] = {
	PGPGIN,
	PGPGOUT,
	PGFAULT,
	PGMAJFAULT,
};

static int memcg_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);
	unsigned long memory, memsw;
	struct mem_cgroup *mi;
	unsigned int i;

	BUILD_BUG_ON(ARRAY_SIZE(memcg1_stat_names) != ARRAY_SIZE(memcg1_stats));

	for (i = 0; i < ARRAY_SIZE(memcg1_stats); i++) {
		unsigned long nr;

		if (memcg1_stats[i] == MEMCG_SWAP && !do_memsw_account())
			continue;
		nr = memcg_page_state_local(memcg, memcg1_stats[i]);
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		if (memcg1_stats[i] == NR_ANON_THPS)
			nr *= HPAGE_PMD_NR;
#endif
		seq_printf(m, "%s %lu\n", memcg1_stat_names[i], nr * PAGE_SIZE);
	}

	for (i = 0; i < ARRAY_SIZE(memcg1_events); i++)
		seq_printf(m, "%s %lu\n", vm_event_name(memcg1_events[i]),
			   memcg_events_local(memcg, memcg1_events[i]));

	for (i = 0; i < NR_LRU_LISTS; i++)
		seq_printf(m, "%s %lu\n", lru_list_name(i),
			   memcg_page_state_local(memcg, NR_LRU_BASE + i) *
			   PAGE_SIZE);

	/* Hierarchical information */
	memory = memsw = PAGE_COUNTER_MAX;
	for (mi = memcg; mi; mi = parent_mem_cgroup(mi)) {
		memory = min(memory, READ_ONCE(mi->memory.max));
		memsw = min(memsw, READ_ONCE(mi->memsw.max));
	}
	seq_printf(m, "hierarchical_memory_limit %llu\n",
		   (u64)memory * PAGE_SIZE);
	if (do_memsw_account())
		seq_printf(m, "hierarchical_memsw_limit %llu\n",
			   (u64)memsw * PAGE_SIZE);

	for (i = 0; i < ARRAY_SIZE(memcg1_stats); i++) {
		unsigned long nr;

		if (memcg1_stats[i] == MEMCG_SWAP && !do_memsw_account())
			continue;
		nr = memcg_page_state(memcg, memcg1_stats[i]);
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		if (memcg1_stats[i] == NR_ANON_THPS)
			nr *= HPAGE_PMD_NR;
#endif
		seq_printf(m, "total_%s %llu\n", memcg1_stat_names[i],
						(u64)nr * PAGE_SIZE);
	}

	for (i = 0; i < ARRAY_SIZE(memcg1_events); i++)
		seq_printf(m, "total_%s %llu\n",
			   vm_event_name(memcg1_events[i]),
			   (u64)memcg_events(memcg, memcg1_events[i]));

	for (i = 0; i < NR_LRU_LISTS; i++)
		seq_printf(m, "total_%s %llu\n", lru_list_name(i),
			   (u64)memcg_page_state(memcg, NR_LRU_BASE + i) *
			   PAGE_SIZE);

#ifdef CONFIG_DEBUG_VM
	{
		pg_data_t *pgdat;
		struct mem_cgroup_per_node *mz;
		unsigned long anon_cost = 0;
		unsigned long file_cost = 0;

		for_each_online_pgdat(pgdat) {
			mz = mem_cgroup_nodeinfo(memcg, pgdat->node_id);

			anon_cost += mz->lruvec.anon_cost;
			file_cost += mz->lruvec.file_cost;
		}
		seq_printf(m, "anon_cost %lu\n", anon_cost);
		seq_printf(m, "file_cost %lu\n", file_cost);
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
	new = kmalloc(struct_size(new, entries, size), GFP_KERNEL);
	if (!new) {
		ret = -ENOMEM;
		goto unlock;
	}
	new->size = size;

	/* Copy thresholds (if any) to new array */
	if (thresholds->primary)
		memcpy(new->entries, thresholds->primary->entries,
		       flex_array_size(new, entries, size - 1));

	/* Add new threshold */
	new->entries[size - 1].eventfd = eventfd;
	new->entries[size - 1].threshold = threshold;

	/* Sort thresholds. Registering of new threshold isn't time-critical */
	sort(new->entries, size, sizeof(*new->entries),
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
	int i, j, size, entries;

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
	size = entries = 0;
	for (i = 0; i < thresholds->primary->size; i++) {
		if (thresholds->primary->entries[i].eventfd != eventfd)
			size++;
		else
			entries++;
	}

	new = thresholds->spare;

	/* If no items related to eventfd have been cleared, nothing to do */
	if (!entries)
		goto unlock;

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
	struct mem_cgroup *memcg = mem_cgroup_from_seq(sf);

	seq_printf(sf, "oom_kill_disable %d\n", memcg->oom_kill_disable);
	seq_printf(sf, "under_oom %d\n", (bool)memcg->under_oom);
	seq_printf(sf, "oom_kill %lu\n",
		   atomic_long_read(&memcg->memory_events[MEMCG_OOM_KILL]));
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

/*
 * idx can be of type enum memcg_stat_item or node_stat_item.
 * Keep in sync with memcg_exact_page().
 */
static unsigned long memcg_exact_page_state(struct mem_cgroup *memcg, int idx)
{
	long x = atomic_long_read(&memcg->vmstats[idx]);
	int cpu;

	for_each_online_cpu(cpu)
		x += per_cpu_ptr(memcg->vmstats_percpu, cpu)->stat[idx];
	if (x < 0)
		x = 0;
	return x;
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

	*pdirty = memcg_exact_page_state(memcg, NR_FILE_DIRTY);

	*pwriteback = memcg_exact_page_state(memcg, NR_WRITEBACK);
	*pfilepages = memcg_exact_page_state(memcg, NR_INACTIVE_FILE) +
			memcg_exact_page_state(memcg, NR_ACTIVE_FILE);
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
 * trackes ownership per-page while the latter per-inode.  This was a
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
 * Conditions like the above can lead to a cgroup getting repatedly and
 * severely throttled after making some progress after each
 * dirty_expire_interval while the underyling IO device is almost
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
void mem_cgroup_track_foreign_dirty_slowpath(struct page *page,
					     struct bdi_writeback *wb)
{
	struct mem_cgroup *memcg = page->mem_cgroup;
	struct memcg_cgwb_frn *frn;
	u64 now = get_jiffies_64();
	u64 oldest_at = now;
	int oldest = -1;
	int i;

	trace_track_foreign_dirty(page, wb);

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
			cgroup_writeback_by_id(frn->bdi_id, frn->memcg_id, 0,
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
 * Gets called on EPOLLHUP on eventfd when user closes it.
 *
 * Called with wqh->lock held and interrupts disabled.
 */
static int memcg_event_wake(wait_queue_entry_t *wait, unsigned mode,
			    int sync, void *key)
{
	struct mem_cgroup_event *event =
		container_of(wait, struct mem_cgroup_event, wait);
	struct mem_cgroup *memcg = event->memcg;
	__poll_t flags = key_to_poll(key);

	if (flags & EPOLLHUP) {
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

	vfs_poll(efile.file, &event->pt);

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
#if defined(CONFIG_MEMCG_KMEM) && \
	(defined(CONFIG_SLAB) || defined(CONFIG_SLUB_DEBUG))
	{
		.name = "kmem.slabinfo",
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

static DEFINE_IDR(mem_cgroup_idr);

static void mem_cgroup_id_remove(struct mem_cgroup *memcg)
{
	if (memcg->id.id > 0) {
		idr_remove(&mem_cgroup_idr, memcg->id.id);
		memcg->id.id = 0;
	}
}

static void __maybe_unused mem_cgroup_id_get_many(struct mem_cgroup *memcg,
						  unsigned int n)
{
	refcount_add(n, &memcg->id.ref);
}

static void mem_cgroup_id_put_many(struct mem_cgroup *memcg, unsigned int n)
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

	pn->lruvec_stat_local = alloc_percpu_gfp(struct lruvec_stat,
						 GFP_KERNEL_ACCOUNT);
	if (!pn->lruvec_stat_local) {
		kfree(pn);
		return 1;
	}

	pn->lruvec_stat_cpu = alloc_percpu_gfp(struct lruvec_stat,
					       GFP_KERNEL_ACCOUNT);
	if (!pn->lruvec_stat_cpu) {
		free_percpu(pn->lruvec_stat_local);
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

	if (!pn)
		return;

	free_percpu(pn->lruvec_stat_cpu);
	free_percpu(pn->lruvec_stat_local);
	kfree(pn);
}

static void __mem_cgroup_free(struct mem_cgroup *memcg)
{
	int node;

	for_each_node(node)
		free_mem_cgroup_per_node_info(memcg, node);
	free_percpu(memcg->vmstats_percpu);
	free_percpu(memcg->vmstats_local);
	kfree(memcg);
}

static void mem_cgroup_free(struct mem_cgroup *memcg)
{
	memcg_wb_domain_exit(memcg);
	/*
	 * Flush percpu vmstats and vmevents to guarantee the value correctness
	 * on parent's and all ancestor levels.
	 */
	memcg_flush_percpu_vmstats(memcg);
	memcg_flush_percpu_vmevents(memcg);
	__mem_cgroup_free(memcg);
}

static struct mem_cgroup *mem_cgroup_alloc(void)
{
	struct mem_cgroup *memcg;
	unsigned int size;
	int node;
	int __maybe_unused i;
	long error = -ENOMEM;

	size = sizeof(struct mem_cgroup);
	size += nr_node_ids * sizeof(struct mem_cgroup_per_node *);

	memcg = kzalloc(size, GFP_KERNEL);
	if (!memcg)
		return ERR_PTR(error);

	memcg->id.id = idr_alloc(&mem_cgroup_idr, NULL,
				 1, MEM_CGROUP_ID_MAX,
				 GFP_KERNEL);
	if (memcg->id.id < 0) {
		error = memcg->id.id;
		goto fail;
	}

	memcg->vmstats_local = alloc_percpu_gfp(struct memcg_vmstats_percpu,
						GFP_KERNEL_ACCOUNT);
	if (!memcg->vmstats_local)
		goto fail;

	memcg->vmstats_percpu = alloc_percpu_gfp(struct memcg_vmstats_percpu,
						 GFP_KERNEL_ACCOUNT);
	if (!memcg->vmstats_percpu)
		goto fail;

	for_each_node(node)
		if (alloc_mem_cgroup_per_node_info(memcg, node))
			goto fail;

	if (memcg_wb_domain_init(memcg, GFP_KERNEL))
		goto fail;

	INIT_WORK(&memcg->high_work, high_work_func);
	INIT_LIST_HEAD(&memcg->oom_notify);
	mutex_init(&memcg->thresholds_lock);
	spin_lock_init(&memcg->move_lock);
	vmpressure_init(&memcg->vmpressure);
	INIT_LIST_HEAD(&memcg->event_list);
	spin_lock_init(&memcg->event_list_lock);
	memcg->socket_pressure = jiffies;
#ifdef CONFIG_MEMCG_KMEM
	memcg->kmemcg_id = -1;
	INIT_LIST_HEAD(&memcg->objcg_list);
#endif
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
	idr_replace(&mem_cgroup_idr, memcg, memcg->id.id);
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
	long error = -ENOMEM;

	old_memcg = set_active_memcg(parent);
	memcg = mem_cgroup_alloc();
	set_active_memcg(old_memcg);
	if (IS_ERR(memcg))
		return ERR_CAST(memcg);

	page_counter_set_high(&memcg->memory, PAGE_COUNTER_MAX);
	memcg->soft_limit = PAGE_COUNTER_MAX;
	page_counter_set_high(&memcg->swap, PAGE_COUNTER_MAX);
	if (parent) {
		memcg->swappiness = mem_cgroup_swappiness(parent);
		memcg->oom_kill_disable = parent->oom_kill_disable;
	}
	if (!parent) {
		page_counter_init(&memcg->memory, NULL);
		page_counter_init(&memcg->swap, NULL);
		page_counter_init(&memcg->kmem, NULL);
		page_counter_init(&memcg->tcpmem, NULL);
	} else if (parent->use_hierarchy) {
		memcg->use_hierarchy = true;
		page_counter_init(&memcg->memory, &parent->memory);
		page_counter_init(&memcg->swap, &parent->swap);
		page_counter_init(&memcg->kmem, &parent->kmem);
		page_counter_init(&memcg->tcpmem, &parent->tcpmem);
	} else {
		page_counter_init(&memcg->memory, &root_mem_cgroup->memory);
		page_counter_init(&memcg->swap, &root_mem_cgroup->swap);
		page_counter_init(&memcg->kmem, &root_mem_cgroup->kmem);
		page_counter_init(&memcg->tcpmem, &root_mem_cgroup->tcpmem);
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
	mem_cgroup_id_remove(memcg);
	mem_cgroup_free(memcg);
	return ERR_PTR(error);
}

static int mem_cgroup_css_online(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	/*
	 * A memcg must be visible for memcg_expand_shrinker_maps()
	 * by the time the maps are allocated. So, we allocate maps
	 * here, when for_each_mem_cgroup() can't skip it.
	 */
	if (memcg_alloc_shrinker_maps(memcg)) {
		mem_cgroup_id_remove(memcg);
		return -ENOMEM;
	}

	/* Online state pins memcg ID, memcg ID pins CSS */
	refcount_set(&memcg->id.ref, 1);
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

	page_counter_set_min(&memcg->memory, 0);
	page_counter_set_low(&memcg->memory, 0);

	memcg_offline_kmem(memcg);
	wb_memcg_offline(memcg);

	drain_all_stock(memcg);

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
	int __maybe_unused i;

#ifdef CONFIG_CGROUP_WRITEBACK
	for (i = 0; i < MEMCG_CGWB_FRN_CNT; i++)
		wb_wait_for_completion(&memcg->cgwb_frn[i].done);
#endif
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys) && !cgroup_memory_nosocket)
		static_branch_dec(&memcg_sockets_enabled_key);

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && memcg->tcpmem_active)
		static_branch_dec(&memcg_sockets_enabled_key);

	vmpressure_cleanup(&memcg->vmpressure);
	cancel_work_sync(&memcg->high_work);
	mem_cgroup_remove_from_trees(memcg);
	memcg_free_shrinker_maps(memcg);
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

	page_counter_set_max(&memcg->memory, PAGE_COUNTER_MAX);
	page_counter_set_max(&memcg->swap, PAGE_COUNTER_MAX);
	page_counter_set_max(&memcg->kmem, PAGE_COUNTER_MAX);
	page_counter_set_max(&memcg->tcpmem, PAGE_COUNTER_MAX);
	page_counter_set_min(&memcg->memory, 0);
	page_counter_set_low(&memcg->memory, 0);
	page_counter_set_high(&memcg->memory, PAGE_COUNTER_MAX);
	memcg->soft_limit = PAGE_COUNTER_MAX;
	page_counter_set_high(&memcg->swap, PAGE_COUNTER_MAX);
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
	struct page *page = vm_normal_page(vma, addr, ptent);

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

	if (!(mc.flags & MOVE_ANON))
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

	if (non_swap_entry(ent))
		return NULL;

	/*
	 * Because lookup_swap_cache() updates some statistics counter,
	 * we call find_get_page() with swapper_space directly.
	 */
	page = find_get_page(swap_address_space(ent), swp_offset(ent));
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
	if (!vma->vm_file) /* anonymous vma */
		return NULL;
	if (!(mc.flags & MOVE_FILE))
		return NULL;

	/* page is moved even if it's not RSS of this task(page-faulted). */
	/* shmem/tmpfs may report page out on swap: account for that too. */
	return find_get_incore_page(vma->vm_file->f_mapping,
			linear_page_index(vma, addr));
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
	struct lruvec *from_vec, *to_vec;
	struct pglist_data *pgdat;
	unsigned int nr_pages = compound ? thp_nr_pages(page) : 1;
	int ret;

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

	pgdat = page_pgdat(page);
	from_vec = mem_cgroup_lruvec(from, pgdat);
	to_vec = mem_cgroup_lruvec(to, pgdat);

	lock_page_memcg(page);

	if (PageAnon(page)) {
		if (page_mapped(page)) {
			__mod_lruvec_state(from_vec, NR_ANON_MAPPED, -nr_pages);
			__mod_lruvec_state(to_vec, NR_ANON_MAPPED, nr_pages);
			if (PageTransHuge(page)) {
				__dec_lruvec_state(from_vec, NR_ANON_THPS);
				__inc_lruvec_state(to_vec, NR_ANON_THPS);
			}

		}
	} else {
		__mod_lruvec_state(from_vec, NR_FILE_PAGES, -nr_pages);
		__mod_lruvec_state(to_vec, NR_FILE_PAGES, nr_pages);

		if (PageSwapBacked(page)) {
			__mod_lruvec_state(from_vec, NR_SHMEM, -nr_pages);
			__mod_lruvec_state(to_vec, NR_SHMEM, nr_pages);
		}

		if (page_mapped(page)) {
			__mod_lruvec_state(from_vec, NR_FILE_MAPPED, -nr_pages);
			__mod_lruvec_state(to_vec, NR_FILE_MAPPED, nr_pages);
		}

		if (PageDirty(page)) {
			struct address_space *mapping = page_mapping(page);

			if (mapping_can_writeback(mapping)) {
				__mod_lruvec_state(from_vec, NR_FILE_DIRTY,
						   -nr_pages);
				__mod_lruvec_state(to_vec, NR_FILE_DIRTY,
						   nr_pages);
			}
		}
	}

	if (PageWriteback(page)) {
		__mod_lruvec_state(from_vec, NR_WRITEBACK, -nr_pages);
		__mod_lruvec_state(to_vec, NR_WRITEBACK, nr_pages);
	}

	/*
	 * All state has been migrated, let's switch to the new memcg.
	 *
	 * It is safe to change page->mem_cgroup here because the page
	 * is referenced, charged, isolated, and locked: we can't race
	 * with (un)charging, migration, LRU putback, or anything else
	 * that would rely on a stable page->mem_cgroup.
	 *
	 * Note that lock_page_memcg is a memcg lock, not a page lock,
	 * to save space. As soon as we switch page->mem_cgroup to a
	 * new memcg that isn't locked, the above state can change
	 * concurrently again. Make sure we're truly done with it.
	 */
	smp_mb();

	css_get(&to->css);
	css_put(&from->css);

	page->mem_cgroup = to;

	__unlock_page_memcg(from);

	ret = 0;

	local_irq_disable();
	mem_cgroup_charge_statistics(to, page, nr_pages);
	memcg_check_events(to, page);
	mem_cgroup_charge_statistics(from, page, -nr_pages);
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
 *   3(MC_TARGET_DEVICE): like MC_TARGET_PAGE  but page is MEMORY_DEVICE_PRIVATE
 *     (so ZONE_DEVICE page and thus not on the lru).
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
			if (is_device_private_page(page))
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
		 * support transparent huge page with MEMORY_DEVICE_PRIVATE but
		 * this might change.
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

static const struct mm_walk_ops precharge_walk_ops = {
	.pmd_entry	= mem_cgroup_count_precharge_pte_range,
};

static unsigned long mem_cgroup_count_precharge(struct mm_struct *mm)
{
	unsigned long precharge;

	mmap_read_lock(mm);
	walk_page_range(mm, 0, mm->highest_vm_end, &precharge_walk_ops, NULL);
	mmap_read_unlock(mm);

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
			fallthrough;
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
				mem_cgroup_id_get_many(mc.to, 1);
				/* we fixup other refcnts and charges later. */
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

static const struct mm_walk_ops charge_walk_ops = {
	.pmd_entry	= mem_cgroup_move_charge_pte_range,
};

static void mem_cgroup_move_charge(void)
{
	lru_add_drain_all();
	/*
	 * Signal lock_page_memcg() to take the memcg's move_lock
	 * while we're moving its pages to another memcg. Then wait
	 * for already started RCU-only updates to finish.
	 */
	atomic_inc(&mc.from->moving_account);
	synchronize_rcu();
retry:
	if (unlikely(!mmap_read_trylock(mc.mm))) {
		/*
		 * Someone who are holding the mmap_lock might be waiting in
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
	walk_page_range(mc.mm, 0, mc.mm->highest_vm_end, &charge_walk_ops,
			NULL);

	mmap_read_unlock(mc.mm);
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
							 GFP_KERNEL, true);

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
							  GFP_KERNEL, true))
				nr_reclaims--;
			continue;
		}

		memcg_memory_event(memcg, MEMCG_OOM);
		if (!mem_cgroup_out_of_memory(memcg, GFP_KERNEL, 0))
			break;
	}

	memcg_wb_domain_size_changed(memcg);
	return nbytes;
}

static void __memory_events_show(struct seq_file *m, atomic_long_t *events)
{
	seq_printf(m, "low %lu\n", atomic_long_read(&events[MEMCG_LOW]));
	seq_printf(m, "high %lu\n", atomic_long_read(&events[MEMCG_HIGH]));
	seq_printf(m, "max %lu\n", atomic_long_read(&events[MEMCG_MAX]));
	seq_printf(m, "oom %lu\n", atomic_long_read(&events[MEMCG_OOM]));
	seq_printf(m, "oom_kill %lu\n",
		   atomic_long_read(&events[MEMCG_OOM_KILL]));
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

static int memory_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);
	char *buf;

	buf = memory_stat_format(memcg);
	if (!buf)
		return -ENOMEM;
	seq_puts(m, buf);
	kfree(buf);
	return 0;
}

#ifdef CONFIG_NUMA
static int memory_numa_stat_show(struct seq_file *m, void *v)
{
	int i;
	struct mem_cgroup *memcg = mem_cgroup_from_seq(m);

	for (i = 0; i < ARRAY_SIZE(memory_stats); i++) {
		int nid;

		if (memory_stats[i].idx >= NR_VM_NODE_STAT_ITEMS)
			continue;

		seq_printf(m, "%s", memory_stats[i].name);
		for_each_node_state(nid, N_MEMORY) {
			u64 size;
			struct lruvec *lruvec;

			lruvec = mem_cgroup_lruvec(memcg, NODE_DATA(nid));
			size = lruvec_page_state(lruvec, memory_stats[i].idx);
			size *= memory_stats[i].ratio;
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

	seq_printf(m, "%d\n", memcg->oom_group);

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

	memcg->oom_group = oom_group;

	return nbytes;
}

static struct cftype memory_files[] = {
	{
		.name = "current",
		.flags = CFTYPE_NOT_ON_ROOT,
		.read_u64 = memory_current_read,
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

/*
 * This function calculates an individual cgroup's effective
 * protection which is derived from its own memory.min/low, its
 * parent's and siblings' settings, as well as the actual memory
 * distribution in the tree.
 *
 * The following rules apply to the effective protection values:
 *
 * 1. At the first level of reclaim, effective protection is equal to
 *    the declared protection in memory.min and memory.low.
 *
 * 2. To enable safe delegation of the protection configuration, at
 *    subsequent levels the effective protection is capped to the
 *    parent's effective protection.
 *
 * 3. To make complex and dynamic subtrees easier to configure, the
 *    user is allowed to overcommit the declared protection at a given
 *    level. If that is the case, the parent's effective protection is
 *    distributed to the children in proportion to how much protection
 *    they have declared and how much of it they are utilizing.
 *
 *    This makes distribution proportional, but also work-conserving:
 *    if one cgroup claims much more protection than it uses memory,
 *    the unused remainder is available to its siblings.
 *
 * 4. Conversely, when the declared protection is undercommitted at a
 *    given level, the distribution of the larger parental protection
 *    budget is NOT proportional. A cgroup's protection from a sibling
 *    is capped to its own memory.min/low setting.
 *
 * 5. However, to allow protecting recursive subtrees from each other
 *    without having to declare each individual cgroup's fixed share
 *    of the ancestor's claim to protection, any unutilized -
 *    "floating" - protection from up the tree is distributed in
 *    proportion to each cgroup's *usage*. This makes the protection
 *    neutral wrt sibling cgroups and lets them compete freely over
 *    the shared parental protection budget, but it protects the
 *    subtree as a whole from neighboring subtrees.
 *
 * Note that 4. and 5. are not in conflict: 4. is about protecting
 * against immediate siblings whereas 5. is about protecting against
 * neighboring subtrees.
 */
static unsigned long effective_protection(unsigned long usage,
					  unsigned long parent_usage,
					  unsigned long setting,
					  unsigned long parent_effective,
					  unsigned long siblings_protected)
{
	unsigned long protected;
	unsigned long ep;

	protected = min(usage, setting);
	/*
	 * If all cgroups at this level combined claim and use more
	 * protection then what the parent affords them, distribute
	 * shares in proportion to utilization.
	 *
	 * We are using actual utilization rather than the statically
	 * claimed protection in order to be work-conserving: claimed
	 * but unused protection is available to siblings that would
	 * otherwise get a smaller chunk than what they claimed.
	 */
	if (siblings_protected > parent_effective)
		return protected * parent_effective / siblings_protected;

	/*
	 * Ok, utilized protection of all children is within what the
	 * parent affords them, so we know whatever this child claims
	 * and utilizes is effectively protected.
	 *
	 * If there is unprotected usage beyond this value, reclaim
	 * will apply pressure in proportion to that amount.
	 *
	 * If there is unutilized protection, the cgroup will be fully
	 * shielded from reclaim, but we do return a smaller value for
	 * protection than what the group could enjoy in theory. This
	 * is okay. With the overcommit distribution above, effective
	 * protection is always dependent on how memory is actually
	 * consumed among the siblings anyway.
	 */
	ep = protected;

	/*
	 * If the children aren't claiming (all of) the protection
	 * afforded to them by the parent, distribute the remainder in
	 * proportion to the (unprotected) memory of each cgroup. That
	 * way, cgroups that aren't explicitly prioritized wrt each
	 * other compete freely over the allowance, but they are
	 * collectively protected from neighboring trees.
	 *
	 * We're using unprotected memory for the weight so that if
	 * some cgroups DO claim explicit protection, we don't protect
	 * the same bytes twice.
	 *
	 * Check both usage and parent_usage against the respective
	 * protected values. One should imply the other, but they
	 * aren't read atomically - make sure the division is sane.
	 */
	if (!(cgrp_dfl_root.flags & CGRP_ROOT_MEMORY_RECURSIVE_PROT))
		return ep;
	if (parent_effective > siblings_protected &&
	    parent_usage > siblings_protected &&
	    usage > protected) {
		unsigned long unclaimed;

		unclaimed = parent_effective - siblings_protected;
		unclaimed *= usage - protected;
		unclaimed /= parent_usage - siblings_protected;

		ep += unclaimed;
	}

	return ep;
}

/**
 * mem_cgroup_protected - check if memory consumption is in the normal range
 * @root: the top ancestor of the sub-tree being checked
 * @memcg: the memory cgroup to check
 *
 * WARNING: This function is not stateless! It can only be used as part
 *          of a top-down tree iteration, not for isolated queries.
 */
void mem_cgroup_calculate_protection(struct mem_cgroup *root,
				     struct mem_cgroup *memcg)
{
	unsigned long usage, parent_usage;
	struct mem_cgroup *parent;

	if (mem_cgroup_disabled())
		return;

	if (!root)
		root = root_mem_cgroup;

	/*
	 * Effective values of the reclaim targets are ignored so they
	 * can be stale. Have a look at mem_cgroup_protection for more
	 * details.
	 * TODO: calculation should be more robust so that we do not need
	 * that special casing.
	 */
	if (memcg == root)
		return;

	usage = page_counter_read(&memcg->memory);
	if (!usage)
		return;

	parent = parent_mem_cgroup(memcg);
	/* No parent means a non-hierarchical mode on v1 memcg */
	if (!parent)
		return;

	if (parent == root) {
		memcg->memory.emin = READ_ONCE(memcg->memory.min);
		memcg->memory.elow = READ_ONCE(memcg->memory.low);
		return;
	}

	parent_usage = page_counter_read(&parent->memory);

	WRITE_ONCE(memcg->memory.emin, effective_protection(usage, parent_usage,
			READ_ONCE(memcg->memory.min),
			READ_ONCE(parent->memory.emin),
			atomic_long_read(&parent->memory.children_min_usage)));

	WRITE_ONCE(memcg->memory.elow, effective_protection(usage, parent_usage,
			READ_ONCE(memcg->memory.low),
			READ_ONCE(parent->memory.elow),
			atomic_long_read(&parent->memory.children_low_usage)));
}

/**
 * mem_cgroup_charge - charge a newly allocated page to a cgroup
 * @page: page to charge
 * @mm: mm context of the victim
 * @gfp_mask: reclaim mode
 *
 * Try to charge @page to the memcg that @mm belongs to, reclaiming
 * pages according to @gfp_mask if necessary.
 *
 * Returns 0 on success. Otherwise, an error code is returned.
 */
int mem_cgroup_charge(struct page *page, struct mm_struct *mm, gfp_t gfp_mask)
{
	unsigned int nr_pages = thp_nr_pages(page);
	struct mem_cgroup *memcg = NULL;
	int ret = 0;

	if (mem_cgroup_disabled())
		goto out;

	if (PageSwapCache(page)) {
		swp_entry_t ent = { .val = page_private(page), };
		unsigned short id;

		/*
		 * Every swap fault against a single page tries to charge the
		 * page, bail as early as possible.  shmem_unuse() encounters
		 * already charged pages, too.  page->mem_cgroup is protected
		 * by the page lock, which serializes swap cache removal, which
		 * in turn serializes uncharging.
		 */
		VM_BUG_ON_PAGE(!PageLocked(page), page);
		if (compound_head(page)->mem_cgroup)
			goto out;

		id = lookup_swap_cgroup_id(ent);
		rcu_read_lock();
		memcg = mem_cgroup_from_id(id);
		if (memcg && !css_tryget_online(&memcg->css))
			memcg = NULL;
		rcu_read_unlock();
	}

	if (!memcg)
		memcg = get_mem_cgroup_from_mm(mm);

	ret = try_charge(memcg, gfp_mask, nr_pages);
	if (ret)
		goto out_put;

	css_get(&memcg->css);
	commit_charge(page, memcg);

	local_irq_disable();
	mem_cgroup_charge_statistics(memcg, page, nr_pages);
	memcg_check_events(memcg, page);
	local_irq_enable();

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
	if (do_memsw_account() && PageSwapCache(page)) {
		swp_entry_t entry = { .val = page_private(page) };
		/*
		 * The swap entry might not get freed for a long time,
		 * let's not wait for it.  The page already received a
		 * memory+swap charge, drop the swap entry duplicate.
		 */
		mem_cgroup_uncharge_swap(entry, nr_pages);
	}

out_put:
	css_put(&memcg->css);
out:
	return ret;
}

struct uncharge_gather {
	struct mem_cgroup *memcg;
	unsigned long nr_pages;
	unsigned long pgpgout;
	unsigned long nr_kmem;
	struct page *dummy_page;
};

static inline void uncharge_gather_clear(struct uncharge_gather *ug)
{
	memset(ug, 0, sizeof(*ug));
}

static void uncharge_batch(const struct uncharge_gather *ug)
{
	unsigned long flags;

	if (!mem_cgroup_is_root(ug->memcg)) {
		page_counter_uncharge(&ug->memcg->memory, ug->nr_pages);
		if (do_memsw_account())
			page_counter_uncharge(&ug->memcg->memsw, ug->nr_pages);
		if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && ug->nr_kmem)
			page_counter_uncharge(&ug->memcg->kmem, ug->nr_kmem);
		memcg_oom_recover(ug->memcg);
	}

	local_irq_save(flags);
	__count_memcg_events(ug->memcg, PGPGOUT, ug->pgpgout);
	__this_cpu_add(ug->memcg->vmstats_percpu->nr_page_events, ug->nr_pages);
	memcg_check_events(ug->memcg, ug->dummy_page);
	local_irq_restore(flags);

	/* drop reference from uncharge_page */
	css_put(&ug->memcg->css);
}

static void uncharge_page(struct page *page, struct uncharge_gather *ug)
{
	unsigned long nr_pages;

	VM_BUG_ON_PAGE(PageLRU(page), page);

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

		/* pairs with css_put in uncharge_batch */
		css_get(&ug->memcg->css);
	}

	nr_pages = compound_nr(page);
	ug->nr_pages += nr_pages;

	if (!PageKmemcg(page)) {
		ug->pgpgout++;
	} else {
		ug->nr_kmem += nr_pages;
		__ClearPageKmemcg(page);
	}

	ug->dummy_page = page;
	page->mem_cgroup = NULL;
	css_put(&ug->memcg->css);
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
 * Uncharge a page previously charged with mem_cgroup_charge().
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
 * mem_cgroup_charge().
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
	nr_pages = thp_nr_pages(newpage);

	page_counter_charge(&memcg->memory, nr_pages);
	if (do_memsw_account())
		page_counter_charge(&memcg->memsw, nr_pages);

	css_get(&memcg->css);
	commit_charge(newpage, memcg);

	local_irq_save(flags);
	mem_cgroup_charge_statistics(memcg, newpage, nr_pages);
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

	/* Do not associate the sock with unrelated interrupted task's memcg. */
	if (in_interrupt())
		return;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(current);
	if (memcg == root_mem_cgroup)
		goto out;
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && !memcg->tcpmem_active)
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

	mod_memcg_state(memcg, MEMCG_SOCK, nr_pages);

	if (try_charge(memcg, gfp_mask, nr_pages) == 0)
		return true;

	try_charge(memcg, gfp_mask|__GFP_NOFAIL, nr_pages);
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
		page_counter_uncharge(&memcg->tcpmem, nr_pages);
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
	while (!refcount_inc_not_zero(&memcg->id.ref)) {
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

	if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
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
	nr_entries = thp_nr_pages(page);
	/* Get references for the tail pages, too */
	if (nr_entries > 1)
		mem_cgroup_id_get_many(swap_memcg, nr_entries - 1);
	oldid = swap_cgroup_record(entry, mem_cgroup_id(swap_memcg),
				   nr_entries);
	VM_BUG_ON_PAGE(oldid, page);
	mod_memcg_state(swap_memcg, MEMCG_SWAP, nr_entries);

	page->mem_cgroup = NULL;

	if (!mem_cgroup_is_root(memcg))
		page_counter_uncharge(&memcg->memory, nr_entries);

	if (!cgroup_memory_noswap && memcg != swap_memcg) {
		if (!mem_cgroup_is_root(swap_memcg))
			page_counter_charge(&swap_memcg->memsw, nr_entries);
		page_counter_uncharge(&memcg->memsw, nr_entries);
	}

	/*
	 * Interrupts should be disabled here because the caller holds the
	 * i_pages lock which is taken with interrupts-off. It is
	 * important here to have the interrupts disabled because it is the
	 * only synchronisation we have for updating the per-CPU variables.
	 */
	VM_BUG_ON(!irqs_disabled());
	mem_cgroup_charge_statistics(memcg, page, -nr_entries);
	memcg_check_events(memcg, page);

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
	unsigned int nr_pages = thp_nr_pages(page);
	struct page_counter *counter;
	struct mem_cgroup *memcg;
	unsigned short oldid;

	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return 0;

	memcg = page->mem_cgroup;

	/* Readahead page, never charged */
	if (!memcg)
		return 0;

	if (!entry.val) {
		memcg_memory_event(memcg, MEMCG_SWAP_FAIL);
		return 0;
	}

	memcg = mem_cgroup_id_get_online(memcg);

	if (!cgroup_memory_noswap && !mem_cgroup_is_root(memcg) &&
	    !page_counter_try_charge(&memcg->swap, nr_pages, &counter)) {
		memcg_memory_event(memcg, MEMCG_SWAP_MAX);
		memcg_memory_event(memcg, MEMCG_SWAP_FAIL);
		mem_cgroup_id_put(memcg);
		return -ENOMEM;
	}

	/* Get references for the tail pages, too */
	if (nr_pages > 1)
		mem_cgroup_id_get_many(memcg, nr_pages - 1);
	oldid = swap_cgroup_record(entry, mem_cgroup_id(memcg), nr_pages);
	VM_BUG_ON_PAGE(oldid, page);
	mod_memcg_state(memcg, MEMCG_SWAP, nr_pages);

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

	id = swap_cgroup_record(entry, 0, nr_pages);
	rcu_read_lock();
	memcg = mem_cgroup_from_id(id);
	if (memcg) {
		if (!cgroup_memory_noswap && !mem_cgroup_is_root(memcg)) {
			if (cgroup_subsys_on_dfl(memory_cgrp_subsys))
				page_counter_uncharge(&memcg->swap, nr_pages);
			else
				page_counter_uncharge(&memcg->memsw, nr_pages);
		}
		mod_memcg_state(memcg, MEMCG_SWAP, -nr_pages);
		mem_cgroup_id_put_many(memcg, nr_pages);
	}
	rcu_read_unlock();
}

long mem_cgroup_get_nr_swap_pages(struct mem_cgroup *memcg)
{
	long nr_swap_pages = get_nr_swap_pages();

	if (cgroup_memory_noswap || !cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return nr_swap_pages;
	for (; memcg != root_mem_cgroup; memcg = parent_mem_cgroup(memcg))
		nr_swap_pages = min_t(long, nr_swap_pages,
				      READ_ONCE(memcg->swap.max) -
				      page_counter_read(&memcg->swap));
	return nr_swap_pages;
}

bool mem_cgroup_swap_full(struct page *page)
{
	struct mem_cgroup *memcg;

	VM_BUG_ON_PAGE(!PageLocked(page), page);

	if (vm_swap_full())
		return true;
	if (cgroup_memory_noswap || !cgroup_subsys_on_dfl(memory_cgrp_subsys))
		return false;

	memcg = page->mem_cgroup;
	if (!memcg)
		return false;

	for (; memcg != root_mem_cgroup; memcg = parent_mem_cgroup(memcg)) {
		unsigned long usage = page_counter_read(&memcg->swap);

		if (usage * 2 >= READ_ONCE(memcg->swap.high) ||
		    usage * 2 >= READ_ONCE(memcg->swap.max))
			return true;
	}

	return false;
}

static int __init setup_swap_account(char *s)
{
	if (!strcmp(s, "1"))
		cgroup_memory_noswap = 0;
	else if (!strcmp(s, "0"))
		cgroup_memory_noswap = 1;
	return 1;
}
__setup("swapaccount=", setup_swap_account);

static u64 swap_current_read(struct cgroup_subsys_state *css,
			     struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);

	return (u64)page_counter_read(&memcg->swap) * PAGE_SIZE;
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
		.name = "swap.events",
		.flags = CFTYPE_NOT_ON_ROOT,
		.file_offset = offsetof(struct mem_cgroup, swap_events_file),
		.seq_show = swap_events_show,
	},
	{ }	/* terminate */
};

static struct cftype memsw_files[] = {
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

/*
 * If mem_cgroup_swap_init() is implemented as a subsys_initcall()
 * instead of a core_initcall(), this could mean cgroup_memory_noswap still
 * remains set to false even when memcg is disabled via "cgroup_disable=memory"
 * boot parameter. This may result in premature OOPS inside
 * mem_cgroup_get_nr_swap_pages() function in corner cases.
 */
static int __init mem_cgroup_swap_init(void)
{
	/* No memory control -> no swap control */
	if (mem_cgroup_disabled())
		cgroup_memory_noswap = true;

	if (cgroup_memory_noswap)
		return 0;

	WARN_ON(cgroup_add_dfl_cftypes(&memory_cgrp_subsys, swap_files));
	WARN_ON(cgroup_add_legacy_cftypes(&memory_cgrp_subsys, memsw_files));

	return 0;
}
core_initcall(mem_cgroup_swap_init);

#endif /* CONFIG_MEMCG_SWAP */
