/*
 *
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

#include <linux/cgroup.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/hugetlb_cgroup.h>

struct hugetlb_cgroup {
	struct cgroup_subsys_state css;
	/*
	 * the counter to account for hugepages from hugetlb.
	 */
	struct res_counter hugepage[HUGE_MAX_HSTATE];
};

struct cgroup_subsys hugetlb_subsys __read_mostly;
static struct hugetlb_cgroup *root_h_cgroup __read_mostly;

static inline
struct hugetlb_cgroup *hugetlb_cgroup_from_css(struct cgroup_subsys_state *s)
{
	return container_of(s, struct hugetlb_cgroup, css);
}

static inline
struct hugetlb_cgroup *hugetlb_cgroup_from_cgroup(struct cgroup *cgroup)
{
	return hugetlb_cgroup_from_css(cgroup_subsys_state(cgroup,
							   hugetlb_subsys_id));
}

static inline
struct hugetlb_cgroup *hugetlb_cgroup_from_task(struct task_struct *task)
{
	return hugetlb_cgroup_from_css(task_subsys_state(task,
							 hugetlb_subsys_id));
}

static inline bool hugetlb_cgroup_is_root(struct hugetlb_cgroup *h_cg)
{
	return (h_cg == root_h_cgroup);
}

static inline struct hugetlb_cgroup *parent_hugetlb_cgroup(struct cgroup *cg)
{
	if (!cg->parent)
		return NULL;
	return hugetlb_cgroup_from_cgroup(cg->parent);
}

static inline bool hugetlb_cgroup_have_usage(struct cgroup *cg)
{
	int idx;
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_cgroup(cg);

	for (idx = 0; idx < hugetlb_max_hstate; idx++) {
		if ((res_counter_read_u64(&h_cg->hugepage[idx], RES_USAGE)) > 0)
			return true;
	}
	return false;
}

static struct cgroup_subsys_state *hugetlb_cgroup_create(struct cgroup *cgroup)
{
	int idx;
	struct cgroup *parent_cgroup;
	struct hugetlb_cgroup *h_cgroup, *parent_h_cgroup;

	h_cgroup = kzalloc(sizeof(*h_cgroup), GFP_KERNEL);
	if (!h_cgroup)
		return ERR_PTR(-ENOMEM);

	parent_cgroup = cgroup->parent;
	if (parent_cgroup) {
		parent_h_cgroup = hugetlb_cgroup_from_cgroup(parent_cgroup);
		for (idx = 0; idx < HUGE_MAX_HSTATE; idx++)
			res_counter_init(&h_cgroup->hugepage[idx],
					 &parent_h_cgroup->hugepage[idx]);
	} else {
		root_h_cgroup = h_cgroup;
		for (idx = 0; idx < HUGE_MAX_HSTATE; idx++)
			res_counter_init(&h_cgroup->hugepage[idx], NULL);
	}
	return &h_cgroup->css;
}

static void hugetlb_cgroup_destroy(struct cgroup *cgroup)
{
	struct hugetlb_cgroup *h_cgroup;

	h_cgroup = hugetlb_cgroup_from_cgroup(cgroup);
	kfree(h_cgroup);
}

static int hugetlb_cgroup_pre_destroy(struct cgroup *cgroup)
{
	/* We will add the cgroup removal support in later patches */
	   return -EBUSY;
}

struct cgroup_subsys hugetlb_subsys = {
	.name = "hugetlb",
	.create     = hugetlb_cgroup_create,
	.pre_destroy = hugetlb_cgroup_pre_destroy,
	.destroy    = hugetlb_cgroup_destroy,
	.subsys_id  = hugetlb_subsys_id,
};
