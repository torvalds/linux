/* memcontrol.h - Memory Controller
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

#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H

struct mem_cgroup;
struct page_cgroup;
struct page;
struct mm_struct;

#ifdef CONFIG_CGROUP_MEM_RES_CTLR

extern void mm_init_cgroup(struct mm_struct *mm, struct task_struct *p);
extern void mm_free_cgroup(struct mm_struct *mm);
extern void page_assign_page_cgroup(struct page *page,
					struct page_cgroup *pc);
extern struct page_cgroup *page_get_page_cgroup(struct page *page);
extern int mem_cgroup_charge(struct page *page, struct mm_struct *mm,
				gfp_t gfp_mask);
extern void mem_cgroup_uncharge(struct page_cgroup *pc);
extern void mem_cgroup_uncharge_page(struct page *page);
extern void mem_cgroup_move_lists(struct page_cgroup *pc, bool active);
extern unsigned long mem_cgroup_isolate_pages(unsigned long nr_to_scan,
					struct list_head *dst,
					unsigned long *scanned, int order,
					int mode, struct zone *z,
					struct mem_cgroup *mem_cont,
					int active);
extern void mem_cgroup_out_of_memory(struct mem_cgroup *mem, gfp_t gfp_mask);
extern int mem_cgroup_cache_charge(struct page *page, struct mm_struct *mm,
					gfp_t gfp_mask);
int task_in_mem_cgroup(struct task_struct *task, const struct mem_cgroup *mem);

#define mm_match_cgroup(mm, cgroup)	\
	((cgroup) == rcu_dereference((mm)->mem_cgroup))

extern int mem_cgroup_prepare_migration(struct page *page);
extern void mem_cgroup_end_migration(struct page *page);
extern void mem_cgroup_page_migration(struct page *page, struct page *newpage);

/*
 * For memory reclaim.
 */
extern int mem_cgroup_calc_mapped_ratio(struct mem_cgroup *mem);
extern long mem_cgroup_reclaim_imbalance(struct mem_cgroup *mem);

extern int mem_cgroup_get_reclaim_priority(struct mem_cgroup *mem);
extern void mem_cgroup_note_reclaim_priority(struct mem_cgroup *mem,
							int priority);
extern void mem_cgroup_record_reclaim_priority(struct mem_cgroup *mem,
							int priority);

extern long mem_cgroup_calc_reclaim_active(struct mem_cgroup *mem,
				struct zone *zone, int priority);
extern long mem_cgroup_calc_reclaim_inactive(struct mem_cgroup *mem,
				struct zone *zone, int priority);

#else /* CONFIG_CGROUP_MEM_RES_CTLR */
static inline void mm_init_cgroup(struct mm_struct *mm,
					struct task_struct *p)
{
}

static inline void mm_free_cgroup(struct mm_struct *mm)
{
}

static inline void page_assign_page_cgroup(struct page *page,
						struct page_cgroup *pc)
{
}

static inline struct page_cgroup *page_get_page_cgroup(struct page *page)
{
	return NULL;
}

static inline int mem_cgroup_charge(struct page *page, struct mm_struct *mm,
					gfp_t gfp_mask)
{
	return 0;
}

static inline void mem_cgroup_uncharge(struct page_cgroup *pc)
{
}

static inline void mem_cgroup_uncharge_page(struct page *page)
{
}

static inline void mem_cgroup_move_lists(struct page_cgroup *pc,
						bool active)
{
}

static inline int mem_cgroup_cache_charge(struct page *page,
						struct mm_struct *mm,
						gfp_t gfp_mask)
{
	return 0;
}

static inline int mm_match_cgroup(struct mm_struct *mm, struct mem_cgroup *mem)
{
	return 1;
}

static inline int task_in_mem_cgroup(struct task_struct *task,
				     const struct mem_cgroup *mem)
{
	return 1;
}

static inline int mem_cgroup_prepare_migration(struct page *page)
{
	return 0;
}

static inline void mem_cgroup_end_migration(struct page *page)
{
}

static inline void
mem_cgroup_page_migration(struct page *page, struct page *newpage)
{
}

static inline int mem_cgroup_calc_mapped_ratio(struct mem_cgroup *mem)
{
	return 0;
}

static inline int mem_cgroup_reclaim_imbalance(struct mem_cgroup *mem)
{
	return 0;
}

static inline int mem_cgroup_get_reclaim_priority(struct mem_cgroup *mem)
{
	return 0;
}

static inline void mem_cgroup_note_reclaim_priority(struct mem_cgroup *mem,
						int priority)
{
}

static inline void mem_cgroup_record_reclaim_priority(struct mem_cgroup *mem,
						int priority)
{
}

static inline long mem_cgroup_calc_reclaim_active(struct mem_cgroup *mem,
					struct zone *zone, int priority)
{
	return 0;
}

static inline long mem_cgroup_calc_reclaim_inactive(struct mem_cgroup *mem,
					struct zone *zone, int priority)
{
	return 0;
}
#endif /* CONFIG_CGROUP_MEM_CONT */

#endif /* _LINUX_MEMCONTROL_H */

