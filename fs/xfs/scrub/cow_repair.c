// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_inode_fork.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_rmap.h"
#include "xfs_refcount.h"
#include "xfs_quota.h"
#include "xfs_ialloc.h"
#include "xfs_ag.h"
#include "xfs_error.h"
#include "xfs_errortag.h"
#include "xfs_icache.h"
#include "xfs_refcount_btree.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
#include "scrub/off_bitmap.h"
#include "scrub/fsb_bitmap.h"
#include "scrub/reap.h"

/*
 * CoW Fork Mapping Repair
 * =======================
 *
 * Although CoW staging extents are owned by incore CoW inode forks, on disk
 * they are owned by the refcount btree.  The ondisk metadata does not record
 * any ownership information, which limits what we can do to repair the
 * mappings in the CoW fork.  At most, we can replace ifork mappings that lack
 * an entry in the refcount btree or are described by a reverse mapping record
 * whose owner is not OWN_COW.
 *
 * Replacing extents is also tricky -- we can't touch written CoW fork extents
 * since they are undergoing writeback, and delalloc extents do not require
 * repair since they only exist incore.  Hence the most we can do is find the
 * bad parts of unwritten mappings, allocate a replacement set of blocks, and
 * replace the incore mapping.  We use the regular reaping process to unmap
 * or free the discarded blocks, as appropriate.
 */
struct xrep_cow {
	struct xfs_scrub	*sc;

	/* Bitmap of file offset ranges that need replacing. */
	struct xoff_bitmap	bad_fileoffs;

	/* Bitmap of fsblocks that were removed from the CoW fork. */
	struct xfsb_bitmap	old_cowfork_fsblocks;

	/* CoW fork mappings used to scan for bad CoW staging extents. */
	struct xfs_bmbt_irec	irec;

	/* refcount btree block number of irec.br_startblock */
	unsigned int		irec_startbno;

	/* refcount btree block number of the next refcount record we expect */
	unsigned int		next_bno;
};

/* CoW staging extent. */
struct xrep_cow_extent {
	xfs_fsblock_t		fsbno;
	xfs_extlen_t		len;
};

/*
 * Mark the part of the file range that corresponds to the given physical
 * space.  Caller must ensure that the physical range is within xc->irec.
 */
STATIC int
xrep_cow_mark_file_range(
	struct xrep_cow		*xc,
	xfs_fsblock_t		startblock,
	xfs_filblks_t		blockcount)
{
	xfs_fileoff_t		startoff;

	startoff = xc->irec.br_startoff +
				(startblock - xc->irec.br_startblock);

	trace_xrep_cow_mark_file_range(xc->sc->ip, startblock, startoff,
			blockcount);

	return xoff_bitmap_set(&xc->bad_fileoffs, startoff, blockcount);
}

/*
 * Trim @src to fit within the CoW fork mapping being examined, and put the
 * result in @dst.
 */
static inline void
xrep_cow_trim_refcount(
	struct xrep_cow			*xc,
	struct xfs_refcount_irec	*dst,
	const struct xfs_refcount_irec	*src)
{
	unsigned int			adj;

	memcpy(dst, src, sizeof(*dst));

	if (dst->rc_startblock < xc->irec_startbno) {
		adj = xc->irec_startbno - dst->rc_startblock;
		dst->rc_blockcount -= adj;
		dst->rc_startblock += adj;
	}

	if (dst->rc_startblock + dst->rc_blockcount >
	    xc->irec_startbno + xc->irec.br_blockcount) {
		adj = (dst->rc_startblock + dst->rc_blockcount) -
		      (xc->irec_startbno + xc->irec.br_blockcount);
		dst->rc_blockcount -= adj;
	}
}

/* Mark any shared CoW staging extents. */
STATIC int
xrep_cow_mark_shared_staging(
	struct xfs_btree_cur		*cur,
	const struct xfs_refcount_irec	*rec,
	void				*priv)
{
	struct xrep_cow			*xc = priv;
	struct xfs_refcount_irec	rrec;
	xfs_fsblock_t			fsbno;

	if (!xfs_refcount_check_domain(rec) ||
	    rec->rc_domain != XFS_REFC_DOMAIN_SHARED)
		return -EFSCORRUPTED;

	xrep_cow_trim_refcount(xc, &rrec, rec);

	fsbno = XFS_AGB_TO_FSB(xc->sc->mp, cur->bc_ag.pag->pag_agno,
			rrec.rc_startblock);
	return xrep_cow_mark_file_range(xc, fsbno, rrec.rc_blockcount);
}

/*
 * Mark any portion of the CoW fork file offset range where there is not a CoW
 * staging extent record in the refcountbt, and keep a record of where we did
 * find correct refcountbt records.  Staging records are always cleaned out at
 * mount time, so any two inodes trying to map the same staging area would have
 * already taken the fs down due to refcount btree verifier errors.  Hence this
 * inode should be the sole creator of the staging extent records ondisk.
 */
STATIC int
xrep_cow_mark_missing_staging(
	struct xfs_btree_cur		*cur,
	const struct xfs_refcount_irec	*rec,
	void				*priv)
{
	struct xrep_cow			*xc = priv;
	struct xfs_refcount_irec	rrec;
	int				error;

	if (!xfs_refcount_check_domain(rec) ||
	    rec->rc_domain != XFS_REFC_DOMAIN_COW)
		return -EFSCORRUPTED;

	xrep_cow_trim_refcount(xc, &rrec, rec);

	if (xc->next_bno >= rrec.rc_startblock)
		goto next;

	error = xrep_cow_mark_file_range(xc,
			XFS_AGB_TO_FSB(xc->sc->mp, cur->bc_ag.pag->pag_agno,
				       xc->next_bno),
			rrec.rc_startblock - xc->next_bno);
	if (error)
		return error;

next:
	xc->next_bno = rrec.rc_startblock + rrec.rc_blockcount;
	return 0;
}

/*
 * Mark any area that does not correspond to a CoW staging rmap.  These are
 * cross-linked areas that must be avoided.
 */
STATIC int
xrep_cow_mark_missing_staging_rmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_cow			*xc = priv;
	xfs_fsblock_t			fsbno;
	xfs_agblock_t			rec_bno;
	xfs_extlen_t			rec_len;
	unsigned int			adj;

	if (rec->rm_owner == XFS_RMAP_OWN_COW)
		return 0;

	rec_bno = rec->rm_startblock;
	rec_len = rec->rm_blockcount;
	if (rec_bno < xc->irec_startbno) {
		adj = xc->irec_startbno - rec_bno;
		rec_len -= adj;
		rec_bno += adj;
	}

	if (rec_bno + rec_len > xc->irec_startbno + xc->irec.br_blockcount) {
		adj = (rec_bno + rec_len) -
		      (xc->irec_startbno + xc->irec.br_blockcount);
		rec_len -= adj;
	}

	fsbno = XFS_AGB_TO_FSB(xc->sc->mp, cur->bc_ag.pag->pag_agno, rec_bno);
	return xrep_cow_mark_file_range(xc, fsbno, rec_len);
}

/*
 * Find any part of the CoW fork mapping that isn't a single-owner CoW staging
 * extent and mark the corresponding part of the file range in the bitmap.
 */
STATIC int
xrep_cow_find_bad(
	struct xrep_cow			*xc)
{
	struct xfs_refcount_irec	rc_low = { 0 };
	struct xfs_refcount_irec	rc_high = { 0 };
	struct xfs_rmap_irec		rm_low = { 0 };
	struct xfs_rmap_irec		rm_high = { 0 };
	struct xfs_perag		*pag;
	struct xfs_scrub		*sc = xc->sc;
	xfs_agnumber_t			agno;
	int				error;

	agno = XFS_FSB_TO_AGNO(sc->mp, xc->irec.br_startblock);
	xc->irec_startbno = XFS_FSB_TO_AGBNO(sc->mp, xc->irec.br_startblock);

	pag = xfs_perag_get(sc->mp, agno);
	if (!pag)
		return -EFSCORRUPTED;

	error = xrep_ag_init(sc, pag, &sc->sa);
	if (error)
		goto out_pag;

	/* Mark any CoW fork extents that are shared. */
	rc_low.rc_startblock = xc->irec_startbno;
	rc_high.rc_startblock = xc->irec_startbno + xc->irec.br_blockcount - 1;
	rc_low.rc_domain = rc_high.rc_domain = XFS_REFC_DOMAIN_SHARED;
	error = xfs_refcount_query_range(sc->sa.refc_cur, &rc_low, &rc_high,
			xrep_cow_mark_shared_staging, xc);
	if (error)
		goto out_sa;

	/* Make sure there are CoW staging extents for the whole mapping. */
	rc_low.rc_startblock = xc->irec_startbno;
	rc_high.rc_startblock = xc->irec_startbno + xc->irec.br_blockcount - 1;
	rc_low.rc_domain = rc_high.rc_domain = XFS_REFC_DOMAIN_COW;
	xc->next_bno = xc->irec_startbno;
	error = xfs_refcount_query_range(sc->sa.refc_cur, &rc_low, &rc_high,
			xrep_cow_mark_missing_staging, xc);
	if (error)
		goto out_sa;

	if (xc->next_bno < xc->irec_startbno + xc->irec.br_blockcount) {
		error = xrep_cow_mark_file_range(xc,
				XFS_AGB_TO_FSB(sc->mp, pag->pag_agno,
					       xc->next_bno),
				xc->irec_startbno + xc->irec.br_blockcount -
				xc->next_bno);
		if (error)
			goto out_sa;
	}

	/* Mark any area has an rmap that isn't a COW staging extent. */
	rm_low.rm_startblock = xc->irec_startbno;
	memset(&rm_high, 0xFF, sizeof(rm_high));
	rm_high.rm_startblock = xc->irec_startbno + xc->irec.br_blockcount - 1;
	error = xfs_rmap_query_range(sc->sa.rmap_cur, &rm_low, &rm_high,
			xrep_cow_mark_missing_staging_rmap, xc);
	if (error)
		goto out_sa;

	/*
	 * If userspace is forcing us to rebuild the CoW fork or someone turned
	 * on the debugging knob, replace everything in the CoW fork.
	 */
	if ((sc->sm->sm_flags & XFS_SCRUB_IFLAG_FORCE_REBUILD) ||
	    XFS_TEST_ERROR(false, sc->mp, XFS_ERRTAG_FORCE_SCRUB_REPAIR)) {
		error = xrep_cow_mark_file_range(xc, xc->irec.br_startblock,
				xc->irec.br_blockcount);
		if (error)
			return error;
	}

out_sa:
	xchk_ag_free(sc, &sc->sa);
out_pag:
	xfs_perag_put(pag);
	return 0;
}

/*
 * Allocate a replacement CoW staging extent of up to the given number of
 * blocks, and fill out the mapping.
 */
STATIC int
xrep_cow_alloc(
	struct xfs_scrub	*sc,
	xfs_extlen_t		maxlen,
	struct xrep_cow_extent	*repl)
{
	struct xfs_alloc_arg	args = {
		.tp		= sc->tp,
		.mp		= sc->mp,
		.oinfo		= XFS_RMAP_OINFO_SKIP_UPDATE,
		.minlen		= 1,
		.maxlen		= maxlen,
		.prod		= 1,
		.resv		= XFS_AG_RESV_NONE,
		.datatype	= XFS_ALLOC_USERDATA,
	};
	int			error;

	error = xfs_trans_reserve_more(sc->tp, maxlen, 0);
	if (error)
		return error;

	error = xfs_alloc_vextent_start_ag(&args,
			XFS_INO_TO_FSB(sc->mp, sc->ip->i_ino));
	if (error)
		return error;
	if (args.fsbno == NULLFSBLOCK)
		return -ENOSPC;

	xfs_refcount_alloc_cow_extent(sc->tp, args.fsbno, args.len);

	repl->fsbno = args.fsbno;
	repl->len = args.len;
	return 0;
}

/*
 * Look up the current CoW fork mapping so that we only allocate enough to
 * replace a single mapping.  If we don't find a mapping that covers the start
 * of the file range, or we find a delalloc or written extent, something is
 * seriously wrong, since we didn't drop the ILOCK.
 */
static inline int
xrep_cow_find_mapping(
	struct xrep_cow		*xc,
	struct xfs_iext_cursor	*icur,
	xfs_fileoff_t		startoff,
	struct xfs_bmbt_irec	*got)
{
	struct xfs_inode	*ip = xc->sc->ip;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_COW_FORK);

	if (!xfs_iext_lookup_extent(ip, ifp, startoff, icur, got))
		goto bad;

	if (got->br_startoff > startoff)
		goto bad;

	if (got->br_blockcount == 0)
		goto bad;

	if (isnullstartblock(got->br_startblock))
		goto bad;

	if (xfs_bmap_is_written_extent(got))
		goto bad;

	return 0;
bad:
	ASSERT(0);
	return -EFSCORRUPTED;
}

#define REPLACE_LEFT_SIDE	(1U << 0)
#define REPLACE_RIGHT_SIDE	(1U << 1)

/*
 * Given a CoW fork mapping @got and a replacement mapping @repl, remap the
 * beginning of @got with the space described by @rep.
 */
static inline void
xrep_cow_replace_mapping(
	struct xfs_inode		*ip,
	struct xfs_iext_cursor		*icur,
	const struct xfs_bmbt_irec	*got,
	const struct xrep_cow_extent	*repl)
{
	struct xfs_bmbt_irec		new = *got; /* struct copy */

	ASSERT(repl->len > 0);
	ASSERT(!isnullstartblock(got->br_startblock));

	trace_xrep_cow_replace_mapping(ip, got, repl->fsbno, repl->len);

	if (got->br_blockcount == repl->len) {
		/*
		 * The new extent is a complete replacement for the existing
		 * extent.  Update the COW fork record.
		 */
		new.br_startblock = repl->fsbno;
		xfs_iext_update_extent(ip, BMAP_COWFORK, icur, &new);
		return;
	}

	/*
	 * The new extent can replace the beginning of the COW fork record.
	 * Move the left side of @got upwards, then insert the new record.
	 */
	new.br_startoff += repl->len;
	new.br_startblock += repl->len;
	new.br_blockcount -= repl->len;
	xfs_iext_update_extent(ip, BMAP_COWFORK, icur, &new);

	new.br_startoff = got->br_startoff;
	new.br_startblock = repl->fsbno;
	new.br_blockcount = repl->len;
	xfs_iext_insert(ip, icur, &new, BMAP_COWFORK);
}

/*
 * Replace the unwritten CoW staging extent backing the given file range with a
 * new space extent that isn't as problematic.
 */
STATIC int
xrep_cow_replace_range(
	struct xrep_cow		*xc,
	xfs_fileoff_t		startoff,
	xfs_extlen_t		*blockcount)
{
	struct xfs_iext_cursor	icur;
	struct xrep_cow_extent	repl;
	struct xfs_bmbt_irec	got;
	struct xfs_scrub	*sc = xc->sc;
	xfs_fileoff_t		nextoff;
	xfs_extlen_t		alloc_len;
	int			error;

	/*
	 * Put the existing CoW fork mapping in @got.  If @got ends before
	 * @rep, truncate @rep so we only replace one extent mapping at a time.
	 */
	error = xrep_cow_find_mapping(xc, &icur, startoff, &got);
	if (error)
		return error;
	nextoff = min(startoff + *blockcount,
		      got.br_startoff + got.br_blockcount);

	/*
	 * Allocate a replacement extent.  If we don't fill all the blocks,
	 * shorten the quantity that will be deleted in this step.
	 */
	alloc_len = min_t(xfs_fileoff_t, XFS_MAX_BMBT_EXTLEN,
			  nextoff - startoff);
	error = xrep_cow_alloc(sc, alloc_len, &repl);
	if (error)
		return error;

	/*
	 * Replace the old mapping with the new one, and commit the metadata
	 * changes made so far.
	 */
	xrep_cow_replace_mapping(sc->ip, &icur, &got, &repl);

	xfs_inode_set_cowblocks_tag(sc->ip);
	error = xfs_defer_finish(&sc->tp);
	if (error)
		return error;

	/* Note the old CoW staging extents; we'll reap them all later. */
	error = xfsb_bitmap_set(&xc->old_cowfork_fsblocks, got.br_startblock,
			repl.len);
	if (error)
		return error;

	*blockcount = repl.len;
	return 0;
}

/*
 * Replace a bad part of an unwritten CoW staging extent with a fresh delalloc
 * reservation.
 */
STATIC int
xrep_cow_replace(
	uint64_t		startoff,
	uint64_t		blockcount,
	void			*priv)
{
	struct xrep_cow		*xc = priv;
	int			error = 0;

	while (blockcount > 0) {
		xfs_extlen_t	len = min_t(xfs_filblks_t, blockcount,
					    XFS_MAX_BMBT_EXTLEN);

		error = xrep_cow_replace_range(xc, startoff, &len);
		if (error)
			break;

		blockcount -= len;
		startoff += len;
	}

	return error;
}

/*
 * Repair an inode's CoW fork.  The CoW fork is an in-core structure, so
 * there's no btree to rebuid.  Instead, we replace any mappings that are
 * cross-linked or lack ondisk CoW fork records in the refcount btree.
 */
int
xrep_bmap_cow(
	struct xfs_scrub	*sc)
{
	struct xrep_cow		*xc;
	struct xfs_iext_cursor	icur;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(sc->ip, XFS_COW_FORK);
	int			error;

	if (!xfs_has_rmapbt(sc->mp) || !xfs_has_reflink(sc->mp))
		return -EOPNOTSUPP;

	if (!ifp)
		return 0;

	/* realtime files aren't supported yet */
	if (XFS_IS_REALTIME_INODE(sc->ip))
		return -EOPNOTSUPP;

	/*
	 * If we're somehow not in extents format, then reinitialize it to
	 * an empty extent mapping fork and exit.
	 */
	if (ifp->if_format != XFS_DINODE_FMT_EXTENTS) {
		ifp->if_format = XFS_DINODE_FMT_EXTENTS;
		ifp->if_nextents = 0;
		return 0;
	}

	xc = kzalloc(sizeof(struct xrep_cow), XCHK_GFP_FLAGS);
	if (!xc)
		return -ENOMEM;

	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	xc->sc = sc;
	xoff_bitmap_init(&xc->bad_fileoffs);
	xfsb_bitmap_init(&xc->old_cowfork_fsblocks);

	for_each_xfs_iext(ifp, &icur, &xc->irec) {
		if (xchk_should_terminate(sc, &error))
			goto out_bitmap;

		/*
		 * delalloc reservations only exist incore, so there is no
		 * ondisk metadata that we can examine.  Hence we leave them
		 * alone.
		 */
		if (isnullstartblock(xc->irec.br_startblock))
			continue;

		/*
		 * COW fork extents are only in the written state if writeback
		 * is actively writing to disk.  We cannot restart the write
		 * at a different disk address since we've already issued the
		 * IO, so we leave these alone and hope for the best.
		 */
		if (xfs_bmap_is_written_extent(&xc->irec))
			continue;

		error = xrep_cow_find_bad(xc);
		if (error)
			goto out_bitmap;
	}

	/* Replace any bad unwritten mappings with fresh reservations. */
	error = xoff_bitmap_walk(&xc->bad_fileoffs, xrep_cow_replace, xc);
	if (error)
		goto out_bitmap;

	/*
	 * Reap as many of the old CoW blocks as we can.  They are owned ondisk
	 * by the refcount btree, not the inode, so it is correct to treat them
	 * like inode metadata.
	 */
	error = xrep_reap_fsblocks(sc, &xc->old_cowfork_fsblocks,
			&XFS_RMAP_OINFO_COW);
	if (error)
		goto out_bitmap;

out_bitmap:
	xfsb_bitmap_destroy(&xc->old_cowfork_fsblocks);
	xoff_bitmap_destroy(&xc->bad_fileoffs);
	kfree(xc);
	return error;
}
