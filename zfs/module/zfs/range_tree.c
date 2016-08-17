/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, 2014 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dnode.h>
#include <sys/zio.h>
#include <sys/range_tree.h>

kmem_cache_t *range_seg_cache;

void
range_tree_init(void)
{
	ASSERT(range_seg_cache == NULL);
	range_seg_cache = kmem_cache_create("range_seg_cache",
	    sizeof (range_seg_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
range_tree_fini(void)
{
	kmem_cache_destroy(range_seg_cache);
	range_seg_cache = NULL;
}

void
range_tree_stat_verify(range_tree_t *rt)
{
	range_seg_t *rs;
	uint64_t hist[RANGE_TREE_HISTOGRAM_SIZE] = { 0 };
	int i;

	for (rs = avl_first(&rt->rt_root); rs != NULL;
	    rs = AVL_NEXT(&rt->rt_root, rs)) {
		uint64_t size = rs->rs_end - rs->rs_start;
		int idx	= highbit64(size) - 1;

		hist[idx]++;
		ASSERT3U(hist[idx], !=, 0);
	}

	for (i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i++) {
		if (hist[i] != rt->rt_histogram[i]) {
			zfs_dbgmsg("i=%d, hist=%p, hist=%llu, rt_hist=%llu",
			    i, hist, hist[i], rt->rt_histogram[i]);
		}
		VERIFY3U(hist[i], ==, rt->rt_histogram[i]);
	}
}

static void
range_tree_stat_incr(range_tree_t *rt, range_seg_t *rs)
{
	uint64_t size = rs->rs_end - rs->rs_start;
	int idx = highbit64(size) - 1;

	ASSERT(size != 0);
	ASSERT3U(idx, <,
	    sizeof (rt->rt_histogram) / sizeof (*rt->rt_histogram));

	ASSERT(MUTEX_HELD(rt->rt_lock));
	rt->rt_histogram[idx]++;
	ASSERT3U(rt->rt_histogram[idx], !=, 0);
}

static void
range_tree_stat_decr(range_tree_t *rt, range_seg_t *rs)
{
	uint64_t size = rs->rs_end - rs->rs_start;
	int idx = highbit64(size) - 1;

	ASSERT(size != 0);
	ASSERT3U(idx, <,
	    sizeof (rt->rt_histogram) / sizeof (*rt->rt_histogram));

	ASSERT(MUTEX_HELD(rt->rt_lock));
	ASSERT3U(rt->rt_histogram[idx], !=, 0);
	rt->rt_histogram[idx]--;
}

/*
 * NOTE: caller is responsible for all locking.
 */
static int
range_tree_seg_compare(const void *x1, const void *x2)
{
	const range_seg_t *r1 = (const range_seg_t *)x1;
	const range_seg_t *r2 = (const range_seg_t *)x2;

	ASSERT3U(r1->rs_start, <=, r1->rs_end);
	ASSERT3U(r2->rs_start, <=, r2->rs_end);

	return ((r1->rs_start >= r2->rs_end) - (r1->rs_end <= r2->rs_start));
}

range_tree_t *
range_tree_create(range_tree_ops_t *ops, void *arg, kmutex_t *lp)
{
	range_tree_t *rt;

	rt = kmem_zalloc(sizeof (range_tree_t), KM_SLEEP);

	avl_create(&rt->rt_root, range_tree_seg_compare,
	    sizeof (range_seg_t), offsetof(range_seg_t, rs_node));

	rt->rt_lock = lp;
	rt->rt_ops = ops;
	rt->rt_arg = arg;

	if (rt->rt_ops != NULL)
		rt->rt_ops->rtop_create(rt, rt->rt_arg);

	return (rt);
}

void
range_tree_destroy(range_tree_t *rt)
{
	VERIFY0(rt->rt_space);

	if (rt->rt_ops != NULL)
		rt->rt_ops->rtop_destroy(rt, rt->rt_arg);

	avl_destroy(&rt->rt_root);
	kmem_free(rt, sizeof (*rt));
}

void
range_tree_add(void *arg, uint64_t start, uint64_t size)
{
	range_tree_t *rt = arg;
	avl_index_t where;
	range_seg_t rsearch, *rs_before, *rs_after, *rs;
	uint64_t end = start + size;
	boolean_t merge_before, merge_after;

	ASSERT(MUTEX_HELD(rt->rt_lock));
	VERIFY(size != 0);

	rsearch.rs_start = start;
	rsearch.rs_end = end;
	rs = avl_find(&rt->rt_root, &rsearch, &where);

	if (rs != NULL && rs->rs_start <= start && rs->rs_end >= end) {
		zfs_panic_recover("zfs: allocating allocated segment"
		    "(offset=%llu size=%llu)\n",
		    (longlong_t)start, (longlong_t)size);
		return;
	}

	/* Make sure we don't overlap with either of our neighbors */
	VERIFY(rs == NULL);

	rs_before = avl_nearest(&rt->rt_root, where, AVL_BEFORE);
	rs_after = avl_nearest(&rt->rt_root, where, AVL_AFTER);

	merge_before = (rs_before != NULL && rs_before->rs_end == start);
	merge_after = (rs_after != NULL && rs_after->rs_start == end);

	if (merge_before && merge_after) {
		avl_remove(&rt->rt_root, rs_before);
		if (rt->rt_ops != NULL) {
			rt->rt_ops->rtop_remove(rt, rs_before, rt->rt_arg);
			rt->rt_ops->rtop_remove(rt, rs_after, rt->rt_arg);
		}

		range_tree_stat_decr(rt, rs_before);
		range_tree_stat_decr(rt, rs_after);

		rs_after->rs_start = rs_before->rs_start;
		kmem_cache_free(range_seg_cache, rs_before);
		rs = rs_after;
	} else if (merge_before) {
		if (rt->rt_ops != NULL)
			rt->rt_ops->rtop_remove(rt, rs_before, rt->rt_arg);

		range_tree_stat_decr(rt, rs_before);

		rs_before->rs_end = end;
		rs = rs_before;
	} else if (merge_after) {
		if (rt->rt_ops != NULL)
			rt->rt_ops->rtop_remove(rt, rs_after, rt->rt_arg);

		range_tree_stat_decr(rt, rs_after);

		rs_after->rs_start = start;
		rs = rs_after;
	} else {
		rs = kmem_cache_alloc(range_seg_cache, KM_SLEEP);
		rs->rs_start = start;
		rs->rs_end = end;
		avl_insert(&rt->rt_root, rs, where);
	}

	if (rt->rt_ops != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);

	range_tree_stat_incr(rt, rs);
	rt->rt_space += size;
}

void
range_tree_remove(void *arg, uint64_t start, uint64_t size)
{
	range_tree_t *rt = arg;
	avl_index_t where;
	range_seg_t rsearch, *rs, *newseg;
	uint64_t end = start + size;
	boolean_t left_over, right_over;

	ASSERT(MUTEX_HELD(rt->rt_lock));
	VERIFY3U(size, !=, 0);
	VERIFY3U(size, <=, rt->rt_space);

	rsearch.rs_start = start;
	rsearch.rs_end = end;
	rs = avl_find(&rt->rt_root, &rsearch, &where);

	/* Make sure we completely overlap with someone */
	if (rs == NULL) {
		zfs_panic_recover("zfs: freeing free segment "
		    "(offset=%llu size=%llu)",
		    (longlong_t)start, (longlong_t)size);
		return;
	}
	VERIFY3U(rs->rs_start, <=, start);
	VERIFY3U(rs->rs_end, >=, end);

	left_over = (rs->rs_start != start);
	right_over = (rs->rs_end != end);

	range_tree_stat_decr(rt, rs);

	if (rt->rt_ops != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

	if (left_over && right_over) {
		newseg = kmem_cache_alloc(range_seg_cache, KM_SLEEP);
		newseg->rs_start = end;
		newseg->rs_end = rs->rs_end;
		range_tree_stat_incr(rt, newseg);

		rs->rs_end = start;

		avl_insert_here(&rt->rt_root, newseg, rs, AVL_AFTER);
		if (rt->rt_ops != NULL)
			rt->rt_ops->rtop_add(rt, newseg, rt->rt_arg);
	} else if (left_over) {
		rs->rs_end = start;
	} else if (right_over) {
		rs->rs_start = end;
	} else {
		avl_remove(&rt->rt_root, rs);
		kmem_cache_free(range_seg_cache, rs);
		rs = NULL;
	}

	if (rs != NULL) {
		range_tree_stat_incr(rt, rs);

		if (rt->rt_ops != NULL)
			rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);
	}

	rt->rt_space -= size;
}

static range_seg_t *
range_tree_find_impl(range_tree_t *rt, uint64_t start, uint64_t size)
{
	avl_index_t where;
	range_seg_t rsearch;
	uint64_t end = start + size;

	ASSERT(MUTEX_HELD(rt->rt_lock));
	VERIFY(size != 0);

	rsearch.rs_start = start;
	rsearch.rs_end = end;
	return (avl_find(&rt->rt_root, &rsearch, &where));
}

static range_seg_t *
range_tree_find(range_tree_t *rt, uint64_t start, uint64_t size)
{
	range_seg_t *rs = range_tree_find_impl(rt, start, size);
	if (rs != NULL && rs->rs_start <= start && rs->rs_end >= start + size)
		return (rs);
	return (NULL);
}

void
range_tree_verify(range_tree_t *rt, uint64_t off, uint64_t size)
{
	range_seg_t *rs;

	mutex_enter(rt->rt_lock);
	rs = range_tree_find(rt, off, size);
	if (rs != NULL)
		panic("freeing free block; rs=%p", (void *)rs);
	mutex_exit(rt->rt_lock);
}

boolean_t
range_tree_contains(range_tree_t *rt, uint64_t start, uint64_t size)
{
	return (range_tree_find(rt, start, size) != NULL);
}

/*
 * Ensure that this range is not in the tree, regardless of whether
 * it is currently in the tree.
 */
void
range_tree_clear(range_tree_t *rt, uint64_t start, uint64_t size)
{
	range_seg_t *rs;

	while ((rs = range_tree_find_impl(rt, start, size)) != NULL) {
		uint64_t free_start = MAX(rs->rs_start, start);
		uint64_t free_end = MIN(rs->rs_end, start + size);
		range_tree_remove(rt, free_start, free_end - free_start);
	}
}

void
range_tree_swap(range_tree_t **rtsrc, range_tree_t **rtdst)
{
	range_tree_t *rt;

	ASSERT(MUTEX_HELD((*rtsrc)->rt_lock));
	ASSERT0(range_tree_space(*rtdst));
	ASSERT0(avl_numnodes(&(*rtdst)->rt_root));

	rt = *rtsrc;
	*rtsrc = *rtdst;
	*rtdst = rt;
}

void
range_tree_vacate(range_tree_t *rt, range_tree_func_t *func, void *arg)
{
	range_seg_t *rs;
	void *cookie = NULL;

	ASSERT(MUTEX_HELD(rt->rt_lock));

	if (rt->rt_ops != NULL)
		rt->rt_ops->rtop_vacate(rt, rt->rt_arg);

	while ((rs = avl_destroy_nodes(&rt->rt_root, &cookie)) != NULL) {
		if (func != NULL)
			func(arg, rs->rs_start, rs->rs_end - rs->rs_start);
		kmem_cache_free(range_seg_cache, rs);
	}

	bzero(rt->rt_histogram, sizeof (rt->rt_histogram));
	rt->rt_space = 0;
}

void
range_tree_walk(range_tree_t *rt, range_tree_func_t *func, void *arg)
{
	range_seg_t *rs;

	ASSERT(MUTEX_HELD(rt->rt_lock));

	for (rs = avl_first(&rt->rt_root); rs; rs = AVL_NEXT(&rt->rt_root, rs))
		func(arg, rs->rs_start, rs->rs_end - rs->rs_start);
}

uint64_t
range_tree_space(range_tree_t *rt)
{
	return (rt->rt_space);
}
