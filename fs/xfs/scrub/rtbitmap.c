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
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_inode.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"

/* Set us up with the realtime metadata locked. */
int
xfs_scrub_setup_rt(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	int				error;

	error = xfs_scrub_setup_fs(sc, ip);
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
xfs_scrub_rtbitmap_rec(
	struct xfs_trans		*tp,
	struct xfs_rtalloc_rec		*rec,
	void				*priv)
{
	struct xfs_scrub_context	*sc = priv;
	xfs_rtblock_t			startblock;
	xfs_rtblock_t			blockcount;

	startblock = rec->ar_startext * tp->t_mountp->m_sb.sb_rextsize;
	blockcount = rec->ar_extcount * tp->t_mountp->m_sb.sb_rextsize;

	if (startblock + blockcount <= startblock ||
	    !xfs_verify_rtbno(sc->mp, startblock) ||
	    !xfs_verify_rtbno(sc->mp, startblock + blockcount - 1))
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
	return 0;
}

/* Scrub the realtime bitmap. */
int
xfs_scrub_rtbitmap(
	struct xfs_scrub_context	*sc)
{
	int				error;

	/* Invoke the fork scrubber. */
	error = xfs_scrub_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	error = xfs_rtalloc_query_all(sc->tp, xfs_scrub_rtbitmap_rec, sc);
	if (!xfs_scrub_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out;

out:
	return error;
}

/* Scrub the realtime summary. */
int
xfs_scrub_rtsummary(
	struct xfs_scrub_context	*sc)
{
	struct xfs_inode		*rsumip = sc->mp->m_rsumip;
	struct xfs_inode		*old_ip = sc->ip;
	uint				old_ilock_flags = sc->ilock_flags;
	int				error = 0;

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
	error = xfs_scrub_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		goto out;

	/* XXX: implement this some day */
	xfs_scrub_set_incomplete(sc);
out:
	/* Switch back to the rtbitmap inode and lock flags. */
	xfs_iunlock(sc->ip, sc->ilock_flags);
	sc->ilock_flags = old_ilock_flags;
	sc->ip = old_ip;
	return error;
}


/* xref check that the extent is not free in the rtbitmap */
void
xfs_scrub_xref_is_used_rt_space(
	struct xfs_scrub_context	*sc,
	xfs_rtblock_t			fsbno,
	xfs_extlen_t			len)
{
	xfs_rtblock_t			startext;
	xfs_rtblock_t			endext;
	xfs_rtblock_t			extcount;
	bool				is_free;
	int				error;

	if (xfs_scrub_skip_xref(sc->sm))
		return;

	startext = fsbno;
	endext = fsbno + len - 1;
	do_div(startext, sc->mp->m_sb.sb_rextsize);
	if (do_div(endext, sc->mp->m_sb.sb_rextsize))
		endext++;
	extcount = endext - startext;
	xfs_ilock(sc->mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	error = xfs_rtalloc_extent_is_free(sc->mp, sc->tp, startext, extcount,
			&is_free);
	if (!xfs_scrub_should_check_xref(sc, &error, NULL))
		goto out_unlock;
	if (is_free)
		xfs_scrub_ino_xref_set_corrupt(sc, sc->mp->m_rbmip->i_ino);
out_unlock:
	xfs_iunlock(sc->mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
}
