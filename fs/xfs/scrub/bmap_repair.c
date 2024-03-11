// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
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
#include "xfs_btree_staging.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_inode_fork.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_bmap_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount.h"
#include "xfs_quota.h"
#include "xfs_ialloc.h"
#include "xfs_ag.h"
#include "xfs_reflink.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
#include "scrub/fsb_bitmap.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/newbt.h"
#include "scrub/reap.h"

/*
 * Inode Fork Block Mapping (BMBT) Repair
 * ======================================
 *
 * Gather all the rmap records for the inode and fork we're fixing, reset the
 * incore fork, then recreate the btree.
 */

enum reflink_scan_state {
	RLS_IRRELEVANT = -1,	/* not applicable to this file */
	RLS_UNKNOWN,		/* shared extent scans required */
	RLS_SET_IFLAG,		/* iflag must be set */
};

struct xrep_bmap {
	/* Old bmbt blocks */
	struct xfsb_bitmap	old_bmbt_blocks;

	/* New fork. */
	struct xrep_newbt	new_bmapbt;

	/* List of new bmap records. */
	struct xfarray		*bmap_records;

	struct xfs_scrub	*sc;

	/* How many blocks did we find allocated to this file? */
	xfs_rfsblock_t		nblocks;

	/* How many bmbt blocks did we find for this fork? */
	xfs_rfsblock_t		old_bmbt_block_count;

	/* get_records()'s position in the free space record array. */
	xfarray_idx_t		array_cur;

	/* How many real (non-hole, non-delalloc) mappings do we have? */
	uint64_t		real_mappings;

	/* Which fork are we fixing? */
	int			whichfork;

	/* What d the REFLINK flag be set when the repair is over? */
	enum reflink_scan_state	reflink_scan;

	/* Do we allow unwritten extents? */
	bool			allow_unwritten;
};

/* Is this space extent shared?  Flag the inode if it is. */
STATIC int
xrep_bmap_discover_shared(
	struct xrep_bmap	*rb,
	xfs_fsblock_t		startblock,
	xfs_filblks_t		blockcount)
{
	struct xfs_scrub	*sc = rb->sc;
	xfs_agblock_t		agbno;
	xfs_agblock_t		fbno;
	xfs_extlen_t		flen;
	int			error;

	agbno = XFS_FSB_TO_AGBNO(sc->mp, startblock);
	error = xfs_refcount_find_shared(sc->sa.refc_cur, agbno, blockcount,
			&fbno, &flen, false);
	if (error)
		return error;

	if (fbno != NULLAGBLOCK)
		rb->reflink_scan = RLS_SET_IFLAG;

	return 0;
}

/* Remember this reverse-mapping as a series of bmap records. */
STATIC int
xrep_bmap_from_rmap(
	struct xrep_bmap	*rb,
	xfs_fileoff_t		startoff,
	xfs_fsblock_t		startblock,
	xfs_filblks_t		blockcount,
	bool			unwritten)
{
	struct xfs_bmbt_irec	irec = {
		.br_startoff	= startoff,
		.br_startblock	= startblock,
		.br_state	= unwritten ? XFS_EXT_UNWRITTEN : XFS_EXT_NORM,
	};
	struct xfs_bmbt_rec	rbe;
	struct xfs_scrub	*sc = rb->sc;
	int			error = 0;

	/*
	 * If we're repairing the data fork of a non-reflinked regular file on
	 * a reflink filesystem, we need to figure out if this space extent is
	 * shared.
	 */
	if (rb->reflink_scan == RLS_UNKNOWN && !unwritten) {
		error = xrep_bmap_discover_shared(rb, startblock, blockcount);
		if (error)
			return error;
	}

	do {
		xfs_failaddr_t	fa;

		irec.br_blockcount = min_t(xfs_filblks_t, blockcount,
				XFS_MAX_BMBT_EXTLEN);

		fa = xfs_bmap_validate_extent(sc->ip, rb->whichfork, &irec);
		if (fa)
			return -EFSCORRUPTED;

		xfs_bmbt_disk_set_all(&rbe, &irec);

		trace_xrep_bmap_found(sc->ip, rb->whichfork, &irec);

		if (xchk_should_terminate(sc, &error))
			return error;

		error = xfarray_append(rb->bmap_records, &rbe);
		if (error)
			return error;

		rb->real_mappings++;

		irec.br_startblock += irec.br_blockcount;
		irec.br_startoff += irec.br_blockcount;
		blockcount -= irec.br_blockcount;
	} while (blockcount > 0);

	return 0;
}

/* Check for any obvious errors or conflicts in the file mapping. */
STATIC int
xrep_bmap_check_fork_rmap(
	struct xrep_bmap		*rb,
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec)
{
	struct xfs_scrub		*sc = rb->sc;
	enum xbtree_recpacking		outcome;
	int				error;

	/*
	 * Data extents for rt files are never stored on the data device, but
	 * everything else (xattrs, bmbt blocks) can be.
	 */
	if (XFS_IS_REALTIME_INODE(sc->ip) &&
	    !(rec->rm_flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK)))
		return -EFSCORRUPTED;

	/* Check that this is within the AG. */
	if (!xfs_verify_agbext(cur->bc_ag.pag, rec->rm_startblock,
				rec->rm_blockcount))
		return -EFSCORRUPTED;

	/* Check the file offset range. */
	if (!(rec->rm_flags & XFS_RMAP_BMBT_BLOCK) &&
	    !xfs_verify_fileext(sc->mp, rec->rm_offset, rec->rm_blockcount))
		return -EFSCORRUPTED;

	/* No contradictory flags. */
	if ((rec->rm_flags & (XFS_RMAP_ATTR_FORK | XFS_RMAP_BMBT_BLOCK)) &&
	    (rec->rm_flags & XFS_RMAP_UNWRITTEN))
		return -EFSCORRUPTED;

	/* Make sure this isn't free space. */
	error = xfs_alloc_has_records(sc->sa.bno_cur, rec->rm_startblock,
			rec->rm_blockcount, &outcome);
	if (error)
		return error;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		return -EFSCORRUPTED;

	/* Must not be an inode chunk. */
	error = xfs_ialloc_has_inodes_at_extent(sc->sa.ino_cur,
			rec->rm_startblock, rec->rm_blockcount, &outcome);
	if (error)
		return error;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		return -EFSCORRUPTED;

	return 0;
}

/* Record extents that belong to this inode's fork. */
STATIC int
xrep_bmap_walk_rmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_bmap		*rb = priv;
	struct xfs_mount		*mp = cur->bc_mp;
	xfs_fsblock_t			fsbno;
	int				error = 0;

	if (xchk_should_terminate(rb->sc, &error))
		return error;

	if (rec->rm_owner != rb->sc->ip->i_ino)
		return 0;

	error = xrep_bmap_check_fork_rmap(rb, cur, rec);
	if (error)
		return error;

	/*
	 * Record all blocks allocated to this file even if the extent isn't
	 * for the fork we're rebuilding so that we can reset di_nblocks later.
	 */
	rb->nblocks += rec->rm_blockcount;

	/* If this rmap isn't for the fork we want, we're done. */
	if (rb->whichfork == XFS_DATA_FORK &&
	    (rec->rm_flags & XFS_RMAP_ATTR_FORK))
		return 0;
	if (rb->whichfork == XFS_ATTR_FORK &&
	    !(rec->rm_flags & XFS_RMAP_ATTR_FORK))
		return 0;

	/* Reject unwritten extents if we don't allow those. */
	if ((rec->rm_flags & XFS_RMAP_UNWRITTEN) && !rb->allow_unwritten)
		return -EFSCORRUPTED;

	fsbno = XFS_AGB_TO_FSB(mp, cur->bc_ag.pag->pag_agno,
			rec->rm_startblock);

	if (rec->rm_flags & XFS_RMAP_BMBT_BLOCK) {
		rb->old_bmbt_block_count += rec->rm_blockcount;
		return xfsb_bitmap_set(&rb->old_bmbt_blocks, fsbno,
				rec->rm_blockcount);
	}

	return xrep_bmap_from_rmap(rb, rec->rm_offset, fsbno,
			rec->rm_blockcount,
			rec->rm_flags & XFS_RMAP_UNWRITTEN);
}

/*
 * Compare two block mapping records.  We want to sort in order of increasing
 * file offset.
 */
static int
xrep_bmap_extent_cmp(
	const void			*a,
	const void			*b)
{
	const struct xfs_bmbt_rec	*ba = a;
	const struct xfs_bmbt_rec	*bb = b;
	xfs_fileoff_t			ao = xfs_bmbt_disk_get_startoff(ba);
	xfs_fileoff_t			bo = xfs_bmbt_disk_get_startoff(bb);

	if (ao > bo)
		return 1;
	else if (ao < bo)
		return -1;
	return 0;
}

/*
 * Sort the bmap extents by fork offset or else the records will be in the
 * wrong order.  Ensure there are no overlaps in the file offset ranges.
 */
STATIC int
xrep_bmap_sort_records(
	struct xrep_bmap	*rb)
{
	struct xfs_bmbt_irec	irec;
	xfs_fileoff_t		next_off = 0;
	xfarray_idx_t		array_cur;
	int			error;

	error = xfarray_sort(rb->bmap_records, xrep_bmap_extent_cmp,
			XFARRAY_SORT_KILLABLE);
	if (error)
		return error;

	foreach_xfarray_idx(rb->bmap_records, array_cur) {
		struct xfs_bmbt_rec	rec;

		if (xchk_should_terminate(rb->sc, &error))
			return error;

		error = xfarray_load(rb->bmap_records, array_cur, &rec);
		if (error)
			return error;

		xfs_bmbt_disk_get_all(&rec, &irec);

		if (irec.br_startoff < next_off)
			return -EFSCORRUPTED;

		next_off = irec.br_startoff + irec.br_blockcount;
	}

	return 0;
}

/* Scan one AG for reverse mappings that we can turn into extent maps. */
STATIC int
xrep_bmap_scan_ag(
	struct xrep_bmap	*rb,
	struct xfs_perag	*pag)
{
	struct xfs_scrub	*sc = rb->sc;
	int			error;

	error = xrep_ag_init(sc, pag, &sc->sa);
	if (error)
		return error;

	error = xfs_rmap_query_all(sc->sa.rmap_cur, xrep_bmap_walk_rmap, rb);
	xchk_ag_free(sc, &sc->sa);
	return error;
}

/* Find the delalloc extents from the old incore extent tree. */
STATIC int
xrep_bmap_find_delalloc(
	struct xrep_bmap	*rb)
{
	struct xfs_bmbt_irec	irec;
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_rec	rbe;
	struct xfs_inode	*ip = rb->sc->ip;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, rb->whichfork);
	int			error = 0;

	/*
	 * Skip this scan if we don't expect to find delayed allocation
	 * reservations in this fork.
	 */
	if (rb->whichfork == XFS_ATTR_FORK || ip->i_delayed_blks == 0)
		return 0;

	for_each_xfs_iext(ifp, &icur, &irec) {
		if (!isnullstartblock(irec.br_startblock))
			continue;

		xfs_bmbt_disk_set_all(&rbe, &irec);

		trace_xrep_bmap_found(ip, rb->whichfork, &irec);

		if (xchk_should_terminate(rb->sc, &error))
			return error;

		error = xfarray_append(rb->bmap_records, &rbe);
		if (error)
			return error;
	}

	return 0;
}

/*
 * Collect block mappings for this fork of this inode and decide if we have
 * enough space to rebuild.  Caller is responsible for cleaning up the list if
 * anything goes wrong.
 */
STATIC int
xrep_bmap_find_mappings(
	struct xrep_bmap	*rb)
{
	struct xfs_scrub	*sc = rb->sc;
	struct xfs_perag	*pag;
	xfs_agnumber_t		agno;
	int			error = 0;

	/* Iterate the rmaps for extents. */
	for_each_perag(sc->mp, agno, pag) {
		error = xrep_bmap_scan_ag(rb, pag);
		if (error) {
			xfs_perag_rele(pag);
			return error;
		}
	}

	return xrep_bmap_find_delalloc(rb);
}

/* Retrieve real extent mappings for bulk loading the bmap btree. */
STATIC int
xrep_bmap_get_records(
	struct xfs_btree_cur	*cur,
	unsigned int		idx,
	struct xfs_btree_block	*block,
	unsigned int		nr_wanted,
	void			*priv)
{
	struct xfs_bmbt_rec	rec;
	struct xfs_bmbt_irec	*irec = &cur->bc_rec.b;
	struct xrep_bmap	*rb = priv;
	union xfs_btree_rec	*block_rec;
	unsigned int		loaded;
	int			error;

	for (loaded = 0; loaded < nr_wanted; loaded++, idx++) {
		do {
			error = xfarray_load(rb->bmap_records, rb->array_cur++,
					&rec);
			if (error)
				return error;

			xfs_bmbt_disk_get_all(&rec, irec);
		} while (isnullstartblock(irec->br_startblock));

		block_rec = xfs_btree_rec_addr(cur, idx, block);
		cur->bc_ops->init_rec_from_cur(cur, block_rec);
	}

	return loaded;
}

/* Feed one of the new btree blocks to the bulk loader. */
STATIC int
xrep_bmap_claim_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	void			*priv)
{
	struct xrep_bmap        *rb = priv;

	return xrep_newbt_claim_block(cur, &rb->new_bmapbt, ptr);
}

/* Figure out how much space we need to create the incore btree root block. */
STATIC size_t
xrep_bmap_iroot_size(
	struct xfs_btree_cur	*cur,
	unsigned int		level,
	unsigned int		nr_this_level,
	void			*priv)
{
	ASSERT(level > 0);

	return XFS_BMAP_BROOT_SPACE_CALC(cur->bc_mp, nr_this_level);
}

/* Update the inode counters. */
STATIC int
xrep_bmap_reset_counters(
	struct xrep_bmap	*rb)
{
	struct xfs_scrub	*sc = rb->sc;
	struct xbtree_ifakeroot	*ifake = &rb->new_bmapbt.ifake;
	int64_t			delta;

	if (rb->reflink_scan == RLS_SET_IFLAG)
		sc->ip->i_diflags2 |= XFS_DIFLAG2_REFLINK;

	/*
	 * Update the inode block counts to reflect the extents we found in the
	 * rmapbt.
	 */
	delta = ifake->if_blocks - rb->old_bmbt_block_count;
	sc->ip->i_nblocks = rb->nblocks + delta;
	xfs_trans_log_inode(sc->tp, sc->ip, XFS_ILOG_CORE);

	/*
	 * Adjust the quota counts by the difference in size between the old
	 * and new bmbt.
	 */
	xfs_trans_mod_dquot_byino(sc->tp, sc->ip, XFS_TRANS_DQ_BCOUNT, delta);
	return 0;
}

/*
 * Create a new iext tree and load it with block mappings.  If the inode is
 * in extents format, that's all we need to do to commit the new mappings.
 * If it is in btree format, this takes care of preloading the incore tree.
 */
STATIC int
xrep_bmap_extents_load(
	struct xrep_bmap	*rb)
{
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	irec;
	struct xfs_ifork	*ifp = rb->new_bmapbt.ifake.if_fork;
	xfarray_idx_t		array_cur;
	int			error;

	ASSERT(ifp->if_bytes == 0);

	/* Add all the mappings (incl. delalloc) to the incore extent tree. */
	xfs_iext_first(ifp, &icur);
	foreach_xfarray_idx(rb->bmap_records, array_cur) {
		struct xfs_bmbt_rec	rec;

		error = xfarray_load(rb->bmap_records, array_cur, &rec);
		if (error)
			return error;

		xfs_bmbt_disk_get_all(&rec, &irec);

		xfs_iext_insert_raw(ifp, &icur, &irec);
		if (!isnullstartblock(irec.br_startblock))
			ifp->if_nextents++;

		xfs_iext_next(ifp, &icur);
	}

	return xrep_ino_ensure_extent_count(rb->sc, rb->whichfork,
			ifp->if_nextents);
}

/*
 * Reserve new btree blocks, bulk load the bmap records into the ondisk btree,
 * and load the incore extent tree.
 */
STATIC int
xrep_bmap_btree_load(
	struct xrep_bmap	*rb,
	struct xfs_btree_cur	*bmap_cur)
{
	struct xfs_scrub	*sc = rb->sc;
	int			error;

	/* Compute how many blocks we'll need. */
	error = xfs_btree_bload_compute_geometry(bmap_cur,
			&rb->new_bmapbt.bload, rb->real_mappings);
	if (error)
		return error;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		return error;

	/*
	 * Guess how many blocks we're going to need to rebuild an entire bmap
	 * from the number of extents we found, and pump up our transaction to
	 * have sufficient block reservation.  We're allowed to exceed file
	 * quota to repair inconsistent metadata.
	 */
	error = xfs_trans_reserve_more_inode(sc->tp, sc->ip,
			rb->new_bmapbt.bload.nr_blocks, 0, true);
	if (error)
		return error;

	/* Reserve the space we'll need for the new btree. */
	error = xrep_newbt_alloc_blocks(&rb->new_bmapbt,
			rb->new_bmapbt.bload.nr_blocks);
	if (error)
		return error;

	/* Add all observed bmap records. */
	rb->array_cur = XFARRAY_CURSOR_INIT;
	error = xfs_btree_bload(bmap_cur, &rb->new_bmapbt.bload, rb);
	if (error)
		return error;

	/*
	 * Load the new bmap records into the new incore extent tree to
	 * preserve delalloc reservations for regular files.  The directory
	 * code loads the extent tree during xfs_dir_open and assumes
	 * thereafter that it remains loaded, so we must not violate that
	 * assumption.
	 */
	return xrep_bmap_extents_load(rb);
}

/*
 * Use the collected bmap information to stage a new bmap fork.  If this is
 * successful we'll return with the new fork information logged to the repair
 * transaction but not yet committed.  The caller must ensure that the inode
 * is joined to the transaction; the inode will be joined to a clean
 * transaction when the function returns.
 */
STATIC int
xrep_bmap_build_new_fork(
	struct xrep_bmap	*rb)
{
	struct xfs_owner_info	oinfo;
	struct xfs_scrub	*sc = rb->sc;
	struct xfs_btree_cur	*bmap_cur;
	struct xbtree_ifakeroot	*ifake = &rb->new_bmapbt.ifake;
	int			error;

	error = xrep_bmap_sort_records(rb);
	if (error)
		return error;

	/*
	 * Prepare to construct the new fork by initializing the new btree
	 * structure and creating a fake ifork in the ifakeroot structure.
	 */
	xfs_rmap_ino_bmbt_owner(&oinfo, sc->ip->i_ino, rb->whichfork);
	error = xrep_newbt_init_inode(&rb->new_bmapbt, sc, rb->whichfork,
			&oinfo);
	if (error)
		return error;

	rb->new_bmapbt.bload.get_records = xrep_bmap_get_records;
	rb->new_bmapbt.bload.claim_block = xrep_bmap_claim_block;
	rb->new_bmapbt.bload.iroot_size = xrep_bmap_iroot_size;
	bmap_cur = xfs_bmbt_stage_cursor(sc->mp, sc->ip, ifake);

	/*
	 * Figure out the size and format of the new fork, then fill it with
	 * all the bmap records we've found.  Join the inode to the transaction
	 * so that we can roll the transaction while holding the inode locked.
	 */
	if (rb->real_mappings <= XFS_IFORK_MAXEXT(sc->ip, rb->whichfork)) {
		ifake->if_fork->if_format = XFS_DINODE_FMT_EXTENTS;
		error = xrep_bmap_extents_load(rb);
	} else {
		ifake->if_fork->if_format = XFS_DINODE_FMT_BTREE;
		error = xrep_bmap_btree_load(rb, bmap_cur);
	}
	if (error)
		goto err_cur;

	/*
	 * Install the new fork in the inode.  After this point the old mapping
	 * data are no longer accessible and the new tree is live.  We delete
	 * the cursor immediately after committing the staged root because the
	 * staged fork might be in extents format.
	 */
	xfs_bmbt_commit_staged_btree(bmap_cur, sc->tp, rb->whichfork);
	xfs_btree_del_cursor(bmap_cur, 0);

	/* Reset the inode counters now that we've changed the fork. */
	error = xrep_bmap_reset_counters(rb);
	if (error)
		goto err_newbt;

	/* Dispose of any unused blocks and the accounting information. */
	error = xrep_newbt_commit(&rb->new_bmapbt);
	if (error)
		return error;

	return xrep_roll_trans(sc);

err_cur:
	if (bmap_cur)
		xfs_btree_del_cursor(bmap_cur, error);
err_newbt:
	xrep_newbt_cancel(&rb->new_bmapbt);
	return error;
}

/*
 * Now that we've logged the new inode btree, invalidate all of the old blocks
 * and free them, if there were any.
 */
STATIC int
xrep_bmap_remove_old_tree(
	struct xrep_bmap	*rb)
{
	struct xfs_scrub	*sc = rb->sc;
	struct xfs_owner_info	oinfo;

	/* Free the old bmbt blocks if they're not in use. */
	xfs_rmap_ino_bmbt_owner(&oinfo, sc->ip->i_ino, rb->whichfork);
	return xrep_reap_fsblocks(sc, &rb->old_bmbt_blocks, &oinfo);
}

/* Check for garbage inputs.  Returns -ECANCELED if there's nothing to do. */
STATIC int
xrep_bmap_check_inputs(
	struct xfs_scrub	*sc,
	int			whichfork)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(sc->ip, whichfork);

	ASSERT(whichfork == XFS_DATA_FORK || whichfork == XFS_ATTR_FORK);

	if (!xfs_has_rmapbt(sc->mp))
		return -EOPNOTSUPP;

	/* No fork means nothing to rebuild. */
	if (!ifp)
		return -ECANCELED;

	/*
	 * We only know how to repair extent mappings, which is to say that we
	 * only support extents and btree fork format.  Repairs to a local
	 * format fork require a higher level repair function, so we do not
	 * have any work to do here.
	 */
	switch (ifp->if_format) {
	case XFS_DINODE_FMT_DEV:
	case XFS_DINODE_FMT_LOCAL:
	case XFS_DINODE_FMT_UUID:
		return -ECANCELED;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		break;
	default:
		return -EFSCORRUPTED;
	}

	if (whichfork == XFS_ATTR_FORK)
		return 0;

	/* Only files, symlinks, and directories get to have data forks. */
	switch (VFS_I(sc->ip)->i_mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		/* ok */
		break;
	default:
		return -EINVAL;
	}

	/* Don't know how to rebuild realtime data forks. */
	if (XFS_IS_REALTIME_INODE(sc->ip))
		return -EOPNOTSUPP;

	return 0;
}

/* Set up the initial state of the reflink scan. */
static inline enum reflink_scan_state
xrep_bmap_init_reflink_scan(
	struct xfs_scrub	*sc,
	int			whichfork)
{
	/* cannot share on non-reflink filesystem */
	if (!xfs_has_reflink(sc->mp))
		return RLS_IRRELEVANT;

	/* preserve flag if it's already set */
	if (xfs_is_reflink_inode(sc->ip))
		return RLS_SET_IFLAG;

	/* can only share regular files */
	if (!S_ISREG(VFS_I(sc->ip)->i_mode))
		return RLS_IRRELEVANT;

	/* cannot share attr fork extents */
	if (whichfork != XFS_DATA_FORK)
		return RLS_IRRELEVANT;

	/* cannot share realtime extents */
	if (XFS_IS_REALTIME_INODE(sc->ip))
		return RLS_IRRELEVANT;

	return RLS_UNKNOWN;
}

/* Repair an inode fork. */
int
xrep_bmap(
	struct xfs_scrub	*sc,
	int			whichfork,
	bool			allow_unwritten)
{
	struct xrep_bmap	*rb;
	char			*descr;
	unsigned int		max_bmbt_recs;
	bool			large_extcount;
	int			error = 0;

	error = xrep_bmap_check_inputs(sc, whichfork);
	if (error == -ECANCELED)
		return 0;
	if (error)
		return error;

	rb = kzalloc(sizeof(struct xrep_bmap), XCHK_GFP_FLAGS);
	if (!rb)
		return -ENOMEM;
	rb->sc = sc;
	rb->whichfork = whichfork;
	rb->reflink_scan = xrep_bmap_init_reflink_scan(sc, whichfork);
	rb->allow_unwritten = allow_unwritten;

	/* Set up enough storage to handle the max records for this fork. */
	large_extcount = xfs_has_large_extent_counts(sc->mp);
	max_bmbt_recs = xfs_iext_max_nextents(large_extcount, whichfork);
	descr = xchk_xfile_ino_descr(sc, "%s fork mapping records",
			whichfork == XFS_DATA_FORK ? "data" : "attr");
	error = xfarray_create(descr, max_bmbt_recs,
			sizeof(struct xfs_bmbt_rec), &rb->bmap_records);
	kfree(descr);
	if (error)
		goto out_rb;

	/* Collect all reverse mappings for this fork's extents. */
	xfsb_bitmap_init(&rb->old_bmbt_blocks);
	error = xrep_bmap_find_mappings(rb);
	if (error)
		goto out_bitmap;

	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	/* Rebuild the bmap information. */
	error = xrep_bmap_build_new_fork(rb);
	if (error)
		goto out_bitmap;

	/* Kill the old tree. */
	error = xrep_bmap_remove_old_tree(rb);
	if (error)
		goto out_bitmap;

out_bitmap:
	xfsb_bitmap_destroy(&rb->old_bmbt_blocks);
	xfarray_destroy(rb->bmap_records);
out_rb:
	kfree(rb);
	return error;
}

/* Repair an inode's data fork. */
int
xrep_bmap_data(
	struct xfs_scrub	*sc)
{
	return xrep_bmap(sc, XFS_DATA_FORK, true);
}

/* Repair an inode's attr fork. */
int
xrep_bmap_attr(
	struct xfs_scrub	*sc)
{
	return xrep_bmap(sc, XFS_ATTR_FORK, false);
}
