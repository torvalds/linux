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
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_rtalloc.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"

/* Set us up with the realtime metadata locked. */
int
xchk_setup_rt(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	int			error;

	error = xchk_setup_fs(sc, ip);
	if (error)
		return error;

	sc->ilock_flags = XFS_ILOCK_EXCL | XFS_ILOCK_RTBITMAP;
	sc->ip = sc->mp->m_rbmip;
	xfs_ilock(sc->ip, sc->ilock_flags);

	return 0;
}

/* Realtime bitmap. */

/* Scrub a free extent record from the realtime bitmap. */
STATIC int
xchk_rtbitmap_rec(
	struct xfs_trans	*tp,
	struct xfs_rtalloc_rec	*rec,
	void			*priv)
{
	struct xfs_scrub	*sc = priv;
	xfs_rtblock_t		startblock;
	xfs_rtblock_t		blockcount;

	startblock = rec->ar_startext * tp->t_mountp->m_sb.sb_rextsize;
	blockcount = rec->ar_extcount * tp->t_mountp->m_sb.sb_rextsize;

	if (!xfs_verify_rtext(sc->mp, startblock, blockcount))
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
	if (sc->mp->m_rbmip->i_d.di_size !=
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

	error = xfs_rtalloc_query_all(sc->tp, xchk_rtbitmap_rec, sc);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out;

out:
	return error;
}

/* Scrub the realtime summary. */
int
xchk_rtsummary(
	struct xfs_scrub	*sc)
{
	struct xfs_inode	*rsumip = sc->mp->m_rsumip;
	struct xfs_inode	*old_ip = sc->ip;
	uint			old_ilock_flags = sc->ilock_flags;
	int			error = 0;

	/*
	 * We ILOCK'd the rt bitmap ip in the setup routine, now lock the
	 * rt summary ip in compliance with the rt inode locking rules.
	 *
	 * Since we switch sc->ip to rsumip we have to save the old ilock
	 * flags so that we don't mix up the inode state that @sc tracks.
	 */
	sc->ip = rsumip;
	sc->ilock_flags = XFS_ILOCK_EXCL | XFS_ILOCK_RTSUM;
	xfs_ilock(sc->ip, sc->ilock_flags);

	/* Invoke the fork scrubber. */
	error = xchk_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		goto out;

	/* XXX: implement this some day */
	xchk_set_incomplete(sc);
out:
	/* Switch back to the rtbitmap inode and lock flags. */
	xfs_iunlock(sc->ip, sc->ilock_flags);
	sc->ilock_flags = old_ilock_flags;
	sc->ip = old_ip;
	return error;
}


/* xref check that the extent is not free in the rtbitmap */
void
xchk_xref_is_used_rt_space(
	struct xfs_scrub	*sc,
	xfs_rtblock_t		fsbno,
	xfs_extlen_t		len)
{
	xfs_rtblock_t		startext;
	xfs_rtblock_t		endext;
	xfs_rtblock_t		extcount;
	bool			is_free;
	int			error;

	if (xchk_skip_xref(sc->sm))
		return;

	startext = fsbno;
	endext = fsbno + len - 1;
	do_div(startext, sc->mp->m_sb.sb_rextsize);
	do_div(endext, sc->mp->m_sb.sb_rextsize);
	extcount = endext - startext + 1;
	xfs_ilock(sc->mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	error = xfs_rtalloc_extent_is_free(sc->mp, sc->tp, startext, extcount,
			&is_free);
	if (!xchk_should_check_xref(sc, &error, NULL))
		goto out_unlock;
	if (is_free)
		xchk_ino_xref_set_corrupt(sc, sc->mp->m_rbmip->i_ino);
out_unlock:
	xfs_iunlock(sc->mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
}
