/* memcontrol.c - Memory Controller
 *
 * Copyright IBM Corporation, 2007
 * Author Balbir Singh <balbir@linux.vnet.ibm.com>
 *
 * Copyright 2007 OpenVZ SWsoft Inc
 * Author: Pavel Emelianov <xemul@openvz.org>
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
#include <linux/pagemap.h>
#include <linux/smp.h>
#include <linux/page-flags.h>
#include <linux/backing-dev.h>
#include <linux/bit_spinlock.h>
#include <linux/rcupdate.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/mm_inline.h>
#include <linux/page_cgroup.h>
#include "internal.h"

#include <asm/uaccess.h>

struct cgroup_subsys mem_cgroup_subsys __read_mostly;
#define MEM_CGROUP_RECLAIM_RETRIES	5

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
/* Turned on only when memory cgroup is enabled && really_do_swap_account = 0 */
int do_swap_account __read_mostly;
static int really_do_swap_account __initdata = 1; /* for remember boot option*/
#else
#define do_swap_account		(0)
#endif

static DEFINE_MUTEX(memcg_tasklist);	/* can be hold under cgroup_mutex */

/*
 * Statistics for memory cgroup.
 */
enum mem_cgroup_stat_index {
	/*
	 * For MEM_CONTAINER_TYPE_ALL, usage = pagecache + rss.
	 */
	MEM_CGROUP_STAT_CACHE, 	   /* # of pages charged as cache */
	MEM_CGROUP_STAT_RSS,	   /* # of pages charged as rss */
	MEM_CGROUP_STAT_PGPGIN_COUNT,	/* # of pages paged in */
	MEM_CGROUP_STAT_PGPGOUT_COUNT,	/* # of pages paged out */

	MEM_CGROUP_STAT_NSTATS,
};

struct mem_cgroup_stat_cpu {
	s64 count[MEM_CGROUP_STAT_NSTATS];
} ____cacheline_aligned_in_smp;

struct mem_cgroup_stat {
	struct mem_cgroup_stat_cpu cpustat[0];
};

/*
 * For accounting under irq disable, no need for increment preempt count.
 */
static inline void __mem_cgroup_stat_add_safe(struct mem_cgroup_stat_cpu *stat,
		enum mem_cgroup_stat_index idx, int val)
{
	stat->count[idx] += val;
}

static s64 mem_cgroup_read_stat(struct mem_cgroup_stat *stat,
		enum mem_cgroup_stat_index idx)
{
	int cpu;
	s64 ret = 0;
	for_each_possible_cpu(cpu)
		ret += stat->cpustat[cpu].count[idx];
	return ret;
}

static s64 mem_cgroup_local_usage(struct mem_cgroup_stat *stat)
{
	s64 ret;

	ret = mem_cgroup_read_stat(stat, MEM_CGROUP_STAT_CACHE);
	ret += mem_cgroup_read_stat(stat, MEM_CGROUP_STAT_RSS);
	return ret;
}

/*
 * per-zone information in memory controller.
 */
struct mem_cgroup_per_zone {
	/*
	 * spin_lock to protect the per cgroup LRU
	 */
	struct list_head	lists[NR_LRU_LISTS];
	unsigned long		count[NR_LRU_LISTS];

	struct zone_reclaim_stat reclaim_stat;
};
/* Macro for accessing counter */
#define MEM_CGROUP_ZSTAT(mz, idx)	((mz)->count[(idx)])

struct mem_cgroup_per_node {
	struct mem_cgroup_per_zone zoneinfo[MAX_NR_ZONES];
};

struct mem_cgroup_lru_info {
	struct mem_cgroup_per_node *nodeinfo[MAX_NUMNODES];
};

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
	/*
	 * the counter to account for mem+swap usage.
	 */
	struct res_counter memsw;
	/*
	 * Per cgroup active and inactive list, similar to the
	 * per zone LRU lists.
	 */
	struct mem_cgroup_lru_info info;

	/*
	  protect against reclaim related member.
	*/
	spinlock_t reclaim_param_lock;

	int	prev_priority;	/* for recording reclaim priority */

	/*
	 * While reclaiming in a hiearchy, we cache the last child we
	 * reclaimed from.
	 */
	int last_scanned_child;
	/*
	 * Should the accounting and control be hierarchical, per subtree?
	 */
	bool use_hierarchy;
	unsigned long	last_oom_jiffies;
	atomic_t	refcnt;

	unsigned int	swappiness;

	/*
	 * statistics. This must be placed at the end of memcg.
	 */
	struct mem_cgroup_stat stat;
};

enum charge_type {
	MEM_CGROUP_CHARGE_TYPE_CACHE = 0,
	MEM_CGROUP_CHARGE_TYPE_MAPPED,
	MEM_CGROUP_CHARGE_TYPE_SHMEM,	/* used by page migration of shmem */
	MEM_CGROUP_CHARGE_TYPE_FORCE,	/* used by force_empty */
	MEM_CGROUP_CHARGE_TYPE_SWAPOUT,	/* for accounting swapcache */
	NR_CHARGE_TYPE,
};

/* only for here (for easy reading.) */
#define PCGF_CACHE	(1UL << PCG_CACHE)
#define PCGF_USED	(1UL << PCG_USED)
#define PCGF_LOCK	(1UL << PCG_LOCK)
static const unsigned long
pcg_default_flags[NR_CHARGE_TYPE] = {
	PCGF_CACHE | PCGF_USED | PCGF_LOCK, /* File Cache */
	PCGF_USED | PCGF_LOCK, /* Anon */
	PCGF_CACHE | PCGF_USED | PCGF_LOCK, /* Shmem */
	0, /* FORCE */
};

/* for encoding cft->private value on file */
#define _MEM			(0)
#define _MEMSWAP		(1)
#define MEMFILE_PRIVATE(x, val)	(((x) << 16) | (val))
#define MEMFILE_TYPE(val)	(((val) >> 16) & 0xffff)
#define MEMFILE_ATTR(val)	((val) & 0xffff)

static void mem_cgroup_get(struct mem_cgroup *mem);
static void mem_cgroup_put(struct mem_cgroup *mem);
static struct mem_cgroup *parent_mem_cgroup(struct mem_cgroup *mem);

static void mem_cgroup_charge_statistics(struct mem_cgroup *mem,
					 struct page_cgroup *pc,
					 bool charge)
{
	int val = (charge)? 1 : -1;
	struct mem_cgroup_stat *stat = &mem->stat;
	struct mem_cgroup_stat_cpu *cpustat;
	int cpu = get_cpu();

	cpustat = &stat->cpustat[cpu];
	if (PageCgroupCache(pc))
		__mem_cgroup_stat_add_safe(cpustat, MEM_CGROUP_STAT_CACHE, val);
	else
		__mem_cgroup_stat_add_safe(cpustat, MEM_CGROUP_STAT_RSS, val);

	if (charge)
		__mem_cgroup_stat_add_safe(cpustat,
				MEM_CGROUP_STAT_PGPGIN_COUNT, 1);
	else
		__mem_cgroup_stat_add_safe(cpustat,
				MEM_CGROUP_STAT_PGPGOUT_COUNT, 1);
	put_cpu();
}

static struct mem_cgroup_per_zone *
mem_cgroup_zoneinfo(struct mem_cgroup *mem, int nid, int zid)
{
	return &mem->info.nodeinfo[nid]->zoneinfo[zid];
}

static struct mem_cgroup_per_zone *
page_cgroup_zoneinfo(struct page_cgroup *pc)
{
	struct mem_cgroup *mem = pc->mem_cgroup;
	int nid = page_cgroup_nid(pc);
	int zid = page_cgroup_zid(pc);

	if (!mem)
		return NULL;

	return mem_cgroup_zoneinfo(mem, nid, zid);
}

static unsigned long mem_cgroup_get_local_zonestat(struct mem_cgroup *mem,
					enum lru_list idx)
{
	int nid, zid;
	struct mem_cgroup_per_zone *mz;
	u64 total = 0;

	for_each_online_node(nid)
		for (zid = 0; zid < MAX_NR_ZONES; zid++) {
			mz = mem_cgroup_zoneinfo(mem, nid, zid);
			total += MEM_CGROUP_ZSTAT(mz, idx);
		}
	return total;
}

static struct mem_cgroup *mem_cgroup_from_cont(struct cgroup *cont)
{
	return container_of(cgroup_subsys_state(cont,
				mem_cgroup_subsys_id), struct mem_cgroup,
				css);
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

	return container_of(task_subsys_state(p, mem_cgroup_subsys_id),
				struct mem_cgroup, css);
}

static struct mem_cgroup *try_get_mem_cgroup_from_mm(struct mm_struct *mm)
{
	struct mem_cgroup *mem = NULL;

	if (!mm)
		return NULL;
	/*
	 * Because we have no locks, mm->owner's may be being moved to other
	 * cgroup. We use css_tryget() here even if this looks
	 * pessimistic (rather than adding locks here).
	 */
	rcu_read_lock();
	do {
		mem = mem_cgroup_from_task(rcu_dereference(mm->owner));
		if (unlikely(!mem))
			break;
	} while (!css_tryget(&mem->css));
	rcu_read_unlock();
	return mem;
}

static bool mem_cgroup_is_obsolete(struct mem_cgroup *mem)
{
	if (!mem)
		return true;
	return css_is_removed(&mem->css);
}


/*
 * Call callback function against all cgroup under hierarchy tree.
 */
static int mem_cgroup_walk_tree(struct mem_cgroup *root, void *data,
			  int (*func)(struct mem_cgroup *, void *))
{
	int found, ret, nextid;
	struct cgroup_subsys_state *css;
	struct mem_cgroup *mem;

	if (!root->use_hierarchy)
		return (*func)(root, data);

	nextid = 1;
	do {
		ret = 0;
		mem = NULL;

		rcu_read_lock();
		css = css_get_next(&mem_cgroup_subsys, nextid, &root->css,
				   &found);
		if (css && css_tryget(css))
			mem = container_of(css, struct mem_cgroup, css);
		rcu_read_unlock();

		if (mem) {
			ret = (*func)(mem, data);
			css_put(&mem->css);
		}
		nextid = found + 1;
	} while (!ret && css);

	return ret;
}

/*
 * Following LRU functions are allowed to be used without PCG_LOCK.
 * Operations are called by routine of global LRU independently from memcg.
 * What we have to take care of here is validness of pc->mem_cgroup.
 *
 * Changes to pc->mem_cgroup happens when
 * 1. charge
 * 2. moving account
 * In typical case, "charge" is done before add-to-lru. Exception is SwapCache.
 * It is added to LRU before charge.
 * If PCG_USED bit is not set, page_cgroup is not added to this private LRU.
 * When moving account, the page is not on LRU. It's isolated.
 */

void mem_cgroup_del_lru_list(struct page *page, enum lru_list lru)
{
	struct page_cgroup *pc;
	struct mem_cgroup *mem;
	struct mem_cgroup_per_zone *mz;

	if (mem_cgroup_disabled())
		return;
	pc = lookup_page_cgroup(page);
	/* can happen while we handle swapcache. */
	if (list_empty(&pc->lru) || !pc->mem_cgroup)
		return;
	/*
	 * We don't check PCG_USED bit. It's cleared when the "page" is finally
	 * removed from global LRU.
	 */
	mz = page_cgroup_zoneinfo(pc);
	mem = pc->mem_cgroup;
	MEM_CGROUP_ZSTAT(mz, lru) -= 1;
	list_del_init(&pc->lru);
	return;
}

void mem_cgroup_del_lru(struct page *page)
{
	mem_cgroup_del_lru_list(page, page_lru(page));
}

void mem_cgroup_rotate_lru_list(struct page *page, enum lru_list lru)
{
	struct mem_cgroup_per_zone *mz;
	struct page_cgroup *pc;

	if (mem_cgroup_disabled())
		return;

	pc = lookup_page_cgroup(page);
	/*
	 * Used bit is set without atomic ops but after smp_wmb().
	 * For making pc->mem_cgroup visible, insert smp_rmb() here.
	 */
	smp_rmb();
	/* unused page is not rotated. */
	if (!PageCgroupUsed(pc))
		return;
	mz = page_cgroup_zoneinfo(pc);
	list_move(&pc->lru, &mz->lists[lru]);
}

void mem_cgroup_add_lru_list(struct page *page, enum lru_list lru)
{
	struct page_cgroup *pc;
	struct mem_cgroup_per_zone *mz;

	if (mem_cgroup_disabled())
		return;
	pc = lookup_page_cgroup(page);
	/*
	 * Used bit is set without atomic ops but after smp_wmb().
	 * For making pc->mem_cgroup visible, insert smp_rmb() here.
	 */
	smp_rmb();
	if (!PageCgroupUsed(pc))
		return;

	mz = page_cgroup_zoneinfo(pc);
	MEM_CGROUP_ZSTAT(mz, lru) += 1;
	list_add(&pc->lru, &mz->lists[lru]);
}

/*
 * At handling SwapCache, pc->mem_cgroup may be changed while it's linked to
 * lru because the page may.be reused after it's fully uncharged (because of
 * SwapCache behavior).To handle that, unlink page_cgroup from LRU when charge
 * it again. This function is only used to charge SwapCache. It's done under
 * lock_page and expected that zone->lru_lock is never held.
 */
static void mem_cgroup_lru_del_before_commit_swapcache(struct page *page)
{
	unsigned long flags;
	struct zone *zone = page_zone(page);
	struct page_cgroup *pc = lookup_page_cgroup(page);

	spin_lock_irqsave(&zone->lru_lock, flags);
	/*
	 * Forget old LRU when this page_cgroup is *not* used. This Used bit
	 * is guarded by lock_page() because the page is SwapCache.
	 */
	if (!PageCgroupUsed(pc))
		mem_cgroup_del_lru_list(page, page_lru(page));
	spin_unlock_irqrestore(&zone->lru_lock, flags);
}

static void mem_cgroup_lru_add_after_commit_swapcache(struct page *page)
{
	unsigned long flags;
	struct zone *zone = page_zone(page);
	struct page_cgroup *pc = lookup_page_cgroup(page);

	spin_lock_irqsave(&zone->lru_lock, flags);
	/* link when the page is linked to LRU but page_cgroup isn't */
	if (PageLRU(page) && list_empty(&pc->lru))
		mem_cgroup_add_lru_list(page, page_lru(page));
	spin_unlock_irqrestore(&zone->lru_lock, flags);
}


void mem_cgroup_move_lists(struct page *page,
			   enum lru_list from, enum lru_list to)
{
	if (mem_cgroup_disabled())
		return;
	mem_cgroup_del_lru_list(page, from);
	mem_cgroup_add_lru_list(page, to);
}

int task_in_mem_cgroup(struct task_struct *task, const struct mem_cgroup *mem)
{
	int ret;
	struct mem_cgroup *curr = NULL;

	task_lock(task);
	rcu_read_lock();
	curr = try_get_mem_cgroup_from_mm(task->mm);
	rcu_read_unlock();
	task_unlock(task);
	if (!curr)
		return 0;
	if (curr->use_hierarchy)
		ret = css_is_ancestor(&curr->css, &mem->css);
	else
		ret = (curr == mem);
	css_put(&curr->css);
	return ret;
}

/*
 * prev_priority control...this will be used in memory reclaim path.
 */
int mem_cgroup_get_reclaim_priority(struct mem_cgroup *mem)
{
	int prev_priority;

	spin_lock(&mem->reclaim_param_lock);
	prev_priority = mem->prev_priority;
	spin_unlock(&mem->reclaim_param_lock);

	return prev_priority;
}

void mem_cgroup_note_reclaim_priority(struct mem_cgroup *mem, int priority)
{
	spin_lock(&mem->reclaim_param_lock);
	if (priority < mem->prev_priority)
		mem->prev_priority = priority;
	spin_unlock(&mem->reclaim_param_lock);
}

void mem_cgroup_record_reclaim_priority(struct mem_cgroup *mem, int priority)
{
	spin_lock(&mem->reclaim_param_lock);
	mem->prev_priority = priority;
	spin_unlock(&mem->reclaim_param_lock);
}

static int calc_inactive_ratio(struct mem_cgroup *memcg, unsigned long *present_pages)
{
	unsigned long active;
	unsigned long inactive;
	unsigned long gb;
	unsigned long inactive_ratio;

	inactive = mem_cgroup_get_local_zonestat(memcg, LRU_INACTIVE_ANON);
	active = mem_cgroup_get_local_zonestat(memcg, LRU_ACTIVE_ANON);

	gb = (inactive + active) >> (30 - PAGE_SHIFT);
	if (gb)
		inactive_ratio = int_sqrt(10 * gb);
	else
		inactive_ratio = 1;

	if (present_pages) {
		present_pages[0] = inactive;
		present_pages[1] = active;
	}

	return inactive_ratio;
}

int mem_cgroup_inactive_anon_is_low(struct mem_cgroup *memcg)
{
	unsigned long active;
	unsigned long inactive;
	unsigned long present_pages[2];
	unsigned long inactive_ratio;

	inactive_ratio = calc_inactive_ratio(memcg, present_pages);

	inactive = present_pages[0];
	active = present_pages[1];

	if (inactive * inactive_ratio < active)
		return 1;

	return 0;
}

unsigned long mem_cgroup_zone_nr_pages(struct mem_cgroup *memcg,
				       struct zone *zone,
				       enum lru_list lru)
{
	int nid = zone->zone_pgdat->node_id;
	int zid = zone_idx(zone);
	struct mem_cgroup_per_zone *mz = mem_cgroup_zoneinfo(memcg, nid, zid);

	return MEM_CGROUP_ZSTAT(mz, lru);
}

struct zone_reclaim_stat *mem_cgroup_get_reclaim_stat(struct mem_cgroup *memcg,
						      struct zone *zone)
{
	int nid = zone->zone_pgdat->node_id;
	int zid = zone_idx(zone);
	struct mem_cgroup_per_zone *mz = mem_cgroup_zoneinfo(memcg, nid, zid);

	return &mz->reclaim_stat;
}

struct zone_reclaim_stat *
mem_cgroup_get_reclaim_stat_from_page(struct page *page)
{
	struct page_cgroup *pc;
	struct mem_cgroup_per_zone *mz;

	if (mem_cgroup_disabled())
		return NULL;

	pc = lookup_page_cgroup(page);
	/*
	 * Used bit is set without atomic ops but after smp_wmb().
	 * For making pc->mem_cgroup visible, insert smp_rmb() here.
	 */
	smp_rmb();
	if (!PageCgroupUsed(pc))
		return NULL;

	mz = page_cgroup_zoneinfo(pc);
	if (!mz)
		return NULL;

	return &mz->reclaim_stat;
}

unsigned long mem_cgroup_isolate_pages(unsigned long nr_to_scan,
					struct list_head *dst,
					unsigned long *scanned, int order,
					int mode, struct zone *z,
					struct mem_cgroup *mem_cont,
					int active, int file)
{
	unsigned long nr_taken = 0;
	struct page *page;
	unsigned long scan;
	LIST_HEAD(pc_list);
	struct list_head *src;
	struct page_cgroup *pc, *tmp;
	int nid = z->zone_pgdat->node_id;
	int zid = zone_idx(z);
	struct mem_cgroup_per_zone *mz;
	int lru = LRU_FILE * !!file + !!active;

	BUG_ON(!mem_cont);
	mz = mem_cgroup_zoneinfo(mem_cont, nid, zid);
	src = &mz->lists[lru];

	scan = 0;
	list_for_each_entry_safe_reverse(pc, tmp, src, lru) {
		if (scan >= nr_to_scan)
			break;

		page = pc->page;
		if (unlikely(!PageCgroupUsed(pc)))
			continue;
		if (unlikely(!PageLRU(page)))
			continue;

		scan++;
		if (__isolate_lru_page(page, mode, file) == 0) {
			list_move(&page->lru, dst);
			nr_taken++;
		}
	}

	*scanned = scan;
	return nr_taken;
}

#define mem_cgroup_from_res_counter(counter, member)	\
	container_of(counter, struct mem_cgroup, member)

static bool mem_cgroup_check_under_limit(struct mem_cgroup *mem)
{
	if (do_swap_account) {
		if (res_counter_check_under_limit(&mem->res) &&
			res_counter_check_under_limit(&mem->memsw))
			return true;
	} else
		if (res_counter_check_under_limit(&mem->res))
			return true;
	return false;
}

static unsigned int get_swappiness(struct mem_cgroup *memcg)
{
	struct cgroup *cgrp = memcg->css.cgroup;
	unsigned int swappiness;

	/* root ? */
	if (cgrp->parent == NULL)
		return vm_swappiness;

	spin_lock(&memcg->reclaim_param_lock);
	swappiness = memcg->swappiness;
	spin_unlock(&memcg->reclaim_param_lock);

	return swappiness;
}

static int mem_cgroup_count_children_cb(struct mem_cgroup *mem, void *data)
{
	int *val = data;
	(*val)++;
	return 0;
}

/**
 * mem_cgroup_print_mem_info: Called from OOM with tasklist_lock held in read mode.
 * @memcg: The memory cgroup that went over limit
 * @p: Task that is going to be killed
 *
 * NOTE: @memcg and @p's mem_cgroup can be different when hierarchy is
 * enabled
 */
void mem_cgroup_print_oom_info(struct mem_cgroup *memcg, struct task_struct *p)
{
	struct cgroup *task_cgrp;
	struct cgroup *mem_cgrp;
	/*
	 * Need a buffer in BSS, can't rely on allocations. The code relies
	 * on the assumption that OOM is serialized for memory controller.
	 * If this assumption is broken, revisit this code.
	 */
	static char memcg_name[PATH_MAX];
	int ret;

	if (!memcg)
		return;


	rcu_read_lock();

	mem_cgrp = memcg->css.cgroup;
	task_cgrp = task_cgroup(p, mem_cgroup_subsys_id);

	ret = cgroup_path(task_cgrp, memcg_name, PATH_MAX);
	if (ret < 0) {
		/*
		 * Unfortunately, we are unable to convert to a useful name
		 * But we'll still print out the usage information
		 */
		rcu_read_unlock();
		goto done;
	}
	rcu_read_unlock();

	printk(KERN_INFO "Task in %s killed", memcg_name);

	rcu_read_lock();
	ret = cgroup_path(mem_cgrp, memcg_name, PATH_MAX);
	if (ret < 0) {
		rcu_read_unlock();
		goto done;
	}
	rcu_read_unlock();

	/*
	 * Continues from above, so we don't need an KERN_ level
	 */
	printk(KERN_CONT " as a result of limit of %s\n", memcg_name);
done:

	printk(KERN_INFO "memory: usage %llukB, limit %llukB, failcnt %llu\n",
		res_counter_read_u64(&memcg->res, RES_USAGE) >> 10,
		res_counter_read_u64(&memcg->res, RES_LIMIT) >> 10,
		res_counter_read_u64(&memcg->res, RES_FAILCNT));
	printk(KERN_INFO "memory+swap: usage %llukB, limit %llukB, "
		"failcnt %llu\n",
		res_counter_read_u64(&memcg->memsw, RES_USAGE) >> 10,
		res_counter_read_u64(&memcg->memsw, RES_LIMIT) >> 10,
		res_counter_read_u64(&memcg->memsw, RES_FAILCNT));
}

/*
 * This function returns the number of memcg under hierarchy tree. Returns
 * 1(self count) if no children.
 */
static int mem_cgroup_count_children(struct mem_cgroup *mem)
{
	int num = 0;
 	mem_cgroup_walk_tree(mem, &num, mem_cgroup_count_children_cb);
	return num;
}

/*
 * Visit the first child (need not be the first child as per the ordering
 * of the cgroup list, since we track last_scanned_child) of @mem and use
 * that to reclaim free pages from.
 */
static struct mem_cgroup *
mem_cgroup_select_victim(struct mem_cgroup *root_mem)
{
	struct mem_cgroup *ret = NULL;
	struct cgroup_subsys_state *css;
	int nextid, found;

	if (!root_mem->use_hierarchy) {
		css_get(&root_mem->css);
		ret = root_mem;
	}

	while (!ret) {
		rcu_read_lock();
		nextid = root_mem->last_scanned_child + 1;
		css = css_get_next(&mem_cgroup_subsys, nextid, &root_mem->css,
				   &found);
		if (css && css_tryget(css))
			ret = container_of(css, struct mem_cgroup, css);

		rcu_read_unlock();
		/* Updates scanning parameter */
		spin_lock(&root_mem->reclaim_param_lock);
		if (!css) {
			/* this means start scan from ID:1 */
			root_mem->last_scanned_child = 0;
		} else
			root_mem->last_scanned_child = found;
		spin_unlock(&root_mem->reclaim_param_lock);
	}

	return ret;
}

/*
 * Scan the hierarchy if needed to reclaim memory. We remember the last child
 * we reclaimed from, so that we don't end up penalizing one child extensively
 * based on its position in the children list.
 *
 * root_mem is the original ancestor that we've been reclaim from.
 *
 * We give up and return to the caller when we visit root_mem twice.
 * (other groups can be removed while we're walking....)
 *
 * If shrink==true, for avoiding to free too much, this returns immedieately.
 */
static int mem_cgroup_hierarchical_reclaim(struct mem_cgroup *root_mem,
				   gfp_t gfp_mask, bool noswap, bool shrink)
{
	struct mem_cgroup *victim;
	int ret, total = 0;
	int loop = 0;

	while (loop < 2) {
		victim = mem_cgroup_select_victim(root_mem);
		if (victim == root_mem)
			loop++;
		if (!mem_cgroup_local_usage(&victim->stat)) {
			/* this cgroup's local usage == 0 */
			css_put(&victim->css);
			continue;
		}
		/* we use swappiness of local cgroup */
		ret = try_to_free_mem_cgroup_pages(victim, gfp_mask, noswap,
						   get_swappiness(victim));
		css_put(&victim->css);
		/*
		 * At shrinking usage, we can't check we should stop here or
		 * reclaim more. It's depends on callers. last_scanned_child
		 * will work enough for keeping fairness under tree.
		 */
		if (shrink)
			return ret;
		total += ret;
		if (mem_cgroup_check_under_limit(root_mem))
			return 1 + total;
	}
	return total;
}

bool mem_cgroup_oom_called(struct task_struct *task)
{
	bool ret = false;
	struct mem_cgroup *mem;
	struct mm_struct *mm;

	rcu_read_lock();
	mm = task->mm;
	if (!mm)
		mm = &init_mm;
	mem = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (mem && time_before(jiffies, mem->last_oom_jiffies + HZ/10))
		ret = true;
	rcu_read_unlock();
	return ret;
}

static int record_last_oom_cb(struct mem_cgroup *mem, void *data)
{
	mem->last_oom_jiffies = jiffies;
	return 0;
}

static void record_last_oom(struct mem_cgroup *mem)
{
	mem_cgroup_walk_tree(mem, NULL, record_last_oom_cb);
}


/*
 * Unlike exported interface, "oom" parameter is added. if oom==true,
 * oom-killer can be invoked.
 */
static int __mem_cgroup_try_charge(struct mm_struct *mm,
			gfp_t gfp_mask, struct mem_cgroup **memcg,
			bool oom)
{
	struct mem_cgroup *mem, *mem_over_limit;
	int nr_retries = MEM_CGROUP_RECLAIM_RETRIES;
	struct res_counter *fail_res;

	if (unlikely(test_thread_flag(TIF_MEMDIE))) {
		/* Don't account this! */
		*memcg = NULL;
		return 0;
	}

	/*
	 * We always charge the cgroup the mm_struct belongs to.
	 * The mm_struct's mem_cgroup changes on task migration if the
	 * thread group leader migrates. It's possible that mm is not
	 * set, if so charge the init_mm (happens for pagecache usage).
	 */
	mem = *memcg;
	if (likely(!mem)) {
		mem = try_get_mem_cgroup_from_mm(mm);
		*memcg = mem;
	} else {
		css_get(&mem->css);
	}
	if (unlikely(!mem))
		return 0;

	VM_BUG_ON(!mem || mem_cgroup_is_obsolete(mem));

	while (1) {
		int ret;
		bool noswap = false;

		ret = res_counter_charge(&mem->res, PAGE_SIZE, &fail_res);
		if (likely(!ret)) {
			if (!do_swap_account)
				break;
			ret = res_counter_charge(&mem->memsw, PAGE_SIZE,
							&fail_res);
			if (likely(!ret))
				break;
			/* mem+swap counter fails */
			res_counter_uncharge(&mem->res, PAGE_SIZE);
			noswap = true;
			mem_over_limit = mem_cgroup_from_res_counter(fail_res,
									memsw);
		} else
			/* mem counter fails */
			mem_over_limit = mem_cgroup_from_res_counter(fail_res,
									res);

		if (!(gfp_mask & __GFP_WAIT))
			goto nomem;

		ret = mem_cgroup_hierarchical_reclaim(mem_over_limit, gfp_mask,
							noswap, false);
		if (ret)
			continue;

		/*
		 * try_to_free_mem_cgroup_pages() might not give us a full
		 * picture of reclaim. Some pages are reclaimed and might be
		 * moved to swap cache or just unmapped from the cgroup.
		 * Check the limit again to see if the reclaim reduced the
		 * current usage of the cgroup before giving up
		 *
		 */
		if (mem_cgroup_check_under_limit(mem_over_limit))
			continue;

		if (!nr_retries--) {
			if (oom) {
				mutex_lock(&memcg_tasklist);
				mem_cgroup_out_of_memory(mem_over_limit, gfp_mask);
				mutex_unlock(&memcg_tasklist);
				record_last_oom(mem_over_limit);
			}
			goto nomem;
		}
	}
	return 0;
nomem:
	css_put(&mem->css);
	return -ENOMEM;
}


/*
 * A helper function to get mem_cgroup from ID. must be called under
 * rcu_read_lock(). The caller must check css_is_removed() or some if
 * it's concern. (dropping refcnt from swap can be called against removed
 * memcg.)
 */
static struct mem_cgroup *mem_cgroup_lookup(unsigned short id)
{
	struct cgroup_subsys_state *css;

	/* ID 0 is unused ID */
	if (!id)
		return NULL;
	css = css_lookup(&mem_cgroup_subsys, id);
	if (!css)
		return NULL;
	return container_of(css, struct mem_cgroup, css);
}

static struct mem_cgroup *try_get_mem_cgroup_from_swapcache(struct page *page)
{
	struct mem_cgroup *mem;
	struct page_cgroup *pc;
	unsigned short id;
	swp_entry_t ent;

	VM_BUG_ON(!PageLocked(page));

	if (!PageSwapCache(page))
		return NULL;

	pc = lookup_page_cgroup(page);
	lock_page_cgroup(pc);
	if (PageCgroupUsed(pc)) {
		mem = pc->mem_cgroup;
		if (mem && !css_tryget(&mem->css))
			mem = NULL;
	} else {
		ent.val = page_private(page);
		id = lookup_swap_cgroup(ent);
		rcu_read_lock();
		mem = mem_cgroup_lookup(id);
		if (mem && !css_tryget(&mem->css))
			mem = NULL;
		rcu_read_unlock();
	}
	unlock_page_cgroup(pc);
	return mem;
}

/*
 * commit a charge got by __mem_cgroup_try_charge() and makes page_cgroup to be
 * USED state. If already USED, uncharge and return.
 */

static void __mem_cgroup_commit_charge(struct mem_cgroup *mem,
				     struct page_cgroup *pc,
				     enum charge_type ctype)
{
	/* try_charge() can return NULL to *memcg, taking care of it. */
	if (!mem)
		return;

	lock_page_cgroup(pc);
	if (unlikely(PageCgroupUsed(pc))) {
		unlock_page_cgroup(pc);
		res_counter_uncharge(&mem->res, PAGE_SIZE);
		if (do_swap_account)
			res_counter_uncharge(&mem->memsw, PAGE_SIZE);
		css_put(&mem->css);
		return;
	}
	pc->mem_cgroup = mem;
	smp_wmb();
	pc->flags = pcg_default_flags[ctype];

	mem_cgroup_charge_statistics(mem, pc, true);

	unlock_page_cgroup(pc);
}

/**
 * mem_cgroup_move_account - move account of the page
 * @pc:	page_cgroup of the page.
 * @from: mem_cgroup which the page is moved from.
 * @to:	mem_cgroup which the page is moved to. @from != @to.
 *
 * The caller must confirm following.
 * - page is not on LRU (isolate_page() is useful.)
 *
 * returns 0 at success,
 * returns -EBUSY when lock is busy or "pc" is unstable.
 *
 * This function does "uncharge" from old cgroup but doesn't do "charge" to
 * new cgroup. It should be done by a caller.
 */

static int mem_cgroup_move_account(struct page_cgroup *pc,
	struct mem_cgroup *from, struct mem_cgroup *to)
{
	struct mem_cgroup_per_zone *from_mz, *to_mz;
	int nid, zid;
	int ret = -EBUSY;

	VM_BUG_ON(from == to);
	VM_BUG_ON(PageLRU(pc->page));

	nid = page_cgroup_nid(pc);
	zid = page_cgroup_zid(pc);
	from_mz =  mem_cgroup_zoneinfo(from, nid, zid);
	to_mz =  mem_cgroup_zoneinfo(to, nid, zid);

	if (!trylock_page_cgroup(pc))
		return ret;

	if (!PageCgroupUsed(pc))
		goto out;

	if (pc->mem_cgroup != from)
		goto out;

	res_counter_uncharge(&from->res, PAGE_SIZE);
	mem_cgroup_charge_statistics(from, pc, false);
	if (do_swap_account)
		res_counter_uncharge(&from->memsw, PAGE_SIZE);
	css_put(&from->css);

	css_get(&to->css);
	pc->mem_cgroup = to;
	mem_cgroup_charge_statistics(to, pc, true);
	ret = 0;
out:
	unlock_page_cgroup(pc);
	return ret;
}

/*
 * move charges to its parent.
 */

static int mem_cgroup_move_parent(struct page_cgroup *pc,
				  struct mem_cgroup *child,
				  gfp_t gfp_mask)
{
	struct page *page = pc->page;
	struct cgroup *cg = child->css.cgroup;
	struct cgroup *pcg = cg->parent;
	struct mem_cgroup *parent;
	int ret;

	/* Is ROOT ? */
	if (!pcg)
		return -EINVAL;


	parent = mem_cgroup_from_cont(pcg);


	ret = __mem_cgroup_try_charge(NULL, gfp_mask, &parent, false);
	if (ret || !parent)
		return ret;

	if (!get_page_unless_zero(page)) {
		ret = -EBUSY;
		goto uncharge;
	}

	ret = isolate_lru_page(page);

	if (ret)
		goto cancel;

	ret = mem_cgroup_move_account(pc, child, parent);

	putback_lru_page(page);
	if (!ret) {
		put_page(page);
		/* drop extra refcnt by try_charge() */
		css_put(&parent->css);
		return 0;
	}

cancel:
	put_page(page);
uncharge:
	/* drop extra refcnt by try_charge() */
	css_put(&parent->css);
	/* uncharge if move fails */
	res_counter_uncharge(&parent->res, PAGE_SIZE);
	if (do_swap_account)
		res_counter_uncharge(&parent->memsw, PAGE_SIZE);
	return ret;
}

/*
 * Charge the memory controller for page usage.
 * Return
 * 0 if the charge was successful
 * < 0 if the cgroup is over its limit
 */
static int mem_cgroup_charge_common(struct page *page, struct mm_struct *mm,
				gfp_t gfp_mask, enum charge_type ctype,
				struct mem_cgroup *memcg)
{
	struct mem_cgroup *mem;
	struct page_cgroup *pc;
	int ret;

	pc = lookup_page_cgroup(page);
	/* can happen at boot */
	if (unlikely(!pc))
		return 0;
	prefetchw(pc);

	mem = memcg;
	ret = __mem_cgroup_try_charge(mm, gfp_mask, &mem, true);
	if (ret || !mem)
		return ret;

	__mem_cgroup_commit_charge(mem, pc, ctype);
	return 0;
}

int mem_cgroup_newpage_charge(struct page *page,
			      struct mm_struct *mm, gfp_t gfp_mask)
{
	if (mem_cgroup_disabled())
		return 0;
	if (PageCompound(page))
		return 0;
	/*
	 * If already mapped, we don't have to account.
	 * If page cache, page->mapping has address_space.
	 * But page->mapping may have out-of-use anon_vma pointer,
	 * detecit it by PageAnon() check. newly-mapped-anon's page->mapping
	 * is NULL.
  	 */
	if (page_mapped(page) || (page->mapping && !PageAnon(page)))
		return 0;
	if (unlikely(!mm))
		mm = &init_mm;
	return mem_cgroup_charge_common(page, mm, gfp_mask,
				MEM_CGROUP_CHARGE_TYPE_MAPPED, NULL);
}

static void
__mem_cgroup_commit_charge_swapin(struct page *page, struct mem_cgroup *ptr,
					enum charge_type ctype);

int mem_cgroup_cache_charge(struct page *page, struct mm_struct *mm,
				gfp_t gfp_mask)
{
	struct mem_cgroup *mem = NULL;
	int ret;

	if (mem_cgroup_disabled())
		return 0;
	if (PageCompound(page))
		return 0;
	/*
	 * Corner case handling. This is called from add_to_page_cache()
	 * in usual. But some FS (shmem) precharges this page before calling it
	 * and call add_to_page_cache() with GFP_NOWAIT.
	 *
	 * For GFP_NOWAIT case, the page may be pre-charged before calling
	 * add_to_page_cache(). (See shmem.c) check it here and avoid to call
	 * charge twice. (It works but has to pay a bit larger cost.)
	 * And when the page is SwapCache, it should take swap information
	 * into account. This is under lock_page() now.
	 */
	if (!(gfp_mask & __GFP_WAIT)) {
		struct page_cgroup *pc;


		pc = lookup_page_cgroup(page);
		if (!pc)
			return 0;
		lock_page_cgroup(pc);
		if (PageCgroupUsed(pc)) {
			unlock_page_cgroup(pc);
			return 0;
		}
		unlock_page_cgroup(pc);
	}

	if (unlikely(!mm && !mem))
		mm = &init_mm;

	if (page_is_file_cache(page))
		return mem_cgroup_charge_common(page, mm, gfp_mask,
				MEM_CGROUP_CHARGE_TYPE_CACHE, NULL);

	/* shmem */
	if (PageSwapCache(page)) {
		ret = mem_cgroup_try_charge_swapin(mm, page, gfp_mask, &mem);
		if (!ret)
			__mem_cgroup_commit_charge_swapin(page, mem,
					MEM_CGROUP_CHARGE_TYPE_SHMEM);
	} else
		ret = mem_cgroup_charge_common(page, mm, gfp_mask,
					MEM_CGROUP_CHARGE_TYPE_SHMEM, mem);

	return ret;
}

/*
 * While swap-in, try_charge -> commit or cancel, the page is locked.
 * And when try_charge() successfully returns, one refcnt to memcg without
 * struct page_cgroup is aquired. This refcnt will be cumsumed by
 * "commit()" or removed by "cancel()"
 */
int mem_cgroup_try_charge_swapin(struct mm_struct *mm,
				 struct page *page,
				 gfp_t mask, struct mem_cgroup **ptr)
{
	struct mem_cgroup *mem;
	int ret;

	if (mem_cgroup_disabled())
		return 0;

	if (!do_swap_account)
		goto charge_cur_mm;
	/*
	 * A racing thread's fault, or swapoff, may have already updated
	 * the pte, and even removed page from swap cache: return success
	 * to go on to do_swap_page()'s pte_same() test, which should fail.
	 */
	if (!PageSwapCache(page))
		return 0;
	mem = try_get_mem_cgroup_from_swapcache(page);
	if (!mem)
		goto charge_cur_mm;
	*ptr = mem;
	ret = __mem_cgroup_try_charge(NULL, mask, ptr, true);
	/* drop extra refcnt from tryget */
	css_put(&mem->css);
	return ret;
charge_cur_mm:
	if (unlikely(!mm))
		mm = &init_mm;
	return __mem_cgroup_try_charge(mm, mask, ptr, true);
}

static void
__mem_cgroup_commit_charge_swapin(struct page *page, struct mem_cgroup *ptr,
					enum charge_type ctype)
{
	struct page_cgroup *pc;

	if (mem_cgroup_disabled())
		return;
	if (!ptr)
		return;
	pc = lookup_page_cgroup(page);
	mem_cgroup_lru_del_before_commit_swapcache(page);
	__mem_cgroup_commit_charge(ptr, pc, ctype);
	mem_cgroup_lru_add_after_commit_swapcache(page);
	/*
	 * Now swap is on-memory. This means this page may be
	 * counted both as mem and swap....double count.
	 * Fix it by uncharging from memsw. Basically, this SwapCache is stable
	 * under lock_page(). But in do_swap_page()::memory.c, reuse_swap_page()
	 * may call delete_from_swap_cache() before reach here.
	 */
	if (do_swap_account && PageSwapCache(page)) {
		swp_entry_t ent = {.val = page_private(page)};
		unsigned short id;
		struct mem_cgroup *memcg;

		id = swap_cgroup_record(ent, 0);
		rcu_read_lock();
		memcg = mem_cgroup_lookup(id);
		if (memcg) {
			/*
			 * This recorded memcg can be obsolete one. So, avoid
			 * calling css_tryget
			 */
			res_counter_uncharge(&memcg->memsw, PAGE_SIZE);
			mem_cgroup_put(memcg);
		}
		rcu_read_unlock();
	}
	/* add this page(page_cgroup) to the LRU we want. */

}

void mem_cgroup_commit_charge_swapin(struct page *page, struct mem_cgroup *ptr)
{
	__mem_cgroup_commit_charge_swapin(page, ptr,
					MEM_CGROUP_CHARGE_TYPE_MAPPED);
}

void mem_cgroup_cancel_charge_swapin(struct mem_cgroup *mem)
{
	if (mem_cgroup_disabled())
		return;
	if (!mem)
		return;
	res_counter_uncharge(&mem->res, PAGE_SIZE);
	if (do_swap_account)
		res_counter_uncharge(&mem->memsw, PAGE_SIZE);
	css_put(&mem->css);
}


/*
 * uncharge if !page_mapped(page)
 */
static struct mem_cgroup *
__mem_cgroup_uncharge_common(struct page *page, enum charge_type ctype)
{
	struct page_cgroup *pc;
	struct mem_cgroup *mem = NULL;
	struct mem_cgroup_per_zone *mz;

	if (mem_cgroup_disabled())
		return NULL;

	if (PageSwapCache(page))
		return NULL;

	/*
	 * Check if our page_cgroup is valid
	 */
	pc = lookup_page_cgroup(page);
	if (unlikely(!pc || !PageCgroupUsed(pc)))
		return NULL;

	lock_page_cgroup(pc);

	mem = pc->mem_cgroup;

	if (!PageCgroupUsed(pc))
		goto unlock_out;

	switch (ctype) {
	case MEM_CGROUP_CHARGE_TYPE_MAPPED:
		if (page_mapped(page))
			goto unlock_out;
		break;
	case MEM_CGROUP_CHARGE_TYPE_SWAPOUT:
		if (!PageAnon(page)) {	/* Shared memory */
			if (page->mapping && !page_is_file_cache(page))
				goto unlock_out;
		} else if (page_mapped(page)) /* Anon */
				goto unlock_out;
		break;
	default:
		break;
	}

	res_counter_uncharge(&mem->res, PAGE_SIZE);
	if (do_swap_account && (ctype != MEM_CGROUP_CHARGE_TYPE_SWAPOUT))
		res_counter_uncharge(&mem->memsw, PAGE_SIZE);
	mem_cgroup_charge_statistics(mem, pc, false);

	ClearPageCgroupUsed(pc);
	/*
	 * pc->mem_cgroup is not cleared here. It will be accessed when it's
	 * freed from LRU. This is safe because uncharged page is expected not
	 * to be reused (freed soon). Exception is SwapCache, it's handled by
	 * special functions.
	 */

	mz = page_cgroup_zoneinfo(pc);
	unlock_page_cgroup(pc);

	/* at swapout, this memcg will be accessed to record to swap */
	if (ctype != MEM_CGROUP_CHARGE_TYPE_SWAPOUT)
		css_put(&mem->css);

	return mem;

unlock_out:
	unlock_page_cgroup(pc);
	return NULL;
}

void mem_cgroup_uncharge_page(struct page *page)
{
	/* early check. */
	if (page_mapped(page))
		return;
	if (page->mapping && !PageAnon(page))
		return;
	__mem_cgroup_uncharge_common(page, MEM_CGROUP_CHARGE_TYPE_MAPPED);
}

void mem_cgroup_uncharge_cache_page(struct page *page)
{
	VM_BUG_ON(page_mapped(page));
	VM_BUG_ON(page->mapping);
	__mem_cgroup_uncharge_common(page, MEM_CGROUP_CHARGE_TYPE_CACHE);
}

/*
 * called from __delete_from_swap_cache() and drop "page" account.
 * memcg information is recorded to swap_cgroup of "ent"
 */
void mem_cgroup_uncharge_swapcache(struct page *page, swp_entry_t ent)
{
	struct mem_cgroup *memcg;

	memcg = __mem_cgroup_uncharge_common(page,
					MEM_CGROUP_CHARGE_TYPE_SWAPOUT);
	/* record memcg information */
	if (do_swap_account && memcg) {
		swap_cgroup_record(ent, css_id(&memcg->css));
		mem_cgroup_get(memcg);
	}
	if (memcg)
		css_put(&memcg->css);
}

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
/*
 * called from swap_entry_free(). remove record in swap_cgroup and
 * uncharge "memsw" account.
 */
void mem_cgroup_uncharge_swap(swp_entry_t ent)
{
	struct mem_cgroup *memcg;
	unsigned short id;

	if (!do_swap_account)
		return;

	id = swap_cgroup_record(ent, 0);
	rcu_read_lock();
	memcg = mem_cgroup_lookup(id);
	if (memcg) {
		/*
		 * We uncharge this because swap is freed.
		 * This memcg can be obsolete one. We avoid calling css_tryget
		 */
		res_counter_uncharge(&memcg->memsw, PAGE_SIZE);
		mem_cgroup_put(memcg);
	}
	rcu_read_unlock();
}
#endif

/*
 * Before starting migration, account PAGE_SIZE to mem_cgroup that the old
 * page belongs to.
 */
int mem_cgroup_prepare_migration(struct page *page, struct mem_cgroup **ptr)
{
	struct page_cgroup *pc;
	struct mem_cgroup *mem = NULL;
	int ret = 0;

	if (mem_cgroup_disabled())
		return 0;

	pc = lookup_page_cgroup(page);
	lock_page_cgroup(pc);
	if (PageCgroupUsed(pc)) {
		mem = pc->mem_cgroup;
		css_get(&mem->css);
	}
	unlock_page_cgroup(pc);

	if (mem) {
		ret = __mem_cgroup_try_charge(NULL, GFP_KERNEL, &mem, false);
		css_put(&mem->css);
	}
	*ptr = mem;
	return ret;
}

/* remove redundant charge if migration failed*/
void mem_cgroup_end_migration(struct mem_cgroup *mem,
		struct page *oldpage, struct page *newpage)
{
	struct page *target, *unused;
	struct page_cgroup *pc;
	enum charge_type ctype;

	if (!mem)
		return;

	/* at migration success, oldpage->mapping is NULL. */
	if (oldpage->mapping) {
		target = oldpage;
		unused = NULL;
	} else {
		target = newpage;
		unused = oldpage;
	}

	if (PageAnon(target))
		ctype = MEM_CGROUP_CHARGE_TYPE_MAPPED;
	else if (page_is_file_cache(target))
		ctype = MEM_CGROUP_CHARGE_TYPE_CACHE;
	else
		ctype = MEM_CGROUP_CHARGE_TYPE_SHMEM;

	/* unused page is not on radix-tree now. */
	if (unused)
		__mem_cgroup_uncharge_common(unused, ctype);

	pc = lookup_page_cgroup(target);
	/*
	 * __mem_cgroup_commit_charge() check PCG_USED bit of page_cgroup.
	 * So, double-counting is effectively avoided.
	 */
	__mem_cgroup_commit_charge(mem, pc, ctype);

	/*
	 * Both of oldpage and newpage are still under lock_page().
	 * Then, we don't have to care about race in radix-tree.
	 * But we have to be careful that this page is unmapped or not.
	 *
	 * There is a case for !page_mapped(). At the start of
	 * migration, oldpage was mapped. But now, it's zapped.
	 * But we know *target* page is not freed/reused under us.
	 * mem_cgroup_uncharge_page() does all necessary checks.
	 */
	if (ctype == MEM_CGROUP_CHARGE_TYPE_MAPPED)
		mem_cgroup_uncharge_page(target);
}

/*
 * A call to try to shrink memory usage on charge failure at shmem's swapin.
 * Calling hierarchical_reclaim is not enough because we should update
 * last_oom_jiffies to prevent pagefault_out_of_memory from invoking global OOM.
 * Moreover considering hierarchy, we should reclaim from the mem_over_limit,
 * not from the memcg which this page would be charged to.
 * try_charge_swapin does all of these works properly.
 */
int mem_cgroup_shmem_charge_fallback(struct page *page,
			    struct mm_struct *mm,
			    gfp_t gfp_mask)
{
	struct mem_cgroup *mem = NULL;
	int ret;

	if (mem_cgroup_disabled())
		return 0;

	ret = mem_cgroup_try_charge_swapin(mm, page, gfp_mask, &mem);
	if (!ret)
		mem_cgroup_cancel_charge_swapin(mem); /* it does !mem check */

	return ret;
}

static DEFINE_MUTEX(set_limit_mutex);

static int mem_cgroup_resize_limit(struct mem_cgroup *memcg,
				unsigned long long val)
{
	int retry_count;
	int progress;
	u64 memswlimit;
	int ret = 0;
	int children = mem_cgroup_count_children(memcg);
	u64 curusage, oldusage;

	/*
	 * For keeping hierarchical_reclaim simple, how long we should retry
	 * is depends on callers. We set our retry-count to be function
	 * of # of children which we should visit in this loop.
	 */
	retry_count = MEM_CGROUP_RECLAIM_RETRIES * children;

	oldusage = res_counter_read_u64(&memcg->res, RES_USAGE);

	while (retry_count) {
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
		/*
		 * Rather than hide all in some function, I do this in
		 * open coded manner. You see what this really does.
		 * We have to guarantee mem->res.limit < mem->memsw.limit.
		 */
		mutex_lock(&set_limit_mutex);
		memswlimit = res_counter_read_u64(&memcg->memsw, RES_LIMIT);
		if (memswlimit < val) {
			ret = -EINVAL;
			mutex_unlock(&set_limit_mutex);
			break;
		}
		ret = res_counter_set_limit(&memcg->res, val);
		mutex_unlock(&set_limit_mutex);

		if (!ret)
			break;

		progress = mem_cgroup_hierarchical_reclaim(memcg, GFP_KERNEL,
						   false, true);
		curusage = res_counter_read_u64(&memcg->res, RES_USAGE);
		/* Usage is reduced ? */
  		if (curusage >= oldusage)
			retry_count--;
		else
			oldusage = curusage;
	}

	return ret;
}

int mem_cgroup_resize_memsw_limit(struct mem_cgroup *memcg,
				unsigned long long val)
{
	int retry_count;
	u64 memlimit, oldusage, curusage;
	int children = mem_cgroup_count_children(memcg);
	int ret = -EBUSY;

	if (!do_swap_account)
		return -EINVAL;
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
		 * We have to guarantee mem->res.limit < mem->memsw.limit.
		 */
		mutex_lock(&set_limit_mutex);
		memlimit = res_counter_read_u64(&memcg->res, RES_LIMIT);
		if (memlimit > val) {
			ret = -EINVAL;
			mutex_unlock(&set_limit_mutex);
			break;
		}
		ret = res_counter_set_limit(&memcg->memsw, val);
		mutex_unlock(&set_limit_mutex);

		if (!ret)
			break;

		mem_cgroup_hierarchical_reclaim(memcg, GFP_KERNEL, true, true);
		curusage = res_counter_read_u64(&memcg->memsw, RES_USAGE);
		/* Usage is reduced ? */
		if (curusage >= oldusage)
			retry_count--;
		else
			oldusage = curusage;
	}
	return ret;
}

/*
 * This routine traverse page_cgroup in given list and drop them all.
 * *And* this routine doesn't reclaim page itself, just removes page_cgroup.
 */
static int mem_cgroup_force_empty_list(struct mem_cgroup *mem,
				int node, int zid, enum lru_list lru)
{
	struct zone *zone;
	struct mem_cgroup_per_zone *mz;
	struct page_cgroup *pc, *busy;
	unsigned long flags, loop;
	struct list_head *list;
	int ret = 0;

	zone = &NODE_DATA(node)->node_zones[zid];
	mz = mem_cgroup_zoneinfo(mem, node, zid);
	list = &mz->lists[lru];

	loop = MEM_CGROUP_ZSTAT(mz, lru);
	/* give some margin against EBUSY etc...*/
	loop += 256;
	busy = NULL;
	while (loop--) {
		ret = 0;
		spin_lock_irqsave(&zone->lru_lock, flags);
		if (list_empty(list)) {
			spin_unlock_irqrestore(&zone->lru_lock, flags);
			break;
		}
		pc = list_entry(list->prev, struct page_cgroup, lru);
		if (busy == pc) {
			list_move(&pc->lru, list);
			busy = 0;
			spin_unlock_irqrestore(&zone->lru_lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&zone->lru_lock, flags);

		ret = mem_cgroup_move_parent(pc, mem, GFP_KERNEL);
		if (ret == -ENOMEM)
			break;

		if (ret == -EBUSY || ret == -EINVAL) {
			/* found lock contention or "pc" is obsolete. */
			busy = pc;
			cond_resched();
		} else
			busy = NULL;
	}

	if (!ret && !list_empty(list))
		return -EBUSY;
	return ret;
}

/*
 * make mem_cgroup's charge to be 0 if there is no task.
 * This enables deleting this mem_cgroup.
 */
static int mem_cgroup_force_empty(struct mem_cgroup *mem, bool free_all)
{
	int ret;
	int node, zid, shrink;
	int nr_retries = MEM_CGROUP_RECLAIM_RETRIES;
	struct cgroup *cgrp = mem->css.cgroup;

	css_get(&mem->css);

	shrink = 0;
	/* should free all ? */
	if (free_all)
		goto try_to_free;
move_account:
	while (mem->res.usage > 0) {
		ret = -EBUSY;
		if (cgroup_task_count(cgrp) || !list_empty(&cgrp->children))
			goto out;
		ret = -EINTR;
		if (signal_pending(current))
			goto out;
		/* This is for making all *used* pages to be on LRU. */
		lru_add_drain_all();
		ret = 0;
		for_each_node_state(node, N_HIGH_MEMORY) {
			for (zid = 0; !ret && zid < MAX_NR_ZONES; zid++) {
				enum lru_list l;
				for_each_lru(l) {
					ret = mem_cgroup_force_empty_list(mem,
							node, zid, l);
					if (ret)
						break;
				}
			}
			if (ret)
				break;
		}
		/* it seems parent cgroup doesn't have enough mem */
		if (ret == -ENOMEM)
			goto try_to_free;
		cond_resched();
	}
	ret = 0;
out:
	css_put(&mem->css);
	return ret;

try_to_free:
	/* returns EBUSY if there is a task or if we come here twice. */
	if (cgroup_task_count(cgrp) || !list_empty(&cgrp->children) || shrink) {
		ret = -EBUSY;
		goto out;
	}
	/* we call try-to-free pages for make this cgroup empty */
	lru_add_drain_all();
	/* try to free all pages in this cgroup */
	shrink = 1;
	while (nr_retries && mem->res.usage > 0) {
		int progress;

		if (signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
		progress = try_to_free_mem_cgroup_pages(mem, GFP_KERNEL,
						false, get_swappiness(mem));
		if (!progress) {
			nr_retries--;
			/* maybe some writeback is necessary */
			congestion_wait(WRITE, HZ/10);
		}

	}
	lru_add_drain();
	/* try move_account...there may be some *locked* pages. */
	if (mem->res.usage)
		goto move_account;
	ret = 0;
	goto out;
}

int mem_cgroup_force_empty_write(struct cgroup *cont, unsigned int event)
{
	return mem_cgroup_force_empty(mem_cgroup_from_cont(cont), true);
}


static u64 mem_cgroup_hierarchy_read(struct cgroup *cont, struct cftype *cft)
{
	return mem_cgroup_from_cont(cont)->use_hierarchy;
}

static int mem_cgroup_hierarchy_write(struct cgroup *cont, struct cftype *cft,
					u64 val)
{
	int retval = 0;
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);
	struct cgroup *parent = cont->parent;
	struct mem_cgroup *parent_mem = NULL;

	if (parent)
		parent_mem = mem_cgroup_from_cont(parent);

	cgroup_lock();
	/*
	 * If parent's use_hiearchy is set, we can't make any modifications
	 * in the child subtrees. If it is unset, then the change can
	 * occur, provided the current cgroup has no children.
	 *
	 * For the root cgroup, parent_mem is NULL, we allow value to be
	 * set if there are no children.
	 */
	if ((!parent_mem || !parent_mem->use_hierarchy) &&
				(val == 1 || val == 0)) {
		if (list_empty(&cont->children))
			mem->use_hierarchy = val;
		else
			retval = -EBUSY;
	} else
		retval = -EINVAL;
	cgroup_unlock();

	return retval;
}

static u64 mem_cgroup_read(struct cgroup *cont, struct cftype *cft)
{
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);
	u64 val = 0;
	int type, name;

	type = MEMFILE_TYPE(cft->private);
	name = MEMFILE_ATTR(cft->private);
	switch (type) {
	case _MEM:
		val = res_counter_read_u64(&mem->res, name);
		break;
	case _MEMSWAP:
		if (do_swap_account)
			val = res_counter_read_u64(&mem->memsw, name);
		break;
	default:
		BUG();
		break;
	}
	return val;
}
/*
 * The user of this function is...
 * RES_LIMIT.
 */
static int mem_cgroup_write(struct cgroup *cont, struct cftype *cft,
			    const char *buffer)
{
	struct mem_cgroup *memcg = mem_cgroup_from_cont(cont);
	int type, name;
	unsigned long long val;
	int ret;

	type = MEMFILE_TYPE(cft->private);
	name = MEMFILE_ATTR(cft->private);
	switch (name) {
	case RES_LIMIT:
		/* This function does all necessary parse...reuse it */
		ret = res_counter_memparse_write_strategy(buffer, &val);
		if (ret)
			break;
		if (type == _MEM)
			ret = mem_cgroup_resize_limit(memcg, val);
		else
			ret = mem_cgroup_resize_memsw_limit(memcg, val);
		break;
	default:
		ret = -EINVAL; /* should be BUG() ? */
		break;
	}
	return ret;
}

static void memcg_get_hierarchical_limit(struct mem_cgroup *memcg,
		unsigned long long *mem_limit, unsigned long long *memsw_limit)
{
	struct cgroup *cgroup;
	unsigned long long min_limit, min_memsw_limit, tmp;

	min_limit = res_counter_read_u64(&memcg->res, RES_LIMIT);
	min_memsw_limit = res_counter_read_u64(&memcg->memsw, RES_LIMIT);
	cgroup = memcg->css.cgroup;
	if (!memcg->use_hierarchy)
		goto out;

	while (cgroup->parent) {
		cgroup = cgroup->parent;
		memcg = mem_cgroup_from_cont(cgroup);
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
	return;
}

static int mem_cgroup_reset(struct cgroup *cont, unsigned int event)
{
	struct mem_cgroup *mem;
	int type, name;

	mem = mem_cgroup_from_cont(cont);
	type = MEMFILE_TYPE(event);
	name = MEMFILE_ATTR(event);
	switch (name) {
	case RES_MAX_USAGE:
		if (type == _MEM)
			res_counter_reset_max(&mem->res);
		else
			res_counter_reset_max(&mem->memsw);
		break;
	case RES_FAILCNT:
		if (type == _MEM)
			res_counter_reset_failcnt(&mem->res);
		else
			res_counter_reset_failcnt(&mem->memsw);
		break;
	}
	return 0;
}


/* For read statistics */
enum {
	MCS_CACHE,
	MCS_RSS,
	MCS_PGPGIN,
	MCS_PGPGOUT,
	MCS_INACTIVE_ANON,
	MCS_ACTIVE_ANON,
	MCS_INACTIVE_FILE,
	MCS_ACTIVE_FILE,
	MCS_UNEVICTABLE,
	NR_MCS_STAT,
};

struct mcs_total_stat {
	s64 stat[NR_MCS_STAT];
};

struct {
	char *local_name;
	char *total_name;
} memcg_stat_strings[NR_MCS_STAT] = {
	{"cache", "total_cache"},
	{"rss", "total_rss"},
	{"pgpgin", "total_pgpgin"},
	{"pgpgout", "total_pgpgout"},
	{"inactive_anon", "total_inactive_anon"},
	{"active_anon", "total_active_anon"},
	{"inactive_file", "total_inactive_file"},
	{"active_file", "total_active_file"},
	{"unevictable", "total_unevictable"}
};


static int mem_cgroup_get_local_stat(struct mem_cgroup *mem, void *data)
{
	struct mcs_total_stat *s = data;
	s64 val;

	/* per cpu stat */
	val = mem_cgroup_read_stat(&mem->stat, MEM_CGROUP_STAT_CACHE);
	s->stat[MCS_CACHE] += val * PAGE_SIZE;
	val = mem_cgroup_read_stat(&mem->stat, MEM_CGROUP_STAT_RSS);
	s->stat[MCS_RSS] += val * PAGE_SIZE;
	val = mem_cgroup_read_stat(&mem->stat, MEM_CGROUP_STAT_PGPGIN_COUNT);
	s->stat[MCS_PGPGIN] += val;
	val = mem_cgroup_read_stat(&mem->stat, MEM_CGROUP_STAT_PGPGOUT_COUNT);
	s->stat[MCS_PGPGOUT] += val;

	/* per zone stat */
	val = mem_cgroup_get_local_zonestat(mem, LRU_INACTIVE_ANON);
	s->stat[MCS_INACTIVE_ANON] += val * PAGE_SIZE;
	val = mem_cgroup_get_local_zonestat(mem, LRU_ACTIVE_ANON);
	s->stat[MCS_ACTIVE_ANON] += val * PAGE_SIZE;
	val = mem_cgroup_get_local_zonestat(mem, LRU_INACTIVE_FILE);
	s->stat[MCS_INACTIVE_FILE] += val * PAGE_SIZE;
	val = mem_cgroup_get_local_zonestat(mem, LRU_ACTIVE_FILE);
	s->stat[MCS_ACTIVE_FILE] += val * PAGE_SIZE;
	val = mem_cgroup_get_local_zonestat(mem, LRU_UNEVICTABLE);
	s->stat[MCS_UNEVICTABLE] += val * PAGE_SIZE;
	return 0;
}

static void
mem_cgroup_get_total_stat(struct mem_cgroup *mem, struct mcs_total_stat *s)
{
	mem_cgroup_walk_tree(mem, s, mem_cgroup_get_local_stat);
}

static int mem_control_stat_show(struct cgroup *cont, struct cftype *cft,
				 struct cgroup_map_cb *cb)
{
	struct mem_cgroup *mem_cont = mem_cgroup_from_cont(cont);
	struct mcs_total_stat mystat;
	int i;

	memset(&mystat, 0, sizeof(mystat));
	mem_cgroup_get_local_stat(mem_cont, &mystat);

	for (i = 0; i < NR_MCS_STAT; i++)
		cb->fill(cb, memcg_stat_strings[i].local_name, mystat.stat[i]);

	/* Hierarchical information */
	{
		unsigned long long limit, memsw_limit;
		memcg_get_hierarchical_limit(mem_cont, &limit, &memsw_limit);
		cb->fill(cb, "hierarchical_memory_limit", limit);
		if (do_swap_account)
			cb->fill(cb, "hierarchical_memsw_limit", memsw_limit);
	}

	memset(&mystat, 0, sizeof(mystat));
	mem_cgroup_get_total_stat(mem_cont, &mystat);
	for (i = 0; i < NR_MCS_STAT; i++)
		cb->fill(cb, memcg_stat_strings[i].total_name, mystat.stat[i]);


#ifdef CONFIG_DEBUG_VM
	cb->fill(cb, "inactive_ratio", calc_inactive_ratio(mem_cont, NULL));

	{
		int nid, zid;
		struct mem_cgroup_per_zone *mz;
		unsigned long recent_rotated[2] = {0, 0};
		unsigned long recent_scanned[2] = {0, 0};

		for_each_online_node(nid)
			for (zid = 0; zid < MAX_NR_ZONES; zid++) {
				mz = mem_cgroup_zoneinfo(mem_cont, nid, zid);

				recent_rotated[0] +=
					mz->reclaim_stat.recent_rotated[0];
				recent_rotated[1] +=
					mz->reclaim_stat.recent_rotated[1];
				recent_scanned[0] +=
					mz->reclaim_stat.recent_scanned[0];
				recent_scanned[1] +=
					mz->reclaim_stat.recent_scanned[1];
			}
		cb->fill(cb, "recent_rotated_anon", recent_rotated[0]);
		cb->fill(cb, "recent_rotated_file", recent_rotated[1]);
		cb->fill(cb, "recent_scanned_anon", recent_scanned[0]);
		cb->fill(cb, "recent_scanned_file", recent_scanned[1]);
	}
#endif

	return 0;
}

static u64 mem_cgroup_swappiness_read(struct cgroup *cgrp, struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_cont(cgrp);

	return get_swappiness(memcg);
}

static int mem_cgroup_swappiness_write(struct cgroup *cgrp, struct cftype *cft,
				       u64 val)
{
	struct mem_cgroup *memcg = mem_cgroup_from_cont(cgrp);
	struct mem_cgroup *parent;

	if (val > 100)
		return -EINVAL;

	if (cgrp->parent == NULL)
		return -EINVAL;

	parent = mem_cgroup_from_cont(cgrp->parent);

	cgroup_lock();

	/* If under hierarchy, only empty-root can set this value */
	if ((parent->use_hierarchy) ||
	    (memcg->use_hierarchy && !list_empty(&cgrp->children))) {
		cgroup_unlock();
		return -EINVAL;
	}

	spin_lock(&memcg->reclaim_param_lock);
	memcg->swappiness = val;
	spin_unlock(&memcg->reclaim_param_lock);

	cgroup_unlock();

	return 0;
}


static struct cftype mem_cgroup_files[] = {
	{
		.name = "usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_USAGE),
		.read_u64 = mem_cgroup_read,
	},
	{
		.name = "max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_MAX_USAGE),
		.trigger = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read,
	},
	{
		.name = "limit_in_bytes",
		.private = MEMFILE_PRIVATE(_MEM, RES_LIMIT),
		.write_string = mem_cgroup_write,
		.read_u64 = mem_cgroup_read,
	},
	{
		.name = "failcnt",
		.private = MEMFILE_PRIVATE(_MEM, RES_FAILCNT),
		.trigger = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read,
	},
	{
		.name = "stat",
		.read_map = mem_control_stat_show,
	},
	{
		.name = "force_empty",
		.trigger = mem_cgroup_force_empty_write,
	},
	{
		.name = "use_hierarchy",
		.write_u64 = mem_cgroup_hierarchy_write,
		.read_u64 = mem_cgroup_hierarchy_read,
	},
	{
		.name = "swappiness",
		.read_u64 = mem_cgroup_swappiness_read,
		.write_u64 = mem_cgroup_swappiness_write,
	},
};

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
static struct cftype memsw_cgroup_files[] = {
	{
		.name = "memsw.usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_USAGE),
		.read_u64 = mem_cgroup_read,
	},
	{
		.name = "memsw.max_usage_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_MAX_USAGE),
		.trigger = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read,
	},
	{
		.name = "memsw.limit_in_bytes",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_LIMIT),
		.write_string = mem_cgroup_write,
		.read_u64 = mem_cgroup_read,
	},
	{
		.name = "memsw.failcnt",
		.private = MEMFILE_PRIVATE(_MEMSWAP, RES_FAILCNT),
		.trigger = mem_cgroup_reset,
		.read_u64 = mem_cgroup_read,
	},
};

static int register_memsw_files(struct cgroup *cont, struct cgroup_subsys *ss)
{
	if (!do_swap_account)
		return 0;
	return cgroup_add_files(cont, ss, memsw_cgroup_files,
				ARRAY_SIZE(memsw_cgroup_files));
};
#else
static int register_memsw_files(struct cgroup *cont, struct cgroup_subsys *ss)
{
	return 0;
}
#endif

static int alloc_mem_cgroup_per_zone_info(struct mem_cgroup *mem, int node)
{
	struct mem_cgroup_per_node *pn;
	struct mem_cgroup_per_zone *mz;
	enum lru_list l;
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
	pn = kmalloc_node(sizeof(*pn), GFP_KERNEL, tmp);
	if (!pn)
		return 1;

	mem->info.nodeinfo[node] = pn;
	memset(pn, 0, sizeof(*pn));

	for (zone = 0; zone < MAX_NR_ZONES; zone++) {
		mz = &pn->zoneinfo[zone];
		for_each_lru(l)
			INIT_LIST_HEAD(&mz->lists[l]);
	}
	return 0;
}

static void free_mem_cgroup_per_zone_info(struct mem_cgroup *mem, int node)
{
	kfree(mem->info.nodeinfo[node]);
}

static int mem_cgroup_size(void)
{
	int cpustat_size = nr_cpu_ids * sizeof(struct mem_cgroup_stat_cpu);
	return sizeof(struct mem_cgroup) + cpustat_size;
}

static struct mem_cgroup *mem_cgroup_alloc(void)
{
	struct mem_cgroup *mem;
	int size = mem_cgroup_size();

	if (size < PAGE_SIZE)
		mem = kmalloc(size, GFP_KERNEL);
	else
		mem = vmalloc(size);

	if (mem)
		memset(mem, 0, size);
	return mem;
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

static void __mem_cgroup_free(struct mem_cgroup *mem)
{
	int node;

	free_css_id(&mem_cgroup_subsys, &mem->css);

	for_each_node_state(node, N_POSSIBLE)
		free_mem_cgroup_per_zone_info(mem, node);

	if (mem_cgroup_size() < PAGE_SIZE)
		kfree(mem);
	else
		vfree(mem);
}

static void mem_cgroup_get(struct mem_cgroup *mem)
{
	atomic_inc(&mem->refcnt);
}

static void mem_cgroup_put(struct mem_cgroup *mem)
{
	if (atomic_dec_and_test(&mem->refcnt)) {
		struct mem_cgroup *parent = parent_mem_cgroup(mem);
		__mem_cgroup_free(mem);
		if (parent)
			mem_cgroup_put(parent);
	}
}

/*
 * Returns the parent mem_cgroup in memcgroup hierarchy with hierarchy enabled.
 */
static struct mem_cgroup *parent_mem_cgroup(struct mem_cgroup *mem)
{
	if (!mem->res.parent)
		return NULL;
	return mem_cgroup_from_res_counter(mem->res.parent, res);
}

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
static void __init enable_swap_cgroup(void)
{
	if (!mem_cgroup_disabled() && really_do_swap_account)
		do_swap_account = 1;
}
#else
static void __init enable_swap_cgroup(void)
{
}
#endif

static struct cgroup_subsys_state * __ref
mem_cgroup_create(struct cgroup_subsys *ss, struct cgroup *cont)
{
	struct mem_cgroup *mem, *parent;
	long error = -ENOMEM;
	int node;

	mem = mem_cgroup_alloc();
	if (!mem)
		return ERR_PTR(error);

	for_each_node_state(node, N_POSSIBLE)
		if (alloc_mem_cgroup_per_zone_info(mem, node))
			goto free_out;
	/* root ? */
	if (cont->parent == NULL) {
		enable_swap_cgroup();
		parent = NULL;
	} else {
		parent = mem_cgroup_from_cont(cont->parent);
		mem->use_hierarchy = parent->use_hierarchy;
	}

	if (parent && parent->use_hierarchy) {
		res_counter_init(&mem->res, &parent->res);
		res_counter_init(&mem->memsw, &parent->memsw);
		/*
		 * We increment refcnt of the parent to ensure that we can
		 * safely access it on res_counter_charge/uncharge.
		 * This refcnt will be decremented when freeing this
		 * mem_cgroup(see mem_cgroup_put).
		 */
		mem_cgroup_get(parent);
	} else {
		res_counter_init(&mem->res, NULL);
		res_counter_init(&mem->memsw, NULL);
	}
	mem->last_scanned_child = 0;
	spin_lock_init(&mem->reclaim_param_lock);

	if (parent)
		mem->swappiness = get_swappiness(parent);
	atomic_set(&mem->refcnt, 1);
	return &mem->css;
free_out:
	__mem_cgroup_free(mem);
	return ERR_PTR(error);
}

static int mem_cgroup_pre_destroy(struct cgroup_subsys *ss,
					struct cgroup *cont)
{
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);

	return mem_cgroup_force_empty(mem, false);
}

static void mem_cgroup_destroy(struct cgroup_subsys *ss,
				struct cgroup *cont)
{
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);

	mem_cgroup_put(mem);
}

static int mem_cgroup_populate(struct cgroup_subsys *ss,
				struct cgroup *cont)
{
	int ret;

	ret = cgroup_add_files(cont, ss, mem_cgroup_files,
				ARRAY_SIZE(mem_cgroup_files));

	if (!ret)
		ret = register_memsw_files(cont, ss);
	return ret;
}

static void mem_cgroup_move_task(struct cgroup_subsys *ss,
				struct cgroup *cont,
				struct cgroup *old_cont,
				struct task_struct *p)
{
	mutex_lock(&memcg_tasklist);
	/*
	 * FIXME: It's better to move charges of this process from old
	 * memcg to new memcg. But it's just on TODO-List now.
	 */
	mutex_unlock(&memcg_tasklist);
}

struct cgroup_subsys mem_cgroup_subsys = {
	.name = "memory",
	.subsys_id = mem_cgroup_subsys_id,
	.create = mem_cgroup_create,
	.pre_destroy = mem_cgroup_pre_destroy,
	.destroy = mem_cgroup_destroy,
	.populate = mem_cgroup_populate,
	.attach = mem_cgroup_move_task,
	.early_init = 0,
	.use_id = 1,
};

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP

static int __init disable_swap_account(char *s)
{
	really_do_swap_account = 0;
	return 1;
}
__setup("noswapaccount", disable_swap_account);
#endif
