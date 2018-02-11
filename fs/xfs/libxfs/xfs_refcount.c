/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_refcount_btree.h"
#include "xfs_alloc.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_bit.h"
#include "xfs_refcount.h"
#include "xfs_rmap.h"

/* Allowable refcount adjustment amounts. */
enum xfs_refc_adjust_op {
	XFS_REFCOUNT_ADJUST_INCREASE	= 1,
	XFS_REFCOUNT_ADJUST_DECREASE	= -1,
	XFS_REFCOUNT_ADJUST_COW_ALLOC	= 0,
	XFS_REFCOUNT_ADJUST_COW_FREE	= -1,
};

STATIC int __xfs_refcount_cow_alloc(struct xfs_btree_cur *rcur,
		xfs_agblock_t agbno, xfs_extlen_t aglen,
		struct xfs_defer_ops *dfops);
STATIC int __xfs_refcount_cow_free(struct xfs_btree_cur *rcur,
		xfs_agblock_t agbno, xfs_extlen_t aglen,
		struct xfs_defer_ops *dfops);

/*
 * Look up the first record less than or equal to [bno, len] in the btree
 * given by cur.
 */
int
xfs_refcount_lookup_le(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	int			*stat)
{
	trace_xfs_refcount_lookup(cur->bc_mp, cur->bc_private.a.agno, bno,
			XFS_LOOKUP_LE);
	cur->bc_rec.rc.rc_startblock = bno;
	cur->bc_rec.rc.rc_blockcount = 0;
	return xfs_btree_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Look up the first record greater than or equal to [bno, len] in the btree
 * given by cur.
 */
int
xfs_refcount_lookup_ge(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	int			*stat)
{
	trace_xfs_refcount_lookup(cur->bc_mp, cur->bc_private.a.agno, bno,
			XFS_LOOKUP_GE);
	cur->bc_rec.rc.rc_startblock = bno;
	cur->bc_rec.rc.rc_blockcount = 0;
	return xfs_btree_lookup(cur, XFS_LOOKUP_GE, stat);
}

/* Convert on-disk record to in-core format. */
static inline void
xfs_refcount_btrec_to_irec(
	union xfs_btree_rec		*rec,
	struct xfs_refcount_irec	*irec)
{
	irec->rc_startblock = be32_to_cpu(rec->refc.rc_startblock);
	irec->rc_blockcount = be32_to_cpu(rec->refc.rc_blockcount);
	irec->rc_refcount = be32_to_cpu(rec->refc.rc_refcount);
}

/*
 * Get the data from the pointed-to record.
 */
int
xfs_refcount_get_rec(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*irec,
	int				*stat)
{
	union xfs_btree_rec		*rec;
	int				error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (!error && *stat == 1) {
		xfs_refcount_btrec_to_irec(rec, irec);
		trace_xfs_refcount_get(cur->bc_mp, cur->bc_private.a.agno,
				irec);
	}
	return error;
}

/*
 * Update the record referred to by cur to the value given
 * by [bno, len, refcount].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_refcount_update(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*irec)
{
	union xfs_btree_rec	rec;
	int			error;

	trace_xfs_refcount_update(cur->bc_mp, cur->bc_private.a.agno, irec);
	rec.refc.rc_startblock = cpu_to_be32(irec->rc_startblock);
	rec.refc.rc_blockcount = cpu_to_be32(irec->rc_blockcount);
	rec.refc.rc_refcount = cpu_to_be32(irec->rc_refcount);
	error = xfs_btree_update(cur, &rec);
	if (error)
		trace_xfs_refcount_update_error(cur->bc_mp,
				cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Insert the record referred to by cur to the value given
 * by [bno, len, refcount].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_refcount_insert(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*irec,
	int				*i)
{
	int				error;

	trace_xfs_refcount_insert(cur->bc_mp, cur->bc_private.a.agno, irec);
	cur->bc_rec.rc.rc_startblock = irec->rc_startblock;
	cur->bc_rec.rc.rc_blockcount = irec->rc_blockcount;
	cur->bc_rec.rc.rc_refcount = irec->rc_refcount;
	error = xfs_btree_insert(cur, i);
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, *i == 1, out_error);
out_error:
	if (error)
		trace_xfs_refcount_insert_error(cur->bc_mp,
				cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Remove the record referred to by cur, then set the pointer to the spot
 * where the record could be re-inserted, in case we want to increment or
 * decrement the cursor.
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_refcount_delete(
	struct xfs_btree_cur	*cur,
	int			*i)
{
	struct xfs_refcount_irec	irec;
	int			found_rec;
	int			error;

	error = xfs_refcount_get_rec(cur, &irec, &found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);
	trace_xfs_refcount_delete(cur->bc_mp, cur->bc_private.a.agno, &irec);
	error = xfs_btree_delete(cur, i);
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, *i == 1, out_error);
	if (error)
		goto out_error;
	error = xfs_refcount_lookup_ge(cur, irec.rc_startblock, &found_rec);
out_error:
	if (error)
		trace_xfs_refcount_delete_error(cur->bc_mp,
				cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Adjusting the Reference Count
 *
 * As stated elsewhere, the reference count btree (refcbt) stores
 * >1 reference counts for extents of physical blocks.  In this
 * operation, we're either raising or lowering the reference count of
 * some subrange stored in the tree:
 *
 *      <------ adjustment range ------>
 * ----+   +---+-----+ +--+--------+---------
 *  2  |   | 3 |  4  | |17|   55   |   10
 * ----+   +---+-----+ +--+--------+---------
 * X axis is physical blocks number;
 * reference counts are the numbers inside the rectangles
 *
 * The first thing we need to do is to ensure that there are no
 * refcount extents crossing either boundary of the range to be
 * adjusted.  For any extent that does cross a boundary, split it into
 * two extents so that we can increment the refcount of one of the
 * pieces later:
 *
 *      <------ adjustment range ------>
 * ----+   +---+-----+ +--+--------+----+----
 *  2  |   | 3 |  2  | |17|   55   | 10 | 10
 * ----+   +---+-----+ +--+--------+----+----
 *
 * For this next step, let's assume that all the physical blocks in
 * the adjustment range are mapped to a file and are therefore in use
 * at least once.  Therefore, we can infer that any gap in the
 * refcount tree within the adjustment range represents a physical
 * extent with refcount == 1:
 *
 *      <------ adjustment range ------>
 * ----+---+---+-----+-+--+--------+----+----
 *  2  |"1"| 3 |  2  |1|17|   55   | 10 | 10
 * ----+---+---+-----+-+--+--------+----+----
 *      ^
 *
 * For each extent that falls within the interval range, figure out
 * which extent is to the left or the right of that extent.  Now we
 * have a left, current, and right extent.  If the new reference count
 * of the center extent enables us to merge left, center, and right
 * into one record covering all three, do so.  If the center extent is
 * at the left end of the range, abuts the left extent, and its new
 * reference count matches the left extent's record, then merge them.
 * If the center extent is at the right end of the range, abuts the
 * right extent, and the reference counts match, merge those.  In the
 * example, we can left merge (assuming an increment operation):
 *
 *      <------ adjustment range ------>
 * --------+---+-----+-+--+--------+----+----
 *    2    | 3 |  2  |1|17|   55   | 10 | 10
 * --------+---+-----+-+--+--------+----+----
 *          ^
 *
 * For all other extents within the range, adjust the reference count
 * or delete it if the refcount falls below 2.  If we were
 * incrementing, the end result looks like this:
 *
 *      <------ adjustment range ------>
 * --------+---+-----+-+--+--------+----+----
 *    2    | 4 |  3  |2|18|   56   | 11 | 10
 * --------+---+-----+-+--+--------+----+----
 *
 * The result of a decrement operation looks as such:
 *
 *      <------ adjustment range ------>
 * ----+   +---+       +--+--------+----+----
 *  2  |   | 2 |       |16|   54   |  9 | 10
 * ----+   +---+       +--+--------+----+----
 *      DDDD    111111DD
 *
 * The blocks marked "D" are freed; the blocks marked "1" are only
 * referenced once and therefore the record is removed from the
 * refcount btree.
 */

/* Next block after this extent. */
static inline xfs_agblock_t
xfs_refc_next(
	struct xfs_refcount_irec	*rc)
{
	return rc->rc_startblock + rc->rc_blockcount;
}

/*
 * Split a refcount extent that crosses agbno.
 */
STATIC int
xfs_refcount_split_extent(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			agbno,
	bool				*shape_changed)
{
	struct xfs_refcount_irec	rcext, tmp;
	int				found_rec;
	int				error;

	*shape_changed = false;
	error = xfs_refcount_lookup_le(cur, agbno, &found_rec);
	if (error)
		goto out_error;
	if (!found_rec)
		return 0;

	error = xfs_refcount_get_rec(cur, &rcext, &found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);
	if (rcext.rc_startblock == agbno || xfs_refc_next(&rcext) <= agbno)
		return 0;

	*shape_changed = true;
	trace_xfs_refcount_split_extent(cur->bc_mp, cur->bc_private.a.agno,
			&rcext, agbno);

	/* Establish the right extent. */
	tmp = rcext;
	tmp.rc_startblock = agbno;
	tmp.rc_blockcount -= (agbno - rcext.rc_startblock);
	error = xfs_refcount_update(cur, &tmp);
	if (error)
		goto out_error;

	/* Insert the left extent. */
	tmp = rcext;
	tmp.rc_blockcount = agbno - rcext.rc_startblock;
	error = xfs_refcount_insert(cur, &tmp, &found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);
	return error;

out_error:
	trace_xfs_refcount_split_extent_error(cur->bc_mp,
			cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Merge the left, center, and right extents.
 */
STATIC int
xfs_refcount_merge_center_extents(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*left,
	struct xfs_refcount_irec	*center,
	struct xfs_refcount_irec	*right,
	unsigned long long		extlen,
	xfs_agblock_t			*agbno,
	xfs_extlen_t			*aglen)
{
	int				error;
	int				found_rec;

	trace_xfs_refcount_merge_center_extents(cur->bc_mp,
			cur->bc_private.a.agno, left, center, right);

	/*
	 * Make sure the center and right extents are not in the btree.
	 * If the center extent was synthesized, the first delete call
	 * removes the right extent and we skip the second deletion.
	 * If center and right were in the btree, then the first delete
	 * call removes the center and the second one removes the right
	 * extent.
	 */
	error = xfs_refcount_lookup_ge(cur, center->rc_startblock,
			&found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);

	error = xfs_refcount_delete(cur, &found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);

	if (center->rc_refcount > 1) {
		error = xfs_refcount_delete(cur, &found_rec);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1,
				out_error);
	}

	/* Enlarge the left extent. */
	error = xfs_refcount_lookup_le(cur, left->rc_startblock,
			&found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);

	left->rc_blockcount = extlen;
	error = xfs_refcount_update(cur, left);
	if (error)
		goto out_error;

	*aglen = 0;
	return error;

out_error:
	trace_xfs_refcount_merge_center_extents_error(cur->bc_mp,
			cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Merge with the left extent.
 */
STATIC int
xfs_refcount_merge_left_extent(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*left,
	struct xfs_refcount_irec	*cleft,
	xfs_agblock_t			*agbno,
	xfs_extlen_t			*aglen)
{
	int				error;
	int				found_rec;

	trace_xfs_refcount_merge_left_extent(cur->bc_mp,
			cur->bc_private.a.agno, left, cleft);

	/* If the extent at agbno (cleft) wasn't synthesized, remove it. */
	if (cleft->rc_refcount > 1) {
		error = xfs_refcount_lookup_le(cur, cleft->rc_startblock,
				&found_rec);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1,
				out_error);

		error = xfs_refcount_delete(cur, &found_rec);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1,
				out_error);
	}

	/* Enlarge the left extent. */
	error = xfs_refcount_lookup_le(cur, left->rc_startblock,
			&found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);

	left->rc_blockcount += cleft->rc_blockcount;
	error = xfs_refcount_update(cur, left);
	if (error)
		goto out_error;

	*agbno += cleft->rc_blockcount;
	*aglen -= cleft->rc_blockcount;
	return error;

out_error:
	trace_xfs_refcount_merge_left_extent_error(cur->bc_mp,
			cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Merge with the right extent.
 */
STATIC int
xfs_refcount_merge_right_extent(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*right,
	struct xfs_refcount_irec	*cright,
	xfs_agblock_t			*agbno,
	xfs_extlen_t			*aglen)
{
	int				error;
	int				found_rec;

	trace_xfs_refcount_merge_right_extent(cur->bc_mp,
			cur->bc_private.a.agno, cright, right);

	/*
	 * If the extent ending at agbno+aglen (cright) wasn't synthesized,
	 * remove it.
	 */
	if (cright->rc_refcount > 1) {
		error = xfs_refcount_lookup_le(cur, cright->rc_startblock,
			&found_rec);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1,
				out_error);

		error = xfs_refcount_delete(cur, &found_rec);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1,
				out_error);
	}

	/* Enlarge the right extent. */
	error = xfs_refcount_lookup_le(cur, right->rc_startblock,
			&found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);

	right->rc_startblock -= cright->rc_blockcount;
	right->rc_blockcount += cright->rc_blockcount;
	error = xfs_refcount_update(cur, right);
	if (error)
		goto out_error;

	*aglen -= cright->rc_blockcount;
	return error;

out_error:
	trace_xfs_refcount_merge_right_extent_error(cur->bc_mp,
			cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

#define XFS_FIND_RCEXT_SHARED	1
#define XFS_FIND_RCEXT_COW	2
/*
 * Find the left extent and the one after it (cleft).  This function assumes
 * that we've already split any extent crossing agbno.
 */
STATIC int
xfs_refcount_find_left_extents(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*left,
	struct xfs_refcount_irec	*cleft,
	xfs_agblock_t			agbno,
	xfs_extlen_t			aglen,
	int				flags)
{
	struct xfs_refcount_irec	tmp;
	int				error;
	int				found_rec;

	left->rc_startblock = cleft->rc_startblock = NULLAGBLOCK;
	error = xfs_refcount_lookup_le(cur, agbno - 1, &found_rec);
	if (error)
		goto out_error;
	if (!found_rec)
		return 0;

	error = xfs_refcount_get_rec(cur, &tmp, &found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);

	if (xfs_refc_next(&tmp) != agbno)
		return 0;
	if ((flags & XFS_FIND_RCEXT_SHARED) && tmp.rc_refcount < 2)
		return 0;
	if ((flags & XFS_FIND_RCEXT_COW) && tmp.rc_refcount > 1)
		return 0;
	/* We have a left extent; retrieve (or invent) the next right one */
	*left = tmp;

	error = xfs_btree_increment(cur, 0, &found_rec);
	if (error)
		goto out_error;
	if (found_rec) {
		error = xfs_refcount_get_rec(cur, &tmp, &found_rec);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1,
				out_error);

		/* if tmp starts at the end of our range, just use that */
		if (tmp.rc_startblock == agbno)
			*cleft = tmp;
		else {
			/*
			 * There's a gap in the refcntbt at the start of the
			 * range we're interested in (refcount == 1) so
			 * synthesize the implied extent and pass it back.
			 * We assume here that the agbno/aglen range was
			 * passed in from a data fork extent mapping and
			 * therefore is allocated to exactly one owner.
			 */
			cleft->rc_startblock = agbno;
			cleft->rc_blockcount = min(aglen,
					tmp.rc_startblock - agbno);
			cleft->rc_refcount = 1;
		}
	} else {
		/*
		 * No extents, so pretend that there's one covering the whole
		 * range.
		 */
		cleft->rc_startblock = agbno;
		cleft->rc_blockcount = aglen;
		cleft->rc_refcount = 1;
	}
	trace_xfs_refcount_find_left_extent(cur->bc_mp, cur->bc_private.a.agno,
			left, cleft, agbno);
	return error;

out_error:
	trace_xfs_refcount_find_left_extent_error(cur->bc_mp,
			cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Find the right extent and the one before it (cright).  This function
 * assumes that we've already split any extents crossing agbno + aglen.
 */
STATIC int
xfs_refcount_find_right_extents(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*right,
	struct xfs_refcount_irec	*cright,
	xfs_agblock_t			agbno,
	xfs_extlen_t			aglen,
	int				flags)
{
	struct xfs_refcount_irec	tmp;
	int				error;
	int				found_rec;

	right->rc_startblock = cright->rc_startblock = NULLAGBLOCK;
	error = xfs_refcount_lookup_ge(cur, agbno + aglen, &found_rec);
	if (error)
		goto out_error;
	if (!found_rec)
		return 0;

	error = xfs_refcount_get_rec(cur, &tmp, &found_rec);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1, out_error);

	if (tmp.rc_startblock != agbno + aglen)
		return 0;
	if ((flags & XFS_FIND_RCEXT_SHARED) && tmp.rc_refcount < 2)
		return 0;
	if ((flags & XFS_FIND_RCEXT_COW) && tmp.rc_refcount > 1)
		return 0;
	/* We have a right extent; retrieve (or invent) the next left one */
	*right = tmp;

	error = xfs_btree_decrement(cur, 0, &found_rec);
	if (error)
		goto out_error;
	if (found_rec) {
		error = xfs_refcount_get_rec(cur, &tmp, &found_rec);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, found_rec == 1,
				out_error);

		/* if tmp ends at the end of our range, just use that */
		if (xfs_refc_next(&tmp) == agbno + aglen)
			*cright = tmp;
		else {
			/*
			 * There's a gap in the refcntbt at the end of the
			 * range we're interested in (refcount == 1) so
			 * create the implied extent and pass it back.
			 * We assume here that the agbno/aglen range was
			 * passed in from a data fork extent mapping and
			 * therefore is allocated to exactly one owner.
			 */
			cright->rc_startblock = max(agbno, xfs_refc_next(&tmp));
			cright->rc_blockcount = right->rc_startblock -
					cright->rc_startblock;
			cright->rc_refcount = 1;
		}
	} else {
		/*
		 * No extents, so pretend that there's one covering the whole
		 * range.
		 */
		cright->rc_startblock = agbno;
		cright->rc_blockcount = aglen;
		cright->rc_refcount = 1;
	}
	trace_xfs_refcount_find_right_extent(cur->bc_mp, cur->bc_private.a.agno,
			cright, right, agbno + aglen);
	return error;

out_error:
	trace_xfs_refcount_find_right_extent_error(cur->bc_mp,
			cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/* Is this extent valid? */
static inline bool
xfs_refc_valid(
	struct xfs_refcount_irec	*rc)
{
	return rc->rc_startblock != NULLAGBLOCK;
}

/*
 * Try to merge with any extents on the boundaries of the adjustment range.
 */
STATIC int
xfs_refcount_merge_extents(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		*agbno,
	xfs_extlen_t		*aglen,
	enum xfs_refc_adjust_op adjust,
	int			flags,
	bool			*shape_changed)
{
	struct xfs_refcount_irec	left = {0}, cleft = {0};
	struct xfs_refcount_irec	cright = {0}, right = {0};
	int				error;
	unsigned long long		ulen;
	bool				cequal;

	*shape_changed = false;
	/*
	 * Find the extent just below agbno [left], just above agbno [cleft],
	 * just below (agbno + aglen) [cright], and just above (agbno + aglen)
	 * [right].
	 */
	error = xfs_refcount_find_left_extents(cur, &left, &cleft, *agbno,
			*aglen, flags);
	if (error)
		return error;
	error = xfs_refcount_find_right_extents(cur, &right, &cright, *agbno,
			*aglen, flags);
	if (error)
		return error;

	/* No left or right extent to merge; exit. */
	if (!xfs_refc_valid(&left) && !xfs_refc_valid(&right))
		return 0;

	cequal = (cleft.rc_startblock == cright.rc_startblock) &&
		 (cleft.rc_blockcount == cright.rc_blockcount);

	/* Try to merge left, cleft, and right.  cleft must == cright. */
	ulen = (unsigned long long)left.rc_blockcount + cleft.rc_blockcount +
			right.rc_blockcount;
	if (xfs_refc_valid(&left) && xfs_refc_valid(&right) &&
	    xfs_refc_valid(&cleft) && xfs_refc_valid(&cright) && cequal &&
	    left.rc_refcount == cleft.rc_refcount + adjust &&
	    right.rc_refcount == cleft.rc_refcount + adjust &&
	    ulen < MAXREFCEXTLEN) {
		*shape_changed = true;
		return xfs_refcount_merge_center_extents(cur, &left, &cleft,
				&right, ulen, agbno, aglen);
	}

	/* Try to merge left and cleft. */
	ulen = (unsigned long long)left.rc_blockcount + cleft.rc_blockcount;
	if (xfs_refc_valid(&left) && xfs_refc_valid(&cleft) &&
	    left.rc_refcount == cleft.rc_refcount + adjust &&
	    ulen < MAXREFCEXTLEN) {
		*shape_changed = true;
		error = xfs_refcount_merge_left_extent(cur, &left, &cleft,
				agbno, aglen);
		if (error)
			return error;

		/*
		 * If we just merged left + cleft and cleft == cright,
		 * we no longer have a cright to merge with right.  We're done.
		 */
		if (cequal)
			return 0;
	}

	/* Try to merge cright and right. */
	ulen = (unsigned long long)right.rc_blockcount + cright.rc_blockcount;
	if (xfs_refc_valid(&right) && xfs_refc_valid(&cright) &&
	    right.rc_refcount == cright.rc_refcount + adjust &&
	    ulen < MAXREFCEXTLEN) {
		*shape_changed = true;
		return xfs_refcount_merge_right_extent(cur, &right, &cright,
				agbno, aglen);
	}

	return error;
}

/*
 * XXX: This is a pretty hand-wavy estimate.  The penalty for guessing
 * true incorrectly is a shutdown FS; the penalty for guessing false
 * incorrectly is more transaction rolls than might be necessary.
 * Be conservative here.
 */
static bool
xfs_refcount_still_have_space(
	struct xfs_btree_cur		*cur)
{
	unsigned long			overhead;

	overhead = cur->bc_private.a.priv.refc.shape_changes *
			xfs_allocfree_log_count(cur->bc_mp, 1);
	overhead *= cur->bc_mp->m_sb.sb_blocksize;

	/*
	 * Only allow 2 refcount extent updates per transaction if the
	 * refcount continue update "error" has been injected.
	 */
	if (cur->bc_private.a.priv.refc.nr_ops > 2 &&
	    XFS_TEST_ERROR(false, cur->bc_mp,
			XFS_ERRTAG_REFCOUNT_CONTINUE_UPDATE))
		return false;

	if (cur->bc_private.a.priv.refc.nr_ops == 0)
		return true;
	else if (overhead > cur->bc_tp->t_log_res)
		return false;
	return  cur->bc_tp->t_log_res - overhead >
		cur->bc_private.a.priv.refc.nr_ops * XFS_REFCOUNT_ITEM_OVERHEAD;
}

/*
 * Adjust the refcounts of middle extents.  At this point we should have
 * split extents that crossed the adjustment range; merged with adjacent
 * extents; and updated agbno/aglen to reflect the merges.  Therefore,
 * all we have to do is update the extents inside [agbno, agbno + aglen].
 */
STATIC int
xfs_refcount_adjust_extents(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		*agbno,
	xfs_extlen_t		*aglen,
	enum xfs_refc_adjust_op	adj,
	struct xfs_defer_ops	*dfops,
	struct xfs_owner_info	*oinfo)
{
	struct xfs_refcount_irec	ext, tmp;
	int				error;
	int				found_rec, found_tmp;
	xfs_fsblock_t			fsbno;

	/* Merging did all the work already. */
	if (*aglen == 0)
		return 0;

	error = xfs_refcount_lookup_ge(cur, *agbno, &found_rec);
	if (error)
		goto out_error;

	while (*aglen > 0 && xfs_refcount_still_have_space(cur)) {
		error = xfs_refcount_get_rec(cur, &ext, &found_rec);
		if (error)
			goto out_error;
		if (!found_rec) {
			ext.rc_startblock = cur->bc_mp->m_sb.sb_agblocks;
			ext.rc_blockcount = 0;
			ext.rc_refcount = 0;
		}

		/*
		 * Deal with a hole in the refcount tree; if a file maps to
		 * these blocks and there's no refcountbt record, pretend that
		 * there is one with refcount == 1.
		 */
		if (ext.rc_startblock != *agbno) {
			tmp.rc_startblock = *agbno;
			tmp.rc_blockcount = min(*aglen,
					ext.rc_startblock - *agbno);
			tmp.rc_refcount = 1 + adj;
			trace_xfs_refcount_modify_extent(cur->bc_mp,
					cur->bc_private.a.agno, &tmp);

			/*
			 * Either cover the hole (increment) or
			 * delete the range (decrement).
			 */
			if (tmp.rc_refcount) {
				error = xfs_refcount_insert(cur, &tmp,
						&found_tmp);
				if (error)
					goto out_error;
				XFS_WANT_CORRUPTED_GOTO(cur->bc_mp,
						found_tmp == 1, out_error);
				cur->bc_private.a.priv.refc.nr_ops++;
			} else {
				fsbno = XFS_AGB_TO_FSB(cur->bc_mp,
						cur->bc_private.a.agno,
						tmp.rc_startblock);
				xfs_bmap_add_free(cur->bc_mp, dfops, fsbno,
						tmp.rc_blockcount, oinfo);
			}

			(*agbno) += tmp.rc_blockcount;
			(*aglen) -= tmp.rc_blockcount;

			error = xfs_refcount_lookup_ge(cur, *agbno,
					&found_rec);
			if (error)
				goto out_error;
		}

		/* Stop if there's nothing left to modify */
		if (*aglen == 0 || !xfs_refcount_still_have_space(cur))
			break;

		/*
		 * Adjust the reference count and either update the tree
		 * (incr) or free the blocks (decr).
		 */
		if (ext.rc_refcount == MAXREFCOUNT)
			goto skip;
		ext.rc_refcount += adj;
		trace_xfs_refcount_modify_extent(cur->bc_mp,
				cur->bc_private.a.agno, &ext);
		if (ext.rc_refcount > 1) {
			error = xfs_refcount_update(cur, &ext);
			if (error)
				goto out_error;
			cur->bc_private.a.priv.refc.nr_ops++;
		} else if (ext.rc_refcount == 1) {
			error = xfs_refcount_delete(cur, &found_rec);
			if (error)
				goto out_error;
			XFS_WANT_CORRUPTED_GOTO(cur->bc_mp,
					found_rec == 1, out_error);
			cur->bc_private.a.priv.refc.nr_ops++;
			goto advloop;
		} else {
			fsbno = XFS_AGB_TO_FSB(cur->bc_mp,
					cur->bc_private.a.agno,
					ext.rc_startblock);
			xfs_bmap_add_free(cur->bc_mp, dfops, fsbno,
					ext.rc_blockcount, oinfo);
		}

skip:
		error = xfs_btree_increment(cur, 0, &found_rec);
		if (error)
			goto out_error;

advloop:
		(*agbno) += ext.rc_blockcount;
		(*aglen) -= ext.rc_blockcount;
	}

	return error;
out_error:
	trace_xfs_refcount_modify_extent_error(cur->bc_mp,
			cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/* Adjust the reference count of a range of AG blocks. */
STATIC int
xfs_refcount_adjust(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen,
	xfs_agblock_t		*new_agbno,
	xfs_extlen_t		*new_aglen,
	enum xfs_refc_adjust_op	adj,
	struct xfs_defer_ops	*dfops,
	struct xfs_owner_info	*oinfo)
{
	bool			shape_changed;
	int			shape_changes = 0;
	int			error;

	*new_agbno = agbno;
	*new_aglen = aglen;
	if (adj == XFS_REFCOUNT_ADJUST_INCREASE)
		trace_xfs_refcount_increase(cur->bc_mp, cur->bc_private.a.agno,
				agbno, aglen);
	else
		trace_xfs_refcount_decrease(cur->bc_mp, cur->bc_private.a.agno,
				agbno, aglen);

	/*
	 * Ensure that no rcextents cross the boundary of the adjustment range.
	 */
	error = xfs_refcount_split_extent(cur, agbno, &shape_changed);
	if (error)
		goto out_error;
	if (shape_changed)
		shape_changes++;

	error = xfs_refcount_split_extent(cur, agbno + aglen, &shape_changed);
	if (error)
		goto out_error;
	if (shape_changed)
		shape_changes++;

	/*
	 * Try to merge with the left or right extents of the range.
	 */
	error = xfs_refcount_merge_extents(cur, new_agbno, new_aglen, adj,
			XFS_FIND_RCEXT_SHARED, &shape_changed);
	if (error)
		goto out_error;
	if (shape_changed)
		shape_changes++;
	if (shape_changes)
		cur->bc_private.a.priv.refc.shape_changes++;

	/* Now that we've taken care of the ends, adjust the middle extents */
	error = xfs_refcount_adjust_extents(cur, new_agbno, new_aglen,
			adj, dfops, oinfo);
	if (error)
		goto out_error;

	return 0;

out_error:
	trace_xfs_refcount_adjust_error(cur->bc_mp, cur->bc_private.a.agno,
			error, _RET_IP_);
	return error;
}

/* Clean up after calling xfs_refcount_finish_one. */
void
xfs_refcount_finish_one_cleanup(
	struct xfs_trans	*tp,
	struct xfs_btree_cur	*rcur,
	int			error)
{
	struct xfs_buf		*agbp;

	if (rcur == NULL)
		return;
	agbp = rcur->bc_private.a.agbp;
	xfs_btree_del_cursor(rcur, error ? XFS_BTREE_ERROR : XFS_BTREE_NOERROR);
	if (error)
		xfs_trans_brelse(tp, agbp);
}

/*
 * Process one of the deferred refcount operations.  We pass back the
 * btree cursor to maintain our lock on the btree between calls.
 * This saves time and eliminates a buffer deadlock between the
 * superblock and the AGF because we'll always grab them in the same
 * order.
 */
int
xfs_refcount_finish_one(
	struct xfs_trans		*tp,
	struct xfs_defer_ops		*dfops,
	enum xfs_refcount_intent_type	type,
	xfs_fsblock_t			startblock,
	xfs_extlen_t			blockcount,
	xfs_fsblock_t			*new_fsb,
	xfs_extlen_t			*new_len,
	struct xfs_btree_cur		**pcur)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_btree_cur		*rcur;
	struct xfs_buf			*agbp = NULL;
	int				error = 0;
	xfs_agnumber_t			agno;
	xfs_agblock_t			bno;
	xfs_agblock_t			new_agbno;
	unsigned long			nr_ops = 0;
	int				shape_changes = 0;

	agno = XFS_FSB_TO_AGNO(mp, startblock);
	ASSERT(agno != NULLAGNUMBER);
	bno = XFS_FSB_TO_AGBNO(mp, startblock);

	trace_xfs_refcount_deferred(mp, XFS_FSB_TO_AGNO(mp, startblock),
			type, XFS_FSB_TO_AGBNO(mp, startblock),
			blockcount);

	if (XFS_TEST_ERROR(false, mp,
			XFS_ERRTAG_REFCOUNT_FINISH_ONE))
		return -EIO;

	/*
	 * If we haven't gotten a cursor or the cursor AG doesn't match
	 * the startblock, get one now.
	 */
	rcur = *pcur;
	if (rcur != NULL && rcur->bc_private.a.agno != agno) {
		nr_ops = rcur->bc_private.a.priv.refc.nr_ops;
		shape_changes = rcur->bc_private.a.priv.refc.shape_changes;
		xfs_refcount_finish_one_cleanup(tp, rcur, 0);
		rcur = NULL;
		*pcur = NULL;
	}
	if (rcur == NULL) {
		error = xfs_alloc_read_agf(tp->t_mountp, tp, agno,
				XFS_ALLOC_FLAG_FREEING, &agbp);
		if (error)
			return error;
		if (!agbp)
			return -EFSCORRUPTED;

		rcur = xfs_refcountbt_init_cursor(mp, tp, agbp, agno, dfops);
		if (!rcur) {
			error = -ENOMEM;
			goto out_cur;
		}
		rcur->bc_private.a.priv.refc.nr_ops = nr_ops;
		rcur->bc_private.a.priv.refc.shape_changes = shape_changes;
	}
	*pcur = rcur;

	switch (type) {
	case XFS_REFCOUNT_INCREASE:
		error = xfs_refcount_adjust(rcur, bno, blockcount, &new_agbno,
			new_len, XFS_REFCOUNT_ADJUST_INCREASE, dfops, NULL);
		*new_fsb = XFS_AGB_TO_FSB(mp, agno, new_agbno);
		break;
	case XFS_REFCOUNT_DECREASE:
		error = xfs_refcount_adjust(rcur, bno, blockcount, &new_agbno,
			new_len, XFS_REFCOUNT_ADJUST_DECREASE, dfops, NULL);
		*new_fsb = XFS_AGB_TO_FSB(mp, agno, new_agbno);
		break;
	case XFS_REFCOUNT_ALLOC_COW:
		*new_fsb = startblock + blockcount;
		*new_len = 0;
		error = __xfs_refcount_cow_alloc(rcur, bno, blockcount, dfops);
		break;
	case XFS_REFCOUNT_FREE_COW:
		*new_fsb = startblock + blockcount;
		*new_len = 0;
		error = __xfs_refcount_cow_free(rcur, bno, blockcount, dfops);
		break;
	default:
		ASSERT(0);
		error = -EFSCORRUPTED;
	}
	if (!error && *new_len > 0)
		trace_xfs_refcount_finish_one_leftover(mp, agno, type,
				bno, blockcount, new_agbno, *new_len);
	return error;

out_cur:
	xfs_trans_brelse(tp, agbp);

	return error;
}

/*
 * Record a refcount intent for later processing.
 */
static int
__xfs_refcount_add(
	struct xfs_mount		*mp,
	struct xfs_defer_ops		*dfops,
	enum xfs_refcount_intent_type	type,
	xfs_fsblock_t			startblock,
	xfs_extlen_t			blockcount)
{
	struct xfs_refcount_intent	*ri;

	trace_xfs_refcount_defer(mp, XFS_FSB_TO_AGNO(mp, startblock),
			type, XFS_FSB_TO_AGBNO(mp, startblock),
			blockcount);

	ri = kmem_alloc(sizeof(struct xfs_refcount_intent),
			KM_SLEEP | KM_NOFS);
	INIT_LIST_HEAD(&ri->ri_list);
	ri->ri_type = type;
	ri->ri_startblock = startblock;
	ri->ri_blockcount = blockcount;

	xfs_defer_add(dfops, XFS_DEFER_OPS_TYPE_REFCOUNT, &ri->ri_list);
	return 0;
}

/*
 * Increase the reference count of the blocks backing a file's extent.
 */
int
xfs_refcount_increase_extent(
	struct xfs_mount		*mp,
	struct xfs_defer_ops		*dfops,
	struct xfs_bmbt_irec		*PREV)
{
	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return 0;

	return __xfs_refcount_add(mp, dfops, XFS_REFCOUNT_INCREASE,
			PREV->br_startblock, PREV->br_blockcount);
}

/*
 * Decrease the reference count of the blocks backing a file's extent.
 */
int
xfs_refcount_decrease_extent(
	struct xfs_mount		*mp,
	struct xfs_defer_ops		*dfops,
	struct xfs_bmbt_irec		*PREV)
{
	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return 0;

	return __xfs_refcount_add(mp, dfops, XFS_REFCOUNT_DECREASE,
			PREV->br_startblock, PREV->br_blockcount);
}

/*
 * Given an AG extent, find the lowest-numbered run of shared blocks
 * within that range and return the range in fbno/flen.  If
 * find_end_of_shared is set, return the longest contiguous extent of
 * shared blocks; if not, just return the first extent we find.  If no
 * shared blocks are found, fbno and flen will be set to NULLAGBLOCK
 * and 0, respectively.
 */
int
xfs_refcount_find_shared(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			agbno,
	xfs_extlen_t			aglen,
	xfs_agblock_t			*fbno,
	xfs_extlen_t			*flen,
	bool				find_end_of_shared)
{
	struct xfs_refcount_irec	tmp;
	int				i;
	int				have;
	int				error;

	trace_xfs_refcount_find_shared(cur->bc_mp, cur->bc_private.a.agno,
			agbno, aglen);

	/* By default, skip the whole range */
	*fbno = NULLAGBLOCK;
	*flen = 0;

	/* Try to find a refcount extent that crosses the start */
	error = xfs_refcount_lookup_le(cur, agbno, &have);
	if (error)
		goto out_error;
	if (!have) {
		/* No left extent, look at the next one */
		error = xfs_btree_increment(cur, 0, &have);
		if (error)
			goto out_error;
		if (!have)
			goto done;
	}
	error = xfs_refcount_get_rec(cur, &tmp, &i);
	if (error)
		goto out_error;
	XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, i == 1, out_error);

	/* If the extent ends before the start, look at the next one */
	if (tmp.rc_startblock + tmp.rc_blockcount <= agbno) {
		error = xfs_btree_increment(cur, 0, &have);
		if (error)
			goto out_error;
		if (!have)
			goto done;
		error = xfs_refcount_get_rec(cur, &tmp, &i);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, i == 1, out_error);
	}

	/* If the extent starts after the range we want, bail out */
	if (tmp.rc_startblock >= agbno + aglen)
		goto done;

	/* We found the start of a shared extent! */
	if (tmp.rc_startblock < agbno) {
		tmp.rc_blockcount -= (agbno - tmp.rc_startblock);
		tmp.rc_startblock = agbno;
	}

	*fbno = tmp.rc_startblock;
	*flen = min(tmp.rc_blockcount, agbno + aglen - *fbno);
	if (!find_end_of_shared)
		goto done;

	/* Otherwise, find the end of this shared extent */
	while (*fbno + *flen < agbno + aglen) {
		error = xfs_btree_increment(cur, 0, &have);
		if (error)
			goto out_error;
		if (!have)
			break;
		error = xfs_refcount_get_rec(cur, &tmp, &i);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp, i == 1, out_error);
		if (tmp.rc_startblock >= agbno + aglen ||
		    tmp.rc_startblock != *fbno + *flen)
			break;
		*flen = min(*flen + tmp.rc_blockcount, agbno + aglen - *fbno);
	}

done:
	trace_xfs_refcount_find_shared_result(cur->bc_mp,
			cur->bc_private.a.agno, *fbno, *flen);

out_error:
	if (error)
		trace_xfs_refcount_find_shared_error(cur->bc_mp,
				cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Recovering CoW Blocks After a Crash
 *
 * Due to the way that the copy on write mechanism works, there's a window of
 * opportunity in which we can lose track of allocated blocks during a crash.
 * Because CoW uses delayed allocation in the in-core CoW fork, writeback
 * causes blocks to be allocated and stored in the CoW fork.  The blocks are
 * no longer in the free space btree but are not otherwise recorded anywhere
 * until the write completes and the blocks are mapped into the file.  A crash
 * in between allocation and remapping results in the replacement blocks being
 * lost.  This situation is exacerbated by the CoW extent size hint because
 * allocations can hang around for long time.
 *
 * However, there is a place where we can record these allocations before they
 * become mappings -- the reference count btree.  The btree does not record
 * extents with refcount == 1, so we can record allocations with a refcount of
 * 1.  Blocks being used for CoW writeout cannot be shared, so there should be
 * no conflict with shared block records.  These mappings should be created
 * when we allocate blocks to the CoW fork and deleted when they're removed
 * from the CoW fork.
 *
 * Minor nit: records for in-progress CoW allocations and records for shared
 * extents must never be merged, to preserve the property that (except for CoW
 * allocations) there are no refcount btree entries with refcount == 1.  The
 * only time this could potentially happen is when unsharing a block that's
 * adjacent to CoW allocations, so we must be careful to avoid this.
 *
 * At mount time we recover lost CoW allocations by searching the refcount
 * btree for these refcount == 1 mappings.  These represent CoW allocations
 * that were in progress at the time the filesystem went down, so we can free
 * them to get the space back.
 *
 * This mechanism is superior to creating EFIs for unmapped CoW extents for
 * several reasons -- first, EFIs pin the tail of the log and would have to be
 * periodically relogged to avoid filling up the log.  Second, CoW completions
 * will have to file an EFD and create new EFIs for whatever remains in the
 * CoW fork; this partially takes care of (1) but extent-size reservations
 * will have to periodically relog even if there's no writeout in progress.
 * This can happen if the CoW extent size hint is set, which you really want.
 * Third, EFIs cannot currently be automatically relogged into newer
 * transactions to advance the log tail.  Fourth, stuffing the log full of
 * EFIs places an upper bound on the number of CoW allocations that can be
 * held filesystem-wide at any given time.  Recording them in the refcount
 * btree doesn't require us to maintain any state in memory and doesn't pin
 * the log.
 */
/*
 * Adjust the refcounts of CoW allocations.  These allocations are "magic"
 * in that they're not referenced anywhere else in the filesystem, so we
 * stash them in the refcount btree with a refcount of 1 until either file
 * remapping (or CoW cancellation) happens.
 */
STATIC int
xfs_refcount_adjust_cow_extents(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen,
	enum xfs_refc_adjust_op	adj,
	struct xfs_defer_ops	*dfops,
	struct xfs_owner_info	*oinfo)
{
	struct xfs_refcount_irec	ext, tmp;
	int				error;
	int				found_rec, found_tmp;

	if (aglen == 0)
		return 0;

	/* Find any overlapping refcount records */
	error = xfs_refcount_lookup_ge(cur, agbno, &found_rec);
	if (error)
		goto out_error;
	error = xfs_refcount_get_rec(cur, &ext, &found_rec);
	if (error)
		goto out_error;
	if (!found_rec) {
		ext.rc_startblock = cur->bc_mp->m_sb.sb_agblocks +
				XFS_REFC_COW_START;
		ext.rc_blockcount = 0;
		ext.rc_refcount = 0;
	}

	switch (adj) {
	case XFS_REFCOUNT_ADJUST_COW_ALLOC:
		/* Adding a CoW reservation, there should be nothing here. */
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp,
				ext.rc_startblock >= agbno + aglen, out_error);

		tmp.rc_startblock = agbno;
		tmp.rc_blockcount = aglen;
		tmp.rc_refcount = 1;
		trace_xfs_refcount_modify_extent(cur->bc_mp,
				cur->bc_private.a.agno, &tmp);

		error = xfs_refcount_insert(cur, &tmp,
				&found_tmp);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp,
				found_tmp == 1, out_error);
		break;
	case XFS_REFCOUNT_ADJUST_COW_FREE:
		/* Removing a CoW reservation, there should be one extent. */
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp,
			ext.rc_startblock == agbno, out_error);
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp,
			ext.rc_blockcount == aglen, out_error);
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp,
			ext.rc_refcount == 1, out_error);

		ext.rc_refcount = 0;
		trace_xfs_refcount_modify_extent(cur->bc_mp,
				cur->bc_private.a.agno, &ext);
		error = xfs_refcount_delete(cur, &found_rec);
		if (error)
			goto out_error;
		XFS_WANT_CORRUPTED_GOTO(cur->bc_mp,
				found_rec == 1, out_error);
		break;
	default:
		ASSERT(0);
	}

	return error;
out_error:
	trace_xfs_refcount_modify_extent_error(cur->bc_mp,
			cur->bc_private.a.agno, error, _RET_IP_);
	return error;
}

/*
 * Add or remove refcount btree entries for CoW reservations.
 */
STATIC int
xfs_refcount_adjust_cow(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen,
	enum xfs_refc_adjust_op	adj,
	struct xfs_defer_ops	*dfops)
{
	bool			shape_changed;
	int			error;

	agbno += XFS_REFC_COW_START;

	/*
	 * Ensure that no rcextents cross the boundary of the adjustment range.
	 */
	error = xfs_refcount_split_extent(cur, agbno, &shape_changed);
	if (error)
		goto out_error;

	error = xfs_refcount_split_extent(cur, agbno + aglen, &shape_changed);
	if (error)
		goto out_error;

	/*
	 * Try to merge with the left or right extents of the range.
	 */
	error = xfs_refcount_merge_extents(cur, &agbno, &aglen, adj,
			XFS_FIND_RCEXT_COW, &shape_changed);
	if (error)
		goto out_error;

	/* Now that we've taken care of the ends, adjust the middle extents */
	error = xfs_refcount_adjust_cow_extents(cur, agbno, aglen, adj,
			dfops, NULL);
	if (error)
		goto out_error;

	return 0;

out_error:
	trace_xfs_refcount_adjust_cow_error(cur->bc_mp, cur->bc_private.a.agno,
			error, _RET_IP_);
	return error;
}

/*
 * Record a CoW allocation in the refcount btree.
 */
STATIC int
__xfs_refcount_cow_alloc(
	struct xfs_btree_cur	*rcur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen,
	struct xfs_defer_ops	*dfops)
{
	trace_xfs_refcount_cow_increase(rcur->bc_mp, rcur->bc_private.a.agno,
			agbno, aglen);

	/* Add refcount btree reservation */
	return xfs_refcount_adjust_cow(rcur, agbno, aglen,
			XFS_REFCOUNT_ADJUST_COW_ALLOC, dfops);
}

/*
 * Remove a CoW allocation from the refcount btree.
 */
STATIC int
__xfs_refcount_cow_free(
	struct xfs_btree_cur	*rcur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen,
	struct xfs_defer_ops	*dfops)
{
	trace_xfs_refcount_cow_decrease(rcur->bc_mp, rcur->bc_private.a.agno,
			agbno, aglen);

	/* Remove refcount btree reservation */
	return xfs_refcount_adjust_cow(rcur, agbno, aglen,
			XFS_REFCOUNT_ADJUST_COW_FREE, dfops);
}

/* Record a CoW staging extent in the refcount btree. */
int
xfs_refcount_alloc_cow_extent(
	struct xfs_mount		*mp,
	struct xfs_defer_ops		*dfops,
	xfs_fsblock_t			fsb,
	xfs_extlen_t			len)
{
	int				error;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return 0;

	error = __xfs_refcount_add(mp, dfops, XFS_REFCOUNT_ALLOC_COW,
			fsb, len);
	if (error)
		return error;

	/* Add rmap entry */
	return xfs_rmap_alloc_extent(mp, dfops, XFS_FSB_TO_AGNO(mp, fsb),
			XFS_FSB_TO_AGBNO(mp, fsb), len, XFS_RMAP_OWN_COW);
}

/* Forget a CoW staging event in the refcount btree. */
int
xfs_refcount_free_cow_extent(
	struct xfs_mount		*mp,
	struct xfs_defer_ops		*dfops,
	xfs_fsblock_t			fsb,
	xfs_extlen_t			len)
{
	int				error;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return 0;

	/* Remove rmap entry */
	error = xfs_rmap_free_extent(mp, dfops, XFS_FSB_TO_AGNO(mp, fsb),
			XFS_FSB_TO_AGBNO(mp, fsb), len, XFS_RMAP_OWN_COW);
	if (error)
		return error;

	return __xfs_refcount_add(mp, dfops, XFS_REFCOUNT_FREE_COW,
			fsb, len);
}

struct xfs_refcount_recovery {
	struct list_head		rr_list;
	struct xfs_refcount_irec	rr_rrec;
};

/* Stuff an extent on the recovery list. */
STATIC int
xfs_refcount_recover_extent(
	struct xfs_btree_cur		*cur,
	union xfs_btree_rec		*rec,
	void				*priv)
{
	struct list_head		*debris = priv;
	struct xfs_refcount_recovery	*rr;

	if (be32_to_cpu(rec->refc.rc_refcount) != 1)
		return -EFSCORRUPTED;

	rr = kmem_alloc(sizeof(struct xfs_refcount_recovery), KM_SLEEP);
	xfs_refcount_btrec_to_irec(rec, &rr->rr_rrec);
	list_add_tail(&rr->rr_list, debris);

	return 0;
}

/* Find and remove leftover CoW reservations. */
int
xfs_refcount_recover_cow_leftovers(
	struct xfs_mount		*mp,
	xfs_agnumber_t			agno)
{
	struct xfs_trans		*tp;
	struct xfs_btree_cur		*cur;
	struct xfs_buf			*agbp;
	struct xfs_refcount_recovery	*rr, *n;
	struct list_head		debris;
	union xfs_btree_irec		low;
	union xfs_btree_irec		high;
	struct xfs_defer_ops		dfops;
	xfs_fsblock_t			fsb;
	xfs_agblock_t			agbno;
	int				error;

	if (mp->m_sb.sb_agblocks >= XFS_REFC_COW_START)
		return -EOPNOTSUPP;

	INIT_LIST_HEAD(&debris);

	/*
	 * In this first part, we use an empty transaction to gather up
	 * all the leftover CoW extents so that we can subsequently
	 * delete them.  The empty transaction is used to avoid
	 * a buffer lock deadlock if there happens to be a loop in the
	 * refcountbt because we're allowed to re-grab a buffer that is
	 * already attached to our transaction.  When we're done
	 * recording the CoW debris we cancel the (empty) transaction
	 * and everything goes away cleanly.
	 */
	error = xfs_trans_alloc_empty(mp, &tp);
	if (error)
		return error;

	error = xfs_alloc_read_agf(mp, tp, agno, 0, &agbp);
	if (error)
		goto out_trans;
	if (!agbp) {
		error = -ENOMEM;
		goto out_trans;
	}
	cur = xfs_refcountbt_init_cursor(mp, tp, agbp, agno, NULL);

	/* Find all the leftover CoW staging extents. */
	memset(&low, 0, sizeof(low));
	memset(&high, 0, sizeof(high));
	low.rc.rc_startblock = XFS_REFC_COW_START;
	high.rc.rc_startblock = -1U;
	error = xfs_btree_query_range(cur, &low, &high,
			xfs_refcount_recover_extent, &debris);
	if (error)
		goto out_cursor;
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	xfs_trans_brelse(tp, agbp);
	xfs_trans_cancel(tp);

	/* Now iterate the list to free the leftovers */
	list_for_each_entry_safe(rr, n, &debris, rr_list) {
		/* Set up transaction. */
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, 0, 0, 0, &tp);
		if (error)
			goto out_free;

		trace_xfs_refcount_recover_extent(mp, agno, &rr->rr_rrec);

		/* Free the orphan record */
		xfs_defer_init(&dfops, &fsb);
		agbno = rr->rr_rrec.rc_startblock - XFS_REFC_COW_START;
		fsb = XFS_AGB_TO_FSB(mp, agno, agbno);
		error = xfs_refcount_free_cow_extent(mp, &dfops, fsb,
				rr->rr_rrec.rc_blockcount);
		if (error)
			goto out_defer;

		/* Free the block. */
		xfs_bmap_add_free(mp, &dfops, fsb,
				rr->rr_rrec.rc_blockcount, NULL);

		error = xfs_defer_finish(&tp, &dfops);
		if (error)
			goto out_defer;

		error = xfs_trans_commit(tp);
		if (error)
			goto out_free;

		list_del(&rr->rr_list);
		kmem_free(rr);
	}

	return error;
out_defer:
	xfs_defer_cancel(&dfops);
out_trans:
	xfs_trans_cancel(tp);
out_free:
	/* Free the leftover list */
	list_for_each_entry_safe(rr, n, &debris, rr_list) {
		list_del(&rr->rr_list);
		kmem_free(rr);
	}
	return error;

out_cursor:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	xfs_trans_brelse(tp, agbp);
	goto out_trans;
}
