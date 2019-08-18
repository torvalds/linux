// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
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
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/* Set us up with an inode's bmap. */
int
xchk_setup_inode_bmap(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	int			error;

	error = xchk_get_inode(sc, ip);
	if (error)
		goto out;

	sc->ilock_flags = XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL;
	xfs_ilock(sc->ip, sc->ilock_flags);

	/*
	 * We don't want any ephemeral data fork updates sitting around
	 * while we inspect block mappings, so wait for directio to finish
	 * and flush dirty data if we have delalloc reservations.
	 */
	if (S_ISREG(VFS_I(sc->ip)->i_mode) &&
	    sc->sm->sm_type == XFS_SCRUB_TYPE_BMBTD) {
		inode_dio_wait(VFS_I(sc->ip));
		error = filemap_write_and_wait(VFS_I(sc->ip)->i_mapping);
		if (error)
			goto out;
	}

	/* Got the inode, lock it and we're ready to go. */
	error = xchk_trans_alloc(sc, 0);
	if (error)
		goto out;
	sc->ilock_flags |= XFS_ILOCK_EXCL;
	xfs_ilock(sc->ip, XFS_ILOCK_EXCL);

out:
	/* scrub teardown will unlock and release the inode */
	return error;
}

/*
 * Inode fork block mapping (BMBT) scrubber.
 * More complex than the others because we have to scrub
 * all the extents regardless of whether or not the fork
 * is in btree format.
 */

struct xchk_bmap_info {
	struct xfs_scrub	*sc;
	xfs_fileoff_t		lastoff;
	bool			is_rt;
	bool			is_shared;
	int			whichfork;
};

/* Look for a corresponding rmap for this irec. */
static inline bool
xchk_bmap_get_rmap(
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec,
	xfs_agblock_t		agbno,
	uint64_t		owner,
	struct xfs_rmap_irec	*rmap)
{
	xfs_fileoff_t		offset;
	unsigned int		rflags = 0;
	int			has_rmap;
	int			error;

	if (info->whichfork == XFS_ATTR_FORK)
		rflags |= XFS_RMAP_ATTR_FORK;

	/*
	 * CoW staging extents are owned (on disk) by the refcountbt, so
	 * their rmaps do not have offsets.
	 */
	if (info->whichfork == XFS_COW_FORK)
		offset = 0;
	else
		offset = irec->br_startoff;

	/*
	 * If the caller thinks this could be a shared bmbt extent (IOWs,
	 * any data fork extent of a reflink inode) then we have to use the
	 * range rmap lookup to make sure we get the correct owner/offset.
	 */
	if (info->is_shared) {
		error = xfs_rmap_lookup_le_range(info->sc->sa.rmap_cur, agbno,
				owner, offset, rflags, rmap, &has_rmap);
		if (!xchk_should_check_xref(info->sc, &error,
				&info->sc->sa.rmap_cur))
			return false;
		goto out;
	}

	/*
	 * Otherwise, use the (faster) regular lookup.
	 */
	error = xfs_rmap_lookup_le(info->sc->sa.rmap_cur, agbno, 0, owner,
			offset, rflags, &has_rmap);
	if (!xchk_should_check_xref(info->sc, &error,
			&info->sc->sa.rmap_cur))
		return false;
	if (!has_rmap)
		goto out;

	error = xfs_rmap_get_rec(info->sc->sa.rmap_cur, rmap, &has_rmap);
	if (!xchk_should_check_xref(info->sc, &error,
			&info->sc->sa.rmap_cur))
		return false;

out:
	if (!has_rmap)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
			irec->br_startoff);
	return has_rmap;
}

/* Make sure that we have rmapbt records for this extent. */
STATIC void
xchk_bmap_xref_rmap(
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec,
	xfs_agblock_t		agbno)
{
	struct xfs_rmap_irec	rmap;
	unsigned long long	rmap_end;
	uint64_t		owner;

	if (!info->sc->sa.rmap_cur || xchk_skip_xref(info->sc->sm))
		return;

	if (info->whichfork == XFS_COW_FORK)
		owner = XFS_RMAP_OWN_COW;
	else
		owner = info->sc->ip->i_ino;

	/* Find the rmap record for this irec. */
	if (!xchk_bmap_get_rmap(info, irec, agbno, owner, &rmap))
		return;

	/* Check the rmap. */
	rmap_end = (unsigned long long)rmap.rm_startblock + rmap.rm_blockcount;
	if (rmap.rm_startblock > agbno ||
	    agbno + irec->br_blockcount > rmap_end)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/*
	 * Check the logical offsets if applicable.  CoW staging extents
	 * don't track logical offsets since the mappings only exist in
	 * memory.
	 */
	if (info->whichfork != XFS_COW_FORK) {
		rmap_end = (unsigned long long)rmap.rm_offset +
				rmap.rm_blockcount;
		if (rmap.rm_offset > irec->br_startoff ||
		    irec->br_startoff + irec->br_blockcount > rmap_end)
			xchk_fblock_xref_set_corrupt(info->sc,
					info->whichfork, irec->br_startoff);
	}

	if (rmap.rm_owner != owner)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/*
	 * Check for discrepancies between the unwritten flag in the irec and
	 * the rmap.  Note that the (in-memory) CoW fork distinguishes between
	 * unwritten and written extents, but we don't track that in the rmap
	 * records because the blocks are owned (on-disk) by the refcountbt,
	 * which doesn't track unwritten state.
	 */
	if (owner != XFS_RMAP_OWN_COW &&
	    irec->br_state == XFS_EXT_UNWRITTEN &&
	    !(rmap.rm_flags & XFS_RMAP_UNWRITTEN))
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	if (info->whichfork == XFS_ATTR_FORK &&
	    !(rmap.rm_flags & XFS_RMAP_ATTR_FORK))
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (rmap.rm_flags & XFS_RMAP_BMBT_BLOCK)
		xchk_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
}

/* Cross-reference a single rtdev extent record. */
STATIC void
xchk_bmap_rt_extent_xref(
	struct xchk_bmap_info	*info,
	struct xfs_inode	*ip,
	struct xfs_btree_cur	*cur,
	struct xfs_bmbt_irec	*irec)
{
	if (info->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_rt_space(info->sc, irec->br_startblock,
			irec->br_blockcount);
}

/* Cross-reference a single datadev extent record. */
STATIC void
xchk_bmap_extent_xref(
	struct xchk_bmap_info	*info,
	struct xfs_inode	*ip,
	struct xfs_btree_cur	*cur,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_mount	*mp = info->sc->mp;
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;
	xfs_extlen_t		len;
	int			error;

	if (info->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	agno = XFS_FSB_TO_AGNO(mp, irec->br_startblock);
	agbno = XFS_FSB_TO_AGBNO(mp, irec->br_startblock);
	len = irec->br_blockcount;

	error = xchk_ag_init(info->sc, agno, &info->sc->sa);
	if (!xchk_fblock_process_error(info->sc, info->whichfork,
			irec->br_startoff, &error))
		return;

	xchk_xref_is_used_space(info->sc, agbno, len);
	xchk_xref_is_not_inode_chunk(info->sc, agbno, len);
	xchk_bmap_xref_rmap(info, irec, agbno);
	switch (info->whichfork) {
	case XFS_DATA_FORK:
		if (xfs_is_reflink_inode(info->sc->ip))
			break;
		/* fall through */
	case XFS_ATTR_FORK:
		xchk_xref_is_not_shared(info->sc, agbno,
				irec->br_blockcount);
		break;
	case XFS_COW_FORK:
		xchk_xref_is_cow_staging(info->sc, agbno,
				irec->br_blockcount);
		break;
	}

	xchk_ag_free(info->sc, &info->sc->sa);
}

/*
 * Directories and attr forks should never have blocks that can't be addressed
 * by a xfs_dablk_t.
 */
STATIC void
xchk_bmap_dirattr_extent(
	struct xfs_inode	*ip,
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		off;

	if (!S_ISDIR(VFS_I(ip)->i_mode) && info->whichfork != XFS_ATTR_FORK)
		return;

	if (!xfs_verify_dablk(mp, irec->br_startoff))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	off = irec->br_startoff + irec->br_blockcount - 1;
	if (!xfs_verify_dablk(mp, off))
		xchk_fblock_set_corrupt(info->sc, info->whichfork, off);
}

/* Scrub a single extent record. */
STATIC int
xchk_bmap_extent(
	struct xfs_inode	*ip,
	struct xfs_btree_cur	*cur,
	struct xchk_bmap_info	*info,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_mount	*mp = info->sc->mp;
	struct xfs_buf		*bp = NULL;
	xfs_filblks_t		end;
	int			error = 0;

	if (cur)
		xfs_btree_get_block(cur, 0, &bp);

	/*
	 * Check for out-of-order extents.  This record could have come
	 * from the incore list, for which there is no ordering check.
	 */
	if (irec->br_startoff < info->lastoff)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	xchk_bmap_dirattr_extent(ip, info, irec);

	/* There should never be a "hole" extent in either extent list. */
	if (irec->br_startblock == HOLESTARTBLOCK)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/*
	 * Check for delalloc extents.  We never iterate the ones in the
	 * in-core extent scan, and we should never see these in the bmbt.
	 */
	if (isnullstartblock(irec->br_startblock))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* Make sure the extent points to a valid place. */
	if (irec->br_blockcount > MAXEXTLEN)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (irec->br_startblock + irec->br_blockcount <= irec->br_startblock)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	end = irec->br_startblock + irec->br_blockcount - 1;
	if (info->is_rt &&
	    (!xfs_verify_rtbno(mp, irec->br_startblock) ||
	     !xfs_verify_rtbno(mp, end)))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (!info->is_rt &&
	    (!xfs_verify_fsbno(mp, irec->br_startblock) ||
	     !xfs_verify_fsbno(mp, end) ||
	     XFS_FSB_TO_AGNO(mp, irec->br_startblock) !=
				XFS_FSB_TO_AGNO(mp, end)))
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* We don't allow unwritten extents on attr forks. */
	if (irec->br_state == XFS_EXT_UNWRITTEN &&
	    info->whichfork == XFS_ATTR_FORK)
		xchk_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	if (info->is_rt)
		xchk_bmap_rt_extent_xref(info, ip, cur, irec);
	else
		xchk_bmap_extent_xref(info, ip, cur, irec);

	info->lastoff = irec->br_startoff + irec->br_blockcount;
	return error;
}

/* Scrub a bmbt record. */
STATIC int
xchk_bmapbt_rec(
	struct xchk_btree	*bs,
	union xfs_btree_rec	*rec)
{
	struct xfs_bmbt_irec	irec;
	struct xchk_bmap_info	*info = bs->private;
	struct xfs_inode	*ip = bs->cur->bc_private.b.ip;
	struct xfs_buf		*bp = NULL;
	struct xfs_btree_block	*block;
	uint64_t		owner;
	int			i;

	/*
	 * Check the owners of the btree blocks up to the level below
	 * the root since the verifiers don't do that.
	 */
	if (xfs_sb_version_hascrc(&bs->cur->bc_mp->m_sb) &&
	    bs->cur->bc_ptrs[0] == 1) {
		for (i = 0; i < bs->cur->bc_nlevels - 1; i++) {
			block = xfs_btree_get_block(bs->cur, i, &bp);
			owner = be64_to_cpu(block->bb_u.l.bb_owner);
			if (owner != ip->i_ino)
				xchk_fblock_set_corrupt(bs->sc,
						info->whichfork, 0);
		}
	}

	/* Set up the in-core record and scrub it. */
	xfs_bmbt_disk_get_all(&rec->bmbt, &irec);
	return xchk_bmap_extent(ip, bs->cur, info, &irec);
}

/* Scan the btree records. */
STATIC int
xchk_bmap_btree(
	struct xfs_scrub	*sc,
	int			whichfork,
	struct xchk_bmap_info	*info)
{
	struct xfs_owner_info	oinfo;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*ip = sc->ip;
	struct xfs_btree_cur	*cur;
	int			error;

	cur = xfs_bmbt_init_cursor(mp, sc->tp, ip, whichfork);
	xfs_rmap_ino_bmbt_owner(&oinfo, ip->i_ino, whichfork);
	error = xchk_btree(sc, cur, xchk_bmapbt_rec, &oinfo, info);
	xfs_btree_del_cursor(cur, error);
	return error;
}

struct xchk_bmap_check_rmap_info {
	struct xfs_scrub	*sc;
	int			whichfork;
	struct xfs_iext_cursor	icur;
};

/* Can we find bmaps that fit this rmap? */
STATIC int
xchk_bmap_check_rmap(
	struct xfs_btree_cur		*cur,
	struct xfs_rmap_irec		*rec,
	void				*priv)
{
	struct xfs_bmbt_irec		irec;
	struct xchk_bmap_check_rmap_info	*sbcri = priv;
	struct xfs_ifork		*ifp;
	struct xfs_scrub		*sc = sbcri->sc;
	bool				have_map;

	/* Is this even the right fork? */
	if (rec->rm_owner != sc->ip->i_ino)
		return 0;
	if ((sbcri->whichfork == XFS_ATTR_FORK) ^
	    !!(rec->rm_flags & XFS_RMAP_ATTR_FORK))
		return 0;
	if (rec->rm_flags & XFS_RMAP_BMBT_BLOCK)
		return 0;

	/* Now look up the bmbt record. */
	ifp = XFS_IFORK_PTR(sc->ip, sbcri->whichfork);
	if (!ifp) {
		xchk_fblock_set_corrupt(sc, sbcri->whichfork,
				rec->rm_offset);
		goto out;
	}
	have_map = xfs_iext_lookup_extent(sc->ip, ifp, rec->rm_offset,
			&sbcri->icur, &irec);
	if (!have_map)
		xchk_fblock_set_corrupt(sc, sbcri->whichfork,
				rec->rm_offset);
	/*
	 * bmap extent record lengths are constrained to 2^21 blocks in length
	 * because of space constraints in the on-disk metadata structure.
	 * However, rmap extent record lengths are constrained only by AG
	 * length, so we have to loop through the bmbt to make sure that the
	 * entire rmap is covered by bmbt records.
	 */
	while (have_map) {
		if (irec.br_startoff != rec->rm_offset)
			xchk_fblock_set_corrupt(sc, sbcri->whichfork,
					rec->rm_offset);
		if (irec.br_startblock != XFS_AGB_TO_FSB(sc->mp,
				cur->bc_private.a.agno, rec->rm_startblock))
			xchk_fblock_set_corrupt(sc, sbcri->whichfork,
					rec->rm_offset);
		if (irec.br_blockcount > rec->rm_blockcount)
			xchk_fblock_set_corrupt(sc, sbcri->whichfork,
					rec->rm_offset);
		if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			break;
		rec->rm_startblock += irec.br_blockcount;
		rec->rm_offset += irec.br_blockcount;
		rec->rm_blockcount -= irec.br_blockcount;
		if (rec->rm_blockcount == 0)
			break;
		have_map = xfs_iext_next_extent(ifp, &sbcri->icur, &irec);
		if (!have_map)
			xchk_fblock_set_corrupt(sc, sbcri->whichfork,
					rec->rm_offset);
	}

out:
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return XFS_BTREE_QUERY_RANGE_ABORT;
	return 0;
}

/* Make sure each rmap has a corresponding bmbt entry. */
STATIC int
xchk_bmap_check_ag_rmaps(
	struct xfs_scrub		*sc,
	int				whichfork,
	xfs_agnumber_t			agno)
{
	struct xchk_bmap_check_rmap_info	sbcri;
	struct xfs_btree_cur		*cur;
	struct xfs_buf			*agf;
	int				error;

	error = xfs_alloc_read_agf(sc->mp, sc->tp, agno, 0, &agf);
	if (error)
		return error;

	cur = xfs_rmapbt_init_cursor(sc->mp, sc->tp, agf, agno);
	if (!cur) {
		error = -ENOMEM;
		goto out_agf;
	}

	sbcri.sc = sc;
	sbcri.whichfork = whichfork;
	error = xfs_rmap_query_all(cur, xchk_bmap_check_rmap, &sbcri);
	if (error == XFS_BTREE_QUERY_RANGE_ABORT)
		error = 0;

	xfs_btree_del_cursor(cur, error);
out_agf:
	xfs_trans_brelse(sc->tp, agf);
	return error;
}

/* Make sure each rmap has a corresponding bmbt entry. */
STATIC int
xchk_bmap_check_rmaps(
	struct xfs_scrub	*sc,
	int			whichfork)
{
	loff_t			size;
	xfs_agnumber_t		agno;
	int			error;

	if (!xfs_sb_version_hasrmapbt(&sc->mp->m_sb) ||
	    whichfork == XFS_COW_FORK ||
	    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return 0;

	/* Don't support realtime rmap checks yet. */
	if (XFS_IS_REALTIME_INODE(sc->ip) && whichfork == XFS_DATA_FORK)
		return 0;

	/*
	 * Only do this for complex maps that are in btree format, or for
	 * situations where we would seem to have a size but zero extents.
	 * The inode repair code can zap broken iforks, which means we have
	 * to flag this bmap as corrupt if there are rmaps that need to be
	 * reattached.
	 */
	switch (whichfork) {
	case XFS_DATA_FORK:
		size = i_size_read(VFS_I(sc->ip));
		break;
	case XFS_ATTR_FORK:
		size = XFS_IFORK_Q(sc->ip);
		break;
	default:
		size = 0;
		break;
	}
	if (XFS_IFORK_FORMAT(sc->ip, whichfork) != XFS_DINODE_FMT_BTREE &&
	    (size == 0 || XFS_IFORK_NEXTENTS(sc->ip, whichfork) > 0))
		return 0;

	for (agno = 0; agno < sc->mp->m_sb.sb_agcount; agno++) {
		error = xchk_bmap_check_ag_rmaps(sc, whichfork, agno);
		if (error)
			return error;
		if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			break;
	}

	return 0;
}

/*
 * Scrub an inode fork's block mappings.
 *
 * First we scan every record in every btree block, if applicable.
 * Then we unconditionally scan the incore extent cache.
 */
STATIC int
xchk_bmap(
	struct xfs_scrub	*sc,
	int			whichfork)
{
	struct xfs_bmbt_irec	irec;
	struct xchk_bmap_info	info = { NULL };
	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*ip = sc->ip;
	struct xfs_ifork	*ifp;
	xfs_fileoff_t		endoff;
	struct xfs_iext_cursor	icur;
	int			error = 0;

	ifp = XFS_IFORK_PTR(ip, whichfork);

	info.is_rt = whichfork == XFS_DATA_FORK && XFS_IS_REALTIME_INODE(ip);
	info.whichfork = whichfork;
	info.is_shared = whichfork == XFS_DATA_FORK && xfs_is_reflink_inode(ip);
	info.sc = sc;

	switch (whichfork) {
	case XFS_COW_FORK:
		/* Non-existent CoW forks are ignorable. */
		if (!ifp)
			goto out;
		/* No CoW forks on non-reflink inodes/filesystems. */
		if (!xfs_is_reflink_inode(ip)) {
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
			goto out;
		}
		break;
	case XFS_ATTR_FORK:
		if (!ifp)
			goto out_check_rmap;
		if (!xfs_sb_version_hasattr(&mp->m_sb) &&
		    !xfs_sb_version_hasattr2(&mp->m_sb))
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		break;
	default:
		ASSERT(whichfork == XFS_DATA_FORK);
		break;
	}

	/* Check the fork values */
	switch (XFS_IFORK_FORMAT(ip, whichfork)) {
	case XFS_DINODE_FMT_UUID:
	case XFS_DINODE_FMT_DEV:
	case XFS_DINODE_FMT_LOCAL:
		/* No mappings to check. */
		goto out;
	case XFS_DINODE_FMT_EXTENTS:
		if (!(ifp->if_flags & XFS_IFEXTENTS)) {
			xchk_fblock_set_corrupt(sc, whichfork, 0);
			goto out;
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		if (whichfork == XFS_COW_FORK) {
			xchk_fblock_set_corrupt(sc, whichfork, 0);
			goto out;
		}

		error = xchk_bmap_btree(sc, whichfork, &info);
		if (error)
			goto out;
		break;
	default:
		xchk_fblock_set_corrupt(sc, whichfork, 0);
		goto out;
	}

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Now try to scrub the in-memory extent list. */
        if (!(ifp->if_flags & XFS_IFEXTENTS)) {
		error = xfs_iread_extents(sc->tp, ip, whichfork);
		if (!xchk_fblock_process_error(sc, whichfork, 0, &error))
			goto out;
	}

	/* Find the offset of the last extent in the mapping. */
	error = xfs_bmap_last_offset(ip, &endoff, whichfork);
	if (!xchk_fblock_process_error(sc, whichfork, 0, &error))
		goto out;

	/* Scrub extent records. */
	info.lastoff = 0;
	ifp = XFS_IFORK_PTR(ip, whichfork);
	for_each_xfs_iext(ifp, &icur, &irec) {
		if (xchk_should_terminate(sc, &error) ||
		    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
			break;
		if (isnullstartblock(irec.br_startblock))
			continue;
		if (irec.br_startoff >= endoff) {
			xchk_fblock_set_corrupt(sc, whichfork,
					irec.br_startoff);
			goto out;
		}
		error = xchk_bmap_extent(ip, NULL, &info, &irec);
		if (error)
			goto out;
	}

out_check_rmap:
	error = xchk_bmap_check_rmaps(sc, whichfork);
	if (!xchk_fblock_xref_process_error(sc, whichfork, 0, &error))
		goto out;
out:
	return error;
}

/* Scrub an inode's data fork. */
int
xchk_bmap_data(
	struct xfs_scrub	*sc)
{
	return xchk_bmap(sc, XFS_DATA_FORK);
}

/* Scrub an inode's attr fork. */
int
xchk_bmap_attr(
	struct xfs_scrub	*sc)
{
	return xchk_bmap(sc, XFS_ATTR_FORK);
}

/* Scrub an inode's CoW fork. */
int
xchk_bmap_cow(
	struct xfs_scrub	*sc)
{
	if (!xfs_is_reflink_inode(sc->ip))
		return -ENOENT;

	return xchk_bmap(sc, XFS_COW_FORK);
}
