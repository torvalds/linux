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
#include "xfs_rtbitmap.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "xfs_sb.h"
#include "xfs_exchmaps.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/xfile.h"
#include "scrub/repair.h"
#include "scrub/tempexch.h"
#include "scrub/rtsummary.h"

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
	struct xchk_rtsummary	*rts;
	int			error;

	rts = kvzalloc(struct_size(rts, words, mp->m_blockwsize),
			XCHK_GFP_FLAGS);
	if (!rts)
		return -ENOMEM;
	sc->buf = rts;

	if (xchk_could_repair(sc)) {
		error = xrep_setup_rtsummary(sc, rts);
		if (error)
			return error;
	}

	/*
	 * Create an xfile to construct a new rtsummary file.  The xfile allows
	 * us to avoid pinning kernel memory for this purpose.
	 */
	descr = xchk_xfile_descr(sc, "realtime summary file");
	error = xfile_create(descr, mp->m_rsumsize, &sc->xfile);
	kfree(descr);
	if (error)
		return error;

	error = xchk_trans_alloc(sc, rts->resblks);
	if (error)
		return error;

	error = xchk_install_live_inode(sc, mp->m_rsumip);
	if (error)
		return error;

	error = xchk_ino_dqattach(sc);
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

	/*
	 * Now that we've locked the rtbitmap and rtsummary, we can't race with
	 * growfsrt trying to expand the summary or change the size of the rt
	 * volume.  Hence it is safe to compute and check the geometry values.
	 */
	if (mp->m_sb.sb_rblocks) {
		xfs_filblks_t	rsumblocks;
		int		rextslog;

		rts->rextents = xfs_rtb_to_rtx(mp, mp->m_sb.sb_rblocks);
		rextslog = xfs_compute_rextslog(rts->rextents);
		rts->rsumlevels = rextslog + 1;
		rts->rbmblocks = xfs_rtbitmap_blockcount(mp, rts->rextents);
		rsumblocks = xfs_rtsummary_blockcount(mp, rts->rsumlevels,
				rts->rbmblocks);
		rts->rsumsize = XFS_FSB_TO_B(mp, rsumblocks);
	}
	return 0;
}

/* Helper functions to record suminfo words in an xfile. */

static inline int
xfsum_load(
	struct xfs_scrub	*sc,
	xfs_rtsumoff_t		sumoff,
	union xfs_suminfo_raw	*rawinfo)
{
	return xfile_load(sc->xfile, rawinfo,
			sizeof(union xfs_suminfo_raw),
			sumoff << XFS_WORDLOG);
}

static inline int
xfsum_store(
	struct xfs_scrub	*sc,
	xfs_rtsumoff_t		sumoff,
	const union xfs_suminfo_raw rawinfo)
{
	return xfile_store(sc->xfile, &rawinfo,
			sizeof(union xfs_suminfo_raw),
			sumoff << XFS_WORDLOG);
}

inline int
xfsum_copyout(
	struct xfs_scrub	*sc,
	xfs_rtsumoff_t		sumoff,
	union xfs_suminfo_raw	*rawinfo,
	unsigned int		nr_words)
{
	return xfile_load(sc->xfile, rawinfo, nr_words << XFS_WORDLOG,
			sumoff << XFS_WORDLOG);
}

static inline xfs_suminfo_t
xchk_rtsum_inc(
	struct xfs_mount	*mp,
	union xfs_suminfo_raw	*v)
{
	v->old += 1;
	return v->old;
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
	xfs_rtsumoff_t			offs;
	unsigned int			lenlog;
	union xfs_suminfo_raw		v;
	xfs_suminfo_t			value;
	int				error = 0;

	if (xchk_should_terminate(sc, &error))
		return error;

	/* Compute the relevant location in the rtsum file. */
	rbmoff = xfs_rtx_to_rbmblock(mp, rec->ar_startext);
	lenlog = xfs_highbit64(rec->ar_extcount);
	offs = xfs_rtsumoffs(mp, lenlog, rbmoff);

	rtbno = xfs_rtx_to_rtb(mp, rec->ar_startext);
	rtlen = xfs_rtx_to_rtb(mp, rec->ar_extcount);

	if (!xfs_verify_rtbext(mp, rtbno, rtlen)) {
		xchk_ino_xref_set_corrupt(sc, mp->m_rbmip->i_ino);
		return -EFSCORRUPTED;
	}

	/* Bump the summary count. */
	error = xfsum_load(sc, offs, &v);
	if (error)
		return error;

	value = xchk_rtsum_inc(sc->mp, &v);
	trace_xchk_rtsum_record_free(mp, rec->ar_startext, rec->ar_extcount,
			lenlog, offs, value);

	return xfsum_store(sc, offs, v);
}

/* Compute the realtime summary from the realtime bitmap. */
STATIC int
xchk_rtsum_compute(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	unsigned long long	rtbmp_blocks;

	/* If the bitmap size doesn't match the computed size, bail. */
	rtbmp_blocks = xfs_rtbitmap_blockcount(mp, mp->m_sb.sb_rextents);
	if (XFS_FSB_TO_B(mp, rtbmp_blocks) != mp->m_rbmip->i_disk_size)
		return -EFSCORRUPTED;

	return xfs_rtalloc_query_all(sc->mp, sc->tp, xchk_rtsum_record_free,
			sc);
}

/* Compare the rtsummary file against the one we computed. */
STATIC int
xchk_rtsum_compare(
	struct xfs_scrub	*sc)
{
	struct xfs_bmbt_irec	map;
	struct xfs_iext_cursor	icur;

	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*ip = sc->ip;
	struct xchk_rtsummary	*rts = sc->buf;
	xfs_fileoff_t		off = 0;
	xfs_fileoff_t		endoff;
	xfs_rtsumoff_t		sumoff = 0;
	int			error = 0;

	rts->args.mp = sc->mp;
	rts->args.tp = sc->tp;

	/* Mappings may not cross or lie beyond EOF. */
	endoff = XFS_B_TO_FSB(mp, ip->i_disk_size);
	if (xfs_iext_lookup_extent(ip, &ip->i_df, endoff, &icur, &map)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, endoff);
		return 0;
	}

	while (off < endoff) {
		int		nmap = 1;

		if (xchk_should_terminate(sc, &error))
			return error;
		if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			return 0;

		/* Make sure we have a written extent. */
		error = xfs_bmapi_read(ip, off, endoff - off, &map, &nmap,
				XFS_DATA_FORK);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, off, &error))
			return error;

		if (nmap != 1 || !xfs_bmap_is_written_extent(&map)) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, off);
			return 0;
		}

		off += map.br_blockcount;
	}

	for (off = 0; off < endoff; off++) {
		union xfs_suminfo_raw	*ondisk_info;

		/* Read a block's worth of ondisk rtsummary file. */
		error = xfs_rtsummary_read_buf(&rts->args, off);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, off, &error))
			return error;

		/* Read a block's worth of computed rtsummary file. */
		error = xfsum_copyout(sc, sumoff, rts->words, mp->m_blockwsize);
		if (error) {
			xfs_rtbuf_cache_relse(&rts->args);
			return error;
		}

		ondisk_info = xfs_rsumblock_infoptr(&rts->args, 0);
		if (memcmp(ondisk_info, rts->words,
					mp->m_blockwsize << XFS_WORDLOG) != 0) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, off);
			xfs_rtbuf_cache_relse(&rts->args);
			return error;
		}

		xfs_rtbuf_cache_relse(&rts->args);
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
	struct xchk_rtsummary	*rts = sc->buf;
	int			error = 0;

	/* Is sb_rextents correct? */
	if (mp->m_sb.sb_rextents != rts->rextents) {
		xchk_ino_set_corrupt(sc, mp->m_rbmip->i_ino);
		goto out_rbm;
	}

	/* Is m_rsumlevels correct? */
	if (mp->m_rsumlevels != rts->rsumlevels) {
		xchk_ino_set_corrupt(sc, mp->m_rsumip->i_ino);
		goto out_rbm;
	}

	/* Is m_rsumsize correct? */
	if (mp->m_rsumsize != rts->rsumsize) {
		xchk_ino_set_corrupt(sc, mp->m_rsumip->i_ino);
		goto out_rbm;
	}

	/* The summary file length must be aligned to an fsblock. */
	if (mp->m_rsumip->i_disk_size & mp->m_blockmask) {
		xchk_ino_set_corrupt(sc, mp->m_rsumip->i_ino);
		goto out_rbm;
	}

	/*
	 * Is the summary file itself large enough to handle the rt volume?
	 * growfsrt expands the summary file before updating sb_rextents, so
	 * the file can be larger than rsumsize.
	 */
	if (mp->m_rsumip->i_disk_size < rts->rsumsize) {
		xchk_ino_set_corrupt(sc, mp->m_rsumip->i_ino);
		goto out_rbm;
	}

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
	/*
	 * Unlock the rtbitmap since we're done with it.  All other writers of
	 * the rt free space metadata grab the bitmap and summary ILOCKs in
	 * that order, so we're still protected against allocation activities
	 * even if we continue on to the repair function.
	 */
	xfs_iunlock(mp->m_rbmip, XFS_ILOCK_SHARED | XFS_ILOCK_RTBITMAP);
	return error;
}
