// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_error.h"
#include "xfs_btree.h"
#include "xfs_rmap_btree.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_rmap.h"
#include "xfs_alloc.h"
#include "xfs_bit.h"
#include <linux/fsmap.h>
#include "xfs_fsmap.h"
#include "xfs_refcount.h"
#include "xfs_refcount_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_rtalloc.h"

/* Convert an xfs_fsmap to an fsmap. */
void
xfs_fsmap_from_internal(
	struct fsmap		*dest,
	struct xfs_fsmap	*src)
{
	dest->fmr_device = src->fmr_device;
	dest->fmr_flags = src->fmr_flags;
	dest->fmr_physical = BBTOB(src->fmr_physical);
	dest->fmr_owner = src->fmr_owner;
	dest->fmr_offset = BBTOB(src->fmr_offset);
	dest->fmr_length = BBTOB(src->fmr_length);
	dest->fmr_reserved[0] = 0;
	dest->fmr_reserved[1] = 0;
	dest->fmr_reserved[2] = 0;
}

/* Convert an fsmap to an xfs_fsmap. */
void
xfs_fsmap_to_internal(
	struct xfs_fsmap	*dest,
	struct fsmap		*src)
{
	dest->fmr_device = src->fmr_device;
	dest->fmr_flags = src->fmr_flags;
	dest->fmr_physical = BTOBBT(src->fmr_physical);
	dest->fmr_owner = src->fmr_owner;
	dest->fmr_offset = BTOBBT(src->fmr_offset);
	dest->fmr_length = BTOBBT(src->fmr_length);
}

/* Convert an fsmap owner into an rmapbt owner. */
static int
xfs_fsmap_owner_to_rmap(
	struct xfs_rmap_irec	*dest,
	struct xfs_fsmap	*src)
{
	if (!(src->fmr_flags & FMR_OF_SPECIAL_OWNER)) {
		dest->rm_owner = src->fmr_owner;
		return 0;
	}

	switch (src->fmr_owner) {
	case 0:			/* "lowest owner id possible" */
	case -1ULL:		/* "highest owner id possible" */
		dest->rm_owner = 0;
		break;
	case XFS_FMR_OWN_FREE:
		dest->rm_owner = XFS_RMAP_OWN_NULL;
		break;
	case XFS_FMR_OWN_UNKNOWN:
		dest->rm_owner = XFS_RMAP_OWN_UNKNOWN;
		break;
	case XFS_FMR_OWN_FS:
		dest->rm_owner = XFS_RMAP_OWN_FS;
		break;
	case XFS_FMR_OWN_LOG:
		dest->rm_owner = XFS_RMAP_OWN_LOG;
		break;
	case XFS_FMR_OWN_AG:
		dest->rm_owner = XFS_RMAP_OWN_AG;
		break;
	case XFS_FMR_OWN_INOBT:
		dest->rm_owner = XFS_RMAP_OWN_INOBT;
		break;
	case XFS_FMR_OWN_INODES:
		dest->rm_owner = XFS_RMAP_OWN_INODES;
		break;
	case XFS_FMR_OWN_REFC:
		dest->rm_owner = XFS_RMAP_OWN_REFC;
		break;
	case XFS_FMR_OWN_COW:
		dest->rm_owner = XFS_RMAP_OWN_COW;
		break;
	case XFS_FMR_OWN_DEFECTIVE:	/* not implemented */
		/* fall through */
	default:
		return -EINVAL;
	}
	return 0;
}

/* Convert an rmapbt owner into an fsmap owner. */
static int
xfs_fsmap_owner_from_rmap(
	struct xfs_fsmap	*dest,
	struct xfs_rmap_irec	*src)
{
	dest->fmr_flags = 0;
	if (!XFS_RMAP_NON_INODE_OWNER(src->rm_owner)) {
		dest->fmr_owner = src->rm_owner;
		return 0;
	}
	dest->fmr_flags |= FMR_OF_SPECIAL_OWNER;

	switch (src->rm_owner) {
	case XFS_RMAP_OWN_FS:
		dest->fmr_owner = XFS_FMR_OWN_FS;
		break;
	case XFS_RMAP_OWN_LOG:
		dest->fmr_owner = XFS_FMR_OWN_LOG;
		break;
	case XFS_RMAP_OWN_AG:
		dest->fmr_owner = XFS_FMR_OWN_AG;
		break;
	case XFS_RMAP_OWN_INOBT:
		dest->fmr_owner = XFS_FMR_OWN_INOBT;
		break;
	case XFS_RMAP_OWN_INODES:
		dest->fmr_owner = XFS_FMR_OWN_INODES;
		break;
	case XFS_RMAP_OWN_REFC:
		dest->fmr_owner = XFS_FMR_OWN_REFC;
		break;
	case XFS_RMAP_OWN_COW:
		dest->fmr_owner = XFS_FMR_OWN_COW;
		break;
	case XFS_RMAP_OWN_NULL:	/* "free" */
		dest->fmr_owner = XFS_FMR_OWN_FREE;
		break;
	default:
		return -EFSCORRUPTED;
	}
	return 0;
}

/* getfsmap query state */
struct xfs_getfsmap_info {
	struct xfs_fsmap_head	*head;
	xfs_fsmap_format_t	formatter;	/* formatting fn */
	void			*format_arg;	/* format buffer */
	struct xfs_buf		*agf_bp;	/* AGF, for refcount queries */
	xfs_daddr_t		next_daddr;	/* next daddr we expect */
	u64			missing_owner;	/* owner of holes */
	u32			dev;		/* device id */
	xfs_agnumber_t		agno;		/* AG number, if applicable */
	struct xfs_rmap_irec	low;		/* low rmap key */
	struct xfs_rmap_irec	high;		/* high rmap key */
	bool			last;		/* last extent? */
};

/* Associate a device with a getfsmap handler. */
struct xfs_getfsmap_dev {
	u32			dev;
	int			(*fn)(struct xfs_trans *tp,
				      struct xfs_fsmap *keys,
				      struct xfs_getfsmap_info *info);
};

/* Compare two getfsmap device handlers. */
static int
xfs_getfsmap_dev_compare(
	const void			*p1,
	const void			*p2)
{
	const struct xfs_getfsmap_dev	*d1 = p1;
	const struct xfs_getfsmap_dev	*d2 = p2;

	return d1->dev - d2->dev;
}

/* Decide if this mapping is shared. */
STATIC int
xfs_getfsmap_is_shared(
	struct xfs_trans		*tp,
	struct xfs_getfsmap_info	*info,
	struct xfs_rmap_irec		*rec,
	bool				*stat)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_btree_cur		*cur;
	xfs_agblock_t			fbno;
	xfs_extlen_t			flen;
	int				error;

	*stat = false;
	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return 0;
	/* rt files will have agno set to NULLAGNUMBER */
	if (info->agno == NULLAGNUMBER)
		return 0;

	/* Are there any shared blocks here? */
	flen = 0;
	cur = xfs_refcountbt_init_cursor(mp, tp, info->agf_bp,
			info->agno, NULL);

	error = xfs_refcount_find_shared(cur, rec->rm_startblock,
			rec->rm_blockcount, &fbno, &flen, false);

	xfs_btree_del_cursor(cur, error ? XFS_BTREE_ERROR : XFS_BTREE_NOERROR);
	if (error)
		return error;

	*stat = flen > 0;
	return 0;
}

/*
 * Format a reverse mapping for getfsmap, having translated rm_startblock
 * into the appropriate daddr units.
 */
STATIC int
xfs_getfsmap_helper(
	struct xfs_trans		*tp,
	struct xfs_getfsmap_info	*info,
	struct xfs_rmap_irec		*rec,
	xfs_daddr_t			rec_daddr)
{
	struct xfs_fsmap		fmr;
	struct xfs_mount		*mp = tp->t_mountp;
	bool				shared;
	int				error;

	if (fatal_signal_pending(current))
		return -EINTR;

	/*
	 * Filter out records that start before our startpoint, if the
	 * caller requested that.
	 */
	if (xfs_rmap_compare(rec, &info->low) < 0) {
		rec_daddr += XFS_FSB_TO_BB(mp, rec->rm_blockcount);
		if (info->next_daddr < rec_daddr)
			info->next_daddr = rec_daddr;
		return XFS_BTREE_QUERY_RANGE_CONTINUE;
	}

	/* Are we just counting mappings? */
	if (info->head->fmh_count == 0) {
		if (rec_daddr > info->next_daddr)
			info->head->fmh_entries++;

		if (info->last)
			return XFS_BTREE_QUERY_RANGE_CONTINUE;

		info->head->fmh_entries++;

		rec_daddr += XFS_FSB_TO_BB(mp, rec->rm_blockcount);
		if (info->next_daddr < rec_daddr)
			info->next_daddr = rec_daddr;
		return XFS_BTREE_QUERY_RANGE_CONTINUE;
	}

	/*
	 * If the record starts past the last physical block we saw,
	 * then we've found a gap.  Report the gap as being owned by
	 * whatever the caller specified is the missing owner.
	 */
	if (rec_daddr > info->next_daddr) {
		if (info->head->fmh_entries >= info->head->fmh_count)
			return XFS_BTREE_QUERY_RANGE_ABORT;

		fmr.fmr_device = info->dev;
		fmr.fmr_physical = info->next_daddr;
		fmr.fmr_owner = info->missing_owner;
		fmr.fmr_offset = 0;
		fmr.fmr_length = rec_daddr - info->next_daddr;
		fmr.fmr_flags = FMR_OF_SPECIAL_OWNER;
		error = info->formatter(&fmr, info->format_arg);
		if (error)
			return error;
		info->head->fmh_entries++;
	}

	if (info->last)
		goto out;

	/* Fill out the extent we found */
	if (info->head->fmh_entries >= info->head->fmh_count)
		return XFS_BTREE_QUERY_RANGE_ABORT;

	trace_xfs_fsmap_mapping(mp, info->dev, info->agno, rec);

	fmr.fmr_device = info->dev;
	fmr.fmr_physical = rec_daddr;
	error = xfs_fsmap_owner_from_rmap(&fmr, rec);
	if (error)
		return error;
	fmr.fmr_offset = XFS_FSB_TO_BB(mp, rec->rm_offset);
	fmr.fmr_length = XFS_FSB_TO_BB(mp, rec->rm_blockcount);
	if (rec->rm_flags & XFS_RMAP_UNWRITTEN)
		fmr.fmr_flags |= FMR_OF_PREALLOC;
	if (rec->rm_flags & XFS_RMAP_ATTR_FORK)
		fmr.fmr_flags |= FMR_OF_ATTR_FORK;
	if (rec->rm_flags & XFS_RMAP_BMBT_BLOCK)
		fmr.fmr_flags |= FMR_OF_EXTENT_MAP;
	if (fmr.fmr_flags == 0) {
		error = xfs_getfsmap_is_shared(tp, info, rec, &shared);
		if (error)
			return error;
		if (shared)
			fmr.fmr_flags |= FMR_OF_SHARED;
	}
	error = info->formatter(&fmr, info->format_arg);
	if (error)
		return error;
	info->head->fmh_entries++;

out:
	rec_daddr += XFS_FSB_TO_BB(mp, rec->rm_blockcount);
	if (info->next_daddr < rec_daddr)
		info->next_daddr = rec_daddr;
	return XFS_BTREE_QUERY_RANGE_CONTINUE;
}

/* Transform a rmapbt irec into a fsmap */
STATIC int
xfs_getfsmap_datadev_helper(
	struct xfs_btree_cur		*cur,
	struct xfs_rmap_irec		*rec,
	void				*priv)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_getfsmap_info	*info = priv;
	xfs_fsblock_t			fsb;
	xfs_daddr_t			rec_daddr;

	fsb = XFS_AGB_TO_FSB(mp, cur->bc_private.a.agno, rec->rm_startblock);
	rec_daddr = XFS_FSB_TO_DADDR(mp, fsb);

	return xfs_getfsmap_helper(cur->bc_tp, info, rec, rec_daddr);
}

/* Transform a bnobt irec into a fsmap */
STATIC int
xfs_getfsmap_datadev_bnobt_helper(
	struct xfs_btree_cur		*cur,
	struct xfs_alloc_rec_incore	*rec,
	void				*priv)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_getfsmap_info	*info = priv;
	struct xfs_rmap_irec		irec;
	xfs_daddr_t			rec_daddr;

	rec_daddr = XFS_AGB_TO_DADDR(mp, cur->bc_private.a.agno,
			rec->ar_startblock);

	irec.rm_startblock = rec->ar_startblock;
	irec.rm_blockcount = rec->ar_blockcount;
	irec.rm_owner = XFS_RMAP_OWN_NULL;	/* "free" */
	irec.rm_offset = 0;
	irec.rm_flags = 0;

	return xfs_getfsmap_helper(cur->bc_tp, info, &irec, rec_daddr);
}

/* Set rmap flags based on the getfsmap flags */
static void
xfs_getfsmap_set_irec_flags(
	struct xfs_rmap_irec	*irec,
	struct xfs_fsmap	*fmr)
{
	irec->rm_flags = 0;
	if (fmr->fmr_flags & FMR_OF_ATTR_FORK)
		irec->rm_flags |= XFS_RMAP_ATTR_FORK;
	if (fmr->fmr_flags & FMR_OF_EXTENT_MAP)
		irec->rm_flags |= XFS_RMAP_BMBT_BLOCK;
	if (fmr->fmr_flags & FMR_OF_PREALLOC)
		irec->rm_flags |= XFS_RMAP_UNWRITTEN;
}

/* Execute a getfsmap query against the log device. */
STATIC int
xfs_getfsmap_logdev(
	struct xfs_trans		*tp,
	struct xfs_fsmap		*keys,
	struct xfs_getfsmap_info	*info)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_rmap_irec		rmap;
	int				error;

	/* Set up search keys */
	info->low.rm_startblock = XFS_BB_TO_FSBT(mp, keys[0].fmr_physical);
	info->low.rm_offset = XFS_BB_TO_FSBT(mp, keys[0].fmr_offset);
	error = xfs_fsmap_owner_to_rmap(&info->low, keys);
	if (error)
		return error;
	info->low.rm_blockcount = 0;
	xfs_getfsmap_set_irec_flags(&info->low, &keys[0]);

	error = xfs_fsmap_owner_to_rmap(&info->high, keys + 1);
	if (error)
		return error;
	info->high.rm_startblock = -1U;
	info->high.rm_owner = ULLONG_MAX;
	info->high.rm_offset = ULLONG_MAX;
	info->high.rm_blockcount = 0;
	info->high.rm_flags = XFS_RMAP_KEY_FLAGS | XFS_RMAP_REC_FLAGS;
	info->missing_owner = XFS_FMR_OWN_FREE;

	trace_xfs_fsmap_low_key(mp, info->dev, info->agno, &info->low);
	trace_xfs_fsmap_high_key(mp, info->dev, info->agno, &info->high);

	if (keys[0].fmr_physical > 0)
		return 0;

	/* Fabricate an rmap entry for the external log device. */
	rmap.rm_startblock = 0;
	rmap.rm_blockcount = mp->m_sb.sb_logblocks;
	rmap.rm_owner = XFS_RMAP_OWN_LOG;
	rmap.rm_offset = 0;
	rmap.rm_flags = 0;

	return xfs_getfsmap_helper(tp, info, &rmap, 0);
}

#ifdef CONFIG_XFS_RT
/* Transform a rtbitmap "record" into a fsmap */
STATIC int
xfs_getfsmap_rtdev_rtbitmap_helper(
	struct xfs_trans		*tp,
	struct xfs_rtalloc_rec		*rec,
	void				*priv)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_getfsmap_info	*info = priv;
	struct xfs_rmap_irec		irec;
	xfs_daddr_t			rec_daddr;

	irec.rm_startblock = rec->ar_startext * mp->m_sb.sb_rextsize;
	rec_daddr = XFS_FSB_TO_BB(mp, irec.rm_startblock);
	irec.rm_blockcount = rec->ar_extcount * mp->m_sb.sb_rextsize;
	irec.rm_owner = XFS_RMAP_OWN_NULL;	/* "free" */
	irec.rm_offset = 0;
	irec.rm_flags = 0;

	return xfs_getfsmap_helper(tp, info, &irec, rec_daddr);
}

/* Execute a getfsmap query against the realtime device. */
STATIC int
__xfs_getfsmap_rtdev(
	struct xfs_trans		*tp,
	struct xfs_fsmap		*keys,
	int				(*query_fn)(struct xfs_trans *,
						    struct xfs_getfsmap_info *),
	struct xfs_getfsmap_info	*info)
{
	struct xfs_mount		*mp = tp->t_mountp;
	xfs_fsblock_t			start_fsb;
	xfs_fsblock_t			end_fsb;
	xfs_daddr_t			eofs;
	int				error = 0;

	eofs = XFS_FSB_TO_BB(mp, mp->m_sb.sb_rblocks);
	if (keys[0].fmr_physical >= eofs)
		return 0;
	if (keys[1].fmr_physical >= eofs)
		keys[1].fmr_physical = eofs - 1;
	start_fsb = XFS_BB_TO_FSBT(mp, keys[0].fmr_physical);
	end_fsb = XFS_BB_TO_FSB(mp, keys[1].fmr_physical);

	/* Set up search keys */
	info->low.rm_startblock = start_fsb;
	error = xfs_fsmap_owner_to_rmap(&info->low, &keys[0]);
	if (error)
		return error;
	info->low.rm_offset = XFS_BB_TO_FSBT(mp, keys[0].fmr_offset);
	info->low.rm_blockcount = 0;
	xfs_getfsmap_set_irec_flags(&info->low, &keys[0]);

	info->high.rm_startblock = end_fsb;
	error = xfs_fsmap_owner_to_rmap(&info->high, &keys[1]);
	if (error)
		return error;
	info->high.rm_offset = XFS_BB_TO_FSBT(mp, keys[1].fmr_offset);
	info->high.rm_blockcount = 0;
	xfs_getfsmap_set_irec_flags(&info->high, &keys[1]);

	trace_xfs_fsmap_low_key(mp, info->dev, info->agno, &info->low);
	trace_xfs_fsmap_high_key(mp, info->dev, info->agno, &info->high);

	return query_fn(tp, info);
}

/* Actually query the realtime bitmap. */
STATIC int
xfs_getfsmap_rtdev_rtbitmap_query(
	struct xfs_trans		*tp,
	struct xfs_getfsmap_info	*info)
{
	struct xfs_rtalloc_rec		alow;
	struct xfs_rtalloc_rec		ahigh;
	int				error;

	xfs_ilock(tp->t_mountp->m_rbmip, XFS_ILOCK_SHARED);

	alow.ar_startext = info->low.rm_startblock;
	ahigh.ar_startext = info->high.rm_startblock;
	do_div(alow.ar_startext, tp->t_mountp->m_sb.sb_rextsize);
	if (do_div(ahigh.ar_startext, tp->t_mountp->m_sb.sb_rextsize))
		ahigh.ar_startext++;
	error = xfs_rtalloc_query_range(tp, &alow, &ahigh,
			xfs_getfsmap_rtdev_rtbitmap_helper, info);
	if (error)
		goto err;

	/* Report any gaps at the end of the rtbitmap */
	info->last = true;
	error = xfs_getfsmap_rtdev_rtbitmap_helper(tp, &ahigh, info);
	if (error)
		goto err;
err:
	xfs_iunlock(tp->t_mountp->m_rbmip, XFS_ILOCK_SHARED);
	return error;
}

/* Execute a getfsmap query against the realtime device rtbitmap. */
STATIC int
xfs_getfsmap_rtdev_rtbitmap(
	struct xfs_trans		*tp,
	struct xfs_fsmap		*keys,
	struct xfs_getfsmap_info	*info)
{
	info->missing_owner = XFS_FMR_OWN_UNKNOWN;
	return __xfs_getfsmap_rtdev(tp, keys, xfs_getfsmap_rtdev_rtbitmap_query,
			info);
}
#endif /* CONFIG_XFS_RT */

/* Execute a getfsmap query against the regular data device. */
STATIC int
__xfs_getfsmap_datadev(
	struct xfs_trans		*tp,
	struct xfs_fsmap		*keys,
	struct xfs_getfsmap_info	*info,
	int				(*query_fn)(struct xfs_trans *,
						    struct xfs_getfsmap_info *,
						    struct xfs_btree_cur **,
						    void *),
	void				*priv)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_btree_cur		*bt_cur = NULL;
	xfs_fsblock_t			start_fsb;
	xfs_fsblock_t			end_fsb;
	xfs_agnumber_t			start_ag;
	xfs_agnumber_t			end_ag;
	xfs_daddr_t			eofs;
	int				error = 0;

	eofs = XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks);
	if (keys[0].fmr_physical >= eofs)
		return 0;
	if (keys[1].fmr_physical >= eofs)
		keys[1].fmr_physical = eofs - 1;
	start_fsb = XFS_DADDR_TO_FSB(mp, keys[0].fmr_physical);
	end_fsb = XFS_DADDR_TO_FSB(mp, keys[1].fmr_physical);

	/*
	 * Convert the fsmap low/high keys to AG based keys.  Initialize
	 * low to the fsmap low key and max out the high key to the end
	 * of the AG.
	 */
	info->low.rm_startblock = XFS_FSB_TO_AGBNO(mp, start_fsb);
	info->low.rm_offset = XFS_BB_TO_FSBT(mp, keys[0].fmr_offset);
	error = xfs_fsmap_owner_to_rmap(&info->low, &keys[0]);
	if (error)
		return error;
	info->low.rm_blockcount = 0;
	xfs_getfsmap_set_irec_flags(&info->low, &keys[0]);

	info->high.rm_startblock = -1U;
	info->high.rm_owner = ULLONG_MAX;
	info->high.rm_offset = ULLONG_MAX;
	info->high.rm_blockcount = 0;
	info->high.rm_flags = XFS_RMAP_KEY_FLAGS | XFS_RMAP_REC_FLAGS;

	start_ag = XFS_FSB_TO_AGNO(mp, start_fsb);
	end_ag = XFS_FSB_TO_AGNO(mp, end_fsb);

	/* Query each AG */
	for (info->agno = start_ag; info->agno <= end_ag; info->agno++) {
		/*
		 * Set the AG high key from the fsmap high key if this
		 * is the last AG that we're querying.
		 */
		if (info->agno == end_ag) {
			info->high.rm_startblock = XFS_FSB_TO_AGBNO(mp,
					end_fsb);
			info->high.rm_offset = XFS_BB_TO_FSBT(mp,
					keys[1].fmr_offset);
			error = xfs_fsmap_owner_to_rmap(&info->high, &keys[1]);
			if (error)
				goto err;
			xfs_getfsmap_set_irec_flags(&info->high, &keys[1]);
		}

		if (bt_cur) {
			xfs_btree_del_cursor(bt_cur, XFS_BTREE_NOERROR);
			bt_cur = NULL;
			xfs_trans_brelse(tp, info->agf_bp);
			info->agf_bp = NULL;
		}

		error = xfs_alloc_read_agf(mp, tp, info->agno, 0,
				&info->agf_bp);
		if (error)
			goto err;

		trace_xfs_fsmap_low_key(mp, info->dev, info->agno, &info->low);
		trace_xfs_fsmap_high_key(mp, info->dev, info->agno,
				&info->high);

		error = query_fn(tp, info, &bt_cur, priv);
		if (error)
			goto err;

		/*
		 * Set the AG low key to the start of the AG prior to
		 * moving on to the next AG.
		 */
		if (info->agno == start_ag) {
			info->low.rm_startblock = 0;
			info->low.rm_owner = 0;
			info->low.rm_offset = 0;
			info->low.rm_flags = 0;
		}
	}

	/* Report any gap at the end of the AG */
	info->last = true;
	error = query_fn(tp, info, &bt_cur, priv);
	if (error)
		goto err;

err:
	if (bt_cur)
		xfs_btree_del_cursor(bt_cur, error < 0 ? XFS_BTREE_ERROR :
							 XFS_BTREE_NOERROR);
	if (info->agf_bp) {
		xfs_trans_brelse(tp, info->agf_bp);
		info->agf_bp = NULL;
	}

	return error;
}

/* Actually query the rmap btree. */
STATIC int
xfs_getfsmap_datadev_rmapbt_query(
	struct xfs_trans		*tp,
	struct xfs_getfsmap_info	*info,
	struct xfs_btree_cur		**curpp,
	void				*priv)
{
	/* Report any gap at the end of the last AG. */
	if (info->last)
		return xfs_getfsmap_datadev_helper(*curpp, &info->high, info);

	/* Allocate cursor for this AG and query_range it. */
	*curpp = xfs_rmapbt_init_cursor(tp->t_mountp, tp, info->agf_bp,
			info->agno);
	return xfs_rmap_query_range(*curpp, &info->low, &info->high,
			xfs_getfsmap_datadev_helper, info);
}

/* Execute a getfsmap query against the regular data device rmapbt. */
STATIC int
xfs_getfsmap_datadev_rmapbt(
	struct xfs_trans		*tp,
	struct xfs_fsmap		*keys,
	struct xfs_getfsmap_info	*info)
{
	info->missing_owner = XFS_FMR_OWN_FREE;
	return __xfs_getfsmap_datadev(tp, keys, info,
			xfs_getfsmap_datadev_rmapbt_query, NULL);
}

/* Actually query the bno btree. */
STATIC int
xfs_getfsmap_datadev_bnobt_query(
	struct xfs_trans		*tp,
	struct xfs_getfsmap_info	*info,
	struct xfs_btree_cur		**curpp,
	void				*priv)
{
	struct xfs_alloc_rec_incore	*key = priv;

	/* Report any gap at the end of the last AG. */
	if (info->last)
		return xfs_getfsmap_datadev_bnobt_helper(*curpp, &key[1], info);

	/* Allocate cursor for this AG and query_range it. */
	*curpp = xfs_allocbt_init_cursor(tp->t_mountp, tp, info->agf_bp,
			info->agno, XFS_BTNUM_BNO);
	key->ar_startblock = info->low.rm_startblock;
	key[1].ar_startblock = info->high.rm_startblock;
	return xfs_alloc_query_range(*curpp, key, &key[1],
			xfs_getfsmap_datadev_bnobt_helper, info);
}

/* Execute a getfsmap query against the regular data device's bnobt. */
STATIC int
xfs_getfsmap_datadev_bnobt(
	struct xfs_trans		*tp,
	struct xfs_fsmap		*keys,
	struct xfs_getfsmap_info	*info)
{
	struct xfs_alloc_rec_incore	akeys[2];

	info->missing_owner = XFS_FMR_OWN_UNKNOWN;
	return __xfs_getfsmap_datadev(tp, keys, info,
			xfs_getfsmap_datadev_bnobt_query, &akeys[0]);
}

/* Do we recognize the device? */
STATIC bool
xfs_getfsmap_is_valid_device(
	struct xfs_mount	*mp,
	struct xfs_fsmap	*fm)
{
	if (fm->fmr_device == 0 || fm->fmr_device == UINT_MAX ||
	    fm->fmr_device == new_encode_dev(mp->m_ddev_targp->bt_dev))
		return true;
	if (mp->m_logdev_targp &&
	    fm->fmr_device == new_encode_dev(mp->m_logdev_targp->bt_dev))
		return true;
	if (mp->m_rtdev_targp &&
	    fm->fmr_device == new_encode_dev(mp->m_rtdev_targp->bt_dev))
		return true;
	return false;
}

/* Ensure that the low key is less than the high key. */
STATIC bool
xfs_getfsmap_check_keys(
	struct xfs_fsmap		*low_key,
	struct xfs_fsmap		*high_key)
{
	if (low_key->fmr_device > high_key->fmr_device)
		return false;
	if (low_key->fmr_device < high_key->fmr_device)
		return true;

	if (low_key->fmr_physical > high_key->fmr_physical)
		return false;
	if (low_key->fmr_physical < high_key->fmr_physical)
		return true;

	if (low_key->fmr_owner > high_key->fmr_owner)
		return false;
	if (low_key->fmr_owner < high_key->fmr_owner)
		return true;

	if (low_key->fmr_offset > high_key->fmr_offset)
		return false;
	if (low_key->fmr_offset < high_key->fmr_offset)
		return true;

	return false;
}

/*
 * There are only two devices if we didn't configure RT devices at build time.
 */
#ifdef CONFIG_XFS_RT
#define XFS_GETFSMAP_DEVS	3
#else
#define XFS_GETFSMAP_DEVS	2
#endif /* CONFIG_XFS_RT */

/*
 * Get filesystem's extents as described in head, and format for
 * output.  Calls formatter to fill the user's buffer until all
 * extents are mapped, until the passed-in head->fmh_count slots have
 * been filled, or until the formatter short-circuits the loop, if it
 * is tracking filled-in extents on its own.
 *
 * Key to Confusion
 * ----------------
 * There are multiple levels of keys and counters at work here:
 * xfs_fsmap_head.fmh_keys	-- low and high fsmap keys passed in;
 * 				   these reflect fs-wide sector addrs.
 * dkeys			-- fmh_keys used to query each device;
 * 				   these are fmh_keys but w/ the low key
 * 				   bumped up by fmr_length.
 * xfs_getfsmap_info.next_daddr	-- next disk addr we expect to see; this
 *				   is how we detect gaps in the fsmap
				   records and report them.
 * xfs_getfsmap_info.low/high	-- per-AG low/high keys computed from
 * 				   dkeys; used to query the metadata.
 */
int
xfs_getfsmap(
	struct xfs_mount		*mp,
	struct xfs_fsmap_head		*head,
	xfs_fsmap_format_t		formatter,
	void				*arg)
{
	struct xfs_trans		*tp = NULL;
	struct xfs_fsmap		dkeys[2];	/* per-dev keys */
	struct xfs_getfsmap_dev		handlers[XFS_GETFSMAP_DEVS];
	struct xfs_getfsmap_info	info = { NULL };
	bool				use_rmap;
	int				i;
	int				error = 0;

	if (head->fmh_iflags & ~FMH_IF_VALID)
		return -EINVAL;
	if (!xfs_getfsmap_is_valid_device(mp, &head->fmh_keys[0]) ||
	    !xfs_getfsmap_is_valid_device(mp, &head->fmh_keys[1]))
		return -EINVAL;

	use_rmap = capable(CAP_SYS_ADMIN) &&
		   xfs_sb_version_hasrmapbt(&mp->m_sb);
	head->fmh_entries = 0;

	/* Set up our device handlers. */
	memset(handlers, 0, sizeof(handlers));
	handlers[0].dev = new_encode_dev(mp->m_ddev_targp->bt_dev);
	if (use_rmap)
		handlers[0].fn = xfs_getfsmap_datadev_rmapbt;
	else
		handlers[0].fn = xfs_getfsmap_datadev_bnobt;
	if (mp->m_logdev_targp != mp->m_ddev_targp) {
		handlers[1].dev = new_encode_dev(mp->m_logdev_targp->bt_dev);
		handlers[1].fn = xfs_getfsmap_logdev;
	}
#ifdef CONFIG_XFS_RT
	if (mp->m_rtdev_targp) {
		handlers[2].dev = new_encode_dev(mp->m_rtdev_targp->bt_dev);
		handlers[2].fn = xfs_getfsmap_rtdev_rtbitmap;
	}
#endif /* CONFIG_XFS_RT */

	xfs_sort(handlers, XFS_GETFSMAP_DEVS, sizeof(struct xfs_getfsmap_dev),
			xfs_getfsmap_dev_compare);

	/*
	 * To continue where we left off, we allow userspace to use the
	 * last mapping from a previous call as the low key of the next.
	 * This is identified by a non-zero length in the low key. We
	 * have to increment the low key in this scenario to ensure we
	 * don't return the same mapping again, and instead return the
	 * very next mapping.
	 *
	 * If the low key mapping refers to file data, the same physical
	 * blocks could be mapped to several other files/offsets.
	 * According to rmapbt record ordering, the minimal next
	 * possible record for the block range is the next starting
	 * offset in the same inode. Therefore, bump the file offset to
	 * continue the search appropriately.  For all other low key
	 * mapping types (attr blocks, metadata), bump the physical
	 * offset as there can be no other mapping for the same physical
	 * block range.
	 */
	dkeys[0] = head->fmh_keys[0];
	if (dkeys[0].fmr_flags & (FMR_OF_SPECIAL_OWNER | FMR_OF_EXTENT_MAP)) {
		dkeys[0].fmr_physical += dkeys[0].fmr_length;
		dkeys[0].fmr_owner = 0;
		if (dkeys[0].fmr_offset)
			return -EINVAL;
	} else
		dkeys[0].fmr_offset += dkeys[0].fmr_length;
	dkeys[0].fmr_length = 0;
	memset(&dkeys[1], 0xFF, sizeof(struct xfs_fsmap));

	if (!xfs_getfsmap_check_keys(dkeys, &head->fmh_keys[1]))
		return -EINVAL;

	info.next_daddr = head->fmh_keys[0].fmr_physical +
			  head->fmh_keys[0].fmr_length;
	info.formatter = formatter;
	info.format_arg = arg;
	info.head = head;

	/* For each device we support... */
	for (i = 0; i < XFS_GETFSMAP_DEVS; i++) {
		/* Is this device within the range the user asked for? */
		if (!handlers[i].fn)
			continue;
		if (head->fmh_keys[0].fmr_device > handlers[i].dev)
			continue;
		if (head->fmh_keys[1].fmr_device < handlers[i].dev)
			break;

		/*
		 * If this device number matches the high key, we have
		 * to pass the high key to the handler to limit the
		 * query results.  If the device number exceeds the
		 * low key, zero out the low key so that we get
		 * everything from the beginning.
		 */
		if (handlers[i].dev == head->fmh_keys[1].fmr_device)
			dkeys[1] = head->fmh_keys[1];
		if (handlers[i].dev > head->fmh_keys[0].fmr_device)
			memset(&dkeys[0], 0, sizeof(struct xfs_fsmap));

		error = xfs_trans_alloc_empty(mp, &tp);
		if (error)
			break;

		info.dev = handlers[i].dev;
		info.last = false;
		info.agno = NULLAGNUMBER;
		error = handlers[i].fn(tp, dkeys, &info);
		if (error)
			break;
		xfs_trans_cancel(tp);
		tp = NULL;
		info.next_daddr = 0;
	}

	if (tp)
		xfs_trans_cancel(tp);
	head->fmh_oflags = FMH_OF_DEV_T;
	return error;
}
