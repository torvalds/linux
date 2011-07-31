/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * reservations.c
 *
 * Allocation reservations implementation
 *
 * Some code borrowed from fs/ext3/balloc.c and is:
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 * The rest is copyright (C) 2010 Novell.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/bitops.h>
#include <linux/list.h>

#define MLOG_MASK_PREFIX ML_RESERVATIONS
#include <cluster/masklog.h>

#include "ocfs2.h"

#ifdef CONFIG_OCFS2_DEBUG_FS
#define OCFS2_CHECK_RESERVATIONS
#endif

DEFINE_SPINLOCK(resv_lock);

#define	OCFS2_MIN_RESV_WINDOW_BITS	8
#define	OCFS2_MAX_RESV_WINDOW_BITS	1024

int ocfs2_dir_resv_allowed(struct ocfs2_super *osb)
{
	return (osb->osb_resv_level && osb->osb_dir_resv_level);
}

static unsigned int ocfs2_resv_window_bits(struct ocfs2_reservation_map *resmap,
					   struct ocfs2_alloc_reservation *resv)
{
	struct ocfs2_super *osb = resmap->m_osb;
	unsigned int bits;

	if (!(resv->r_flags & OCFS2_RESV_FLAG_DIR)) {
		/* 8, 16, 32, 64, 128, 256, 512, 1024 */
		bits = 4 << osb->osb_resv_level;
	} else {
		bits = 4 << osb->osb_dir_resv_level;
	}
	return bits;
}

static inline unsigned int ocfs2_resv_end(struct ocfs2_alloc_reservation *resv)
{
	if (resv->r_len)
		return resv->r_start + resv->r_len - 1;
	return resv->r_start;
}

static inline int ocfs2_resv_empty(struct ocfs2_alloc_reservation *resv)
{
	return !!(resv->r_len == 0);
}

static inline int ocfs2_resmap_disabled(struct ocfs2_reservation_map *resmap)
{
	if (resmap->m_osb->osb_resv_level == 0)
		return 1;
	return 0;
}

static void ocfs2_dump_resv(struct ocfs2_reservation_map *resmap)
{
	struct ocfs2_super *osb = resmap->m_osb;
	struct rb_node *node;
	struct ocfs2_alloc_reservation *resv;
	int i = 0;

	mlog(ML_NOTICE, "Dumping resmap for device %s. Bitmap length: %u\n",
	     osb->dev_str, resmap->m_bitmap_len);

	node = rb_first(&resmap->m_reservations);
	while (node) {
		resv = rb_entry(node, struct ocfs2_alloc_reservation, r_node);

		mlog(ML_NOTICE, "start: %u\tend: %u\tlen: %u\tlast_start: %u"
		     "\tlast_len: %u\n", resv->r_start,
		     ocfs2_resv_end(resv), resv->r_len, resv->r_last_start,
		     resv->r_last_len);

		node = rb_next(node);
		i++;
	}

	mlog(ML_NOTICE, "%d reservations found. LRU follows\n", i);

	i = 0;
	list_for_each_entry(resv, &resmap->m_lru, r_lru) {
		mlog(ML_NOTICE, "LRU(%d) start: %u\tend: %u\tlen: %u\t"
		     "last_start: %u\tlast_len: %u\n", i, resv->r_start,
		     ocfs2_resv_end(resv), resv->r_len, resv->r_last_start,
		     resv->r_last_len);

		i++;
	}
}

#ifdef OCFS2_CHECK_RESERVATIONS
static int ocfs2_validate_resmap_bits(struct ocfs2_reservation_map *resmap,
				      int i,
				      struct ocfs2_alloc_reservation *resv)
{
	char *disk_bitmap = resmap->m_disk_bitmap;
	unsigned int start = resv->r_start;
	unsigned int end = ocfs2_resv_end(resv);

	while (start <= end) {
		if (ocfs2_test_bit(start, disk_bitmap)) {
			mlog(ML_ERROR,
			     "reservation %d covers an allocated area "
			     "starting at bit %u!\n", i, start);
			return 1;
		}

		start++;
	}
	return 0;
}

static void ocfs2_check_resmap(struct ocfs2_reservation_map *resmap)
{
	unsigned int off = 0;
	int i = 0;
	struct rb_node *node;
	struct ocfs2_alloc_reservation *resv;

	node = rb_first(&resmap->m_reservations);
	while (node) {
		resv = rb_entry(node, struct ocfs2_alloc_reservation, r_node);

		if (i > 0 && resv->r_start <= off) {
			mlog(ML_ERROR, "reservation %d has bad start off!\n",
			     i);
			goto bad;
		}

		if (resv->r_len == 0) {
			mlog(ML_ERROR, "reservation %d has no length!\n",
			     i);
			goto bad;
		}

		if (resv->r_start > ocfs2_resv_end(resv)) {
			mlog(ML_ERROR, "reservation %d has invalid range!\n",
			     i);
			goto bad;
		}

		if (ocfs2_resv_end(resv) >= resmap->m_bitmap_len) {
			mlog(ML_ERROR, "reservation %d extends past bitmap!\n",
			     i);
			goto bad;
		}

		if (ocfs2_validate_resmap_bits(resmap, i, resv))
			goto bad;

		off = ocfs2_resv_end(resv);
		node = rb_next(node);

		i++;
	}
	return;

bad:
	ocfs2_dump_resv(resmap);
	BUG();
}
#else
static inline void ocfs2_check_resmap(struct ocfs2_reservation_map *resmap)
{

}
#endif

void ocfs2_resv_init_once(struct ocfs2_alloc_reservation *resv)
{
	memset(resv, 0, sizeof(*resv));
	INIT_LIST_HEAD(&resv->r_lru);
}

void ocfs2_resv_set_type(struct ocfs2_alloc_reservation *resv,
			 unsigned int flags)
{
	BUG_ON(flags & ~OCFS2_RESV_TYPES);

	resv->r_flags |= flags;
}

int ocfs2_resmap_init(struct ocfs2_super *osb,
		      struct ocfs2_reservation_map *resmap)
{
	memset(resmap, 0, sizeof(*resmap));

	resmap->m_osb = osb;
	resmap->m_reservations = RB_ROOT;
	/* m_bitmap_len is initialized to zero by the above memset. */
	INIT_LIST_HEAD(&resmap->m_lru);

	return 0;
}

static void ocfs2_resv_mark_lru(struct ocfs2_reservation_map *resmap,
				struct ocfs2_alloc_reservation *resv)
{
	assert_spin_locked(&resv_lock);

	if (!list_empty(&resv->r_lru))
		list_del_init(&resv->r_lru);

	list_add_tail(&resv->r_lru, &resmap->m_lru);
}

static void __ocfs2_resv_trunc(struct ocfs2_alloc_reservation *resv)
{
	resv->r_len = 0;
	resv->r_start = 0;
}

static void ocfs2_resv_remove(struct ocfs2_reservation_map *resmap,
			      struct ocfs2_alloc_reservation *resv)
{
	if (resv->r_flags & OCFS2_RESV_FLAG_INUSE) {
		list_del_init(&resv->r_lru);
		rb_erase(&resv->r_node, &resmap->m_reservations);
		resv->r_flags &= ~OCFS2_RESV_FLAG_INUSE;
	}
}

static void __ocfs2_resv_discard(struct ocfs2_reservation_map *resmap,
				 struct ocfs2_alloc_reservation *resv)
{
	assert_spin_locked(&resv_lock);

	__ocfs2_resv_trunc(resv);
	/*
	 * last_len and last_start no longer make sense if
	 * we're changing the range of our allocations.
	 */
	resv->r_last_len = resv->r_last_start = 0;

	ocfs2_resv_remove(resmap, resv);
}

/* does nothing if 'resv' is null */
void ocfs2_resv_discard(struct ocfs2_reservation_map *resmap,
			struct ocfs2_alloc_reservation *resv)
{
	if (resv) {
		spin_lock(&resv_lock);
		__ocfs2_resv_discard(resmap, resv);
		spin_unlock(&resv_lock);
	}
}

static void ocfs2_resmap_clear_all_resv(struct ocfs2_reservation_map *resmap)
{
	struct rb_node *node;
	struct ocfs2_alloc_reservation *resv;

	assert_spin_locked(&resv_lock);

	while ((node = rb_last(&resmap->m_reservations)) != NULL) {
		resv = rb_entry(node, struct ocfs2_alloc_reservation, r_node);

		__ocfs2_resv_discard(resmap, resv);
	}
}

void ocfs2_resmap_restart(struct ocfs2_reservation_map *resmap,
			  unsigned int clen, char *disk_bitmap)
{
	if (ocfs2_resmap_disabled(resmap))
		return;

	spin_lock(&resv_lock);

	ocfs2_resmap_clear_all_resv(resmap);
	resmap->m_bitmap_len = clen;
	resmap->m_disk_bitmap = disk_bitmap;

	spin_unlock(&resv_lock);
}

void ocfs2_resmap_uninit(struct ocfs2_reservation_map *resmap)
{
	/* Does nothing for now. Keep this around for API symmetry */
}

static void ocfs2_resv_insert(struct ocfs2_reservation_map *resmap,
			      struct ocfs2_alloc_reservation *new)
{
	struct rb_root *root = &resmap->m_reservations;
	struct rb_node *parent = NULL;
	struct rb_node **p = &root->rb_node;
	struct ocfs2_alloc_reservation *tmp;

	assert_spin_locked(&resv_lock);

	mlog(0, "Insert reservation start: %u len: %u\n", new->r_start,
	     new->r_len);

	while (*p) {
		parent = *p;

		tmp = rb_entry(parent, struct ocfs2_alloc_reservation, r_node);

		if (new->r_start < tmp->r_start) {
			p = &(*p)->rb_left;

			/*
			 * This is a good place to check for
			 * overlapping reservations.
			 */
			BUG_ON(ocfs2_resv_end(new) >= tmp->r_start);
		} else if (new->r_start > ocfs2_resv_end(tmp)) {
			p = &(*p)->rb_right;
		} else {
			/* This should never happen! */
			mlog(ML_ERROR, "Duplicate reservation window!\n");
			BUG();
		}
	}

	rb_link_node(&new->r_node, parent, p);
	rb_insert_color(&new->r_node, root);
	new->r_flags |= OCFS2_RESV_FLAG_INUSE;

	ocfs2_resv_mark_lru(resmap, new);

	ocfs2_check_resmap(resmap);
}

/**
 * ocfs2_find_resv_lhs() - find the window which contains goal
 * @resmap: reservation map to search
 * @goal: which bit to search for
 *
 * If a window containing that goal is not found, we return the window
 * which comes before goal. Returns NULL on empty rbtree or no window
 * before goal.
 */
static struct ocfs2_alloc_reservation *
ocfs2_find_resv_lhs(struct ocfs2_reservation_map *resmap, unsigned int goal)
{
	struct ocfs2_alloc_reservation *resv = NULL;
	struct ocfs2_alloc_reservation *prev_resv = NULL;
	struct rb_node *node = resmap->m_reservations.rb_node;

	assert_spin_locked(&resv_lock);

	if (!node)
		return NULL;

	node = rb_first(&resmap->m_reservations);
	while (node) {
		resv = rb_entry(node, struct ocfs2_alloc_reservation, r_node);

		if (resv->r_start <= goal && ocfs2_resv_end(resv) >= goal)
			break;

		/* Check if we overshot the reservation just before goal? */
		if (resv->r_start > goal) {
			resv = prev_resv;
			break;
		}

		prev_resv = resv;
		node = rb_next(node);
	}

	return resv;
}

/*
 * We are given a range within the bitmap, which corresponds to a gap
 * inside the reservations tree (search_start, search_len). The range
 * can be anything from the whole bitmap, to a gap between
 * reservations.
 *
 * The start value of *rstart is insignificant.
 *
 * This function searches the bitmap range starting at search_start
 * with length search_len for a set of contiguous free bits. We try
 * to find up to 'wanted' bits, but can sometimes return less.
 *
 * Returns the length of allocation, 0 if no free bits are found.
 *
 * *cstart and *clen will also be populated with the result.
 */
static int ocfs2_resmap_find_free_bits(struct ocfs2_reservation_map *resmap,
				       unsigned int wanted,
				       unsigned int search_start,
				       unsigned int search_len,
				       unsigned int *rstart,
				       unsigned int *rlen)
{
	void *bitmap = resmap->m_disk_bitmap;
	unsigned int best_start, best_len = 0;
	int offset, start, found;

	mlog(0, "Find %u bits within range (%u, len %u) resmap len: %u\n",
	     wanted, search_start, search_len, resmap->m_bitmap_len);

	found = best_start = best_len = 0;

	start = search_start;
	while ((offset = ocfs2_find_next_zero_bit(bitmap, resmap->m_bitmap_len,
						 start)) != -1) {
		/* Search reached end of the region */
		if (offset >= (search_start + search_len))
			break;

		if (offset == start) {
			/* we found a zero */
			found++;
			/* move start to the next bit to test */
			start++;
		} else {
			/* got a zero after some ones */
			found = 1;
			start = offset + 1;
		}
		if (found > best_len) {
			best_len = found;
			best_start = start - found;
		}

		if (found >= wanted)
			break;
	}

	if (best_len == 0)
		return 0;

	if (best_len >= wanted)
		best_len = wanted;

	*rlen = best_len;
	*rstart = best_start;

	mlog(0, "Found start: %u len: %u\n", best_start, best_len);

	return *rlen;
}

static void __ocfs2_resv_find_window(struct ocfs2_reservation_map *resmap,
				     struct ocfs2_alloc_reservation *resv,
				     unsigned int goal, unsigned int wanted)
{
	struct rb_root *root = &resmap->m_reservations;
	unsigned int gap_start, gap_end, gap_len;
	struct ocfs2_alloc_reservation *prev_resv, *next_resv;
	struct rb_node *prev, *next;
	unsigned int cstart, clen;
	unsigned int best_start = 0, best_len = 0;

	/*
	 * Nasty cases to consider:
	 *
	 * - rbtree is empty
	 * - our window should be first in all reservations
	 * - our window should be last in all reservations
	 * - need to make sure we don't go past end of bitmap
	 */

	mlog(0, "resv start: %u resv end: %u goal: %u wanted: %u\n",
	     resv->r_start, ocfs2_resv_end(resv), goal, wanted);

	assert_spin_locked(&resv_lock);

	if (RB_EMPTY_ROOT(root)) {
		/*
		 * Easiest case - empty tree. We can just take
		 * whatever window of free bits we want.
		 */

		mlog(0, "Empty root\n");

		clen = ocfs2_resmap_find_free_bits(resmap, wanted, goal,
						   resmap->m_bitmap_len - goal,
						   &cstart, &clen);

		/*
		 * This should never happen - the local alloc window
		 * will always have free bits when we're called.
		 */
		BUG_ON(goal == 0 && clen == 0);

		if (clen == 0)
			return;

		resv->r_start = cstart;
		resv->r_len = clen;

		ocfs2_resv_insert(resmap, resv);
		return;
	}

	prev_resv = ocfs2_find_resv_lhs(resmap, goal);

	if (prev_resv == NULL) {
		mlog(0, "Goal on LHS of leftmost window\n");

		/*
		 * A NULL here means that the search code couldn't
		 * find a window that starts before goal.
		 *
		 * However, we can take the first window after goal,
		 * which is also by definition, the leftmost window in
		 * the entire tree. If we can find free bits in the
		 * gap between goal and the LHS window, then the
		 * reservation can safely be placed there.
		 *
		 * Otherwise we fall back to a linear search, checking
		 * the gaps in between windows for a place to
		 * allocate.
		 */

		next = rb_first(root);
		next_resv = rb_entry(next, struct ocfs2_alloc_reservation,
				     r_node);

		/*
		 * The search should never return such a window. (see
		 * comment above
		 */
		if (next_resv->r_start <= goal) {
			mlog(ML_ERROR, "goal: %u next_resv: start %u len %u\n",
			     goal, next_resv->r_start, next_resv->r_len);
			ocfs2_dump_resv(resmap);
			BUG();
		}

		clen = ocfs2_resmap_find_free_bits(resmap, wanted, goal,
						   next_resv->r_start - goal,
						   &cstart, &clen);
		if (clen) {
			best_len = clen;
			best_start = cstart;
			if (best_len == wanted)
				goto out_insert;
		}

		prev_resv = next_resv;
		next_resv = NULL;
	}

	prev = &prev_resv->r_node;

	/* Now we do a linear search for a window, starting at 'prev_rsv' */
	while (1) {
		next = rb_next(prev);
		if (next) {
			mlog(0, "One more resv found in linear search\n");
			next_resv = rb_entry(next,
					     struct ocfs2_alloc_reservation,
					     r_node);

			gap_start = ocfs2_resv_end(prev_resv) + 1;
			gap_end = next_resv->r_start - 1;
			gap_len = gap_end - gap_start + 1;
		} else {
			mlog(0, "No next node\n");
			/*
			 * We're at the rightmost edge of the
			 * tree. See if a reservation between this
			 * window and the end of the bitmap will work.
			 */
			gap_start = ocfs2_resv_end(prev_resv) + 1;
			gap_len = resmap->m_bitmap_len - gap_start;
			gap_end = resmap->m_bitmap_len - 1;
		}

		/*
		 * No need to check this gap if we have already found
		 * a larger region of free bits.
		 */
		if (gap_len <= best_len)
			goto next_resv;

		clen = ocfs2_resmap_find_free_bits(resmap, wanted, gap_start,
						   gap_len, &cstart, &clen);
		if (clen == wanted) {
			best_len = clen;
			best_start = cstart;
			goto out_insert;
		} else if (clen > best_len) {
			best_len = clen;
			best_start = cstart;
		}

next_resv:
		if (!next)
			break;

		prev = next;
		prev_resv = rb_entry(prev, struct ocfs2_alloc_reservation,
				     r_node);
	}

out_insert:
	if (best_len) {
		resv->r_start = best_start;
		resv->r_len = best_len;
		ocfs2_resv_insert(resmap, resv);
	}
}

static void ocfs2_cannibalize_resv(struct ocfs2_reservation_map *resmap,
				   struct ocfs2_alloc_reservation *resv,
				   unsigned int wanted)
{
	struct ocfs2_alloc_reservation *lru_resv;
	int tmpwindow = !!(resv->r_flags & OCFS2_RESV_FLAG_TMP);
	unsigned int min_bits;

	if (!tmpwindow)
		min_bits = ocfs2_resv_window_bits(resmap, resv) >> 1;
	else
		min_bits = wanted; /* We at know the temp window will use all
				    * of these bits */

	/*
	 * Take the first reservation off the LRU as our 'target'. We
	 * don't try to be smart about it. There might be a case for
	 * searching based on size but I don't have enough data to be
	 * sure. --Mark (3/16/2010)
	 */
	lru_resv = list_first_entry(&resmap->m_lru,
				    struct ocfs2_alloc_reservation, r_lru);

	mlog(0, "lru resv: start: %u len: %u end: %u\n", lru_resv->r_start,
	     lru_resv->r_len, ocfs2_resv_end(lru_resv));

	/*
	 * Cannibalize (some or all) of the target reservation and
	 * feed it to the current window.
	 */
	if (lru_resv->r_len <= min_bits) {
		/*
		 * Discard completely if size is less than or equal to a
		 * reasonable threshold - 50% of window bits for non temporary
		 * windows.
		 */
		resv->r_start = lru_resv->r_start;
		resv->r_len = lru_resv->r_len;

		__ocfs2_resv_discard(resmap, lru_resv);
	} else {
		unsigned int shrink;
		if (tmpwindow)
			shrink = min_bits;
		else
			shrink = lru_resv->r_len / 2;

		lru_resv->r_len -= shrink;

		resv->r_start = ocfs2_resv_end(lru_resv) + 1;
		resv->r_len = shrink;
	}

	mlog(0, "Reservation now looks like: r_start: %u r_end: %u "
	     "r_len: %u r_last_start: %u r_last_len: %u\n",
	     resv->r_start, ocfs2_resv_end(resv), resv->r_len,
	     resv->r_last_start, resv->r_last_len);

	ocfs2_resv_insert(resmap, resv);
}

static void ocfs2_resv_find_window(struct ocfs2_reservation_map *resmap,
				   struct ocfs2_alloc_reservation *resv,
				   unsigned int wanted)
{
	unsigned int goal = 0;

	BUG_ON(!ocfs2_resv_empty(resv));

	/*
	 * Begin by trying to get a window as close to the previous
	 * one as possible. Using the most recent allocation as a
	 * start goal makes sense.
	 */
	if (resv->r_last_len) {
		goal = resv->r_last_start + resv->r_last_len;
		if (goal >= resmap->m_bitmap_len)
			goal = 0;
	}

	__ocfs2_resv_find_window(resmap, resv, goal, wanted);

	/* Search from last alloc didn't work, try once more from beginning. */
	if (ocfs2_resv_empty(resv) && goal != 0)
		__ocfs2_resv_find_window(resmap, resv, 0, wanted);

	if (ocfs2_resv_empty(resv)) {
		/*
		 * Still empty? Pull oldest one off the LRU, remove it from
		 * tree, put this one in it's place.
		 */
		ocfs2_cannibalize_resv(resmap, resv, wanted);
	}

	BUG_ON(ocfs2_resv_empty(resv));
}

int ocfs2_resmap_resv_bits(struct ocfs2_reservation_map *resmap,
			   struct ocfs2_alloc_reservation *resv,
			   int *cstart, int *clen)
{
	if (resv == NULL || ocfs2_resmap_disabled(resmap))
		return -ENOSPC;

	spin_lock(&resv_lock);

	if (ocfs2_resv_empty(resv)) {
		/*
		 * We don't want to over-allocate for temporary
		 * windows. Otherwise, we run the risk of fragmenting the
		 * allocation space.
		 */
		unsigned int wanted = ocfs2_resv_window_bits(resmap, resv);

		if ((resv->r_flags & OCFS2_RESV_FLAG_TMP) || wanted < *clen)
			wanted = *clen;

		mlog(0, "empty reservation, find new window\n");
		/*
		 * Try to get a window here. If it works, we must fall
		 * through and test the bitmap . This avoids some
		 * ping-ponging of windows due to non-reserved space
		 * being allocation before we initialize a window for
		 * that inode.
		 */
		ocfs2_resv_find_window(resmap, resv, wanted);
	}

	BUG_ON(ocfs2_resv_empty(resv));

	*cstart = resv->r_start;
	*clen = resv->r_len;

	spin_unlock(&resv_lock);
	return 0;
}

static void
	ocfs2_adjust_resv_from_alloc(struct ocfs2_reservation_map *resmap,
				     struct ocfs2_alloc_reservation *resv,
				     unsigned int start, unsigned int end)
{
	unsigned int rhs = 0;
	unsigned int old_end = ocfs2_resv_end(resv);

	BUG_ON(start != resv->r_start || old_end < end);

	/*
	 * Completely used? We can remove it then.
	 */
	if (old_end == end) {
		__ocfs2_resv_discard(resmap, resv);
		return;
	}

	rhs = old_end - end;

	/*
	 * This should have been trapped above.
	 */
	BUG_ON(rhs == 0);

	resv->r_start = end + 1;
	resv->r_len = old_end - resv->r_start + 1;
}

void ocfs2_resmap_claimed_bits(struct ocfs2_reservation_map *resmap,
			       struct ocfs2_alloc_reservation *resv,
			       u32 cstart, u32 clen)
{
	unsigned int cend = cstart + clen - 1;

	if (resmap == NULL || ocfs2_resmap_disabled(resmap))
		return;

	if (resv == NULL)
		return;

	BUG_ON(cstart != resv->r_start);

	spin_lock(&resv_lock);

	mlog(0, "claim bits: cstart: %u cend: %u clen: %u r_start: %u "
	     "r_end: %u r_len: %u, r_last_start: %u r_last_len: %u\n",
	     cstart, cend, clen, resv->r_start, ocfs2_resv_end(resv),
	     resv->r_len, resv->r_last_start, resv->r_last_len);

	BUG_ON(cstart < resv->r_start);
	BUG_ON(cstart > ocfs2_resv_end(resv));
	BUG_ON(cend > ocfs2_resv_end(resv));

	ocfs2_adjust_resv_from_alloc(resmap, resv, cstart, cend);
	resv->r_last_start = cstart;
	resv->r_last_len = clen;

	/*
	 * May have been discarded above from
	 * ocfs2_adjust_resv_from_alloc().
	 */
	if (!ocfs2_resv_empty(resv))
		ocfs2_resv_mark_lru(resmap, resv);

	mlog(0, "Reservation now looks like: r_start: %u r_end: %u "
	     "r_len: %u r_last_start: %u r_last_len: %u\n",
	     resv->r_start, ocfs2_resv_end(resv), resv->r_len,
	     resv->r_last_start, resv->r_last_len);

	ocfs2_check_resmap(resmap);

	spin_unlock(&resv_lock);
}
