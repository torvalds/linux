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
#include <linux/page_counter.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/hugetlb_cgroup.h>

struct hugetlb_cgroup {
	struct cgroup_subsys_state css;
	/*
	 * the counter to account for hugepages from hugetlb.
	 */
	struct page_counter hugepage[HUGE_MAX_HSTATE];
};

#define MEMFILE_PRIVATE(x, val)	(((x) << 16) | (val))
#define MEMFILE_IDX(val)	(((val) >> 16) & 0xffff)
#define MEMFILE_ATTR(val)	((val) & 0xffff)

static struct hugetlb_cgroup *root_h_cgroup __read_mostly;

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
	int idx;

	for (idx = 0; idx < hugetlb_max_hstate; idx++) {
		if (page_counter_read(&h_cg->hugepage[idx]))
			return true;
	}
	return false;
}

static struct cgroup_subsys_state *
hugetlb_cgroup_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct hugetlb_cgroup *parent_h_cgroup = hugetlb_cgroup_from_css(parent_css);
	struct hugetlb_cgroup *h_cgroup;
	int idx;

	h_cgroup = kzalloc(sizeof(*h_cgroup), GFP_KERNEL);
	if (!h_cgroup)
		return ERR_PTR(-ENOMEM);

	if (parent_h_cgroup) {
		for (idx = 0; idx < HUGE_MAX_HSTATE; idx++)
			page_counter_init(&h_cgroup->hugepage[idx],
					  &parent_h_cgroup->hugepage[idx]);
	} else {
		root_h_cgroup = h_cgroup;
		for (idx = 0; idx < HUGE_MAX_HSTATE; idx++)
			page_counter_init(&h_cgroup->hugepage[idx], NULL);
	}
	return &h_cgroup->css;
}

static void hugetlb_cgroup_css_free(struct cgroup_subsys_state *css)
{
	struct hugetlb_cgroup *h_cgroup;

	h_cgroup = hugetlb_cgroup_from_css(css);
	kfree(h_cgroup);
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

	nr_pages = 1 << compound_order(page);
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
	int idx = 0;

	do {
		for_each_hstate(h) {
			spin_lock(&hugetlb_lock);
			list_for_each_entry(page, &h->hugepage_activelist, lru)
				hugetlb_cgroup_move_parent(idx, h_cg, page);

			spin_unlock(&hugetlb_lock);
			idx++;
		}
		cond_resched();
	} while (hugetlb_cgroup_have_usage(h_cg));
}

int hugetlb_cgroup_charge_cgroup(int idx, unsigned long nr_pages,
				 struct hugetlb_cgroup **ptr)
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
	if (!css_tryget_online(&h_cg->css)) {
		rcu_read_unlock();
		goto again;
	}
	rcu_read_unlock();

	ret = page_counter_try_charge(&h_cg->hugepage[idx], nr_pages, &counter);
	css_put(&h_cg->css);
done:
	*ptr = h_cg;
	return ret;
}

/* Should be called with hugetlb_lock held */
void hugetlb_cgroup_commit_charge(int idx, unsigned long nr_pages,
				  struct hugetlb_cgroup *h_cg,
				  struct page *page)
{
	if (hugetlb_cgroup_disabled() || !h_cg)
		return;

	set_hugetlb_cgroup(page, h_cg);
	return;
}

/*
 * Should be called with hugetlb_lock held
 */
void hugetlb_cgroup_uncharge_page(int idx, unsigned long nr_pages,
				  struct page *page)
{
	struct hugetlb_cgroup *h_cg;

	if (hugetlb_cgroup_disabled())
		return;
	lockdep_assert_held(&hugetlb_lock);
	h_cg = hugetlb_cgroup_from_page(page);
	if (unlikely(!h_cg))
		return;
	set_hugetlb_cgroup(page, NULL);
	page_counter_uncharge(&h_cg->hugepage[idx], nr_pages);
	return;
}

void hugetlb_cgroup_uncharge_cgroup(int idx, unsigned long nr_pages,
				    struct hugetlb_cgroup *h_cg)
{
	if (hugetlb_cgroup_disabled() || !h_cg)
		return;

	if (huge_page_order(&hstates[idx]) < HUGETLB_CGROUP_MIN_ORDER)
		return;

	page_counter_uncharge(&h_cg->hugepage[idx], nr_pages);
	return;
}

enum {
	RES_USAGE,
	RES_LIMIT,
	RES_MAX_USAGE,
	RES_FAILCNT,
};

static u64 hugetlb_cgroup_read_u64(struct cgroup_subsys_state *css,
				   struct cftype *cft)
{
	struct page_counter *counter;
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(css);

	counter = &h_cg->hugepage[MEMFILE_IDX(cft->private)];

	switch (MEMFILE_ATTR(cft->private)) {
	case RES_USAGE:
		return (u64)page_counter_read(counter) * PAGE_SIZE;
	case RES_LIMIT:
		return (u64)counter->limit * PAGE_SIZE;
	case RES_MAX_USAGE:
		return (u64)counter->watermark * PAGE_SIZE;
	case RES_FAILCNT:
		return counter->failcnt;
	default:
		BUG();
	}
}

static DEFINE_MUTEX(hugetlb_limit_mutex);

static ssize_t hugetlb_cgroup_write(struct kernfs_open_file *of,
				    char *buf, size_t nbytes, loff_t off)
{
	int ret, idx;
	unsigned long nr_pages;
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(of_css(of));

	if (hugetlb_cgroup_is_root(h_cg)) /* Can't set limit on root */
		return -EINVAL;

	buf = strstrip(buf);
	ret = page_counter_memparse(buf, "-1", &nr_pages);
	if (ret)
		return ret;

	idx = MEMFILE_IDX(of_cft(of)->private);

	switch (MEMFILE_ATTR(of_cft(of)->private)) {
	case RES_LIMIT:
		mutex_lock(&hugetlb_limit_mutex);
		ret = page_counter_limit(&h_cg->hugepage[idx], nr_pages);
		mutex_unlock(&hugetlb_limit_mutex);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret ?: nbytes;
}

static ssize_t hugetlb_cgroup_reset(struct kernfs_open_file *of,
				    char *buf, size_t nbytes, loff_t off)
{
	int ret = 0;
	struct page_counter *counter;
	struct hugetlb_cgroup *h_cg = hugetlb_cgroup_from_css(of_css(of));

	counter = &h_cg->hugepage[MEMFILE_IDX(of_cft(of)->private)];

	switch (MEMFILE_ATTR(of_cft(of)->private)) {
	case RES_MAX_USAGE:
		page_counter_reset_watermark(counter);
		break;
	case RES_FAILCNT:
		counter->failcnt = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret ?: nbytes;
}

static char *mem_fmt(char *buf, int size, unsigned long hsize)
{
	if (hsize >= (1UL << 30))
		snprintf(buf, size, "%luGB", hsize >> 30);
	else if (hsize >= (1UL << 20))
		snprintf(buf, size, "%luMB", hsize >> 20);
	else
		snprintf(buf, size, "%luKB", hsize >> 10);
	return buf;
}

static void __init __hugetlb_cgroup_file_init(int idx)
{
	char buf[32];
	struct cftype *cft;
	struct hstate *h = &hstates[idx];

	/* format the size */
	mem_fmt(buf, 32, huge_page_size(h));

	/* Add the limit file */
	cft = &h->cgroup_files[0];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.limit_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_LIMIT);
	cft->read_u64 = hugetlb_cgroup_read_u64;
	cft->write = hugetlb_cgroup_write;

	/* Add the usage file */
	cft = &h->cgroup_files[1];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.usage_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_USAGE);
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* Add the MAX usage file */
	cft = &h->cgroup_files[2];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.max_usage_in_bytes", buf);
	cft->private = MEMFILE_PRIVATE(idx, RES_MAX_USAGE);
	cft->write = hugetlb_cgroup_reset;
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* Add the failcntfile */
	cft = &h->cgroup_files[3];
	snprintf(cft->name, MAX_CFTYPE_NAME, "%s.failcnt", buf);
	cft->private  = MEMFILE_PRIVATE(idx, RES_FAILCNT);
	cft->write = hugetlb_cgroup_reset;
	cft->read_u64 = hugetlb_cgroup_read_u64;

	/* NULL terminate the last cft */
	cft = &h->cgroup_files[4];
	memset(cft, 0, sizeof(*cft));

	WARN_ON(cgroup_add_legacy_cftypes(&hugetlb_cgrp_subsys,
					  h->cgroup_files));
}

void __init hugetlb_cgroup_file_init(void)
{
	struct hstate *h;

	for_each_hstate(h) {
		/*
		 * Add cgroup control files only if the huge page consists
		 * of more than two normal pages. This is because we use
		 * page[2].lru.next for storing cgroup details.
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
	struct hstate *h = page_hstate(oldhpage);

	if (hugetlb_cgroup_disabled())
		return;

	VM_BUG_ON_PAGE(!PageHuge(oldhpage), oldhpage);
	spin_lock(&hugetlb_lock);
	h_cg = hugetlb_cgroup_from_page(oldhpage);
	set_hugetlb_cgroup(oldhpage, NULL);

	/* move the h_cg details to new cgroup */
	set_hugetlb_cgroup(newhpage, h_cg);
	list_move(&newhpage->lru, &h->hugepage_activelist);
	spin_unlock(&hugetlb_lock);
	return;
}

struct cgroup_subsys hugetlb_cgrp_subsys = {
	.css_alloc	= hugetlb_cgroup_css_alloc,
	.css_offline	= hugetlb_cgroup_css_offline,
	.css_free	= hugetlb_cgroup_css_free,
};
