// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_rtbitmap.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"

/* Set us up with the realtime metadata locked. */
int
xchk_setup_rtbitmap(
	struct xfs_scrub	*sc)
{
	int			error;

	error = xchk_trans_alloc(sc, 0);
	if (error)
		return error;

	error = xchk_install_live_inode(sc, sc->mp->m_rbmip);
	if (error)
		return error;

	xchk_ilock(sc, XFS_ILOCK_EXCL | XFS_ILOCK_RTBITMAP);
	return 0;
}

/* Realtime bitmap. */

/* Scrub a free extent record from the realtime bitmap. */
STATIC int
xchk_rtbitmap_rec(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	const struct xfs_rtalloc_rec *rec,
	void			*priv)
{
	struct xfs_scrub	*sc = priv;
	xfs_rtblock_t		startblock;
	xfs_filblks_t		blockcount;

	startblock = xfs_rtx_to_rtb(mp, rec->ar_startext);
	blockcount = xfs_rtx_to_rtb(mp, rec->ar_extcount);

	if (!xfs_verify_rtbext(mp, startblock, blockcount))
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
	return 0;
}

/* Make sure the entire rtbitmap file is mapped with written extents. */
STATIC int
xchk_rtbitmap_check_extents(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_bmbt_irec	map;
	xfs_rtblock_t		off;
	int			nmap;
	int			error = 0;

	for (off = 0; off < mp->m_sb.sb_rbmblocks;) {
		if (xchk_should_terminate(sc, &error) ||
		    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
			break;

		/* Make sure we have a written extent. */
		nmap = 1;
		error = xfs_bmapi_read(mp->m_rbmip, off,
				mp->m_sb.sb_rbmblocks - off, &map, &nmap,
				XFS_DATA_FORK);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, off, &error))
			break;

		if (nmap != 1 || !xfs_bmap_is_written_extent(&map)) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, off);
			break;
		}

		off += map.br_blockcount;
	}

	return error;
}

/* Scrub the realtime bitmap. */
int
xchk_rtbitmap(
	struct xfs_scrub	*sc)
{
	int			error;

	/* Is the size of the rtbitmap correct? */
	if (sc->mp->m_rbmip->i_disk_size !=
	    XFS_FSB_TO_B(sc->mp, sc->mp->m_sb.sb_rbmblocks)) {
		xchk_ino_set_corrupt(sc, sc->mp->m_rbmip->i_ino);
		return 0;
	}

	/* Invoke the fork scrubber. */
	error = xchk_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	error = xchk_rtbitmap_check_extents(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	error = xfs_rtalloc_query_all(sc->mp, sc->tp, xchk_rtbitmap_rec, sc);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out;

out:
	return error;
}

/* xref check that the extent is not free in the rtbitmap */
void
xchk_xref_is_used_rt_space(
	struct xfs_scrub	*sc,
	xfs_rtblock_t		rtbno,
	xfs_extlen_t		len)
{
	xfs_rtxnum_t		startext;
	xfs_rtxnum_t		endext;
	bool			is_free;
	int			error;

	if (xchk_skip_xref(sc->sm))
		return;

	startext = xfs_rtb_to_rtx(sc->mp, rtbno);
	endext = xfs_rtb_to_rtx(sc->mp, rtbno + len - 1);
	xfs_ilock(sc->mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	error = xfs_rtalloc_extent_is_free(sc->mp, sc->tp, startext,
			endext - startext + 1, &is_free);
	if (!xchk_should_check_xref(sc, &error, NULL))
		goto out_unlock;
	if (is_free)
		xchk_ino_xref_set_corrupt(sc, sc->mp->m_rbmip->i_ino);
out_unlock:
	xfs_iunlock(sc->mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
}
