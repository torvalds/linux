// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_sb.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_trans.h"
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_trace.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_inode.h"
#include "xfs_ag.h"
#include "xfs_health.h"
#include "xfs_rmap_item.h"

struct kmem_cache	*xfs_rmap_intent_cache;

/*
 * Lookup the first record less than or equal to [bno, len, owner, offset]
 * in the btree given by cur.
 */
int
xfs_rmap_lookup_le(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags,
	struct xfs_rmap_irec	*irec,
	int			*stat)
{
	int			get_stat = 0;
	int			error;

	cur->bc_rec.r.rm_startblock = bno;
	cur->bc_rec.r.rm_blockcount = 0;
	cur->bc_rec.r.rm_owner = owner;
	cur->bc_rec.r.rm_offset = offset;
	cur->bc_rec.r.rm_flags = flags;

	error = xfs_btree_lookup(cur, XFS_LOOKUP_LE, stat);
	if (error || !(*stat) || !irec)
		return error;

	error = xfs_rmap_get_rec(cur, irec, &get_stat);
	if (error)
		return error;
	if (!get_stat) {
		xfs_btree_mark_sick(cur);
		return -EFSCORRUPTED;
	}

	return 0;
}

/*
 * Lookup the record exactly matching [bno, len, owner, offset]
 * in the btree given by cur.
 */
int
xfs_rmap_lookup_eq(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags,
	int			*stat)
{
	cur->bc_rec.r.rm_startblock = bno;
	cur->bc_rec.r.rm_blockcount = len;
	cur->bc_rec.r.rm_owner = owner;
	cur->bc_rec.r.rm_offset = offset;
	cur->bc_rec.r.rm_flags = flags;
	return xfs_btree_lookup(cur, XFS_LOOKUP_EQ, stat);
}

/*
 * Update the record referred to by cur to the value given
 * by [bno, len, owner, offset].
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int
xfs_rmap_update(
	struct xfs_btree_cur	*cur,
	struct xfs_rmap_irec	*irec)
{
	union xfs_btree_rec	rec;
	int			error;

	trace_xfs_rmap_update(cur, irec->rm_startblock, irec->rm_blockcount,
			irec->rm_owner, irec->rm_offset, irec->rm_flags);

	rec.rmap.rm_startblock = cpu_to_be32(irec->rm_startblock);
	rec.rmap.rm_blockcount = cpu_to_be32(irec->rm_blockcount);
	rec.rmap.rm_owner = cpu_to_be64(irec->rm_owner);
	rec.rmap.rm_offset = cpu_to_be64(
			xfs_rmap_irec_offset_pack(irec));
	error = xfs_btree_update(cur, &rec);
	if (error)
		trace_xfs_rmap_update_error(cur, error, _RET_IP_);
	return error;
}

int
xfs_rmap_insert(
	struct xfs_btree_cur	*rcur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags)
{
	int			i;
	int			error;

	trace_xfs_rmap_insert(rcur, agbno, len, owner, offset, flags);

	error = xfs_rmap_lookup_eq(rcur, agbno, len, owner, offset, flags, &i);
	if (error)
		goto done;
	if (XFS_IS_CORRUPT(rcur->bc_mp, i != 0)) {
		xfs_btree_mark_sick(rcur);
		error = -EFSCORRUPTED;
		goto done;
	}

	rcur->bc_rec.r.rm_startblock = agbno;
	rcur->bc_rec.r.rm_blockcount = len;
	rcur->bc_rec.r.rm_owner = owner;
	rcur->bc_rec.r.rm_offset = offset;
	rcur->bc_rec.r.rm_flags = flags;
	error = xfs_btree_insert(rcur, &i);
	if (error)
		goto done;
	if (XFS_IS_CORRUPT(rcur->bc_mp, i != 1)) {
		xfs_btree_mark_sick(rcur);
		error = -EFSCORRUPTED;
		goto done;
	}
done:
	if (error)
		trace_xfs_rmap_insert_error(rcur, error, _RET_IP_);
	return error;
}

STATIC int
xfs_rmap_delete(
	struct xfs_btree_cur	*rcur,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags)
{
	int			i;
	int			error;

	trace_xfs_rmap_delete(rcur, agbno, len, owner, offset, flags);

	error = xfs_rmap_lookup_eq(rcur, agbno, len, owner, offset, flags, &i);
	if (error)
		goto done;
	if (XFS_IS_CORRUPT(rcur->bc_mp, i != 1)) {
		xfs_btree_mark_sick(rcur);
		error = -EFSCORRUPTED;
		goto done;
	}

	error = xfs_btree_delete(rcur, &i);
	if (error)
		goto done;
	if (XFS_IS_CORRUPT(rcur->bc_mp, i != 1)) {
		xfs_btree_mark_sick(rcur);
		error = -EFSCORRUPTED;
		goto done;
	}
done:
	if (error)
		trace_xfs_rmap_delete_error(rcur, error, _RET_IP_);
	return error;
}

/* Convert an internal btree record to an rmap record. */
xfs_failaddr_t
xfs_rmap_btrec_to_irec(
	const union xfs_btree_rec	*rec,
	struct xfs_rmap_irec		*irec)
{
	irec->rm_startblock = be32_to_cpu(rec->rmap.rm_startblock);
	irec->rm_blockcount = be32_to_cpu(rec->rmap.rm_blockcount);
	irec->rm_owner = be64_to_cpu(rec->rmap.rm_owner);
	return xfs_rmap_irec_offset_unpack(be64_to_cpu(rec->rmap.rm_offset),
			irec);
}

/* Simple checks for rmap records. */
xfs_failaddr_t
xfs_rmap_check_irec(
	struct xfs_perag		*pag,
	const struct xfs_rmap_irec	*irec)
{
	struct xfs_mount		*mp = pag->pag_mount;
	bool				is_inode;
	bool				is_unwritten;
	bool				is_bmbt;
	bool				is_attr;

	if (irec->rm_blockcount == 0)
		return __this_address;
	if (irec->rm_startblock <= XFS_AGFL_BLOCK(mp)) {
		if (irec->rm_owner != XFS_RMAP_OWN_FS)
			return __this_address;
		if (irec->rm_blockcount != XFS_AGFL_BLOCK(mp) + 1)
			return __this_address;
	} else {
		/* check for valid extent range, including overflow */
		if (!xfs_verify_agbext(pag, irec->rm_startblock,
					    irec->rm_blockcount))
			return __this_address;
	}

	if (!(xfs_verify_ino(mp, irec->rm_owner) ||
	      (irec->rm_owner <= XFS_RMAP_OWN_FS &&
	       irec->rm_owner >= XFS_RMAP_OWN_MIN)))
		return __this_address;

	/* Check flags. */
	is_inode = !XFS_RMAP_NON_INODE_OWNER(irec->rm_owner);
	is_bmbt = irec->rm_flags & XFS_RMAP_BMBT_BLOCK;
	is_attr = irec->rm_flags & XFS_RMAP_ATTR_FORK;
	is_unwritten = irec->rm_flags & XFS_RMAP_UNWRITTEN;

	if (is_bmbt && irec->rm_offset != 0)
		return __this_address;

	if (!is_inode && irec->rm_offset != 0)
		return __this_address;

	if (is_unwritten && (is_bmbt || !is_inode || is_attr))
		return __this_address;

	if (!is_inode && (is_bmbt || is_unwritten || is_attr))
		return __this_address;

	/* Check for a valid fork offset, if applicable. */
	if (is_inode && !is_bmbt &&
	    !xfs_verify_fileext(mp, irec->rm_offset, irec->rm_blockcount))
		return __this_address;

	return NULL;
}

static inline xfs_failaddr_t
xfs_rmap_check_btrec(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*irec)
{
	if (xfs_btree_is_mem_rmap(cur->bc_ops))
		return xfs_rmap_check_irec(cur->bc_mem.pag, irec);
	return xfs_rmap_check_irec(cur->bc_ag.pag, irec);
}

static inline int
xfs_rmap_complain_bad_rec(
	struct xfs_btree_cur		*cur,
	xfs_failaddr_t			fa,
	const struct xfs_rmap_irec	*irec)
{
	struct xfs_mount		*mp = cur->bc_mp;

	if (xfs_btree_is_mem_rmap(cur->bc_ops))
		xfs_warn(mp,
 "In-Memory Reverse Mapping BTree record corruption detected at %pS!", fa);
	else
		xfs_warn(mp,
 "Reverse Mapping BTree record corruption in AG %d detected at %pS!",
			cur->bc_ag.pag->pag_agno, fa);
	xfs_warn(mp,
		"Owner 0x%llx, flags 0x%x, start block 0x%x block count 0x%x",
		irec->rm_owner, irec->rm_flags, irec->rm_startblock,
		irec->rm_blockcount);
	xfs_btree_mark_sick(cur);
	return -EFSCORRUPTED;
}

/*
 * Get the data from the pointed-to record.
 */
int
xfs_rmap_get_rec(
	struct xfs_btree_cur	*cur,
	struct xfs_rmap_irec	*irec,
	int			*stat)
{
	union xfs_btree_rec	*rec;
	xfs_failaddr_t		fa;
	int			error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (error || !*stat)
		return error;

	fa = xfs_rmap_btrec_to_irec(rec, irec);
	if (!fa)
		fa = xfs_rmap_check_btrec(cur, irec);
	if (fa)
		return xfs_rmap_complain_bad_rec(cur, fa, irec);

	return 0;
}

struct xfs_find_left_neighbor_info {
	struct xfs_rmap_irec	high;
	struct xfs_rmap_irec	*irec;
};

/* For each rmap given, figure out if it matches the key we want. */
STATIC int
xfs_rmap_find_left_neighbor_helper(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xfs_find_left_neighbor_info	*info = priv;

	trace_xfs_rmap_find_left_neighbor_candidate(cur, rec->rm_startblock,
			rec->rm_blockcount, rec->rm_owner, rec->rm_offset,
			rec->rm_flags);

	if (rec->rm_owner != info->high.rm_owner)
		return 0;
	if (!XFS_RMAP_NON_INODE_OWNER(rec->rm_owner) &&
	    !(rec->rm_flags & XFS_RMAP_BMBT_BLOCK) &&
	    rec->rm_offset + rec->rm_blockcount - 1 != info->high.rm_offset)
		return 0;

	*info->irec = *rec;
	return -ECANCELED;
}

/*
 * Find the record to the left of the given extent, being careful only to
 * return a match with the same owner and adjacent physical and logical
 * block ranges.
 */
STATIC int
xfs_rmap_find_left_neighbor(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags,
	struct xfs_rmap_irec	*irec,
	int			*stat)
{
	struct xfs_find_left_neighbor_info	info;
	int			found = 0;
	int			error;

	*stat = 0;
	if (bno == 0)
		return 0;
	info.high.rm_startblock = bno - 1;
	info.high.rm_owner = owner;
	if (!XFS_RMAP_NON_INODE_OWNER(owner) &&
	    !(flags & XFS_RMAP_BMBT_BLOCK)) {
		if (offset == 0)
			return 0;
		info.high.rm_offset = offset - 1;
	} else
		info.high.rm_offset = 0;
	info.high.rm_flags = flags;
	info.high.rm_blockcount = 0;
	info.irec = irec;

	trace_xfs_rmap_find_left_neighbor_query(cur, bno, 0, owner, offset,
			flags);

	/*
	 * Historically, we always used the range query to walk every reverse
	 * mapping that could possibly overlap the key that the caller asked
	 * for, and filter out the ones that don't.  That is very slow when
	 * there are a lot of records.
	 *
	 * However, there are two scenarios where the classic btree search can
	 * produce correct results -- if the index contains a record that is an
	 * exact match for the lookup key; and if there are no other records
	 * between the record we want and the key we supplied.
	 *
	 * As an optimization, try a non-overlapped lookup first.  This makes
	 * extent conversion and remap operations run a bit faster if the
	 * physical extents aren't being shared.  If we don't find what we
	 * want, we fall back to the overlapped query.
	 */
	error = xfs_rmap_lookup_le(cur, bno, owner, offset, flags, irec,
			&found);
	if (error)
		return error;
	if (found)
		error = xfs_rmap_find_left_neighbor_helper(cur, irec, &info);
	if (!error)
		error = xfs_rmap_query_range(cur, &info.high, &info.high,
				xfs_rmap_find_left_neighbor_helper, &info);
	if (error != -ECANCELED)
		return error;

	*stat = 1;
	trace_xfs_rmap_find_left_neighbor_result(cur, irec->rm_startblock,
			irec->rm_blockcount, irec->rm_owner, irec->rm_offset,
			irec->rm_flags);
	return 0;
}

/* For each rmap given, figure out if it matches the key we want. */
STATIC int
xfs_rmap_lookup_le_range_helper(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xfs_find_left_neighbor_info	*info = priv;

	trace_xfs_rmap_lookup_le_range_candidate(cur, rec->rm_startblock,
			rec->rm_blockcount, rec->rm_owner, rec->rm_offset,
			rec->rm_flags);

	if (rec->rm_owner != info->high.rm_owner)
		return 0;
	if (!XFS_RMAP_NON_INODE_OWNER(rec->rm_owner) &&
	    !(rec->rm_flags & XFS_RMAP_BMBT_BLOCK) &&
	    (rec->rm_offset > info->high.rm_offset ||
	     rec->rm_offset + rec->rm_blockcount <= info->high.rm_offset))
		return 0;

	*info->irec = *rec;
	return -ECANCELED;
}

/*
 * Find the record to the left of the given extent, being careful only to
 * return a match with the same owner and overlapping physical and logical
 * block ranges.  This is the overlapping-interval version of
 * xfs_rmap_lookup_le.
 */
int
xfs_rmap_lookup_le_range(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags,
	struct xfs_rmap_irec	*irec,
	int			*stat)
{
	struct xfs_find_left_neighbor_info	info;
	int			found = 0;
	int			error;

	info.high.rm_startblock = bno;
	info.high.rm_owner = owner;
	if (!XFS_RMAP_NON_INODE_OWNER(owner) && !(flags & XFS_RMAP_BMBT_BLOCK))
		info.high.rm_offset = offset;
	else
		info.high.rm_offset = 0;
	info.high.rm_flags = flags;
	info.high.rm_blockcount = 0;
	*stat = 0;
	info.irec = irec;

	trace_xfs_rmap_lookup_le_range(cur, bno, 0, owner, offset, flags);

	/*
	 * Historically, we always used the range query to walk every reverse
	 * mapping that could possibly overlap the key that the caller asked
	 * for, and filter out the ones that don't.  That is very slow when
	 * there are a lot of records.
	 *
	 * However, there are two scenarios where the classic btree search can
	 * produce correct results -- if the index contains a record that is an
	 * exact match for the lookup key; and if there are no other records
	 * between the record we want and the key we supplied.
	 *
	 * As an optimization, try a non-overlapped lookup first.  This makes
	 * scrub run much faster on most filesystems because bmbt records are
	 * usually an exact match for rmap records.  If we don't find what we
	 * want, we fall back to the overlapped query.
	 */
	error = xfs_rmap_lookup_le(cur, bno, owner, offset, flags, irec,
			&found);
	if (error)
		return error;
	if (found)
		error = xfs_rmap_lookup_le_range_helper(cur, irec, &info);
	if (!error)
		error = xfs_rmap_query_range(cur, &info.high, &info.high,
				xfs_rmap_lookup_le_range_helper, &info);
	if (error != -ECANCELED)
		return error;

	*stat = 1;
	trace_xfs_rmap_lookup_le_range_result(cur, irec->rm_startblock,
			irec->rm_blockcount, irec->rm_owner, irec->rm_offset,
			irec->rm_flags);
	return 0;
}

/*
 * Perform all the relevant owner checks for a removal op.  If we're doing an
 * unknown-owner removal then we have no owner information to check.
 */
static int
xfs_rmap_free_check_owner(
	struct xfs_btree_cur	*cur,
	uint64_t		ltoff,
	struct xfs_rmap_irec	*rec,
	xfs_filblks_t		len,
	uint64_t		owner,
	uint64_t		offset,
	unsigned int		flags)
{
	struct xfs_mount	*mp = cur->bc_mp;
	int			error = 0;

	if (owner == XFS_RMAP_OWN_UNKNOWN)
		return 0;

	/* Make sure the unwritten flag matches. */
	if (XFS_IS_CORRUPT(mp,
			   (flags & XFS_RMAP_UNWRITTEN) !=
			   (rec->rm_flags & XFS_RMAP_UNWRITTEN))) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out;
	}

	/* Make sure the owner matches what we expect to find in the tree. */
	if (XFS_IS_CORRUPT(mp, owner != rec->rm_owner)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out;
	}

	/* Check the offset, if necessary. */
	if (XFS_RMAP_NON_INODE_OWNER(owner))
		goto out;

	if (flags & XFS_RMAP_BMBT_BLOCK) {
		if (XFS_IS_CORRUPT(mp,
				   !(rec->rm_flags & XFS_RMAP_BMBT_BLOCK))) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out;
		}
	} else {
		if (XFS_IS_CORRUPT(mp, rec->rm_offset > offset)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out;
		}
		if (XFS_IS_CORRUPT(mp,
				   offset + len > ltoff + rec->rm_blockcount)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out;
		}
	}

out:
	return error;
}

/*
 * Find the extent in the rmap btree and remove it.
 *
 * The record we find should always be an exact match for the extent that we're
 * looking for, since we insert them into the btree without modification.
 *
 * Special Case #1: when growing the filesystem, we "free" an extent when
 * growing the last AG. This extent is new space and so it is not tracked as
 * used space in the btree. The growfs code will pass in an owner of
 * XFS_RMAP_OWN_NULL to indicate that it expected that there is no owner of this
 * extent. We verify that - the extent lookup result in a record that does not
 * overlap.
 *
 * Special Case #2: EFIs do not record the owner of the extent, so when
 * recovering EFIs from the log we pass in XFS_RMAP_OWN_UNKNOWN to tell the rmap
 * btree to ignore the owner (i.e. wildcard match) so we don't trigger
 * corruption checks during log recovery.
 */
STATIC int
xfs_rmap_unmap(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	bool				unwritten,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_rmap_irec		ltrec;
	uint64_t			ltoff;
	int				error = 0;
	int				i;
	uint64_t			owner;
	uint64_t			offset;
	unsigned int			flags;
	bool				ignore_off;

	xfs_owner_info_unpack(oinfo, &owner, &offset, &flags);
	ignore_off = XFS_RMAP_NON_INODE_OWNER(owner) ||
			(flags & XFS_RMAP_BMBT_BLOCK);
	if (unwritten)
		flags |= XFS_RMAP_UNWRITTEN;
	trace_xfs_rmap_unmap(cur, bno, len, unwritten, oinfo);

	/*
	 * We should always have a left record because there's a static record
	 * for the AG headers at rm_startblock == 0 created by mkfs/growfs that
	 * will not ever be removed from the tree.
	 */
	error = xfs_rmap_lookup_le(cur, bno, owner, offset, flags, &ltrec, &i);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	trace_xfs_rmap_lookup_le_range_result(cur, ltrec.rm_startblock,
			ltrec.rm_blockcount, ltrec.rm_owner, ltrec.rm_offset,
			ltrec.rm_flags);
	ltoff = ltrec.rm_offset;

	/*
	 * For growfs, the incoming extent must be beyond the left record we
	 * just found as it is new space and won't be used by anyone. This is
	 * just a corruption check as we don't actually do anything with this
	 * extent.  Note that we need to use >= instead of > because it might
	 * be the case that the "left" extent goes all the way to EOFS.
	 */
	if (owner == XFS_RMAP_OWN_NULL) {
		if (XFS_IS_CORRUPT(mp,
				   bno <
				   ltrec.rm_startblock + ltrec.rm_blockcount)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		goto out_done;
	}

	/*
	 * If we're doing an unknown-owner removal for EFI recovery, we expect
	 * to find the full range in the rmapbt or nothing at all.  If we
	 * don't find any rmaps overlapping either end of the range, we're
	 * done.  Hopefully this means that the EFI creator already queued
	 * (and finished) a RUI to remove the rmap.
	 */
	if (owner == XFS_RMAP_OWN_UNKNOWN &&
	    ltrec.rm_startblock + ltrec.rm_blockcount <= bno) {
		struct xfs_rmap_irec    rtrec;

		error = xfs_btree_increment(cur, 0, &i);
		if (error)
			goto out_error;
		if (i == 0)
			goto out_done;
		error = xfs_rmap_get_rec(cur, &rtrec, &i);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		if (rtrec.rm_startblock >= bno + len)
			goto out_done;
	}

	/* Make sure the extent we found covers the entire freeing range. */
	if (XFS_IS_CORRUPT(mp,
			   ltrec.rm_startblock > bno ||
			   ltrec.rm_startblock + ltrec.rm_blockcount <
			   bno + len)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	/* Check owner information. */
	error = xfs_rmap_free_check_owner(cur, ltoff, &ltrec, len, owner,
			offset, flags);
	if (error)
		goto out_error;

	if (ltrec.rm_startblock == bno && ltrec.rm_blockcount == len) {
		/* exact match, simply remove the record from rmap tree */
		trace_xfs_rmap_delete(cur, ltrec.rm_startblock,
				ltrec.rm_blockcount, ltrec.rm_owner,
				ltrec.rm_offset, ltrec.rm_flags);
		error = xfs_btree_delete(cur, &i);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
	} else if (ltrec.rm_startblock == bno) {
		/*
		 * overlap left hand side of extent: move the start, trim the
		 * length and update the current record.
		 *
		 *       ltbno                ltlen
		 * Orig:    |oooooooooooooooooooo|
		 * Freeing: |fffffffff|
		 * Result:            |rrrrrrrrrr|
		 *         bno       len
		 */
		ltrec.rm_startblock += len;
		ltrec.rm_blockcount -= len;
		if (!ignore_off)
			ltrec.rm_offset += len;
		error = xfs_rmap_update(cur, &ltrec);
		if (error)
			goto out_error;
	} else if (ltrec.rm_startblock + ltrec.rm_blockcount == bno + len) {
		/*
		 * overlap right hand side of extent: trim the length and update
		 * the current record.
		 *
		 *       ltbno                ltlen
		 * Orig:    |oooooooooooooooooooo|
		 * Freeing:            |fffffffff|
		 * Result:  |rrrrrrrrrr|
		 *                    bno       len
		 */
		ltrec.rm_blockcount -= len;
		error = xfs_rmap_update(cur, &ltrec);
		if (error)
			goto out_error;
	} else {

		/*
		 * overlap middle of extent: trim the length of the existing
		 * record to the length of the new left-extent size, increment
		 * the insertion position so we can insert a new record
		 * containing the remaining right-extent space.
		 *
		 *       ltbno                ltlen
		 * Orig:    |oooooooooooooooooooo|
		 * Freeing:       |fffffffff|
		 * Result:  |rrrrr|         |rrrr|
		 *               bno       len
		 */
		xfs_extlen_t	orig_len = ltrec.rm_blockcount;

		ltrec.rm_blockcount = bno - ltrec.rm_startblock;
		error = xfs_rmap_update(cur, &ltrec);
		if (error)
			goto out_error;

		error = xfs_btree_increment(cur, 0, &i);
		if (error)
			goto out_error;

		cur->bc_rec.r.rm_startblock = bno + len;
		cur->bc_rec.r.rm_blockcount = orig_len - len -
						     ltrec.rm_blockcount;
		cur->bc_rec.r.rm_owner = ltrec.rm_owner;
		if (ignore_off)
			cur->bc_rec.r.rm_offset = 0;
		else
			cur->bc_rec.r.rm_offset = offset + len;
		cur->bc_rec.r.rm_flags = flags;
		trace_xfs_rmap_insert(cur, cur->bc_rec.r.rm_startblock,
				cur->bc_rec.r.rm_blockcount,
				cur->bc_rec.r.rm_owner,
				cur->bc_rec.r.rm_offset,
				cur->bc_rec.r.rm_flags);
		error = xfs_btree_insert(cur, &i);
		if (error)
			goto out_error;
	}

out_done:
	trace_xfs_rmap_unmap_done(cur, bno, len, unwritten, oinfo);
out_error:
	if (error)
		trace_xfs_rmap_unmap_error(cur, error, _RET_IP_);
	return error;
}

#ifdef CONFIG_XFS_LIVE_HOOKS
/*
 * Use a static key here to reduce the overhead of rmapbt live updates.  If
 * the compiler supports jump labels, the static branch will be replaced by a
 * nop sled when there are no hook users.  Online fsck is currently the only
 * caller, so this is a reasonable tradeoff.
 *
 * Note: Patching the kernel code requires taking the cpu hotplug lock.  Other
 * parts of the kernel allocate memory with that lock held, which means that
 * XFS callers cannot hold any locks that might be used by memory reclaim or
 * writeback when calling the static_branch_{inc,dec} functions.
 */
DEFINE_STATIC_XFS_HOOK_SWITCH(xfs_rmap_hooks_switch);

void
xfs_rmap_hook_disable(void)
{
	xfs_hooks_switch_off(&xfs_rmap_hooks_switch);
}

void
xfs_rmap_hook_enable(void)
{
	xfs_hooks_switch_on(&xfs_rmap_hooks_switch);
}

/* Call downstream hooks for a reverse mapping update. */
static inline void
xfs_rmap_update_hook(
	struct xfs_trans		*tp,
	struct xfs_perag		*pag,
	enum xfs_rmap_intent_type	op,
	xfs_agblock_t			startblock,
	xfs_extlen_t			blockcount,
	bool				unwritten,
	const struct xfs_owner_info	*oinfo)
{
	if (xfs_hooks_switched_on(&xfs_rmap_hooks_switch)) {
		struct xfs_rmap_update_params	p = {
			.startblock	= startblock,
			.blockcount	= blockcount,
			.unwritten	= unwritten,
			.oinfo		= *oinfo, /* struct copy */
		};

		if (pag)
			xfs_hooks_call(&pag->pag_rmap_update_hooks, op, &p);
	}
}

/* Call the specified function during a reverse mapping update. */
int
xfs_rmap_hook_add(
	struct xfs_perag	*pag,
	struct xfs_rmap_hook	*hook)
{
	return xfs_hooks_add(&pag->pag_rmap_update_hooks, &hook->rmap_hook);
}

/* Stop calling the specified function during a reverse mapping update. */
void
xfs_rmap_hook_del(
	struct xfs_perag	*pag,
	struct xfs_rmap_hook	*hook)
{
	xfs_hooks_del(&pag->pag_rmap_update_hooks, &hook->rmap_hook);
}

/* Configure rmap update hook functions. */
void
xfs_rmap_hook_setup(
	struct xfs_rmap_hook	*hook,
	notifier_fn_t		mod_fn)
{
	xfs_hook_setup(&hook->rmap_hook, mod_fn);
}
#else
# define xfs_rmap_update_hook(t, p, o, s, b, u, oi)	do { } while (0)
#endif /* CONFIG_XFS_LIVE_HOOKS */

/*
 * Remove a reference to an extent in the rmap btree.
 */
int
xfs_rmap_free(
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	struct xfs_perag		*pag,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_btree_cur		*cur;
	int				error;

	if (!xfs_has_rmapbt(mp))
		return 0;

	cur = xfs_rmapbt_init_cursor(mp, tp, agbp, pag);
	xfs_rmap_update_hook(tp, pag, XFS_RMAP_UNMAP, bno, len, false, oinfo);
	error = xfs_rmap_unmap(cur, bno, len, false, oinfo);

	xfs_btree_del_cursor(cur, error);
	return error;
}

/*
 * A mergeable rmap must have the same owner and the same values for
 * the unwritten, attr_fork, and bmbt flags.  The startblock and
 * offset are checked separately.
 */
static bool
xfs_rmap_is_mergeable(
	struct xfs_rmap_irec	*irec,
	uint64_t		owner,
	unsigned int		flags)
{
	if (irec->rm_owner == XFS_RMAP_OWN_NULL)
		return false;
	if (irec->rm_owner != owner)
		return false;
	if ((flags & XFS_RMAP_UNWRITTEN) ^
	    (irec->rm_flags & XFS_RMAP_UNWRITTEN))
		return false;
	if ((flags & XFS_RMAP_ATTR_FORK) ^
	    (irec->rm_flags & XFS_RMAP_ATTR_FORK))
		return false;
	if ((flags & XFS_RMAP_BMBT_BLOCK) ^
	    (irec->rm_flags & XFS_RMAP_BMBT_BLOCK))
		return false;
	return true;
}

/*
 * When we allocate a new block, the first thing we do is add a reference to
 * the extent in the rmap btree. This takes the form of a [agbno, length,
 * owner, offset] record.  Flags are encoded in the high bits of the offset
 * field.
 */
STATIC int
xfs_rmap_map(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	bool				unwritten,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_rmap_irec		ltrec;
	struct xfs_rmap_irec		gtrec;
	int				have_gt;
	int				have_lt;
	int				error = 0;
	int				i;
	uint64_t			owner;
	uint64_t			offset;
	unsigned int			flags = 0;
	bool				ignore_off;

	xfs_owner_info_unpack(oinfo, &owner, &offset, &flags);
	ASSERT(owner != 0);
	ignore_off = XFS_RMAP_NON_INODE_OWNER(owner) ||
			(flags & XFS_RMAP_BMBT_BLOCK);
	if (unwritten)
		flags |= XFS_RMAP_UNWRITTEN;
	trace_xfs_rmap_map(cur, bno, len, unwritten, oinfo);
	ASSERT(!xfs_rmap_should_skip_owner_update(oinfo));

	/*
	 * For the initial lookup, look for an exact match or the left-adjacent
	 * record for our insertion point. This will also give us the record for
	 * start block contiguity tests.
	 */
	error = xfs_rmap_lookup_le(cur, bno, owner, offset, flags, &ltrec,
			&have_lt);
	if (error)
		goto out_error;
	if (have_lt) {
		trace_xfs_rmap_lookup_le_range_result(cur, ltrec.rm_startblock,
				ltrec.rm_blockcount, ltrec.rm_owner,
				ltrec.rm_offset, ltrec.rm_flags);

		if (!xfs_rmap_is_mergeable(&ltrec, owner, flags))
			have_lt = 0;
	}

	if (XFS_IS_CORRUPT(mp,
			   have_lt != 0 &&
			   ltrec.rm_startblock + ltrec.rm_blockcount > bno)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	/*
	 * Increment the cursor to see if we have a right-adjacent record to our
	 * insertion point. This will give us the record for end block
	 * contiguity tests.
	 */
	error = xfs_btree_increment(cur, 0, &have_gt);
	if (error)
		goto out_error;
	if (have_gt) {
		error = xfs_rmap_get_rec(cur, &gtrec, &have_gt);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(mp, have_gt != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		if (XFS_IS_CORRUPT(mp, bno + len > gtrec.rm_startblock)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		trace_xfs_rmap_find_right_neighbor_result(cur,
				gtrec.rm_startblock, gtrec.rm_blockcount,
				gtrec.rm_owner, gtrec.rm_offset,
				gtrec.rm_flags);
		if (!xfs_rmap_is_mergeable(&gtrec, owner, flags))
			have_gt = 0;
	}

	/*
	 * Note: cursor currently points one record to the right of ltrec, even
	 * if there is no record in the tree to the right.
	 */
	if (have_lt &&
	    ltrec.rm_startblock + ltrec.rm_blockcount == bno &&
	    (ignore_off || ltrec.rm_offset + ltrec.rm_blockcount == offset)) {
		/*
		 * left edge contiguous, merge into left record.
		 *
		 *       ltbno     ltlen
		 * orig:   |ooooooooo|
		 * adding:           |aaaaaaaaa|
		 * result: |rrrrrrrrrrrrrrrrrrr|
		 *                  bno       len
		 */
		ltrec.rm_blockcount += len;
		if (have_gt &&
		    bno + len == gtrec.rm_startblock &&
		    (ignore_off || offset + len == gtrec.rm_offset) &&
		    (unsigned long)ltrec.rm_blockcount + len +
				gtrec.rm_blockcount <= XFS_RMAP_LEN_MAX) {
			/*
			 * right edge also contiguous, delete right record
			 * and merge into left record.
			 *
			 *       ltbno     ltlen    gtbno     gtlen
			 * orig:   |ooooooooo|         |ooooooooo|
			 * adding:           |aaaaaaaaa|
			 * result: |rrrrrrrrrrrrrrrrrrrrrrrrrrrrr|
			 */
			ltrec.rm_blockcount += gtrec.rm_blockcount;
			trace_xfs_rmap_delete(cur, gtrec.rm_startblock,
					gtrec.rm_blockcount, gtrec.rm_owner,
					gtrec.rm_offset, gtrec.rm_flags);
			error = xfs_btree_delete(cur, &i);
			if (error)
				goto out_error;
			if (XFS_IS_CORRUPT(mp, i != 1)) {
				xfs_btree_mark_sick(cur);
				error = -EFSCORRUPTED;
				goto out_error;
			}
		}

		/* point the cursor back to the left record and update */
		error = xfs_btree_decrement(cur, 0, &have_gt);
		if (error)
			goto out_error;
		error = xfs_rmap_update(cur, &ltrec);
		if (error)
			goto out_error;
	} else if (have_gt &&
		   bno + len == gtrec.rm_startblock &&
		   (ignore_off || offset + len == gtrec.rm_offset)) {
		/*
		 * right edge contiguous, merge into right record.
		 *
		 *                 gtbno     gtlen
		 * Orig:             |ooooooooo|
		 * adding: |aaaaaaaaa|
		 * Result: |rrrrrrrrrrrrrrrrrrr|
		 *        bno       len
		 */
		gtrec.rm_startblock = bno;
		gtrec.rm_blockcount += len;
		if (!ignore_off)
			gtrec.rm_offset = offset;
		error = xfs_rmap_update(cur, &gtrec);
		if (error)
			goto out_error;
	} else {
		/*
		 * no contiguous edge with identical owner, insert
		 * new record at current cursor position.
		 */
		cur->bc_rec.r.rm_startblock = bno;
		cur->bc_rec.r.rm_blockcount = len;
		cur->bc_rec.r.rm_owner = owner;
		cur->bc_rec.r.rm_offset = offset;
		cur->bc_rec.r.rm_flags = flags;
		trace_xfs_rmap_insert(cur, bno, len, owner, offset, flags);
		error = xfs_btree_insert(cur, &i);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
	}

	trace_xfs_rmap_map_done(cur, bno, len, unwritten, oinfo);
out_error:
	if (error)
		trace_xfs_rmap_map_error(cur, error, _RET_IP_);
	return error;
}

/*
 * Add a reference to an extent in the rmap btree.
 */
int
xfs_rmap_alloc(
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	struct xfs_perag		*pag,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_btree_cur		*cur;
	int				error;

	if (!xfs_has_rmapbt(mp))
		return 0;

	cur = xfs_rmapbt_init_cursor(mp, tp, agbp, pag);
	xfs_rmap_update_hook(tp, pag, XFS_RMAP_MAP, bno, len, false, oinfo);
	error = xfs_rmap_map(cur, bno, len, false, oinfo);

	xfs_btree_del_cursor(cur, error);
	return error;
}

#define RMAP_LEFT_CONTIG	(1 << 0)
#define RMAP_RIGHT_CONTIG	(1 << 1)
#define RMAP_LEFT_FILLING	(1 << 2)
#define RMAP_RIGHT_FILLING	(1 << 3)
#define RMAP_LEFT_VALID		(1 << 6)
#define RMAP_RIGHT_VALID	(1 << 7)

#define LEFT		r[0]
#define RIGHT		r[1]
#define PREV		r[2]
#define NEW		r[3]

/*
 * Convert an unwritten extent to a real extent or vice versa.
 * Does not handle overlapping extents.
 */
STATIC int
xfs_rmap_convert(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	bool				unwritten,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_rmap_irec		r[4];	/* neighbor extent entries */
						/* left is 0, right is 1, */
						/* prev is 2, new is 3 */
	uint64_t		owner;
	uint64_t		offset;
	uint64_t		new_endoff;
	unsigned int		oldext;
	unsigned int		newext;
	unsigned int		flags = 0;
	int			i;
	int			state = 0;
	int			error;

	xfs_owner_info_unpack(oinfo, &owner, &offset, &flags);
	ASSERT(!(XFS_RMAP_NON_INODE_OWNER(owner) ||
			(flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK))));
	oldext = unwritten ? XFS_RMAP_UNWRITTEN : 0;
	new_endoff = offset + len;
	trace_xfs_rmap_convert(cur, bno, len, unwritten, oinfo);

	/*
	 * For the initial lookup, look for an exact match or the left-adjacent
	 * record for our insertion point. This will also give us the record for
	 * start block contiguity tests.
	 */
	error = xfs_rmap_lookup_le(cur, bno, owner, offset, oldext, &PREV, &i);
	if (error)
		goto done;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto done;
	}

	trace_xfs_rmap_lookup_le_range_result(cur, PREV.rm_startblock,
			PREV.rm_blockcount, PREV.rm_owner, PREV.rm_offset,
			PREV.rm_flags);

	ASSERT(PREV.rm_offset <= offset);
	ASSERT(PREV.rm_offset + PREV.rm_blockcount >= new_endoff);
	ASSERT((PREV.rm_flags & XFS_RMAP_UNWRITTEN) == oldext);
	newext = ~oldext & XFS_RMAP_UNWRITTEN;

	/*
	 * Set flags determining what part of the previous oldext allocation
	 * extent is being replaced by a newext allocation.
	 */
	if (PREV.rm_offset == offset)
		state |= RMAP_LEFT_FILLING;
	if (PREV.rm_offset + PREV.rm_blockcount == new_endoff)
		state |= RMAP_RIGHT_FILLING;

	/*
	 * Decrement the cursor to see if we have a left-adjacent record to our
	 * insertion point. This will give us the record for end block
	 * contiguity tests.
	 */
	error = xfs_btree_decrement(cur, 0, &i);
	if (error)
		goto done;
	if (i) {
		state |= RMAP_LEFT_VALID;
		error = xfs_rmap_get_rec(cur, &LEFT, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		if (XFS_IS_CORRUPT(mp,
				   LEFT.rm_startblock + LEFT.rm_blockcount >
				   bno)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		trace_xfs_rmap_find_left_neighbor_result(cur,
				LEFT.rm_startblock, LEFT.rm_blockcount,
				LEFT.rm_owner, LEFT.rm_offset, LEFT.rm_flags);
		if (LEFT.rm_startblock + LEFT.rm_blockcount == bno &&
		    LEFT.rm_offset + LEFT.rm_blockcount == offset &&
		    xfs_rmap_is_mergeable(&LEFT, owner, newext))
			state |= RMAP_LEFT_CONTIG;
	}

	/*
	 * Increment the cursor to see if we have a right-adjacent record to our
	 * insertion point. This will give us the record for end block
	 * contiguity tests.
	 */
	error = xfs_btree_increment(cur, 0, &i);
	if (error)
		goto done;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto done;
	}
	error = xfs_btree_increment(cur, 0, &i);
	if (error)
		goto done;
	if (i) {
		state |= RMAP_RIGHT_VALID;
		error = xfs_rmap_get_rec(cur, &RIGHT, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		if (XFS_IS_CORRUPT(mp, bno + len > RIGHT.rm_startblock)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		trace_xfs_rmap_find_right_neighbor_result(cur,
				RIGHT.rm_startblock, RIGHT.rm_blockcount,
				RIGHT.rm_owner, RIGHT.rm_offset,
				RIGHT.rm_flags);
		if (bno + len == RIGHT.rm_startblock &&
		    offset + len == RIGHT.rm_offset &&
		    xfs_rmap_is_mergeable(&RIGHT, owner, newext))
			state |= RMAP_RIGHT_CONTIG;
	}

	/* check that left + prev + right is not too long */
	if ((state & (RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG |
			 RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG)) ==
	    (RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG |
	     RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG) &&
	    (unsigned long)LEFT.rm_blockcount + len +
	     RIGHT.rm_blockcount > XFS_RMAP_LEN_MAX)
		state &= ~RMAP_RIGHT_CONTIG;

	trace_xfs_rmap_convert_state(cur, state, _RET_IP_);

	/* reset the cursor back to PREV */
	error = xfs_rmap_lookup_le(cur, bno, owner, offset, oldext, NULL, &i);
	if (error)
		goto done;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto done;
	}

	/*
	 * Switch out based on the FILLING and CONTIG state bits.
	 */
	switch (state & (RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG |
			 RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG)) {
	case RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG |
	     RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG:
		/*
		 * Setting all of a previous oldext extent to newext.
		 * The left and right neighbors are both contiguous with new.
		 */
		error = xfs_btree_increment(cur, 0, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		trace_xfs_rmap_delete(cur, RIGHT.rm_startblock,
				RIGHT.rm_blockcount, RIGHT.rm_owner,
				RIGHT.rm_offset, RIGHT.rm_flags);
		error = xfs_btree_delete(cur, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		trace_xfs_rmap_delete(cur, PREV.rm_startblock,
				PREV.rm_blockcount, PREV.rm_owner,
				PREV.rm_offset, PREV.rm_flags);
		error = xfs_btree_delete(cur, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW = LEFT;
		NEW.rm_blockcount += PREV.rm_blockcount + RIGHT.rm_blockcount;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_RIGHT_FILLING | RMAP_LEFT_CONTIG:
		/*
		 * Setting all of a previous oldext extent to newext.
		 * The left neighbor is contiguous, the right is not.
		 */
		trace_xfs_rmap_delete(cur, PREV.rm_startblock,
				PREV.rm_blockcount, PREV.rm_owner,
				PREV.rm_offset, PREV.rm_flags);
		error = xfs_btree_delete(cur, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW = LEFT;
		NEW.rm_blockcount += PREV.rm_blockcount;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG:
		/*
		 * Setting all of a previous oldext extent to newext.
		 * The right neighbor is contiguous, the left is not.
		 */
		error = xfs_btree_increment(cur, 0, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		trace_xfs_rmap_delete(cur, RIGHT.rm_startblock,
				RIGHT.rm_blockcount, RIGHT.rm_owner,
				RIGHT.rm_offset, RIGHT.rm_flags);
		error = xfs_btree_delete(cur, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW = PREV;
		NEW.rm_blockcount = len + RIGHT.rm_blockcount;
		NEW.rm_flags = newext;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_RIGHT_FILLING:
		/*
		 * Setting all of a previous oldext extent to newext.
		 * Neither the left nor right neighbors are contiguous with
		 * the new one.
		 */
		NEW = PREV;
		NEW.rm_flags = newext;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG:
		/*
		 * Setting the first part of a previous oldext extent to newext.
		 * The left neighbor is contiguous.
		 */
		NEW = PREV;
		NEW.rm_offset += len;
		NEW.rm_startblock += len;
		NEW.rm_blockcount -= len;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			goto done;
		NEW = LEFT;
		NEW.rm_blockcount += len;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING:
		/*
		 * Setting the first part of a previous oldext extent to newext.
		 * The left neighbor is not contiguous.
		 */
		NEW = PREV;
		NEW.rm_startblock += len;
		NEW.rm_offset += len;
		NEW.rm_blockcount -= len;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		NEW.rm_startblock = bno;
		NEW.rm_owner = owner;
		NEW.rm_offset = offset;
		NEW.rm_blockcount = len;
		NEW.rm_flags = newext;
		cur->bc_rec.r = NEW;
		trace_xfs_rmap_insert(cur, bno, len, owner, offset, newext);
		error = xfs_btree_insert(cur, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		break;

	case RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG:
		/*
		 * Setting the last part of a previous oldext extent to newext.
		 * The right neighbor is contiguous with the new allocation.
		 */
		NEW = PREV;
		NEW.rm_blockcount -= len;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		error = xfs_btree_increment(cur, 0, &i);
		if (error)
			goto done;
		NEW = RIGHT;
		NEW.rm_offset = offset;
		NEW.rm_startblock = bno;
		NEW.rm_blockcount += len;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_RIGHT_FILLING:
		/*
		 * Setting the last part of a previous oldext extent to newext.
		 * The right neighbor is not contiguous.
		 */
		NEW = PREV;
		NEW.rm_blockcount -= len;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		error = xfs_rmap_lookup_eq(cur, bno, len, owner, offset,
				oldext, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 0)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_startblock = bno;
		NEW.rm_owner = owner;
		NEW.rm_offset = offset;
		NEW.rm_blockcount = len;
		NEW.rm_flags = newext;
		cur->bc_rec.r = NEW;
		trace_xfs_rmap_insert(cur, bno, len, owner, offset, newext);
		error = xfs_btree_insert(cur, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		break;

	case 0:
		/*
		 * Setting the middle part of a previous oldext extent to
		 * newext.  Contiguity is impossible here.
		 * One extent becomes three extents.
		 */
		/* new right extent - oldext */
		NEW.rm_startblock = bno + len;
		NEW.rm_owner = owner;
		NEW.rm_offset = new_endoff;
		NEW.rm_blockcount = PREV.rm_offset + PREV.rm_blockcount -
				new_endoff;
		NEW.rm_flags = PREV.rm_flags;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		/* new left extent - oldext */
		NEW = PREV;
		NEW.rm_blockcount = offset - PREV.rm_offset;
		cur->bc_rec.r = NEW;
		trace_xfs_rmap_insert(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner, NEW.rm_offset,
				NEW.rm_flags);
		error = xfs_btree_insert(cur, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		/*
		 * Reset the cursor to the position of the new extent
		 * we are about to insert as we can't trust it after
		 * the previous insert.
		 */
		error = xfs_rmap_lookup_eq(cur, bno, len, owner, offset,
				oldext, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 0)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		/* new middle extent - newext */
		cur->bc_rec.r.rm_flags &= ~XFS_RMAP_UNWRITTEN;
		cur->bc_rec.r.rm_flags |= newext;
		trace_xfs_rmap_insert(cur, bno, len, owner, offset, newext);
		error = xfs_btree_insert(cur, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		break;

	case RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG | RMAP_RIGHT_CONTIG:
	case RMAP_RIGHT_FILLING | RMAP_LEFT_CONTIG | RMAP_RIGHT_CONTIG:
	case RMAP_LEFT_FILLING | RMAP_RIGHT_CONTIG:
	case RMAP_RIGHT_FILLING | RMAP_LEFT_CONTIG:
	case RMAP_LEFT_CONTIG | RMAP_RIGHT_CONTIG:
	case RMAP_LEFT_CONTIG:
	case RMAP_RIGHT_CONTIG:
		/*
		 * These cases are all impossible.
		 */
		ASSERT(0);
	}

	trace_xfs_rmap_convert_done(cur, bno, len, unwritten, oinfo);
done:
	if (error)
		trace_xfs_rmap_convert_error(cur, error, _RET_IP_);
	return error;
}

/*
 * Convert an unwritten extent to a real extent or vice versa.  If there is no
 * possibility of overlapping extents, delegate to the simpler convert
 * function.
 */
STATIC int
xfs_rmap_convert_shared(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	bool				unwritten,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_rmap_irec		r[4];	/* neighbor extent entries */
						/* left is 0, right is 1, */
						/* prev is 2, new is 3 */
	uint64_t		owner;
	uint64_t		offset;
	uint64_t		new_endoff;
	unsigned int		oldext;
	unsigned int		newext;
	unsigned int		flags = 0;
	int			i;
	int			state = 0;
	int			error;

	xfs_owner_info_unpack(oinfo, &owner, &offset, &flags);
	ASSERT(!(XFS_RMAP_NON_INODE_OWNER(owner) ||
			(flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK))));
	oldext = unwritten ? XFS_RMAP_UNWRITTEN : 0;
	new_endoff = offset + len;
	trace_xfs_rmap_convert(cur, bno, len, unwritten, oinfo);

	/*
	 * For the initial lookup, look for and exact match or the left-adjacent
	 * record for our insertion point. This will also give us the record for
	 * start block contiguity tests.
	 */
	error = xfs_rmap_lookup_le_range(cur, bno, owner, offset, oldext,
			&PREV, &i);
	if (error)
		goto done;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto done;
	}

	ASSERT(PREV.rm_offset <= offset);
	ASSERT(PREV.rm_offset + PREV.rm_blockcount >= new_endoff);
	ASSERT((PREV.rm_flags & XFS_RMAP_UNWRITTEN) == oldext);
	newext = ~oldext & XFS_RMAP_UNWRITTEN;

	/*
	 * Set flags determining what part of the previous oldext allocation
	 * extent is being replaced by a newext allocation.
	 */
	if (PREV.rm_offset == offset)
		state |= RMAP_LEFT_FILLING;
	if (PREV.rm_offset + PREV.rm_blockcount == new_endoff)
		state |= RMAP_RIGHT_FILLING;

	/* Is there a left record that abuts our range? */
	error = xfs_rmap_find_left_neighbor(cur, bno, owner, offset, newext,
			&LEFT, &i);
	if (error)
		goto done;
	if (i) {
		state |= RMAP_LEFT_VALID;
		if (XFS_IS_CORRUPT(mp,
				   LEFT.rm_startblock + LEFT.rm_blockcount >
				   bno)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		if (xfs_rmap_is_mergeable(&LEFT, owner, newext))
			state |= RMAP_LEFT_CONTIG;
	}

	/* Is there a right record that abuts our range? */
	error = xfs_rmap_lookup_eq(cur, bno + len, len, owner, offset + len,
			newext, &i);
	if (error)
		goto done;
	if (i) {
		state |= RMAP_RIGHT_VALID;
		error = xfs_rmap_get_rec(cur, &RIGHT, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		if (XFS_IS_CORRUPT(mp, bno + len > RIGHT.rm_startblock)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		trace_xfs_rmap_find_right_neighbor_result(cur,
				RIGHT.rm_startblock, RIGHT.rm_blockcount,
				RIGHT.rm_owner, RIGHT.rm_offset,
				RIGHT.rm_flags);
		if (xfs_rmap_is_mergeable(&RIGHT, owner, newext))
			state |= RMAP_RIGHT_CONTIG;
	}

	/* check that left + prev + right is not too long */
	if ((state & (RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG |
			 RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG)) ==
	    (RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG |
	     RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG) &&
	    (unsigned long)LEFT.rm_blockcount + len +
	     RIGHT.rm_blockcount > XFS_RMAP_LEN_MAX)
		state &= ~RMAP_RIGHT_CONTIG;

	trace_xfs_rmap_convert_state(cur, state, _RET_IP_);
	/*
	 * Switch out based on the FILLING and CONTIG state bits.
	 */
	switch (state & (RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG |
			 RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG)) {
	case RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG |
	     RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG:
		/*
		 * Setting all of a previous oldext extent to newext.
		 * The left and right neighbors are both contiguous with new.
		 */
		error = xfs_rmap_delete(cur, RIGHT.rm_startblock,
				RIGHT.rm_blockcount, RIGHT.rm_owner,
				RIGHT.rm_offset, RIGHT.rm_flags);
		if (error)
			goto done;
		error = xfs_rmap_delete(cur, PREV.rm_startblock,
				PREV.rm_blockcount, PREV.rm_owner,
				PREV.rm_offset, PREV.rm_flags);
		if (error)
			goto done;
		NEW = LEFT;
		error = xfs_rmap_lookup_eq(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_blockcount += PREV.rm_blockcount + RIGHT.rm_blockcount;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_RIGHT_FILLING | RMAP_LEFT_CONTIG:
		/*
		 * Setting all of a previous oldext extent to newext.
		 * The left neighbor is contiguous, the right is not.
		 */
		error = xfs_rmap_delete(cur, PREV.rm_startblock,
				PREV.rm_blockcount, PREV.rm_owner,
				PREV.rm_offset, PREV.rm_flags);
		if (error)
			goto done;
		NEW = LEFT;
		error = xfs_rmap_lookup_eq(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_blockcount += PREV.rm_blockcount;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG:
		/*
		 * Setting all of a previous oldext extent to newext.
		 * The right neighbor is contiguous, the left is not.
		 */
		error = xfs_rmap_delete(cur, RIGHT.rm_startblock,
				RIGHT.rm_blockcount, RIGHT.rm_owner,
				RIGHT.rm_offset, RIGHT.rm_flags);
		if (error)
			goto done;
		NEW = PREV;
		error = xfs_rmap_lookup_eq(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_blockcount += RIGHT.rm_blockcount;
		NEW.rm_flags = RIGHT.rm_flags;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_RIGHT_FILLING:
		/*
		 * Setting all of a previous oldext extent to newext.
		 * Neither the left nor right neighbors are contiguous with
		 * the new one.
		 */
		NEW = PREV;
		error = xfs_rmap_lookup_eq(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_flags = newext;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG:
		/*
		 * Setting the first part of a previous oldext extent to newext.
		 * The left neighbor is contiguous.
		 */
		NEW = PREV;
		error = xfs_rmap_delete(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags);
		if (error)
			goto done;
		NEW.rm_offset += len;
		NEW.rm_startblock += len;
		NEW.rm_blockcount -= len;
		error = xfs_rmap_insert(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags);
		if (error)
			goto done;
		NEW = LEFT;
		error = xfs_rmap_lookup_eq(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_blockcount += len;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING:
		/*
		 * Setting the first part of a previous oldext extent to newext.
		 * The left neighbor is not contiguous.
		 */
		NEW = PREV;
		error = xfs_rmap_delete(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags);
		if (error)
			goto done;
		NEW.rm_offset += len;
		NEW.rm_startblock += len;
		NEW.rm_blockcount -= len;
		error = xfs_rmap_insert(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags);
		if (error)
			goto done;
		error = xfs_rmap_insert(cur, bno, len, owner, offset, newext);
		if (error)
			goto done;
		break;

	case RMAP_RIGHT_FILLING | RMAP_RIGHT_CONTIG:
		/*
		 * Setting the last part of a previous oldext extent to newext.
		 * The right neighbor is contiguous with the new allocation.
		 */
		NEW = PREV;
		error = xfs_rmap_lookup_eq(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_blockcount = offset - NEW.rm_offset;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		NEW = RIGHT;
		error = xfs_rmap_delete(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags);
		if (error)
			goto done;
		NEW.rm_offset = offset;
		NEW.rm_startblock = bno;
		NEW.rm_blockcount += len;
		error = xfs_rmap_insert(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags);
		if (error)
			goto done;
		break;

	case RMAP_RIGHT_FILLING:
		/*
		 * Setting the last part of a previous oldext extent to newext.
		 * The right neighbor is not contiguous.
		 */
		NEW = PREV;
		error = xfs_rmap_lookup_eq(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_blockcount -= len;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		error = xfs_rmap_insert(cur, bno, len, owner, offset, newext);
		if (error)
			goto done;
		break;

	case 0:
		/*
		 * Setting the middle part of a previous oldext extent to
		 * newext.  Contiguity is impossible here.
		 * One extent becomes three extents.
		 */
		/* new right extent - oldext */
		NEW.rm_startblock = bno + len;
		NEW.rm_owner = owner;
		NEW.rm_offset = new_endoff;
		NEW.rm_blockcount = PREV.rm_offset + PREV.rm_blockcount -
				new_endoff;
		NEW.rm_flags = PREV.rm_flags;
		error = xfs_rmap_insert(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner, NEW.rm_offset,
				NEW.rm_flags);
		if (error)
			goto done;
		/* new left extent - oldext */
		NEW = PREV;
		error = xfs_rmap_lookup_eq(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner,
				NEW.rm_offset, NEW.rm_flags, &i);
		if (error)
			goto done;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto done;
		}
		NEW.rm_blockcount = offset - NEW.rm_offset;
		error = xfs_rmap_update(cur, &NEW);
		if (error)
			goto done;
		/* new middle extent - newext */
		NEW.rm_startblock = bno;
		NEW.rm_blockcount = len;
		NEW.rm_owner = owner;
		NEW.rm_offset = offset;
		NEW.rm_flags = newext;
		error = xfs_rmap_insert(cur, NEW.rm_startblock,
				NEW.rm_blockcount, NEW.rm_owner, NEW.rm_offset,
				NEW.rm_flags);
		if (error)
			goto done;
		break;

	case RMAP_LEFT_FILLING | RMAP_LEFT_CONTIG | RMAP_RIGHT_CONTIG:
	case RMAP_RIGHT_FILLING | RMAP_LEFT_CONTIG | RMAP_RIGHT_CONTIG:
	case RMAP_LEFT_FILLING | RMAP_RIGHT_CONTIG:
	case RMAP_RIGHT_FILLING | RMAP_LEFT_CONTIG:
	case RMAP_LEFT_CONTIG | RMAP_RIGHT_CONTIG:
	case RMAP_LEFT_CONTIG:
	case RMAP_RIGHT_CONTIG:
		/*
		 * These cases are all impossible.
		 */
		ASSERT(0);
	}

	trace_xfs_rmap_convert_done(cur, bno, len, unwritten, oinfo);
done:
	if (error)
		trace_xfs_rmap_convert_error(cur, error, _RET_IP_);
	return error;
}

#undef	NEW
#undef	LEFT
#undef	RIGHT
#undef	PREV

/*
 * Find an extent in the rmap btree and unmap it.  For rmap extent types that
 * can overlap (data fork rmaps on reflink filesystems) we must be careful
 * that the prev/next records in the btree might belong to another owner.
 * Therefore we must use delete+insert to alter any of the key fields.
 *
 * For every other situation there can only be one owner for a given extent,
 * so we can call the regular _free function.
 */
STATIC int
xfs_rmap_unmap_shared(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	bool				unwritten,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_rmap_irec		ltrec;
	uint64_t			ltoff;
	int				error = 0;
	int				i;
	uint64_t			owner;
	uint64_t			offset;
	unsigned int			flags;

	xfs_owner_info_unpack(oinfo, &owner, &offset, &flags);
	if (unwritten)
		flags |= XFS_RMAP_UNWRITTEN;
	trace_xfs_rmap_unmap(cur, bno, len, unwritten, oinfo);

	/*
	 * We should always have a left record because there's a static record
	 * for the AG headers at rm_startblock == 0 created by mkfs/growfs that
	 * will not ever be removed from the tree.
	 */
	error = xfs_rmap_lookup_le_range(cur, bno, owner, offset, flags,
			&ltrec, &i);
	if (error)
		goto out_error;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}
	ltoff = ltrec.rm_offset;

	/* Make sure the extent we found covers the entire freeing range. */
	if (XFS_IS_CORRUPT(mp,
			   ltrec.rm_startblock > bno ||
			   ltrec.rm_startblock + ltrec.rm_blockcount <
			   bno + len)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	/* Make sure the owner matches what we expect to find in the tree. */
	if (XFS_IS_CORRUPT(mp, owner != ltrec.rm_owner)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	/* Make sure the unwritten flag matches. */
	if (XFS_IS_CORRUPT(mp,
			   (flags & XFS_RMAP_UNWRITTEN) !=
			   (ltrec.rm_flags & XFS_RMAP_UNWRITTEN))) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	/* Check the offset. */
	if (XFS_IS_CORRUPT(mp, ltrec.rm_offset > offset)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}
	if (XFS_IS_CORRUPT(mp, offset > ltoff + ltrec.rm_blockcount)) {
		xfs_btree_mark_sick(cur);
		error = -EFSCORRUPTED;
		goto out_error;
	}

	if (ltrec.rm_startblock == bno && ltrec.rm_blockcount == len) {
		/* Exact match, simply remove the record from rmap tree. */
		error = xfs_rmap_delete(cur, ltrec.rm_startblock,
				ltrec.rm_blockcount, ltrec.rm_owner,
				ltrec.rm_offset, ltrec.rm_flags);
		if (error)
			goto out_error;
	} else if (ltrec.rm_startblock == bno) {
		/*
		 * Overlap left hand side of extent: move the start, trim the
		 * length and update the current record.
		 *
		 *       ltbno                ltlen
		 * Orig:    |oooooooooooooooooooo|
		 * Freeing: |fffffffff|
		 * Result:            |rrrrrrrrrr|
		 *         bno       len
		 */

		/* Delete prev rmap. */
		error = xfs_rmap_delete(cur, ltrec.rm_startblock,
				ltrec.rm_blockcount, ltrec.rm_owner,
				ltrec.rm_offset, ltrec.rm_flags);
		if (error)
			goto out_error;

		/* Add an rmap at the new offset. */
		ltrec.rm_startblock += len;
		ltrec.rm_blockcount -= len;
		ltrec.rm_offset += len;
		error = xfs_rmap_insert(cur, ltrec.rm_startblock,
				ltrec.rm_blockcount, ltrec.rm_owner,
				ltrec.rm_offset, ltrec.rm_flags);
		if (error)
			goto out_error;
	} else if (ltrec.rm_startblock + ltrec.rm_blockcount == bno + len) {
		/*
		 * Overlap right hand side of extent: trim the length and
		 * update the current record.
		 *
		 *       ltbno                ltlen
		 * Orig:    |oooooooooooooooooooo|
		 * Freeing:            |fffffffff|
		 * Result:  |rrrrrrrrrr|
		 *                    bno       len
		 */
		error = xfs_rmap_lookup_eq(cur, ltrec.rm_startblock,
				ltrec.rm_blockcount, ltrec.rm_owner,
				ltrec.rm_offset, ltrec.rm_flags, &i);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		ltrec.rm_blockcount -= len;
		error = xfs_rmap_update(cur, &ltrec);
		if (error)
			goto out_error;
	} else {
		/*
		 * Overlap middle of extent: trim the length of the existing
		 * record to the length of the new left-extent size, increment
		 * the insertion position so we can insert a new record
		 * containing the remaining right-extent space.
		 *
		 *       ltbno                ltlen
		 * Orig:    |oooooooooooooooooooo|
		 * Freeing:       |fffffffff|
		 * Result:  |rrrrr|         |rrrr|
		 *               bno       len
		 */
		xfs_extlen_t	orig_len = ltrec.rm_blockcount;

		/* Shrink the left side of the rmap */
		error = xfs_rmap_lookup_eq(cur, ltrec.rm_startblock,
				ltrec.rm_blockcount, ltrec.rm_owner,
				ltrec.rm_offset, ltrec.rm_flags, &i);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		ltrec.rm_blockcount = bno - ltrec.rm_startblock;
		error = xfs_rmap_update(cur, &ltrec);
		if (error)
			goto out_error;

		/* Add an rmap at the new offset */
		error = xfs_rmap_insert(cur, bno + len,
				orig_len - len - ltrec.rm_blockcount,
				ltrec.rm_owner, offset + len,
				ltrec.rm_flags);
		if (error)
			goto out_error;
	}

	trace_xfs_rmap_unmap_done(cur, bno, len, unwritten, oinfo);
out_error:
	if (error)
		trace_xfs_rmap_unmap_error(cur, error, _RET_IP_);
	return error;
}

/*
 * Find an extent in the rmap btree and map it.  For rmap extent types that
 * can overlap (data fork rmaps on reflink filesystems) we must be careful
 * that the prev/next records in the btree might belong to another owner.
 * Therefore we must use delete+insert to alter any of the key fields.
 *
 * For every other situation there can only be one owner for a given extent,
 * so we can call the regular _alloc function.
 */
STATIC int
xfs_rmap_map_shared(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	bool				unwritten,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_rmap_irec		ltrec;
	struct xfs_rmap_irec		gtrec;
	int				have_gt;
	int				have_lt;
	int				error = 0;
	int				i;
	uint64_t			owner;
	uint64_t			offset;
	unsigned int			flags = 0;

	xfs_owner_info_unpack(oinfo, &owner, &offset, &flags);
	if (unwritten)
		flags |= XFS_RMAP_UNWRITTEN;
	trace_xfs_rmap_map(cur, bno, len, unwritten, oinfo);

	/* Is there a left record that abuts our range? */
	error = xfs_rmap_find_left_neighbor(cur, bno, owner, offset, flags,
			&ltrec, &have_lt);
	if (error)
		goto out_error;
	if (have_lt &&
	    !xfs_rmap_is_mergeable(&ltrec, owner, flags))
		have_lt = 0;

	/* Is there a right record that abuts our range? */
	error = xfs_rmap_lookup_eq(cur, bno + len, len, owner, offset + len,
			flags, &have_gt);
	if (error)
		goto out_error;
	if (have_gt) {
		error = xfs_rmap_get_rec(cur, &gtrec, &have_gt);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(mp, have_gt != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}
		trace_xfs_rmap_find_right_neighbor_result(cur,
				gtrec.rm_startblock, gtrec.rm_blockcount,
				gtrec.rm_owner, gtrec.rm_offset,
				gtrec.rm_flags);

		if (!xfs_rmap_is_mergeable(&gtrec, owner, flags))
			have_gt = 0;
	}

	if (have_lt &&
	    ltrec.rm_startblock + ltrec.rm_blockcount == bno &&
	    ltrec.rm_offset + ltrec.rm_blockcount == offset) {
		/*
		 * Left edge contiguous, merge into left record.
		 *
		 *       ltbno     ltlen
		 * orig:   |ooooooooo|
		 * adding:           |aaaaaaaaa|
		 * result: |rrrrrrrrrrrrrrrrrrr|
		 *                  bno       len
		 */
		ltrec.rm_blockcount += len;
		if (have_gt &&
		    bno + len == gtrec.rm_startblock &&
		    offset + len == gtrec.rm_offset) {
			/*
			 * Right edge also contiguous, delete right record
			 * and merge into left record.
			 *
			 *       ltbno     ltlen    gtbno     gtlen
			 * orig:   |ooooooooo|         |ooooooooo|
			 * adding:           |aaaaaaaaa|
			 * result: |rrrrrrrrrrrrrrrrrrrrrrrrrrrrr|
			 */
			ltrec.rm_blockcount += gtrec.rm_blockcount;
			error = xfs_rmap_delete(cur, gtrec.rm_startblock,
					gtrec.rm_blockcount, gtrec.rm_owner,
					gtrec.rm_offset, gtrec.rm_flags);
			if (error)
				goto out_error;
		}

		/* Point the cursor back to the left record and update. */
		error = xfs_rmap_lookup_eq(cur, ltrec.rm_startblock,
				ltrec.rm_blockcount, ltrec.rm_owner,
				ltrec.rm_offset, ltrec.rm_flags, &i);
		if (error)
			goto out_error;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			xfs_btree_mark_sick(cur);
			error = -EFSCORRUPTED;
			goto out_error;
		}

		error = xfs_rmap_update(cur, &ltrec);
		if (error)
			goto out_error;
	} else if (have_gt &&
		   bno + len == gtrec.rm_startblock &&
		   offset + len == gtrec.rm_offset) {
		/*
		 * Right edge contiguous, merge into right record.
		 *
		 *                 gtbno     gtlen
		 * Orig:             |ooooooooo|
		 * adding: |aaaaaaaaa|
		 * Result: |rrrrrrrrrrrrrrrrrrr|
		 *        bno       len
		 */
		/* Delete the old record. */
		error = xfs_rmap_delete(cur, gtrec.rm_startblock,
				gtrec.rm_blockcount, gtrec.rm_owner,
				gtrec.rm_offset, gtrec.rm_flags);
		if (error)
			goto out_error;

		/* Move the start and re-add it. */
		gtrec.rm_startblock = bno;
		gtrec.rm_blockcount += len;
		gtrec.rm_offset = offset;
		error = xfs_rmap_insert(cur, gtrec.rm_startblock,
				gtrec.rm_blockcount, gtrec.rm_owner,
				gtrec.rm_offset, gtrec.rm_flags);
		if (error)
			goto out_error;
	} else {
		/*
		 * No contiguous edge with identical owner, insert
		 * new record at current cursor position.
		 */
		error = xfs_rmap_insert(cur, bno, len, owner, offset, flags);
		if (error)
			goto out_error;
	}

	trace_xfs_rmap_map_done(cur, bno, len, unwritten, oinfo);
out_error:
	if (error)
		trace_xfs_rmap_map_error(cur, error, _RET_IP_);
	return error;
}

/* Insert a raw rmap into the rmapbt. */
int
xfs_rmap_map_raw(
	struct xfs_btree_cur	*cur,
	struct xfs_rmap_irec	*rmap)
{
	struct xfs_owner_info	oinfo;

	xfs_owner_info_pack(&oinfo, rmap->rm_owner, rmap->rm_offset,
			rmap->rm_flags);

	if ((rmap->rm_flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK |
			       XFS_RMAP_UNWRITTEN)) ||
	    XFS_RMAP_NON_INODE_OWNER(rmap->rm_owner))
		return xfs_rmap_map(cur, rmap->rm_startblock,
				rmap->rm_blockcount,
				rmap->rm_flags & XFS_RMAP_UNWRITTEN,
				&oinfo);

	return xfs_rmap_map_shared(cur, rmap->rm_startblock,
			rmap->rm_blockcount,
			rmap->rm_flags & XFS_RMAP_UNWRITTEN,
			&oinfo);
}

struct xfs_rmap_query_range_info {
	xfs_rmap_query_range_fn	fn;
	void				*priv;
};

/* Format btree record and pass to our callback. */
STATIC int
xfs_rmap_query_range_helper(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	struct xfs_rmap_query_range_info	*query = priv;
	struct xfs_rmap_irec			irec;
	xfs_failaddr_t				fa;

	fa = xfs_rmap_btrec_to_irec(rec, &irec);
	if (!fa)
		fa = xfs_rmap_check_btrec(cur, &irec);
	if (fa)
		return xfs_rmap_complain_bad_rec(cur, fa, &irec);

	return query->fn(cur, &irec, query->priv);
}

/* Find all rmaps between two keys. */
int
xfs_rmap_query_range(
	struct xfs_btree_cur			*cur,
	const struct xfs_rmap_irec		*low_rec,
	const struct xfs_rmap_irec		*high_rec,
	xfs_rmap_query_range_fn			fn,
	void					*priv)
{
	union xfs_btree_irec			low_brec = { .r = *low_rec };
	union xfs_btree_irec			high_brec = { .r = *high_rec };
	struct xfs_rmap_query_range_info	query = { .priv = priv, .fn = fn };

	return xfs_btree_query_range(cur, &low_brec, &high_brec,
			xfs_rmap_query_range_helper, &query);
}

/* Find all rmaps. */
int
xfs_rmap_query_all(
	struct xfs_btree_cur			*cur,
	xfs_rmap_query_range_fn			fn,
	void					*priv)
{
	struct xfs_rmap_query_range_info	query;

	query.priv = priv;
	query.fn = fn;
	return xfs_btree_query_all(cur, xfs_rmap_query_range_helper, &query);
}

/* Commit an rmap operation into the ondisk tree. */
int
__xfs_rmap_finish_intent(
	struct xfs_btree_cur		*rcur,
	enum xfs_rmap_intent_type	op,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo,
	bool				unwritten)
{
	switch (op) {
	case XFS_RMAP_ALLOC:
	case XFS_RMAP_MAP:
		return xfs_rmap_map(rcur, bno, len, unwritten, oinfo);
	case XFS_RMAP_MAP_SHARED:
		return xfs_rmap_map_shared(rcur, bno, len, unwritten, oinfo);
	case XFS_RMAP_FREE:
	case XFS_RMAP_UNMAP:
		return xfs_rmap_unmap(rcur, bno, len, unwritten, oinfo);
	case XFS_RMAP_UNMAP_SHARED:
		return xfs_rmap_unmap_shared(rcur, bno, len, unwritten, oinfo);
	case XFS_RMAP_CONVERT:
		return xfs_rmap_convert(rcur, bno, len, !unwritten, oinfo);
	case XFS_RMAP_CONVERT_SHARED:
		return xfs_rmap_convert_shared(rcur, bno, len, !unwritten,
				oinfo);
	default:
		ASSERT(0);
		return -EFSCORRUPTED;
	}
}

/*
 * Process one of the deferred rmap operations.  We pass back the
 * btree cursor to maintain our lock on the rmapbt between calls.
 * This saves time and eliminates a buffer deadlock between the
 * superblock and the AGF because we'll always grab them in the same
 * order.
 */
int
xfs_rmap_finish_one(
	struct xfs_trans		*tp,
	struct xfs_rmap_intent		*ri,
	struct xfs_btree_cur		**pcur)
{
	struct xfs_owner_info		oinfo;
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_btree_cur		*rcur = *pcur;
	struct xfs_buf			*agbp = NULL;
	xfs_agblock_t			bno;
	bool				unwritten;
	int				error = 0;

	trace_xfs_rmap_deferred(mp, ri);

	if (XFS_TEST_ERROR(false, mp, XFS_ERRTAG_RMAP_FINISH_ONE))
		return -EIO;

	/*
	 * If we haven't gotten a cursor or the cursor AG doesn't match
	 * the startblock, get one now.
	 */
	if (rcur != NULL && rcur->bc_ag.pag != ri->ri_pag) {
		xfs_btree_del_cursor(rcur, 0);
		rcur = NULL;
		*pcur = NULL;
	}
	if (rcur == NULL) {
		/*
		 * Refresh the freelist before we start changing the
		 * rmapbt, because a shape change could cause us to
		 * allocate blocks.
		 */
		error = xfs_free_extent_fix_freelist(tp, ri->ri_pag, &agbp);
		if (error) {
			xfs_ag_mark_sick(ri->ri_pag, XFS_SICK_AG_AGFL);
			return error;
		}
		if (XFS_IS_CORRUPT(tp->t_mountp, !agbp)) {
			xfs_ag_mark_sick(ri->ri_pag, XFS_SICK_AG_AGFL);
			return -EFSCORRUPTED;
		}

		*pcur = rcur = xfs_rmapbt_init_cursor(mp, tp, agbp, ri->ri_pag);
	}

	xfs_rmap_ino_owner(&oinfo, ri->ri_owner, ri->ri_whichfork,
			ri->ri_bmap.br_startoff);
	unwritten = ri->ri_bmap.br_state == XFS_EXT_UNWRITTEN;
	bno = XFS_FSB_TO_AGBNO(rcur->bc_mp, ri->ri_bmap.br_startblock);

	error = __xfs_rmap_finish_intent(rcur, ri->ri_type, bno,
			ri->ri_bmap.br_blockcount, &oinfo, unwritten);
	if (error)
		return error;

	xfs_rmap_update_hook(tp, ri->ri_pag, ri->ri_type, bno,
			ri->ri_bmap.br_blockcount, unwritten, &oinfo);
	return 0;
}

/*
 * Don't defer an rmap if we aren't an rmap filesystem.
 */
static bool
xfs_rmap_update_is_needed(
	struct xfs_mount	*mp,
	int			whichfork)
{
	return xfs_has_rmapbt(mp) && whichfork != XFS_COW_FORK;
}

/*
 * Record a rmap intent; the list is kept sorted first by AG and then by
 * increasing age.
 */
static void
__xfs_rmap_add(
	struct xfs_trans		*tp,
	enum xfs_rmap_intent_type	type,
	uint64_t			owner,
	int				whichfork,
	struct xfs_bmbt_irec		*bmap)
{
	struct xfs_rmap_intent		*ri;

	ri = kmem_cache_alloc(xfs_rmap_intent_cache, GFP_KERNEL | __GFP_NOFAIL);
	INIT_LIST_HEAD(&ri->ri_list);
	ri->ri_type = type;
	ri->ri_owner = owner;
	ri->ri_whichfork = whichfork;
	ri->ri_bmap = *bmap;

	xfs_rmap_defer_add(tp, ri);
}

/* Map an extent into a file. */
void
xfs_rmap_map_extent(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	int			whichfork,
	struct xfs_bmbt_irec	*PREV)
{
	enum xfs_rmap_intent_type type = XFS_RMAP_MAP;

	if (!xfs_rmap_update_is_needed(tp->t_mountp, whichfork))
		return;

	if (whichfork != XFS_ATTR_FORK && xfs_is_reflink_inode(ip))
		type = XFS_RMAP_MAP_SHARED;

	__xfs_rmap_add(tp, type, ip->i_ino, whichfork, PREV);
}

/* Unmap an extent out of a file. */
void
xfs_rmap_unmap_extent(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	int			whichfork,
	struct xfs_bmbt_irec	*PREV)
{
	enum xfs_rmap_intent_type type = XFS_RMAP_UNMAP;

	if (!xfs_rmap_update_is_needed(tp->t_mountp, whichfork))
		return;

	if (whichfork != XFS_ATTR_FORK && xfs_is_reflink_inode(ip))
		type = XFS_RMAP_UNMAP_SHARED;

	__xfs_rmap_add(tp, type, ip->i_ino, whichfork, PREV);
}

/*
 * Convert a data fork extent from unwritten to real or vice versa.
 *
 * Note that tp can be NULL here as no transaction is used for COW fork
 * unwritten conversion.
 */
void
xfs_rmap_convert_extent(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	int			whichfork,
	struct xfs_bmbt_irec	*PREV)
{
	enum xfs_rmap_intent_type type = XFS_RMAP_CONVERT;

	if (!xfs_rmap_update_is_needed(mp, whichfork))
		return;

	if (whichfork != XFS_ATTR_FORK && xfs_is_reflink_inode(ip))
		type = XFS_RMAP_CONVERT_SHARED;

	__xfs_rmap_add(tp, type, ip->i_ino, whichfork, PREV);
}

/* Schedule the creation of an rmap for non-file data. */
void
xfs_rmap_alloc_extent(
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	uint64_t		owner)
{
	struct xfs_bmbt_irec	bmap;

	if (!xfs_rmap_update_is_needed(tp->t_mountp, XFS_DATA_FORK))
		return;

	bmap.br_startblock = XFS_AGB_TO_FSB(tp->t_mountp, agno, bno);
	bmap.br_blockcount = len;
	bmap.br_startoff = 0;
	bmap.br_state = XFS_EXT_NORM;

	__xfs_rmap_add(tp, XFS_RMAP_ALLOC, owner, XFS_DATA_FORK, &bmap);
}

/* Schedule the deletion of an rmap for non-file data. */
void
xfs_rmap_free_extent(
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	uint64_t		owner)
{
	struct xfs_bmbt_irec	bmap;

	if (!xfs_rmap_update_is_needed(tp->t_mountp, XFS_DATA_FORK))
		return;

	bmap.br_startblock = XFS_AGB_TO_FSB(tp->t_mountp, agno, bno);
	bmap.br_blockcount = len;
	bmap.br_startoff = 0;
	bmap.br_state = XFS_EXT_NORM;

	__xfs_rmap_add(tp, XFS_RMAP_FREE, owner, XFS_DATA_FORK, &bmap);
}

/* Compare rmap records.  Returns -1 if a < b, 1 if a > b, and 0 if equal. */
int
xfs_rmap_compare(
	const struct xfs_rmap_irec	*a,
	const struct xfs_rmap_irec	*b)
{
	__u64				oa;
	__u64				ob;

	oa = xfs_rmap_irec_offset_pack(a);
	ob = xfs_rmap_irec_offset_pack(b);

	if (a->rm_startblock < b->rm_startblock)
		return -1;
	else if (a->rm_startblock > b->rm_startblock)
		return 1;
	else if (a->rm_owner < b->rm_owner)
		return -1;
	else if (a->rm_owner > b->rm_owner)
		return 1;
	else if (oa < ob)
		return -1;
	else if (oa > ob)
		return 1;
	else
		return 0;
}

/*
 * Scan the physical storage part of the keyspace of the reverse mapping index
 * and tell us if the area has no records, is fully mapped by records, or is
 * partially filled.
 */
int
xfs_rmap_has_records(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	enum xbtree_recpacking	*outcome)
{
	union xfs_btree_key	mask = {
		.rmap.rm_startblock = cpu_to_be32(-1U),
	};
	union xfs_btree_irec	low;
	union xfs_btree_irec	high;

	memset(&low, 0, sizeof(low));
	low.r.rm_startblock = bno;
	memset(&high, 0xFF, sizeof(high));
	high.r.rm_startblock = bno + len - 1;

	return xfs_btree_has_records(cur, &low, &high, &mask, outcome);
}

struct xfs_rmap_ownercount {
	/* Owner that we're looking for. */
	struct xfs_rmap_irec	good;

	/* rmap search keys */
	struct xfs_rmap_irec	low;
	struct xfs_rmap_irec	high;

	struct xfs_rmap_matches	*results;

	/* Stop early if we find a nonmatch? */
	bool			stop_on_nonmatch;
};

/* Does this rmap represent space that can have multiple owners? */
static inline bool
xfs_rmap_shareable(
	struct xfs_mount		*mp,
	const struct xfs_rmap_irec	*rmap)
{
	if (!xfs_has_reflink(mp))
		return false;
	if (XFS_RMAP_NON_INODE_OWNER(rmap->rm_owner))
		return false;
	if (rmap->rm_flags & (XFS_RMAP_ATTR_FORK |
			      XFS_RMAP_BMBT_BLOCK))
		return false;
	return true;
}

static inline void
xfs_rmap_ownercount_init(
	struct xfs_rmap_ownercount	*roc,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo,
	struct xfs_rmap_matches		*results)
{
	memset(roc, 0, sizeof(*roc));
	roc->results = results;

	roc->low.rm_startblock = bno;
	memset(&roc->high, 0xFF, sizeof(roc->high));
	roc->high.rm_startblock = bno + len - 1;

	memset(results, 0, sizeof(*results));
	roc->good.rm_startblock = bno;
	roc->good.rm_blockcount = len;
	roc->good.rm_owner = oinfo->oi_owner;
	roc->good.rm_offset = oinfo->oi_offset;
	if (oinfo->oi_flags & XFS_OWNER_INFO_ATTR_FORK)
		roc->good.rm_flags |= XFS_RMAP_ATTR_FORK;
	if (oinfo->oi_flags & XFS_OWNER_INFO_BMBT_BLOCK)
		roc->good.rm_flags |= XFS_RMAP_BMBT_BLOCK;
}

/* Figure out if this is a match for the owner. */
STATIC int
xfs_rmap_count_owners_helper(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xfs_rmap_ownercount	*roc = priv;
	struct xfs_rmap_irec		check = *rec;
	unsigned int			keyflags;
	bool				filedata;
	int64_t				delta;

	filedata = !XFS_RMAP_NON_INODE_OWNER(check.rm_owner) &&
		   !(check.rm_flags & XFS_RMAP_BMBT_BLOCK);

	/* Trim the part of check that comes before the comparison range. */
	delta = (int64_t)roc->good.rm_startblock - check.rm_startblock;
	if (delta > 0) {
		check.rm_startblock += delta;
		check.rm_blockcount -= delta;
		if (filedata)
			check.rm_offset += delta;
	}

	/* Trim the part of check that comes after the comparison range. */
	delta = (check.rm_startblock + check.rm_blockcount) -
		(roc->good.rm_startblock + roc->good.rm_blockcount);
	if (delta > 0)
		check.rm_blockcount -= delta;

	/* Don't care about unwritten status for establishing ownership. */
	keyflags = check.rm_flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK);

	if (check.rm_startblock	== roc->good.rm_startblock &&
	    check.rm_blockcount	== roc->good.rm_blockcount &&
	    check.rm_owner	== roc->good.rm_owner &&
	    check.rm_offset	== roc->good.rm_offset &&
	    keyflags		== roc->good.rm_flags) {
		roc->results->matches++;
	} else {
		roc->results->non_owner_matches++;
		if (xfs_rmap_shareable(cur->bc_mp, &roc->good) ^
		    xfs_rmap_shareable(cur->bc_mp, &check))
			roc->results->bad_non_owner_matches++;
	}

	if (roc->results->non_owner_matches && roc->stop_on_nonmatch)
		return -ECANCELED;

	return 0;
}

/* Count the number of owners and non-owners of this range of blocks. */
int
xfs_rmap_count_owners(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo,
	struct xfs_rmap_matches		*results)
{
	struct xfs_rmap_ownercount	roc;
	int				error;

	xfs_rmap_ownercount_init(&roc, bno, len, oinfo, results);
	error = xfs_rmap_query_range(cur, &roc.low, &roc.high,
			xfs_rmap_count_owners_helper, &roc);
	if (error)
		return error;

	/*
	 * There can't be any non-owner rmaps that conflict with the given
	 * owner if we didn't find any rmaps matching the owner.
	 */
	if (!results->matches)
		results->bad_non_owner_matches = 0;

	return 0;
}

/*
 * Given an extent and some owner info, can we find records overlapping
 * the extent whose owner info does not match the given owner?
 */
int
xfs_rmap_has_other_keys(
	struct xfs_btree_cur		*cur,
	xfs_agblock_t			bno,
	xfs_extlen_t			len,
	const struct xfs_owner_info	*oinfo,
	bool				*has_other)
{
	struct xfs_rmap_matches		res;
	struct xfs_rmap_ownercount	roc;
	int				error;

	xfs_rmap_ownercount_init(&roc, bno, len, oinfo, &res);
	roc.stop_on_nonmatch = true;

	error = xfs_rmap_query_range(cur, &roc.low, &roc.high,
			xfs_rmap_count_owners_helper, &roc);
	if (error == -ECANCELED) {
		*has_other = true;
		return 0;
	}
	if (error)
		return error;

	*has_other = false;
	return 0;
}

const struct xfs_owner_info XFS_RMAP_OINFO_SKIP_UPDATE = {
	.oi_owner = XFS_RMAP_OWN_NULL,
};
const struct xfs_owner_info XFS_RMAP_OINFO_ANY_OWNER = {
	.oi_owner = XFS_RMAP_OWN_UNKNOWN,
};
const struct xfs_owner_info XFS_RMAP_OINFO_FS = {
	.oi_owner = XFS_RMAP_OWN_FS,
};
const struct xfs_owner_info XFS_RMAP_OINFO_LOG = {
	.oi_owner = XFS_RMAP_OWN_LOG,
};
const struct xfs_owner_info XFS_RMAP_OINFO_AG = {
	.oi_owner = XFS_RMAP_OWN_AG,
};
const struct xfs_owner_info XFS_RMAP_OINFO_INOBT = {
	.oi_owner = XFS_RMAP_OWN_INOBT,
};
const struct xfs_owner_info XFS_RMAP_OINFO_INODES = {
	.oi_owner = XFS_RMAP_OWN_INODES,
};
const struct xfs_owner_info XFS_RMAP_OINFO_REFC = {
	.oi_owner = XFS_RMAP_OWN_REFC,
};
const struct xfs_owner_info XFS_RMAP_OINFO_COW = {
	.oi_owner = XFS_RMAP_OWN_COW,
};

int __init
xfs_rmap_intent_init_cache(void)
{
	xfs_rmap_intent_cache = kmem_cache_create("xfs_rmap_intent",
			sizeof(struct xfs_rmap_intent),
			0, 0, NULL);

	return xfs_rmap_intent_cache != NULL ? 0 : -ENOMEM;
}

void
xfs_rmap_intent_destroy_cache(void)
{
	kmem_cache_destroy(xfs_rmap_intent_cache);
	xfs_rmap_intent_cache = NULL;
}
