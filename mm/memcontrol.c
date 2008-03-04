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
#include <linux/smp.h>
#include <linux/page-flags.h>
#include <linux/backing-dev.h>
#include <linux/bit_spinlock.h>
#include <linux/rcupdate.h>
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>

struct cgroup_subsys mem_cgroup_subsys;
static const int MEM_CGROUP_RECLAIM_RETRIES = 5;

/*
 * Statistics for memory cgroup.
 */
enum mem_cgroup_stat_index {
	/*
	 * For MEM_CONTAINER_TYPE_ALL, usage = pagecache + rss.
	 */
	MEM_CGROUP_STAT_CACHE, 	   /* # of pages charged as cache */
	MEM_CGROUP_STAT_RSS,	   /* # of pages charged as rss */

	MEM_CGROUP_STAT_NSTATS,
};

struct mem_cgroup_stat_cpu {
	s64 count[MEM_CGROUP_STAT_NSTATS];
} ____cacheline_aligned_in_smp;

struct mem_cgroup_stat {
	struct mem_cgroup_stat_cpu cpustat[NR_CPUS];
};

/*
 * For accounting under irq disable, no need for increment preempt count.
 */
static void __mem_cgroup_stat_add_safe(struct mem_cgroup_stat *stat,
		enum mem_cgroup_stat_index idx, int val)
{
	int cpu = smp_processor_id();
	stat->cpustat[cpu].count[idx] += val;
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

/*
 * per-zone information in memory controller.
 */

enum mem_cgroup_zstat_index {
	MEM_CGROUP_ZSTAT_ACTIVE,
	MEM_CGROUP_ZSTAT_INACTIVE,

	NR_MEM_CGROUP_ZSTAT,
};

struct mem_cgroup_per_zone {
	/*
	 * spin_lock to protect the per cgroup LRU
	 */
	spinlock_t		lru_lock;
	struct list_head	active_list;
	struct list_head	inactive_list;
	unsigned long count[NR_MEM_CGROUP_ZSTAT];
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
	 * Per cgroup active and inactive list, similar to the
	 * per zone LRU lists.
	 */
	struct mem_cgroup_lru_info info;

	int	prev_priority;	/* for recording reclaim priority */
	/*
	 * statistics.
	 */
	struct mem_cgroup_stat stat;
};
static struct mem_cgroup init_mem_cgroup;

/*
 * We use the lower bit of the page->page_cgroup pointer as a bit spin
 * lock.  We need to ensure that page->page_cgroup is at least two
 * byte aligned (based on comments from Nick Piggin).  But since
 * bit_spin_lock doesn't actually set that lock bit in a non-debug
 * uniprocessor kernel, we should avoid setting it here too.
 */
#define PAGE_CGROUP_LOCK_BIT 	0x0
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
#define PAGE_CGROUP_LOCK 	(1 << PAGE_CGROUP_LOCK_BIT)
#else
#define PAGE_CGROUP_LOCK	0x0
#endif

/*
 * A page_cgroup page is associated with every page descriptor. The
 * page_cgroup helps us identify information about the cgroup
 */
struct page_cgroup {
	struct list_head lru;		/* per cgroup LRU list */
	struct page *page;
	struct mem_cgroup *mem_cgroup;
	atomic_t ref_cnt;		/* Helpful when pages move b/w  */
					/* mapped and cached states     */
	int flags;
};
#define PAGE_CGROUP_FLAG_CACHE	(0x1)	/* charged as cache */
#define PAGE_CGROUP_FLAG_ACTIVE (0x2)	/* page is active in this cgroup */

static inline int page_cgroup_nid(struct page_cgroup *pc)
{
	return page_to_nid(pc->page);
}

static inline enum zone_type page_cgroup_zid(struct page_cgroup *pc)
{
	return page_zonenum(pc->page);
}

enum charge_type {
	MEM_CGROUP_CHARGE_TYPE_CACHE = 0,
	MEM_CGROUP_CHARGE_TYPE_MAPPED,
};

/*
 * Always modified under lru lock. Then, not necessary to preempt_disable()
 */
static void mem_cgroup_charge_statistics(struct mem_cgroup *mem, int flags,
					bool charge)
{
	int val = (charge)? 1 : -1;
	struct mem_cgroup_stat *stat = &mem->stat;

	VM_BUG_ON(!irqs_disabled());
	if (flags & PAGE_CGROUP_FLAG_CACHE)
		__mem_cgroup_stat_add_safe(stat, MEM_CGROUP_STAT_CACHE, val);
	else
		__mem_cgroup_stat_add_safe(stat, MEM_CGROUP_STAT_RSS, val);
}

static inline struct mem_cgroup_per_zone *
mem_cgroup_zoneinfo(struct mem_cgroup *mem, int nid, int zid)
{
	BUG_ON(!mem->info.nodeinfo[nid]);
	return &mem->info.nodeinfo[nid]->zoneinfo[zid];
}

static inline struct mem_cgroup_per_zone *
page_cgroup_zoneinfo(struct page_cgroup *pc)
{
	struct mem_cgroup *mem = pc->mem_cgroup;
	int nid = page_cgroup_nid(pc);
	int zid = page_cgroup_zid(pc);

	return mem_cgroup_zoneinfo(mem, nid, zid);
}

static unsigned long mem_cgroup_get_all_zonestat(struct mem_cgroup *mem,
					enum mem_cgroup_zstat_index idx)
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

static inline
struct mem_cgroup *mem_cgroup_from_cont(struct cgroup *cont)
{
	return container_of(cgroup_subsys_state(cont,
				mem_cgroup_subsys_id), struct mem_cgroup,
				css);
}

static inline
struct mem_cgroup *mem_cgroup_from_task(struct task_struct *p)
{
	return container_of(task_subsys_state(p, mem_cgroup_subsys_id),
				struct mem_cgroup, css);
}

void mm_init_cgroup(struct mm_struct *mm, struct task_struct *p)
{
	struct mem_cgroup *mem;

	mem = mem_cgroup_from_task(p);
	css_get(&mem->css);
	mm->mem_cgroup = mem;
}

void mm_free_cgroup(struct mm_struct *mm)
{
	css_put(&mm->mem_cgroup->css);
}

static inline int page_cgroup_locked(struct page *page)
{
	return bit_spin_is_locked(PAGE_CGROUP_LOCK_BIT, &page->page_cgroup);
}

static void page_assign_page_cgroup(struct page *page, struct page_cgroup *pc)
{
	VM_BUG_ON(!page_cgroup_locked(page));
	page->page_cgroup = ((unsigned long)pc | PAGE_CGROUP_LOCK);
}

struct page_cgroup *page_get_page_cgroup(struct page *page)
{
	return (struct page_cgroup *) (page->page_cgroup & ~PAGE_CGROUP_LOCK);
}

static void __always_inline lock_page_cgroup(struct page *page)
{
	bit_spin_lock(PAGE_CGROUP_LOCK_BIT, &page->page_cgroup);
	VM_BUG_ON(!page_cgroup_locked(page));
}

static void __always_inline unlock_page_cgroup(struct page *page)
{
	bit_spin_unlock(PAGE_CGROUP_LOCK_BIT, &page->page_cgroup);
}

/*
 * Clear page->page_cgroup member under lock_page_cgroup().
 * If given "pc" value is different from one page->page_cgroup,
 * page->cgroup is not cleared.
 * Returns a value of page->page_cgroup at lock taken.
 * A can can detect failure of clearing by following
 *  clear_page_cgroup(page, pc) == pc
 */
static struct page_cgroup *clear_page_cgroup(struct page *page,
						struct page_cgroup *pc)
{
	struct page_cgroup *ret;
	/* lock and clear */
	lock_page_cgroup(page);
	ret = page_get_page_cgroup(page);
	if (likely(ret == pc))
		page_assign_page_cgroup(page, NULL);
	unlock_page_cgroup(page);
	return ret;
}

static void __mem_cgroup_remove_list(struct page_cgroup *pc)
{
	int from = pc->flags & PAGE_CGROUP_FLAG_ACTIVE;
	struct mem_cgroup_per_zone *mz = page_cgroup_zoneinfo(pc);

	if (from)
		MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_ACTIVE) -= 1;
	else
		MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_INACTIVE) -= 1;

	mem_cgroup_charge_statistics(pc->mem_cgroup, pc->flags, false);
	list_del_init(&pc->lru);
}

static void __mem_cgroup_add_list(struct page_cgroup *pc)
{
	int to = pc->flags & PAGE_CGROUP_FLAG_ACTIVE;
	struct mem_cgroup_per_zone *mz = page_cgroup_zoneinfo(pc);

	if (!to) {
		MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_INACTIVE) += 1;
		list_add(&pc->lru, &mz->inactive_list);
	} else {
		MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_ACTIVE) += 1;
		list_add(&pc->lru, &mz->active_list);
	}
	mem_cgroup_charge_statistics(pc->mem_cgroup, pc->flags, true);
}

static void __mem_cgroup_move_lists(struct page_cgroup *pc, bool active)
{
	int from = pc->flags & PAGE_CGROUP_FLAG_ACTIVE;
	struct mem_cgroup_per_zone *mz = page_cgroup_zoneinfo(pc);

	if (from)
		MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_ACTIVE) -= 1;
	else
		MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_INACTIVE) -= 1;

	if (active) {
		MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_ACTIVE) += 1;
		pc->flags |= PAGE_CGROUP_FLAG_ACTIVE;
		list_move(&pc->lru, &mz->active_list);
	} else {
		MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_INACTIVE) += 1;
		pc->flags &= ~PAGE_CGROUP_FLAG_ACTIVE;
		list_move(&pc->lru, &mz->inactive_list);
	}
}

int task_in_mem_cgroup(struct task_struct *task, const struct mem_cgroup *mem)
{
	int ret;

	task_lock(task);
	ret = task->mm && mm_match_cgroup(task->mm, mem);
	task_unlock(task);
	return ret;
}

/*
 * This routine assumes that the appropriate zone's lru lock is already held
 */
void mem_cgroup_move_lists(struct page *page, bool active)
{
	struct page_cgroup *pc;
	struct mem_cgroup_per_zone *mz;
	unsigned long flags;

	pc = page_get_page_cgroup(page);
	if (!pc)
		return;

	mz = page_cgroup_zoneinfo(pc);
	spin_lock_irqsave(&mz->lru_lock, flags);
	__mem_cgroup_move_lists(pc, active);
	spin_unlock_irqrestore(&mz->lru_lock, flags);
}

/*
 * Calculate mapped_ratio under memory controller. This will be used in
 * vmscan.c for deteremining we have to reclaim mapped pages.
 */
int mem_cgroup_calc_mapped_ratio(struct mem_cgroup *mem)
{
	long total, rss;

	/*
	 * usage is recorded in bytes. But, here, we assume the number of
	 * physical pages can be represented by "long" on any arch.
	 */
	total = (long) (mem->res.usage >> PAGE_SHIFT) + 1L;
	rss = (long)mem_cgroup_read_stat(&mem->stat, MEM_CGROUP_STAT_RSS);
	return (int)((rss * 100L) / total);
}

/*
 * This function is called from vmscan.c. In page reclaiming loop. balance
 * between active and inactive list is calculated. For memory controller
 * page reclaiming, we should use using mem_cgroup's imbalance rather than
 * zone's global lru imbalance.
 */
long mem_cgroup_reclaim_imbalance(struct mem_cgroup *mem)
{
	unsigned long active, inactive;
	/* active and inactive are the number of pages. 'long' is ok.*/
	active = mem_cgroup_get_all_zonestat(mem, MEM_CGROUP_ZSTAT_ACTIVE);
	inactive = mem_cgroup_get_all_zonestat(mem, MEM_CGROUP_ZSTAT_INACTIVE);
	return (long) (active / (inactive + 1));
}

/*
 * prev_priority control...this will be used in memory reclaim path.
 */
int mem_cgroup_get_reclaim_priority(struct mem_cgroup *mem)
{
	return mem->prev_priority;
}

void mem_cgroup_note_reclaim_priority(struct mem_cgroup *mem, int priority)
{
	if (priority < mem->prev_priority)
		mem->prev_priority = priority;
}

void mem_cgroup_record_reclaim_priority(struct mem_cgroup *mem, int priority)
{
	mem->prev_priority = priority;
}

/*
 * Calculate # of pages to be scanned in this priority/zone.
 * See also vmscan.c
 *
 * priority starts from "DEF_PRIORITY" and decremented in each loop.
 * (see include/linux/mmzone.h)
 */

long mem_cgroup_calc_reclaim_active(struct mem_cgroup *mem,
				   struct zone *zone, int priority)
{
	long nr_active;
	int nid = zone->zone_pgdat->node_id;
	int zid = zone_idx(zone);
	struct mem_cgroup_per_zone *mz = mem_cgroup_zoneinfo(mem, nid, zid);

	nr_active = MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_ACTIVE);
	return (nr_active >> priority);
}

long mem_cgroup_calc_reclaim_inactive(struct mem_cgroup *mem,
					struct zone *zone, int priority)
{
	long nr_inactive;
	int nid = zone->zone_pgdat->node_id;
	int zid = zone_idx(zone);
	struct mem_cgroup_per_zone *mz = mem_cgroup_zoneinfo(mem, nid, zid);

	nr_inactive = MEM_CGROUP_ZSTAT(mz, MEM_CGROUP_ZSTAT_INACTIVE);
	return (nr_inactive >> priority);
}

unsigned long mem_cgroup_isolate_pages(unsigned long nr_to_scan,
					struct list_head *dst,
					unsigned long *scanned, int order,
					int mode, struct zone *z,
					struct mem_cgroup *mem_cont,
					int active)
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

	mz = mem_cgroup_zoneinfo(mem_cont, nid, zid);
	if (active)
		src = &mz->active_list;
	else
		src = &mz->inactive_list;


	spin_lock(&mz->lru_lock);
	scan = 0;
	list_for_each_entry_safe_reverse(pc, tmp, src, lru) {
		if (scan >= nr_to_scan)
			break;
		page = pc->page;

		if (unlikely(!PageLRU(page)))
			continue;

		if (PageActive(page) && !active) {
			__mem_cgroup_move_lists(pc, true);
			continue;
		}
		if (!PageActive(page) && active) {
			__mem_cgroup_move_lists(pc, false);
			continue;
		}

		scan++;
		list_move(&pc->lru, &pc_list);

		if (__isolate_lru_page(page, mode) == 0) {
			list_move(&page->lru, dst);
			nr_taken++;
		}
	}

	list_splice(&pc_list, src);
	spin_unlock(&mz->lru_lock);

	*scanned = scan;
	return nr_taken;
}

/*
 * Charge the memory controller for page usage.
 * Return
 * 0 if the charge was successful
 * < 0 if the cgroup is over its limit
 */
static int mem_cgroup_charge_common(struct page *page, struct mm_struct *mm,
				gfp_t gfp_mask, enum charge_type ctype)
{
	struct mem_cgroup *mem;
	struct page_cgroup *pc;
	unsigned long flags;
	unsigned long nr_retries = MEM_CGROUP_RECLAIM_RETRIES;
	struct mem_cgroup_per_zone *mz;

	/*
	 * Should page_cgroup's go to their own slab?
	 * One could optimize the performance of the charging routine
	 * by saving a bit in the page_flags and using it as a lock
	 * to see if the cgroup page already has a page_cgroup associated
	 * with it
	 */
retry:
	lock_page_cgroup(page);
	pc = page_get_page_cgroup(page);
	/*
	 * The page_cgroup exists and
	 * the page has already been accounted.
	 */
	if (pc) {
		if (unlikely(!atomic_inc_not_zero(&pc->ref_cnt))) {
			/* this page is under being uncharged ? */
			unlock_page_cgroup(page);
			cpu_relax();
			goto retry;
		} else {
			unlock_page_cgroup(page);
			goto done;
		}
	}
	unlock_page_cgroup(page);

	pc = kzalloc(sizeof(struct page_cgroup), gfp_mask);
	if (pc == NULL)
		goto err;

	/*
	 * We always charge the cgroup the mm_struct belongs to.
	 * The mm_struct's mem_cgroup changes on task migration if the
	 * thread group leader migrates. It's possible that mm is not
	 * set, if so charge the init_mm (happens for pagecache usage).
	 */
	if (!mm)
		mm = &init_mm;

	rcu_read_lock();
	mem = rcu_dereference(mm->mem_cgroup);
	/*
	 * For every charge from the cgroup, increment reference count
	 */
	css_get(&mem->css);
	rcu_read_unlock();

	while (res_counter_charge(&mem->res, PAGE_SIZE)) {
		if (!(gfp_mask & __GFP_WAIT))
			goto out;

		if (try_to_free_mem_cgroup_pages(mem, gfp_mask))
			continue;

		/*
		 * try_to_free_mem_cgroup_pages() might not give us a full
		 * picture of reclaim. Some pages are reclaimed and might be
		 * moved to swap cache or just unmapped from the cgroup.
		 * Check the limit again to see if the reclaim reduced the
		 * current usage of the cgroup before giving up
		 */
		if (res_counter_check_under_limit(&mem->res))
			continue;

		if (!nr_retries--) {
			mem_cgroup_out_of_memory(mem, gfp_mask);
			goto out;
		}
		congestion_wait(WRITE, HZ/10);
	}

	atomic_set(&pc->ref_cnt, 1);
	pc->mem_cgroup = mem;
	pc->page = page;
	pc->flags = PAGE_CGROUP_FLAG_ACTIVE;
	if (ctype == MEM_CGROUP_CHARGE_TYPE_CACHE)
		pc->flags |= PAGE_CGROUP_FLAG_CACHE;

	lock_page_cgroup(page);
	if (page_get_page_cgroup(page)) {
		unlock_page_cgroup(page);
		/*
		 * Another charge has been added to this page already.
		 * We take lock_page_cgroup(page) again and read
		 * page->cgroup, increment refcnt.... just retry is OK.
		 */
		res_counter_uncharge(&mem->res, PAGE_SIZE);
		css_put(&mem->css);
		kfree(pc);
		goto retry;
	}
	page_assign_page_cgroup(page, pc);
	unlock_page_cgroup(page);

	mz = page_cgroup_zoneinfo(pc);
	spin_lock_irqsave(&mz->lru_lock, flags);
	__mem_cgroup_add_list(pc);
	spin_unlock_irqrestore(&mz->lru_lock, flags);

done:
	return 0;
out:
	css_put(&mem->css);
	kfree(pc);
err:
	return -ENOMEM;
}

int mem_cgroup_charge(struct page *page, struct mm_struct *mm, gfp_t gfp_mask)
{
	return mem_cgroup_charge_common(page, mm, gfp_mask,
				MEM_CGROUP_CHARGE_TYPE_MAPPED);
}

int mem_cgroup_cache_charge(struct page *page, struct mm_struct *mm,
				gfp_t gfp_mask)
{
	if (!mm)
		mm = &init_mm;
	return mem_cgroup_charge_common(page, mm, gfp_mask,
				MEM_CGROUP_CHARGE_TYPE_CACHE);
}

/*
 * Uncharging is always a welcome operation, we never complain, simply
 * uncharge.
 */
void mem_cgroup_uncharge_page(struct page *page)
{
	struct page_cgroup *pc;
	struct mem_cgroup *mem;
	struct mem_cgroup_per_zone *mz;
	unsigned long flags;

	/*
	 * Check if our page_cgroup is valid
	 */
	lock_page_cgroup(page);
	pc = page_get_page_cgroup(page);
	if (!pc)
		goto unlock;

	if (atomic_dec_and_test(&pc->ref_cnt)) {
		page = pc->page;
		mz = page_cgroup_zoneinfo(pc);
		/*
		 * get page->cgroup and clear it under lock.
		 * force_empty can drop page->cgroup without checking refcnt.
		 */
		unlock_page_cgroup(page);
		if (clear_page_cgroup(page, pc) == pc) {
			mem = pc->mem_cgroup;
			css_put(&mem->css);
			res_counter_uncharge(&mem->res, PAGE_SIZE);
			spin_lock_irqsave(&mz->lru_lock, flags);
			__mem_cgroup_remove_list(pc);
			spin_unlock_irqrestore(&mz->lru_lock, flags);
			kfree(pc);
		}
		lock_page_cgroup(page);
	}

unlock:
	unlock_page_cgroup(page);
}

/*
 * Returns non-zero if a page (under migration) has valid page_cgroup member.
 * Refcnt of page_cgroup is incremented.
 */
int mem_cgroup_prepare_migration(struct page *page)
{
	struct page_cgroup *pc;
	int ret = 0;

	lock_page_cgroup(page);
	pc = page_get_page_cgroup(page);
	if (pc && atomic_inc_not_zero(&pc->ref_cnt))
		ret = 1;
	unlock_page_cgroup(page);
	return ret;
}

void mem_cgroup_end_migration(struct page *page)
{
	mem_cgroup_uncharge_page(page);
}

/*
 * We know both *page* and *newpage* are now not-on-LRU and PG_locked.
 * And no race with uncharge() routines because page_cgroup for *page*
 * has extra one reference by mem_cgroup_prepare_migration.
 */
void mem_cgroup_page_migration(struct page *page, struct page *newpage)
{
	struct page_cgroup *pc;
	struct mem_cgroup *mem;
	unsigned long flags;
	struct mem_cgroup_per_zone *mz;

retry:
	pc = page_get_page_cgroup(page);
	if (!pc)
		return;

	mem = pc->mem_cgroup;
	mz = page_cgroup_zoneinfo(pc);
	if (clear_page_cgroup(page, pc) != pc)
		goto retry;

	spin_lock_irqsave(&mz->lru_lock, flags);
	__mem_cgroup_remove_list(pc);
	spin_unlock_irqrestore(&mz->lru_lock, flags);

	pc->page = newpage;
	lock_page_cgroup(newpage);
	page_assign_page_cgroup(newpage, pc);
	unlock_page_cgroup(newpage);

	mz = page_cgroup_zoneinfo(pc);
	spin_lock_irqsave(&mz->lru_lock, flags);
	__mem_cgroup_add_list(pc);
	spin_unlock_irqrestore(&mz->lru_lock, flags);
}

/*
 * This routine traverse page_cgroup in given list and drop them all.
 * This routine ignores page_cgroup->ref_cnt.
 * *And* this routine doesn't reclaim page itself, just removes page_cgroup.
 */
#define FORCE_UNCHARGE_BATCH	(128)
static void mem_cgroup_force_empty_list(struct mem_cgroup *mem,
			    struct mem_cgroup_per_zone *mz,
			    int active)
{
	struct page_cgroup *pc;
	struct page *page;
	int count;
	unsigned long flags;
	struct list_head *list;

	if (active)
		list = &mz->active_list;
	else
		list = &mz->inactive_list;

	if (list_empty(list))
		return;
retry:
	count = FORCE_UNCHARGE_BATCH;
	spin_lock_irqsave(&mz->lru_lock, flags);

	while (--count && !list_empty(list)) {
		pc = list_entry(list->prev, struct page_cgroup, lru);
		page = pc->page;
		/* Avoid race with charge */
		atomic_set(&pc->ref_cnt, 0);
		if (clear_page_cgroup(page, pc) == pc) {
			css_put(&mem->css);
			res_counter_uncharge(&mem->res, PAGE_SIZE);
			__mem_cgroup_remove_list(pc);
			kfree(pc);
		} else 	/* being uncharged ? ...do relax */
			break;
	}

	spin_unlock_irqrestore(&mz->lru_lock, flags);
	if (!list_empty(list)) {
		cond_resched();
		goto retry;
	}
}

/*
 * make mem_cgroup's charge to be 0 if there is no task.
 * This enables deleting this mem_cgroup.
 */
int mem_cgroup_force_empty(struct mem_cgroup *mem)
{
	int ret = -EBUSY;
	int node, zid;

	css_get(&mem->css);
	/*
	 * page reclaim code (kswapd etc..) will move pages between
	 * active_list <-> inactive_list while we don't take a lock.
	 * So, we have to do loop here until all lists are empty.
	 */
	while (mem->res.usage > 0) {
		if (atomic_read(&mem->css.cgroup->count) > 0)
			goto out;
		for_each_node_state(node, N_POSSIBLE)
			for (zid = 0; zid < MAX_NR_ZONES; zid++) {
				struct mem_cgroup_per_zone *mz;
				mz = mem_cgroup_zoneinfo(mem, node, zid);
				/* drop all page_cgroup in active_list */
				mem_cgroup_force_empty_list(mem, mz, 1);
				/* drop all page_cgroup in inactive_list */
				mem_cgroup_force_empty_list(mem, mz, 0);
			}
	}
	ret = 0;
out:
	css_put(&mem->css);
	return ret;
}

int mem_cgroup_write_strategy(char *buf, unsigned long long *tmp)
{
	*tmp = memparse(buf, &buf);
	if (*buf != '\0')
		return -EINVAL;

	/*
	 * Round up the value to the closest page size
	 */
	*tmp = ((*tmp + PAGE_SIZE - 1) >> PAGE_SHIFT) << PAGE_SHIFT;
	return 0;
}

static ssize_t mem_cgroup_read(struct cgroup *cont,
			struct cftype *cft, struct file *file,
			char __user *userbuf, size_t nbytes, loff_t *ppos)
{
	return res_counter_read(&mem_cgroup_from_cont(cont)->res,
				cft->private, userbuf, nbytes, ppos,
				NULL);
}

static ssize_t mem_cgroup_write(struct cgroup *cont, struct cftype *cft,
				struct file *file, const char __user *userbuf,
				size_t nbytes, loff_t *ppos)
{
	return res_counter_write(&mem_cgroup_from_cont(cont)->res,
				cft->private, userbuf, nbytes, ppos,
				mem_cgroup_write_strategy);
}

static ssize_t mem_force_empty_write(struct cgroup *cont,
				struct cftype *cft, struct file *file,
				const char __user *userbuf,
				size_t nbytes, loff_t *ppos)
{
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);
	int ret = mem_cgroup_force_empty(mem);
	if (!ret)
		ret = nbytes;
	return ret;
}

/*
 * Note: This should be removed if cgroup supports write-only file.
 */
static ssize_t mem_force_empty_read(struct cgroup *cont,
				struct cftype *cft,
				struct file *file, char __user *userbuf,
				size_t nbytes, loff_t *ppos)
{
	return -EINVAL;
}

static const struct mem_cgroup_stat_desc {
	const char *msg;
	u64 unit;
} mem_cgroup_stat_desc[] = {
	[MEM_CGROUP_STAT_CACHE] = { "cache", PAGE_SIZE, },
	[MEM_CGROUP_STAT_RSS] = { "rss", PAGE_SIZE, },
};

static int mem_control_stat_show(struct seq_file *m, void *arg)
{
	struct cgroup *cont = m->private;
	struct mem_cgroup *mem_cont = mem_cgroup_from_cont(cont);
	struct mem_cgroup_stat *stat = &mem_cont->stat;
	int i;

	for (i = 0; i < ARRAY_SIZE(stat->cpustat[0].count); i++) {
		s64 val;

		val = mem_cgroup_read_stat(stat, i);
		val *= mem_cgroup_stat_desc[i].unit;
		seq_printf(m, "%s %lld\n", mem_cgroup_stat_desc[i].msg,
				(long long)val);
	}
	/* showing # of active pages */
	{
		unsigned long active, inactive;

		inactive = mem_cgroup_get_all_zonestat(mem_cont,
						MEM_CGROUP_ZSTAT_INACTIVE);
		active = mem_cgroup_get_all_zonestat(mem_cont,
						MEM_CGROUP_ZSTAT_ACTIVE);
		seq_printf(m, "active %ld\n", (active) * PAGE_SIZE);
		seq_printf(m, "inactive %ld\n", (inactive) * PAGE_SIZE);
	}
	return 0;
}

static const struct file_operations mem_control_stat_file_operations = {
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int mem_control_stat_open(struct inode *unused, struct file *file)
{
	/* XXX __d_cont */
	struct cgroup *cont = file->f_dentry->d_parent->d_fsdata;

	file->f_op = &mem_control_stat_file_operations;
	return single_open(file, mem_control_stat_show, cont);
}

static struct cftype mem_cgroup_files[] = {
	{
		.name = "usage_in_bytes",
		.private = RES_USAGE,
		.read = mem_cgroup_read,
	},
	{
		.name = "limit_in_bytes",
		.private = RES_LIMIT,
		.write = mem_cgroup_write,
		.read = mem_cgroup_read,
	},
	{
		.name = "failcnt",
		.private = RES_FAILCNT,
		.read = mem_cgroup_read,
	},
	{
		.name = "force_empty",
		.write = mem_force_empty_write,
		.read = mem_force_empty_read,
	},
	{
		.name = "stat",
		.open = mem_control_stat_open,
	},
};

static int alloc_mem_cgroup_per_zone_info(struct mem_cgroup *mem, int node)
{
	struct mem_cgroup_per_node *pn;
	struct mem_cgroup_per_zone *mz;
	int zone;
	/*
	 * This routine is called against possible nodes.
	 * But it's BUG to call kmalloc() against offline node.
	 *
	 * TODO: this routine can waste much memory for nodes which will
	 *       never be onlined. It's better to use memory hotplug callback
	 *       function.
	 */
	if (node_state(node, N_HIGH_MEMORY))
		pn = kmalloc_node(sizeof(*pn), GFP_KERNEL, node);
	else
		pn = kmalloc(sizeof(*pn), GFP_KERNEL);
	if (!pn)
		return 1;

	mem->info.nodeinfo[node] = pn;
	memset(pn, 0, sizeof(*pn));

	for (zone = 0; zone < MAX_NR_ZONES; zone++) {
		mz = &pn->zoneinfo[zone];
		INIT_LIST_HEAD(&mz->active_list);
		INIT_LIST_HEAD(&mz->inactive_list);
		spin_lock_init(&mz->lru_lock);
	}
	return 0;
}

static void free_mem_cgroup_per_zone_info(struct mem_cgroup *mem, int node)
{
	kfree(mem->info.nodeinfo[node]);
}

static struct cgroup_subsys_state *
mem_cgroup_create(struct cgroup_subsys *ss, struct cgroup *cont)
{
	struct mem_cgroup *mem;
	int node;

	if (unlikely((cont->parent) == NULL)) {
		mem = &init_mem_cgroup;
		init_mm.mem_cgroup = mem;
	} else
		mem = kzalloc(sizeof(struct mem_cgroup), GFP_KERNEL);

	if (mem == NULL)
		return ERR_PTR(-ENOMEM);

	res_counter_init(&mem->res);

	memset(&mem->info, 0, sizeof(mem->info));

	for_each_node_state(node, N_POSSIBLE)
		if (alloc_mem_cgroup_per_zone_info(mem, node))
			goto free_out;

	return &mem->css;
free_out:
	for_each_node_state(node, N_POSSIBLE)
		free_mem_cgroup_per_zone_info(mem, node);
	if (cont->parent != NULL)
		kfree(mem);
	return ERR_PTR(-ENOMEM);
}

static void mem_cgroup_pre_destroy(struct cgroup_subsys *ss,
					struct cgroup *cont)
{
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);
	mem_cgroup_force_empty(mem);
}

static void mem_cgroup_destroy(struct cgroup_subsys *ss,
				struct cgroup *cont)
{
	int node;
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);

	for_each_node_state(node, N_POSSIBLE)
		free_mem_cgroup_per_zone_info(mem, node);

	kfree(mem_cgroup_from_cont(cont));
}

static int mem_cgroup_populate(struct cgroup_subsys *ss,
				struct cgroup *cont)
{
	return cgroup_add_files(cont, ss, mem_cgroup_files,
					ARRAY_SIZE(mem_cgroup_files));
}

static void mem_cgroup_move_task(struct cgroup_subsys *ss,
				struct cgroup *cont,
				struct cgroup *old_cont,
				struct task_struct *p)
{
	struct mm_struct *mm;
	struct mem_cgroup *mem, *old_mem;

	mm = get_task_mm(p);
	if (mm == NULL)
		return;

	mem = mem_cgroup_from_cont(cont);
	old_mem = mem_cgroup_from_cont(old_cont);

	if (mem == old_mem)
		goto out;

	/*
	 * Only thread group leaders are allowed to migrate, the mm_struct is
	 * in effect owned by the leader
	 */
	if (p->tgid != p->pid)
		goto out;

	css_get(&mem->css);
	rcu_assign_pointer(mm->mem_cgroup, mem);
	css_put(&old_mem->css);

out:
	mmput(mm);
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
};
