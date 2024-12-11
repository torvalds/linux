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
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/repair.h"
#include "scrub/rtbitmap.h"

/* Set us up with the realtime metadata locked. */
int
xchk_setup_rtbitmap(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xchk_rtbitmap	*rtb;
	int			error;

	rtb = kzalloc(sizeof(struct xchk_rtbitmap), XCHK_GFP_FLAGS);
	if (!rtb)
		return -ENOMEM;
	sc->buf = rtb;

	error = xchk_rtgroup_init(sc, sc->sm->sm_agno, &sc->sr);
	if (error)
		return error;

	if (xchk_could_repair(sc)) {
		error = xrep_setup_rtbitmap(sc, rtb);
		if (error)
			return error;
	}

	error = xchk_trans_alloc(sc, rtb->resblks);
	if (error)
		return error;

	error = xchk_install_live_inode(sc,
			sc->sr.rtg->rtg_inodes[XFS_RTGI_BITMAP]);
	if (error)
		return error;

	error = xchk_ino_dqattach(sc);
	if (error)
		return error;

	/*
	 * Now that we've locked the rtbitmap, we can't race with growfsrt
	 * trying to expand the bitmap or change the size of the rt volume.
	 * Hence it is safe to compute and check the geometry values.
	 */
	xchk_rtgroup_lock(&sc->sr, XFS_RTGLOCK_BITMAP);
	if (mp->m_sb.sb_rblocks) {
		rtb->rextents = xfs_blen_to_rtbxlen(mp, mp->m_sb.sb_rblocks);
		rtb->rextslog = xfs_compute_rextslog(rtb->rextents);
		rtb->rbmblocks = xfs_rtbitmap_blockcount(mp);
	}

	return 0;
}

/* Realtime bitmap. */

/* Scrub a free extent record from the realtime bitmap. */
STATIC int
xchk_rtbitmap_rec(
	struct xfs_rtgroup	*rtg,
	struct xfs_trans	*tp,
	const struct xfs_rtalloc_rec *rec,
	void			*priv)
{
	struct xfs_scrub	*sc = priv;
	xfs_rtblock_t		startblock;
	xfs_filblks_t		blockcount;

	startblock = xfs_rtx_to_rtb(rtg, rec->ar_startext);
	blockcount = xfs_rtxlen_to_extlen(rtg_mount(rtg), rec->ar_extcount);

	if (!xfs_verify_rtbext(rtg_mount(rtg), startblock, blockcount))
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
	return 0;
}

/* Make sure the entire rtbitmap file is mapped with written extents. */
STATIC int
xchk_rtbitmap_check_extents(
	struct xfs_scrub	*sc)
{
	struct xfs_bmbt_irec	map;
	struct xfs_iext_cursor	icur;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*ip = sc->ip;
	xfs_fileoff_t		off = 0;
	xfs_fileoff_t		endoff;
	int			error = 0;

	/* Mappings may not cross or lie beyond EOF. */
	endoff = XFS_B_TO_FSB(mp, ip->i_disk_size);
	if (xfs_iext_lookup_extent(ip, &ip->i_df, endoff, &icur, &map)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, endoff);
		return 0;
	}

	while (off < endoff) {
		int		nmap = 1;

		if (xchk_should_terminate(sc, &error) ||
		    (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
			break;

		/* Make sure we have a written extent. */
		error = xfs_bmapi_read(ip, off, endoff - off, &map, &nmap,
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
	struct xfs_mount	*mp = sc->mp;
	struct xfs_rtgroup	*rtg = sc->sr.rtg;
	struct xfs_inode	*rbmip = rtg->rtg_inodes[XFS_RTGI_BITMAP];
	struct xchk_rtbitmap	*rtb = sc->buf;
	int			error;

	/* Is sb_rextents correct? */
	if (mp->m_sb.sb_rextents != rtb->rextents) {
		xchk_ino_set_corrupt(sc, rbmip->i_ino);
		return 0;
	}

	/* Is sb_rextslog correct? */
	if (mp->m_sb.sb_rextslog != rtb->rextslog) {
		xchk_ino_set_corrupt(sc, rbmip->i_ino);
		return 0;
	}

	/*
	 * Is sb_rbmblocks large enough to handle the current rt volume?  In no
	 * case can we exceed 4bn bitmap blocks since the super field is a u32.
	 */
	if (rtb->rbmblocks > U32_MAX) {
		xchk_ino_set_corrupt(sc, rbmip->i_ino);
		return 0;
	}
	if (mp->m_sb.sb_rbmblocks != rtb->rbmblocks) {
		xchk_ino_set_corrupt(sc, rbmip->i_ino);
		return 0;
	}

	/* The bitmap file length must be aligned to an fsblock. */
	if (rbmip->i_disk_size & mp->m_blockmask) {
		xchk_ino_set_corrupt(sc, rbmip->i_ino);
		return 0;
	}

	/*
	 * Is the bitmap file itself large enough to handle the rt volume?
	 * growfsrt expands the bitmap file before updating sb_rextents, so the
	 * file can be larger than sb_rbmblocks.
	 */
	if (rbmip->i_disk_size < XFS_FSB_TO_B(mp, rtb->rbmblocks)) {
		xchk_ino_set_corrupt(sc, rbmip->i_ino);
		return 0;
	}

	/* Invoke the fork scrubber. */
	error = xchk_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	error = xchk_rtbitmap_check_extents(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		return error;

	error = xfs_rtalloc_query_all(rtg, sc->tp, xchk_rtbitmap_rec, sc);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		return error;

	return 0;
}

/* xref check that the extent is not free in the rtbitmap */
void
xchk_xref_is_used_rt_space(
	struct xfs_scrub	*sc,
	xfs_rtblock_t		rtbno,
	xfs_extlen_t		len)
{
	struct xfs_rtgroup	*rtg = sc->sr.rtg;
	struct xfs_inode	*rbmip = rtg->rtg_inodes[XFS_RTGI_BITMAP];
	xfs_rtxnum_t		startext;
	xfs_rtxnum_t		endext;
	bool			is_free;
	int			error;

	if (xchk_skip_xref(sc->sm))
		return;

	startext = xfs_rtb_to_rtx(sc->mp, rtbno);
	endext = xfs_rtb_to_rtx(sc->mp, rtbno + len - 1);
	error = xfs_rtalloc_extent_is_free(rtg, sc->tp, startext,
			endext - startext + 1, &is_free);
	if (!xchk_should_check_xref(sc, &error, NULL))
		return;
	if (is_free)
		xchk_ino_xref_set_corrupt(sc, rbmip->i_ino);
}
