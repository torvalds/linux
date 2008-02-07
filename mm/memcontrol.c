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

#include <asm/uaccess.h>

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
	unsigned long control_type;	/* control RSS or RSS+Pagecache */
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
	int	 flags;
};
#define PAGE_CGROUP_FLAG_CACHE	(0x1)	/* charged as cache */

enum {
	MEM_CGROUP_TYPE_UNSPEC = 0,
	MEM_CGROUP_TYPE_MAPPED,
	MEM_CGROUP_TYPE_CACHED,
	MEM_CGROUP_TYPE_ALL,
	MEM_CGROUP_TYPE_MAX,
};

enum charge_type {
	MEM_CGROUP_CHARGE_TYPE_CACHE = 0,
	MEM_CGROUP_CHARGE_TYPE_MAPPED,
};

static struct mem_cgroup init_mem_cgroup;

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
 * Tie new page_cgroup to struct page under lock_page_cgroup()
 * This can fail if the page has been tied to a page_cgroup.
 * If success, returns 0.
 */
static inline int
page_cgroup_assign_new_page_cgroup(struct page *page, struct page_cgroup *pc)
{
	int ret = 0;

	lock_page_cgroup(page);
	if (!page_get_page_cgroup(page))
		page_assign_page_cgroup(page, pc);
	else /* A page is tied to other pc. */
		ret = 1;
	unlock_page_cgroup(page);
	return ret;
}

/*
 * Clear page->page_cgroup member under lock_page_cgroup().
 * If given "pc" value is different from one page->page_cgroup,
 * page->cgroup is not cleared.
 * Returns a value of page->page_cgroup at lock taken.
 * A can can detect failure of clearing by following
 *  clear_page_cgroup(page, pc) == pc
 */

static inline struct page_cgroup *
clear_page_cgroup(struct page *page, struct page_cgroup *pc)
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


static void __mem_cgroup_move_lists(struct page_cgroup *pc, bool active)
{
	if (active)
		list_move(&pc->lru, &pc->mem_cgroup->active_list);
	else
		list_move(&pc->lru, &pc->mem_cgroup->inactive_list);
}

int task_in_mem_cgroup(struct task_struct *task, const struct mem_cgroup *mem)
{
	int ret;

	task_lock(task);
	ret = task->mm && mm_cgroup(task->mm) == mem;
	task_unlock(task);
	return ret;
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
	struct page_cgroup *pc, *tmp;

	if (active)
		src = &mem_cont->active_list;
	else
		src = &mem_cont->inactive_list;

	spin_lock(&mem_cont->lru_lock);
	scan = 0;
	list_for_each_entry_safe_reverse(pc, tmp, src, lru) {
		if (scan >= nr_to_scan)
			break;
		page = pc->page;
		VM_BUG_ON(!pc);

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

		/*
		 * Reclaim, per zone
		 * TODO: make the active/inactive lists per zone
		 */
		if (page_zone(page) != z)
			continue;

		scan++;
		list_move(&pc->lru, &pc_list);

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
static int mem_cgroup_charge_common(struct page *page, struct mm_struct *mm,
				gfp_t gfp_mask, enum charge_type ctype)
{
	struct mem_cgroup *mem;
	struct page_cgroup *pc;
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
	if (page) {
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
	}

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
	 * For every charge from the cgroup, increment reference
	 * count
	 */
	css_get(&mem->css);
	rcu_read_unlock();

	/*
	 * If we created the page_cgroup, we should free it on exceeding
	 * the cgroup limit.
	 */
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
	pc->flags = 0;
	if (ctype == MEM_CGROUP_CHARGE_TYPE_CACHE)
		pc->flags |= PAGE_CGROUP_FLAG_CACHE;

	if (!page || page_cgroup_assign_new_page_cgroup(page, pc)) {
		/*
		 * Another charge has been added to this page already.
		 * We take lock_page_cgroup(page) again and read
		 * page->cgroup, increment refcnt.... just retry is OK.
		 */
		res_counter_uncharge(&mem->res, PAGE_SIZE);
		css_put(&mem->css);
		kfree(pc);
		if (!page)
			goto done;
		goto retry;
	}

	spin_lock_irqsave(&mem->lru_lock, flags);
	list_add(&pc->lru, &mem->active_list);
	spin_unlock_irqrestore(&mem->lru_lock, flags);

done:
	return 0;
out:
	css_put(&mem->css);
	kfree(pc);
err:
	return -ENOMEM;
}

int mem_cgroup_charge(struct page *page, struct mm_struct *mm,
			gfp_t gfp_mask)
{
	return mem_cgroup_charge_common(page, mm, gfp_mask,
			MEM_CGROUP_CHARGE_TYPE_MAPPED);
}

/*
 * See if the cached pages should be charged at all?
 */
int mem_cgroup_cache_charge(struct page *page, struct mm_struct *mm,
				gfp_t gfp_mask)
{
	int ret = 0;
	struct mem_cgroup *mem;
	if (!mm)
		mm = &init_mm;

	rcu_read_lock();
	mem = rcu_dereference(mm->mem_cgroup);
	css_get(&mem->css);
	rcu_read_unlock();
	if (mem->control_type == MEM_CGROUP_TYPE_ALL)
		ret = mem_cgroup_charge_common(page, mm, gfp_mask,
				MEM_CGROUP_CHARGE_TYPE_CACHE);
	css_put(&mem->css);
	return ret;
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

	/*
	 * This can handle cases when a page is not charged at all and we
	 * are switching between handling the control_type.
	 */
	if (!pc)
		return;

	if (atomic_dec_and_test(&pc->ref_cnt)) {
		page = pc->page;
		/*
		 * get page->cgroup and clear it under lock.
		 * force_empty can drop page->cgroup without checking refcnt.
		 */
		if (clear_page_cgroup(page, pc) == pc) {
			mem = pc->mem_cgroup;
			css_put(&mem->css);
			res_counter_uncharge(&mem->res, PAGE_SIZE);
			spin_lock_irqsave(&mem->lru_lock, flags);
			list_del_init(&pc->lru);
			spin_unlock_irqrestore(&mem->lru_lock, flags);
			kfree(pc);
		}
	}
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
	struct page_cgroup *pc = page_get_page_cgroup(page);
	mem_cgroup_uncharge(pc);
}
/*
 * We know both *page* and *newpage* are now not-on-LRU and Pg_locked.
 * And no race with uncharge() routines because page_cgroup for *page*
 * has extra one reference by mem_cgroup_prepare_migration.
 */

void mem_cgroup_page_migration(struct page *page, struct page *newpage)
{
	struct page_cgroup *pc;
retry:
	pc = page_get_page_cgroup(page);
	if (!pc)
		return;
	if (clear_page_cgroup(page, pc) != pc)
		goto retry;
	pc->page = newpage;
	lock_page_cgroup(newpage);
	page_assign_page_cgroup(newpage, pc);
	unlock_page_cgroup(newpage);
	return;
}

/*
 * This routine traverse page_cgroup in given list and drop them all.
 * This routine ignores page_cgroup->ref_cnt.
 * *And* this routine doesn't reclaim page itself, just removes page_cgroup.
 */
#define FORCE_UNCHARGE_BATCH	(128)
static void
mem_cgroup_force_empty_list(struct mem_cgroup *mem, struct list_head *list)
{
	struct page_cgroup *pc;
	struct page *page;
	int count;
	unsigned long flags;

retry:
	count = FORCE_UNCHARGE_BATCH;
	spin_lock_irqsave(&mem->lru_lock, flags);

	while (--count && !list_empty(list)) {
		pc = list_entry(list->prev, struct page_cgroup, lru);
		page = pc->page;
		/* Avoid race with charge */
		atomic_set(&pc->ref_cnt, 0);
		if (clear_page_cgroup(page, pc) == pc) {
			css_put(&mem->css);
			res_counter_uncharge(&mem->res, PAGE_SIZE);
			list_del_init(&pc->lru);
			kfree(pc);
		} else 	/* being uncharged ? ...do relax */
			break;
	}
	spin_unlock_irqrestore(&mem->lru_lock, flags);
	if (!list_empty(list)) {
		cond_resched();
		goto retry;
	}
	return;
}

/*
 * make mem_cgroup's charge to be 0 if there is no task.
 * This enables deleting this mem_cgroup.
 */

int mem_cgroup_force_empty(struct mem_cgroup *mem)
{
	int ret = -EBUSY;
	css_get(&mem->css);
	/*
	 * page reclaim code (kswapd etc..) will move pages between
`	 * active_list <-> inactive_list while we don't take a lock.
	 * So, we have to do loop here until all lists are empty.
	 */
	while (!(list_empty(&mem->active_list) &&
		 list_empty(&mem->inactive_list))) {
		if (atomic_read(&mem->css.cgroup->count) > 0)
			goto out;
		/* drop all page_cgroup in active_list */
		mem_cgroup_force_empty_list(mem, &mem->active_list);
		/* drop all page_cgroup in inactive_list */
		mem_cgroup_force_empty_list(mem, &mem->inactive_list);
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

static ssize_t mem_control_type_write(struct cgroup *cont,
			struct cftype *cft, struct file *file,
			const char __user *userbuf,
			size_t nbytes, loff_t *pos)
{
	int ret;
	char *buf, *end;
	unsigned long tmp;
	struct mem_cgroup *mem;

	mem = mem_cgroup_from_cont(cont);
	buf = kmalloc(nbytes + 1, GFP_KERNEL);
	ret = -ENOMEM;
	if (buf == NULL)
		goto out;

	buf[nbytes] = 0;
	ret = -EFAULT;
	if (copy_from_user(buf, userbuf, nbytes))
		goto out_free;

	ret = -EINVAL;
	tmp = simple_strtoul(buf, &end, 10);
	if (*end != '\0')
		goto out_free;

	if (tmp <= MEM_CGROUP_TYPE_UNSPEC || tmp >= MEM_CGROUP_TYPE_MAX)
		goto out_free;

	mem->control_type = tmp;
	ret = nbytes;
out_free:
	kfree(buf);
out:
	return ret;
}

static ssize_t mem_control_type_read(struct cgroup *cont,
				struct cftype *cft,
				struct file *file, char __user *userbuf,
				size_t nbytes, loff_t *ppos)
{
	unsigned long val;
	char buf[64], *s;
	struct mem_cgroup *mem;

	mem = mem_cgroup_from_cont(cont);
	s = buf;
	val = mem->control_type;
	s += sprintf(s, "%lu\n", val);
	return simple_read_from_buffer((void __user *)userbuf, nbytes,
			ppos, buf, s - buf);
}


static ssize_t mem_force_empty_write(struct cgroup *cont,
				struct cftype *cft, struct file *file,
				const char __user *userbuf,
				size_t nbytes, loff_t *ppos)
{
	struct mem_cgroup *mem = mem_cgroup_from_cont(cont);
	int ret;
	ret = mem_cgroup_force_empty(mem);
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
		.name = "control_type",
		.write = mem_control_type_write,
		.read = mem_control_type_read,
	},
	{
		.name = "force_empty",
		.write = mem_force_empty_write,
		.read = mem_force_empty_read,
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
	mem->control_type = MEM_CGROUP_TYPE_ALL;
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
