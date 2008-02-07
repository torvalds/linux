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
#include <linux/bit_spinlock.h>
#include <linux/rcupdate.h>

struct cgroup_subsys mem_cgroup_subsys;

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

	/*
	 * Should page_cgroup's go to their own slab?
	 * One could optimize the performance of the charging routine
	 * by saving a bit in the page_flags and using it as a lock
	 * to see if the cgroup page already has a page_cgroup associated
	 * with it
	 */
	lock_page_cgroup(page);
	pc = page_get_page_cgroup(page);
	/*
	 * The page_cgroup exists and the page has already been accounted
	 */
	if (pc) {
		atomic_inc(&pc->ref_cnt);
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
	if (res_counter_charge(&mem->res, 1)) {
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

done:
	unlock_page_cgroup(page);
	return 0;
free_pc:
	kfree(pc);
	return -ENOMEM;
err:
	unlock_page_cgroup(page);
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
