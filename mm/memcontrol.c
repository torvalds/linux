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
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/mm_inline.h>
#include <linux/page_cgroup.h>

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

/*
 * per-zone information in memory controller.
 */
struct mem_cgroup_per_zone {
	/*
	 * spin_lock to protect the per cgroup LRU
	 */
	spinlock_t		lru_lock;
	struct list_head	lists[NR_LRU_LISTS];
	unsigned long		count[NR_LRU_LISTS];
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

	int	prev_priority;	/* for recording reclaim priority */
	int		obsolete;
	atomic_t	refcnt;
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
#define PCGF_ACTIVE	(1UL << PCG_ACTIVE)
#define PCGF_LOCK	(1UL << PCG_LOCK)
#define PCGF_FILE	(1UL << PCG_FILE)
static const unsigned long
pcg_default_flags[NR_CHARGE_TYPE] = {
	PCGF_CACHE | PCGF_FILE | PCGF_USED | PCGF_LOCK, /* File Cache */
	PCGF_ACTIVE | PCGF_USED | PCGF_LOCK, /* Anon */
	PCGF_ACTIVE | PCGF_CACHE | PCGF_USED | PCGF_LOCK, /* Shmem */
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

/*
 * Always modified under lru lock. Then, not necessary to preempt_disable()
 */
static void mem_cgroup_charge_statistics(struct mem_cgroup *mem,
					 struct page_cgroup *pc,
					 bool charge)
{
	int val = (charge)? 1 : -1;
	struct mem_cgroup_stat *stat = &mem->stat;
	struct mem_cgroup_stat_cpu *cpustat;

	VM_BUG_ON(!irqs_disabled());

	cpustat = &stat->cpustat[smp_processor_id()];
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

	return mem_cgroup_zoneinfo(mem, nid, zid);
}

static unsigned long mem_cgroup_get_all_zonestat(struct mem_cgroup *mem,
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

static void __mem_cgroup_remove_list(struct mem_cgroup_per_zone *mz,
			struct page_cgroup *pc)
{
	int lru = LRU_BASE;

	if (PageCgroupUnevictable(pc))
		lru = LRU_UNEVICTABLE;
	else {
		if (PageCgroupActive(pc))
			lru += LRU_ACTIVE;
		if (PageCgroupFile(pc))
			lru += LRU_FILE;
	}

	MEM_CGROUP_ZSTAT(mz, lru) -= 1;

	mem_cgroup_charge_statistics(pc->mem_cgroup, pc, false);
	list_del(&pc->lru);
}

static void __mem_cgroup_add_list(struct mem_cgroup_per_zone *mz,
				struct page_cgroup *pc, bool hot)
{
	int lru = LRU_BASE;

	if (PageCgroupUnevictable(pc))
		lru = LRU_UNEVICTABLE;
	else {
		if (PageCgroupActive(pc))
			lru += LRU_ACTIVE;
		if (PageCgroupFile(pc))
			lru += LRU_FILE;
	}

	MEM_CGROUP_ZSTAT(mz, lru) += 1;
	if (hot)
		list_add(&pc->lru, &mz->lists[lru]);
	else
		list_add_tail(&pc->lru, &mz->lists[lru]);

	mem_cgroup_charge_statistics(pc->mem_cgroup, pc, true);
}

static void __mem_cgroup_move_lists(struct page_cgroup *pc, enum lru_list lru)
{
	struct mem_cgroup_per_zone *mz = page_cgroup_zoneinfo(pc);
	int active    = PageCgroupActive(pc);
	int file      = PageCgroupFile(pc);
	int unevictable = PageCgroupUnevictable(pc);
	enum lru_list from = unevictable ? LRU_UNEVICTABLE :
				(LRU_FILE * !!file + !!active);

	if (lru == from)
		return;

	MEM_CGROUP_ZSTAT(mz, from) -= 1;
	/*
	 * However this is done under mz->lru_lock, another flags, which
	 * are not related to LRU, will be modified from out-of-lock.
	 * We have to use atomic set/clear flags.
	 */
	if (is_unevictable_lru(lru)) {
		ClearPageCgroupActive(pc);
		SetPageCgroupUnevictable(pc);
	} else {
		if (is_active_lru(lru))
			SetPageCgroupActive(pc);
		else
			ClearPageCgroupActive(pc);
		ClearPageCgroupUnevictable(pc);
	}

	MEM_CGROUP_ZSTAT(mz, lru) += 1;
	list_move(&pc->lru, &mz->lists[lru]);
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
void mem_cgroup_move_lists(struct page *page, enum lru_list lru)
{
	struct page_cgroup *pc;
	struct mem_cgroup_per_zone *mz;
	unsigned long flags;

	if (mem_cgroup_subsys.disabled)
		return;

	/*
	 * We cannot lock_page_cgroup while holding zone's lru_lock,
	 * because other holders of lock_page_cgroup can be interrupted
	 * with an attempt to rotate_reclaimable_page.  But we cannot
	 * safely get to page_cgroup without it, so just try_lock it:
	 * mem_cgroup_isolate_pages allows for page left on wrong list.
	 */
	pc = lookup_page_cgroup(page);
	if (!trylock_page_cgroup(pc))
		return;
	if (pc && PageCgroupUsed(pc)) {
		mz = page_cgroup_zoneinfo(pc);
		spin_lock_irqsave(&mz->lru_lock, flags);
		__mem_cgroup_move_lists(pc, lru);
		spin_unlock_irqrestore(&mz->lru_lock, flags);
	}
	unlock_page_cgroup(pc);
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

long mem_cgroup_calc_reclaim(struct mem_cgroup *mem, struct zone *zone,
					int priority, enum lru_list lru)
{
	long nr_pages;
	int nid = zone->zone_pgdat->node_id;
	int zid = zone_idx(zone);
	struct mem_cgroup_per_zone *mz = mem_cgroup_zoneinfo(mem, nid, zid);

	nr_pages = MEM_CGROUP_ZSTAT(mz, lru);

	return (nr_pages >> priority);
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

	spin_lock(&mz->lru_lock);
	scan = 0;
	list_for_each_entry_safe_reverse(pc, tmp, src, lru) {
		if (scan >= nr_to_scan)
			break;
		if (unlikely(!PageCgroupUsed(pc)))
			continue;
		page = pc->page;

		if (unlikely(!PageLRU(page)))
			continue;

		/*
		 * TODO: play better with lumpy reclaim, grabbing anything.
		 */
		if (PageUnevictable(page) ||
		    (PageActive(page) && !active) ||
		    (!PageActive(page) && active)) {
			__mem_cgroup_move_lists(pc, page_lru(page));
			continue;
		}

		scan++;
		list_move(&pc->lru, &pc_list);

		if (__isolate_lru_page(page, mode, file) == 0) {
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
 * Unlike exported interface, "oom" parameter is added. if oom==true,
 * oom-killer can be invoked.
 */
static int __mem_cgroup_try_charge(struct mm_struct *mm,
			gfp_t gfp_mask, struct mem_cgroup **memcg,
			bool oom)
{
	struct mem_cgroup *mem;
	int nr_retries = MEM_CGROUP_RECLAIM_RETRIES;
	/*
	 * We always charge the cgroup the mm_struct belongs to.
	 * The mm_struct's mem_cgroup changes on task migration if the
	 * thread group leader migrates. It's possible that mm is not
	 * set, if so charge the init_mm (happens for pagecache usage).
	 */
	if (likely(!*memcg)) {
		rcu_read_lock();
		mem = mem_cgroup_from_task(rcu_dereference(mm->owner));
		if (unlikely(!mem)) {
			rcu_read_unlock();
			return 0;
		}
		/*
		 * For every charge from the cgroup, increment reference count
		 */
		css_get(&mem->css);
		*memcg = mem;
		rcu_read_unlock();
	} else {
		mem = *memcg;
		css_get(&mem->css);
	}

	while (1) {
		int ret;
		bool noswap = false;

		ret = res_counter_charge(&mem->res, PAGE_SIZE);
		if (likely(!ret)) {
			if (!do_swap_account)
				break;
			ret = res_counter_charge(&mem->memsw, PAGE_SIZE);
			if (likely(!ret))
				break;
			/* mem+swap counter fails */
			res_counter_uncharge(&mem->res, PAGE_SIZE);
			noswap = true;
		}
		if (!(gfp_mask & __GFP_WAIT))
			goto nomem;

		if (try_to_free_mem_cgroup_pages(mem, gfp_mask, noswap))
			continue;

		/*
		 * try_to_free_mem_cgroup_pages() might not give us a full
		 * picture of reclaim. Some pages are reclaimed and might be
		 * moved to swap cache or just unmapped from the cgroup.
		 * Check the limit again to see if the reclaim reduced the
		 * current usage of the cgroup before giving up
		 *
		 */
		if (!do_swap_account &&
			res_counter_check_under_limit(&mem->res))
			continue;
		if (do_swap_account &&
			res_counter_check_under_limit(&mem->memsw))
			continue;

		if (!nr_retries--) {
			if (oom)
				mem_cgroup_out_of_memory(mem, gfp_mask);
			goto nomem;
		}
	}
	return 0;
nomem:
	css_put(&mem->css);
	return -ENOMEM;
}

/**
 * mem_cgroup_try_charge - get charge of PAGE_SIZE.
 * @mm: an mm_struct which is charged against. (when *memcg is NULL)
 * @gfp_mask: gfp_mask for reclaim.
 * @memcg: a pointer to memory cgroup which is charged against.
 *
 * charge against memory cgroup pointed by *memcg. if *memcg == NULL, estimated
 * memory cgroup from @mm is got and stored in *memcg.
 *
 * Returns 0 if success. -ENOMEM at failure.
 * This call can invoke OOM-Killer.
 */

int mem_cgroup_try_charge(struct mm_struct *mm,
			  gfp_t mask, struct mem_cgroup **memcg)
{
	return __mem_cgroup_try_charge(mm, mask, memcg, true);
}

/*
 * commit a charge got by mem_cgroup_try_charge() and makes page_cgroup to be
 * USED state. If already USED, uncharge and return.
 */

static void __mem_cgroup_commit_charge(struct mem_cgroup *mem,
				     struct page_cgroup *pc,
				     enum charge_type ctype)
{
	struct mem_cgroup_per_zone *mz;
	unsigned long flags;

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
	/*
	 * If a page is accounted as a page cache, insert to inactive list.
	 * If anon, insert to active list.
	 */
	pc->flags = pcg_default_flags[ctype];

	mz = page_cgroup_zoneinfo(pc);

	spin_lock_irqsave(&mz->lru_lock, flags);
	__mem_cgroup_add_list(mz, pc, true);
	spin_unlock_irqrestore(&mz->lru_lock, flags);
	unlock_page_cgroup(pc);
}

/**
 * mem_cgroup_move_account - move account of the page
 * @pc:	page_cgroup of the page.
 * @from: mem_cgroup which the page is moved from.
 * @to:	mem_cgroup which the page is moved to. @from != @to.
 *
 * The caller must confirm following.
 * 1. disable irq.
 * 2. lru_lock of old mem_cgroup(@from) should be held.
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

	VM_BUG_ON(!irqs_disabled());
	VM_BUG_ON(from == to);

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

	if (spin_trylock(&to_mz->lru_lock)) {
		__mem_cgroup_remove_list(from_mz, pc);
		css_put(&from->css);
		res_counter_uncharge(&from->res, PAGE_SIZE);
		if (do_swap_account)
			res_counter_uncharge(&from->memsw, PAGE_SIZE);
		pc->mem_cgroup = to;
		css_get(&to->css);
		__mem_cgroup_add_list(to_mz, pc, false);
		ret = 0;
		spin_unlock(&to_mz->lru_lock);
	}
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
	struct cgroup *cg = child->css.cgroup;
	struct cgroup *pcg = cg->parent;
	struct mem_cgroup *parent;
	struct mem_cgroup_per_zone *mz;
	unsigned long flags;
	int ret;

	/* Is ROOT ? */
	if (!pcg)
		return -EINVAL;

	parent = mem_cgroup_from_cont(pcg);

	ret = __mem_cgroup_try_charge(NULL, gfp_mask, &parent, false);
	if (ret)
		return ret;

	mz = mem_cgroup_zoneinfo(child,
			page_cgroup_nid(pc), page_cgroup_zid(pc));

	spin_lock_irqsave(&mz->lru_lock, flags);
	ret = mem_cgroup_move_account(pc, child, parent);
	spin_unlock_irqrestore(&mz->lru_lock, flags);

	/* drop extra refcnt */
	css_put(&parent->css);
	/* uncharge if move fails */
	if (ret) {
		res_counter_uncharge(&parent->res, PAGE_SIZE);
		if (do_swap_account)
			res_counter_uncharge(&parent->memsw, PAGE_SIZE);
	}

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
	if (ret)
		return ret;

	__mem_cgroup_commit_charge(mem, pc, ctype);
	return 0;
}

int mem_cgroup_newpage_charge(struct page *page,
			      struct mm_struct *mm, gfp_t gfp_mask)
{
	if (mem_cgroup_subsys.disabled)
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

int mem_cgroup_cache_charge(struct page *page, struct mm_struct *mm,
				gfp_t gfp_mask)
{
	if (mem_cgroup_subsys.disabled)
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

	if (unlikely(!mm))
		mm = &init_mm;

	if (page_is_file_cache(page))
		return mem_cgroup_charge_common(page, mm, gfp_mask,
				MEM_CGROUP_CHARGE_TYPE_CACHE, NULL);
	else
		return mem_cgroup_charge_common(page, mm, gfp_mask,
				MEM_CGROUP_CHARGE_TYPE_SHMEM, NULL);
}

int mem_cgroup_try_charge_swapin(struct mm_struct *mm,
				 struct page *page,
				 gfp_t mask, struct mem_cgroup **ptr)
{
	struct mem_cgroup *mem;
	swp_entry_t     ent;

	if (mem_cgroup_subsys.disabled)
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

	ent.val = page_private(page);

	mem = lookup_swap_cgroup(ent);
	if (!mem || mem->obsolete)
		goto charge_cur_mm;
	*ptr = mem;
	return __mem_cgroup_try_charge(NULL, mask, ptr, true);
charge_cur_mm:
	if (unlikely(!mm))
		mm = &init_mm;
	return __mem_cgroup_try_charge(mm, mask, ptr, true);
}

#ifdef CONFIG_SWAP

int mem_cgroup_cache_charge_swapin(struct page *page,
			struct mm_struct *mm, gfp_t mask, bool locked)
{
	int ret = 0;

	if (mem_cgroup_subsys.disabled)
		return 0;
	if (unlikely(!mm))
		mm = &init_mm;
	if (!locked)
		lock_page(page);
	/*
	 * If not locked, the page can be dropped from SwapCache until
	 * we reach here.
	 */
	if (PageSwapCache(page)) {
		struct mem_cgroup *mem = NULL;
		swp_entry_t ent;

		ent.val = page_private(page);
		if (do_swap_account) {
			mem = lookup_swap_cgroup(ent);
			if (mem && mem->obsolete)
				mem = NULL;
			if (mem)
				mm = NULL;
		}
		ret = mem_cgroup_charge_common(page, mm, mask,
				MEM_CGROUP_CHARGE_TYPE_SHMEM, mem);

		if (!ret && do_swap_account) {
			/* avoid double counting */
			mem = swap_cgroup_record(ent, NULL);
			if (mem) {
				res_counter_uncharge(&mem->memsw, PAGE_SIZE);
				mem_cgroup_put(mem);
			}
		}
	}
	if (!locked)
		unlock_page(page);

	return ret;
}
#endif

void mem_cgroup_commit_charge_swapin(struct page *page, struct mem_cgroup *ptr)
{
	struct page_cgroup *pc;

	if (mem_cgroup_subsys.disabled)
		return;
	if (!ptr)
		return;
	pc = lookup_page_cgroup(page);
	__mem_cgroup_commit_charge(ptr, pc, MEM_CGROUP_CHARGE_TYPE_MAPPED);
	/*
	 * Now swap is on-memory. This means this page may be
	 * counted both as mem and swap....double count.
	 * Fix it by uncharging from memsw. This SwapCache is stable
	 * because we're still under lock_page().
	 */
	if (do_swap_account) {
		swp_entry_t ent = {.val = page_private(page)};
		struct mem_cgroup *memcg;
		memcg = swap_cgroup_record(ent, NULL);
		if (memcg) {
			/* If memcg is obsolete, memcg can be != ptr */
			res_counter_uncharge(&memcg->memsw, PAGE_SIZE);
			mem_cgroup_put(memcg);
		}

	}
}

void mem_cgroup_cancel_charge_swapin(struct mem_cgroup *mem)
{
	if (mem_cgroup_subsys.disabled)
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
	unsigned long flags;

	if (mem_cgroup_subsys.disabled)
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

	ClearPageCgroupUsed(pc);

	mz = page_cgroup_zoneinfo(pc);
	spin_lock_irqsave(&mz->lru_lock, flags);
	__mem_cgroup_remove_list(mz, pc);
	spin_unlock_irqrestore(&mz->lru_lock, flags);
	unlock_page_cgroup(pc);

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
		swap_cgroup_record(ent, memcg);
		mem_cgroup_get(memcg);
	}
}

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
/*
 * called from swap_entry_free(). remove record in swap_cgroup and
 * uncharge "memsw" account.
 */
void mem_cgroup_uncharge_swap(swp_entry_t ent)
{
	struct mem_cgroup *memcg;

	if (!do_swap_account)
		return;

	memcg = swap_cgroup_record(ent, NULL);
	if (memcg) {
		res_counter_uncharge(&memcg->memsw, PAGE_SIZE);
		mem_cgroup_put(memcg);
	}
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

	if (mem_cgroup_subsys.disabled)
		return 0;

	pc = lookup_page_cgroup(page);
	lock_page_cgroup(pc);
	if (PageCgroupUsed(pc)) {
		mem = pc->mem_cgroup;
		css_get(&mem->css);
	}
	unlock_page_cgroup(pc);

	if (mem) {
		ret = mem_cgroup_try_charge(NULL, GFP_HIGHUSER_MOVABLE, &mem);
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
 * A call to try to shrink memory usage under specified resource controller.
 * This is typically used for page reclaiming for shmem for reducing side
 * effect of page allocation from shmem, which is used by some mem_cgroup.
 */
int mem_cgroup_shrink_usage(struct mm_struct *mm, gfp_t gfp_mask)
{
	struct mem_cgroup *mem;
	int progress = 0;
	int retry = MEM_CGROUP_RECLAIM_RETRIES;

	if (mem_cgroup_subsys.disabled)
		return 0;
	if (!mm)
		return 0;

	rcu_read_lock();
	mem = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (unlikely(!mem)) {
		rcu_read_unlock();
		return 0;
	}
	css_get(&mem->css);
	rcu_read_unlock();

	do {
		progress = try_to_free_mem_cgroup_pages(mem, gfp_mask, true);
		progress += res_counter_check_under_limit(&mem->res);
	} while (!progress && --retry);

	css_put(&mem->css);
	if (!retry)
		return -ENOMEM;
	return 0;
}

static DEFINE_MUTEX(set_limit_mutex);

static int mem_cgroup_resize_limit(struct mem_cgroup *memcg,
				unsigned long long val)
{

	int retry_count = MEM_CGROUP_RECLAIM_RETRIES;
	int progress;
	u64 memswlimit;
	int ret = 0;

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

		progress = try_to_free_mem_cgroup_pages(memcg,
				GFP_HIGHUSER_MOVABLE, false);
  		if (!progress)			retry_count--;
	}
	return ret;
}

int mem_cgroup_resize_memsw_limit(struct mem_cgroup *memcg,
				unsigned long long val)
{
	int retry_count = MEM_CGROUP_RECLAIM_RETRIES;
	u64 memlimit, oldusage, curusage;
	int ret;

	if (!do_swap_account)
		return -EINVAL;

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

		oldusage = res_counter_read_u64(&memcg->memsw, RES_USAGE);
		try_to_free_mem_cgroup_pages(memcg, GFP_HIGHUSER_MOVABLE, true);
		curusage = res_counter_read_u64(&memcg->memsw, RES_USAGE);
		if (curusage >= oldusage)
			retry_count--;
	}
	return ret;
}


/*
 * This routine traverse page_cgroup in given list and drop them all.
 * *And* this routine doesn't reclaim page itself, just removes page_cgroup.
 */
static int mem_cgroup_force_empty_list(struct mem_cgroup *mem,
			    struct mem_cgroup_per_zone *mz,
			    enum lru_list lru)
{
	struct page_cgroup *pc, *busy;
	unsigned long flags;
	unsigned long loop;
	struct list_head *list;
	int ret = 0;

	list = &mz->lists[lru];

	loop = MEM_CGROUP_ZSTAT(mz, lru);
	/* give some margin against EBUSY etc...*/
	loop += 256;
	busy = NULL;
	while (loop--) {
		ret = 0;
		spin_lock_irqsave(&mz->lru_lock, flags);
		if (list_empty(list)) {
			spin_unlock_irqrestore(&mz->lru_lock, flags);
			break;
		}
		pc = list_entry(list->prev, struct page_cgroup, lru);
		if (busy == pc) {
			list_move(&pc->lru, list);
			busy = 0;
			spin_unlock_irqrestore(&mz->lru_lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&mz->lru_lock, flags);

		ret = mem_cgroup_move_parent(pc, mem, GFP_HIGHUSER_MOVABLE);
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
		for_each_node_state(node, N_POSSIBLE) {
			for (zid = 0; !ret && zid < MAX_NR_ZONES; zid++) {
				struct mem_cgroup_per_zone *mz;
				enum lru_list l;
				mz = mem_cgroup_zoneinfo(mem, node, zid);
				for_each_lru(l) {
					ret = mem_cgroup_force_empty_list(mem,
								  mz, l);
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
		progress = try_to_free_mem_cgroup_pages(mem,
						  GFP_HIGHUSER_MOVABLE, false);
		if (!progress) {
			nr_retries--;
			/* maybe some writeback is necessary */
			congestion_wait(WRITE, HZ/10);
		}

	}
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

static const struct mem_cgroup_stat_desc {
	const char *msg;
	u64 unit;
} mem_cgroup_stat_desc[] = {
	[MEM_CGROUP_STAT_CACHE] = { "cache", PAGE_SIZE, },
	[MEM_CGROUP_STAT_RSS] = { "rss", PAGE_SIZE, },
	[MEM_CGROUP_STAT_PGPGIN_COUNT] = {"pgpgin", 1, },
	[MEM_CGROUP_STAT_PGPGOUT_COUNT] = {"pgpgout", 1, },
};

static int mem_control_stat_show(struct cgroup *cont, struct cftype *cft,
				 struct cgroup_map_cb *cb)
{
	struct mem_cgroup *mem_cont = mem_cgroup_from_cont(cont);
	struct mem_cgroup_stat *stat = &mem_cont->stat;
	int i;

	for (i = 0; i < ARRAY_SIZE(stat->cpustat[0].count); i++) {
		s64 val;

		val = mem_cgroup_read_stat(stat, i);
		val *= mem_cgroup_stat_desc[i].unit;
		cb->fill(cb, mem_cgroup_stat_desc[i].msg, val);
	}
	/* showing # of active pages */
	{
		unsigned long active_anon, inactive_anon;
		unsigned long active_file, inactive_file;
		unsigned long unevictable;

		inactive_anon = mem_cgroup_get_all_zonestat(mem_cont,
						LRU_INACTIVE_ANON);
		active_anon = mem_cgroup_get_all_zonestat(mem_cont,
						LRU_ACTIVE_ANON);
		inactive_file = mem_cgroup_get_all_zonestat(mem_cont,
						LRU_INACTIVE_FILE);
		active_file = mem_cgroup_get_all_zonestat(mem_cont,
						LRU_ACTIVE_FILE);
		unevictable = mem_cgroup_get_all_zonestat(mem_cont,
							LRU_UNEVICTABLE);

		cb->fill(cb, "active_anon", (active_anon) * PAGE_SIZE);
		cb->fill(cb, "inactive_anon", (inactive_anon) * PAGE_SIZE);
		cb->fill(cb, "active_file", (active_file) * PAGE_SIZE);
		cb->fill(cb, "inactive_file", (inactive_file) * PAGE_SIZE);
		cb->fill(cb, "unevictable", unevictable * PAGE_SIZE);

	}
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
		spin_lock_init(&mz->lru_lock);
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
 * When mem_cgroup is destroyed, mem->obsolete will be set to 0 and
 * entry which points to this memcg will be ignore at swapin.
 *
 * Removal of cgroup itself succeeds regardless of refs from swap.
 */

static void mem_cgroup_free(struct mem_cgroup *mem)
{
	if (atomic_read(&mem->refcnt) > 0)
		return;
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
		if (!mem->obsolete)
			return;
		mem_cgroup_free(mem);
	}
}


#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP
static void __init enable_swap_cgroup(void)
{
	if (!mem_cgroup_subsys.disabled && really_do_swap_account)
		do_swap_account = 1;
}
#else
static void __init enable_swap_cgroup(void)
{
}
#endif

static struct cgroup_subsys_state *
mem_cgroup_create(struct cgroup_subsys *ss, struct cgroup *cont)
{
	struct mem_cgroup *mem;
	int node;

	mem = mem_cgroup_alloc();
	if (!mem)
		return ERR_PTR(-ENOMEM);

	res_counter_init(&mem->res);
	res_counter_init(&mem->memsw);

	for_each_node_state(node, N_POSSIBLE)
		if (alloc_mem_cgroup_per_zone_info(mem, node))
			goto free_out;
	/* root ? */
	if (cont->parent == NULL)
		enable_swap_cgroup();

	return &mem->css;
free_out:
	for_each_node_state(node, N_POSSIBLE)
		free_mem_cgroup_per_zone_info(mem, node);
	mem_cgroup_free(mem);
	return ERR_PTR(-ENOMEM);
}

static void mem_cgroup_pre_destroy(struct cgroup_subsys *ss,
					struct cgroup *cont)
{
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);
	mem->obsolete = 1;
	mem_cgroup_force_empty(mem, false);
}

static void mem_cgroup_destroy(struct cgroup_subsys *ss,
				struct cgroup *cont)
{
	int node;
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);

	for_each_node_state(node, N_POSSIBLE)
		free_mem_cgroup_per_zone_info(mem, node);

	mem_cgroup_free(mem_cgroup_from_cont(cont));
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
	struct mm_struct *mm;
	struct mem_cgroup *mem, *old_mem;

	mm = get_task_mm(p);
	if (mm == NULL)
		return;

	mem = mem_cgroup_from_cont(cont);
	old_mem = mem_cgroup_from_cont(old_cont);

	/*
	 * Only thread group leaders are allowed to migrate, the mm_struct is
	 * in effect owned by the leader
	 */
	if (!thread_group_leader(p))
		goto out;

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

#ifdef CONFIG_CGROUP_MEM_RES_CTLR_SWAP

static int __init disable_swap_account(char *s)
{
	really_do_swap_account = 0;
	return 1;
}
__setup("noswapaccount", disable_swap_account);
#endif
