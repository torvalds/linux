/*
 *
 * Copyright IBM Corporation, 2012
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * Cgroup v2
 * Copyright (C) 2019 Red Hat, Inc.
 * Author: Giuseppe Scrivano <gscrivan@redhat.com>
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
#include <linux/page_counter.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/hugetlb_cgroup.h>

#define MEMFILE_PRIVATE(x, val)	(((x) << 16) | (val))
#define MEMFILE_IDX(val)	(((val) >> 16) & 0xffff)
#define MEMFILE_ATTR(val)	((val) & 0xffff)

static struct hugetlb_cgroup *root_h_cgroup __read_mostly;

static inline struct page_counter *
__hugetlb_cgroup_counter_from_cgroup(struct hugetlb_cgroup *h_cg, int idx,
				     bool rsvd)
{
	if (rsvd)
		return &h_cg->rsvd_hugepage[idx];
	return &h_cg->hugepage[idx];
}

static inline struct page_counter *
hugetlb_cgroup_counter_from_cgroup(struct hugetlb_cgroup *h_cg, int idx)
{
	return __hugetlb_cgroup_counter_from_cgroup(h_cg, idx, false);
}

static inline struct page_counter *
hugetlb_cgroup_counter_from_cgroup_rsvd(struct hugetlb_cgroup *h_cg, int idx)
{
	return __hugetlb_cgroup_counter_from_cgroup(h_cg, idx, true);
}

static inline
struct hugetlb_cgroup *hugetlb_cgroup_from_css(struct cgroup_subsys_state *s)
{
	return s ? container_of(s, struct hugetlb_cgroup, css) : NULL;
}

static inline
struct hugetlb_cgroup *hugetlb_cgroup_from_task(struct task_struct *task)
{
	return hugetlb_cgroup_from_css(task_css(task, hugetlb_cgrp_id));
}

static inline bool hugetlb_cgroup_is_root(struct hugetlb_cgroup *h_cg)
{
	return (h_cg == root_h_cgroup);
}

static inline struct hugetlb_cgroup *
parent_hugetlb_cgroup(struct hugetlb_cgroup *h_cg)
{
	return hugetlb_cgroup_from_css(h_cg->css.parent);
}

static inline bool hugetlb_cgroup_have_usage(struct hugetlb_cgroup *h_cg)
{
	struct hstate *h;

	for_each_hstate(h) {
		if (page_counter_read(
		    hugetlb_cgroup_counter_from_cgroup(h_cg, hstate_index(h))))
			return true;
	}
	return false;
}

static void hugetlb_cgroup_init(struct hugetlb_cgroup *h_cgroup,
				struct hugetlb_cgroup *parent_h_cgroup)
{
	int idx;

	for (idx = 0; idx < HUGE_MAX_HSTATE; idx++) {
		struct page_counter *fault_parent = NULL;
		struct page_counter *rsvd_parent = NULL;
		unsigned long limit;
		int ret;

		if (parent_h_cgroup) {
			fault_parent = hugetlb_cgroup_counter_from_cgroup(
				parent_h_cgroup, idx);
			rsvd_parent = hugetlb_cgroup_counter_from_cgroup_rsvd(
				parent_h_cgroup, idx);
		}
		page_counter_init(hugetlb_cgroup_counter_from_cgroup(h_cgroup,
								     idx),
				  fault_parent);
		page_counter_init(
			hugetlb_cgroup_counter_from_cgroup_rsvd(h_cgroup, idx),
			rsvd_parent);

		limit = round_down(PAGE_COUNTER_MAX,
				   pages_per_huge_page(&hstates[idx]));

		ret = page_counter_set_max(
			hugetlb_cgroup_counter_from_cgroup(h_cgroup, idx),
			limit);
		VM_BUG_ON(ret);
		ret = page_counter_set_max(
			hugetlb_cgroup_counter_from_cgroup_rsvd(h_cgroup, idx),
			limit);
		VM_BUG_ON(ret);
	}
}

static void hugetlb_cgroup_free(struct hugetlb_cgroup *h_cgroup)
{
	int node;

	for_each_node(node)
		kfree(h_cgroup->nodeinfo[node]);
	kfree(h_cgroup);
}

static struct cgroup_subsys_state *
hugetlb_cgroup_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct hugetlb_cgroup *parent_h_cgroup = hugetlb_cgroup_from_css(parent_css);
	struct hugetlb_cgroup *h_cgroup;
	int node;

	h_cgroup = kzalloc(struct_size(h_cgroup, nodeinfo, nr_node_ids),
			   GFP_KERNEL);

	if (!h_cgroup)
		return ERR_PTR(-ENOMEM);

	if (!parent_h_cgroup)
		root_h_cgroup = h_cgroup;

	/*
	 * TODO: this routine can waste much memory for nodes which will
	 * never be onlined. It's better to use memory hotplug callback
	 * function.
	 */
	for_each_node(node) {
		/* Set node_to_alloc to NUMA_NO_NODE for offline nodes. */
		int node_to_alloc =
			node_state(node, N_NORMAL_MEMORY) ? node : NUMA_NO_NODE;
		h_cgroup->nodeinfo[node] =
			kzalloc_node(sizeof(struct hugetlb_cgroup_per_node),
				     GFP_KERNEL, node_to_alloc);
		if (!h_cgroup->nodeinfo[node])
			goto fail_alloc_nodeinfo;
	}

	hugetlb_cgroup_init(h_cgroup, parent_h_cgroup);
	return &h_cgroup->css;

fail_alloc_nodeinfo:
	hugetlb_cgroup_free(h_cgroup);
	return ERR_PTR(-ENOMEM);
}

static void hugetlb_cgroup_css_free(struct cgroup_subsys_state *css)
{
	hugetlb_cgroup_free(hugetlb_cgroup_from_css(css));
}

/*
 * Should be called with hugetlb_lock held.
 * Since we are holding hugetlb_lock, pages cannot get moved from
 * active list or uncharged from the cgroup, So no need to get
 * page reference and test for page active here. This function
 * cannot fail.
 */
static void hugetlb_cgroup_move_parent(int idx, struct hugetlb_cgroup *h_cg,
				       struct page *page)
{
	unsigned int nr_pages;
	struct page_counter *counter;
	struct hugetlb_cgroup *page_hcg;
	struct hugetlb_cgroup *parent = parent_hugetlb_cgroup(h_cg);

	page_hcg = hugetlb_cgroup_from_page(page);
	/*
	 * We can have pages in active list without any cgroup
	 * ie, hugepage with less than 3 pages. We can safely
	 * ignore those pages.
	 */
	if (!page_hcg || page_hcg != h_cg)
		goto out;

	nr_pages = compound_nr(page);
	if (!parent) {
		parent = root_h_cgroup;
		/* root has no limit */
		page_counter_charge(&parent->hugepage[idx], nr_pages);
	}
	counter = &h_cg->hugepage[idx];
	/* Take the pages off the local counter */
	page_counter_cancel(counter, nr_pages);

	set_hugetlb_cgroup(page, parent);
out:
	return;
}

/*
 * Force the hugetlb cgroup to empty the hugetlb resources by moving them to
 * the parent cgroup.
 */
static void hugetlb_cgroup_css_offline(struct cgroup_subsys_state *css)
{
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(css);
	struct hstate *h;
	struct page *page;

	do {
		for_each_hstate(h) {
			spin_lock_irq(&hugetlb_lock);
			list_for_each_entry(page, &h->hugepage_activelist, lru)
				hugetlb_cgroup_move_parent(hstate_index(h), h_cg, page);

			spin_unlock_irq(&hugetlb_lock);
		}
		cond_resched();
	} while (hugetlb_cgroup_have_usage(h_cg));
}

static inline void hugetlb_event(struct hugetlb_cgroup *hugetlb, int idx,
				 enum hugetlb_memory_event event)
{
	atomic_long_inc(&hugetlb->events_local[idx][event]);
	cgroup_file_notify(&hugetlb->events_local_file[idx]);

	do {
		atomic_long_inc(&hugetlb->events[idx][event]);
		cgroup_file_notify(&hugetlb->events_file[idx]);
	} while ((hugetlb = parent_hugetlb_cgroup(hugetlb)) &&
		 !hugetlb_cgroup_is_root(hugetlb));
}

static int __hugetlb_cgroup_charge_cgroup(int idx, unsigned long nr_pages,
					  struct hugetlb_cgroup **ptr,
					  bool rsvd)
{
	int ret = 0;
	struct page_counter *counter;
	struct hugetlb_cgroup *h_cg = NULL;

	if (hugetlb_cgroup_disabled())
		goto done;
	/*
	 * We don't charge any cgroup if the compound page have less
	 * than 3 pages.
	 */
	if (huge_page_order(&hstates[idx]) < HUGETLB_CGROUP_MIN_ORDER)
		goto done;
again:
	rcu_read_lock();
	h_cg = hugetlb_cgroup_from_task(current);
	if (!css_tryget(&h_cg->css)) {
		rcu_read_unlock();
		goto again;
	}
	rcu_read_unlock();

	if (!page_counter_try_charge(
		    __hugetlb_cgroup_counter_from_cgroup(h_cg, idx, rsvd),
		    nr_pages, &counter)) {
		ret = -ENOMEM;
		hugetlb_event(h_cg, idx, HUGETLB_MAX);
		css_put(&h_cg->css);
		goto done;
	}
	/* Reservations take a reference to the css because they do not get
	 * reparented.
	 */
	if (!rsvd)
		css_put(&h_cg->css);
done:
	*ptr = h_cg;
	return ret;
}

int hugetlb_cgroup_charge_cgroup(int idx, unsigned long nr_pages,
				 struct hugetlb_cgroup **ptr)
{
	return __hugetlb_cgroup_charge_cgroup(idx, nr_pages, ptr, false);
}

int hugetlb_cgroup_charge_cgroup_rsvd(int idx, unsigned long nr_pages,
				      struct hugetlb_cgroup **ptr)
{
	return __hugetlb_cgroup_charge_cgroup(idx, nr_pages, ptr, true);
}

/* Should be called with hugetlb_lock held */
static void __hugetlb_cgroup_commit_charge(int idx, unsigned long nr_pages,
					   struct hugetlb_cgroup *h_cg,
					   struct page *page, bool rsvd)
{
	if (hugetlb_cgroup_disabled() || !h_cg)
		return;

	__set_hugetlb_cgroup(page, h_cg, rsvd);
	if (!rsvd) {
		unsigned long usage =
			h_cg->nodeinfo[page_to_nid(page)]->usage[idx];
		/*
		 * This write is not atomic due to fetching usage and writing
		 * to it, but that's fine because we call this with
		 * hugetlb_lock held anyway.
		 */
		WRITE_ONCE(h_cg->nodeinfo[page_to_nid(page)]->usage[idx],
			   usage + nr_pages);
	}
}

void hugetlb_cgroup_commit_charge(int idx, unsigned long nr_pages,
				  struct hugetlb_cgroup *h_cg,
				  struct page *page)
{
	__hugetlb_cgroup_commit_charge(idx, nr_pages, h_cg, page, false);
}

void hugetlb_cgroup_commit_charge_rsvd(int idx, unsigned long nr_pages,
				       struct hugetlb_cgroup *h_cg,
				       struct page *page)
{
	__hugetlb_cgroup_commit_charge(idx, nr_pages, h_cg, page, true);
}

/*
 * Should be called with hugetlb_lock held
 */
static void __hugetlb_cgroup_uncharge_page(int idx, unsigned long nr_pages,
					   struct page *page, bool rsvd)
{
	struct hugetlb_cgroup *h_cg;

	if (hugetlb_cgroup_disabled())
		return;
	lockdep_assert_held(&hugetlb_lock);
	h_cg = __hugetlb_cgroup_from_page(page, rsvd);
	if (unlikely(!h_cg))
		return;
	__set_hugetlb_cgroup(page, NULL, rsvd);

	page_counter_uncharge(__hugetlb_cgroup_counter_from_cgroup(h_cg, idx,
								   rsvd),
			      nr_pages);

	if (rsvd)
		css_put(&h_cg->css);
	else {
		unsigned long usage =
			h_cg->nodeinfo[page_to_nid(page)]->usage[idx];
		/*
		 * This write is not atomic due to fetching usage and writing
		 * to it, but that's fine because we call this with
		 * hugetlb_lock held anyway.
		 */
		WRITE_ONCE(h_cg->nodeinfo[page_to_nid(page)]->usage[idx],
			   usage - nr_pages);
	}
}

void hugetlb_cgroup_uncharge_page(int idx, unsigned long nr_pages,
				  struct page *page)
{
	__hugetlb_cgroup_uncharge_page(idx, nr_pages, page, false);
}

void hugetlb_cgroup_uncharge_page_rsvd(int idx, unsigned long nr_pages,
				       struct page *page)
{
	__hugetlb_cgroup_uncharge_page(idx, nr_pages, page, true);
}

static void __hugetlb_cgroup_uncharge_cgroup(int idx, unsigned long nr_pages,
					     struct hugetlb_cgroup *h_cg,
					     bool rsvd)
{
	if (hugetlb_cgroup_disabled() || !h_cg)
		return;

	if (huge_page_order(&hstates[idx]) < HUGETLB_CGROUP_MIN_ORDER)
		return;

	page_counter_uncharge(__hugetlb_cgroup_counter_from_cgroup(h_cg, idx,
								   rsvd),
			      nr_pages);

	if (rsvd)
		css_put(&h_cg->css);
}

void hugetlb_cgroup_uncharge_cgroup(int idx, unsigned long nr_pages,
				    struct hugetlb_cgroup *h_cg)
{
	__hugetlb_cgroup_uncharge_cgroup(idx, nr_pages, h_cg, false);
}

void hugetlb_cgroup_uncharge_cgroup_rsvd(int idx, unsigned long nr_pages,
					 struct hugetlb_cgroup *h_cg)
{
	__hugetlb_cgroup_uncharge_cgroup(idx, nr_pages, h_cg, true);
}

void hugetlb_cgroup_uncharge_counter(struct resv_map *resv, unsigned long start,
				     unsigned long end)
{
	if (hugetlb_cgroup_disabled() || !resv || !resv->reservation_counter ||
	    !resv->css)
		return;

	page_counter_uncharge(resv->reservation_counter,
			      (end - start) * resv->pages_per_hpage);
	css_put(resv->css);
}

void hugetlb_cgroup_uncharge_file_region(struct resv_map *resv,
					 struct file_region *rg,
					 unsigned long nr_pages,
					 bool region_del)
{
	if (hugetlb_cgroup_disabled() || !resv || !rg || !nr_pages)
		return;

	if (rg->reservation_counter && resv->pages_per_hpage &&
	    !resv->reservation_counter) {
		page_counter_uncharge(rg->reservation_counter,
				      nr_pages * resv->pages_per_hpage);
		/*
		 * Only do css_put(rg->css) when we delete the entire region
		 * because one file_region must hold exactly one css reference.
		 */
		if (region_del)
			css_put(rg->css);
	}
}

enum {
	RES_USAGE,
	RES_RSVD_USAGE,
	RES_LIMIT,
	RES_RSVD_LIMIT,
	RES_MAX_USAGE,
	RES_RSVD_MAX_USAGE,
	RES_FAILCNT,
	RES_RSVD_FAILCNT,
};

static int hugetlb_cgroup_read_numa_stat(struct seq_file *seq, void *dummy)
{
	int nid;
	struct cftype *cft = seq_cft(seq);
	int idx = MEMFILE_IDX(cft->private);
	bool legacy = MEMFILE_ATTR(cft->private);
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(seq_css(seq));
	struct cgroup_subsys_state *css;
	unsigned long usage;

	if (legacy) {
		/* Add up usage across all nodes for the non-hierarchical total. */
		usage = 0;
		for_each_node_state(nid, N_MEMORY)
			usage += READ_ONCE(h_cg->nodeinfo[nid]->usage[idx]);
		seq_printf(seq, "total=%lu", usage * PAGE_SIZE);

		/* Simply print the per-node usage for the non-hierarchical total. */
		for_each_node_state(nid, N_MEMORY)
			seq_printf(seq, " N%d=%lu", nid,
				   READ_ONCE(h_cg->nodeinfo[nid]->usage[idx]) *
					   PAGE_SIZE);
		seq_putc(seq, '\n');
	}

	/*
	 * The hierarchical total is pretty much the value recorded by the
	 * counter, so use that.
	 */
	seq_printf(seq, "%stotal=%lu", legacy ? "hierarchical_" : "",
		   page_counter_read(&h_cg->hugepage[idx]) * PAGE_SIZE);

	/*
	 * For each node, transverse the css tree to obtain the hierarchical
	 * node usage.
	 */
	for_each_node_state(nid, N_MEMORY) {
		usage = 0;
		rcu_read_lock();
		css_for_each_descendant_pre(css, &h_cg->css) {
			usage += READ_ONCE(hugetlb_cgroup_from_css(css)
						   ->nodeinfo[nid]
						   ->usage[idx]);
		}
		rcu_read_unlock();
		seq_printf(seq, " N%d=%lu", nid, usage * PAGE_SIZE);
	}

	seq_putc(seq, '\n');

	return 0;
}

static u64 hugetlb_cgroup_read_u64(struct cgroup_subsys_state *css,
				   struct cftype *cft)
{
	struct page_counter *counter;
	struct page_counter *rsvd_counter;
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(css);

	counter = &h_cg->hugepage[MEMFILE_IDX(cft->private)];
	rsvd_counter = &h_cg->rsvd_hugepage[MEMFILE_IDX(cft->private)];

	switch (MEMFILE_ATTR(cft->private)) {
	case RES_USAGE:
		return (u64)page_counter_read(counter) * PAGE_SIZE;
	case RES_RSVD_USAGE:
		return (u64)page_counter_read(rsvd_counter) * PAGE_SIZE;
	case RES_LIMIT:
		return (u64)counter->max * PAGE_SIZE;
	case RES_RSVD_LIMIT:
		return (u64)rsvd_counter->max * PAGE_SIZE;
	case RES_MAX_USAGE:
		return (u64)counter->watermark * PAGE_SIZE;
	case RES_RSVD_MAX_USAGE:
		return (u64)rsvd_counter->watermark * PAGE_SIZE;
	case RES_FAILCNT:
		return counter->failcnt;
	case RES_RSVD_FAILCNT:
		return rsvd_counter->failcnt;
	default:
		BUG();
	}
}

static int hugetlb_cgroup_read_u64_max(struct seq_file *seq, void *v)
{
	int idx;
	u64 val;
	struct cftype *cft = seq_cft(seq);
	unsigned long limit;
	struct page_counter *counter;
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(seq_css(seq));

	idx = MEMFILE_IDX(cft->private);
	counter = &h_cg->hugepage[idx];

	limit = round_down(PAGE_COUNTER_MAX,
			   pages_per_huge_page(&hstates[idx]));

	switch (MEMFILE_ATTR(cft->private)) {
	case RES_RSVD_USAGE:
		counter = &h_cg->rsvd_hugepage[idx];
		fallthrough;
	case RES_USAGE:
		val = (u64)page_counter_read(counter);
		seq_printf(seq, "%llu\n", val * PAGE_SIZE);
		break;
	case RES_RSVD_LIMIT:
		counter = &h_cg->rsvd_hugepage[idx];
		fallthrough;
	case RES_LIMIT:
		val = (u64)counter->max;
		if (val == limit)
			seq_puts(seq, "max\n");
		else
			seq_printf(seq, "%llu\n", val * PAGE_SIZE);
		break;
	default:
		BUG();
	}

	return 0;
}

static DEFINE_MUTEX(hugetlb_limit_mutex);

static ssize_t hugetlb_cgroup_write(struct kernfs_open_file *of,
				    char *buf, size_t nbytes, loff_t off,
				    const char *max)
{
	int ret, idx;
	unsigned long nr_pages;
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(of_css(of));
	bool rsvd = false;

	if (hugetlb_cgroup_is_root(h_cg)) /* Can't set limit on root */
		return -EINVAL;

	buf = strstrip(buf);
	ret = page_counter_memparse(buf, max, &nr_pages);
	if (ret)
		return ret;

	idx = MEMFILE_IDX(of_cft(of)->private);
	nr_pages = round_down(nr_pages, pages_per_huge_page(&hstates[idx]));

	switch (MEMFILE_ATTR(of_cft(of)->private)) {
	case RES_RSVD_LIMIT:
		rsvd = true;
		fallthrough;
	case RES_LIMIT:
		mutex_lock(&hugetlb_limit_mutex);
		ret = page_counter_set_max(
			__hugetlb_cgroup_counter_from_cgroup(h_cg, idx, rsvd),
			nr_pages);
		mutex_unlock(&hugetlb_limit_mutex);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret ?: nbytes;
}

static ssize_t hugetlb_cgroup_write_legacy(struct kernfs_open_file *of,
					   char *buf, size_t nbytes, loff_t off)
{
	return hugetlb_cgroup_write(of, buf, nbytes, off, "-1");
}

static ssize_t hugetlb_cgroup_write_dfl(struct kernfs_open_file *of,
					char *buf, size_t nbytes, loff_t off)
{
	return hugetlb_cgroup_write(of, buf, nbytes, off, "max");
}

static ssize_t hugetlb_cgroup_reset(struct kernfs_open_file *of,
				    char *buf, size_t nbytes, loff_t off)
{
	int ret = 0;
	struct page_counter *counter, *rsvd_counter;
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(of_css(of));

	counter = &h_cg->hugepage[MEMFILE_IDX(of_cft(of)->private)];
	rsvd_counter = &h_cg->rsvd_hugepage[MEMFILE_IDX(of_cft(of)->private)];

	switch (MEMFILE_ATTR(of_cft(of)->private)) {
	case RES_MAX_USAGE:
		page_counter_reset_watermark(counter);
		break;
	case RES_RSVD_MAX_USAGE:
		page_counter_reset_watermark(rsvd_counter);
		break;
	case RES_FAILCNT:
		counter->failcnt = 0;
		break;
	case RES_RSVD_FAILCNT:
		rsvd_counter->failcnt = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret ?: nbytes;
}

static char *mem_fmt(char *buf, int size, unsigned long hsize)
{
	if (hsize >= SZ_1G)
		snprintf(buf, size, "%luGB", hsize / SZ_1G);
	else if (hsize >= SZ_1M)
		snprintf(buf, size, "%luMB", hsize / SZ_1M);
	else
		snprintf(buf, size, "%luKB", hsize / SZ_1K);
	return buf;
}

static int __hugetlb_events_show(struct seq_file *seq, bool local)
{
	int idx;
	long max;
	struct cftype *cft = seq_cft(seq);
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(seq_css(seq));

	idx = MEMFILE_IDX(cft->private);

	if (local)
		max = atomic_long_read(&h_cg->events_local[idx][HUGETLB_MAX]);
	else
		max = atomic_long_read(&h_cg->events[idx][HUGETLB_MAX]);

	seq_printf(seq, "max %lu\n", max);

	return 0;
}

static int hugetlb_events_show(struct seq_file *seq, void *v)
{
	return __hugetlb_events_show(seq, false);
}

static int hugetlb_events_local_show(struct seq_file *seq, void *v)
{
	return __hugetlb_events_show(seq, true);
}

static void __init __hugetlb_cgroup_file_dfl_init(int idx)
{
	char buf[32];
	struct cftype *cft;
	struct hstate *h = &hstates[idx];

	/* format the size */
	mem_fmt(buf, sizeof(buf), huge_page_size(h));

	/* Add the limit file */
	cft = &h->cgroup_files_dfl[0];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.max", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_LIMIT);
	cft->seq_show = hugetlb_cgroup_read_u64_max;
	cft->write = hugetlb_cgroup_write_dfl;
	cft->flags = CFTYPE_NOT_ON_ROOT;

	/* Add the reservation limit file */
	cft = &h->cgroup_files_dfl[1];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.rsvd.max", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_RSVD_LIMIT);
	cft->seq_show = hugetlb_cgroup_read_u64_max;
	cft->write = hugetlb_cgroup_write_dfl;
	cft->flags = CFTYPE_NOT_ON_ROOT;

	/* Add the current usage file */
	cft = &h->cgroup_files_dfl[2];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.current", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_USAGE);
	cft->seq_show = hugetlb_cgroup_read_u64_max;
	cft->flags = CFTYPE_NOT_ON_ROOT;

	/* Add the current reservation usage file */
	cft = &h->cgroup_files_dfl[3];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.rsvd.current", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_RSVD_USAGE);
	cft->seq_show = hugetlb_cgroup_read_u64_max;
	cft->flags = CFTYPE_NOT_ON_ROOT;

	/* Add the events file */
	cft = &h->cgroup_files_dfl[4];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.events", buf);
	cft->private = MEMFILE_PRIVATE(idx, 0);
	cft->seq_show = hugetlb_events_show;
	cft->file_offset = offsetof(struct hugetlb_cgroup, events_file[idx]);
	cft->flags = CFTYPE_NOT_ON_ROOT;

	/* Add the events.local file */
	cft = &h->cgroup_files_dfl[5];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.events.local", buf);
	cft->private = MEMFILE_PRIVATE(idx, 0);
	cft->seq_show = hugetlb_events_local_show;
	cft->file_offset = offsetof(struct hugetlb_cgroup,
				    events_local_file[idx]);
	cft->flags = CFTYPE_NOT_ON_ROOT;

	/* Add the numa stat file */
	cft = &h->cgroup_files_dfl[6];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.numa_stat", buf);
	cft->private = MEMFILE_PRIVATE(idx, 0);
	cft->seq_show = hugetlb_cgroup_read_numa_stat;
	cft->flags = CFTYPE_NOT_ON_ROOT;

	/* NULL terminate the last cft */
	cft = &h->cgroup_files_dfl[7];
	memset(cft, 0, sizeof(*cft));

	WARN_ON(cgroup_add_dfl_cftypes(&hugetlb_cgrp_subsys,
				       h->cgroup_files_dfl));
}

static void __init __hugetlb_cgroup_file_legacy_init(int idx)
{
	char buf[32];
	struct cftype *cft;
	struct hstate *h = &hstates[idx];

	/* format the size */
	mem_fmt(buf, sizeof(buf), huge_page_size(h));

	/* Add the limit file */
	cft = &h->cgroup_files_legacy[0];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.limit_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_LIMIT);
	cft->read_u64 = hugetlb_cgroup_read_u64;
	cft->write = hugetlb_cgroup_write_legacy;

	/* Add the reservation limit file */
	cft = &h->cgroup_files_legacy[1];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.rsvd.limit_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_RSVD_LIMIT);
	cft->read_u64 = hugetlb_cgroup_read_u64;
	cft->write = hugetlb_cgroup_write_legacy;

	/* Add the usage file */
	cft = &h->cgroup_files_legacy[2];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.usage_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_USAGE);
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* Add the reservation usage file */
	cft = &h->cgroup_files_legacy[3];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.rsvd.usage_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_RSVD_USAGE);
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* Add the MAX usage file */
	cft = &h->cgroup_files_legacy[4];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.max_usage_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_MAX_USAGE);
	cft->write = hugetlb_cgroup_reset;
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* Add the MAX reservation usage file */
	cft = &h->cgroup_files_legacy[5];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.rsvd.max_usage_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_RSVD_MAX_USAGE);
	cft->write = hugetlb_cgroup_reset;
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* Add the failcntfile */
	cft = &h->cgroup_files_legacy[6];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.failcnt", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_FAILCNT);
	cft->write = hugetlb_cgroup_reset;
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* Add the reservation failcntfile */
	cft = &h->cgroup_files_legacy[7];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.rsvd.failcnt", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_RSVD_FAILCNT);
	cft->write = hugetlb_cgroup_reset;
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* Add the numa stat file */
	cft = &h->cgroup_files_legacy[8];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.numa_stat", buf);
	cft->private = MEMFILE_PRIVATE(idx, 1);
	cft->seq_show = hugetlb_cgroup_read_numa_stat;

	/* NULL terminate the last cft */
	cft = &h->cgroup_files_legacy[9];
	memset(cft, 0, sizeof(*cft));

	WARN_ON(cgroup_add_legacy_cftypes(&hugetlb_cgrp_subsys,
					  h->cgroup_files_legacy));
}

static void __init __hugetlb_cgroup_file_init(int idx)
{
	__hugetlb_cgroup_file_dfl_init(idx);
	__hugetlb_cgroup_file_legacy_init(idx);
}

void __init hugetlb_cgroup_file_init(void)
{
	struct hstate *h;

	for_each_hstate(h) {
		/*
		 * Add cgroup control files only if the huge page consists
		 * of more than two normal pages. This is because we use
		 * page[2].private for storing cgroup details.
		 */
		if (huge_page_order(h) >= HUGETLB_CGROUP_MIN_ORDER)
			__hugetlb_cgroup_file_init(hstate_index(h));
	}
}

/*
 * hugetlb_lock will make sure a parallel cgroup rmdir won't happen
 * when we migrate hugepages
 */
void hugetlb_cgroup_migrate(struct page *oldhpage, struct page *newhpage)
{
	struct hugetlb_cgroup *h_cg;
	struct hugetlb_cgroup *h_cg_rsvd;
	struct hstate *h = page_hstate(oldhpage);

	if (hugetlb_cgroup_disabled())
		return;

	spin_lock_irq(&hugetlb_lock);
	h_cg = hugetlb_cgroup_from_page(oldhpage);
	h_cg_rsvd = hugetlb_cgroup_from_page_rsvd(oldhpage);
	set_hugetlb_cgroup(oldhpage, NULL);
	set_hugetlb_cgroup_rsvd(oldhpage, NULL);

	/* move the h_cg details to new cgroup */
	set_hugetlb_cgroup(newhpage, h_cg);
	set_hugetlb_cgroup_rsvd(newhpage, h_cg_rsvd);
	list_move(&newhpage->lru, &h->hugepage_activelist);
	spin_unlock_irq(&hugetlb_lock);
	return;
}

static struct cftype hugetlb_files[] = {
	{} /* terminate */
};

struct cgroup_subsys hugetlb_cgrp_subsys = {
	.css_alloc	= hugetlb_cgroup_css_alloc,
	.css_offline	= hugetlb_cgroup_css_offline,
	.css_free	= hugetlb_cgroup_css_free,
	.dfl_cftypes	= hugetlb_files,
	.legacy_cftypes	= hugetlb_files,
};
