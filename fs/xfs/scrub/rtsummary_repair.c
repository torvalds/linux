// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
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
#include "xfs_rtalloc.h"
#include "xfs_inode.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_exchmaps.h"
#include "xfs_rtbitmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/tempfile.h"
#include "scrub/tempexch.h"
#include "scrub/reap.h"
#include "scrub/xfile.h"
#include "scrub/rtsummary.h"

/* Set us up to repair the rtsummary file. */
int
xrep_setup_rtsummary(
	struct xfs_scrub	*sc,
	struct xchk_rtsummary	*rts)
{
	struct xfs_mount	*mp = sc->mp;
	unsigned long long	blocks;
	int			error;

	error = xrep_tempfile_create(sc, S_IFREG);
	if (error)
		return error;

	/*
	 * If we're doing a repair, we reserve enough blocks to write out a
	 * completely new summary file, plus twice as many blocks as we would
	 * need if we can only allocate one block per data fork mapping.  This
	 * should cover the preallocation of the temporary file and exchanging
	 * the extent mappings.
	 *
	 * We cannot use xfs_exchmaps_estimate because we have not yet
	 * constructed the replacement rtsummary and therefore do not know how
	 * many extents it will use.  By the time we do, we will have a dirty
	 * transaction (which we cannot drop because we cannot drop the
	 * rtsummary ILOCK) and cannot ask for more reservation.
	 */
	blocks = mp->m_rsumblocks;
	blocks += xfs_bmbt_calc_size(mp, blocks) * 2;
	if (blocks > UINT_MAX)
		return -EOPNOTSUPP;

	rts->resblks += blocks;
	return 0;
}

static int
xrep_rtsummary_prep_buf(
	struct xfs_scrub	*sc,
	struct xfs_buf		*bp,
	void			*data)
{
	struct xchk_rtsummary	*rts = data;
	struct xfs_mount	*mp = sc->mp;
	union xfs_suminfo_raw	*ondisk;
	int			error;

	rts->args.mp = mp;
	rts->args.tp = sc->tp;
	rts->args.rtg = sc->sr.rtg;
	rts->args.sumbp = bp;
	ondisk = xfs_rsumblock_infoptr(&rts->args, 0);
	rts->args.sumbp = NULL;

	error = xfsum_copyout(sc, rts->prep_wordoff, ondisk, mp->m_blockwsize);
	if (error)
		return error;

	if (xfs_has_rtgroups(sc->mp)) {
		struct xfs_rtbuf_blkinfo	*hdr = bp->b_addr;

		hdr->rt_magic = cpu_to_be32(XFS_RTSUMMARY_MAGIC);
		hdr->rt_owner = cpu_to_be64(sc->ip->i_ino);
		hdr->rt_blkno = cpu_to_be64(xfs_buf_daddr(bp));
		hdr->rt_lsn = 0;
		uuid_copy(&hdr->rt_uuid, &sc->mp->m_sb.sb_meta_uuid);
		bp->b_ops = &xfs_rtsummary_buf_ops;
	} else {
		bp->b_ops = &xfs_rtbuf_ops;
	}

	rts->prep_wordoff += mp->m_blockwsize;
	xfs_trans_buf_set_type(sc->tp, bp, XFS_BLFT_RTSUMMARY_BUF);
	return 0;
}

/* Repair the realtime summary. */
int
xrep_rtsummary(
	struct xfs_scrub	*sc)
{
	struct xchk_rtsummary	*rts = sc->buf;
	struct xfs_mount	*mp = sc->mp;
	int			error;

	/* We require the rmapbt to rebuild anything. */
	if (!xfs_has_rmapbt(mp))
		return -EOPNOTSUPP;
	/* We require atomic file exchange range to rebuild anything. */
	if (!xfs_has_exchange_range(mp))
		return -EOPNOTSUPP;

	/* Walk away if we disagree on the size of the rt bitmap. */
	if (rts->rbmblocks != mp->m_sb.sb_rbmblocks)
		return 0;

	/* Make sure any problems with the fork are fixed. */
	error = xrep_metadata_inode_forks(sc);
	if (error)
		return error;

	/*
	 * Try to take ILOCK_EXCL of the temporary file.  We had better be the
	 * only ones holding onto this inode, but we can't block while holding
	 * the rtsummary file's ILOCK_EXCL.
	 */
	while (!xrep_tempfile_ilock_nowait(sc)) {
		if (xchk_should_terminate(sc, &error))
			return error;
		delay(1);
	}

	/* Make sure we have space allocated for the entire summary file. */
	xfs_trans_ijoin(sc->tp, sc->ip, 0);
	xfs_trans_ijoin(sc->tp, sc->tempip, 0);
	error = xrep_tempfile_prealloc(sc, 0, rts->rsumblocks);
	if (error)
		return error;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		return error;

	/* Copy the rtsummary file that we generated. */
	error = xrep_tempfile_copyin(sc, 0, rts->rsumblocks,
			xrep_rtsummary_prep_buf, rts);
	if (error)
		return error;
	error = xrep_tempfile_set_isize(sc, XFS_FSB_TO_B(mp, rts->rsumblocks));
	if (error)
		return error;

	/*
	 * Now exchange the contents.  Nothing in repair uses the temporary
	 * buffer, so we can reuse it for the tempfile exchrange information.
	 */
	error = xrep_tempexch_trans_reserve(sc, XFS_DATA_FORK, 0,
			rts->rsumblocks, &rts->tempexch);
	if (error)
		return error;

	error = xrep_tempexch_contents(sc, &rts->tempexch);
	if (error)
		return error;

	/* Reset incore state and blow out the summary cache. */
	if (sc->sr.rtg->rtg_rsum_cache)
		memset(sc->sr.rtg->rtg_rsum_cache, 0xFF, mp->m_sb.sb_rbmblocks);

	mp->m_rsumlevels = rts->rsumlevels;
	mp->m_rsumblocks = rts->rsumblocks;

	/* Free the old rtsummary blocks if they're not in use. */
	return xrep_reap_ifork(sc, sc->tempip, XFS_DATA_FORK);
}
