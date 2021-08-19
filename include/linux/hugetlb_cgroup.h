/*
 * Copyright IBM Corporation, 2012
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifndef _LINUX_HUGETLB_CGROUP_H
#define _LINUX_HUGETLB_CGROUP_H

#include <linux/mmdebug.h>

struct hugetlb_cgroup;
struct resv_map;
struct file_region;

/*
 * Minimum page order trackable by hugetlb cgroup.
 * At least 4 pages are necessary for all the tracking information.
 * The second tail page (hpage[2]) is the fault usage cgroup.
 * The third tail page (hpage[3]) is the reservation usage cgroup.
 */
#define HUGETLB_CGROUP_MIN_ORDER	2

#ifdef CONFIG_CGROUP_HUGETLB
enum hugetlb_memory_event {
	HUGETLB_MAX,
	HUGETLB_NR_MEMORY_EVENTS,
};

struct hugetlb_cgroup {
	struct cgroup_subsys_state css;

	/*
	 * the counter to account for hugepages from hugetlb.
	 */
	struct page_counter hugepage[HUGE_MAX_HSTATE];

	/*
	 * the counter to account for hugepage reservations from hugetlb.
	 */
	struct page_counter rsvd_hugepage[HUGE_MAX_HSTATE];

	atomic_long_t events[HUGE_MAX_HSTATE][HUGETLB_NR_MEMORY_EVENTS];
	atomic_long_t events_local[HUGE_MAX_HSTATE][HUGETLB_NR_MEMORY_EVENTS];

	/* Handle for "hugetlb.events" */
	struct cgroup_file events_file[HUGE_MAX_HSTATE];

	/* Handle for "hugetlb.events.local" */
	struct cgroup_file events_local_file[HUGE_MAX_HSTATE];
};

static inline struct hugetlb_cgroup *
__hugetlb_cgroup_from_page(struct page *page, bool rsvd)
{
	VM_BUG_ON_PAGE(!PageHuge(page), page);

	if (compound_order(page) < HUGETLB_CGROUP_MIN_ORDER)
		return NULL;
	if (rsvd)
		return (struct hugetlb_cgroup *)page[3].private;
	else
		return (struct hugetlb_cgroup *)page[2].private;
}

static inline struct hugetlb_cgroup *hugetlb_cgroup_from_page(struct page *page)
{
	return __hugetlb_cgroup_from_page(page, false);
}

static inline struct hugetlb_cgroup *
hugetlb_cgroup_from_page_rsvd(struct page *page)
{
	return __hugetlb_cgroup_from_page(page, true);
}

static inline int __set_hugetlb_cgroup(struct page *page,
				       struct hugetlb_cgroup *h_cg, bool rsvd)
{
	VM_BUG_ON_PAGE(!PageHuge(page), page);

	if (compound_order(page) < HUGETLB_CGROUP_MIN_ORDER)
		return -1;
	if (rsvd)
		page[3].private = (unsigned long)h_cg;
	else
		page[2].private = (unsigned long)h_cg;
	return 0;
}

static inline int set_hugetlb_cgroup(struct page *page,
				     struct hugetlb_cgroup *h_cg)
{
	return __set_hugetlb_cgroup(page, h_cg, false);
}

static inline int set_hugetlb_cgroup_rsvd(struct page *page,
					  struct hugetlb_cgroup *h_cg)
{
	return __set_hugetlb_cgroup(page, h_cg, true);
}

static inline bool hugetlb_cgroup_disabled(void)
{
	return !cgroup_subsys_enabled(hugetlb_cgrp_subsys);
}

static inline void hugetlb_cgroup_put_rsvd_cgroup(struct hugetlb_cgroup *h_cg)
{
	css_put(&h_cg->css);
}

extern int hugetlb_cgroup_charge_cgroup(int idx, unsigned long nr_pages,
					struct hugetlb_cgroup **ptr);
extern int hugetlb_cgroup_charge_cgroup_rsvd(int idx, unsigned long nr_pages,
					     struct hugetlb_cgroup **ptr);
extern void hugetlb_cgroup_commit_charge(int idx, unsigned long nr_pages,
					 struct hugetlb_cgroup *h_cg,
					 struct page *page);
extern void hugetlb_cgroup_commit_charge_rsvd(int idx, unsigned long nr_pages,
					      struct hugetlb_cgroup *h_cg,
					      struct page *page);
extern void hugetlb_cgroup_uncharge_page(int idx, unsigned long nr_pages,
					 struct page *page);
extern void hugetlb_cgroup_uncharge_page_rsvd(int idx, unsigned long nr_pages,
					      struct page *page);

extern void hugetlb_cgroup_uncharge_cgroup(int idx, unsigned long nr_pages,
					   struct hugetlb_cgroup *h_cg);
extern void hugetlb_cgroup_uncharge_cgroup_rsvd(int idx, unsigned long nr_pages,
						struct hugetlb_cgroup *h_cg);
extern void hugetlb_cgroup_uncharge_counter(struct resv_map *resv,
					    unsigned long start,
					    unsigned long end);

extern void hugetlb_cgroup_uncharge_file_region(struct resv_map *resv,
						struct file_region *rg,
						unsigned long nr_pages,
						bool region_del);

extern void hugetlb_cgroup_file_init(void) __init;
extern void hugetlb_cgroup_migrate(struct page *oldhpage,
				   struct page *newhpage);

#else
static inline void hugetlb_cgroup_uncharge_file_region(struct resv_map *resv,
						       struct file_region *rg,
						       unsigned long nr_pages,
						       bool region_del)
{
}

static inline struct hugetlb_cgroup *hugetlb_cgroup_from_page(struct page *page)
{
	return NULL;
}

static inline struct hugetlb_cgroup *
hugetlb_cgroup_from_page_resv(struct page *page)
{
	return NULL;
}

static inline struct hugetlb_cgroup *
hugetlb_cgroup_from_page_rsvd(struct page *page)
{
	return NULL;
}

static inline int set_hugetlb_cgroup(struct page *page,
				     struct hugetlb_cgroup *h_cg)
{
	return 0;
}

static inline int set_hugetlb_cgroup_rsvd(struct page *page,
					  struct hugetlb_cgroup *h_cg)
{
	return 0;
}

static inline bool hugetlb_cgroup_disabled(void)
{
	return true;
}

static inline void hugetlb_cgroup_put_rsvd_cgroup(struct hugetlb_cgroup *h_cg)
{
}

static inline int hugetlb_cgroup_charge_cgroup(int idx, unsigned long nr_pages,
					       struct hugetlb_cgroup **ptr)
{
	return 0;
}

static inline int hugetlb_cgroup_charge_cgroup_rsvd(int idx,
						    unsigned long nr_pages,
						    struct hugetlb_cgroup **ptr)
{
	return 0;
}

static inline void hugetlb_cgroup_commit_charge(int idx, unsigned long nr_pages,
						struct hugetlb_cgroup *h_cg,
						struct page *page)
{
}

static inline void
hugetlb_cgroup_commit_charge_rsvd(int idx, unsigned long nr_pages,
				  struct hugetlb_cgroup *h_cg,
				  struct page *page)
{
}

static inline void hugetlb_cgroup_uncharge_page(int idx, unsigned long nr_pages,
						struct page *page)
{
}

static inline void hugetlb_cgroup_uncharge_page_rsvd(int idx,
						     unsigned long nr_pages,
						     struct page *page)
{
}
static inline void hugetlb_cgroup_uncharge_cgroup(int idx,
						  unsigned long nr_pages,
						  struct hugetlb_cgroup *h_cg)
{
}

static inline void
hugetlb_cgroup_uncharge_cgroup_rsvd(int idx, unsigned long nr_pages,
				    struct hugetlb_cgroup *h_cg)
{
}

static inline void hugetlb_cgroup_uncharge_counter(struct resv_map *resv,
						   unsigned long start,
						   unsigned long end)
{
}

static inline void hugetlb_cgroup_file_init(void)
{
}

static inline void hugetlb_cgroup_migrate(struct page *oldhpage,
					  struct page *newhpage)
{
}

#endif  /* CONFIG_MEM_RES_CTLR_HUGETLB */
#endif
