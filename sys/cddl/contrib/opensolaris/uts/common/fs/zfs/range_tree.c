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
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dnode.h>
#include <sys/zio.h>
#include <sys/range_tree.h>

/*
 * Range trees are tree-based data structures that can be used to
 * track free space or generally any space allocation information.
 * A range tree keeps track of individual segments and automatically
 * provides facilities such as adjacent extent merging and extent
 * splitting in response to range add/remove requests.
 *
 * A range tree starts out completely empty, with no segments in it.
 * Adding an allocation via range_tree_add to the range tree can either:
 * 1) create a new extent
 * 2) extend an adjacent extent
 * 3) merge two adjacent extents
 * Conversely, removing an allocation via range_tree_remove can:
 * 1) completely remove an extent
 * 2) shorten an extent (if the allocation was near one of its ends)
 * 3) split an extent into two extents, in effect punching a hole
 *
 * A range tree is also capable of 'bridging' gaps when adding
 * allocations. This is useful for cases when close proximity of
 * allocations is an important detail that needs to be represented
 * in the range tree. See range_tree_set_gap(). The default behavior
 * is not to bridge gaps (i.e. the maximum allowed gap size is 0).
 *
 * In order to traverse a range tree, use either the range_tree_walk()
 * or range_tree_vacate() functions.
 *
 * To obtain more accurate information on individual segment
 * operations that the range tree performs "under the hood", you can
 * specify a set of callbacks by passing a range_tree_ops_t structure
 * to the range_tree_create function. Any callbacks that are non-NULL
 * are then called at the appropriate times.
 *
 * The range tree code also supports a special variant of range trees
 * that can bridge small gaps between segments. This kind of tree is used
 * by the dsl scanning code to group I/Os into mostly sequential chunks to
 * optimize disk performance. The code here attempts to do this with as
 * little memory and computational overhead as possible. One limitation of
 * this implementation is that segments of range trees with gaps can only
 * support removing complete segments.
 */

kmem_cache_t *range_seg_cache;

/* Generic ops for managing an AVL tree alongside a range tree */
struct range_tree_ops rt_avl_ops = {
	.rtop_create = rt_avl_create,
	.rtop_destroy = rt_avl_destroy,
	.rtop_add = rt_avl_add,
	.rtop_remove = rt_avl_remove,
	.rtop_vacate = rt_avl_vacate,
};

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
range_tree_create_impl(range_tree_ops_t *ops, void *arg,
    int (*avl_compare) (const void *, const void *), uint64_t gap)
{
	range_tree_t *rt = kmem_zalloc(sizeof (range_tree_t), KM_SLEEP);

	avl_create(&rt->rt_root, range_tree_seg_compare,
	    sizeof (range_seg_t), offsetof(range_seg_t, rs_node));

	rt->rt_ops = ops;
	rt->rt_arg = arg;
	rt->rt_gap = gap;
	rt->rt_avl_compare = avl_compare;

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_create != NULL)
		rt->rt_ops->rtop_create(rt, rt->rt_arg);

	return (rt);
}

range_tree_t *
range_tree_create(range_tree_ops_t *ops, void *arg)
{
	return (range_tree_create_impl(ops, arg, NULL, 0));
}

void
range_tree_destroy(range_tree_t *rt)
{
	VERIFY0(rt->rt_space);

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_destroy != NULL)
		rt->rt_ops->rtop_destroy(rt, rt->rt_arg);

	avl_destroy(&rt->rt_root);
	kmem_free(rt, sizeof (*rt));
}

void
range_tree_adjust_fill(range_tree_t *rt, range_seg_t *rs, int64_t delta)
{
	ASSERT3U(rs->rs_fill + delta, !=, 0);
	ASSERT3U(rs->rs_fill + delta, <=, rs->rs_end - rs->rs_start);

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);
	rs->rs_fill += delta;
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);
}

static void
range_tree_add_impl(void *arg, uint64_t start, uint64_t size, uint64_t fill)
{
	range_tree_t *rt = arg;
	avl_index_t where;
	range_seg_t rsearch, *rs_before, *rs_after, *rs;
	uint64_t end = start + size, gap = rt->rt_gap;
	uint64_t bridge_size = 0;
	boolean_t merge_before, merge_after;

	ASSERT3U(size, !=, 0);
	ASSERT3U(fill, <=, size);

	rsearch.rs_start = start;
	rsearch.rs_end = end;
	rs = avl_find(&rt->rt_root, &rsearch, &where);

	if (gap == 0 && rs != NULL &&
	    rs->rs_start <= start && rs->rs_end >= end) {
		zfs_panic_recover("zfs: allocating allocated segment"
		    "(offset=%llu size=%llu) of (offset=%llu size=%llu)\n",
		    (longlong_t)start, (longlong_t)size,
		    (longlong_t)rs->rs_start,
		    (longlong_t)rs->rs_end - rs->rs_start);
		return;
	}

	/*
	 * If this is a gap-supporting range tree, it is possible that we
	 * are inserting into an existing segment. In this case simply
	 * bump the fill count and call the remove / add callbacks. If the
	 * new range will extend an existing segment, we remove the
	 * existing one, apply the new extent to it and re-insert it using
	 * the normal code paths.
	 */
	if (rs != NULL) {
		ASSERT3U(gap, !=, 0);
		if (rs->rs_start <= start && rs->rs_end >= end) {
			range_tree_adjust_fill(rt, rs, fill);
			return;
		}

		avl_remove(&rt->rt_root, rs);
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

		range_tree_stat_decr(rt, rs);
		rt->rt_space -= rs->rs_end - rs->rs_start;

		fill += rs->rs_fill;
		start = MIN(start, rs->rs_start);
		end = MAX(end, rs->rs_end);
		size = end - start;

		range_tree_add_impl(rt, start, size, fill);

		kmem_cache_free(range_seg_cache, rs);
		return;
	}

	ASSERT3P(rs, ==, NULL);

	/*
	 * Determine whether or not we will have to merge with our neighbors.
	 * If gap != 0, we might need to merge with our neighbors even if we
	 * aren't directly touching.
	 */
	rs_before = avl_nearest(&rt->rt_root, where, AVL_BEFORE);
	rs_after = avl_nearest(&rt->rt_root, where, AVL_AFTER);

	merge_before = (rs_before != NULL && rs_before->rs_end >= start - gap);
	merge_after = (rs_after != NULL && rs_after->rs_start <= end + gap);

	if (merge_before && gap != 0)
		bridge_size += start - rs_before->rs_end;
	if (merge_after && gap != 0)
		bridge_size += rs_after->rs_start - end;

	if (merge_before && merge_after) {
		avl_remove(&rt->rt_root, rs_before);
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL) {
			rt->rt_ops->rtop_remove(rt, rs_before, rt->rt_arg);
			rt->rt_ops->rtop_remove(rt, rs_after, rt->rt_arg);
		}

		range_tree_stat_decr(rt, rs_before);
		range_tree_stat_decr(rt, rs_after);

		rs_after->rs_fill += rs_before->rs_fill + fill;
		rs_after->rs_start = rs_before->rs_start;
		kmem_cache_free(range_seg_cache, rs_before);
		rs = rs_after;
	} else if (merge_before) {
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs_before, rt->rt_arg);

		range_tree_stat_decr(rt, rs_before);

		rs_before->rs_fill += fill;
		rs_before->rs_end = end;
		rs = rs_before;
	} else if (merge_after) {
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
			rt->rt_ops->rtop_remove(rt, rs_after, rt->rt_arg);

		range_tree_stat_decr(rt, rs_after);

		rs_after->rs_fill += fill;
		rs_after->rs_start = start;
		rs = rs_after;
	} else {
		rs = kmem_cache_alloc(range_seg_cache, KM_SLEEP);

		rs->rs_fill = fill;
		rs->rs_start = start;
		rs->rs_end = end;
		avl_insert(&rt->rt_root, rs, where);
	}

	if (gap != 0)
		ASSERT3U(rs->rs_fill, <=, rs->rs_end - rs->rs_start);
	else
		ASSERT3U(rs->rs_fill, ==, rs->rs_end - rs->rs_start);

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);

	range_tree_stat_incr(rt, rs);
	rt->rt_space += size + bridge_size;
}

void
range_tree_add(void *arg, uint64_t start, uint64_t size)
{
	range_tree_add_impl(arg, start, size, size);
}

static void
range_tree_remove_impl(range_tree_t *rt, uint64_t start, uint64_t size,
    boolean_t do_fill)
{
	avl_index_t where;
	range_seg_t rsearch, *rs, *newseg;
	uint64_t end = start + size;
	boolean_t left_over, right_over;

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

	/*
	 * Range trees with gap support must only remove complete segments
	 * from the tree. This allows us to maintain accurate fill accounting
	 * and to ensure that bridged sections are not leaked. If we need to
	 * remove less than the full segment, we can only adjust the fill count.
	 */
	if (rt->rt_gap != 0) {
		if (do_fill) {
			if (rs->rs_fill == size) {
				start = rs->rs_start;
				end = rs->rs_end;
				size = end - start;
			} else {
				range_tree_adjust_fill(rt, rs, -size);
				return;
			}
		} else if (rs->rs_start != start || rs->rs_end != end) {
			zfs_panic_recover("zfs: freeing partial segment of "
			    "gap tree (offset=%llu size=%llu) of "
			    "(offset=%llu size=%llu)",
			    (longlong_t)start, (longlong_t)size,
			    (longlong_t)rs->rs_start,
			    (longlong_t)rs->rs_end - rs->rs_start);
			return;
		}
	}

	VERIFY3U(rs->rs_start, <=, start);
	VERIFY3U(rs->rs_end, >=, end);

	left_over = (rs->rs_start != start);
	right_over = (rs->rs_end != end);

	range_tree_stat_decr(rt, rs);

	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

	if (left_over && right_over) {
		newseg = kmem_cache_alloc(range_seg_cache, KM_SLEEP);
		newseg->rs_start = end;
		newseg->rs_end = rs->rs_end;
		newseg->rs_fill = newseg->rs_end - newseg->rs_start;
		range_tree_stat_incr(rt, newseg);

		rs->rs_end = start;

		avl_insert_here(&rt->rt_root, newseg, rs, AVL_AFTER);
		if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
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
		/*
		 * The fill of the leftover segment will always be equal to
		 * the size, since we do not support removing partial segments
		 * of range trees with gaps.
		 */
		rs->rs_fill = rs->rs_end - rs->rs_start;
		range_tree_stat_incr(rt, rs);

		if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
			rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);
	}

	rt->rt_space -= size;
}

void
range_tree_remove(void *arg, uint64_t start, uint64_t size)
{
	range_tree_remove_impl(arg, start, size, B_FALSE);
}

void
range_tree_remove_fill(range_tree_t *rt, uint64_t start, uint64_t size)
{
	range_tree_remove_impl(rt, start, size, B_TRUE);
}

void
range_tree_resize_segment(range_tree_t *rt, range_seg_t *rs,
    uint64_t newstart, uint64_t newsize)
{
	int64_t delta = newsize - (rs->rs_end - rs->rs_start);

	range_tree_stat_decr(rt, rs);
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_remove != NULL)
		rt->rt_ops->rtop_remove(rt, rs, rt->rt_arg);

	rs->rs_start = newstart;
	rs->rs_end = newstart + newsize;

	range_tree_stat_incr(rt, rs);
	if (rt->rt_ops != NULL && rt->rt_ops->rtop_add != NULL)
		rt->rt_ops->rtop_add(rt, rs, rt->rt_arg);

	rt->rt_space += delta;
}

static range_seg_t *
range_tree_find_impl(range_tree_t *rt, uint64_t start, uint64_t size)
{
	range_seg_t rsearch;
	uint64_t end = start + size;

	VERIFY(size != 0);

	rsearch.rs_start = start;
	rsearch.rs_end = end;
	return (avl_find(&rt->rt_root, &rsearch, NULL));
}

range_seg_t *
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

	rs = range_tree_find(rt, off, size);
	if (rs != NULL)
		panic("freeing free block; rs=%p", (void *)rs);
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

	if (size == 0)
		return;

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


	if (rt->rt_ops != NULL && rt->rt_ops->rtop_vacate != NULL)
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

	for (rs = avl_first(&rt->rt_root); rs; rs = AVL_NEXT(&rt->rt_root, rs))
		func(arg, rs->rs_start, rs->rs_end - rs->rs_start);
}

range_seg_t *
range_tree_first(range_tree_t *rt)
{
	return (avl_first(&rt->rt_root));
}

uint64_t
range_tree_space(range_tree_t *rt)
{
	return (rt->rt_space);
}

/* Generic range tree functions for maintaining segments in an AVL tree. */
void
rt_avl_create(range_tree_t *rt, void *arg)
{
	avl_tree_t *tree = arg;

	avl_create(tree, rt->rt_avl_compare, sizeof (range_seg_t),
	    offsetof(range_seg_t, rs_pp_node));
}

void
rt_avl_destroy(range_tree_t *rt, void *arg)
{
	avl_tree_t *tree = arg;

	ASSERT0(avl_numnodes(tree));
	avl_destroy(tree);
}

void
rt_avl_add(range_tree_t *rt, range_seg_t *rs, void *arg)
{
	avl_tree_t *tree = arg;
	avl_add(tree, rs);
}

void
rt_avl_remove(range_tree_t *rt, range_seg_t *rs, void *arg)
{
	avl_tree_t *tree = arg;
	avl_remove(tree, rs);
}

void
rt_avl_vacate(range_tree_t *rt, void *arg)
{
	/*
	 * Normally one would walk the tree freeing nodes along the way.
	 * Since the nodes are shared with the range trees we can avoid
	 * walking all nodes and just reinitialize the avl tree. The nodes
	 * will be freed by the range tree, so we don't want to free them here.
	 */
	rt_avl_create(rt, arg);
}

boolean_t
range_tree_is_empty(range_tree_t *rt)
{
	ASSERT(rt != NULL);
	return (range_tree_space(rt) == 0);
}

uint64_t
range_tree_min(range_tree_t *rt)
{
	range_seg_t *rs = avl_first(&rt->rt_root);
	return (rs != NULL ? rs->rs_start : 0);
}

uint64_t
range_tree_max(range_tree_t *rt)
{
	range_seg_t *rs = avl_last(&rt->rt_root);
	return (rs != NULL ? rs->rs_end : 0);
}

uint64_t
range_tree_span(range_tree_t *rt)
{
	return (range_tree_max(rt) - range_tree_min(rt));
}
