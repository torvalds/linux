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

#ifdef CONFIG_CGROUP_MEM_CONT

extern void mm_init_cgroup(struct mm_struct *mm, struct task_struct *p);
extern void mm_free_cgroup(struct mm_struct *mm);
extern void page_assign_page_cgroup(struct page *page,
					struct page_cgroup *pc);
extern struct page_cgroup *page_get_page_cgroup(struct page *page);
extern int mem_cgroup_charge(struct page *page, struct mm_struct *mm);
extern void mem_cgroup_uncharge(struct page_cgroup *pc);
extern void mem_cgroup_move_lists(struct page_cgroup *pc, bool active);
extern unsigned long mem_cgroup_isolate_pages(unsigned long nr_to_scan,
					struct list_head *dst,
					unsigned long *scanned, int order,
					int mode, struct zone *z,
					struct mem_cgroup *mem_cont,
					int active);
extern void mem_cgroup_out_of_memory(struct mem_cgroup *mem, gfp_t gfp_mask);

static inline void mem_cgroup_uncharge_page(struct page *page)
{
	mem_cgroup_uncharge(page_get_page_cgroup(page));
}

#else /* CONFIG_CGROUP_MEM_CONT */
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

static inline int mem_cgroup_charge(struct page *page, struct mm_struct *mm)
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

#endif /* CONFIG_CGROUP_MEM_CONT */

#endif /* _LINUX_MEMCONTROL_H */

