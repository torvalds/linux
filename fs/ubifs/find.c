/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file contains functions for finding LEBs for various purposes e.g.
 * garbage collection. In general, lprops category heaps and lists are used
 * for fast access, falling back on scanning the LPT as a last resort.
 */

#include <linux/sort.h>
#include "ubifs.h"

/**
 * struct scan_data - data provided to scan callback functions
 * @min_space: minimum number of bytes for which to scan
 * @pick_free: whether it is OK to scan for empty LEBs
 * @lnum: LEB number found is returned here
 * @exclude_index: whether to exclude index LEBs
 */
struct scan_data {
	int min_space;
	int pick_free;
	int lnum;
	int exclude_index;
};

/**
 * valuable - determine whether LEB properties are valuable.
 * @c: the UBIFS file-system description object
 * @lprops: LEB properties
 *
 * This function return %1 if the LEB properties should be added to the LEB
 * properties tree in memory. Otherwise %0 is returned.
 */
static int valuable(struct ubifs_info *c, const struct ubifs_lprops *lprops)
{
	int n, cat = lprops->flags & LPROPS_CAT_MASK;
	struct ubifs_lpt_heap *heap;

	switch (cat) {
	case LPROPS_DIRTY:
	case LPROPS_DIRTY_IDX:
	case LPROPS_FREE:
		heap = &c->lpt_heap[cat - 1];
		if (heap->cnt < heap->max_cnt)
			return 1;
		if (lprops->free + lprops->dirty >= c->dark_wm)
			return 1;
		return 0;
	case LPROPS_EMPTY:
		n = c->lst.empty_lebs + c->freeable_cnt -
		    c->lst.taken_empty_lebs;
		if (n < c->lsave_cnt)
			return 1;
		return 0;
	case LPROPS_FREEABLE:
		return 1;
	case LPROPS_FRDI_IDX:
		return 1;
	}
	return 0;
}

/**
 * scan_for_dirty_cb - dirty space scan callback.
 * @c: the UBIFS file-system description object
 * @lprops: LEB properties to scan
 * @in_tree: whether the LEB properties are in main memory
 * @data: information passed to and from the caller of the scan
 *
 * This function returns a code that indicates whether the scan should continue
 * (%LPT_SCAN_CONTINUE), whether the LEB properties should be added to the tree
 * in main memory (%LPT_SCAN_ADD), or whether the scan should stop
 * (%LPT_SCAN_STOP).
 */
static int scan_for_dirty_cb(struct ubifs_info *c,
			     const struct ubifs_lprops *lprops, int in_tree,
			     struct scan_data *data)
{
	int ret = LPT_SCAN_CONTINUE;

	/* Exclude LEBs that are currently in use */
	if (lprops->flags & LPROPS_TAKEN)
		return LPT_SCAN_CONTINUE;
	/* Determine whether to add these LEB properties to the tree */
	if (!in_tree && valuable(c, lprops))
		ret |= LPT_SCAN_ADD;
	/* Exclude LEBs with too little space */
	if (lprops->free + lprops->dirty < data->min_space)
		return ret;
	/* If specified, exclude index LEBs */
	if (data->exclude_index && lprops->flags & LPROPS_INDEX)
		return ret;
	/* If specified, exclude empty or freeable LEBs */
	if (lprops->free + lprops->dirty == c->leb_size) {
		if (!data->pick_free)
			return ret;
	/* Exclude LEBs with too little dirty space (unless it is empty) */
	} else if (lprops->dirty < c->dead_wm)
		return ret;
	/* Finally we found space */
	data->lnum = lprops->lnum;
	return LPT_SCAN_ADD | LPT_SCAN_STOP;
}

/**
 * scan_for_dirty - find a data LEB with free space.
 * @c: the UBIFS file-system description object
 * @min_space: minimum amount free plus dirty space the returned LEB has to
 *             have
 * @pick_free: if it is OK to return a free or freeable LEB
 * @exclude_index: whether to exclude index LEBs
 *
 * This function returns a pointer to the LEB properties found or a negative
 * error code.
 */
static const struct ubifs_lprops *scan_for_dirty(struct ubifs_info *c,
						 int min_space, int pick_free,
						 int exclude_index)
{
	const struct ubifs_lprops *lprops;
	struct ubifs_lpt_heap *heap;
	struct scan_data data;
	int err, i;

	/* There may be an LEB with enough dirty space on the free heap */
	heap = &c->lpt_heap[LPROPS_FREE - 1];
	for (i = 0; i < heap->cnt; i++) {
		lprops = heap->arr[i];
		if (lprops->free + lprops->dirty < min_space)
			continue;
		if (lprops->dirty < c->dead_wm)
			continue;
		return lprops;
	}
	/*
	 * A LEB may have fallen off of the bottom of the dirty heap, and ended
	 * up as uncategorized even though it has enough dirty space for us now,
	 * so check the uncategorized list. N.B. neither empty nor freeable LEBs
	 * can end up as uncategorized because they are kept on lists not
	 * finite-sized heaps.
	 */
	list_for_each_entry(lprops, &c->uncat_list, list) {
		if (lprops->flags & LPROPS_TAKEN)
			continue;
		if (lprops->free + lprops->dirty < min_space)
			continue;
		if (exclude_index && (lprops->flags & LPROPS_INDEX))
			continue;
		if (lprops->dirty < c->dead_wm)
			continue;
		return lprops;
	}
	/* We have looked everywhere in main memory, now scan the flash */
	if (c->pnodes_have >= c->pnode_cnt)
		/* All pnodes are in memory, so skip scan */
		return ERR_PTR(-ENOSPC);
	data.min_space = min_space;
	data.pick_free = pick_free;
	data.lnum = -1;
	data.exclude_index = exclude_index;
	err = ubifs_lpt_scan_nolock(c, -1, c->lscan_lnum,
				    (ubifs_lpt_scan_callback)scan_for_dirty_cb,
				    &data);
	if (err)
		return ERR_PTR(err);
	ubifs_assert(c, data.lnum >= c->main_first && data.lnum < c->leb_cnt);
	c->lscan_lnum = data.lnum;
	lprops = ubifs_lpt_lookup_dirty(c, data.lnum);
	if (IS_ERR(lprops))
		return lprops;
	ubifs_assert(c, lprops->lnum == data.lnum);
	ubifs_assert(c, lprops->free + lprops->dirty >= min_space);
	ubifs_assert(c, lprops->dirty >= c->dead_wm ||
		     (pick_free &&
		      lprops->free + lprops->dirty == c->leb_size));
	ubifs_assert(c, !(lprops->flags & LPROPS_TAKEN));
	ubifs_assert(c, !exclude_index || !(lprops->flags & LPROPS_INDEX));
	return lprops;
}

/**
 * ubifs_find_dirty_leb - find a dirty LEB for the Garbage Collector.
 * @c: the UBIFS file-system description object
 * @ret_lp: LEB properties are returned here on exit
 * @min_space: minimum amount free plus dirty space the returned LEB has to
 *             have
 * @pick_free: controls whether it is OK to pick empty or index LEBs
 *
 * This function tries to find a dirty logical eraseblock which has at least
 * @min_space free and dirty space. It prefers to take an LEB from the dirty or
 * dirty index heap, and it falls-back to LPT scanning if the heaps are empty
 * or do not have an LEB which satisfies the @min_space criteria.
 *
 * Note, LEBs which have less than dead watermark of free + dirty space are
 * never picked by this function.
 *
 * The additional @pick_free argument controls if this function has to return a
 * free or freeable LEB if one is present. For example, GC must to set it to %1,
 * when called from the journal space reservation function, because the
 * appearance of free space may coincide with the loss of enough dirty space
 * for GC to succeed anyway.
 *
 * In contrast, if the Garbage Collector is called from budgeting, it should
 * just make free space, not return LEBs which are already free or freeable.
 *
 * In addition @pick_free is set to %2 by the recovery process in order to
 * recover gc_lnum in which case an index LEB must not be returned.
 *
 * This function returns zero and the LEB properties of found dirty LEB in case
 * of success, %-ENOSPC if no dirty LEB was found and a negative error code in
 * case of other failures. The returned LEB is marked as "taken".
 */
int ubifs_find_dirty_leb(struct ubifs_info *c, struct ubifs_lprops *ret_lp,
			 int min_space, int pick_free)
{
	int err = 0, sum, exclude_index = pick_free == 2 ? 1 : 0;
	const struct ubifs_lprops *lp = NULL, *idx_lp = NULL;
	struct ubifs_lpt_heap *heap, *idx_heap;

	ubifs_get_lprops(c);

	if (pick_free) {
		int lebs, rsvd_idx_lebs = 0;

		spin_lock(&c->space_lock);
		lebs = c->lst.empty_lebs + c->idx_gc_cnt;
		lebs += c->freeable_cnt - c->lst.taken_empty_lebs;

		/*
		 * Note, the index may consume more LEBs than have been reserved
		 * for it. It is OK because it might be consolidated by GC.
		 * But if the index takes fewer LEBs than it is reserved for it,
		 * this function must avoid picking those reserved LEBs.
		 */
		if (c->bi.min_idx_lebs >= c->lst.idx_lebs) {
			rsvd_idx_lebs = c->bi.min_idx_lebs -  c->lst.idx_lebs;
			exclude_index = 1;
		}
		spin_unlock(&c->space_lock);

		/* Check if there are enough free LEBs for the index */
		if (rsvd_idx_lebs < lebs) {
			/* OK, try to find an empty LEB */
			lp = ubifs_fast_find_empty(c);
			if (lp)
				goto found;

			/* Or a freeable LEB */
			lp = ubifs_fast_find_freeable(c);
			if (lp)
				goto found;
		} else
			/*
			 * We cannot pick free/freeable LEBs in the below code.
			 */
			pick_free = 0;
	} else {
		spin_lock(&c->space_lock);
		exclude_index = (c->bi.min_idx_lebs >= c->lst.idx_lebs);
		spin_unlock(&c->space_lock);
	}

	/* Look on the dirty and dirty index heaps */
	heap = &c->lpt_heap[LPROPS_DIRTY - 1];
	idx_heap = &c->lpt_heap[LPROPS_DIRTY_IDX - 1];

	if (idx_heap->cnt && !exclude_index) {
		idx_lp = idx_heap->arr[0];
		sum = idx_lp->free + idx_lp->dirty;
		/*
		 * Since we reserve thrice as much space for the index than it
		 * actually takes, it does not make sense to pick indexing LEBs
		 * with less than, say, half LEB of dirty space. May be half is
		 * not the optimal boundary - this should be tested and
		 * checked. This boundary should determine how much we use
		 * in-the-gaps to consolidate the index comparing to how much
		 * we use garbage collector to consolidate it. The "half"
		 * criteria just feels to be fine.
		 */
		if (sum < min_space || sum < c->half_leb_size)
			idx_lp = NULL;
	}

	if (heap->cnt) {
		lp = heap->arr[0];
		if (lp->dirty + lp->free < min_space)
			lp = NULL;
	}

	/* Pick the LEB with most space */
	if (idx_lp && lp) {
		if (idx_lp->free + idx_lp->dirty >= lp->free + lp->dirty)
			lp = idx_lp;
	} else if (idx_lp && !lp)
		lp = idx_lp;

	if (lp) {
		ubifs_assert(c, lp->free + lp->dirty >= c->dead_wm);
		goto found;
	}

	/* Did not find a dirty LEB on the dirty heaps, have to scan */
	dbg_find("scanning LPT for a dirty LEB");
	lp = scan_for_dirty(c, min_space, pick_free, exclude_index);
	if (IS_ERR(lp)) {
		err = PTR_ERR(lp);
		goto out;
	}
	ubifs_assert(c, lp->dirty >= c->dead_wm ||
		     (pick_free && lp->free + lp->dirty == c->leb_size));

found:
	dbg_find("found LEB %d, free %d, dirty %d, flags %#x",
		 lp->lnum, lp->free, lp->dirty, lp->flags);

	lp = ubifs_change_lp(c, lp, LPROPS_NC, LPROPS_NC,
			     lp->flags | LPROPS_TAKEN, 0);
	if (IS_ERR(lp)) {
		err = PTR_ERR(lp);
		goto out;
	}

	memcpy(ret_lp, lp, sizeof(struct ubifs_lprops));

out:
	ubifs_release_lprops(c);
	return err;
}

/**
 * scan_for_free_cb - free space scan callback.
 * @c: the UBIFS file-system description object
 * @lprops: LEB properties to scan
 * @in_tree: whether the LEB properties are in main memory
 * @data: information passed to and from the caller of the scan
 *
 * This function returns a code that indicates whether the scan should continue
 * (%LPT_SCAN_CONTINUE), whether the LEB properties should be added to the tree
 * in main memory (%LPT_SCAN_ADD), or whether the scan should stop
 * (%LPT_SCAN_STOP).
 */
static int scan_for_free_cb(struct ubifs_info *c,
			    const struct ubifs_lprops *lprops, int in_tree,
			    struct scan_data *data)
{
	int ret = LPT_SCAN_CONTINUE;

	/* Exclude LEBs that are currently in use */
	if (lprops->flags & LPROPS_TAKEN)
		return LPT_SCAN_CONTINUE;
	/* Determine whether to add these LEB properties to the tree */
	if (!in_tree && valuable(c, lprops))
		ret |= LPT_SCAN_ADD;
	/* Exclude index LEBs */
	if (lprops->flags & LPROPS_INDEX)
		return ret;
	/* Exclude LEBs with too little space */
	if (lprops->free < data->min_space)
		return ret;
	/* If specified, exclude empty LEBs */
	if (!data->pick_free && lprops->free == c->leb_size)
		return ret;
	/*
	 * LEBs that have only free and dirty space must not be allocated
	 * because they may have been unmapped already or they may have data
	 * that is obsolete only because of nodes that are still sitting in a
	 * wbuf.
	 */
	if (lprops->free + lprops->dirty == c->leb_size && lprops->dirty > 0)
		return ret;
	/* Finally we found space */
	data->lnum = lprops->lnum;
	return LPT_SCAN_ADD | LPT_SCAN_STOP;
}

/**
 * do_find_free_space - find a data LEB with free space.
 * @c: the UBIFS file-system description object
 * @min_space: minimum amount of free space required
 * @pick_free: whether it is OK to scan for empty LEBs
 * @squeeze: whether to try to find space in a non-empty LEB first
 *
 * This function returns a pointer to the LEB properties found or a negative
 * error code.
 */
static
const struct ubifs_lprops *do_find_free_space(struct ubifs_info *c,
					      int min_space, int pick_free,
					      int squeeze)
{
	const struct ubifs_lprops *lprops;
	struct ubifs_lpt_heap *heap;
	struct scan_data data;
	int err, i;

	if (squeeze) {
		lprops = ubifs_fast_find_free(c);
		if (lprops && lprops->free >= min_space)
			return lprops;
	}
	if (pick_free) {
		lprops = ubifs_fast_find_empty(c);
		if (lprops)
			return lprops;
	}
	if (!squeeze) {
		lprops = ubifs_fast_find_free(c);
		if (lprops && lprops->free >= min_space)
			return lprops;
	}
	/* There may be an LEB with enough free space on the dirty heap */
	heap = &c->lpt_heap[LPROPS_DIRTY - 1];
	for (i = 0; i < heap->cnt; i++) {
		lprops = heap->arr[i];
		if (lprops->free >= min_space)
			return lprops;
	}
	/*
	 * A LEB may have fallen off of the bottom of the free heap, and ended
	 * up as uncategorized even though it has enough free space for us now,
	 * so check the uncategorized list. N.B. neither empty nor freeable LEBs
	 * can end up as uncategorized because they are kept on lists not
	 * finite-sized heaps.
	 */
	list_for_each_entry(lprops, &c->uncat_list, list) {
		if (lprops->flags & LPROPS_TAKEN)
			continue;
		if (lprops->flags & LPROPS_INDEX)
			continue;
		if (lprops->free >= min_space)
			return lprops;
	}
	/* We have looked everywhere in main memory, now scan the flash */
	if (c->pnodes_have >= c->pnode_cnt)
		/* All pnodes are in memory, so skip scan */
		return ERR_PTR(-ENOSPC);
	data.min_space = min_space;
	data.pick_free = pick_free;
	data.lnum = -1;
	err = ubifs_lpt_scan_nolock(c, -1, c->lscan_lnum,
				    (ubifs_lpt_scan_callback)scan_for_free_cb,
				    &data);
	if (err)
		return ERR_PTR(err);
	ubifs_assert(c, data.lnum >= c->main_first && data.lnum < c->leb_cnt);
	c->lscan_lnum = data.lnum;
	lprops = ubifs_lpt_lookup_dirty(c, data.lnum);
	if (IS_ERR(lprops))
		return lprops;
	ubifs_assert(c, lprops->lnum == data.lnum);
	ubifs_assert(c, lprops->free >= min_space);
	ubifs_assert(c, !(lprops->flags & LPROPS_TAKEN));
	ubifs_assert(c, !(lprops->flags & LPROPS_INDEX));
	return lprops;
}

/**
 * ubifs_find_free_space - find a data LEB with free space.
 * @c: the UBIFS file-system description object
 * @min_space: minimum amount of required free space
 * @offs: contains offset of where free space starts on exit
 * @squeeze: whether to try to find space in a non-empty LEB first
 *
 * This function looks for an LEB with at least @min_space bytes of free space.
 * It tries to find an empty LEB if possible. If no empty LEBs are available,
 * this function searches for a non-empty data LEB. The returned LEB is marked
 * as "taken".
 *
 * This function returns found LEB number in case of success, %-ENOSPC if it
 * failed to find a LEB with @min_space bytes of free space and other a negative
 * error codes in case of failure.
 */
int ubifs_find_free_space(struct ubifs_info *c, int min_space, int *offs,
			  int squeeze)
{
	const struct ubifs_lprops *lprops;
	int lebs, rsvd_idx_lebs, pick_free = 0, err, lnum, flags;

	dbg_find("min_space %d", min_space);
	ubifs_get_lprops(c);

	/* Check if there are enough empty LEBs for commit */
	spin_lock(&c->space_lock);
	if (c->bi.min_idx_lebs > c->lst.idx_lebs)
		rsvd_idx_lebs = c->bi.min_idx_lebs -  c->lst.idx_lebs;
	else
		rsvd_idx_lebs = 0;
	lebs = c->lst.empty_lebs + c->freeable_cnt + c->idx_gc_cnt -
	       c->lst.taken_empty_lebs;
	if (rsvd_idx_lebs < lebs)
		/*
		 * OK to allocate an empty LEB, but we still don't want to go
		 * looking for one if there aren't any.
		 */
		if (c->lst.empty_lebs - c->lst.taken_empty_lebs > 0) {
			pick_free = 1;
			/*
			 * Because we release the space lock, we must account
			 * for this allocation here. After the LEB properties
			 * flags have been updated, we subtract one. Note, the
			 * result of this is that lprops also decreases
			 * @taken_empty_lebs in 'ubifs_change_lp()', so it is
			 * off by one for a short period of time which may
			 * introduce a small disturbance to budgeting
			 * calculations, but this is harmless because at the
			 * worst case this would make the budgeting subsystem
			 * be more pessimistic than needed.
			 *
			 * Fundamentally, this is about serialization of the
			 * budgeting and lprops subsystems. We could make the
			 * @space_lock a mutex and avoid dropping it before
			 * calling 'ubifs_change_lp()', but mutex is more
			 * heavy-weight, and we want budgeting to be as fast as
			 * possible.
			 */
			c->lst.taken_empty_lebs += 1;
		}
	spin_unlock(&c->space_lock);

	lprops = do_find_free_space(c, min_space, pick_free, squeeze);
	if (IS_ERR(lprops)) {
		err = PTR_ERR(lprops);
		goto out;
	}

	lnum = lprops->lnum;
	flags = lprops->flags | LPROPS_TAKEN;

	lprops = ubifs_change_lp(c, lprops, LPROPS_NC, LPROPS_NC, flags, 0);
	if (IS_ERR(lprops)) {
		err = PTR_ERR(lprops);
		goto out;
	}

	if (pick_free) {
		spin_lock(&c->space_lock);
		c->lst.taken_empty_lebs -= 1;
		spin_unlock(&c->space_lock);
	}

	*offs = c->leb_size - lprops->free;
	ubifs_release_lprops(c);

	if (*offs == 0) {
		/*
		 * Ensure that empty LEBs have been unmapped. They may not have
		 * been, for example, because of an unclean unmount.  Also
		 * LEBs that were freeable LEBs (free + dirty == leb_size) will
		 * not have been unmapped.
		 */
		err = ubifs_leb_unmap(c, lnum);
		if (err)
			return err;
	}

	dbg_find("found LEB %d, free %d", lnum, c->leb_size - *offs);
	ubifs_assert(c, *offs <= c->leb_size - min_space);
	return lnum;

out:
	if (pick_free) {
		spin_lock(&c->space_lock);
		c->lst.taken_empty_lebs -= 1;
		spin_unlock(&c->space_lock);
	}
	ubifs_release_lprops(c);
	return err;
}

/**
 * scan_for_idx_cb - callback used by the scan for a free LEB for the index.
 * @c: the UBIFS file-system description object
 * @lprops: LEB properties to scan
 * @in_tree: whether the LEB properties are in main memory
 * @data: information passed to and from the caller of the scan
 *
 * This function returns a code that indicates whether the scan should continue
 * (%LPT_SCAN_CONTINUE), whether the LEB properties should be added to the tree
 * in main memory (%LPT_SCAN_ADD), or whether the scan should stop
 * (%LPT_SCAN_STOP).
 */
static int scan_for_idx_cb(struct ubifs_info *c,
			   const struct ubifs_lprops *lprops, int in_tree,
			   struct scan_data *data)
{
	int ret = LPT_SCAN_CONTINUE;

	/* Exclude LEBs that are currently in use */
	if (lprops->flags & LPROPS_TAKEN)
		return LPT_SCAN_CONTINUE;
	/* Determine whether to add these LEB properties to the tree */
	if (!in_tree && valuable(c, lprops))
		ret |= LPT_SCAN_ADD;
	/* Exclude index LEBS */
	if (lprops->flags & LPROPS_INDEX)
		return ret;
	/* Exclude LEBs that cannot be made empty */
	if (lprops->free + lprops->dirty != c->leb_size)
		return ret;
	/*
	 * We are allocating for the index so it is safe to allocate LEBs with
	 * only free and dirty space, because write buffers are sync'd at commit
	 * start.
	 */
	data->lnum = lprops->lnum;
	return LPT_SCAN_ADD | LPT_SCAN_STOP;
}

/**
 * scan_for_leb_for_idx - scan for a free LEB for the index.
 * @c: the UBIFS file-system description object
 */
static const struct ubifs_lprops *scan_for_leb_for_idx(struct ubifs_info *c)
{
	const struct ubifs_lprops *lprops;
	struct scan_data data;
	int err;

	data.lnum = -1;
	err = ubifs_lpt_scan_nolock(c, -1, c->lscan_lnum,
				    (ubifs_lpt_scan_callback)scan_for_idx_cb,
				    &data);
	if (err)
		return ERR_PTR(err);
	ubifs_assert(c, data.lnum >= c->main_first && data.lnum < c->leb_cnt);
	c->lscan_lnum = data.lnum;
	lprops = ubifs_lpt_lookup_dirty(c, data.lnum);
	if (IS_ERR(lprops))
		return lprops;
	ubifs_assert(c, lprops->lnum == data.lnum);
	ubifs_assert(c, lprops->free + lprops->dirty == c->leb_size);
	ubifs_assert(c, !(lprops->flags & LPROPS_TAKEN));
	ubifs_assert(c, !(lprops->flags & LPROPS_INDEX));
	return lprops;
}

/**
 * ubifs_find_free_leb_for_idx - find a free LEB for the index.
 * @c: the UBIFS file-system description object
 *
 * This function looks for a free LEB and returns that LEB number. The returned
 * LEB is marked as "taken", "index".
 *
 * Only empty LEBs are allocated. This is for two reasons. First, the commit
 * calculates the number of LEBs to allocate based on the assumption that they
 * will be empty. Secondly, free space at the end of an index LEB is not
 * guaranteed to be empty because it may have been used by the in-the-gaps
 * method prior to an unclean unmount.
 *
 * If no LEB is found %-ENOSPC is returned. For other failures another negative
 * error code is returned.
 */
int ubifs_find_free_leb_for_idx(struct ubifs_info *c)
{
	const struct ubifs_lprops *lprops;
	int lnum = -1, err, flags;

	ubifs_get_lprops(c);

	lprops = ubifs_fast_find_empty(c);
	if (!lprops) {
		lprops = ubifs_fast_find_freeable(c);
		if (!lprops) {
			/*
			 * The first condition means the following: go scan the
			 * LPT if there are uncategorized lprops, which means
			 * there may be freeable LEBs there (UBIFS does not
			 * store the information about freeable LEBs in the
			 * master node).
			 */
			if (c->in_a_category_cnt != c->main_lebs ||
			    c->lst.empty_lebs - c->lst.taken_empty_lebs > 0) {
				ubifs_assert(c, c->freeable_cnt == 0);
				lprops = scan_for_leb_for_idx(c);
				if (IS_ERR(lprops)) {
					err = PTR_ERR(lprops);
					goto out;
				}
			}
		}
	}

	if (!lprops) {
		err = -ENOSPC;
		goto out;
	}

	lnum = lprops->lnum;

	dbg_find("found LEB %d, free %d, dirty %d, flags %#x",
		 lnum, lprops->free, lprops->dirty, lprops->flags);

	flags = lprops->flags | LPROPS_TAKEN | LPROPS_INDEX;
	lprops = ubifs_change_lp(c, lprops, c->leb_size, 0, flags, 0);
	if (IS_ERR(lprops)) {
		err = PTR_ERR(lprops);
		goto out;
	}

	ubifs_release_lprops(c);

	/*
	 * Ensure that empty LEBs have been unmapped. They may not have been,
	 * for example, because of an unclean unmount. Also LEBs that were
	 * freeable LEBs (free + dirty == leb_size) will not have been unmapped.
	 */
	err = ubifs_leb_unmap(c, lnum);
	if (err) {
		ubifs_change_one_lp(c, lnum, LPROPS_NC, LPROPS_NC, 0,
				    LPROPS_TAKEN | LPROPS_INDEX, 0);
		return err;
	}

	return lnum;

out:
	ubifs_release_lprops(c);
	return err;
}

static int cmp_dirty_idx(const struct ubifs_lprops **a,
			 const struct ubifs_lprops **b)
{
	const struct ubifs_lprops *lpa = *a;
	const struct ubifs_lprops *lpb = *b;

	return lpa->dirty + lpa->free - lpb->dirty - lpb->free;
}

/**
 * ubifs_save_dirty_idx_lnums - save an array of the most dirty index LEB nos.
 * @c: the UBIFS file-system description object
 *
 * This function is called each commit to create an array of LEB numbers of
 * dirty index LEBs sorted in order of dirty and free space.  This is used by
 * the in-the-gaps method of TNC commit.
 */
int ubifs_save_dirty_idx_lnums(struct ubifs_info *c)
{
	int i;

	ubifs_get_lprops(c);
	/* Copy the LPROPS_DIRTY_IDX heap */
	c->dirty_idx.cnt = c->lpt_heap[LPROPS_DIRTY_IDX - 1].cnt;
	memcpy(c->dirty_idx.arr, c->lpt_heap[LPROPS_DIRTY_IDX - 1].arr,
	       sizeof(void *) * c->dirty_idx.cnt);
	/* Sort it so that the dirtiest is now at the end */
	sort(c->dirty_idx.arr, c->dirty_idx.cnt, sizeof(void *),
	     (int (*)(const void *, const void *))cmp_dirty_idx, NULL);
	dbg_find("found %d dirty index LEBs", c->dirty_idx.cnt);
	if (c->dirty_idx.cnt)
		dbg_find("dirtiest index LEB is %d with dirty %d and free %d",
			 c->dirty_idx.arr[c->dirty_idx.cnt - 1]->lnum,
			 c->dirty_idx.arr[c->dirty_idx.cnt - 1]->dirty,
			 c->dirty_idx.arr[c->dirty_idx.cnt - 1]->free);
	/* Replace the lprops pointers with LEB numbers */
	for (i = 0; i < c->dirty_idx.cnt; i++)
		c->dirty_idx.arr[i] = (void *)(size_t)c->dirty_idx.arr[i]->lnum;
	ubifs_release_lprops(c);
	return 0;
}

/**
 * scan_dirty_idx_cb - callback used by the scan for a dirty index LEB.
 * @c: the UBIFS file-system description object
 * @lprops: LEB properties to scan
 * @in_tree: whether the LEB properties are in main memory
 * @data: information passed to and from the caller of the scan
 *
 * This function returns a code that indicates whether the scan should continue
 * (%LPT_SCAN_CONTINUE), whether the LEB properties should be added to the tree
 * in main memory (%LPT_SCAN_ADD), or whether the scan should stop
 * (%LPT_SCAN_STOP).
 */
static int scan_dirty_idx_cb(struct ubifs_info *c,
			   const struct ubifs_lprops *lprops, int in_tree,
			   struct scan_data *data)
{
	int ret = LPT_SCAN_CONTINUE;

	/* Exclude LEBs that are currently in use */
	if (lprops->flags & LPROPS_TAKEN)
		return LPT_SCAN_CONTINUE;
	/* Determine whether to add these LEB properties to the tree */
	if (!in_tree && valuable(c, lprops))
		ret |= LPT_SCAN_ADD;
	/* Exclude non-index LEBs */
	if (!(lprops->flags & LPROPS_INDEX))
		return ret;
	/* Exclude LEBs with too little space */
	if (lprops->free + lprops->dirty < c->min_idx_node_sz)
		return ret;
	/* Finally we found space */
	data->lnum = lprops->lnum;
	return LPT_SCAN_ADD | LPT_SCAN_STOP;
}

/**
 * find_dirty_idx_leb - find a dirty index LEB.
 * @c: the UBIFS file-system description object
 *
 * This function returns LEB number upon success and a negative error code upon
 * failure.  In particular, -ENOSPC is returned if a dirty index LEB is not
 * found.
 *
 * Note that this function scans the entire LPT but it is called very rarely.
 */
static int find_dirty_idx_leb(struct ubifs_info *c)
{
	const struct ubifs_lprops *lprops;
	struct ubifs_lpt_heap *heap;
	struct scan_data data;
	int err, i, ret;

	/* Check all structures in memory first */
	data.lnum = -1;
	heap = &c->lpt_heap[LPROPS_DIRTY_IDX - 1];
	for (i = 0; i < heap->cnt; i++) {
		lprops = heap->arr[i];
		ret = scan_dirty_idx_cb(c, lprops, 1, &data);
		if (ret & LPT_SCAN_STOP)
			goto found;
	}
	list_for_each_entry(lprops, &c->frdi_idx_list, list) {
		ret = scan_dirty_idx_cb(c, lprops, 1, &data);
		if (ret & LPT_SCAN_STOP)
			goto found;
	}
	list_for_each_entry(lprops, &c->uncat_list, list) {
		ret = scan_dirty_idx_cb(c, lprops, 1, &data);
		if (ret & LPT_SCAN_STOP)
			goto found;
	}
	if (c->pnodes_have >= c->pnode_cnt)
		/* All pnodes are in memory, so skip scan */
		return -ENOSPC;
	err = ubifs_lpt_scan_nolock(c, -1, c->lscan_lnum,
				    (ubifs_lpt_scan_callback)scan_dirty_idx_cb,
				    &data);
	if (err)
		return err;
found:
	ubifs_assert(c, data.lnum >= c->main_first && data.lnum < c->leb_cnt);
	c->lscan_lnum = data.lnum;
	lprops = ubifs_lpt_lookup_dirty(c, data.lnum);
	if (IS_ERR(lprops))
		return PTR_ERR(lprops);
	ubifs_assert(c, lprops->lnum == data.lnum);
	ubifs_assert(c, lprops->free + lprops->dirty >= c->min_idx_node_sz);
	ubifs_assert(c, !(lprops->flags & LPROPS_TAKEN));
	ubifs_assert(c, (lprops->flags & LPROPS_INDEX));

	dbg_find("found dirty LEB %d, free %d, dirty %d, flags %#x",
		 lprops->lnum, lprops->free, lprops->dirty, lprops->flags);

	lprops = ubifs_change_lp(c, lprops, LPROPS_NC, LPROPS_NC,
				 lprops->flags | LPROPS_TAKEN, 0);
	if (IS_ERR(lprops))
		return PTR_ERR(lprops);

	return lprops->lnum;
}

/**
 * get_idx_gc_leb - try to get a LEB number from trivial GC.
 * @c: the UBIFS file-system description object
 */
static int get_idx_gc_leb(struct ubifs_info *c)
{
	const struct ubifs_lprops *lp;
	int err, lnum;

	err = ubifs_get_idx_gc_leb(c);
	if (err < 0)
		return err;
	lnum = err;
	/*
	 * The LEB was due to be unmapped after the commit but
	 * it is needed now for this commit.
	 */
	lp = ubifs_lpt_lookup_dirty(c, lnum);
	if (IS_ERR(lp))
		return PTR_ERR(lp);
	lp = ubifs_change_lp(c, lp, LPROPS_NC, LPROPS_NC,
			     lp->flags | LPROPS_INDEX, -1);
	if (IS_ERR(lp))
		return PTR_ERR(lp);
	dbg_find("LEB %d, dirty %d and free %d flags %#x",
		 lp->lnum, lp->dirty, lp->free, lp->flags);
	return lnum;
}

/**
 * find_dirtiest_idx_leb - find dirtiest index LEB from dirtiest array.
 * @c: the UBIFS file-system description object
 */
static int find_dirtiest_idx_leb(struct ubifs_info *c)
{
	const struct ubifs_lprops *lp;
	int lnum;

	while (1) {
		if (!c->dirty_idx.cnt)
			return -ENOSPC;
		/* The lprops pointers were replaced by LEB numbers */
		lnum = (size_t)c->dirty_idx.arr[--c->dirty_idx.cnt];
		lp = ubifs_lpt_lookup(c, lnum);
		if (IS_ERR(lp))
			return PTR_ERR(lp);
		if ((lp->flags & LPROPS_TAKEN) || !(lp->flags & LPROPS_INDEX))
			continue;
		lp = ubifs_change_lp(c, lp, LPROPS_NC, LPROPS_NC,
				     lp->flags | LPROPS_TAKEN, 0);
		if (IS_ERR(lp))
			return PTR_ERR(lp);
		break;
	}
	dbg_find("LEB %d, dirty %d and free %d flags %#x", lp->lnum, lp->dirty,
		 lp->free, lp->flags);
	ubifs_assert(c, lp->flags & LPROPS_TAKEN);
	ubifs_assert(c, lp->flags & LPROPS_INDEX);
	return lnum;
}

/**
 * ubifs_find_dirty_idx_leb - try to find dirtiest index LEB as at last commit.
 * @c: the UBIFS file-system description object
 *
 * This function attempts to find an untaken index LEB with the most free and
 * dirty space that can be used without overwriting index nodes that were in the
 * last index committed.
 */
int ubifs_find_dirty_idx_leb(struct ubifs_info *c)
{
	int err;

	ubifs_get_lprops(c);

	/*
	 * We made an array of the dirtiest index LEB numbers as at the start of
	 * last commit.  Try that array first.
	 */
	err = find_dirtiest_idx_leb(c);

	/* Next try scanning the entire LPT */
	if (err == -ENOSPC)
		err = find_dirty_idx_leb(c);

	/* Finally take any index LEBs awaiting trivial GC */
	if (err == -ENOSPC)
		err = get_idx_gc_leb(c);

	ubifs_release_lprops(c);
	return err;
}
