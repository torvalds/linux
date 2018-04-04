/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
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
#include "xfs_refcount.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/* Set us up with an inode's bmap. */
int
xfs_scrub_setup_inode_bmap(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	struct xfs_mount		*mp = sc->mp;
	int				error;

	error = xfs_scrub_get_inode(sc, ip);
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
	error = xfs_scrub_trans_alloc(sc->sm, mp, &sc->tp);
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

struct xfs_scrub_bmap_info {
	struct xfs_scrub_context	*sc;
	xfs_fileoff_t			lastoff;
	bool				is_rt;
	bool				is_shared;
	int				whichfork;
};

/* Look for a corresponding rmap for this irec. */
static inline bool
xfs_scrub_bmap_get_rmap(
	struct xfs_scrub_bmap_info	*info,
	struct xfs_bmbt_irec		*irec,
	xfs_agblock_t			agbno,
	uint64_t			owner,
	struct xfs_rmap_irec		*rmap)
{
	xfs_fileoff_t			offset;
	unsigned int			rflags = 0;
	int				has_rmap;
	int				error;

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
		if (!xfs_scrub_should_check_xref(info->sc, &error,
				&info->sc->sa.rmap_cur))
			return false;
		goto out;
	}

	/*
	 * Otherwise, use the (faster) regular lookup.
	 */
	error = xfs_rmap_lookup_le(info->sc->sa.rmap_cur, agbno, 0, owner,
			offset, rflags, &has_rmap);
	if (!xfs_scrub_should_check_xref(info->sc, &error,
			&info->sc->sa.rmap_cur))
		return false;
	if (!has_rmap)
		goto out;

	error = xfs_rmap_get_rec(info->sc->sa.rmap_cur, rmap, &has_rmap);
	if (!xfs_scrub_should_check_xref(info->sc, &error,
			&info->sc->sa.rmap_cur))
		return false;

out:
	if (!has_rmap)
		xfs_scrub_fblock_xref_set_corrupt(info->sc, info->whichfork,
			irec->br_startoff);
	return has_rmap;
}

/* Make sure that we have rmapbt records for this extent. */
STATIC void
xfs_scrub_bmap_xref_rmap(
	struct xfs_scrub_bmap_info	*info,
	struct xfs_bmbt_irec		*irec,
	xfs_agblock_t			agbno)
{
	struct xfs_rmap_irec		rmap;
	unsigned long long		rmap_end;
	uint64_t			owner;

	if (!info->sc->sa.rmap_cur)
		return;

	if (info->whichfork == XFS_COW_FORK)
		owner = XFS_RMAP_OWN_COW;
	else
		owner = info->sc->ip->i_ino;

	/* Find the rmap record for this irec. */
	if (!xfs_scrub_bmap_get_rmap(info, irec, agbno, owner, &rmap))
		return;

	/* Check the rmap. */
	rmap_end = (unsigned long long)rmap.rm_startblock + rmap.rm_blockcount;
	if (rmap.rm_startblock > agbno ||
	    agbno + irec->br_blockcount > rmap_end)
		xfs_scrub_fblock_xref_set_corrupt(info->sc, info->whichfork,
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
			xfs_scrub_fblock_xref_set_corrupt(info->sc,
					info->whichfork, irec->br_startoff);
	}

	if (rmap.rm_owner != owner)
		xfs_scrub_fblock_xref_set_corrupt(info->sc, info->whichfork,
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
		xfs_scrub_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	if (info->whichfork == XFS_ATTR_FORK &&
	    !(rmap.rm_flags & XFS_RMAP_ATTR_FORK))
		xfs_scrub_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (rmap.rm_flags & XFS_RMAP_BMBT_BLOCK)
		xfs_scrub_fblock_xref_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
}

/* Cross-reference a single rtdev extent record. */
STATIC void
xfs_scrub_bmap_rt_extent_xref(
	struct xfs_scrub_bmap_info	*info,
	struct xfs_inode		*ip,
	struct xfs_btree_cur		*cur,
	struct xfs_bmbt_irec		*irec)
{
	if (info->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xfs_scrub_xref_is_used_rt_space(info->sc, irec->br_startblock,
			irec->br_blockcount);
}

/* Cross-reference a single datadev extent record. */
STATIC void
xfs_scrub_bmap_extent_xref(
	struct xfs_scrub_bmap_info	*info,
	struct xfs_inode		*ip,
	struct xfs_btree_cur		*cur,
	struct xfs_bmbt_irec		*irec)
{
	struct xfs_mount		*mp = info->sc->mp;
	xfs_agnumber_t			agno;
	xfs_agblock_t			agbno;
	xfs_extlen_t			len;
	int				error;

	if (info->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	agno = XFS_FSB_TO_AGNO(mp, irec->br_startblock);
	agbno = XFS_FSB_TO_AGBNO(mp, irec->br_startblock);
	len = irec->br_blockcount;

	error = xfs_scrub_ag_init(info->sc, agno, &info->sc->sa);
	if (!xfs_scrub_fblock_process_error(info->sc, info->whichfork,
			irec->br_startoff, &error))
		return;

	xfs_scrub_xref_is_used_space(info->sc, agbno, len);
	xfs_scrub_xref_is_not_inode_chunk(info->sc, agbno, len);
	xfs_scrub_bmap_xref_rmap(info, irec, agbno);
	switch (info->whichfork) {
	case XFS_DATA_FORK:
		if (xfs_is_reflink_inode(info->sc->ip))
			break;
		/* fall through */
	case XFS_ATTR_FORK:
		xfs_scrub_xref_is_not_shared(info->sc, agbno,
				irec->br_blockcount);
		break;
	case XFS_COW_FORK:
		xfs_scrub_xref_is_cow_staging(info->sc, agbno,
				irec->br_blockcount);
		break;
	}

	xfs_scrub_ag_free(info->sc, &info->sc->sa);
}

/* Scrub a single extent record. */
STATIC int
xfs_scrub_bmap_extent(
	struct xfs_inode		*ip,
	struct xfs_btree_cur		*cur,
	struct xfs_scrub_bmap_info	*info,
	struct xfs_bmbt_irec		*irec)
{
	struct xfs_mount		*mp = info->sc->mp;
	struct xfs_buf			*bp = NULL;
	xfs_filblks_t			end;
	int				error = 0;

	if (cur)
		xfs_btree_get_block(cur, 0, &bp);

	/*
	 * Check for out-of-order extents.  This record could have come
	 * from the incore list, for which there is no ordering check.
	 */
	if (irec->br_startoff < info->lastoff)
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* There should never be a "hole" extent in either extent list. */
	if (irec->br_startblock == HOLESTARTBLOCK)
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/*
	 * Check for delalloc extents.  We never iterate the ones in the
	 * in-core extent scan, and we should never see these in the bmbt.
	 */
	if (isnullstartblock(irec->br_startblock))
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* Make sure the extent points to a valid place. */
	if (irec->br_blockcount > MAXEXTLEN)
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (irec->br_startblock + irec->br_blockcount <= irec->br_startblock)
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	end = irec->br_startblock + irec->br_blockcount - 1;
	if (info->is_rt &&
	    (!xfs_verify_rtbno(mp, irec->br_startblock) ||
	     !xfs_verify_rtbno(mp, end)))
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (!info->is_rt &&
	    (!xfs_verify_fsbno(mp, irec->br_startblock) ||
	     !xfs_verify_fsbno(mp, end) ||
	     XFS_FSB_TO_AGNO(mp, irec->br_startblock) !=
				XFS_FSB_TO_AGNO(mp, end)))
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* We don't allow unwritten extents on attr forks. */
	if (irec->br_state == XFS_EXT_UNWRITTEN &&
	    info->whichfork == XFS_ATTR_FORK)
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	if (info->is_rt)
		xfs_scrub_bmap_rt_extent_xref(info, ip, cur, irec);
	else
		xfs_scrub_bmap_extent_xref(info, ip, cur, irec);

	info->lastoff = irec->br_startoff + irec->br_blockcount;
	return error;
}

/* Scrub a bmbt record. */
STATIC int
xfs_scrub_bmapbt_rec(
	struct xfs_scrub_btree		*bs,
	union xfs_btree_rec		*rec)
{
	struct xfs_bmbt_irec		irec;
	struct xfs_scrub_bmap_info	*info = bs->private;
	struct xfs_inode		*ip = bs->cur->bc_private.b.ip;
	struct xfs_buf			*bp = NULL;
	struct xfs_btree_block		*block;
	uint64_t			owner;
	int				i;

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
				xfs_scrub_fblock_set_corrupt(bs->sc,
						info->whichfork, 0);
		}
	}

	/* Set up the in-core record and scrub it. */
	xfs_bmbt_disk_get_all(&rec->bmbt, &irec);
	return xfs_scrub_bmap_extent(ip, bs->cur, info, &irec);
}

/* Scan the btree records. */
STATIC int
xfs_scrub_bmap_btree(
	struct xfs_scrub_context	*sc,
	int				whichfork,
	struct xfs_scrub_bmap_info	*info)
{
	struct xfs_owner_info		oinfo;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_inode		*ip = sc->ip;
	struct xfs_btree_cur		*cur;
	int				error;

	cur = xfs_bmbt_init_cursor(mp, sc->tp, ip, whichfork);
	xfs_rmap_ino_bmbt_owner(&oinfo, ip->i_ino, whichfork);
	error = xfs_scrub_btree(sc, cur, xfs_scrub_bmapbt_rec, &oinfo, info);
	xfs_btree_del_cursor(cur, error ? XFS_BTREE_ERROR :
					  XFS_BTREE_NOERROR);
	return error;
}

/*
 * Scrub an inode fork's block mappings.
 *
 * First we scan every record in every btree block, if applicable.
 * Then we unconditionally scan the incore extent cache.
 */
STATIC int
xfs_scrub_bmap(
	struct xfs_scrub_context	*sc,
	int				whichfork)
{
	struct xfs_bmbt_irec		irec;
	struct xfs_scrub_bmap_info	info = { NULL };
	struct xfs_mount		*mp = sc->mp;
	struct xfs_inode		*ip = sc->ip;
	struct xfs_ifork		*ifp;
	xfs_fileoff_t			endoff;
	struct xfs_iext_cursor		icur;
	int				error = 0;

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
			xfs_scrub_ino_set_corrupt(sc, sc->ip->i_ino, NULL);
			goto out;
		}
		break;
	case XFS_ATTR_FORK:
		if (!ifp)
			goto out;
		if (!xfs_sb_version_hasattr(&mp->m_sb) &&
		    !xfs_sb_version_hasattr2(&mp->m_sb))
			xfs_scrub_ino_set_corrupt(sc, sc->ip->i_ino, NULL);
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
			xfs_scrub_fblock_set_corrupt(sc, whichfork, 0);
			goto out;
		}
		break;
	case XFS_DINODE_FMT_BTREE:
		if (whichfork == XFS_COW_FORK) {
			xfs_scrub_fblock_set_corrupt(sc, whichfork, 0);
			goto out;
		}

		error = xfs_scrub_bmap_btree(sc, whichfork, &info);
		if (error)
			goto out;
		break;
	default:
		xfs_scrub_fblock_set_corrupt(sc, whichfork, 0);
		goto out;
	}

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Now try to scrub the in-memory extent list. */
        if (!(ifp->if_flags & XFS_IFEXTENTS)) {
		error = xfs_iread_extents(sc->tp, ip, whichfork);
		if (!xfs_scrub_fblock_process_error(sc, whichfork, 0, &error))
			goto out;
	}

	/* Find the offset of the last extent in the mapping. */
	error = xfs_bmap_last_offset(ip, &endoff, whichfork);
	if (!xfs_scrub_fblock_process_error(sc, whichfork, 0, &error))
		goto out;

	/* Scrub extent records. */
	info.lastoff = 0;
	ifp = XFS_IFORK_PTR(ip, whichfork);
	for_each_xfs_iext(ifp, &icur, &irec) {
		if (xfs_scrub_should_terminate(sc, &error))
			break;
		if (isnullstartblock(irec.br_startblock))
			continue;
		if (irec.br_startoff >= endoff) {
			xfs_scrub_fblock_set_corrupt(sc, whichfork,
					irec.br_startoff);
			goto out;
		}
		error = xfs_scrub_bmap_extent(ip, NULL, &info, &irec);
		if (error)
			goto out;
	}

out:
	return error;
}

/* Scrub an inode's data fork. */
int
xfs_scrub_bmap_data(
	struct xfs_scrub_context	*sc)
{
	return xfs_scrub_bmap(sc, XFS_DATA_FORK);
}

/* Scrub an inode's attr fork. */
int
xfs_scrub_bmap_attr(
	struct xfs_scrub_context	*sc)
{
	return xfs_scrub_bmap(sc, XFS_ATTR_FORK);
}

/* Scrub an inode's CoW fork. */
int
xfs_scrub_bmap_cow(
	struct xfs_scrub_context	*sc)
{
	if (!xfs_is_reflink_inode(sc->ip))
		return -ENOENT;

	return xfs_scrub_bmap(sc, XFS_COW_FORK);
}
