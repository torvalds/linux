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
#include <linux/page-flags.h>
#include <linux/backing-dev.h>
#include <linux/bit_spinlock.h>
#include <linux/rcupdate.h>
#include <linux/swap.h>
#include <linux/spinlock.h>
#include <linux/fs.h>

struct cgroup_subsys mem_cgroup_subsys;
static const int MEM_CGROUP_RECLAIM_RETRIES = 5;

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
	 * TODO: Consider making these lists per zone
	 */
	struct list_head active_list;
	struct list_head inactive_list;
	/*
	 * spin_lock to protect the per cgroup LRU
	 */
	spinlock_t lru_lock;
};

/*
 * We use the lower bit of the page->page_cgroup pointer as a bit spin
 * lock. We need to ensure that page->page_cgroup is atleast two
 * byte aligned (based on comments from Nick Piggin)
 */
#define PAGE_CGROUP_LOCK_BIT 	0x0
#define PAGE_CGROUP_LOCK 		(1 << PAGE_CGROUP_LOCK_BIT)

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
};


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
	return bit_spin_is_locked(PAGE_CGROUP_LOCK_BIT,
					&page->page_cgroup);
}

void page_assign_page_cgroup(struct page *page, struct page_cgroup *pc)
{
	int locked;

	/*
	 * While resetting the page_cgroup we might not hold the
	 * page_cgroup lock. free_hot_cold_page() is an example
	 * of such a scenario
	 */
	if (pc)
		VM_BUG_ON(!page_cgroup_locked(page));
	locked = (page->page_cgroup & PAGE_CGROUP_LOCK);
	page->page_cgroup = ((unsigned long)pc | locked);
}

struct page_cgroup *page_get_page_cgroup(struct page *page)
{
	return (struct page_cgroup *)
		(page->page_cgroup & ~PAGE_CGROUP_LOCK);
}

void __always_inline lock_page_cgroup(struct page *page)
{
	bit_spin_lock(PAGE_CGROUP_LOCK_BIT, &page->page_cgroup);
	VM_BUG_ON(!page_cgroup_locked(page));
}

void __always_inline unlock_page_cgroup(struct page *page)
{
	bit_spin_unlock(PAGE_CGROUP_LOCK_BIT, &page->page_cgroup);
}

void __mem_cgroup_move_lists(struct page_cgroup *pc, bool active)
{
	if (active)
		list_move(&pc->lru, &pc->mem_cgroup->active_list);
	else
		list_move(&pc->lru, &pc->mem_cgroup->inactive_list);
}

/*
 * This routine assumes that the appropriate zone's lru lock is already held
 */
void mem_cgroup_move_lists(struct page_cgroup *pc, bool active)
{
	struct mem_cgroup *mem;
	if (!pc)
		return;

	mem = pc->mem_cgroup;

	spin_lock(&mem->lru_lock);
	__mem_cgroup_move_lists(pc, active);
	spin_unlock(&mem->lru_lock);
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
	struct page_cgroup *pc;

	if (active)
		src = &mem_cont->active_list;
	else
		src = &mem_cont->inactive_list;

	spin_lock(&mem_cont->lru_lock);
	for (scan = 0; scan < nr_to_scan && !list_empty(src); scan++) {
		pc = list_entry(src->prev, struct page_cgroup, lru);
		page = pc->page;
		VM_BUG_ON(!pc);

		if (PageActive(page) && !active) {
			__mem_cgroup_move_lists(pc, true);
			scan--;
			continue;
		}
		if (!PageActive(page) && active) {
			__mem_cgroup_move_lists(pc, false);
			scan--;
			continue;
		}

		/*
		 * Reclaim, per zone
		 * TODO: make the active/inactive lists per zone
		 */
		if (page_zone(page) != z)
			continue;

		/*
		 * Check if the meta page went away from under us
		 */
		if (!list_empty(&pc->lru))
			list_move(&pc->lru, &pc_list);
		else
			continue;

		if (__isolate_lru_page(page, mode) == 0) {
			list_move(&page->lru, dst);
			nr_taken++;
		}
	}

	list_splice(&pc_list, src);
	spin_unlock(&mem_cont->lru_lock);

	*scanned = scan;
	return nr_taken;
}

/*
 * Charge the memory controller for page usage.
 * Return
 * 0 if the charge was successful
 * < 0 if the cgroup is over its limit
 */
int mem_cgroup_charge(struct page *page, struct mm_struct *mm)
{
	struct mem_cgroup *mem;
	struct page_cgroup *pc, *race_pc;
	unsigned long flags;
	unsigned long nr_retries = MEM_CGROUP_RECLAIM_RETRIES;

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
	 * The page_cgroup exists and the page has already been accounted
	 */
	if (pc) {
		if (unlikely(!atomic_inc_not_zero(&pc->ref_cnt))) {
			/* this page is under being uncharged ? */
			unlock_page_cgroup(page);
			cpu_relax();
			goto retry;
		} else
			goto done;
	}

	unlock_page_cgroup(page);

	pc = kzalloc(sizeof(struct page_cgroup), GFP_KERNEL);
	if (pc == NULL)
		goto err;

	rcu_read_lock();
	/*
	 * We always charge the cgroup the mm_struct belongs to
	 * the mm_struct's mem_cgroup changes on task migration if the
	 * thread group leader migrates. It's possible that mm is not
	 * set, if so charge the init_mm (happens for pagecache usage).
	 */
	if (!mm)
		mm = &init_mm;

	mem = rcu_dereference(mm->mem_cgroup);
	/*
	 * For every charge from the cgroup, increment reference
	 * count
	 */
	css_get(&mem->css);
	rcu_read_unlock();

	/*
	 * If we created the page_cgroup, we should free it on exceeding
	 * the cgroup limit.
	 */
	while (res_counter_charge(&mem->res, 1)) {
		if (try_to_free_mem_cgroup_pages(mem))
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
			/*
			 * Since we control both RSS and cache, we end up with a
			 * very interesting scenario where we end up reclaiming
			 * memory (essentially RSS), since the memory is pushed
			 * to swap cache, we eventually end up adding those
			 * pages back to our list. Hence we give ourselves a
			 * few chances before we fail
			 */
		else if (nr_retries--) {
			congestion_wait(WRITE, HZ/10);
			continue;
		}

		css_put(&mem->css);
		goto free_pc;
	}

	lock_page_cgroup(page);
	/*
	 * Check if somebody else beat us to allocating the page_cgroup
	 */
	race_pc = page_get_page_cgroup(page);
	if (race_pc) {
		kfree(pc);
		pc = race_pc;
		atomic_inc(&pc->ref_cnt);
		res_counter_uncharge(&mem->res, 1);
		css_put(&mem->css);
		goto done;
	}

	atomic_set(&pc->ref_cnt, 1);
	pc->mem_cgroup = mem;
	pc->page = page;
	page_assign_page_cgroup(page, pc);

	spin_lock_irqsave(&mem->lru_lock, flags);
	list_add(&pc->lru, &mem->active_list);
	spin_unlock_irqrestore(&mem->lru_lock, flags);

done:
	unlock_page_cgroup(page);
	return 0;
free_pc:
	kfree(pc);
err:
	return -ENOMEM;
}

/*
 * Uncharging is always a welcome operation, we never complain, simply
 * uncharge.
 */
void mem_cgroup_uncharge(struct page_cgroup *pc)
{
	struct mem_cgroup *mem;
	struct page *page;
	unsigned long flags;

	if (!pc)
		return;

	if (atomic_dec_and_test(&pc->ref_cnt)) {
		page = pc->page;
		lock_page_cgroup(page);
		mem = pc->mem_cgroup;
		css_put(&mem->css);
		page_assign_page_cgroup(page, NULL);
		unlock_page_cgroup(page);
		res_counter_uncharge(&mem->res, 1);

 		spin_lock_irqsave(&mem->lru_lock, flags);
 		list_del_init(&pc->lru);
 		spin_unlock_irqrestore(&mem->lru_lock, flags);
		kfree(pc);
	}
}

static ssize_t mem_cgroup_read(struct cgroup *cont, struct cftype *cft,
			struct file *file, char __user *userbuf, size_t nbytes,
			loff_t *ppos)
{
	return res_counter_read(&mem_cgroup_from_cont(cont)->res,
				cft->private, userbuf, nbytes, ppos);
}

static ssize_t mem_cgroup_write(struct cgroup *cont, struct cftype *cft,
				struct file *file, const char __user *userbuf,
				size_t nbytes, loff_t *ppos)
{
	return res_counter_write(&mem_cgroup_from_cont(cont)->res,
				cft->private, userbuf, nbytes, ppos);
}

static struct cftype mem_cgroup_files[] = {
	{
		.name = "usage",
		.private = RES_USAGE,
		.read = mem_cgroup_read,
	},
	{
		.name = "limit",
		.private = RES_LIMIT,
		.write = mem_cgroup_write,
		.read = mem_cgroup_read,
	},
	{
		.name = "failcnt",
		.private = RES_FAILCNT,
		.read = mem_cgroup_read,
	},
};

static struct mem_cgroup init_mem_cgroup;

static struct cgroup_subsys_state *
mem_cgroup_create(struct cgroup_subsys *ss, struct cgroup *cont)
{
	struct mem_cgroup *mem;

	if (unlikely((cont->parent) == NULL)) {
		mem = &init_mem_cgroup;
		init_mm.mem_cgroup = mem;
	} else
		mem = kzalloc(sizeof(struct mem_cgroup), GFP_KERNEL);

	if (mem == NULL)
		return NULL;

	res_counter_init(&mem->res);
	INIT_LIST_HEAD(&mem->active_list);
	INIT_LIST_HEAD(&mem->inactive_list);
	spin_lock_init(&mem->lru_lock);
	return &mem->css;
}

static void mem_cgroup_destroy(struct cgroup_subsys *ss,
				struct cgroup *cont)
{
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
	return;
}

struct cgroup_subsys mem_cgroup_subsys = {
	.name = "memory",
	.subsys_id = mem_cgroup_subsys_id,
	.create = mem_cgroup_create,
	.destroy = mem_cgroup_destroy,
	.populate = mem_cgroup_populate,
	.attach = mem_cgroup_move_task,
	.early_init = 1,
};
