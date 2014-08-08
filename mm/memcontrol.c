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

#include <linux/res_counter.h>
#include <linux/memcontrol.h>
#include <linux/cgroup.h>
#include <linux/mm.h>
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
#include <linux/page_cgroup.h>
#include <linux/cpu.h>
#include <linux/oom.h>
#include <linux/lockdep.h>
#include <linux/file.h>
#include "internal.h"
#include <net/sock.h>
#include <net/ip.h>
#include <net/tcp_memcontrol.h>
#include "slab.h"

#include <asm/uaccess.h>

#include <trace/events/vmscan.h>

struct cgroup_subsys memory_cgrp_subsys __read_mostly;
EXPORT_SYMBOL(memory_cgrp_subsys);

#define MEM_CGROUP_RECLAIM_RETRIES	5
static struct mem_cgroup *root_mem_cgroup __read_mostly;

#ifdef CONFIG_MEMCG_SWAP
/* Turned on only when memory cgroup is enabled && really_do_swap_account = 1 */
int do_swap_account __read_mostly;

/* for remember boot option*/
#ifdef CONFIG_MEMCG_SWAP_ENABLED
static int really_do_swap_account __initdata = 1;
#else
static int really_do_swap_account __initdata;
#endif

#else
#define do_swap_account		0
#endif


static const char * const mem_cgroup_stat_names[] = {
	"cache",
	"rss",
	"rss_huge",
	"mapped_file",
	"writeback",
	"swap",
};

enum mem_cgroup_events_index {
	MEM_CGROUP_EVENTS_PGPGIN,	/* # of pages paged in */
	MEM_CGROUP_EVENTS_PGPGOUT,	/* # of pages paged out */
	MEM_CGROUP_EVENTS_PGFAULT,	/* # of page-faults */
	MEM_CGROUP_EVENTS_PGMAJFAULT,	/* # of major page-faults */
	MEM_CGROUP_EVENTS_NSTATS,
};

static const char * const mem_cgroup_events_names[] = {
	"pgpgin",
	"pgpgout",
	"pgfault",
	"pgmajfault",
};

static const char * const mem_cgroup_lru_names[] = {
	"inactive_anon",
	"active_anon",
	"inactive_file",
	"active_file",
	"unevictable",
};

/*
 * Per memcg event counter is incremented at every pagein/pageout. With THP,
 * it will be incremated by the number of pages. This counter is used for
 * for trigger some periodic events. This is straightforward and better
 * than using jiffies etc. to handle periodic memcg event.
 */
enum mem_cgroup_events_target {
	MEM_CGROUP_TARGET_THRESH,
	MEM_CGROUP_TARGET_SOFTLIMIT,
	MEM_CGROUP_TARGET_NUMAINFO,
	MEM_CGROUP_NTARGETS,
};
#define THRESHOLDS_EVENTS_TARGET 128
#define SOFTLIMIT_EVENTS_TARGET 1024
#define NUMAINFO_EVENTS_TARGET	1024

struct mem_cgroup_stat_cpu {
	long count[MEM_CGROUP_STAT_NSTATS];
	unsigned long events[MEM_CGROUP_EVENTS_NSTATS];
	unsigned long nr_page_events;
	unsigned long targets[MEM_CGROUP_NTARGETS];
};

struct mem_cgroup_reclaim_iter {
	/*
	 * last scanned hierarchy member. Valid only if last_dead_count
	 * matches memcg->dead_count of the hierarchy root group.
	 */
	struct mem_cgroup *last_visited;
	int last_dead_count;

	/* scan generation, increased every round-trip */
	unsigned int generation;
};

/*
 * per-zone information in memory controller.
 */
struct mem_cgroup_per_zone {
	struct lruvec		lruvec;
	unsigned long		lru_size[NR_LRU_LISTS];

	struct mem_cgroup_reclaim_iter reclaim_iter[DEF_PRIORITY + 1];

	struct rb_node		tree_node;	/* RB tree node */
	unsigned long long	usage_in_excess;/* Set to the value by which */
						/* the soft limit is exceeded*/
	bool			on_tree;
	struct mem_cgroup	*memcg;		/* Back pointer, we cannot */
						/* use container_of	   */
};

struct mem_cgroup_per_node {
	struct mem_cgroup_per_zone zoneinfo[MAX_NR_ZONES];
};

/*
 * Cgroups above their limits are maintained in a RB-Tree, independent of
 * their hierarchy representation
 */

struct mem_cgroup_tree_per_zone {
	struct rb_root rb_root;
	spinlock_t lock;
};

struct mem_cgroup_tree_per_node {
	struct mem_cgroup_tree_per_zone rb_tree_per_zone[MAX_NR_ZONES];
};

struct mem_cgroup_tree {
	struct mem_cgroup_tree_per_node *rb_tree_per_node[MAX_NUMNODES];
};

static struct mem_cgroup_tree soft_limit_tree __read_mostly;

struct mem_cgroup_threshold {
	struct eventfd_ctx *eventfd;
	u64 threshold;
};

/* For threshold */
struct mem_cgroup_threshold_ary {
	/* An array index points to threshold just below or equal to usage. */
	int current_threshold;
	/* Size of entries[] */
	unsigned int size;
	/* Array of thresholds */
	struct mem_cgroup_threshold entries[0];
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
	wait_queue_t wait;
	struct work_struct remove;
};

static void mem_cgroup_threshold(struct mem_cgroup *memcg);
static void mem_cgroup_oom_notify(struct mem_cgroup *memcg);

/*
 * The memory controller data structure. The memory controller controls both
 * page cache and RSS per cgroup. We would eventually like to provide
 * statistics based on the statistics developed by Rik Van Riel for clock-pro,
 * to help the administrator determine what knobs to tune.
 *
 * TODO: Add a water mark for the memory controller. Reclaim will begin when
 * we hit the water mark. May be even add a low water mark, such that
 * no reclaim occurs from a cgroup at it's low water mark, this is
 * a feature that will be implemented much later in the future.
 */
struct mem_cgroup {
	struct cgroup_subsys_state css;
	/*
	 * the counter to account for memory usage
	 */
	struct res_counter res;

	/* vmpressure notifications */
	struct vmpressure vmpressure;

	/*
	 * the counter to account for mem+swap usage.
	 */
	struct res_counter memsw;

	/*
	 * the counter to account for kernel memory usage.
	 */
	struct res_counter kmem;
	/*
	 * Should the accounting and control be hierarchical, per subtree?
	 */
	bool use_hierarchy;
	unsigned long kmem_account_flags; /* See KMEM_ACCOUNTED_*, below */

	bool		oom_lock;
	atomic_t	under_oom;
	atomic_t	oom_wakeups;

	int	swappiness;
	/* OOM-Killer disable */
	int		oom_kill_disable;

	/* set when res.limit == memsw.limit */
	bool		memsw_is_minimum;

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
	/*
	 * set > 0 if pages under this cgroup are moving to other cgroup.
	 */
	atomic_t	moving_account;
	/* taken only while moving_account > 0 */
	spinlock_t	move_lock;
	/*
	 * percpu counter.
	 */
	struct mem_cgroup_stat_cpu __percpu *stat;
	/*
	 * used when a cpu is offlined or other synchronizations
	 * See mem_cgroup_read_stat().
	 */
	struct mem_cgroup_stat_cpu nocpu_base;
	spinlock_t pcp_counter_lock;

	atomic_t	dead_count;
#if defined(CONFIG_MEMCG_KMEM) && defined(CONFIG_INET)
	struct cg_proto tcp_mem;
#endif
#if defined(CONFIG_MEMCG_KMEM)
	/* analogous to slab_common's slab_caches list, but per-memcg;
	 * protected by memcg_slab_mutex */
	struct list_head memcg_slab_caches;
        /* Index in the kmem_cache->memcg_params->memcg_caches array */
	int kmemcg_id;
#endif

	int last_scanned_node;
#if MAX_NUMNODES > 1
	nodemask_t	scan_nodes;
	atomic_t	numainfo_events;
	atomic_t	numainfo_updating;
#endif

	/* List of events which userspace want to receive */
	struct list_head event_list;
	spinlock_t event_list_lock;

	struct mem_cgroup_per_node *nodeinfo[0];
	/* WARNING: nodeinfo must be the last member here */
};

/* internal only representation about the status of kmem accounting. */
enum {
	KMEM_ACCOUNTED_ACTIVE, /* accounted by this cgroup itself */
	KMEM_ACCOUNTED_DEAD, /* dead memcg with pending kmem charges */
};

#ifdef CONFIG_MEMCG_KMEM
static inline void memcg_kmem_set_active(struct mem_cgroup *memcg)
{
	set_bit(KMEM_ACCOUNTED_ACTIVE, &memcg->kmem_account_flags);
}

static bool memcg_kmem_is_active(struct mem_cgroup *memcg)
{
	return test_bit(KMEM_ACCOUNTED_ACTIVE, &memcg->kmem_account_flags);
}

static void memcg_kmem_mark_dead(struct mem_cgroup *memcg)
{
	/*
	 * Our caller must use css_get() first, because memcg_uncharge_kmem()
	 * will call css_put() if it sees the memcg is dead.
	 */
	smp_wmb();
	if (test_bit(KMEM_ACCOUNTED_ACTIVE, &memcg->kmem_account_flags))
		set_bit(KMEM_ACCOUNTED_DEAD, &memcg->kmem_account_flags);
}

static bool memcg_kmem_test_and_clear_dead(struct mem_cgroup *memcg)
{
	return test_and_clear_bit(KMEM_ACCOUNTED_DEAD,
				  &memcg->kmem_account_flags);
}
#endif

/* Stuffs for move charges at task migration. */
/*
 * Types of charges to be moved. "move_charge_at_immitgrate" and
 * "immigrate_flags" are treated as a left-shifted bitmap of these types.
 */
enum move_type {
	MOVE_CHARGE_TYPE_ANON,	/* private anonymous page and swap of it */
	MOVE_CHARGE_TYPE_FILE,	/* file page(including tmpfs) and swap of it */
	NR_MOVE_TYPE,
};

/* "mc" and its members are protected by cgroup_mutex */
static struct move_charge_struct {
	spinlock_t	  lock; /* for from, to */
	struct mem_cgroup *from;
	struct mem_cgroup *to;
	unsigned long immigrate_flags;
	unsigned long precharge;
	unsigned long moved_charge;
	unsigned long moved_swap;
	struct task_struct *moving_task;	/* a task moving charges */
	wait_queue_head_t waitq;		/* a waitq for other context */
} mc = {
	.lock = __SPIN_LOCK_UNLOCKED(mc.lock),
	.waitq = __WAIT_QUEUE_HEAD_INITIALIZER(mc.waitq),
};

static bool move_anon(void)
{
	return test_bit(MOVE_CHARGE_TYPE_ANON, &mc.immigrate_flags);
}

static bool move_file(void)
{
	return test_bit(MOVE_CHARGE_TYPE_FILE, &mc.immigrate_flags);
}

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
};

#define MEMFILE_PRIVATE(x, val)	((x) << 16 | (val))
#define MEMFILE_TYPE(val)	((val) >> 16 & 0xffff)
#define MEMFILE_ATTR(val)	((val) & 0xffff)
/* Used for OOM nofiier */
#define OOM_CONTROL		(0)

/*
 * Reclaim flags for mem_cgroup_hierarchical_reclaim
 */
#define MEM_CGROUP_RECLAIM_NOSWAP_BIT	0x0
#define MEM_CGROUP_RECLAIM_NOSWAP	(1 << MEM_CGROUP_RECLAIM_NOSWAP_BIT)
#define MEM_CGROUP_RECLAIM_SHRINK_BIT	0x1
#define MEM_CGROUP_RECLAIM_SHRINK	(1 << MEM_CGROUP_RECLAIM_SHRINK_BIT)

/*
 * The memcg_create_mutex will be held whenever a new cgroup is created.
 * As a consequence, any change that needs to protect against new child cgroups
 * appearing has to hold it as well.
 */
static DEFINE_MUTEX(memcg_create_mutex);

struct mem_cgroup *mem_cgroup_from_css(struct cgroup_subsys_state *s)
{
	return s ? container_of(s, struct mem_cgroup, css) : NULL;
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

static inline bool mem_cgroup_is_root(struct mem_cgroup *memcg)
{
	return (memcg == root_mem_cgroup);
}

/*
 * We restrict the id in the range of [1, 65535], so it can fit into
 * an unsigned short.
 */
#define MEM_CGROUP_ID_MAX	USHRT_MAX

static inline unsigned short mem_cgroup_id(struct mem_cgroup *memcg)
{
	return memcg->css.id;
}

static inline struct mem_cgroup *mem_cgroup_from_id(unsigned short id)
{
	struct cgroup_subsys_state *css;

	css = css_from_id(id, &memory_cgrp_subsys);
	return mem_cgroup_from_css(css);
}

/* Writing them here to avoid exposing memcg's inner layout */
#if defined(CONFIG_INET) && defined(CONFIG_MEMCG_KMEM)

void sock_update_memcg(struct sock *sk)
{
	if (mem_cgroup_sockets_enabled) {
		struct mem_cgroup *memcg;
		struct cg_proto *cg_proto;

		BUG_ON(!sk->sk_prot->proto_cgroup);

		/* Socket cloning can throw us here with sk_cgrp already
		 * filled. It won't however, necessarily happen from
		 * process context. So the test for root memcg given
		 * the current task's memcg won't help us in this case.
		 *
		 * Respecting the original socket's memcg is a better
		 * decision in this case.
		 */
		if (sk->sk_cgrp) {
			BUG_ON(mem_cgroup_is_root(sk->sk_cgrp->memcg));
			css_get(&sk->sk_cgrp->memcg->css);
			return;
		}

		rcu_read_lock();
		memcg = mem_cgroup_from_task(current);
		cg_proto = sk->sk_prot->proto_cgroup(memcg);
		if (!mem_cgroup_is_root(memcg) &&
		    memcg_proto_active(cg_proto) &&
		    css_tryget_online(&memcg->css)) {
			sk->sk_cgrp = cg_proto;
		}
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(sock_update_memcg);

void sock_release_memcg(struct sock *sk)
{
	if (mem_cgroup_sockets_enabled && sk->sk_cgrp) {
		struct mem_cgroup *memcg;
		WARN_ON(!sk->sk_cgrp->memcg);
		memcg = sk->sk_cgrp->memcg;
		css_put(&sk->sk_cgrp->memcg->css);
	}
}

struct cg_proto *tcp_proto_cgroup(struct mem_cgroup *memcg)
{
	if (!memcg || mem_cgroup_is_root(memcg))
		return NULL;

	return &memcg->tcp_mem;
}
EXPORT_SYMBOL(tcp_proto_cgroup);

static void disarm_sock_keys(struct mem_cgroup *memcg)
{
	if (!memcg_proto_activated(&memcg->tcp_mem))
		return;
	static_key_slow_dec(&memcg_socket_limit_enabled);
}
#else
static void disarm_sock_keys(struct mem_cgroup *memcg)
{
}
#endif

#ifdef CONFIG_MEMCG_KMEM
/*
 * This will be the memcg's index in each cache's ->memcg_params->memcg_caches.
 * The main reason for not using cgroup id for this:
 *  this works better in sparse environments, where we have a lot of memcgs,
 *  but only a few kmem-limited. Or also, if we have, for instance, 200
 *  memcgs, and none but the 200th is kmem-limited, we'd have to have a
 *  200 entry array for that.
 *
 * The current size of the caches array is stored in
 * memcg_limited_groups_array_size.  It will double each time we have to
 * increase it.
 */
static DEFINE_IDA(kmem_limited_groups);
int memcg_limited_groups_array_size;

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
struct static_key memcg_kmem_enabled_key;
EXPORT_SYMBOL(memcg_kmem_enabled_key);

static void disarm_kmem_keys(struct mem_cgroup *memcg)
{
	if (memcg_kmem_is_active(memcg)) {
		static_key_slow_dec(&memcg_kmem_enabled_key);
		ida_simple_remove(&kmem_limited_groups, memcg->kmemcg_id);
	}
	/*
	 * This check can't live in kmem destruction function,
	 * since the charges will outlive the cgroup
	 */
	WARN_ON(res_counter_read_u64(&memcg->kmem, RES_USAGE) != 0);
}
#else
static void disarm_kmem_keys(struct mem_cgroup *memcg)
{
}
#endif /* CONFIG_MEMCG_KMEM */

static void disarm_static_keys(struct mem_cgroup *memcg)
{
	disarm_sock_keys(memcg);
	disarm_kmem_keys(memcg);
}

static void drain_all_stock_async(struct mem_cgroup *memcg);

static struct mem_cgroup_per_zone *
mem_cgroup_zone_zoneinfo(struct mem_cgroup *memcg, struct zone *zone)
{
	int nid = zone_to_nid(zone);
	int zid = zone_idx(zone);

	return &memcg->nodeinfo[nid]->zoneinfo[zid];
}

struct cgroup_subsys_state *mem_cgroup_css(struct mem_cgroup *memcg)
{
	return &memcg->css;
}

static struct mem_cgroup_per_zone *
mem_cgroup_page_zoneinfo(struct mem_cgroup *memcg, struct page *page)
{
	int nid = page_to_nid(page);
	int zid = page_zonenum(page);

	return &memcg->nodeinfo[nid]->zoneinfo[zid];
}

static struct mem_cgroup_tree_per_zone *
soft_limit_tree_node_zone(int nid, int zid)
{
	return &soft_limit_tree.rb_tree_per_node[nid]->rb_tree_per_zone[zid];
}

static struct mem_cgroup_tree_per_zone *
soft_limit_tree_from_page(struct page *page)
{
	int nid = page_to_nid(page);
	int zid = page_zonenum(page);

	return &soft_limit_tree.rb_tree_per_node[nid]->rb_tree_per_zone[zid];
}

static void __mem_cgroup_insert_exceeded(struct mem_cgroup_per_zone *mz,
					 struct mem_cgroup_tree_per_zone *mctz,
					 unsigned long long new_usage_in_excess)
{
	struct rb_node **p = &mctz->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct mem_cgroup_per_zone *mz_node;

	if (mz->on_tree)
		return;

	mz->usage_in_excess = new_usage_in_excess;
	if (!mz->usage_in_excess)
		return;
	while (*p) {
		parent = *p;
		mz_node = rb_entry(parent, struct mem_cgroup_per_zone,
					tree_node);
		if (mz->usage_in_excess < mz_node->usage_in_excess)
			p = &(*p)->rb_left;
		/*
		 * We can't avoid mem cgroups that are over their soft
		 * limit by the same amount
		 */
		else if (mz->usage_in_excess >= mz_node->usage_in_excess)
			p = &(*p)->rb_right;
	}
	rb_link_node(&mz->tree_node, parent, p);
	rb_insert_color(&mz->tree_node, &mctz->rb_root);
	mz->on_tree = true;
}

static void __mem_cgroup_remove_exceeded(struct mem_cgroup_per_zone *mz,
					 struct mem_cgroup_tree_per_zone *mctz)
{
	if (!mz->on_tree)
		return;
	rb_erase(&mz->tree_node, &mctz->rb_root);
	mz->on_tree = false;
}

static void mem_cgroup_remove_exceeded(struct mem_cgroup_per_zone *mz,
				       struct mem_cgroup_tree_per_zone *mctz)
{
	unsigned long flags;

	spin_lock_irqsave(&mctz->lock, flags);
	__mem_cgroup_remove_exceeded(mz, mctz);
	spin_unlock_irqrestore(&mctz->lock, flags);
}


static void mem_cgroup_update_tree(struct mem_cgroup *memcg, struct page *page)
{
	unsigned long long excess;
	struct mem_cgroup_per_zone *mz;
	struct mem_cgroup_tree_per_zone *mctz;

	mctz = soft_limit_tree_from_page(page);
	/*
	 * Necessary to update all ancestors when hierarchy is used.
	 * because their event counter is not touched.
	 */
	for (; memcg; memcg = parent_mem_cgroup(memcg)) {
		mz = mem_cgroup_page_zoneinfo(memcg, page);
		excess = res_counter_soft_limit_excess(&memcg->res);
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
	struct mem_cgroup_tree_per_zone *mctz;
	struct mem_cgroup_per_zone *mz;
	int nid, zid;

	for_each_node(nid) {
		for (zid = 0; zid < MAX_NR_ZONES; zid++) {
			mz = &memcg->nodeinfo[nid]->zoneinfo[zid];
			mctz = soft_limit_tree_node_zone(nid, zid);
			mem_cgroup_remove_exceeded(mz, mctz);
		}
	}
}

static struct mem_cgroup_per_zone *
__mem_cgroup_largest_soft_limit_node(struct mem_cgroup_tree_per_zone *mctz)
{
	struct rb_node *rightmost = NULL;
	struct mem_cgroup_per_zone *mz;

retry:
	mz = NULL;
	rightmost = rb_last(&mctz->rb_root);
	if (!rightmost)
		goto done;		/* Nothing to reclaim from */

	mz = rb_entry(rightmost, struct mem_cgroup_per_zone, tree_node);
	/*
	 * Remove the node now but someone else can add it back,
	 * we will to add it back at the end of reclaim to its correct
	 * position in the tree.
	 */
	__mem_cgroup_remove_exceeded(mz, mctz);
	if (!res_counter_soft_limit_excess(&mz->memcg->res) ||
	    !css_tryget_online(&mz->memcg->css))
		goto retry;
done:
	return mz;
}

static struct mem_cgroup_per_zone *
mem_cgroup_largest_soft_limit_node(struct mem_cgroup_tree_per_zone *mctz)
{
	struct mem_cgroup_per_zone *mz;

	spin_lock_irq(&mctz->lock);
	mz = __mem_cgroup_largest_soft_limit_node(mctz);
	spin_unlock_irq(&mctz->lock);
	return mz;
}

/*
 * Implementation Note: reading percpu statistics for memcg.
 *
 * Both of vmstat[] and percpu_counter has threshold and do periodic
 * synchronization to implement "quick" read. There are trade-off between
 * reading cost and precision of value. Then, we may have a chance to implement
 * a periodic synchronizion of counter in memcg's counter.
 *
 * But this _read() function is used for user interface now. The user accounts
 * memory usage by memory cgroup and he _always_ requires exact value because
 * he accounts memory. Even if we provide quick-and-fuzzy read, we always
 * have to visit all online cpus and make sum. So, for now, unnecessary
 * synchronization is not implemented. (just implemented for cpu hotplug)
 *
 * If there are kernel internal actions which can make use of some not-exact
 * value, and reading all cpu value can be performance bottleneck in some
 * common workload, threashold and synchonization as vmstat[] should be
 * implemented.
 */
static long mem_cgroup_read_stat(struct mem_cgroup *memcg,
				 enum mem_cgroup_stat_index idx)
{
	long val = 0;
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		val += per_cpu(memcg->stat->count[idx], cpu);
#ifdef CONFIG_HOTPLUG_CPU
	spin_lock(&memcg->pcp_counter_lock);
	val += memcg->nocpu_base.count[idx];
	spin_unlock(&memcg->pcp_counter_lock);
#endif
	put_online_cpus();
	return val;
}

static unsigned long mem_cgroup_read_events(struct mem_cgroup *memcg,
					    enum mem_cgroup_events_index idx)
{
	unsigned long val = 0;
	int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu)
		val += per_cpu(memcg->stat->events[idx], cpu);
#ifdef CONFIG_HOTPLUG_CPU
	spin_lock(&memcg->pcp_counter_lock);
	val += memcg->nocpu_base.events[idx];
	spin_unlock(&memcg->pcp_counter_lock);
#endif
	put_online_cpus();
	return val;
}

static void mem_cgroup_charge_statistics(struct mem_cgroup *memcg,
					 struct page *page,
					 int nr_pages)
{
	/*
	 * Here, RSS means 'mapped anon' and anon's SwapCache. Shmem/tmpfs is
	 * counted as CACHE even if it's on ANON LRU.
	 */
	if (PageAnon(page))
		__this_cpu_add(memcg->stat->count[MEM_CGROUP_STAT_RSS],
				nr_pages);
	else
		__this_cpu_add(memcg->stat->count[MEM_CGROUP_STAT_CACHE],
				nr_pages);

	if (PageTransHuge(page))
		__this_cpu_add(memcg->stat->count[MEM_CGROUP_STAT_RSS_HUGE],
				nr_pages);

	/* pagein of a big page is an event. So, ignore page size */
	if (nr_pages > 0)
		__this_cpu_inc(memcg->stat->events[MEM_CGROUP_EVENTS_PGPGIN]);
	else {
		__this_cpu_inc(memcg->stat->events[MEM_CGROUP_EVENTS_PGPGOUT]);
		nr_pages = -nr_pages; /* for event */
	}

	__this_cpu_add(memcg->stat->nr_page_events, nr_pages);
}

unsigned long mem_cgroup_get_lru_size(struct lruvec *lruvec, enum lru_list lru)
{
	struct mem_cgroup_per_zone *mz;

	mz = container_of(lruvec, struct mem_cgroup_per_zone, lruvec);
	return mz->lru_size[lru];
}

static unsigned long mem_cgroup_node_nr_lru_pages(struct mem_cgroup *memcg,
						  int nid,
						  unsigned int lru_mask)
{
	unsigned long nr = 0;
	int zid;

	VM_BUG_ON((unsigned)nid >= nr_node_ids);

	for (zid = 0; zid < MAX_NR_ZONES; zid++) {
		struct mem_cgroup_per_zone *mz;
		enum lru_list lru;

		for_each_lru(lru) {
			if (!(BIT(lru) & lru_mask))
				continue;
			mz = &memcg->nodeinfo[nid]->zoneinfo[zid];
			nr += mz->lru_size[lru];
		}
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
	if ((long)next - (long)val < 0) {
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

/*
 * Returns a next (in a pre-order walk) alive memcg (with elevated css
 * ref. count) or NULL if the whole root's subtree has been visited.
 *
 * helper function to be used by mem_cgroup_iter
 */
static struct mem_cgroup *__mem_cgroup_iter_next(struct mem_cgroup *root,
		struct mem_cgroup *last_visited)
{
	struct cgroup_subsys_state *prev_css, *next_css;

	prev_css = last_visited ? &last_visited->css : NULL;
skip_node:
	next_css = css_next_descendant_pre(prev_css, &root->css);

	/*
	 * Even if we found a group we have to make sure it is
	 * alive. css && !memcg means that the groups should be
	 * skipped and we should continue the tree walk.
	 * last_visited css is safe to use because it is
	 * protected by css_get and the tree walk is rcu safe.
	 *
	 * We do not take a reference on the root of the tree walk
	 * because we might race with the root removal when it would
	 * be the only node in the iterated hierarchy and mem_cgroup_iter
	 * would end up in an endless loop because it expects that at
	 * least one valid node will be returned. Root cannot disappear
	 * because caller of the iterator should hold it already so
	 * skipping css reference should be safe.
	 */
	if (next_css) {
		if ((next_css == &root->css) ||
		    ((next_css->flags & CSS_ONLINE) &&
		     css_tryget_online(next_css)))
			return mem_cgroup_from_css(next_css);

		prev_css = next_css;
		goto skip_node;
	}

	return NULL;
}

static void mem_cgroup_iter_invalidate(struct mem_cgroup *root)
{
	/*
	 * When a group in the hierarchy below root is destroyed, the
	 * hierarchy iterator can no longer be trusted since it might
	 * have pointed to the destroyed group.  Invalidate it.
	 */
	atomic_inc(&root->dead_count);
}

static struct mem_cgroup *
mem_cgroup_iter_load(struct mem_cgroup_reclaim_iter *iter,
		     struct mem_cgroup *root,
		     int *sequence)
{
	struct mem_cgroup *position = NULL;
	/*
	 * A cgroup destruction happens in two stages: offlining and
	 * release.  They are separated by a RCU grace period.
	 *
	 * If the iterator is valid, we may still race with an
	 * offlining.  The RCU lock ensures the object won't be
	 * released, tryget will fail if we lost the race.
	 */
	*sequence = atomic_read(&root->dead_count);
	if (iter->last_dead_count == *sequence) {
		smp_rmb();
		position = iter->last_visited;

		/*
		 * We cannot take a reference to root because we might race
		 * with root removal and returning NULL would end up in
		 * an endless loop on the iterator user level when root
		 * would be returned all the time.
		 */
		if (position && position != root &&
		    !css_tryget_online(&position->css))
			position = NULL;
	}
	return position;
}

static void mem_cgroup_iter_update(struct mem_cgroup_reclaim_iter *iter,
				   struct mem_cgroup *last_visited,
				   struct mem_cgroup *new_position,
				   struct mem_cgroup *root,
				   int sequence)
{
	/* root reference counting symmetric to mem_cgroup_iter_load */
	if (last_visited && last_visited != root)
		css_put(&last_visited->css);
	/*
	 * We store the sequence count from the time @last_visited was
	 * loaded successfully instead of rereading it here so that we
	 * don't lose destruction events in between.  We could have
	 * raced with the destruction of @new_position after all.
	 */
	iter->last_visited = new_position;
	smp_wmb();
	iter->last_dead_count = sequence;
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
	struct mem_cgroup *memcg = NULL;
	struct mem_cgroup *last_visited = NULL;

	if (mem_cgroup_disabled())
		return NULL;

	if (!root)
		root = root_mem_cgroup;

	if (prev && !reclaim)
		last_visited = prev;

	if (!root->use_hierarchy && root != root_mem_cgroup) {
		if (prev)
			goto out_css_put;
		return root;
	}

	rcu_read_lock();
	while (!memcg) {
		struct mem_cgroup_reclaim_iter *uninitialized_var(iter);
		int uninitialized_var(seq);

		if (reclaim) {
			struct mem_cgroup_per_zone *mz;

			mz = mem_cgroup_zone_zoneinfo(root, reclaim->zone);
			iter = &mz->reclaim_iter[reclaim->priority];
			if (prev && reclaim->generation != iter->generation) {
				iter->last_visited = NULL;
				goto out_unlock;
			}

			last_visited = mem_cgroup_iter_load(iter, root, &seq);
		}

		memcg = __mem_cgroup_iter_next(root, last_visited);

		if (reclaim) {
			mem_cgroup_iter_update(iter, last_visited, memcg, root,
					seq);

			if (!memcg)
				iter->generation++;
			else if (!prev && memcg)
				reclaim->generation = iter->generation;
		}

		if (prev && !memcg)
			goto out_unlock;
	}
out_unlock:
	rcu_read_unlock();
out_css_put:
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

void __mem_cgroup_count_vm_event(struct mm_struct *mm, enum vm_event_item idx)
{
	struct mem_cgroup *memcg;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (unlikely(!memcg))
		goto out;

	switch (idx) {
	case PGFAULT:
		this_cpu_inc(memcg->stat->events[MEM_CGROUP_EVENTS_PGFAULT]);
		break;
	case PGMAJFAULT:
		this_cpu_inc(memcg->stat->events[MEM_CGROUP_EVENTS_PGMAJFAULT]);
		break;
	default:
		BUG();
	}
out:
	rcu_read_unlock();
}
EXPORT_SYMBOL(__mem_cgroup_count_vm_event);

/**
 * mem_cgroup_zone_lruvec - get the lru list vector for a zone and memcg
 * @zone: zone of the wanted lruvec
 * @memcg: memcg of the wanted lruvec
 *
 * Returns the lru list vector holding pages for the given @zone and
 * @mem.  This can be the global zone lruvec, if the memory controller
 * is disabled.
 */
struct lruvec *mem_cgroup_zone_lruvec(struct zone *zone,
				      struct mem_cgroup *memcg)
{
	struct mem_cgroup_per_zone *mz;
	struct lruvec *lruvec;

	if (mem_cgroup_disabled()) {
		lruvec = &zone->lruvec;
		goto out;
	}

	mz = mem_cgroup_zone_zoneinfo(memcg, zone);
	lruvec = &mz->lruvec;
out:
	/*
	 * Since a node can be onlined after the mem_cgroup was created,
	 * we have to be prepared to initialize lruvec->zone here;
	 * and if offlined then reonlined, we need to reinitialize it.
	 */
	if (unlikely(lruvec->zone != zone))
		lruvec->zone = zone;
	return lruvec;
}

/**
 * mem_cgroup_page_lruvec - return lruvec for adding an lru page
 * @page: the page
 * @zone: zone of the page
 */
struct lruvec *mem_cgroup_page_lruvec(struct page *page, struct zone *zone)
{
	struct mem_cgroup_per_zone *mz;
	struct mem_cgroup *memcg;
	struct page_cgroup *pc;
	struct lruvec *lruvec;

	if (mem_cgroup_disabled()) {
		lruvec = &zone->lruvec;
		goto out;
	}

	pc = lookup_page_cgroup(page);
	memcg = pc->mem_cgroup;

	/*
	 * Surreptitiously switch any uncharged offlist page to root:
	 * an uncharged page off lru does nothing to secure
	 * its former mem_cgroup from sudden removal.
	 *
	 * Our caller holds lru_lock, and PageCgroupUsed is updated
	 * under page_cgroup lock: between them, they make all uses
	 * of pc->mem_cgroup safe.
	 */
	if (!PageLRU(page) && !PageCgroupUsed(pc) && memcg != root_mem_cgroup)
		pc->mem_cgroup = memcg = root_mem_cgroup;

	mz = mem_cgroup_page_zoneinfo(memcg, page);
	lruvec = &mz->lruvec;
out:
	/*
	 * Since a node can be onlined after the mem_cgroup was created,
	 * we have to be prepared to initialize lruvec->zone here;
	 * and if offlined then reonlined, we need to reinitialize it.
	 */
	if (unlikely(lruvec->zone != zone))
		lruvec->zone = zone;
	return lruvec;
}

/**
 * mem_cgroup_update_lru_size - account for adding or removing an lru page
 * @lruvec: mem_cgroup per zone lru vector
 * @lru: index of lru list the page is sitting on
 * @nr_pages: positive when adding or negative when removing
 *
 * This function must be called when a page is added to or removed from an
 * lru list.
 */
void mem_cgroup_update_lru_size(struct lruvec *lruvec, enum lru_list lru,
				int nr_pages)
{
	struct mem_cgroup_per_zone *mz;
	unsigned long *lru_size;

	if (mem_cgroup_disabled())
		return;

	mz = container_of(lruvec, struct mem_cgroup_per_zone, lruvec);
	lru_size = mz->lru_size + lru;
	*lru_size += nr_pages;
	VM_BUG_ON((long)(*lru_size) < 0);
}

/*
 * Checks whether given mem is same or in the root_mem_cgroup's
 * hierarchy subtree
 */
bool __mem_cgroup_same_or_subtree(const struct mem_cgroup *root_memcg,
				  struct mem_cgroup *memcg)
{
	if (root_memcg == memcg)
		return true;
	if (!root_memcg->use_hierarchy || !memcg)
		return false;
	return cgroup_is_descendant(memcg->css.cgroup, root_memcg->css.cgroup);
}

static bool mem_cgroup_same_or_subtree(const struct mem_cgroup *root_memcg,
				       struct mem_cgroup *memcg)
{
	bool ret;

	rcu_read_lock();
	ret = __mem_cgroup_same_or_subtree(root_memcg, memcg);
	rcu_read_unlock();
	return ret;
}

bool task_in_mem_cgroup(struct task_struct *task,
			const struct mem_cgroup *memcg)
{
	struct mem_cgroup *curr = NULL;
	struct task_struct *p;
	bool ret;

	p = find_lock_task_mm(task);
	if (p) {
		curr = get_mem_cgroup_from_mm(p->mm);
		task_unlock(p);
	} else {
		/*
		 * All threads may have already detached their mm's, but the oom
		 * killer still needs to detect if they have already been oom
		 * killed to prevent needlessly killing additional tasks.
		 */
		rcu_read_lock();
		curr = mem_cgroup_from_task(task);
		if (curr)
			css_get(&curr->css);
		rcu_read_unlock();
	}
	/*
	 * We should check use_hierarchy of "memcg" not "curr". Because checking
	 * use_hierarchy of "curr" here make this function true if hierarchy is
	 * enabled in "curr" and "curr" is a child of "memcg" in *cgroup*
	 * hierarchy(even if use_hierarchy is disabled in "memcg").
	 */
	ret = mem_cgroup_same_or_subtree(memcg, curr);
	css_put(&curr->css);
	return ret;
}

int mem_cgroup_inactive_anon_is_low(struct lruvec *lruvec)
{
	unsigned long inactive_ratio;
	unsigned long inactive;
	unsigned long active;
	unsigned long gb;

	inactive = mem_cgroup_get_lru_size(lruvec, LRU_INACTIVE_ANON);
	active = mem_cgroup_get_lru_size(lruvec, LRU_ACTIVE_ANON);

	gb = (inactive + active) >> (30 - PAGE_SHIFT);
	if (gb)
		inactive_ratio = int_sqrt(10 * gb);
	else
		inactive_ratio = 1;

	return inactive * inactive_ratio < active;
}

#define mem_cgroup_from_res_counter(counter, member)	\
	container_of(counter, struct mem_cgroup, member)

/**
 * mem_cgroup_margin - calculate chargeable space of a memory cgroup
 * @memcg: the memory cgroup
 *
 * Returns the maximum amount of memory @mem can be charged with, in
 * pages.
 */
static unsigned long mem_cgroup_margin(struct mem_cgroup *memcg)
{
	unsigned long long margin;

	margin = res_counter_margin(&memcg->res);
	if (do_swap_account)
		margin = min(margin, res_counter_margin(&memcg->memsw));
	return margin >> PAGE_SHIFT;
}

int mem_cgroup_swappiness(struct mem_cgroup *memcg)
{
	/* root ? */
	if (mem_cgroup_disabled() || !memcg->css.parent)
		return vm_swappiness;

	return memcg->swappiness;
}

/*
 * memcg->moving_account is used for checking possibility that some thread is
 * calling move_account(). When a thread on CPU-A starts moving pages under
 * a memcg, other threads should check memcg->moving_account under
 * rcu_read_lock(), like this:
 *
 *         CPU-A                                    CPU-B
 *                                              rcu_read_lock()
 *         memcg->moving_account+1              if (memcg->mocing_account)
 *                                                   take heavy locks.
 *         synchronize_rcu()                    update something.
 *                                              rcu_read_unlock()
 *         start move here.
 */

/* for quick checking without looking up memcg */
atomic_t memcg_moving __read_mostly;

static void mem_cgroup_start_move(struct mem_cgroup *memcg)
{
	atomic_inc(&memcg_moving);
	atomic_inc(&memcg->moving_account);
	synchronize_rcu();
}

static void mem_cgroup_end_move(struct mem_cgroup *memcg)
{
	/*
	 * Now, mem_cgroup_clear_mc() may call this function with NULL.
	 * We check NULL in callee rather than caller.
	 */
	if (memcg) {
		atomic_dec(&memcg_moving);
		atomic_dec(&memcg->moving_account);
	}
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

	ret = mem_cgroup_same_or_subtree(memcg, from)
		|| mem_cgroup_same_or_subtree(memcg, to);
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

/*
 * Take this lock when
 * - a code tries to modify page's memcg while it's USED.
 * - a code tries to modify page state accounting in a memcg.
 */
static void move_lock_mem_cgroup(struct mem_cgroup *memcg,
				  unsigned long *flags)
{
	spin_lock_irqsave(&memcg->move_lock, *flags);
}

static void move_unlock_mem_cgroup(struct mem_cgroup *memcg,
				unsigned long *flags)
{
	spin_unlock_irqrestore(&memcg->move_lock, *flags);
}

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
	/* oom_info_lock ensures that parallel ooms do not interleave */
	static DEFINE_MUTEX(oom_info_lock);
	struct mem_cgroup *iter;
	unsigned int i;

	if (!p)
		return;

	mutex_lock(&oom_info_lock);
	rcu_read_lock();

	pr_info("Task in ");
	pr_cont_cgroup_path(task_cgroup(p, memory_cgrp_id));
	pr_info(" killed as a result of limit of ");
	pr_cont_cgroup_path(memcg->css.cgroup);
	pr_info("\n");

	rcu_read_unlock();

	pr_info("memory: usage %llukB, limit %llukB, failcnt %llu\n",
		res_counter_read_u64(&memcg->res, RES_USAGE) >> 10,
		res_counter_read_u64(&memcg->res, RES_LIMIT) >> 10,
		res_counter_read_u64(&memcg->res, RES_FAILCNT));
	pr_info("memory+swap: usage %llukB, limit %llukB, failcnt %llu\n",
		res_counter_read_u64(&memcg->memsw, RES_USAGE) >> 10,
		res_counter_read_u64(&memcg->memsw, RES_LIMIT) >> 10,
		res_counter_read_u64(&memcg->memsw, RES_FAILCNT));
	pr_info("kmem: usage %llukB, limit %llukB, failcnt %llu\n",
		res_counter_read_u64(&memcg->kmem, RES_USAGE) >> 10,
		res_counter_read_u64(&memcg->kmem, RES_LIMIT) >> 10,
		res_counter_read_u64(&memcg->kmem, RES_FAILCNT));

	for_each_mem_cgroup_tree(iter, memcg) {
		pr_info("Memory cgroup stats for ");
		pr_cont_cgroup_path(iter->css.cgroup);
		pr_cont(":");

		for (i = 0; i < MEM_CGROUP_STAT_NSTATS; i++) {
			if (i == MEM_CGROUP_STAT_SWAP && !do_swap_account)
				continue;
			pr_cont(" %s:%ldKB", mem_cgroup_stat_names[i],
				K(mem_cgroup_read_stat(iter, i)));
		}

		for (i = 0; i < NR_LRU_LISTS; i++)
			pr_cont(" %s:%luKB", mem_cgroup_lru_names[i],
				K(mem_cgroup_nr_lru_pages(iter, BIT(i))));

		pr_cont("\n");
	}
	mutex_unlock(&oom_info_lock);
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
static u64 mem_cgroup_get_limit(struct mem_cgroup *memcg)
{
	u64 limit;

	limit = res_counter_read_u64(&memcg->res, RES_LIMIT);

	/*
	 * Do not consider swap space if we cannot swap due to swappiness
	 */
	if (mem_cgroup_swappiness(memcg)) {
		u64 memsw;

		limit += total_swap_pages << PAGE_SHIFT;
		memsw = res_counter_read_u64(&memcg->memsw, RES_LIMIT);

		/*
		 * If memsw is finite and limits the amount of swap space
		 * available to this memcg, return that limit.
		 */
		limit = min(limit, memsw);
	}

	return limit;
}

static void mem_cgroup_out_of_memory(struct mem_cgroup *memcg, gfp_t gfp_mask,
				     int order)
{
	struct mem_cgroup *iter;
	unsigned long chosen_points = 0;
	unsigned long totalpages;
	unsigned int points = 0;
	struct task_struct *chosen = NULL;

	/*
	 * If current has a pending SIGKILL or is exiting, then automatically
	 * select it.  The goal is to allow it to allocate so that it may
	 * quickly exit and free its memory.
	 */
	if (fatal_signal_pending(current) || current->flags & PF_EXITING) {
		set_thread_flag(TIF_MEMDIE);
		return;
	}

	check_panic_on_oom(CONSTRAINT_MEMCG, gfp_mask, order, NULL);
	totalpages = mem_cgroup_get_limit(memcg) >> PAGE_SHIFT ? : 1;
	for_each_mem_cgroup_tree(iter, memcg) {
		struct css_task_iter it;
		struct task_struct *task;

		css_task_iter_start(&iter->css, &it);
		while ((task = css_task_iter_next(&it))) {
			switch (oom_scan_process_thread(task, totalpages, NULL,
							false)) {
			case OOM_SCAN_SELECT:
				if (chosen)
					put_task_struct(chosen);
				chosen = task;
				chosen_points = ULONG_MAX;
				get_task_struct(chosen);
				/* fall through */
			case OOM_SCAN_CONTINUE:
				continue;
			case OOM_SCAN_ABORT:
				css_task_iter_end(&it);
				mem_cgroup_iter_break(memcg, iter);
				if (chosen)
					put_task_struct(chosen);
				return;
			case OOM_SCAN_OK:
				break;
			};
			points = oom_badness(task, memcg, NULL, totalpages);
			if (!points || points < chosen_points)
				continue;
			/* Prefer thread group leaders for display purposes */
			if (points == chosen_points &&
			    thread_group_leader(chosen))
				continue;

			if (chosen)
				put_task_struct(chosen);
			chosen = task;
			chosen_points = points;
			get_task_struct(chosen);
		}
		css_task_iter_end(&it);
	}

	if (!chosen)
		return;
	points = chosen_points * 1000 / totalpages;
	oom_kill_process(chosen, gfp_mask, order, points, totalpages, memcg,
			 NULL, "Memory cgroup out of memory");
}

static unsigned long mem_cgroup_reclaim(struct mem_cgroup *memcg,
					gfp_t gfp_mask,
					unsigned long flags)
{
	unsigned long total = 0;
	bool noswap = false;
	int loop;

	if (flags & MEM_CGROUP_RECLAIM_NOSWAP)
		noswap = true;
	if (!(flags & MEM_CGROUP_RECLAIM_SHRINK) && memcg->memsw_is_minimum)
		noswap = true;

	for (loop = 0; loop < MEM_CGROUP_MAX_RECLAIM_LOOPS; loop++) {
		if (loop)
			drain_all_stock_async(memcg);
		total += try_to_free_mem_cgroup_pages(memcg, gfp_mask, noswap);
		/*
		 * Allow limit shrinkers, which are triggered directly
		 * by userspace, to catch signals and stop reclaim
		 * after minimal progress, regardless of the margin.
		 */
		if (total && (flags & MEM_CGROUP_RECLAIM_SHRINK))
			break;
		if (mem_cgroup_margin(memcg))
			break;
		/*
		 * If nothing was reclaimed after two attempts, there
		 * may be no reclaimable pages in this hierarchy.
		 */
		if (loop && !total)
			break;
	}
	return total;
}

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
#if MAX_NUMNODES > 1

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

	node = next_node(node, memcg->scan_nodes);
	if (node == MAX_NUMNODES)
		node = first_node(memcg->scan_nodes);
	/*
	 * We call this when we hit limit, not when pages are added to LRU.
	 * No LRU may hold pages because all pages are UNEVICTABLE or
	 * memcg is too small and all pages are not on LRU. In that case,
	 * we use curret node.
	 */
	if (unlikely(node == MAX_NUMNODES))
		node = numa_node_id();

	memcg->last_scanned_node = node;
	return node;
}

/*
 * Check all nodes whether it contains reclaimable pages or not.
 * For quick scan, we make use of scan_nodes. This will allow us to skip
 * unused nodes. But scan_nodes is lazily updated and may not cotain
 * enough new information. We need to do double check.
 */
static bool mem_cgroup_reclaimable(struct mem_cgroup *memcg, bool noswap)
{
	int nid;

	/*
	 * quick check...making use of scan_node.
	 * We can skip unused nodes.
	 */
	if (!nodes_empty(memcg->scan_nodes)) {
		for (nid = first_node(memcg->scan_nodes);
		     nid < MAX_NUMNODES;
		     nid = next_node(nid, memcg->scan_nodes)) {

			if (test_mem_cgroup_node_reclaimable(memcg, nid, noswap))
				return true;
		}
	}
	/*
	 * Check rest of nodes.
	 */
	for_each_node_state(nid, N_MEMORY) {
		if (node_isset(nid, memcg->scan_nodes))
			continue;
		if (test_mem_cgroup_node_reclaimable(memcg, nid, noswap))
			return true;
	}
	return false;
}

#else
int mem_cgroup_select_victim_node(struct mem_cgroup *memcg)
{
	return 0;
}

static bool mem_cgroup_reclaimable(struct mem_cgroup *memcg, bool noswap)
{
	return test_mem_cgroup_node_reclaimable(memcg, 0, noswap);
}
#endif

static int mem_cgroup_soft_reclaim(struct mem_cgroup *root_memcg,
				   struct zone *zone,
				   gfp_t gfp_mask,
				   unsigned long *total_scanned)
{
	struct mem_cgroup *victim = NULL;
	int total = 0;
	int loop = 0;
	unsigned long excess;
	unsigned long nr_scanned;
	struct mem_cgroup_reclaim_cookie reclaim = {
		.zone = zone,
		.priority = 0,
	};

	excess = res_counter_soft_limit_excess(&root_memcg->res) >> PAGE_SHIFT;

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
		if (!mem_cgroup_reclaimable(victim, false))
			continue;
		total += mem_cgroup_shrink_node_zone(victim, gfp_mask, false,
						     zone, &nr_scanned);
		*total_scanned += nr_scanned;
		if (!res_counter_soft_limit_excess(&root_memcg->res))
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

	for_each_mem_cgroup_tree(iter, memcg)
		atomic_inc(&iter->under_oom);
}

static void mem_cgroup_unmark_under_oom(struct mem_cgroup *memcg)
{
	struct mem_cgroup *iter;

	/*
	 * When a new child is created while the hierarchy is under oom,
	 * mem_cgroup_oom_lock() may not be called. We have to use
	 * atomic_add_unless() here.
	 */
	for_each_mem_cgroup_tree(iter, memcg)
		atomic_add_unless(&iter->under_oom, -1, 0);
}

static DECLARE_WAIT_QUEUE_HEAD(memcg_oom_waitq);

struct oom_wait_info {
	struct mem_cgroup *memcg;
	wait_queue_t	wait;
};

static int memcg_oom_wake_function(wait_queue_t *wait,
	unsigned mode, int sync, void *arg)
{
	struct mem_cgroup *wake_memcg = (struct mem_cgroup *)arg;
	struct mem_cgroup *oom_wait_memcg;
	struct oom_wait_info *oom_wait_info;

	oom_wait_info = container_of(wait, struct oom_wait_info, wait);
	oom_wait_memcg = oom_wait_info->memcg;

	/*
	 * Both of oom_wait_info->memcg and wake_memcg are stable under us.
	 * Then we can use css_is_ancestor without taking care of RCU.
	 */
	if (!mem_cgroup_same_or_subtree(oom_wait_memcg, wake_memcg)
		&& !mem_cgroup_same_or_subtree(wake_memcg, oom_wait_memcg))
		return 0;
	return autoremove_wake_function(wait, mode, sync, arg);
}

static void memcg_wakeup_oom(struct mem_cgroup *memcg)
{
	atomic_inc(&memcg->oom_wakeups);
	/* for filtering, pass "memcg" as argument. */
	__wake_up(&memcg_oom_waitq, TASK_NORMAL, 0, memcg);
}

static void memcg_oom_recover(struct mem_cgroup *memcg)
{
	if (memcg && atomic_read(&memcg->under_oom))
		memcg_wakeup_oom(memcg);
}

static void mem_cgroup_oom(struct mem_cgroup *memcg, gfp_t mask, int order)
{
	if (!current->memcg_oom.may_oom)
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
	current->memcg_oom.memcg = memcg;
	current->memcg_oom.gfp_mask = mask;
	current->memcg_oom.order = order;
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
	struct mem_cgroup *memcg = current->memcg_oom.memcg;
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
	INIT_LIST_HEAD(&owait.wait.task_list);

	prepare_to_wait(&memcg_oom_waitq, &owait.wait, TASK_KILLABLE);
	mem_cgroup_mark_under_oom(memcg);

	locked = mem_cgroup_oom_trylock(memcg);

	if (locked)
		mem_cgroup_oom_notify(memcg);

	if (locked && !memcg->oom_kill_disable) {
		mem_cgroup_unmark_under_oom(memcg);
		finish_wait(&memcg_oom_waitq, &owait.wait);
		mem_cgroup_out_of_memory(memcg, current->memcg_oom.gfp_mask,
					 current->memcg_oom.order);
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
	current->memcg_oom.memcg = NULL;
	css_put(&memcg->css);
	return true;
}

/*
 * Used to update mapped file or writeback or other statistics.
 *
 * Notes: Race condition
 *
 * Charging occurs during page instantiation, while the page is
 * unmapped and locked in page migration, or while the page table is
 * locked in THP migration.  No race is possible.
 *
 * Uncharge happens to pages with zero references, no race possible.
 *
 * Charge moving between groups is protected by checking mm->moving
 * account and taking the move_lock in the slowpath.
 */

void __mem_cgroup_begin_update_page_stat(struct page *page,
				bool *locked, unsigned long *flags)
{
	struct mem_cgroup *memcg;
	struct page_cgroup *pc;

	pc = lookup_page_cgroup(page);
again:
	memcg = pc->mem_cgroup;
	if (unlikely(!memcg || !PageCgroupUsed(pc)))
		return;
	/*
	 * If this memory cgroup is not under account moving, we don't
	 * need to take move_lock_mem_cgroup(). Because we already hold
	 * rcu_read_lock(), any calls to move_account will be delayed until
	 * rcu_read_unlock().
	 */
	VM_BUG_ON(!rcu_read_lock_held());
	if (atomic_read(&memcg->moving_account) <= 0)
		return;

	move_lock_mem_cgroup(memcg, flags);
	if (memcg != pc->mem_cgroup || !PageCgroupUsed(pc)) {
		move_unlock_mem_cgroup(memcg, flags);
		goto again;
	}
	*locked = true;
}

void __mem_cgroup_end_update_page_stat(struct page *page, unsigned long *flags)
{
	struct page_cgroup *pc = lookup_page_cgroup(page);

	/*
	 * It's guaranteed that pc->mem_cgroup never changes while
	 * lock is held because a routine modifies pc->mem_cgroup
	 * should take move_lock_mem_cgroup().
	 */
	move_unlock_mem_cgroup(pc->mem_cgroup, flags);
}

void mem_cgroup_update_page_stat(struct page *page,
				 enum mem_cgroup_stat_index idx, int val)
{
	struct mem_cgroup *memcg;
	struct page_cgroup *pc = lookup_page_cgroup(page);
	unsigned long uninitialized_var(flags);

	if (mem_cgroup_disabled())
		return;

	VM_BUG_ON(!rcu_read_lock_held());
	memcg = pc->mem_cgroup;
	if (unlikely(!memcg || !PageCgroupUsed(pc)))
		return;

	this_cpu_add(memcg->stat->count[idx], val);
}

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
	bool ret = true;

	if (nr_pages > CHARGE_BATCH)
		return false;

	stock = &get_cpu_var(memcg_stock);
	if (memcg == stock->cached && stock->nr_pages >= nr_pages)
		stock->nr_pages -= nr_pages;
	else /* need to call res_counter_charge */
		ret = false;
	put_cpu_var(memcg_stock);
	return ret;
}

/*
 * Returns stocks cached in percpu to res_counter and reset cached information.
 */
static void drain_stock(struct memcg_stock_pcp *stock)
{
	struct mem_cgroup *old = stock->cached;

	if (stock->nr_pages) {
		unsigned long bytes = stock->nr_pages * PAGE_SIZE;

		res_counter_uncharge(&old->res, bytes);
		if (do_swap_account)
			res_counter_uncharge(&old->memsw, bytes);
		stock->nr_pages = 0;
	}
	stock->cached = NULL;
}

/*
 * This must be called under preempt disabled or must be called by
 * a thread which is pinned to local cpu.
 */
static void drain_local_stock(struct work_struct *dummy)
{
	struct memcg_stock_pcp *stock = this_cpu_ptr(&memcg_stock);
	drain_stock(stock);
	clear_bit(FLUSHING_CACHED_CHARGE, &stock->flags);
}

static void __init memcg_stock_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct memcg_stock_pcp *stock =
					&per_cpu(memcg_stock, cpu);
		INIT_WORK(&stock->work, drain_local_stock);
	}
}

/*
 * Cache charges(val) which is from res_counter, to local per_cpu area.
 * This will be consumed by consume_stock() function, later.
 */
static void refill_stock(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	struct memcg_stock_pcp *stock = &get_cpu_var(memcg_stock);

	if (stock->cached != memcg) { /* reset if necessary */
		drain_stock(stock);
		stock->cached = memcg;
	}
	stock->nr_pages += nr_pages;
	put_cpu_var(memcg_stock);
}

/*
 * Drains all per-CPU charge caches for given root_memcg resp. subtree
 * of the hierarchy under it. sync flag says whether we should block
 * until the work is done.
 */
static void drain_all_stock(struct mem_cgroup *root_memcg, bool sync)
{
	int cpu, curcpu;

	/* Notify other cpus that system-wide "drain" is running */
	get_online_cpus();
	curcpu = get_cpu();
	for_each_online_cpu(cpu) {
		struct memcg_stock_pcp *stock = &per_cpu(memcg_stock, cpu);
		struct mem_cgroup *memcg;

		memcg = stock->cached;
		if (!memcg || !stock->nr_pages)
			continue;
		if (!mem_cgroup_same_or_subtree(root_memcg, memcg))
			continue;
		if (!test_and_set_bit(FLUSHING_CACHED_CHARGE, &stock->flags)) {
			if (cpu == curcpu)
				drain_local_stock(&stock->work);
			else
				schedule_work_on(cpu, &stock->work);
		}
	}
	put_cpu();

	if (!sync)
		goto out;

	for_each_online_cpu(cpu) {
		struct memcg_stock_pcp *stock = &per_cpu(memcg_stock, cpu);
		if (test_bit(FLUSHING_CACHED_CHARGE, &stock->flags))
			flush_work(&stock->work);
	}
out:
	put_online_cpus();
}

/*
 * Tries to drain stocked charges in other cpus. This function is asynchronous
 * and just put a work per cpu for draining localy on each cpu. Caller can
 * expects some charges will be back to res_counter later but cannot wait for
 * it.
 */
static void drain_all_stock_async(struct mem_cgroup *root_memcg)
{
	/*
	 * If someone calls draining, avoid adding more kworker runs.
	 */
	if (!mutex_trylock(&percpu_charge_mutex))
		return;
	drain_all_stock(root_memcg, false);
	mutex_unlock(&percpu_charge_mutex);
}

/* This is a synchronous drain interface. */
static void drain_all_stock_sync(struct mem_cgroup *root_memcg)
{
	/* called when force_empty is called */
	mutex_lock(&percpu_charge_mutex);
	drain_all_stock(root_memcg, true);
	mutex_unlock(&percpu_charge_mutex);
}

/*
 * This function drains percpu counter value from DEAD cpu and
 * move it to local cpu. Note that this function can be preempted.
 */
static void mem_cgroup_drain_pcp_counter(struct mem_cgroup *memcg, int cpu)
{
	int i;

	spin_lock(&memcg->pcp_counter_lock);
	for (i = 0; i < MEM_CGROUP_STAT_NSTATS; i++) {
		long x = per_cpu(memcg->stat->count[i], cpu);

		per_cpu(memcg->stat->count[i], cpu) = 0;
		memcg->nocpu_base.count[i] += x;
	}
	for (i = 0; i < MEM_CGROUP_EVENTS_NSTATS; i++) {
		unsigned long x = per_cpu(memcg->stat->events[i], cpu);

		per_cpu(memcg->stat->events[i], cpu) = 0;
		memcg->nocpu_base.events[i] += x;
	}
	spin_unlock(&memcg->pcp_counter_lock);
}

static int memcg_cpu_hotplug_callback(struct notifier_block *nb,
					unsigned long action,
					void *hcpu)
{
	int cpu = (unsigned long)hcpu;
	struct memcg_stock_pcp *stock;
	struct mem_cgroup *iter;

	if (action == CPU_ONLINE)
		return NOTIFY_OK;

	if (action != CPU_DEAD && action != CPU_DEAD_FROZEN)
		return NOTIFY_OK;

	for_each_mem_cgroup(iter)
		mem_cgroup_drain_pcp_counter(iter, cpu);

	stock = &per_cpu(memcg_stock, cpu);
	drain_stock(stock);
	return NOTIFY_OK;
}

static int try_charge(struct mem_cgroup *memcg, gfp_t gfp_mask,
		      unsigned int nr_pages)
{
	unsigned int batch = max(CHARGE_BATCH, nr_pages);
	int nr_retries = MEM_CGROUP_RECLAIM_RETRIES;
	struct mem_cgroup *mem_over_limit;
	struct res_counter *fail_res;
	unsigned long nr_reclaimed;
	unsigned long flags = 0;
	unsigned long long size;
	int ret = 0;

retry:
	if (consume_stock(memcg, nr_pages))
		goto done;

	size = batch * PAGE_SIZE;
	if (!res_counter_charge(&memcg->res, size, &fail_res)) {
		if (!do_swap_account)
			goto done_restock;
		if (!res_counter_charge(&memcg->memsw, size, &fail_res))
			goto done_restock;
		res_counter_uncharge(&memcg->res, size);
		mem_over_limit = mem_cgroup_from_res_counter(fail_res, memsw);
		flags |= MEM_CGROUP_RECLAIM_NOSWAP;
	} else
		mem_over_limit = mem_cgroup_from_res_counter(fail_res, res);

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
	if (unlikely(test_thread_flag(TIF_MEMDIE) ||
		     fatal_signal_pending(current) ||
		     current->flags & PF_EXITING))
		goto bypass;

	if (unlikely(task_in_memcg_oom(current)))
		goto nomem;

	if (!(gfp_mask & __GFP_WAIT))
		goto nomem;

	nr_reclaimed = mem_cgroup_reclaim(mem_over_limit, gfp_mask, flags);

	if (mem_cgroup_margin(mem_over_limit) >= nr_pages)
		goto retry;

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
		goto bypass;

	if (fatal_signal_pending(current))
		goto bypass;

	mem_cgroup_oom(mem_over_limit, gfp_mask, get_order(nr_pages));
nomem:
	if (!(gfp_mask & __GFP_NOFAIL))
		return -ENOMEM;
bypass:
	memcg = root_mem_cgroup;
	ret = -EINTR;
	goto retry;

done_restock:
	if (batch > nr_pages)
		refill_stock(memcg, batch - nr_pages);
done:
	return ret;
}

static void cancel_charge(struct mem_cgroup *memcg, unsigned int nr_pages)
{
	unsigned long bytes = nr_pages * PAGE_SIZE;

	res_counter_uncharge(&memcg->res, bytes);
	if (do_swap_account)
		res_counter_uncharge(&memcg->memsw, bytes);
}

/*
 * Cancel chrages in this cgroup....doesn't propagate to parent cgroup.
 * This is useful when moving usage to parent cgroup.
 */
static void __mem_cgroup_cancel_local_charge(struct mem_cgroup *memcg,
					unsigned int nr_pages)
{
	unsigned long bytes = nr_pages * PAGE_SIZE;

	res_counter_uncharge_until(&memcg->res, memcg->res.parent, bytes);
	if (do_swap_account)
		res_counter_uncharge_until(&memcg->memsw,
						memcg->memsw.parent, bytes);
}

/*
 * A helper function to get mem_cgroup from ID. must be called under
 * rcu_read_lock().  The caller is responsible for calling
 * css_tryget_online() if the mem_cgroup is used for charging. (dropping
 * refcnt from swap can be called against removed memcg.)
 */
static struct mem_cgroup *mem_cgroup_lookup(unsigned short id)
{
	/* ID 0 is unused ID */
	if (!id)
		return NULL;
	return mem_cgroup_from_id(id);
}

/*
 * try_get_mem_cgroup_from_page - look up page's memcg association
 * @page: the page
 *
 * Look up, get a css reference, and return the memcg that owns @page.
 *
 * The page must be locked to prevent racing with swap-in and page
 * cache charges.  If coming from an unlocked page table, the caller
 * must ensure the page is on the LRU or this can race with charging.
 */
struct mem_cgroup *try_get_mem_cgroup_from_page(struct page *page)
{
	struct mem_cgroup *memcg = NULL;
	struct page_cgroup *pc;
	unsigned short id;
	swp_entry_t ent;

	VM_BUG_ON_PAGE(!PageLocked(page), page);

	pc = lookup_page_cgroup(page);
	if (PageCgroupUsed(pc)) {
		memcg = pc->mem_cgroup;
		if (memcg && !css_tryget_online(&memcg->css))
			memcg = NULL;
	} else if (PageSwapCache(page)) {
		ent.val = page_private(page);
		id = lookup_swap_cgroup_id(ent);
		rcu_read_lock();
		memcg = mem_cgroup_lookup(id);
		if (memcg && !css_tryget_online(&memcg->css))
			memcg = NULL;
		rcu_read_unlock();
	}
	return memcg;
}

static void lock_page_lru(struct page *page, int *isolated)
{
	struct zone *zone = page_zone(page);

	spin_lock_irq(&zone->lru_lock);
	if (PageLRU(page)) {
		struct lruvec *lruvec;

		lruvec = mem_cgroup_page_lruvec(page, zone);
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

		lruvec = mem_cgroup_page_lruvec(page, zone);
		VM_BUG_ON_PAGE(PageLRU(page), page);
		SetPageLRU(page);
		add_page_to_lru_list(page, lruvec, page_lru(page));
	}
	spin_unlock_irq(&zone->lru_lock);
}

static void commit_charge(struct page *page, struct mem_cgroup *memcg,
			  unsigned int nr_pages, bool lrucare)
{
	struct page_cgroup *pc = lookup_page_cgroup(page);
	int isolated;

	VM_BUG_ON_PAGE(PageCgroupUsed(pc), page);
	/*
	 * we don't need page_cgroup_lock about tail pages, becase they are not
	 * accessed by any other context at this point.
	 */

	/*
	 * In some cases, SwapCache and FUSE(splice_buf->radixtree), the page
	 * may already be on some other mem_cgroup's LRU.  Take care of it.
	 */
	if (lrucare)
		lock_page_lru(page, &isolated);

	/*
	 * Nobody should be changing or seriously looking at
	 * pc->mem_cgroup and pc->flags at this point:
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
	pc->mem_cgroup = memcg;
	pc->flags = PCG_USED | PCG_MEM | (do_swap_account ? PCG_MEMSW : 0);

	if (lrucare)
		unlock_page_lru(page, isolated);

	local_irq_disable();
	mem_cgroup_charge_statistics(memcg, page, nr_pages);
	/*
	 * "charge_statistics" updated event counter. Then, check it.
	 * Insert ancestor (and ancestor's ancestors), to softlimit RB-tree.
	 * if they exceeds softlimit.
	 */
	memcg_check_events(memcg, page);
	local_irq_enable();
}

static DEFINE_MUTEX(set_limit_mutex);

#ifdef CONFIG_MEMCG_KMEM
/*
 * The memcg_slab_mutex is held whenever a per memcg kmem cache is created or
 * destroyed. It protects memcg_caches arrays and memcg_slab_caches lists.
 */
static DEFINE_MUTEX(memcg_slab_mutex);

static DEFINE_MUTEX(activate_kmem_mutex);

static inline bool memcg_can_account_kmem(struct mem_cgroup *memcg)
{
	return !mem_cgroup_disabled() && !mem_cgroup_is_root(memcg) &&
		memcg_kmem_is_active(memcg);
}

/*
 * This is a bit cumbersome, but it is rarely used and avoids a backpointer
 * in the memcg_cache_params struct.
 */
static struct kmem_cache *memcg_params_to_cache(struct memcg_cache_params *p)
{
	struct kmem_cache *cachep;

	VM_BUG_ON(p->is_root_cache);
	cachep = p->root_cache;
	return cache_from_memcg_idx(cachep, memcg_cache_id(p->memcg));
}

#ifdef CONFIG_SLABINFO
static int mem_cgroup_slabinfo_read(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	struct memcg_cache_params *params;

	if (!memcg_can_account_kmem(memcg))
		return -EIO;

	print_slabinfo_header(m);

	mutex_lock(&memcg_slab_mutex);
	list_for_each_entry(params, &memcg->memcg_slab_caches, list)
		cache_show(memcg_params_to_cache(params), m);
	mutex_unlock(&memcg_slab_mutex);

	return 0;
}
#endif

static int memcg_charge_kmem(struct mem_cgroup *memcg, gfp_t gfp, u64 size)
{
	struct res_counter *fail_res;
	int ret = 0;

	ret = res_counter_charge(&memcg->kmem, size, &fail_res);
	if (ret)
		return ret;

	ret = try_charge(memcg, gfp, size >> PAGE_SHIFT);
	if (ret == -EINTR)  {
		/*
		 * try_charge() chose to bypass to root due to OOM kill or
		 * fatal signal.  Since our only options are to either fail
		 * the allocation or charge it to this cgroup, do it as a
		 * temporary condition. But we can't fail. From a kmem/slab
		 * perspective, the cache has already been selected, by
		 * mem_cgroup_kmem_get_cache(), so it is too late to change
		 * our minds.
		 *
		 * This condition will only trigger if the task entered
		 * memcg_charge_kmem in a sane state, but was OOM-killed
		 * during try_charge() above. Tasks that were already dying
		 * when the allocation triggers should have been already
		 * directed to the root cgroup in memcontrol.h
		 */
		res_counter_charge_nofail(&memcg->res, size, &fail_res);
		if (do_swap_account)
			res_counter_charge_nofail(&memcg->memsw, size,
						  &fail_res);
		ret = 0;
	} else if (ret)
		res_counter_uncharge(&memcg->kmem, size);

	return ret;
}

static void memcg_uncharge_kmem(struct mem_cgroup *memcg, u64 size)
{
	res_counter_uncharge(&memcg->res, size);
	if (do_swap_account)
		res_counter_uncharge(&memcg->memsw, size);

	/* Not down to 0 */
	if (res_counter_uncharge(&memcg->kmem, size))
		return;

	/*
	 * Releases a reference taken in kmem_cgroup_css_offline in case
	 * this last uncharge is racing with the offlining code or it is
	 * outliving the memcg existence.
	 *
	 * The memory barrier imposed by test&clear is paired with the
	 * explicit one in memcg_kmem_mark_dead().
	 */
	if (memcg_kmem_test_and_clear_dead(memcg))
		css_put(&memcg->css);
}

/*
 * helper for acessing a memcg's index. It will be used as an index in the
 * child cache array in kmem_cache, and also to derive its name. This function
 * will return -1 when this is not a kmem-limited memcg.
 */
int memcg_cache_id(struct mem_cgroup *memcg)
{
	return memcg ? memcg->kmemcg_id : -1;
}

static size_t memcg_caches_array_size(int num_groups)
{
	ssize_t size;
	if (num_groups <= 0)
		return 0;

	size = 2 * num_groups;
	if (size < MEMCG_CACHES_MIN_SIZE)
		size = MEMCG_CACHES_MIN_SIZE;
	else if (size > MEMCG_CACHES_MAX_SIZE)
		size = MEMCG_CACHES_MAX_SIZE;

	return size;
}

/*
 * We should update the current array size iff all caches updates succeed. This
 * can only be done from the slab side. The slab mutex needs to be held when
 * calling this.
 */
void memcg_update_array_size(int num)
{
	if (num > memcg_limited_groups_array_size)
		memcg_limited_groups_array_size = memcg_caches_array_size(num);
}

int memcg_update_cache_size(struct kmem_cache *s, int num_groups)
{
	struct memcg_cache_params *cur_params = s->memcg_params;

	VM_BUG_ON(!is_root_cache(s));

	if (num_groups > memcg_limited_groups_array_size) {
		int i;
		struct memcg_cache_params *new_params;
		ssize_t size = memcg_caches_array_size(num_groups);

		size *= sizeof(void *);
		size += offsetof(struct memcg_cache_params, memcg_caches);

		new_params = kzalloc(size, GFP_KERNEL);
		if (!new_params)
			return -ENOMEM;

		new_params->is_root_cache = true;

		/*
		 * There is the chance it will be bigger than
		 * memcg_limited_groups_array_size, if we failed an allocation
		 * in a cache, in which case all caches updated before it, will
		 * have a bigger array.
		 *
		 * But if that is the case, the data after
		 * memcg_limited_groups_array_size is certainly unused
		 */
		for (i = 0; i < memcg_limited_groups_array_size; i++) {
			if (!cur_params->memcg_caches[i])
				continue;
			new_params->memcg_caches[i] =
						cur_params->memcg_caches[i];
		}

		/*
		 * Ideally, we would wait until all caches succeed, and only
		 * then free the old one. But this is not worth the extra
		 * pointer per-cache we'd have to have for this.
		 *
		 * It is not a big deal if some caches are left with a size
		 * bigger than the others. And all updates will reset this
		 * anyway.
		 */
		rcu_assign_pointer(s->memcg_params, new_params);
		if (cur_params)
			kfree_rcu(cur_params, rcu_head);
	}
	return 0;
}

int memcg_alloc_cache_params(struct mem_cgroup *memcg, struct kmem_cache *s,
			     struct kmem_cache *root_cache)
{
	size_t size;

	if (!memcg_kmem_enabled())
		return 0;

	if (!memcg) {
		size = offsetof(struct memcg_cache_params, memcg_caches);
		size += memcg_limited_groups_array_size * sizeof(void *);
	} else
		size = sizeof(struct memcg_cache_params);

	s->memcg_params = kzalloc(size, GFP_KERNEL);
	if (!s->memcg_params)
		return -ENOMEM;

	if (memcg) {
		s->memcg_params->memcg = memcg;
		s->memcg_params->root_cache = root_cache;
		css_get(&memcg->css);
	} else
		s->memcg_params->is_root_cache = true;

	return 0;
}

void memcg_free_cache_params(struct kmem_cache *s)
{
	if (!s->memcg_params)
		return;
	if (!s->memcg_params->is_root_cache)
		css_put(&s->memcg_params->memcg->css);
	kfree(s->memcg_params);
}

static void memcg_register_cache(struct mem_cgroup *memcg,
				 struct kmem_cache *root_cache)
{
	static char memcg_name_buf[NAME_MAX + 1]; /* protected by
						     memcg_slab_mutex */
	struct kmem_cache *cachep;
	int id;

	lockdep_assert_held(&memcg_slab_mutex);

	id = memcg_cache_id(memcg);

	/*
	 * Since per-memcg caches are created asynchronously on first
	 * allocation (see memcg_kmem_get_cache()), several threads can try to
	 * create the same cache, but only one of them may succeed.
	 */
	if (cache_from_memcg_idx(root_cache, id))
		return;

	cgroup_name(memcg->css.cgroup, memcg_name_buf, NAME_MAX + 1);
	cachep = memcg_create_kmem_cache(memcg, root_cache, memcg_name_buf);
	/*
	 * If we could not create a memcg cache, do not complain, because
	 * that's not critical at all as we can always proceed with the root
	 * cache.
	 */
	if (!cachep)
		return;

	list_add(&cachep->memcg_params->list, &memcg->memcg_slab_caches);

	/*
	 * Since readers won't lock (see cache_from_memcg_idx()), we need a
	 * barrier here to ensure nobody will see the kmem_cache partially
	 * initialized.
	 */
	smp_wmb();

	BUG_ON(root_cache->memcg_params->memcg_caches[id]);
	root_cache->memcg_params->memcg_caches[id] = cachep;
}

static void memcg_unregister_cache(struct kmem_cache *cachep)
{
	struct kmem_cache *root_cache;
	struct mem_cgroup *memcg;
	int id;

	lockdep_assert_held(&memcg_slab_mutex);

	BUG_ON(is_root_cache(cachep));

	root_cache = cachep->memcg_params->root_cache;
	memcg = cachep->memcg_params->memcg;
	id = memcg_cache_id(memcg);

	BUG_ON(root_cache->memcg_params->memcg_caches[id] != cachep);
	root_cache->memcg_params->memcg_caches[id] = NULL;

	list_del(&cachep->memcg_params->list);

	kmem_cache_destroy(cachep);
}

/*
 * During the creation a new cache, we need to disable our accounting mechanism
 * altogether. This is true even if we are not creating, but rather just
 * enqueing new caches to be created.
 *
 * This is because that process will trigger allocations; some visible, like
 * explicit kmallocs to auxiliary data structures, name strings and internal
 * cache structures; some well concealed, like INIT_WORK() that can allocate
 * objects during debug.
 *
 * If any allocation happens during memcg_kmem_get_cache, we will recurse back
 * to it. This may not be a bounded recursion: since the first cache creation
 * failed to complete (waiting on the allocation), we'll just try to create the
 * cache again, failing at the same point.
 *
 * memcg_kmem_get_cache is prepared to abort after seeing a positive count of
 * memcg_kmem_skip_account. So we enclose anything that might allocate memory
 * inside the following two functions.
 */
static inline void memcg_stop_kmem_account(void)
{
	VM_BUG_ON(!current->mm);
	current->memcg_kmem_skip_account++;
}

static inline void memcg_resume_kmem_account(void)
{
	VM_BUG_ON(!current->mm);
	current->memcg_kmem_skip_account--;
}

int __memcg_cleanup_cache_params(struct kmem_cache *s)
{
	struct kmem_cache *c;
	int i, failed = 0;

	mutex_lock(&memcg_slab_mutex);
	for_each_memcg_cache_index(i) {
		c = cache_from_memcg_idx(s, i);
		if (!c)
			continue;

		memcg_unregister_cache(c);

		if (cache_from_memcg_idx(s, i))
			failed++;
	}
	mutex_unlock(&memcg_slab_mutex);
	return failed;
}

static void memcg_unregister_all_caches(struct mem_cgroup *memcg)
{
	struct kmem_cache *cachep;
	struct memcg_cache_params *params, *tmp;

	if (!memcg_kmem_is_active(memcg))
		return;

	mutex_lock(&memcg_slab_mutex);
	list_for_each_entry_safe(params, tmp, &memcg->memcg_slab_caches, list) {
		cachep = memcg_params_to_cache(params);
		kmem_cache_shrink(cachep);
		if (atomic_read(&cachep->memcg_params->nr_pages) == 0)
			memcg_unregister_cache(cachep);
	}
	mutex_unlock(&memcg_slab_mutex);
}

struct memcg_register_cache_work {
	struct mem_cgroup *memcg;
	struct kmem_cache *cachep;
	struct work_struct work;
};

static void memcg_register_cache_func(struct work_struct *w)
{
	struct memcg_register_cache_work *cw =
		container_of(w, struct memcg_register_cache_work, work);
	struct mem_cgroup *memcg = cw->memcg;
	struct kmem_cache *cachep = cw->cachep;

	mutex_lock(&memcg_slab_mutex);
	memcg_register_cache(memcg, cachep);
	mutex_unlock(&memcg_slab_mutex);

	css_put(&memcg->css);
	kfree(cw);
}

/*
 * Enqueue the creation of a per-memcg kmem_cache.
 */
static void __memcg_schedule_register_cache(struct mem_cgroup *memcg,
					    struct kmem_cache *cachep)
{
	struct memcg_register_cache_work *cw;

	cw = kmalloc(sizeof(*cw), GFP_NOWAIT);
	if (cw == NULL) {
		css_put(&memcg->css);
		return;
	}

	cw->memcg = memcg;
	cw->cachep = cachep;

	INIT_WORK(&cw->work, memcg_register_cache_func);
	schedule_work(&cw->work);
}

static void memcg_schedule_register_cache(struct mem_cgroup *memcg,
					  struct kmem_cache *cachep)
{
	/*
	 * We need to stop accounting when we kmalloc, because if the
	 * corresponding kmalloc cache is not yet created, the first allocation
	 * in __memcg_schedule_register_cache will recurse.
	 *
	 * However, it is better to enclose the whole function. Depending on
	 * the debugging options enabled, INIT_WORK(), for instance, can
	 * trigger an allocation. This too, will make us recurse. Because at
	 * this point we can't allow ourselves back into memcg_kmem_get_cache,
	 * the safest choice is to do it like this, wrapping the whole function.
	 */
	memcg_stop_kmem_account();
	__memcg_schedule_register_cache(memcg, cachep);
	memcg_resume_kmem_account();
}

int __memcg_charge_slab(struct kmem_cache *cachep, gfp_t gfp, int order)
{
	int res;

	res = memcg_charge_kmem(cachep->memcg_params->memcg, gfp,
				PAGE_SIZE << order);
	if (!res)
		atomic_add(1 << order, &cachep->memcg_params->nr_pages);
	return res;
}

void __memcg_uncharge_slab(struct kmem_cache *cachep, int order)
{
	memcg_uncharge_kmem(cachep->memcg_params->memcg, PAGE_SIZE << order);
	atomic_sub(1 << order, &cachep->memcg_params->nr_pages);
}

/*
 * Return the kmem_cache we're supposed to use for a slab allocation.
 * We try to use the current memcg's version of the cache.
 *
 * If the cache does not exist yet, if we are the first user of it,
 * we either create it immediately, if possible, or create it asynchronously
 * in a workqueue.
 * In the latter case, we will let the current allocation go through with
 * the original cache.
 *
 * Can't be called in interrupt context or from kernel threads.
 * This function needs to be called with rcu_read_lock() held.
 */
struct kmem_cache *__memcg_kmem_get_cache(struct kmem_cache *cachep,
					  gfp_t gfp)
{
	struct mem_cgroup *memcg;
	struct kmem_cache *memcg_cachep;

	VM_BUG_ON(!cachep->memcg_params);
	VM_BUG_ON(!cachep->memcg_params->is_root_cache);

	if (!current->mm || current->memcg_kmem_skip_account)
		return cachep;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(rcu_dereference(current->mm->owner));

	if (!memcg_can_account_kmem(memcg))
		goto out;

	memcg_cachep = cache_from_memcg_idx(cachep, memcg_cache_id(memcg));
	if (likely(memcg_cachep)) {
		cachep = memcg_cachep;
		goto out;
	}

	/* The corresponding put will be done in the workqueue. */
	if (!css_tryget_online(&memcg->css))
		goto out;
	rcu_read_unlock();

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
	memcg_schedule_register_cache(memcg, cachep);
	return cachep;
out:
	rcu_read_unlock();
	return cachep;
}

/*
 * We need to verify if the allocation against current->mm->owner's memcg is
 * possible for the given order. But the page is not allocated yet, so we'll
 * need a further commit step to do the final arrangements.
 *
 * It is possible for the task to switch cgroups in this mean time, so at
 * commit time, we can't rely on task conversion any longer.  We'll then use
 * the handle argument to return to the caller which cgroup we should commit
 * against. We could also return the memcg directly and avoid the pointer
 * passing, but a boolean return value gives better semantics considering
 * the compiled-out case as well.
 *
 * Returning true means the allocation is possible.
 */
bool
__memcg_kmem_newpage_charge(gfp_t gfp, struct mem_cgroup **_memcg, int order)
{
	struct mem_cgroup *memcg;
	int ret;

	*_memcg = NULL;

	/*
	 * Disabling accounting is only relevant for some specific memcg
	 * internal allocations. Therefore we would initially not have such
	 * check here, since direct calls to the page allocator that are
	 * accounted to kmemcg (alloc_kmem_pages and friends) only happen
	 * outside memcg core. We are mostly concerned with cache allocations,
	 * and by having this test at memcg_kmem_get_cache, we are already able
	 * to relay the allocation to the root cache and bypass the memcg cache
	 * altogether.
	 *
	 * There is one exception, though: the SLUB allocator does not create
	 * large order caches, but rather service large kmallocs directly from
	 * the page allocator. Therefore, the following sequence when backed by
	 * the SLUB allocator:
	 *
	 *	memcg_stop_kmem_account();
	 *	kmalloc(<large_number>)
	 *	memcg_resume_kmem_account();
	 *
	 * would effectively ignore the fact that we should skip accounting,
	 * since it will drive us directly to this function without passing
	 * through the cache selector memcg_kmem_get_cache. Such large
	 * allocations are extremely rare but can happen, for instance, for the
	 * cache arrays. We bring this test here.
	 */
	if (!current->mm || current->memcg_kmem_skip_account)
		return true;

	memcg = get_mem_cgroup_from_mm(current->mm);

	if (!memcg_can_account_kmem(memcg)) {
		css_put(&memcg->css);
		return true;
	}

	ret = memcg_charge_kmem(memcg, gfp, PAGE_SIZE << order);
	if (!ret)
		*_memcg = memcg;

	css_put(&memcg->css);
	return (ret == 0);
}

void __memcg_kmem_commit_charge(struct page *page, struct mem_cgroup *memcg,
			      int order)
{
	struct page_cgroup *pc;

	VM_BUG_ON(mem_cgroup_is_root(memcg));

	/* The page allocation failed. Revert */
	if (!page) {
		memcg_uncharge_kmem(memcg, PAGE_SIZE << order);
		return;
	}
	/*
	 * The page is freshly allocated and not visible to any
	 * outside callers yet.  Set up pc non-atomically.
	 */
	pc = lookup_page_cgroup(page);
	pc->mem_cgroup = memcg;
	pc->flags = PCG_USED;
}

void __memcg_kmem_uncharge_pages(struct page *page, int order)
{
	struct mem_cgroup *memcg = NULL;
	struct page_cgroup *pc;


	pc = lookup_page_cgroup(page);
	if (!PageCgroupUsed(pc))
		return;

	memcg = pc->mem_cgroup;
	pc->flags = 0;

	/*
	 * We trust that only if there is a memcg associated with the page, it
	 * is a valid allocation
	 */
	if (!memcg)
		return;

	VM_BUG_ON_PAGE(mem_cgroup_is_root(memcg), page);
	memcg_uncharge_kmem(memcg, PAGE_SIZE << order);
}
#else
static inline void memcg_unregister_all_caches(struct mem_cgroup *memcg)
{
}
#endif /* CONFIG_MEMCG_KMEM */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

/*
 * Because tail pages are not marked as "used", set it. We're under
 * zone->lru_lock, 'splitting on pmd' and compound_lock.
 * charge/uncharge will be never happen and move_account() is done under
 * compound_lock(), so we don't have to take care of races.
 */
void mem_cgroup_split_huge_fixup(struct page *head)
{
	struct page_cgroup *head_pc = lookup_page_cgroup(head);
	struct page_cgroup *pc;
	struct mem_cgroup *memcg;
	int i;

	if (mem_cgroup_disabled())
		return;

	memcg = head_pc->mem_cgroup;
	for (i = 1; i < HPAGE_PMD_NR; i++) {
		pc = head_pc + i;
		pc->mem_cgroup = memcg;
		pc->flags = head_pc->flags;
	}
	__this_cpu_sub(memcg->stat->count[MEM_CGROUP_STAT_RSS_HUGE],
		       HPAGE_PMD_NR);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

/**
 * mem_cgroup_move_account - move account of the page
 * @page: the page
 * @nr_pages: number of regular pages (>1 for huge pages)
 * @pc:	page_cgroup of the page.
 * @from: mem_cgroup which the page is moved from.
 * @to:	mem_cgroup which the page is moved to. @from != @to.
 *
 * The caller must confirm following.
 * - page is not on LRU (isolate_page() is useful.)
 * - compound_lock is held when nr_pages > 1
 *
 * This function doesn't do "charge" to new cgroup and doesn't do "uncharge"
 * from old cgroup.
 */
static int mem_cgroup_move_account(struct page *page,
				   unsigned int nr_pages,
				   struct page_cgroup *pc,
				   struct mem_cgroup *from,
				   struct mem_cgroup *to)
{
	unsigned long flags;
	int ret;

	VM_BUG_ON(from == to);
	VM_BUG_ON_PAGE(PageLRU(page), page);
	/*
	 * The page is isolated from LRU. So, collapse function
	 * will not handle this page. But page splitting can happen.
	 * Do this check under compound_page_lock(). The caller should
	 * hold it.
	 */
	ret = -EBUSY;
	if (nr_pages > 1 && !PageTransHuge(page))
		goto out;

	/*
	 * Prevent mem_cgroup_migrate() from looking at pc->mem_cgroup
	 * of its source page while we change it: page migration takes
	 * both pages off the LRU, but page cache replacement doesn't.
	 */
	if (!trylock_page(page))
		goto out;

	ret = -EINVAL;
	if (!PageCgroupUsed(pc) || pc->mem_cgroup != from)
		goto out_unlock;

	move_lock_mem_cgroup(from, &flags);

	if (!PageAnon(page) && page_mapped(page)) {
		__this_cpu_sub(from->stat->count[MEM_CGROUP_STAT_FILE_MAPPED],
			       nr_pages);
		__this_cpu_add(to->stat->count[MEM_CGROUP_STAT_FILE_MAPPED],
			       nr_pages);
	}

	if (PageWriteback(page)) {
		__this_cpu_sub(from->stat->count[MEM_CGROUP_STAT_WRITEBACK],
			       nr_pages);
		__this_cpu_add(to->stat->count[MEM_CGROUP_STAT_WRITEBACK],
			       nr_pages);
	}

	/*
	 * It is safe to change pc->mem_cgroup here because the page
	 * is referenced, charged, and isolated - we can't race with
	 * uncharging, charging, migration, or LRU putback.
	 */

	/* caller should have done css_get */
	pc->mem_cgroup = to;
	move_unlock_mem_cgroup(from, &flags);
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
 * mem_cgroup_move_parent - moves page to the parent group
 * @page: the page to move
 * @pc: page_cgroup of the page
 * @child: page's cgroup
 *
 * move charges to its parent or the root cgroup if the group has no
 * parent (aka use_hierarchy==0).
 * Although this might fail (get_page_unless_zero, isolate_lru_page or
 * mem_cgroup_move_account fails) the failure is always temporary and
 * it signals a race with a page removal/uncharge or migration. In the
 * first case the page is on the way out and it will vanish from the LRU
 * on the next attempt and the call should be retried later.
 * Isolation from the LRU fails only if page has been isolated from
 * the LRU since we looked at it and that usually means either global
 * reclaim or migration going on. The page will either get back to the
 * LRU or vanish.
 * Finaly mem_cgroup_move_account fails only if the page got uncharged
 * (!PageCgroupUsed) or moved to a different group. The page will
 * disappear in the next attempt.
 */
static int mem_cgroup_move_parent(struct page *page,
				  struct page_cgroup *pc,
				  struct mem_cgroup *child)
{
	struct mem_cgroup *parent;
	unsigned int nr_pages;
	unsigned long uninitialized_var(flags);
	int ret;

	VM_BUG_ON(mem_cgroup_is_root(child));

	ret = -EBUSY;
	if (!get_page_unless_zero(page))
		goto out;
	if (isolate_lru_page(page))
		goto put;

	nr_pages = hpage_nr_pages(page);

	parent = parent_mem_cgroup(child);
	/*
	 * If no parent, move charges to root cgroup.
	 */
	if (!parent)
		parent = root_mem_cgroup;

	if (nr_pages > 1) {
		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
		flags = compound_lock_irqsave(page);
	}

	ret = mem_cgroup_move_account(page, nr_pages,
				pc, child, parent);
	if (!ret)
		__mem_cgroup_cancel_local_charge(child, nr_pages);

	if (nr_pages > 1)
		compound_unlock_irqrestore(page, flags);
	putback_lru_page(page);
put:
	put_page(page);
out:
	return ret;
}

#ifdef CONFIG_MEMCG_SWAP
static void mem_cgroup_swap_statistics(struct mem_cgroup *memcg,
					 bool charge)
{
	int val = (charge) ? 1 : -1;
	this_cpu_add(memcg->stat->count[MEM_CGROUP_STAT_SWAP], val);
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
 * The caller must have charged to @to, IOW, called res_counter_charge() about
 * both res and memsw, and called css_get().
 */
static int mem_cgroup_move_swap_account(swp_entry_t entry,
				struct mem_cgroup *from, struct mem_cgroup *to)
{
	unsigned short old_id, new_id;

	old_id = mem_cgroup_id(from);
	new_id = mem_cgroup_id(to);

	if (swap_cgroup_cmpxchg(entry, old_id, new_id) == old_id) {
		mem_cgroup_swap_statistics(from, false);
		mem_cgroup_swap_statistics(to, true);
		/*
		 * This function is only called from task migration context now.
		 * It postpones res_counter and refcount handling till the end
		 * of task migration(mem_cgroup_clear_mc()) for performance
		 * improvement. But we cannot postpone css_get(to)  because if
		 * the process that has been moved to @to does swap-in, the
		 * refcount of @to might be decreased to 0.
		 *
		 * We are in attach() phase, so the cgroup is guaranteed to be
		 * alive, so we can just call css_get().
		 */
		css_get(&to->css);
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

#ifdef CONFIG_DEBUG_VM
static struct page_cgroup *lookup_page_cgroup_used(struct page *page)
{
	struct page_cgroup *pc;

	pc = lookup_page_cgroup(page);
	/*
	 * Can be NULL while feeding pages into the page allocator for
	 * the first time, i.e. during boot or memory hotplug;
	 * or when mem_cgroup_disabled().
	 */
	if (likely(pc) && PageCgroupUsed(pc))
		return pc;
	return NULL;
}

bool mem_cgroup_bad_page_check(struct page *page)
{
	if (mem_cgroup_disabled())
		return false;

	return lookup_page_cgroup_used(page) != NULL;
}

void mem_cgroup_print_bad_page(struct page *page)
{
	struct page_cgroup *pc;

	pc = lookup_page_cgroup_used(page);
	if (pc) {
		pr_alert("pc:%p pc->flags:%lx pc->mem_cgroup:%p\n",
			 pc, pc->flags, pc->mem_cgroup);
	}
}
#endif

static int mem_cgroup_resize_limit(struct mem_cgroup *memcg,
				unsigned long long val)
{
	int retry_count;
	u64 memswlimit, memlimit;
	int ret = 0;
	int children = mem_cgroup_count_children(memcg);
	u64 curusage, oldusage;
	int enlarge;

	/*
	 * For keeping hierarchical_reclaim simple, how long we should retry
	 * is depends on callers. We set our retry-count to be function
	 * of # of children which we should visit in this loop.
	 */
	retry_count = MEM_CGROUP_RECLAIM_RETRIES * children;

	oldusage = res_counter_read_u64(&memcg->res, RES_USAGE);

	enlarge = 0;
	while (retry_count) {
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		/*
		 * Rather than hide all in some function, I do this in
		 * open coded manner. You see what this really does.
		 * We have to guarantee memcg->res.limit <= memcg->memsw.limit.
		 */
		mutex_lock(&set_limit_mutex);
		memswlimit = res_counter_read_u64(&memcg->memsw, RES_LIMIT);
		if (memswlimit < val) {
			ret = -EINVAL;
			mutex_unlock(&set_limit_mutex);
			break;
		}

		memlimit = res_counter_read_u64(&memcg->res, RES_LIMIT);
		if (memlimit < val)
			enlarge = 1;

		ret = res_counter_set_limit(&memcg->res, val);
		if (!ret) {
			if (memswlimit == val)
				memcg->memsw_is_minimum = true;
			else
				memcg->memsw_is_minimum = false;
		}
		mutex_unlock(&set_limit_mutex);

		if (!ret)
			break;

		mem_cgroup_reclaim(memcg, GFP_KERNEL,
				   MEM_CGROUP_RECLAIM_SHRINK);
		curusage = res_counter_read_u64(&memcg->res, RES_USAGE);
		/* Usage is reduced ? */
		if (curusage >= oldusage)
			retry_count--;
		else
			oldusage = curusage;
	}
	if (!ret && enlarge)
		memcg_oom_recover(memcg);

	return ret;
}

static int mem_cgroup_resize_memsw_limit(struct mem_cgroup *memcg,
					unsigned long long val)
{
	int retry_count;
	u64 memlimit, memswlimit, oldusage, curusage;
	int children = mem_cgroup_count_children(memcg);
	int ret = -EBUSY;
	int enlarge = 0;

	/* see mem_cgroup_resize_res_limit */
	retry_count = children * MEM_CGROUP_RECLAIM_RETRIES;
	oldusage = res_counter_read_u64(&memcg->memsw, RES_USAGE);
	while (retry_count) {
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		/*
		 * Rather than hide all in some function, I do this in
		 * open coded manner. You see what this really does.
		 * We have to guarantee memcg->res.limit <= memcg->memsw.limit.
		 */
		mutex_lock(&set_limit_mutex);
		memlimit = res_counter_read_u64(&memcg->res, RES_LIMIT);
		if (memlimit > val) {
			ret = -EINVAL;
			mutex_unlock(&set_limit_mutex);
			break;
		}
		memswlimit = res_counter_read_u64(&memcg->memsw, RES_LIMIT);
		if (memswlimit < val)
			enlarge = 1;
		ret = res_counter_set_limit(&memcg->memsw, val);
		if (!ret) {
			if (memlimit == val)
				memcg->memsw_is_minimum = true;
			else
				memcg->memsw_is_minimum = false;
		}
		mutex_unlock(&set_limit_mutex);

		if (!ret)
			break;

		mem_cgroup_reclaim(memcg, GFP_KERNEL,
				   MEM_CGROUP_RECLAIM_NOSWAP |
				   MEM_CGROUP_RECLAIM_SHRINK);
		curusage = res_counter_read_u64(&memcg->memsw, RES_USAGE);
		/* Usage is reduced ? */
		if (curusage >= oldusage)
			retry_count--;
		else
			oldusage = curusage;
	}
	if (!ret && enlarge)
		memcg_oom_recover(memcg);
	return ret;
}

unsigned long mem_cgroup_soft_limit_reclaim(struct zone *zone, int order,
					    gfp_t gfp_mask,
					    unsigned long *total_scanned)
{
	unsigned long nr_reclaimed = 0;
	struct mem_cgroup_per_zone *mz, *next_mz = NULL;
	unsigned long reclaimed;
	int loop = 0;
	struct mem_cgroup_tree_per_zone *mctz;
	unsigned long long excess;
	unsigned long nr_scanned;

	if (order > 0)
		return 0;

	mctz = soft_limit_tree_node_zone(zone_to_nid(zone), zone_idx(zone));
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
		reclaimed = mem_cgroup_soft_reclaim(mz->memcg, zone,
						    gfp_mask, &nr_scanned);
		nr_reclaimed += reclaimed;
		*total_scanned += nr_scanned;
		spin_lock_irq(&mctz->lock);

		/*
		 * If we failed to reclaim anything from this memory cgroup
		 * it is time to move on to the next cgroup
		 */
		next_mz = NULL;
		if (!reclaimed) {
			do {
				/*
				 * Loop until we find yet another one.
				 *
				 * By the time we get the soft_limit lock
				 * again, someone might have aded the
				 * group back on the RB tree. Iterate to
				 * make sure we get a different mem.
				 * mem_cgroup_largest_soft_limit_node returns
				 * NULL if no other cgroup is present on
				 * the tree
				 */
				next_mz =
				__mem_cgroup_largest_soft_limit_node(mctz);
				if (next_mz == mz)
					css_put(&next_mz->memcg->css);
				else /* next_mz == NULL or other memcg */
					break;
			} while (1);
		}
		__mem_cgroup_remove_exceeded(mz, mctz);
		excess = res_counter_soft_limit_excess(&mz->memcg->res);
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

/**
 * mem_cgroup_force_empty_list - clears LRU of a group
 * @memcg: group to clear
 * @node: NUMA node
 * @zid: zone id
 * @lru: lru to to clear
 *
 * Traverse a specified page_cgroup list and try to drop them all.  This doesn't
 * reclaim the pages page themselves - pages are moved to the parent (or root)
 * group.
 */
static void mem_cgroup_force_empty_list(struct mem_cgroup *memcg,
				int node, int zid, enum lru_list lru)
{
	struct lruvec *lruvec;
	unsigned long flags;
	struct list_head *list;
	struct page *busy;
	struct zone *zone;

	zone = &NODE_DATA(node)->node_zones[zid];
	lruvec = mem_cgroup_zone_lruvec(zone, memcg);
	list = &lruvec->lists[lru];

	busy = NULL;
	do {
		struct page_cgroup *pc;
		struct page *page;

		spin_lock_irqsave(&zone->lru_lock, flags);
		if (list_empty(list)) {
			spin_unlock_irqrestore(&zone->lru_lock, flags);
			break;
		}
		page = list_entry(list->prev, struct page, lru);
		if (busy == page) {
			list_move(&page->lru, list);
			busy = NULL;
			spin_unlock_irqrestore(&zone->lru_lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&zone->lru_lock, flags);

		pc = lookup_page_cgroup(page);

		if (mem_cgroup_move_parent(page, pc, memcg)) {
			/* found lock contention or "pc" is obsolete. */
			busy = page;
		} else
			busy = NULL;
		cond_resched();
	} while (!list_empty(list));
}

/*
 * make mem_cgroup's charge to be 0 if there is no task by moving
 * all the charges and pages to the parent.
 * This enables deleting this mem_cgroup.
 *
 * Caller is responsible for holding css reference on the memcg.
 */
static void mem_cgroup_reparent_charges(struct mem_cgroup *memcg)
{
	int node, zid;
	u64 usage;

	do {
		/* This is for making all *used* pages to be on LRU. */
		lru_add_drain_all();
		drain_all_stock_sync(memcg);
		mem_cgroup_start_move(memcg);
		for_each_node_state(node, N_MEMORY) {
			for (zid = 0; zid < MAX_NR_ZONES; zid++) {
				enum lru_list lru;
				for_each_lru(lru) {
					mem_cgroup_force_empty_list(memcg,
							node, zid, lru);
				}
			}
		}
		mem_cgroup_end_move(memcg);
		memcg_oom_recover(memcg);
		cond_resched();

		/*
		 * Kernel memory may not necessarily be trackable to a specific
		 * process. So they are not migrated, and therefore we can't
		 * expect their value to drop to 0 here.
		 * Having res filled up with kmem only is enough.
		 *
		 * This is a safety check because mem_cgroup_force_empty_list
		 * could have raced with mem_cgroup_replace_page_cache callers
		 * so the lru seemed empty but the page could have been added
		 * right after the check. RES_USAGE should be safe as we always
		 * charge before adding to the LRU.
		 */
		usage = res_counter_read_u64(&memcg->res, RES_USAGE) -
			res_counter_read_u64(&memcg->kmem, RES_USAGE);
	} while (usage > 0);
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

	/*
	 * The lock does not prevent addition or deletion of children, but
	 * it prevents a new child from being initialized based on this
	 * parent in css_online(), so it's enough to decide whether
	 * hierarchically inherited attributes can still be changed or not.
	 */
	lockdep_assert_held(&memcg_create_mutex);

	rcu_read_lock();
	ret = css_next_child(NULL, &memcg->css);
	rcu_read_unlock();
	return ret;
}

/*
 * Reclaims as many pages from the given memcg as possible and moves
 * the rest to the parent.
 *
 * Caller is responsible for holding css reference for memcg.
 */
static int mem_cgroup_force_empty(struct mem_cgroup *memcg)
{
	int nr_retries = MEM_CGROUP_RECLAIM_RETRIES;

	/* we call try-to-free pages for make this cgroup empty */
	lru_add_drain_all();
	/* try to free all pages in this cgroup */
	while (nr_retries && res_counter_read_u64(&memcg->res, RES_USAGE) > 0) {
		int progress;

		if (signal_pending(current))
			return -EINTR;

		progress = try_to_free_mem_cgroup_pages(memcg, GFP_KERNEL,
						false);
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

	mutex_lock(&memcg_create_mutex);

	if (memcg->use_hierarchy == val)
		goto out;

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

out:
	mutex_unlock(&memcg_create_mutex);

	return retval;
}

static u64 mem_cgroup_read_u64(struct cgroup_subsys_state *css,
			       struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	enum res_type type = MEMFILE_TYPE(cft->private);
	int name = MEMFILE_ATTR(cft->private);

	switch (type) {
	case _MEM:
		return res_counter_read_u64(&memcg->res, name);
	case _MEMSWAP:
		return res_counter_read_u64(&memcg->memsw, name);
	case _KMEM:
		return res_counter_read_u64(&memcg->kmem, name);
		break;
	default:
		BUG();
	}
}

#ifdef CONFIG_MEMCG_KMEM
/* should be called with activate_kmem_mutex held */
static int __memcg_activate_kmem(struct mem_cgroup *memcg,
				 unsigned long long limit)
{
	int err = 0;
	int memcg_id;

	if (memcg_kmem_is_active(memcg))
		return 0;

	/*
	 * We are going to allocate memory for data shared by all memory
	 * cgroups so let's stop accounting here.
	 */
	memcg_stop_kmem_account();

	/*
	 * For simplicity, we won't allow this to be disabled.  It also can't
	 * be changed if the cgroup has children already, or if tasks had
	 * already joined.
	 *
	 * If tasks join before we set the limit, a person looking at
	 * kmem.usage_in_bytes will have no way to determine when it took
	 * place, which makes the value quite meaningless.
	 *
	 * After it first became limited, changes in the value of the limit are
	 * of course permitted.
	 */
	mutex_lock(&memcg_create_mutex);
	if (cgroup_has_tasks(memcg->css.cgroup) ||
	    (memcg->use_hierarchy && memcg_has_children(memcg)))
		err = -EBUSY;
	mutex_unlock(&memcg_create_mutex);
	if (err)
		goto out;

	memcg_id = ida_simple_get(&kmem_limited_groups,
				  0, MEMCG_CACHES_MAX_SIZE, GFP_KERNEL);
	if (memcg_id < 0) {
		err = memcg_id;
		goto out;
	}

	/*
	 * Make sure we have enough space for this cgroup in each root cache's
	 * memcg_params.
	 */
	mutex_lock(&memcg_slab_mutex);
	err = memcg_update_all_caches(memcg_id + 1);
	mutex_unlock(&memcg_slab_mutex);
	if (err)
		goto out_rmid;

	memcg->kmemcg_id = memcg_id;
	INIT_LIST_HEAD(&memcg->memcg_slab_caches);

	/*
	 * We couldn't have accounted to this cgroup, because it hasn't got the
	 * active bit set yet, so this should succeed.
	 */
	err = res_counter_set_limit(&memcg->kmem, limit);
	VM_BUG_ON(err);

	static_key_slow_inc(&memcg_kmem_enabled_key);
	/*
	 * Setting the active bit after enabling static branching will
	 * guarantee no one starts accounting before all call sites are
	 * patched.
	 */
	memcg_kmem_set_active(memcg);
out:
	memcg_resume_kmem_account();
	return err;

out_rmid:
	ida_simple_remove(&kmem_limited_groups, memcg_id);
	goto out;
}

static int memcg_activate_kmem(struct mem_cgroup *memcg,
			       unsigned long long limit)
{
	int ret;

	mutex_lock(&activate_kmem_mutex);
	ret = __memcg_activate_kmem(memcg, limit);
	mutex_unlock(&activate_kmem_mutex);
	return ret;
}

static int memcg_update_kmem_limit(struct mem_cgroup *memcg,
				   unsigned long long val)
{
	int ret;

	if (!memcg_kmem_is_active(memcg))
		ret = memcg_activate_kmem(memcg, val);
	else
		ret = res_counter_set_limit(&memcg->kmem, val);
	return ret;
}

static int memcg_propagate_kmem(struct mem_cgroup *memcg)
{
	int ret = 0;
	struct mem_cgroup *parent = parent_mem_cgroup(memcg);

	if (!parent)
		return 0;

	mutex_lock(&activate_kmem_mutex);
	/*
	 * If the parent cgroup is not kmem-active now, it cannot be activated
	 * after this point, because it has at least one child already.
	 */
	if (memcg_kmem_is_active(parent))
		ret = __memcg_activate_kmem(memcg, RES_COUNTER_MAX);
	mutex_unlock(&activate_kmem_mutex);
	return ret;
}
#else
static int memcg_update_kmem_limit(struct mem_cgroup *memcg,
				   unsigned long long val)
{
	return -EINVAL;
}
#endif /* CONFIG_MEMCG_KMEM */

/*
 * The user of this function is...
 * RES_LIMIT.
 */
static ssize_t mem_cgroup_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	enum res_type type;
	int name;
	unsigned long long val;
	int ret;

	buf = strstrip(buf);
	type = MEMFILE_TYPE(of_cft(of)->private);
	name = MEMFILE_ATTR(of_cft(of)->private);

	switch (name) {
	case RES_LIMIT:
		if (mem_cgroup_is_root(memcg)) { /* Can't set limit on root */
			ret = -EINVAL;
			break;
		}
		/* This function does all necessary parse...reuse it */
		ret = res_counter_memparse_write_strategy(buf, &val);
		if (ret)
			break;
		if (type == _MEM)
			ret = mem_cgroup_resize_limit(memcg, val);
		else if (type == _MEMSWAP)
			ret = mem_cgroup_resize_memsw_limit(memcg, val);
		else if (type == _KMEM)
			ret = memcg_update_kmem_limit(memcg, val);
		else
			return -EINVAL;
		break;
	case RES_SOFT_LIMIT:
		ret = res_counter_memparse_write_strategy(buf, &val);
		if (ret)
			break;
		/*
		 * For memsw, soft limits are hard to implement in terms
		 * of semantics, for now, we support soft limits for
		 * control without swap
		 */
		if (type == _MEM)
			ret = res_counter_set_soft_limit(&memcg->res, val);
		else
			ret = -EINVAL;
		break;
	default:
		ret = -EINVAL; /* should be BUG() ? */
		break;
	}
	return ret ?: nbytes;
}

static void memcg_get_hierarchical_limit(struct mem_cgroup *memcg,
		unsigned long long *mem_limit, unsigned long long *memsw_limit)
{
	unsigned long long min_limit, min_memsw_limit, tmp;

	min_limit = res_counter_read_u64(&memcg->res, RES_LIMIT);
	min_memsw_limit = res_counter_read_u64(&memcg->memsw, RES_LIMIT);
	if (!memcg->use_hierarchy)
		goto out;

	while (memcg->css.parent) {
		memcg = mem_cgroup_from_css(memcg->css.parent);
		if (!memcg->use_hierarchy)
			break;
		tmp = res_counter_read_u64(&memcg->res, RES_LIMIT);
		min_limit = min(min_limit, tmp);
		tmp = res_counter_read_u64(&memcg->memsw, RES_LIMIT);
		min_memsw_limit = min(min_memsw_limit, tmp);
	}
out:
	*mem_limit = min_limit;
	*memsw_limit = min_memsw_limit;
}

static ssize_t mem_cgroup_reset(struct kernfs_open_file *of, char *buf,
				size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	int name;
	enum res_type type;

	type = MEMFILE_TYPE(of_cft(of)->private);
	name = MEMFILE_ATTR(of_cft(of)->private);

	switch (name) {
	case RES_MAX_USAGE:
		if (type == _MEM)
			res_counter_reset_max(&memcg->res);
		else if (type == _MEMSWAP)
			res_counter_reset_max(&memcg->memsw);
		else if (type == _KMEM)
			res_counter_reset_max(&memcg->kmem);
		else
			return -EINVAL;
		break;
	case RES_FAILCNT:
		if (type == _MEM)
			res_counter_reset_failcnt(&memcg->res);
		else if (type == _MEMSWAP)
			res_counter_reset_failcnt(&memcg->memsw);
		else if (type == _KMEM)
			res_counter_reset_failcnt(&memcg->kmem);
		else
			return -EINVAL;
		break;
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

	if (val >= (1 << NR_MOVE_TYPE))
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

static inline void mem_cgroup_lru_names_not_uptodate(void)
{
	BUILD_BUG_ON(ARRAY_SIZE(mem_cgroup_lru_names) != NR_LRU_LISTS);
}

static int memcg_stat_show(struct seq_file *m, void *v)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(seq_css(m));
	struct mem_cgroup *mi;
	unsigned int i;

	for (i = 0; i < MEM_CGROUP_STAT_NSTATS; i++) {
		if (i == MEM_CGROUP_STAT_SWAP && !do_swap_account)
			continue;
		seq_printf(m, "%s %ld\n", mem_cgroup_stat_names[i],
			   mem_cgroup_read_stat(memcg, i) * PAGE_SIZE);
	}

	for (i = 0; i < MEM_CGROUP_EVENTS_NSTATS; i++)
		seq_printf(m, "%s %lu\n", mem_cgroup_events_names[i],
			   mem_cgroup_read_events(memcg, i));

	for (i = 0; i < NR_LRU_LISTS; i++)
		seq_printf(m, "%s %lu\n", mem_cgroup_lru_names[i],
			   mem_cgroup_nr_lru_pages(memcg, BIT(i)) * PAGE_SIZE);

	/* Hierarchical information */
	{
		unsigned long long limit, memsw_limit;
		memcg_get_hierarchical_limit(memcg, &limit, &memsw_limit);
		seq_printf(m, "hierarchical_memory_limit %llu\n", limit);
		if (do_swap_account)
			seq_printf(m, "hierarchical_memsw_limit %llu\n",
				   memsw_limit);
	}

	for (i = 0; i < MEM_CGROUP_STAT_NSTATS; i++) {
		long long val = 0;

		if (i == MEM_CGROUP_STAT_SWAP && !do_swap_account)
			continue;
		for_each_mem_cgroup_tree(mi, memcg)
			val += mem_cgroup_read_stat(mi, i) * PAGE_SIZE;
		seq_printf(m, "total_%s %lld\n", mem_cgroup_stat_names[i], val);
	}

	for (i = 0; i < MEM_CGROUP_EVENTS_NSTATS; i++) {
		unsigned long long val = 0;

		for_each_mem_cgroup_tree(mi, memcg)
			val += mem_cgroup_read_events(mi, i);
		seq_printf(m, "total_%s %llu\n",
			   mem_cgroup_events_names[i], val);
	}

	for (i = 0; i < NR_LRU_LISTS; i++) {
		unsigned long long val = 0;

		for_each_mem_cgroup_tree(mi, memcg)
			val += mem_cgroup_nr_lru_pages(mi, BIT(i)) * PAGE_SIZE;
		seq_printf(m, "total_%s %llu\n", mem_cgroup_lru_names[i], val);
	}

#ifdef CONFIG_DEBUG_VM
	{
		int nid, zid;
		struct mem_cgroup_per_zone *mz;
		struct zone_reclaim_stat *rstat;
		unsigned long recent_rotated[2] = {0, 0};
		unsigned long recent_scanned[2] = {0, 0};

		for_each_online_node(nid)
			for (zid = 0; zid < MAX_NR_ZONES; zid++) {
				mz = &memcg->nodeinfo[nid]->zoneinfo[zid];
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
	u64 usage;
	int i;

	rcu_read_lock();
	if (!swap)
		t = rcu_dereference(memcg->thresholds.primary);
	else
		t = rcu_dereference(memcg->memsw_thresholds.primary);

	if (!t)
		goto unlock;

	if (!swap)
		usage = res_counter_read_u64(&memcg->res, RES_USAGE);
	else
		usage = res_counter_read_u64(&memcg->memsw, RES_USAGE);

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
		if (do_swap_account)
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
	u64 threshold, usage;
	int i, size, ret;

	ret = res_counter_memparse_write_strategy(args, &threshold);
	if (ret)
		return ret;

	mutex_lock(&memcg->thresholds_lock);

	if (type == _MEM) {
		thresholds = &memcg->thresholds;
		usage = res_counter_read_u64(&memcg->res, RES_USAGE);
	} else if (type == _MEMSWAP) {
		thresholds = &memcg->memsw_thresholds;
		usage = res_counter_read_u64(&memcg->memsw, RES_USAGE);
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
	u64 usage;
	int i, j, size;

	mutex_lock(&memcg->thresholds_lock);

	if (type == _MEM) {
		thresholds = &memcg->thresholds;
		usage = res_counter_read_u64(&memcg->res, RES_USAGE);
	} else if (type == _MEMSWAP) {
		thresholds = &memcg->memsw_thresholds;
		usage = res_counter_read_u64(&memcg->memsw, RES_USAGE);
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
	/* If all events are unregistered, free the spare array */
	if (!new) {
		kfree(thresholds->spare);
		thresholds->spare = NULL;
	}

	rcu_assign_pointer(thresholds->primary, new);

	/* To be sure that nobody uses thresholds */
	synchronize_rcu();
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
	if (atomic_read(&memcg->under_oom))
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
	seq_printf(sf, "under_oom %d\n", (bool)atomic_read(&memcg->under_oom));
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

#ifdef CONFIG_MEMCG_KMEM
static int memcg_init_kmem(struct mem_cgroup *memcg, struct cgroup_subsys *ss)
{
	int ret;

	memcg->kmemcg_id = -1;
	ret = memcg_propagate_kmem(memcg);
	if (ret)
		return ret;

	return mem_cgroup_sockets_init(memcg, ss);
}

static void memcg_destroy_kmem(struct mem_cgroup *memcg)
{
	mem_cgroup_sockets_destroy(memcg);
}

static void kmem_cgroup_css_offline(struct mem_cgroup *memcg)
{
	if (!memcg_kmem_is_active(memcg))
		return;

	/*
	 * kmem charges can outlive the cgroup. In the case of slab
	 * pages, for instance, a page contain objects from various
	 * processes. As we prevent from taking a reference for every
	 * such allocation we have to be careful when doing uncharge
	 * (see memcg_uncharge_kmem) and here during offlining.
	 *
	 * The idea is that that only the _last_ uncharge which sees
	 * the dead memcg will drop the last reference. An additional
	 * reference is taken here before the group is marked dead
	 * which is then paired with css_put during uncharge resp. here.
	 *
	 * Although this might sound strange as this path is called from
	 * css_offline() when the referencemight have dropped down to 0 and
	 * shouldn't be incremented anymore (css_tryget_online() would
	 * fail) we do not have other options because of the kmem
	 * allocations lifetime.
	 */
	css_get(&memcg->css);

	memcg_kmem_mark_dead(memcg);

	if (res_counter_read_u64(&memcg->kmem, RES_USAGE) != 0)
		return;

	if (memcg_kmem_test_and_clear_dead(memcg))
		css_put(&memcg->css);
}
#else
static int memcg_init_kmem(struct mem_cgroup *memcg, struct cgroup_subsys *ss)
{
	return 0;
}

static void memcg_destroy_kmem(struct mem_cgroup *memcg)
{
}

static void kmem_cgroup_css_offline(struct mem_cgroup *memcg)
{
}
#endif

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
static int memcg_event_wake(wait_queue_t *wait, unsigned mode,
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
	name = cfile.file->f_dentry->d_name.name;

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
	cfile_css = css_tryget_online_from_dir(cfile.file->f_dentry->d_parent,
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

static struct cftype mem_cgroup_files[] = {
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
		.flags = CFTYPE_NO_PREFIX,
		.mode = S_IWUGO,
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
#ifdef CONFIG_MEMCG_KMEM
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
		.seq_show = mem_cgroup_slabinfo_read,
	},
#endif
#endif
	{ },	/* terminate */
};

#ifdef CONFIG_MEMCG_SWAP
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
#endif
static int alloc_mem_cgroup_per_zone_info(struct mem_cgroup *memcg, int node)
{
	struct mem_cgroup_per_node *pn;
	struct mem_cgroup_per_zone *mz;
	int zone, tmp = node;
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

	for (zone = 0; zone < MAX_NR_ZONES; zone++) {
		mz = &pn->zoneinfo[zone];
		lruvec_init(&mz->lruvec);
		mz->usage_in_excess = 0;
		mz->on_tree = false;
		mz->memcg = memcg;
	}
	memcg->nodeinfo[node] = pn;
	return 0;
}

static void free_mem_cgroup_per_zone_info(struct mem_cgroup *memcg, int node)
{
	kfree(memcg->nodeinfo[node]);
}

static struct mem_cgroup *mem_cgroup_alloc(void)
{
	struct mem_cgroup *memcg;
	size_t size;

	size = sizeof(struct mem_cgroup);
	size += nr_node_ids * sizeof(struct mem_cgroup_per_node *);

	memcg = kzalloc(size, GFP_KERNEL);
	if (!memcg)
		return NULL;

	memcg->stat = alloc_percpu(struct mem_cgroup_stat_cpu);
	if (!memcg->stat)
		goto out_free;
	spin_lock_init(&memcg->pcp_counter_lock);
	return memcg;

out_free:
	kfree(memcg);
	return NULL;
}

/*
 * At destroying mem_cgroup, references from swap_cgroup can remain.
 * (scanning all at force_empty is too costly...)
 *
 * Instead of clearing all references at force_empty, we remember
 * the number of reference from swap_cgroup and free mem_cgroup when
 * it goes down to 0.
 *
 * Removal of cgroup itself succeeds regardless of refs from swap.
 */

static void __mem_cgroup_free(struct mem_cgroup *memcg)
{
	int node;

	mem_cgroup_remove_from_trees(memcg);

	for_each_node(node)
		free_mem_cgroup_per_zone_info(memcg, node);

	free_percpu(memcg->stat);

	/*
	 * We need to make sure that (at least for now), the jump label
	 * destruction code runs outside of the cgroup lock. This is because
	 * get_online_cpus(), which is called from the static_branch update,
	 * can't be called inside the cgroup_lock. cpusets are the ones
	 * enforcing this dependency, so if they ever change, we might as well.
	 *
	 * schedule_work() will guarantee this happens. Be careful if you need
	 * to move this code around, and make sure it is outside
	 * the cgroup_lock.
	 */
	disarm_static_keys(memcg);
	kfree(memcg);
}

/*
 * Returns the parent mem_cgroup in memcgroup hierarchy with hierarchy enabled.
 */
struct mem_cgroup *parent_mem_cgroup(struct mem_cgroup *memcg)
{
	if (!memcg->res.parent)
		return NULL;
	return mem_cgroup_from_res_counter(memcg->res.parent, res);
}
EXPORT_SYMBOL(parent_mem_cgroup);

static void __init mem_cgroup_soft_limit_tree_init(void)
{
	struct mem_cgroup_tree_per_node *rtpn;
	struct mem_cgroup_tree_per_zone *rtpz;
	int tmp, node, zone;

	for_each_node(node) {
		tmp = node;
		if (!node_state(node, N_NORMAL_MEMORY))
			tmp = -1;
		rtpn = kzalloc_node(sizeof(*rtpn), GFP_KERNEL, tmp);
		BUG_ON(!rtpn);

		soft_limit_tree.rb_tree_per_node[node] = rtpn;

		for (zone = 0; zone < MAX_NR_ZONES; zone++) {
			rtpz = &rtpn->rb_tree_per_zone[zone];
			rtpz->rb_root = RB_ROOT;
			spin_lock_init(&rtpz->lock);
		}
	}
}

static struct cgroup_subsys_state * __ref
mem_cgroup_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct mem_cgroup *memcg;
	long error = -ENOMEM;
	int node;

	memcg = mem_cgroup_alloc();
	if (!memcg)
		return ERR_PTR(error);

	for_each_node(node)
		if (alloc_mem_cgroup_per_zone_info(memcg, node))
			goto free_out;

	/* root ? */
	if (parent_css == NULL) {
		root_mem_cgroup = memcg;
		res_counter_init(&memcg->res, NULL);
		res_counter_init(&memcg->memsw, NULL);
		res_counter_init(&memcg->kmem, NULL);
	}

	memcg->last_scanned_node = MAX_NUMNODES;
	INIT_LIST_HEAD(&memcg->oom_notify);
	memcg->move_charge_at_immigrate = 0;
	mutex_init(&memcg->thresholds_lock);
	spin_lock_init(&memcg->move_lock);
	vmpressure_init(&memcg->vmpressure);
	INIT_LIST_HEAD(&memcg->event_list);
	spin_lock_init(&memcg->event_list_lock);

	return &memcg->css;

free_out:
	__mem_cgroup_free(memcg);
	return ERR_PTR(error);
}

static int
mem_cgroup_css_online(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct mem_cgroup *parent = mem_cgroup_from_css(css->parent);

	if (css->id > MEM_CGROUP_ID_MAX)
		return -ENOSPC;

	if (!parent)
		return 0;

	mutex_lock(&memcg_create_mutex);

	memcg->use_hierarchy = parent->use_hierarchy;
	memcg->oom_kill_disable = parent->oom_kill_disable;
	memcg->swappiness = mem_cgroup_swappiness(parent);

	if (parent->use_hierarchy) {
		res_counter_init(&memcg->res, &parent->res);
		res_counter_init(&memcg->memsw, &parent->memsw);
		res_counter_init(&memcg->kmem, &parent->kmem);

		/*
		 * No need to take a reference to the parent because cgroup
		 * core guarantees its existence.
		 */
	} else {
		res_counter_init(&memcg->res, &root_mem_cgroup->res);
		res_counter_init(&memcg->memsw, &root_mem_cgroup->memsw);
		res_counter_init(&memcg->kmem, &root_mem_cgroup->kmem);
		/*
		 * Deeper hierachy with use_hierarchy == false doesn't make
		 * much sense so let cgroup subsystem know about this
		 * unfortunate state in our controller.
		 */
		if (parent != root_mem_cgroup)
			memory_cgrp_subsys.broken_hierarchy = true;
	}
	mutex_unlock(&memcg_create_mutex);

	return memcg_init_kmem(memcg, &memory_cgrp_subsys);
}

/*
 * Announce all parents that a group from their hierarchy is gone.
 */
static void mem_cgroup_invalidate_reclaim_iterators(struct mem_cgroup *memcg)
{
	struct mem_cgroup *parent = memcg;

	while ((parent = parent_mem_cgroup(parent)))
		mem_cgroup_iter_invalidate(parent);

	/*
	 * if the root memcg is not hierarchical we have to check it
	 * explicitely.
	 */
	if (!root_mem_cgroup->use_hierarchy)
		mem_cgroup_iter_invalidate(root_mem_cgroup);
}

static void mem_cgroup_css_offline(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct mem_cgroup_event *event, *tmp;
	struct cgroup_subsys_state *iter;

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

	kmem_cgroup_css_offline(memcg);

	mem_cgroup_invalidate_reclaim_iterators(memcg);

	/*
	 * This requires that offlining is serialized.  Right now that is
	 * guaranteed because css_killed_work_fn() holds the cgroup_mutex.
	 */
	css_for_each_descendant_post(iter, css)
		mem_cgroup_reparent_charges(mem_cgroup_from_css(iter));

	memcg_unregister_all_caches(memcg);
	vmpressure_cleanup(&memcg->vmpressure);
}

static void mem_cgroup_css_free(struct cgroup_subsys_state *css)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	/*
	 * XXX: css_offline() would be where we should reparent all
	 * memory to prepare the cgroup for destruction.  However,
	 * memcg does not do css_tryget_online() and res_counter charging
	 * under the same RCU lock region, which means that charging
	 * could race with offlining.  Offlining only happens to
	 * cgroups with no tasks in them but charges can show up
	 * without any tasks from the swapin path when the target
	 * memcg is looked up from the swapout record and not from the
	 * current task as it usually is.  A race like this can leak
	 * charges and put pages with stale cgroup pointers into
	 * circulation:
	 *
	 * #0                        #1
	 *                           lookup_swap_cgroup_id()
	 *                           rcu_read_lock()
	 *                           mem_cgroup_lookup()
	 *                           css_tryget_online()
	 *                           rcu_read_unlock()
	 * disable css_tryget_online()
	 * call_rcu()
	 *   offline_css()
	 *     reparent_charges()
	 *                           res_counter_charge()
	 *                           css_put()
	 *                             css_free()
	 *                           pc->mem_cgroup = dead memcg
	 *                           add page to lru
	 *
	 * The bulk of the charges are still moved in offline_css() to
	 * avoid pinning a lot of pages in case a long-term reference
	 * like a swapout record is deferring the css_free() to long
	 * after offlining.  But this makes sure we catch any charges
	 * made after offlining:
	 */
	mem_cgroup_reparent_charges(memcg);

	memcg_destroy_kmem(memcg);
	__mem_cgroup_free(memcg);
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

	mem_cgroup_resize_limit(memcg, ULLONG_MAX);
	mem_cgroup_resize_memsw_limit(memcg, ULLONG_MAX);
	memcg_update_kmem_limit(memcg, ULLONG_MAX);
	res_counter_set_soft_limit(&memcg->res, ULLONG_MAX);
}

#ifdef CONFIG_MMU
/* Handlers for move charge at task migration. */
static int mem_cgroup_do_precharge(unsigned long count)
{
	int ret;

	/* Try a single bulk charge without reclaim first */
	ret = try_charge(mc.to, GFP_KERNEL & ~__GFP_WAIT, count);
	if (!ret) {
		mc.precharge += count;
		return ret;
	}
	if (ret == -EINTR) {
		cancel_charge(root_mem_cgroup, count);
		return ret;
	}

	/* Try charges one by one with reclaim */
	while (count--) {
		ret = try_charge(mc.to, GFP_KERNEL & ~__GFP_NORETRY, 1);
		/*
		 * In case of failure, any residual charges against
		 * mc.to will be dropped by mem_cgroup_clear_mc()
		 * later on.  However, cancel any charges that are
		 * bypassed to root right away or they'll be lost.
		 */
		if (ret == -EINTR)
			cancel_charge(root_mem_cgroup, 1);
		if (ret)
			return ret;
		mc.precharge++;
		cond_resched();
	}
	return 0;
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
 *
 * Called with pte lock held.
 */
union mc_target {
	struct page	*page;
	swp_entry_t	ent;
};

enum mc_target_type {
	MC_TARGET_NONE = 0,
	MC_TARGET_PAGE,
	MC_TARGET_SWAP,
};

static struct page *mc_handle_present_pte(struct vm_area_struct *vma,
						unsigned long addr, pte_t ptent)
{
	struct page *page = vm_normal_page(vma, addr, ptent);

	if (!page || !page_mapped(page))
		return NULL;
	if (PageAnon(page)) {
		/* we don't move shared anon */
		if (!move_anon())
			return NULL;
	} else if (!move_file())
		/* we ignore mapcount for file pages */
		return NULL;
	if (!get_page_unless_zero(page))
		return NULL;

	return page;
}

#ifdef CONFIG_SWAP
static struct page *mc_handle_swap_pte(struct vm_area_struct *vma,
			unsigned long addr, pte_t ptent, swp_entry_t *entry)
{
	struct page *page = NULL;
	swp_entry_t ent = pte_to_swp_entry(ptent);

	if (!move_anon() || non_swap_entry(ent))
		return NULL;
	/*
	 * Because lookup_swap_cache() updates some statistics counter,
	 * we call find_get_page() with swapper_space directly.
	 */
	page = find_get_page(swap_address_space(ent), ent.val);
	if (do_swap_account)
		entry->val = ent.val;

	return page;
}
#else
static struct page *mc_handle_swap_pte(struct vm_area_struct *vma,
			unsigned long addr, pte_t ptent, swp_entry_t *entry)
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
	if (!move_file())
		return NULL;

	mapping = vma->vm_file->f_mapping;
	if (pte_none(ptent))
		pgoff = linear_page_index(vma, addr);
	else /* pte_file(ptent) is true */
		pgoff = pte_to_pgoff(ptent);

	/* page is moved even if it's not RSS of this task(page-faulted). */
#ifdef CONFIG_SWAP
	/* shmem/tmpfs may report page out on swap: account for that too. */
	if (shmem_mapping(mapping)) {
		page = find_get_entry(mapping, pgoff);
		if (radix_tree_exceptional_entry(page)) {
			swp_entry_t swp = radix_to_swp_entry(page);
			if (do_swap_account)
				*entry = swp;
			page = find_get_page(swap_address_space(swp), swp.val);
		}
	} else
		page = find_get_page(mapping, pgoff);
#else
	page = find_get_page(mapping, pgoff);
#endif
	return page;
}

static enum mc_target_type get_mctgt_type(struct vm_area_struct *vma,
		unsigned long addr, pte_t ptent, union mc_target *target)
{
	struct page *page = NULL;
	struct page_cgroup *pc;
	enum mc_target_type ret = MC_TARGET_NONE;
	swp_entry_t ent = { .val = 0 };

	if (pte_present(ptent))
		page = mc_handle_present_pte(vma, addr, ptent);
	else if (is_swap_pte(ptent))
		page = mc_handle_swap_pte(vma, addr, ptent, &ent);
	else if (pte_none(ptent) || pte_file(ptent))
		page = mc_handle_file_pte(vma, addr, ptent, &ent);

	if (!page && !ent.val)
		return ret;
	if (page) {
		pc = lookup_page_cgroup(page);
		/*
		 * Do only loose check w/o serialization.
		 * mem_cgroup_move_account() checks the pc is valid or
		 * not under LRU exclusion.
		 */
		if (PageCgroupUsed(pc) && pc->mem_cgroup == mc.from) {
			ret = MC_TARGET_PAGE;
			if (target)
				target->page = page;
		}
		if (!ret || !target)
			put_page(page);
	}
	/* There is a swap entry and a page doesn't exist or isn't charged */
	if (ent.val && !ret &&
	    mem_cgroup_id(mc.from) == lookup_swap_cgroup_id(ent)) {
		ret = MC_TARGET_SWAP;
		if (target)
			target->ent = ent;
	}
	return ret;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 * We don't consider swapping or file mapped pages because THP does not
 * support them for now.
 * Caller should make sure that pmd_trans_huge(pmd) is true.
 */
static enum mc_target_type get_mctgt_type_thp(struct vm_area_struct *vma,
		unsigned long addr, pmd_t pmd, union mc_target *target)
{
	struct page *page = NULL;
	struct page_cgroup *pc;
	enum mc_target_type ret = MC_TARGET_NONE;

	page = pmd_page(pmd);
	VM_BUG_ON_PAGE(!page || !PageHead(page), page);
	if (!move_anon())
		return ret;
	pc = lookup_page_cgroup(page);
	if (PageCgroupUsed(pc) && pc->mem_cgroup == mc.from) {
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
	struct vm_area_struct *vma = walk->private;
	pte_t *pte;
	spinlock_t *ptl;

	if (pmd_trans_huge_lock(pmd, vma, &ptl) == 1) {
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
	struct vm_area_struct *vma;

	down_read(&mm->mmap_sem);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		struct mm_walk mem_cgroup_count_precharge_walk = {
			.pmd_entry = mem_cgroup_count_precharge_pte_range,
			.mm = mm,
			.private = vma,
		};
		if (is_vm_hugetlb_page(vma))
			continue;
		walk_page_range(vma->vm_start, vma->vm_end,
					&mem_cgroup_count_precharge_walk);
	}
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
	int i;

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
		res_counter_uncharge(&mc.from->memsw,
				     PAGE_SIZE * mc.moved_swap);

		for (i = 0; i < mc.moved_swap; i++)
			css_put(&mc.from->css);

		/*
		 * we charged both to->res and to->memsw, so we should
		 * uncharge to->res.
		 */
		res_counter_uncharge(&mc.to->res,
				     PAGE_SIZE * mc.moved_swap);
		/* we've already done css_get(mc.to) */
		mc.moved_swap = 0;
	}
	memcg_oom_recover(from);
	memcg_oom_recover(to);
	wake_up_all(&mc.waitq);
}

static void mem_cgroup_clear_mc(void)
{
	struct mem_cgroup *from = mc.from;

	/*
	 * we must clear moving_task before waking up waiters at the end of
	 * task migration.
	 */
	mc.moving_task = NULL;
	__mem_cgroup_clear_mc();
	spin_lock(&mc.lock);
	mc.from = NULL;
	mc.to = NULL;
	spin_unlock(&mc.lock);
	mem_cgroup_end_move(from);
}

static int mem_cgroup_can_attach(struct cgroup_subsys_state *css,
				 struct cgroup_taskset *tset)
{
	struct task_struct *p = cgroup_taskset_first(tset);
	int ret = 0;
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	unsigned long move_charge_at_immigrate;

	/*
	 * We are now commited to this value whatever it is. Changes in this
	 * tunable will only affect upcoming migrations, not the current one.
	 * So we need to save it, and keep it going.
	 */
	move_charge_at_immigrate  = memcg->move_charge_at_immigrate;
	if (move_charge_at_immigrate) {
		struct mm_struct *mm;
		struct mem_cgroup *from = mem_cgroup_from_task(p);

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
			mem_cgroup_start_move(from);
			spin_lock(&mc.lock);
			mc.from = from;
			mc.to = memcg;
			mc.immigrate_flags = move_charge_at_immigrate;
			spin_unlock(&mc.lock);
			/* We set mc.moving_task later */

			ret = mem_cgroup_precharge_mc(mm);
			if (ret)
				mem_cgroup_clear_mc();
		}
		mmput(mm);
	}
	return ret;
}

static void mem_cgroup_cancel_attach(struct cgroup_subsys_state *css,
				     struct cgroup_taskset *tset)
{
	mem_cgroup_clear_mc();
}

static int mem_cgroup_move_charge_pte_range(pmd_t *pmd,
				unsigned long addr, unsigned long end,
				struct mm_walk *walk)
{
	int ret = 0;
	struct vm_area_struct *vma = walk->private;
	pte_t *pte;
	spinlock_t *ptl;
	enum mc_target_type target_type;
	union mc_target target;
	struct page *page;
	struct page_cgroup *pc;

	/*
	 * We don't take compound_lock() here but no race with splitting thp
	 * happens because:
	 *  - if pmd_trans_huge_lock() returns 1, the relevant thp is not
	 *    under splitting, which means there's no concurrent thp split,
	 *  - if another thread runs into split_huge_page() just after we
	 *    entered this if-block, the thread must wait for page table lock
	 *    to be unlocked in __split_huge_page_splitting(), where the main
	 *    part of thp split is not executed yet.
	 */
	if (pmd_trans_huge_lock(pmd, vma, &ptl) == 1) {
		if (mc.precharge < HPAGE_PMD_NR) {
			spin_unlock(ptl);
			return 0;
		}
		target_type = get_mctgt_type_thp(vma, addr, *pmd, &target);
		if (target_type == MC_TARGET_PAGE) {
			page = target.page;
			if (!isolate_lru_page(page)) {
				pc = lookup_page_cgroup(page);
				if (!mem_cgroup_move_account(page, HPAGE_PMD_NR,
							pc, mc.from, mc.to)) {
					mc.precharge -= HPAGE_PMD_NR;
					mc.moved_charge += HPAGE_PMD_NR;
				}
				putback_lru_page(page);
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
		swp_entry_t ent;

		if (!mc.precharge)
			break;

		switch (get_mctgt_type(vma, addr, ptent, &target)) {
		case MC_TARGET_PAGE:
			page = target.page;
			if (isolate_lru_page(page))
				goto put;
			pc = lookup_page_cgroup(page);
			if (!mem_cgroup_move_account(page, 1, pc,
						     mc.from, mc.to)) {
				mc.precharge--;
				/* we uncharge from mc.from later. */
				mc.moved_charge++;
			}
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

static void mem_cgroup_move_charge(struct mm_struct *mm)
{
	struct vm_area_struct *vma;

	lru_add_drain_all();
retry:
	if (unlikely(!down_read_trylock(&mm->mmap_sem))) {
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
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		int ret;
		struct mm_walk mem_cgroup_move_charge_walk = {
			.pmd_entry = mem_cgroup_move_charge_pte_range,
			.mm = mm,
			.private = vma,
		};
		if (is_vm_hugetlb_page(vma))
			continue;
		ret = walk_page_range(vma->vm_start, vma->vm_end,
						&mem_cgroup_move_charge_walk);
		if (ret)
			/*
			 * means we have consumed all precharges and failed in
			 * doing additional charge. Just abandon here.
			 */
			break;
	}
	up_read(&mm->mmap_sem);
}

static void mem_cgroup_move_task(struct cgroup_subsys_state *css,
				 struct cgroup_taskset *tset)
{
	struct task_struct *p = cgroup_taskset_first(tset);
	struct mm_struct *mm = get_task_mm(p);

	if (mm) {
		if (mc.to)
			mem_cgroup_move_charge(mm);
		mmput(mm);
	}
	if (mc.to)
		mem_cgroup_clear_mc();
}
#else	/* !CONFIG_MMU */
static int mem_cgroup_can_attach(struct cgroup_subsys_state *css,
				 struct cgroup_taskset *tset)
{
	return 0;
}
static void mem_cgroup_cancel_attach(struct cgroup_subsys_state *css,
				     struct cgroup_taskset *tset)
{
}
static void mem_cgroup_move_task(struct cgroup_subsys_state *css,
				 struct cgroup_taskset *tset)
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
	if (cgroup_on_dfl(root_css->cgroup))
		mem_cgroup_from_css(root_css)->use_hierarchy = true;
}

struct cgroup_subsys memory_cgrp_subsys = {
	.css_alloc = mem_cgroup_css_alloc,
	.css_online = mem_cgroup_css_online,
	.css_offline = mem_cgroup_css_offline,
	.css_free = mem_cgroup_css_free,
	.css_reset = mem_cgroup_css_reset,
	.can_attach = mem_cgroup_can_attach,
	.cancel_attach = mem_cgroup_cancel_attach,
	.attach = mem_cgroup_move_task,
	.bind = mem_cgroup_bind,
	.legacy_cftypes = mem_cgroup_files,
	.early_init = 0,
};

#ifdef CONFIG_MEMCG_SWAP
static int __init enable_swap_account(char *s)
{
	if (!strcmp(s, "1"))
		really_do_swap_account = 1;
	else if (!strcmp(s, "0"))
		really_do_swap_account = 0;
	return 1;
}
__setup("swapaccount=", enable_swap_account);

static void __init memsw_file_init(void)
{
	WARN_ON(cgroup_add_legacy_cftypes(&memory_cgrp_subsys,
					  memsw_cgroup_files));
}

static void __init enable_swap_cgroup(void)
{
	if (!mem_cgroup_disabled() && really_do_swap_account) {
		do_swap_account = 1;
		memsw_file_init();
	}
}

#else
static void __init enable_swap_cgroup(void)
{
}
#endif

#ifdef CONFIG_MEMCG_SWAP
/**
 * mem_cgroup_swapout - transfer a memsw charge to swap
 * @page: page whose memsw charge to transfer
 * @entry: swap entry to move the charge to
 *
 * Transfer the memsw charge of @page to @entry.
 */
void mem_cgroup_swapout(struct page *page, swp_entry_t entry)
{
	struct page_cgroup *pc;
	unsigned short oldid;

	VM_BUG_ON_PAGE(PageLRU(page), page);
	VM_BUG_ON_PAGE(page_count(page), page);

	if (!do_swap_account)
		return;

	pc = lookup_page_cgroup(page);

	/* Readahead page, never charged */
	if (!PageCgroupUsed(pc))
		return;

	VM_BUG_ON_PAGE(!(pc->flags & PCG_MEMSW), page);

	oldid = swap_cgroup_record(entry, mem_cgroup_id(pc->mem_cgroup));
	VM_BUG_ON_PAGE(oldid, page);

	pc->flags &= ~PCG_MEMSW;
	css_get(&pc->mem_cgroup->css);
	mem_cgroup_swap_statistics(pc->mem_cgroup, true);
}

/**
 * mem_cgroup_uncharge_swap - uncharge a swap entry
 * @entry: swap entry to uncharge
 *
 * Drop the memsw charge associated with @entry.
 */
void mem_cgroup_uncharge_swap(swp_entry_t entry)
{
	struct mem_cgroup *memcg;
	unsigned short id;

	if (!do_swap_account)
		return;

	id = swap_cgroup_record(entry, 0);
	rcu_read_lock();
	memcg = mem_cgroup_lookup(id);
	if (memcg) {
		res_counter_uncharge(&memcg->memsw, PAGE_SIZE);
		mem_cgroup_swap_statistics(memcg, false);
		css_put(&memcg->css);
	}
	rcu_read_unlock();
}
#endif

/**
 * mem_cgroup_try_charge - try charging a page
 * @page: page to charge
 * @mm: mm context of the victim
 * @gfp_mask: reclaim mode
 * @memcgp: charged memcg return
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
			  gfp_t gfp_mask, struct mem_cgroup **memcgp)
{
	struct mem_cgroup *memcg = NULL;
	unsigned int nr_pages = 1;
	int ret = 0;

	if (mem_cgroup_disabled())
		goto out;

	if (PageSwapCache(page)) {
		struct page_cgroup *pc = lookup_page_cgroup(page);
		/*
		 * Every swap fault against a single page tries to charge the
		 * page, bail as early as possible.  shmem_unuse() encounters
		 * already charged pages, too.  The USED bit is protected by
		 * the page lock, which serializes swap cache removal, which
		 * in turn serializes uncharging.
		 */
		if (PageCgroupUsed(pc))
			goto out;
	}

	if (PageTransHuge(page)) {
		nr_pages <<= compound_order(page);
		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
	}

	if (do_swap_account && PageSwapCache(page))
		memcg = try_get_mem_cgroup_from_page(page);
	if (!memcg)
		memcg = get_mem_cgroup_from_mm(mm);

	ret = try_charge(memcg, gfp_mask, nr_pages);

	css_put(&memcg->css);

	if (ret == -EINTR) {
		memcg = root_mem_cgroup;
		ret = 0;
	}
out:
	*memcgp = memcg;
	return ret;
}

/**
 * mem_cgroup_commit_charge - commit a page charge
 * @page: page to charge
 * @memcg: memcg to charge the page to
 * @lrucare: page might be on LRU already
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
			      bool lrucare)
{
	unsigned int nr_pages = 1;

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

	if (PageTransHuge(page)) {
		nr_pages <<= compound_order(page);
		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
	}

	commit_charge(page, memcg, nr_pages, lrucare);

	if (do_swap_account && PageSwapCache(page)) {
		swp_entry_t entry = { .val = page_private(page) };
		/*
		 * The swap entry might not get freed for a long time,
		 * let's not wait for it.  The page already received a
		 * memory+swap charge, drop the swap entry duplicate.
		 */
		mem_cgroup_uncharge_swap(entry);
	}
}

/**
 * mem_cgroup_cancel_charge - cancel a page charge
 * @page: page to charge
 * @memcg: memcg to charge the page to
 *
 * Cancel a charge transaction started by mem_cgroup_try_charge().
 */
void mem_cgroup_cancel_charge(struct page *page, struct mem_cgroup *memcg)
{
	unsigned int nr_pages = 1;

	if (mem_cgroup_disabled())
		return;
	/*
	 * Swap faults will attempt to charge the same page multiple
	 * times.  But reuse_swap_page() might have removed the page
	 * from swapcache already, so we can't check PageSwapCache().
	 */
	if (!memcg)
		return;

	if (PageTransHuge(page)) {
		nr_pages <<= compound_order(page);
		VM_BUG_ON_PAGE(!PageTransHuge(page), page);
	}

	cancel_charge(memcg, nr_pages);
}

static void uncharge_batch(struct mem_cgroup *memcg, unsigned long pgpgout,
			   unsigned long nr_mem, unsigned long nr_memsw,
			   unsigned long nr_anon, unsigned long nr_file,
			   unsigned long nr_huge, struct page *dummy_page)
{
	unsigned long flags;

	if (nr_mem)
		res_counter_uncharge(&memcg->res, nr_mem * PAGE_SIZE);
	if (nr_memsw)
		res_counter_uncharge(&memcg->memsw, nr_memsw * PAGE_SIZE);

	memcg_oom_recover(memcg);

	local_irq_save(flags);
	__this_cpu_sub(memcg->stat->count[MEM_CGROUP_STAT_RSS], nr_anon);
	__this_cpu_sub(memcg->stat->count[MEM_CGROUP_STAT_CACHE], nr_file);
	__this_cpu_sub(memcg->stat->count[MEM_CGROUP_STAT_RSS_HUGE], nr_huge);
	__this_cpu_add(memcg->stat->events[MEM_CGROUP_EVENTS_PGPGOUT], pgpgout);
	__this_cpu_add(memcg->stat->nr_page_events, nr_anon + nr_file);
	memcg_check_events(memcg, dummy_page);
	local_irq_restore(flags);
}

static void uncharge_list(struct list_head *page_list)
{
	struct mem_cgroup *memcg = NULL;
	unsigned long nr_memsw = 0;
	unsigned long nr_anon = 0;
	unsigned long nr_file = 0;
	unsigned long nr_huge = 0;
	unsigned long pgpgout = 0;
	unsigned long nr_mem = 0;
	struct list_head *next;
	struct page *page;

	next = page_list->next;
	do {
		unsigned int nr_pages = 1;
		struct page_cgroup *pc;

		page = list_entry(next, struct page, lru);
		next = page->lru.next;

		VM_BUG_ON_PAGE(PageLRU(page), page);
		VM_BUG_ON_PAGE(page_count(page), page);

		pc = lookup_page_cgroup(page);
		if (!PageCgroupUsed(pc))
			continue;

		/*
		 * Nobody should be changing or seriously looking at
		 * pc->mem_cgroup and pc->flags at this point, we have
		 * fully exclusive access to the page.
		 */

		if (memcg != pc->mem_cgroup) {
			if (memcg) {
				uncharge_batch(memcg, pgpgout, nr_mem, nr_memsw,
					       nr_anon, nr_file, nr_huge, page);
				pgpgout = nr_mem = nr_memsw = 0;
				nr_anon = nr_file = nr_huge = 0;
			}
			memcg = pc->mem_cgroup;
		}

		if (PageTransHuge(page)) {
			nr_pages <<= compound_order(page);
			VM_BUG_ON_PAGE(!PageTransHuge(page), page);
			nr_huge += nr_pages;
		}

		if (PageAnon(page))
			nr_anon += nr_pages;
		else
			nr_file += nr_pages;

		if (pc->flags & PCG_MEM)
			nr_mem += nr_pages;
		if (pc->flags & PCG_MEMSW)
			nr_memsw += nr_pages;
		pc->flags = 0;

		pgpgout++;
	} while (next != page_list);

	if (memcg)
		uncharge_batch(memcg, pgpgout, nr_mem, nr_memsw,
			       nr_anon, nr_file, nr_huge, page);
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
	struct page_cgroup *pc;

	if (mem_cgroup_disabled())
		return;

	/* Don't touch page->lru of any random page, pre-check: */
	pc = lookup_page_cgroup(page);
	if (!PageCgroupUsed(pc))
		return;

	INIT_LIST_HEAD(&page->lru);
	uncharge_list(&page->lru);
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
 * mem_cgroup_migrate - migrate a charge to another page
 * @oldpage: currently charged page
 * @newpage: page to transfer the charge to
 * @lrucare: both pages might be on the LRU already
 *
 * Migrate the charge from @oldpage to @newpage.
 *
 * Both pages must be locked, @newpage->mapping must be set up.
 */
void mem_cgroup_migrate(struct page *oldpage, struct page *newpage,
			bool lrucare)
{
	unsigned int nr_pages = 1;
	struct page_cgroup *pc;
	int isolated;

	VM_BUG_ON_PAGE(!PageLocked(oldpage), oldpage);
	VM_BUG_ON_PAGE(!PageLocked(newpage), newpage);
	VM_BUG_ON_PAGE(!lrucare && PageLRU(oldpage), oldpage);
	VM_BUG_ON_PAGE(!lrucare && PageLRU(newpage), newpage);
	VM_BUG_ON_PAGE(PageAnon(oldpage) != PageAnon(newpage), newpage);

	if (mem_cgroup_disabled())
		return;

	/* Page cache replacement: new page already charged? */
	pc = lookup_page_cgroup(newpage);
	if (PageCgroupUsed(pc))
		return;

	/* Re-entrant migration: old page already uncharged? */
	pc = lookup_page_cgroup(oldpage);
	if (!PageCgroupUsed(pc))
		return;

	VM_BUG_ON_PAGE(!(pc->flags & PCG_MEM), oldpage);
	VM_BUG_ON_PAGE(do_swap_account && !(pc->flags & PCG_MEMSW), oldpage);

	if (PageTransHuge(oldpage)) {
		nr_pages <<= compound_order(oldpage);
		VM_BUG_ON_PAGE(!PageTransHuge(oldpage), oldpage);
		VM_BUG_ON_PAGE(!PageTransHuge(newpage), newpage);
	}

	if (lrucare)
		lock_page_lru(oldpage, &isolated);

	pc->flags = 0;

	if (lrucare)
		unlock_page_lru(oldpage, isolated);

	local_irq_disable();
	mem_cgroup_charge_statistics(pc->mem_cgroup, oldpage, -nr_pages);
	memcg_check_events(pc->mem_cgroup, oldpage);
	local_irq_enable();

	commit_charge(newpage, pc->mem_cgroup, nr_pages, lrucare);
}

/*
 * subsys_initcall() for memory controller.
 *
 * Some parts like hotcpu_notifier() have to be initialized from this context
 * because of lock dependencies (cgroup_lock -> cpu hotplug) but basically
 * everything that doesn't depend on a specific mem_cgroup structure should
 * be initialized from here.
 */
static int __init mem_cgroup_init(void)
{
	hotcpu_notifier(memcg_cpu_hotplug_callback, 0);
	enable_swap_cgroup();
	mem_cgroup_soft_limit_tree_init();
	memcg_stock_init();
	return 0;
}
subsys_initcall(mem_cgroup_init);
