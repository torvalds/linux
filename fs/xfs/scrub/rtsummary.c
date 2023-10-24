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
#include "xfs_btree.h"
#include "xfs_inode.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_rtalloc.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/xfile.h"

/*
 * Realtime Summary
 * ================
 *
 * We check the realtime summary by scanning the realtime bitmap file to create
 * a new summary file incore, and then we compare the computed version against
 * the ondisk version.  We use the 'xfile' functionality to store this
 * (potentially large) amount of data in pageable memory.
 */

/* Set us up to check the rtsummary file. */
int
xchk_setup_rtsummary(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	char			*descr;
	int			error;

	/*
	 * Create an xfile to construct a new rtsummary file.  The xfile allows
	 * us to avoid pinning kernel memory for this purpose.
	 */
	descr = xchk_xfile_descr(sc, "realtime summary file");
	error = xfile_create(descr, mp->m_rsumsize, &sc->xfile);
	kfree(descr);
	if (error)
		return error;

	error = xchk_trans_alloc(sc, 0);
	if (error)
		return error;

	/* Allocate a memory buffer for the summary comparison. */
	sc->buf = kvmalloc(mp->m_sb.sb_blocksize, XCHK_GFP_FLAGS);
	if (!sc->buf)
		return -ENOMEM;

	error = xchk_install_live_inode(sc, mp->m_rsumip);
	if (error)
		return error;

	/*
	 * Locking order requires us to take the rtbitmap first.  We must be
	 * careful to unlock it ourselves when we are done with the rtbitmap
	 * file since the scrub infrastructure won't do that for us.  Only
	 * then we can lock the rtsummary inode.
	 */
	xfs_ilock(mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	xchk_ilock(sc, XFS_ILOCK_EXCL | XFS_ILOCK_RTSUM);
	return 0;
}

/* Helper functions to record suminfo words in an xfile. */

typedef unsigned int xchk_rtsumoff_t;

static inline int
xfsum_load(
	struct xfs_scrub	*sc,
	xchk_rtsumoff_t		sumoff,
	xfs_suminfo_t		*info)
{
	return xfile_obj_load(sc->xfile, info, sizeof(xfs_suminfo_t),
			sumoff << XFS_WORDLOG);
}

static inline int
xfsum_store(
	struct xfs_scrub	*sc,
	xchk_rtsumoff_t		sumoff,
	const xfs_suminfo_t	info)
{
	return xfile_obj_store(sc->xfile, &info, sizeof(xfs_suminfo_t),
			sumoff << XFS_WORDLOG);
}

static inline int
xfsum_copyout(
	struct xfs_scrub	*sc,
	xchk_rtsumoff_t		sumoff,
	xfs_suminfo_t		*info,
	unsigned int		nr_words)
{
	return xfile_obj_load(sc->xfile, info, nr_words << XFS_WORDLOG,
			sumoff << XFS_WORDLOG);
}

/* Update the summary file to reflect the free extent that we've accumulated. */
STATIC int
xchk_rtsum_record_free(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	const struct xfs_rtalloc_rec	*rec,
	void				*priv)
{
	struct xfs_scrub		*sc = priv;
	xfs_fileoff_t			rbmoff;
	xfs_rtblock_t			rtbno;
	xfs_filblks_t			rtlen;
	xchk_rtsumoff_t			offs;
	unsigned int			lenlog;
	xfs_suminfo_t			v = 0;
	int				error = 0;

	if (xchk_should_terminate(sc, &error))
		return error;

	/* Compute the relevant location in the rtsum file. */
	rbmoff = XFS_BITTOBLOCK(mp, rec->ar_startext);
	lenlog = XFS_RTBLOCKLOG(rec->ar_extcount);
	offs = XFS_SUMOFFS(mp, lenlog, rbmoff);

	rtbno = rec->ar_startext * mp->m_sb.sb_rextsize;
	rtlen = rec->ar_extcount * mp->m_sb.sb_rextsize;

	if (!xfs_verify_rtext(mp, rtbno, rtlen)) {
		xchk_ino_xref_set_corrupt(sc, mp->m_rbmip->i_ino);
		return -EFSCORRUPTED;
	}

	/* Bump the summary count. */
	error = xfsum_load(sc, offs, &v);
	if (error)
		return error;

	v++;
	trace_xchk_rtsum_record_free(mp, rec->ar_startext, rec->ar_extcount,
			lenlog, offs, v);

	return xfsum_store(sc, offs, v);
}

/* Compute the realtime summary from the realtime bitmap. */
STATIC int
xchk_rtsum_compute(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	unsigned long long	rtbmp_bytes;

	/* If the bitmap size doesn't match the computed size, bail. */
	rtbmp_bytes = howmany_64(mp->m_sb.sb_rextents, NBBY);
	if (roundup_64(rtbmp_bytes, mp->m_sb.sb_blocksize) !=
			mp->m_rbmip->i_disk_size)
		return -EFSCORRUPTED;

	return xfs_rtalloc_query_all(sc->mp, sc->tp, xchk_rtsum_record_free,
			sc);
}

/* Compare the rtsummary file against the one we computed. */
STATIC int
xchk_rtsum_compare(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_buf		*bp;
	struct xfs_bmbt_irec	map;
	xfs_fileoff_t		off;
	xchk_rtsumoff_t		sumoff = 0;
	int			nmap;

	for (off = 0; off < XFS_B_TO_FSB(mp, mp->m_rsumsize); off++) {
		int		error = 0;

		if (xchk_should_terminate(sc, &error))
			return error;
		if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			return 0;

		/* Make sure we have a written extent. */
		nmap = 1;
		error = xfs_bmapi_read(mp->m_rsumip, off, 1, &map, &nmap,
				XFS_DATA_FORK);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, off, &error))
			return error;

		if (nmap != 1 || !xfs_bmap_is_written_extent(&map)) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, off);
			return 0;
		}

		/* Read a block's worth of ondisk rtsummary file. */
		error = xfs_rtbuf_get(mp, sc->tp, off, 1, &bp);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, off, &error))
			return error;

		/* Read a block's worth of computed rtsummary file. */
		error = xfsum_copyout(sc, sumoff, sc->buf, mp->m_blockwsize);
		if (error) {
			xfs_trans_brelse(sc->tp, bp);
			return error;
		}

		if (memcmp(bp->b_addr, sc->buf,
					mp->m_blockwsize << XFS_WORDLOG) != 0)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, off);

		xfs_trans_brelse(sc->tp, bp);
		sumoff += mp->m_blockwsize;
	}

	return 0;
}

/* Scrub the realtime summary. */
int
xchk_rtsummary(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	int			error = 0;

	/* Invoke the fork scrubber. */
	error = xchk_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		goto out_rbm;

	/* Construct the new summary file from the rtbitmap. */
	error = xchk_rtsum_compute(sc);
	if (error == -EFSCORRUPTED) {
		/*
		 * EFSCORRUPTED means the rtbitmap is corrupt, which is an xref
		 * error since we're checking the summary file.
		 */
		xchk_ino_xref_set_corrupt(sc, mp->m_rbmip->i_ino);
		error = 0;
		goto out_rbm;
	}
	if (error)
		goto out_rbm;

	/* Does the computed summary file match the actual rtsummary file? */
	error = xchk_rtsum_compare(sc);

out_rbm:
	/* Unlock the rtbitmap since we're done with it. */
	xfs_iunlock(mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	return error;
}
