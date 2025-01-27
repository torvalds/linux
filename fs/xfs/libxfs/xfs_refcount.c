// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_refcount_btree.h"
#include "xfs_alloc.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_bit.h"
#include "xfs_refcount.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"
#include "xfs_health.h"
#include "xfs_refcount_item.h"

struct kmem_cache	*xfs_refcount_intent_cache;

/* Allowable refcount adjustment amounts. */
enum xfs_refc_adjust_op {
	XFS_REFCOUNT_ADJUST_INCREASE	= 1,
	XFS_REFCOUNT_ADJUST_DECREASE	= -1,
	XFS_REFCOUNT_ADJUST_COW_ALLOC	= 0,
	XFS_REFCOUNT_ADJUST_COW_FREE	= -1,
};

STATIC int __xfs_refcount_cow_alloc(struct xfs_btree_cur *rcur,
		xfs_agblock_t agbno, xfs_extlen_t aglen);
STATIC int __xfs_refcount_cow_free(struct xfs_btree_cur *rcur,
		xfs_agblock_t agbno, xfs_extlen_t aglen);

/*
 * Look up the first record less than or equal to [bno, len] in the btree
 * given by cur.
 */
int
xfs_refcount_lookup_le(
	struct xfs_btree_cur	*cur,
	enum xfs_refc_domain	domain,
	xfs_agblock_t		bno,
	int			*stat)
{
	trace_xfs_refcount_lookup(cur,
			xfs_refcount_encode_startblock(bno, domain),
			XFS_LOOKUP_LE);
	cur->bc_rec.rc.rc_startblock = bno;
	cur->bc_rec.rc.rc_blockcount = 0;
	cur->bc_rec.rc.rc_domain = domain;
	return xfs_btree_lookup(cur, XFS_LOOKUP_LE, stat);
}

/*
 * Look up the first record greater than or equal to [bno, len] in the btree
 * given by cur.
 */
int
xfs_refcount_lookup_ge(
	struct xfs_btree_cur	*cur,
	enum xfs_refc_domain	domain,
	xfs_agblock_t		bno,
	int			*stat)
{
	trace_xfs_refcount_lookup(cur,
			xfs_refcount_encode_startblock(bno, domain),
			XFS_LOOKUP_GE);
	cur->bc_rec.rc.rc_startblock = bno;
	cur->bc_rec.rc.rc_blockcount = 0;
	cur->bc_rec.rc.rc_domain = domain;
	return xfs_btree_lookup(cur, XFS_LOOKUP_GE, stat);
}

/*
 * Look up the first record equal to [bno, len] in the btree
 * given by cur.
 */
int
xfs_refcount_lookup_eq(
	struct xfs_btree_cur	*cur,
	enum xfs_refc_domain	domain,
	xfs_agblock_t		bno,
	int			*stat)
{
	trace_xfs_refcount_lookup(cur,
			xfs_refcount_encode_startblock(bno, domain),
			XFS_LOOKUP_LE);
	cur->bc_rec.rc.rc_startblock = bno;
	cur->bc_rec.rc.rc_blockcount = 0;
	cur->bc_rec.rc.rc_domain = domain;
	return xfs_btree_lookup(cur, XFS_LOOKUP_EQ, stat);
}

/* Convert on-disk record to in-core format. */
void
xfs_refcount_btrec_to_irec(
	const union xfs_btree_rec	*rec,
	struct xfs_refcount_irec	*irec)
{
	uint32_t			start;

	start = be32_to_cpu(rec->refc.rc_startblock);
	if (start & XFS_REFC_COWFLAG) {
		start &= ~XFS_REFC_COWFLAG;
		irec->rc_domain = XFS_REFC_DOMAIN_COW;
	} else {
		irec->rc_domain = XFS_REFC_DOMAIN_SHARED;
	}

	irec->rc_startblock = start;
	irec->rc_blockcount = be32_to_cpu(rec->refc.rc_blockcount);
	irec->rc_refcount = be32_to_cpu(rec->refc.rc_refcount);
}

/* Simple checks for refcount records. */
xfs_failaddr_t
xfs_refcount_check_irec(
	struct xfs_perag		*pag,
	const struct xfs_refcount_irec	*irec)
{
	if (irec->rc_blockcount == 0 || irec->rc_blockcount > MAXREFCEXTLEN)
		return __this_address;

	if (!xfs_refcount_check_domain(irec))
		return __this_address;

	/* check for valid extent range, including overflow */
	if (!xfs_verify_agbext(pag, irec->rc_startblock, irec->rc_blockcount))
		return __this_address;

	if (irec->rc_refcount == 0 || irec->rc_refcount > MAXREFCOUNT)
		return __this_address;

	return NULL;
}

static inline int
xfs_refcount_complain_bad_rec(
	struct xfs_btree_cur		*cur,
	xfs_failaddr_t			fa,
	const struct xfs_refcount_irec	*irec)
{
	struct xfs_mount		*mp = cur->bc_mp;

	xfs_warn(mp,
 "Refcount BTree record corruption in AG %d detected at %pS!",
				cur->bc_group->xg_gno, fa);
	xfs_warn(mp,
		"Start block 0x%x, block count 0x%x, references 0x%x",
		irec->rc_startblock, irec->rc_blockcount, irec->rc_refcount);
	xfs_btree_mark_sick(cur);
	return -EFSCORRUPTED;
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
	xfs_failaddr_t			fa;
	int				error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (error || !*stat)
		return error;

	xfs_refcount_btrec_to_irec(rec, irec);
	fa = xfs_refcount_check_irec(to_perag(cur->bc_group), irec);
	if (fa)
		return xfs_refcount_complain_bad_rec(cur, fa, irec);

	trace_xfs_refcount_get(cur, irec);
	return 0;
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
	uint32_t		start;
	int			error;

	trace_xfs_refcount_update(cur, irec);

	start = xfs_refcount_encode_startblock(irec->rc_startblock,
			irec->rc_domain);
	rec.refc.rc_startblock = cpu_to_be32(start);
	rec.refc.rc_blockcount = cpu_to_be32(irec->rc_blockcount);
	rec.refc.rc_refcount = cpu_to_be32(irec->rc_refcount);

	error = xfs_btree_update(cur, &rec);
	if (error)
		trace_xfs_refcount_update_error(cur, error, _RET_IP_);
	return error;
}

/*
 * Insert the record referred to by cur to the value given
 * by [bno, len, refcount].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
int
xfs_refcount_insert(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*irec,
	int				*i)
{
	int				error;

	trace_xfs_refcount_insert(cur, irec);

	cur->bc_rec.rc.rc_startblock = irec->rc_startblock;
	cur->bc_rec.rc.rc_blockcount = irec->rc_blockcount;
	cur->bc_rec.rc.rc_refcount = irec->rc_refcount;
	cur->bc_rec.rc.rc_domain = irec->rc_domain;

	error = xfs_btree_insert(cur, i);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, *i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

out_error:
	if (error)
		trace_xfs_refcount_insert_error(cur, error, _RET_IP_);
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
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}
	trace_xfs_refcount_delete(cur, &irec);
	error = xfs_btree_delete(cur, i);
	if (XFS_IS_CORRUPT(cur->bc_mp, *i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}
	if (error)
		goto out_error;
	error = xfs_refcount_lookup_ge(cur, irec.rc_domain, irec.rc_startblock,
			&found_rec);
out_error:
	if (error)
		trace_xfs_refcount_delete_error(cur, error, _RET_IP_);
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
	enum xfs_refc_domain		domain,
	xfs_agblock_t			agbno,
	bool				*shape_changed)
{
	struct xfs_refcount_irec	rcext, tmp;
	int				found_rec;
	int				error;

	*shape_changed = false;
	error = xfs_refcount_lookup_le(cur, domain, agbno, &found_rec);
	if (error)
		goto out_error;
	if (!found_rec)
		return 0;

	error = xfs_refcount_get_rec(cur, &rcext, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}
	if (rcext.rc_domain != domain)
		return 0;
	if (rcext.rc_startblock == agbno || xfs_refc_next(&rcext) <= agbno)
		return 0;

	*shape_changed = true;
	trace_xfs_refcount_split_extent(cur, &rcext, agbno);

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
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}
	return error;

out_error:
	trace_xfs_refcount_split_extent_error(cur, error, _RET_IP_);
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
	xfs_extlen_t			*aglen)
{
	int				error;
	int				found_rec;

	trace_xfs_refcount_merge_center_extents(cur, left, center, right);

	ASSERT(left->rc_domain == center->rc_domain);
	ASSERT(right->rc_domain == center->rc_domain);

	/*
	 * Make sure the center and right extents are not in the btree.
	 * If the center extent was synthesized, the first delete call
	 * removes the right extent and we skip the second deletion.
	 * If center and right were in the btree, then the first delete
	 * call removes the center and the second one removes the right
	 * extent.
	 */
	error = xfs_refcount_lookup_ge(cur, center->rc_domain,
			center->rc_startblock, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	error = xfs_refcount_delete(cur, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	if (center->rc_refcount > 1) {
		error = xfs_refcount_delete(cur, &found_rec);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
	}

	/* Enlarge the left extent. */
	error = xfs_refcount_lookup_le(cur, left->rc_domain,
			left->rc_startblock, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	left->rc_blockcount = extlen;
	error = xfs_refcount_update(cur, left);
	if (error)
		goto out_error;

	*aglen = 0;
	return error;

out_error:
	trace_xfs_refcount_merge_center_extents_error(cur, error, _RET_IP_);
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

	trace_xfs_refcount_merge_left_extent(cur, left, cleft);

	ASSERT(left->rc_domain == cleft->rc_domain);

	/* If the extent at agbno (cleft) wasn't synthesized, remove it. */
	if (cleft->rc_refcount > 1) {
		error = xfs_refcount_lookup_le(cur, cleft->rc_domain,
				cleft->rc_startblock, &found_rec);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}

		error = xfs_refcount_delete(cur, &found_rec);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
	}

	/* Enlarge the left extent. */
	error = xfs_refcount_lookup_le(cur, left->rc_domain,
			left->rc_startblock, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	left->rc_blockcount += cleft->rc_blockcount;
	error = xfs_refcount_update(cur, left);
	if (error)
		goto out_error;

	*agbno += cleft->rc_blockcount;
	*aglen -= cleft->rc_blockcount;
	return error;

out_error:
	trace_xfs_refcount_merge_left_extent_error(cur, error, _RET_IP_);
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
	xfs_extlen_t			*aglen)
{
	int				error;
	int				found_rec;

	trace_xfs_refcount_merge_right_extent(cur, cright, right);

	ASSERT(right->rc_domain == cright->rc_domain);

	/*
	 * If the extent ending at agbno+aglen (cright) wasn't synthesized,
	 * remove it.
	 */
	if (cright->rc_refcount > 1) {
		error = xfs_refcount_lookup_le(cur, cright->rc_domain,
				cright->rc_startblock, &found_rec);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}

		error = xfs_refcount_delete(cur, &found_rec);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
	}

	/* Enlarge the right extent. */
	error = xfs_refcount_lookup_le(cur, right->rc_domain,
			right->rc_startblock, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	right->rc_startblock -= cright->rc_blockcount;
	right->rc_blockcount += cright->rc_blockcount;
	error = xfs_refcount_update(cur, right);
	if (error)
		goto out_error;

	*aglen -= cright->rc_blockcount;
	return error;

out_error:
	trace_xfs_refcount_merge_right_extent_error(cur, error, _RET_IP_);
	return error;
}

/*
 * Find the left extent and the one after it (cleft).  This function assumes
 * that we've already split any extent crossing agbno.
 */
STATIC int
xfs_refcount_find_left_extents(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_irec	*left,
	struct xfs_refcount_irec	*cleft,
	enum xfs_refc_domain		domain,
	xfs_agblock_t			agbno,
	xfs_extlen_t			aglen)
{
	struct xfs_refcount_irec	tmp;
	int				error;
	int				found_rec;

	left->rc_startblock = cleft->rc_startblock = NULLAGBLOCK;
	error = xfs_refcount_lookup_le(cur, domain, agbno - 1, &found_rec);
	if (error)
		goto out_error;
	if (!found_rec)
		return 0;

	error = xfs_refcount_get_rec(cur, &tmp, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	if (tmp.rc_domain != domain)
		return 0;
	if (xfs_refc_next(&tmp) != agbno)
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
		if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}

		if (tmp.rc_domain != domain)
			goto not_found;

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
			cleft->rc_domain = domain;
		}
	} else {
not_found:
		/*
		 * No extents, so pretend that there's one covering the whole
		 * range.
		 */
		cleft->rc_startblock = agbno;
		cleft->rc_blockcount = aglen;
		cleft->rc_refcount = 1;
		cleft->rc_domain = domain;
	}
	trace_xfs_refcount_find_left_extent(cur, left, cleft, agbno);
	return error;

out_error:
	trace_xfs_refcount_find_left_extent_error(cur, error, _RET_IP_);
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
	enum xfs_refc_domain		domain,
	xfs_agblock_t			agbno,
	xfs_extlen_t			aglen)
{
	struct xfs_refcount_irec	tmp;
	int				error;
	int				found_rec;

	right->rc_startblock = cright->rc_startblock = NULLAGBLOCK;
	error = xfs_refcount_lookup_ge(cur, domain, agbno + aglen, &found_rec);
	if (error)
		goto out_error;
	if (!found_rec)
		return 0;

	error = xfs_refcount_get_rec(cur, &tmp, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	if (tmp.rc_domain != domain)
		return 0;
	if (tmp.rc_startblock != agbno + aglen)
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
		if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}

		if (tmp.rc_domain != domain)
			goto not_found;

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
			cright->rc_domain = domain;
		}
	} else {
not_found:
		/*
		 * No extents, so pretend that there's one covering the whole
		 * range.
		 */
		cright->rc_startblock = agbno;
		cright->rc_blockcount = aglen;
		cright->rc_refcount = 1;
		cright->rc_domain = domain;
	}
	trace_xfs_refcount_find_right_extent(cur, cright, right,
			agbno + aglen);
	return error;

out_error:
	trace_xfs_refcount_find_right_extent_error(cur, error, _RET_IP_);
	return error;
}

/* Is this extent valid? */
static inline bool
xfs_refc_valid(
	const struct xfs_refcount_irec	*rc)
{
	return rc->rc_startblock != NULLAGBLOCK;
}

static inline xfs_nlink_t
xfs_refc_merge_refcount(
	const struct xfs_refcount_irec	*irec,
	enum xfs_refc_adjust_op		adjust)
{
	/* Once a record hits MAXREFCOUNT, it is pinned there forever */
	if (irec->rc_refcount == MAXREFCOUNT)
		return MAXREFCOUNT;
	return irec->rc_refcount + adjust;
}

static inline bool
xfs_refc_want_merge_center(
	const struct xfs_refcount_irec	*left,
	const struct xfs_refcount_irec	*cleft,
	const struct xfs_refcount_irec	*cright,
	const struct xfs_refcount_irec	*right,
	bool				cleft_is_cright,
	enum xfs_refc_adjust_op		adjust,
	unsigned long long		*ulenp)
{
	unsigned long long		ulen = left->rc_blockcount;
	xfs_nlink_t			new_refcount;

	/*
	 * To merge with a center record, both shoulder records must be
	 * adjacent to the record we want to adjust.  This is only true if
	 * find_left and find_right made all four records valid.
	 */
	if (!xfs_refc_valid(left)  || !xfs_refc_valid(right) ||
	    !xfs_refc_valid(cleft) || !xfs_refc_valid(cright))
		return false;

	/* There must only be one record for the entire range. */
	if (!cleft_is_cright)
		return false;

	/* The shoulder record refcounts must match the new refcount. */
	new_refcount = xfs_refc_merge_refcount(cleft, adjust);
	if (left->rc_refcount != new_refcount)
		return false;
	if (right->rc_refcount != new_refcount)
		return false;

	/*
	 * The new record cannot exceed the max length.  ulen is a ULL as the
	 * individual record block counts can be up to (u32 - 1) in length
	 * hence we need to catch u32 addition overflows here.
	 */
	ulen += cleft->rc_blockcount + right->rc_blockcount;
	if (ulen >= MAXREFCEXTLEN)
		return false;

	*ulenp = ulen;
	return true;
}

static inline bool
xfs_refc_want_merge_left(
	const struct xfs_refcount_irec	*left,
	const struct xfs_refcount_irec	*cleft,
	enum xfs_refc_adjust_op		adjust)
{
	unsigned long long		ulen = left->rc_blockcount;
	xfs_nlink_t			new_refcount;

	/*
	 * For a left merge, the left shoulder record must be adjacent to the
	 * start of the range.  If this is true, find_left made left and cleft
	 * contain valid contents.
	 */
	if (!xfs_refc_valid(left) || !xfs_refc_valid(cleft))
		return false;

	/* Left shoulder record refcount must match the new refcount. */
	new_refcount = xfs_refc_merge_refcount(cleft, adjust);
	if (left->rc_refcount != new_refcount)
		return false;

	/*
	 * The new record cannot exceed the max length.  ulen is a ULL as the
	 * individual record block counts can be up to (u32 - 1) in length
	 * hence we need to catch u32 addition overflows here.
	 */
	ulen += cleft->rc_blockcount;
	if (ulen >= MAXREFCEXTLEN)
		return false;

	return true;
}

static inline bool
xfs_refc_want_merge_right(
	const struct xfs_refcount_irec	*cright,
	const struct xfs_refcount_irec	*right,
	enum xfs_refc_adjust_op		adjust)
{
	unsigned long long		ulen = right->rc_blockcount;
	xfs_nlink_t			new_refcount;

	/*
	 * For a right merge, the right shoulder record must be adjacent to the
	 * end of the range.  If this is true, find_right made cright and right
	 * contain valid contents.
	 */
	if (!xfs_refc_valid(right) || !xfs_refc_valid(cright))
		return false;

	/* Right shoulder record refcount must match the new refcount. */
	new_refcount = xfs_refc_merge_refcount(cright, adjust);
	if (right->rc_refcount != new_refcount)
		return false;

	/*
	 * The new record cannot exceed the max length.  ulen is a ULL as the
	 * individual record block counts can be up to (u32 - 1) in length
	 * hence we need to catch u32 addition overflows here.
	 */
	ulen += cright->rc_blockcount;
	if (ulen >= MAXREFCEXTLEN)
		return false;

	return true;
}

/*
 * Try to merge with any extents on the boundaries of the adjustment range.
 */
STATIC int
xfs_refcount_merge_extents(
	struct xfs_btree_cur	*cur,
	enum xfs_refc_domain	domain,
	xfs_agblock_t		*agbno,
	xfs_extlen_t		*aglen,
	enum xfs_refc_adjust_op adjust,
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
	error = xfs_refcount_find_left_extents(cur, &left, &cleft, domain,
			*agbno, *aglen);
	if (error)
		return error;
	error = xfs_refcount_find_right_extents(cur, &right, &cright, domain,
			*agbno, *aglen);
	if (error)
		return error;

	/* No left or right extent to merge; exit. */
	if (!xfs_refc_valid(&left) && !xfs_refc_valid(&right))
		return 0;

	cequal = (cleft.rc_startblock == cright.rc_startblock) &&
		 (cleft.rc_blockcount == cright.rc_blockcount);

	/* Try to merge left, cleft, and right.  cleft must == cright. */
	if (xfs_refc_want_merge_center(&left, &cleft, &cright, &right, cequal,
				adjust, &ulen)) {
		*shape_changed = true;
		return xfs_refcount_merge_center_extents(cur, &left, &cleft,
				&right, ulen, aglen);
	}

	/* Try to merge left and cleft. */
	if (xfs_refc_want_merge_left(&left, &cleft, adjust)) {
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
	if (xfs_refc_want_merge_right(&cright, &right, adjust)) {
		*shape_changed = true;
		return xfs_refcount_merge_right_extent(cur, &right, &cright,
				aglen);
	}

	return 0;
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

	/*
	 * Worst case estimate: full splits of the free space and rmap btrees
	 * to handle each of the shape changes to the refcount btree.
	 */
	overhead = xfs_allocfree_block_count(cur->bc_mp,
				cur->bc_refc.shape_changes);
	overhead += cur->bc_mp->m_refc_maxlevels;
	overhead *= cur->bc_mp->m_sb.sb_blocksize;

	/*
	 * Only allow 2 refcount extent updates per transaction if the
	 * refcount continue update "error" has been injected.
	 */
	if (cur->bc_refc.nr_ops > 2 &&
	    XFS_TEST_ERROR(false, cur->bc_mp,
			XFS_ERRTAG_REFCOUNT_CONTINUE_UPDATE))
		return false;

	if (cur->bc_refc.nr_ops == 0)
		return true;
	else if (overhead > cur->bc_tp->t_log_res)
		return false;
	return cur->bc_tp->t_log_res - overhead >
		cur->bc_refc.nr_ops * XFS_REFCOUNT_ITEM_OVERHEAD;
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
	enum xfs_refc_adjust_op	adj)
{
	struct xfs_refcount_irec	ext, tmp;
	int				error;
	int				found_rec, found_tmp;
	xfs_fsblock_t			fsbno;

	/* Merging did all the work already. */
	if (*aglen == 0)
		return 0;

	error = xfs_refcount_lookup_ge(cur, XFS_REFC_DOMAIN_SHARED, *agbno,
			&found_rec);
	if (error)
		goto out_error;

	while (*aglen > 0 && xfs_refcount_still_have_space(cur)) {
		error = xfs_refcount_get_rec(cur, &ext, &found_rec);
		if (error)
			goto out_error;
		if (!found_rec || ext.rc_domain != XFS_REFC_DOMAIN_SHARED) {
			ext.rc_startblock = cur->bc_mp->m_sb.sb_agblocks;
			ext.rc_blockcount = 0;
			ext.rc_refcount = 0;
			ext.rc_domain = XFS_REFC_DOMAIN_SHARED;
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
			tmp.rc_domain = XFS_REFC_DOMAIN_SHARED;

			trace_xfs_refcount_modify_extent(cur, &tmp);

			/*
			 * Either cover the hole (increment) or
			 * delete the range (decrement).
			 */
			cur->bc_refc.nr_ops++;
			if (tmp.rc_refcount) {
				error = xfs_refcount_insert(cur, &tmp,
						&found_tmp);
				if (error)
					goto out_error;
				if (XFS_IS_CORRUPT(cur->bc_mp,
						   found_tmp != 1)) {
					xfs_btree_mark_sick(cur);
					error = -EFSCORRUPTED;
					goto out_error;
				}
			} else {
				fsbno = xfs_agbno_to_fsb(to_perag(cur->bc_group),
						tmp.rc_startblock);
				error = xfs_free_extent_later(cur->bc_tp, fsbno,
						  tmp.rc_blockcount, NULL,
						  XFS_AG_RESV_NONE, 0);
				if (error)
					goto out_error;
			}

			(*agbno) += tmp.rc_blockcount;
			(*aglen) -= tmp.rc_blockcount;

			/* Stop if there's nothing left to modify */
			if (*aglen == 0 || !xfs_refcount_still_have_space(cur))
				break;

			/* Move the cursor to the start of ext. */
			error = xfs_refcount_lookup_ge(cur,
					XFS_REFC_DOMAIN_SHARED, *agbno,
					&found_rec);
			if (error)
				goto out_error;
		}

		/*
		 * A previous step trimmed agbno/aglen such that the end of the
		 * range would not be in the middle of the record.  If this is
		 * no longer the case, something is seriously wrong with the
		 * btree.  Make sure we never feed the synthesized record into
		 * the processing loop below.
		 */
		if (XFS_IS_CORRUPT(cur->bc_mp, ext.rc_blockcount == 0) ||
		    XFS_IS_CORRUPT(cur->bc_mp, ext.rc_blockcount > *aglen)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}

		/*
		 * Adjust the reference count and either update the tree
		 * (incr) or free the blocks (decr).
		 */
		if (ext.rc_refcount == MAXREFCOUNT)
			goto skip;
		ext.rc_refcount += adj;
		trace_xfs_refcount_modify_extent(cur, &ext);
		cur->bc_refc.nr_ops++;
		if (ext.rc_refcount > 1) {
			error = xfs_refcount_update(cur, &ext);
			if (error)
				goto out_error;
		} else if (ext.rc_refcount == 1) {
			error = xfs_refcount_delete(cur, &found_rec);
			if (error)
				goto out_error;
			if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
				xfs_btree_mark_sick(cur);
				error = -EFSCORRUPTED;
				goto out_error;
			}
			goto advloop;
		} else {
			fsbno = xfs_agbno_to_fsb(to_perag(cur->bc_group),
					ext.rc_startblock);
			error = xfs_free_extent_later(cur->bc_tp, fsbno,
					ext.rc_blockcount, NULL,
					XFS_AG_RESV_NONE, 0);
			if (error)
				goto out_error;
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
	trace_xfs_refcount_modify_extent_error(cur, error, _RET_IP_);
	return error;
}

/* Adjust the reference count of a range of AG blocks. */
STATIC int
xfs_refcount_adjust(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		*agbno,
	xfs_extlen_t		*aglen,
	enum xfs_refc_adjust_op	adj)
{
	bool			shape_changed;
	int			shape_changes = 0;
	int			error;

	if (adj == XFS_REFCOUNT_ADJUST_INCREASE)
		trace_xfs_refcount_increase(cur, *agbno, *aglen);
	else
		trace_xfs_refcount_decrease(cur, *agbno, *aglen);

	/*
	 * Ensure that no rcextents cross the boundary of the adjustment range.
	 */
	error = xfs_refcount_split_extent(cur, XFS_REFC_DOMAIN_SHARED,
			*agbno, &shape_changed);
	if (error)
		goto out_error;
	if (shape_changed)
		shape_changes++;

	error = xfs_refcount_split_extent(cur, XFS_REFC_DOMAIN_SHARED,
			*agbno + *aglen, &shape_changed);
	if (error)
		goto out_error;
	if (shape_changed)
		shape_changes++;

	/*
	 * Try to merge with the left or right extents of the range.
	 */
	error = xfs_refcount_merge_extents(cur, XFS_REFC_DOMAIN_SHARED,
			agbno, aglen, adj, &shape_changed);
	if (error)
		goto out_error;
	if (shape_changed)
		shape_changes++;
	if (shape_changes)
		cur->bc_refc.shape_changes++;

	/* Now that we've taken care of the ends, adjust the middle extents */
	error = xfs_refcount_adjust_extents(cur, agbno, aglen, adj);
	if (error)
		goto out_error;

	return 0;

out_error:
	trace_xfs_refcount_adjust_error(cur, error, _RET_IP_);
	return error;
}

/*
 * Set up a continuation a deferred refcount operation by updating the intent.
 * Checks to make sure we're not going to run off the end of the AG.
 */
static inline int
xfs_refcount_continue_op(
	struct xfs_btree_cur		*cur,
	struct xfs_refcount_intent	*ri,
	xfs_agblock_t			new_agbno)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_perag		*pag = to_perag(cur->bc_group);

	if (XFS_IS_CORRUPT(mp, !xfs_verify_agbext(pag, new_agbno,
					ri->ri_blockcount))) {
		xfs_btree_mark_sick(cur);
		return -EFSCORRUPTED;
	}

	ri->ri_startblock = xfs_agbno_to_fsb(pag, new_agbno);

	ASSERT(xfs_verify_fsbext(mp, ri->ri_startblock, ri->ri_blockcount));
	ASSERT(pag_agno(pag) == XFS_FSB_TO_AGNO(mp, ri->ri_startblock));

	return 0;
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
	struct xfs_refcount_intent	*ri,
	struct xfs_btree_cur		**pcur)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_btree_cur		*rcur = *pcur;
	struct xfs_buf			*agbp = NULL;
	int				error = 0;
	xfs_agblock_t			bno;
	unsigned long			nr_ops = 0;
	int				shape_changes = 0;

	bno = XFS_FSB_TO_AGBNO(mp, ri->ri_startblock);

	trace_xfs_refcount_deferred(mp, ri);

	if (XFS_TEST_ERROR(false, mp, XFS_ERRTAG_REFCOUNT_FINISH_ONE))
		return -EIO;

	/*
	 * If we haven't gotten a cursor or the cursor AG doesn't match
	 * the startblock, get one now.
	 */
	if (rcur != NULL && rcur->bc_group != ri->ri_group) {
		nr_ops = rcur->bc_refc.nr_ops;
		shape_changes = rcur->bc_refc.shape_changes;
		xfs_btree_del_cursor(rcur, 0);
		rcur = NULL;
		*pcur = NULL;
	}
	if (rcur == NULL) {
		struct xfs_perag	*pag = to_perag(ri->ri_group);

		error = xfs_alloc_read_agf(pag, tp,
				XFS_ALLOC_FLAG_FREEING, &agbp);
		if (error)
			return error;

		*pcur = rcur = xfs_refcountbt_init_cursor(mp, tp, agbp, pag);
		rcur->bc_refc.nr_ops = nr_ops;
		rcur->bc_refc.shape_changes = shape_changes;
	}

	switch (ri->ri_type) {
	case XFS_REFCOUNT_INCREASE:
		error = xfs_refcount_adjust(rcur, &bno, &ri->ri_blockcount,
				XFS_REFCOUNT_ADJUST_INCREASE);
		if (error)
			return error;
		if (ri->ri_blockcount > 0)
			error = xfs_refcount_continue_op(rcur, ri, bno);
		break;
	case XFS_REFCOUNT_DECREASE:
		error = xfs_refcount_adjust(rcur, &bno, &ri->ri_blockcount,
				XFS_REFCOUNT_ADJUST_DECREASE);
		if (error)
			return error;
		if (ri->ri_blockcount > 0)
			error = xfs_refcount_continue_op(rcur, ri, bno);
		break;
	case XFS_REFCOUNT_ALLOC_COW:
		error = __xfs_refcount_cow_alloc(rcur, bno, ri->ri_blockcount);
		if (error)
			return error;
		ri->ri_blockcount = 0;
		break;
	case XFS_REFCOUNT_FREE_COW:
		error = __xfs_refcount_cow_free(rcur, bno, ri->ri_blockcount);
		if (error)
			return error;
		ri->ri_blockcount = 0;
		break;
	default:
		ASSERT(0);
		return -EFSCORRUPTED;
	}
	if (!error && ri->ri_blockcount > 0)
		trace_xfs_refcount_finish_one_leftover(mp, ri);
	return error;
}

/*
 * Record a refcount intent for later processing.
 */
static void
__xfs_refcount_add(
	struct xfs_trans		*tp,
	enum xfs_refcount_intent_type	type,
	xfs_fsblock_t			startblock,
	xfs_extlen_t			blockcount)
{
	struct xfs_refcount_intent	*ri;

	ri = kmem_cache_alloc(xfs_refcount_intent_cache,
			GFP_KERNEL | __GFP_NOFAIL);
	INIT_LIST_HEAD(&ri->ri_list);
	ri->ri_type = type;
	ri->ri_startblock = startblock;
	ri->ri_blockcount = blockcount;

	xfs_refcount_defer_add(tp, ri);
}

/*
 * Increase the reference count of the blocks backing a file's extent.
 */
void
xfs_refcount_increase_extent(
	struct xfs_trans		*tp,
	struct xfs_bmbt_irec		*PREV)
{
	if (!xfs_has_reflink(tp->t_mountp))
		return;

	__xfs_refcount_add(tp, XFS_REFCOUNT_INCREASE, PREV->br_startblock,
			PREV->br_blockcount);
}

/*
 * Decrease the reference count of the blocks backing a file's extent.
 */
void
xfs_refcount_decrease_extent(
	struct xfs_trans		*tp,
	struct xfs_bmbt_irec		*PREV)
{
	if (!xfs_has_reflink(tp->t_mountp))
		return;

	__xfs_refcount_add(tp, XFS_REFCOUNT_DECREASE, PREV->br_startblock,
			PREV->br_blockcount);
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

	trace_xfs_refcount_find_shared(cur, agbno, aglen);

	/* By default, skip the whole range */
	*fbno = NULLAGBLOCK;
	*flen = 0;

	/* Try to find a refcount extent that crosses the start */
	error = xfs_refcount_lookup_le(cur, XFS_REFC_DOMAIN_SHARED, agbno,
			&have);
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
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}
	if (tmp.rc_domain != XFS_REFC_DOMAIN_SHARED)
		goto done;

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
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		if (tmp.rc_domain != XFS_REFC_DOMAIN_SHARED)
			goto done;
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
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		if (tmp.rc_domain != XFS_REFC_DOMAIN_SHARED ||
		    tmp.rc_startblock >= agbno + aglen ||
		    tmp.rc_startblock != *fbno + *flen)
			break;
		*flen = min(*flen + tmp.rc_blockcount, agbno + aglen - *fbno);
	}

done:
	trace_xfs_refcount_find_shared_result(cur, *fbno, *flen);

out_error:
	if (error)
		trace_xfs_refcount_find_shared_error(cur, error, _RET_IP_);
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
	enum xfs_refc_adjust_op	adj)
{
	struct xfs_refcount_irec	ext, tmp;
	int				error;
	int				found_rec, found_tmp;

	if (aglen == 0)
		return 0;

	/* Find any overlapping refcount records */
	error = xfs_refcount_lookup_ge(cur, XFS_REFC_DOMAIN_COW, agbno,
			&found_rec);
	if (error)
		goto out_error;
	error = xfs_refcount_get_rec(cur, &ext, &found_rec);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(cur->bc_mp, found_rec &&
				ext.rc_domain != XFS_REFC_DOMAIN_COW)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}
	if (!found_rec) {
		ext.rc_startblock = cur->bc_mp->m_sb.sb_agblocks;
		ext.rc_blockcount = 0;
		ext.rc_refcount = 0;
		ext.rc_domain = XFS_REFC_DOMAIN_COW;
	}

	switch (adj) {
	case XFS_REFCOUNT_ADJUST_COW_ALLOC:
		/* Adding a CoW reservation, there should be nothing here. */
		if (XFS_IS_CORRUPT(cur->bc_mp,
				   agbno + aglen > ext.rc_startblock)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}

		tmp.rc_startblock = agbno;
		tmp.rc_blockcount = aglen;
		tmp.rc_refcount = 1;
		tmp.rc_domain = XFS_REFC_DOMAIN_COW;

		trace_xfs_refcount_modify_extent(cur, &tmp);

		error = xfs_refcount_insert(cur, &tmp,
				&found_tmp);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(cur->bc_mp, found_tmp != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		break;
	case XFS_REFCOUNT_ADJUST_COW_FREE:
		/* Removing a CoW reservation, there should be one extent. */
		if (XFS_IS_CORRUPT(cur->bc_mp, ext.rc_startblock != agbno)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		if (XFS_IS_CORRUPT(cur->bc_mp, ext.rc_blockcount != aglen)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		if (XFS_IS_CORRUPT(cur->bc_mp, ext.rc_refcount != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}

		ext.rc_refcount = 0;
		trace_xfs_refcount_modify_extent(cur, &ext);
		error = xfs_refcount_delete(cur, &found_rec);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(cur->bc_mp, found_rec != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		break;
	default:
		ASSERT(0);
	}

	return error;
out_error:
	trace_xfs_refcount_modify_extent_error(cur, error, _RET_IP_);
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
	enum xfs_refc_adjust_op	adj)
{
	bool			shape_changed;
	int			error;

	/*
	 * Ensure that no rcextents cross the boundary of the adjustment range.
	 */
	error = xfs_refcount_split_extent(cur, XFS_REFC_DOMAIN_COW,
			agbno, &shape_changed);
	if (error)
		goto out_error;

	error = xfs_refcount_split_extent(cur, XFS_REFC_DOMAIN_COW,
			agbno + aglen, &shape_changed);
	if (error)
		goto out_error;

	/*
	 * Try to merge with the left or right extents of the range.
	 */
	error = xfs_refcount_merge_extents(cur, XFS_REFC_DOMAIN_COW, &agbno,
			&aglen, adj, &shape_changed);
	if (error)
		goto out_error;

	/* Now that we've taken care of the ends, adjust the middle extents */
	error = xfs_refcount_adjust_cow_extents(cur, agbno, aglen, adj);
	if (error)
		goto out_error;

	return 0;

out_error:
	trace_xfs_refcount_adjust_cow_error(cur, error, _RET_IP_);
	return error;
}

/*
 * Record a CoW allocation in the refcount btree.
 */
STATIC int
__xfs_refcount_cow_alloc(
	struct xfs_btree_cur	*rcur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen)
{
	trace_xfs_refcount_cow_increase(rcur, agbno, aglen);

	/* Add refcount btree reservation */
	return xfs_refcount_adjust_cow(rcur, agbno, aglen,
			XFS_REFCOUNT_ADJUST_COW_ALLOC);
}

/*
 * Remove a CoW allocation from the refcount btree.
 */
STATIC int
__xfs_refcount_cow_free(
	struct xfs_btree_cur	*rcur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen)
{
	trace_xfs_refcount_cow_decrease(rcur, agbno, aglen);

	/* Remove refcount btree reservation */
	return xfs_refcount_adjust_cow(rcur, agbno, aglen,
			XFS_REFCOUNT_ADJUST_COW_FREE);
}

/* Record a CoW staging extent in the refcount btree. */
void
xfs_refcount_alloc_cow_extent(
	struct xfs_trans		*tp,
	xfs_fsblock_t			fsb,
	xfs_extlen_t			len)
{
	struct xfs_mount		*mp = tp->t_mountp;

	if (!xfs_has_reflink(mp))
		return;

	__xfs_refcount_add(tp, XFS_REFCOUNT_ALLOC_COW, fsb, len);

	/* Add rmap entry */
	xfs_rmap_alloc_extent(tp, XFS_FSB_TO_AGNO(mp, fsb),
			XFS_FSB_TO_AGBNO(mp, fsb), len, XFS_RMAP_OWN_COW);
}

/* Forget a CoW staging event in the refcount btree. */
void
xfs_refcount_free_cow_extent(
	struct xfs_trans		*tp,
	xfs_fsblock_t			fsb,
	xfs_extlen_t			len)
{
	struct xfs_mount		*mp = tp->t_mountp;

	if (!xfs_has_reflink(mp))
		return;

	/* Remove rmap entry */
	xfs_rmap_free_extent(tp, XFS_FSB_TO_AGNO(mp, fsb),
			XFS_FSB_TO_AGBNO(mp, fsb), len, XFS_RMAP_OWN_COW);
	__xfs_refcount_add(tp, XFS_REFCOUNT_FREE_COW, fsb, len);
}

struct xfs_refcount_recovery {
	struct list_head		rr_list;
	struct xfs_refcount_irec	rr_rrec;
};

/* Stuff an extent on the recovery list. */
STATIC int
xfs_refcount_recover_extent(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	struct list_head		*debris = priv;
	struct xfs_refcount_recovery	*rr;

	if (XFS_IS_CORRUPT(cur->bc_mp,
			   be32_to_cpu(rec->refc.rc_refcount) != 1)) {
		xfs_btree_mark_sick(cur);
		return -EFSCORRUPTED;
	}

	rr = kmalloc(sizeof(struct xfs_refcount_recovery),
			GFP_KERNEL | __GFP_NOFAIL);
	INIT_LIST_HEAD(&rr->rr_list);
	xfs_refcount_btrec_to_irec(rec, &rr->rr_rrec);

	if (xfs_refcount_check_irec(to_perag(cur->bc_group), &rr->rr_rrec) !=
			NULL ||
	    XFS_IS_CORRUPT(cur->bc_mp,
			   rr->rr_rrec.rc_domain != XFS_REFC_DOMAIN_COW)) {
		xfs_btree_mark_sick(cur);
		kfree(rr);
		return -EFSCORRUPTED;
	}

	list_add_tail(&rr->rr_list, debris);
	return 0;
}

/* Find and remove leftover CoW reservations. */
int
xfs_refcount_recover_cow_leftovers(
	struct xfs_mount		*mp,
	struct xfs_perag		*pag)
{
	struct xfs_trans		*tp;
	struct xfs_btree_cur		*cur;
	struct xfs_buf			*agbp;
	struct xfs_refcount_recovery	*rr, *n;
	struct list_head		debris;
	union xfs_btree_irec		low = {
		.rc.rc_domain		= XFS_REFC_DOMAIN_COW,
	};
	union xfs_btree_irec		high = {
		.rc.rc_domain		= XFS_REFC_DOMAIN_COW,
		.rc.rc_startblock	= -1U,
	};
	xfs_fsblock_t			fsb;
	int				error;

	/* reflink filesystems mustn't have AGs larger than 2^31-1 blocks */
	BUILD_BUG_ON(XFS_MAX_CRC_AG_BLOCKS >= XFS_REFC_COWFLAG);
	if (mp->m_sb.sb_agblocks > XFS_MAX_CRC_AG_BLOCKS)
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

	error = xfs_alloc_read_agf(pag, tp, 0, &agbp);
	if (error)
		goto out_trans;
	cur = xfs_refcountbt_init_cursor(mp, tp, agbp, pag);

	/* Find all the leftover CoW staging extents. */
	error = xfs_btree_query_range(cur, &low, &high,
			xfs_refcount_recover_extent, &debris);
	xfs_btree_del_cursor(cur, error);
	xfs_trans_brelse(tp, agbp);
	xfs_trans_cancel(tp);
	if (error)
		goto out_free;

	/* Now iterate the list to free the leftovers */
	list_for_each_entry_safe(rr, n, &debris, rr_list) {
		/* Set up transaction. */
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, 0, 0, 0, &tp);
		if (error)
			goto out_free;

		/* Free the orphan record */
		fsb = xfs_agbno_to_fsb(pag, rr->rr_rrec.rc_startblock);
		xfs_refcount_free_cow_extent(tp, fsb,
				rr->rr_rrec.rc_blockcount);

		/* Free the block. */
		error = xfs_free_extent_later(tp, fsb,
				rr->rr_rrec.rc_blockcount, NULL,
				XFS_AG_RESV_NONE, 0);
		if (error)
			goto out_trans;

		error = xfs_trans_commit(tp);
		if (error)
			goto out_free;

		list_del(&rr->rr_list);
		kfree(rr);
	}

	return error;
out_trans:
	xfs_trans_cancel(tp);
out_free:
	/* Free the leftover list */
	list_for_each_entry_safe(rr, n, &debris, rr_list) {
		list_del(&rr->rr_list);
		kfree(rr);
	}
	return error;
}

/*
 * Scan part of the keyspace of the refcount records and tell us if the area
 * has no records, is fully mapped by records, or is partially filled.
 */
int
xfs_refcount_has_records(
	struct xfs_btree_cur	*cur,
	enum xfs_refc_domain	domain,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	enum xbtree_recpacking	*outcome)
{
	union xfs_btree_irec	low;
	union xfs_btree_irec	high;

	memset(&low, 0, sizeof(low));
	low.rc.rc_startblock = bno;
	memset(&high, 0xFF, sizeof(high));
	high.rc.rc_startblock = bno + len - 1;
	low.rc.rc_domain = high.rc.rc_domain = domain;

	return xfs_btree_has_records(cur, &low, &high, NULL, outcome);
}

struct xfs_refcount_query_range_info {
	xfs_refcount_query_range_fn	fn;
	void				*priv;
};

/* Format btree record and pass to our callback. */
STATIC int
xfs_refcount_query_range_helper(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	struct xfs_refcount_query_range_info	*query = priv;
	struct xfs_refcount_irec	irec;
	xfs_failaddr_t			fa;

	xfs_refcount_btrec_to_irec(rec, &irec);
	fa = xfs_refcount_check_irec(to_perag(cur->bc_group), &irec);
	if (fa)
		return xfs_refcount_complain_bad_rec(cur, fa, &irec);

	return query->fn(cur, &irec, query->priv);
}

/* Find all refcount records between two keys. */
int
xfs_refcount_query_range(
	struct xfs_btree_cur		*cur,
	const struct xfs_refcount_irec	*low_rec,
	const struct xfs_refcount_irec	*high_rec,
	xfs_refcount_query_range_fn	fn,
	void				*priv)
{
	union xfs_btree_irec		low_brec = { .rc = *low_rec };
	union xfs_btree_irec		high_brec = { .rc = *high_rec };
	struct xfs_refcount_query_range_info query = { .priv = priv, .fn = fn };

	return xfs_btree_query_range(cur, &low_brec, &high_brec,
			xfs_refcount_query_range_helper, &query);
}

int __init
xfs_refcount_intent_init_cache(void)
{
	xfs_refcount_intent_cache = kmem_cache_create("xfs_refc_intent",
			sizeof(struct xfs_refcount_intent),
			0, 0, NULL);

	return xfs_refcount_intent_cache != NULL ? 0 : -ENOMEM;
}

void
xfs_refcount_intent_destroy_cache(void)
{
	kmem_cache_destroy(xfs_refcount_intent_cache);
	xfs_refcount_intent_cache = NULL;
}
