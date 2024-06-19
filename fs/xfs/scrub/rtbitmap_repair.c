// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/xfile.h"
#include "scrub/rtbitmap.h"

/* Set up to repair the realtime bitmap file metadata. */
int
xrep_setup_rtbitmap(
	struct xfs_scrub	*sc,
	struct xchk_rtbitmap	*rtb)
{
	struct xfs_mount	*mp = sc->mp;
	unsigned long long	blocks = 0;

	/*
	 * Reserve enough blocks to write out a completely new bmbt for a
	 * maximally fragmented bitmap file.  We do not hold the rtbitmap
	 * ILOCK yet, so this is entirely speculative.
	 */
	blocks = xfs_bmbt_calc_size(mp, mp->m_sb.sb_rbmblocks);
	if (blocks > UINT_MAX)
		return -EOPNOTSUPP;

	rtb->resblks += blocks;
	return 0;
}

/*
 * Make sure that the given range of the data fork of the realtime file is
 * mapped to written blocks.  The caller must ensure that the inode is joined
 * to the transaction.
 */
STATIC int
xrep_rtbitmap_data_mappings(
	struct xfs_scrub	*sc,
	xfs_filblks_t		len)
{
	struct xfs_bmbt_irec	map;
	xfs_fileoff_t		off = 0;
	int			error;

	ASSERT(sc->ip != NULL);

	while (off < len) {
		int		nmaps = 1;

		/*
		 * If we have a real extent mapping this block then we're
		 * in ok shape.
		 */
		error = xfs_bmapi_read(sc->ip, off, len - off, &map, &nmaps,
				XFS_DATA_FORK);
		if (error)
			return error;
		if (nmaps == 0) {
			ASSERT(nmaps != 0);
			return -EFSCORRUPTED;
		}

		/*
		 * Written extents are ok.  Holes are not filled because we
		 * do not know the freespace information.
		 */
		if (xfs_bmap_is_written_extent(&map) ||
		    map.br_startblock == HOLESTARTBLOCK) {
			off = map.br_startoff + map.br_blockcount;
			continue;
		}

		/*
		 * If we find a delalloc reservation then something is very
		 * very wrong.  Bail out.
		 */
		if (map.br_startblock == DELAYSTARTBLOCK)
			return -EFSCORRUPTED;

		/* Make sure we're really converting an unwritten extent. */
		if (map.br_state != XFS_EXT_UNWRITTEN) {
			ASSERT(map.br_state == XFS_EXT_UNWRITTEN);
			return -EFSCORRUPTED;
		}

		/* Make sure this block has a real zeroed extent mapped. */
		nmaps = 1;
		error = xfs_bmapi_write(sc->tp, sc->ip, map.br_startoff,
				map.br_blockcount,
				XFS_BMAPI_CONVERT | XFS_BMAPI_ZERO,
				0, &map, &nmaps);
		if (error)
			return error;

		/* Commit new extent and all deferred work. */
		error = xrep_defer_finish(sc);
		if (error)
			return error;

		off = map.br_startoff + map.br_blockcount;
	}

	return 0;
}

/* Fix broken rt volume geometry. */
STATIC int
xrep_rtbitmap_geometry(
	struct xfs_scrub	*sc,
	struct xchk_rtbitmap	*rtb)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_trans	*tp = sc->tp;

	/* Superblock fields */
	if (mp->m_sb.sb_rextents != rtb->rextents)
		xfs_trans_mod_sb(sc->tp, XFS_TRANS_SB_REXTENTS,
				rtb->rextents - mp->m_sb.sb_rextents);

	if (mp->m_sb.sb_rbmblocks != rtb->rbmblocks)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_RBMBLOCKS,
				rtb->rbmblocks - mp->m_sb.sb_rbmblocks);

	if (mp->m_sb.sb_rextslog != rtb->rextslog)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_REXTSLOG,
				rtb->rextslog - mp->m_sb.sb_rextslog);

	/* Fix broken isize */
	sc->ip->i_disk_size = roundup_64(sc->ip->i_disk_size,
					 mp->m_sb.sb_blocksize);

	if (sc->ip->i_disk_size < XFS_FSB_TO_B(mp, rtb->rbmblocks))
		sc->ip->i_disk_size = XFS_FSB_TO_B(mp, rtb->rbmblocks);

	xfs_trans_log_inode(sc->tp, sc->ip, XFS_ILOG_CORE);
	return xrep_roll_trans(sc);
}

/* Repair the realtime bitmap file metadata. */
int
xrep_rtbitmap(
	struct xfs_scrub	*sc)
{
	struct xchk_rtbitmap	*rtb = sc->buf;
	struct xfs_mount	*mp = sc->mp;
	unsigned long long	blocks = 0;
	int			error;

	/* Impossibly large rtbitmap means we can't touch the filesystem. */
	if (rtb->rbmblocks > U32_MAX)
		return 0;

	/*
	 * If the size of the rt bitmap file is larger than what we reserved,
	 * figure out if we need to adjust the block reservation in the
	 * transaction.
	 */
	blocks = xfs_bmbt_calc_size(mp, rtb->rbmblocks);
	if (blocks > UINT_MAX)
		return -EOPNOTSUPP;
	if (blocks > rtb->resblks) {
		error = xfs_trans_reserve_more(sc->tp, blocks, 0);
		if (error)
			return error;

		rtb->resblks += blocks;
	}

	/* Fix inode core and forks. */
	error = xrep_metadata_inode_forks(sc);
	if (error)
		return error;

	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	/* Ensure no unwritten extents. */
	error = xrep_rtbitmap_data_mappings(sc, rtb->rbmblocks);
	if (error)
		return error;

	/* Fix inconsistent bitmap geometry */
	return xrep_rtbitmap_geometry(sc, rtb);
}
