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
	if (irec->br_startblock + irec->br_blockcount <= irec->br_startblock)
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (info->is_rt &&
	    (!xfs_verify_rtbno(mp, irec->br_startblock) ||
	     !xfs_verify_rtbno(mp, irec->br_startblock +
				irec->br_blockcount - 1)))
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);
	if (!info->is_rt &&
	    (!xfs_verify_fsbno(mp, irec->br_startblock) ||
	     !xfs_verify_fsbno(mp, irec->br_startblock +
				irec->br_blockcount - 1)))
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

	/* We don't allow unwritten extents on attr forks. */
	if (irec->br_state == XFS_EXT_UNWRITTEN &&
	    info->whichfork == XFS_ATTR_FORK)
		xfs_scrub_fblock_set_corrupt(info->sc, info->whichfork,
				irec->br_startoff);

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
	bool				found;
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
	for (found = xfs_iext_lookup_extent(ip, ifp, 0, &icur, &irec);
	     found != 0;
	     found = xfs_iext_next_extent(ifp, &icur, &irec)) {
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
