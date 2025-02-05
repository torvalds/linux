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
#include "xfs_rtalloc.h"
#include "xfs_inode.h"
#include "xfs_bit.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_rmap.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_exchmaps.h"
#include "xfs_rtbitmap.h"
#include "xfs_rtgroup.h"
#include "xfs_extent_busy.h"
#include "xfs_refcount.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/xfile.h"
#include "scrub/tempfile.h"
#include "scrub/tempexch.h"
#include "scrub/reap.h"
#include "scrub/rtbitmap.h"

/* rt bitmap content repairs */

/* Set up to repair the realtime bitmap for this group. */
int
xrep_setup_rtbitmap(
	struct xfs_scrub	*sc,
	struct xchk_rtbitmap	*rtb)
{
	struct xfs_mount	*mp = sc->mp;
	char			*descr;
	unsigned long long	blocks = mp->m_sb.sb_rbmblocks;
	int			error;

	error = xrep_tempfile_create(sc, S_IFREG);
	if (error)
		return error;

	/* Create an xfile to hold our reconstructed bitmap. */
	descr = xchk_xfile_rtgroup_descr(sc, "bitmap file");
	error = xfile_create(descr, blocks * mp->m_sb.sb_blocksize, &sc->xfile);
	kfree(descr);
	if (error)
		return error;

	/*
	 * Reserve enough blocks to write out a completely new bitmap file,
	 * plus twice as many blocks as we would need if we can only allocate
	 * one block per data fork mapping.  This should cover the
	 * preallocation of the temporary file and exchanging the extent
	 * mappings.
	 *
	 * We cannot use xfs_exchmaps_estimate because we have not yet
	 * constructed the replacement bitmap and therefore do not know how
	 * many extents it will use.  By the time we do, we will have a dirty
	 * transaction (which we cannot drop because we cannot drop the
	 * rtbitmap ILOCK) and cannot ask for more reservation.
	 */
	blocks += xfs_bmbt_calc_size(mp, blocks) * 2;
	if (blocks > UINT_MAX)
		return -EOPNOTSUPP;

	rtb->resblks += blocks;
	return 0;
}

static inline xrep_wordoff_t
rtx_to_wordoff(
	struct xfs_mount	*mp,
	xfs_rtxnum_t		rtx)
{
	return rtx >> XFS_NBWORDLOG;
}

static inline xrep_wordcnt_t
rtxlen_to_wordcnt(
	xfs_rtxlen_t	rtxlen)
{
	return rtxlen >> XFS_NBWORDLOG;
}

/* Helper functions to record rtwords in an xfile. */

static inline int
xfbmp_load(
	struct xchk_rtbitmap	*rtb,
	xrep_wordoff_t		wordoff,
	xfs_rtword_t		*word)
{
	union xfs_rtword_raw	urk;
	int			error;

	ASSERT(xfs_has_rtgroups(rtb->sc->mp));

	error = xfile_load(rtb->sc->xfile, &urk,
			sizeof(union xfs_rtword_raw),
			wordoff << XFS_WORDLOG);
	if (error)
		return error;

	*word = be32_to_cpu(urk.rtg);
	return 0;
}

static inline int
xfbmp_store(
	struct xchk_rtbitmap	*rtb,
	xrep_wordoff_t		wordoff,
	const xfs_rtword_t	word)
{
	union xfs_rtword_raw	urk;

	ASSERT(xfs_has_rtgroups(rtb->sc->mp));

	urk.rtg = cpu_to_be32(word);
	return xfile_store(rtb->sc->xfile, &urk,
			sizeof(union xfs_rtword_raw),
			wordoff << XFS_WORDLOG);
}

static inline int
xfbmp_copyin(
	struct xchk_rtbitmap	*rtb,
	xrep_wordoff_t		wordoff,
	const union xfs_rtword_raw	*word,
	xrep_wordcnt_t		nr_words)
{
	return xfile_store(rtb->sc->xfile, word, nr_words << XFS_WORDLOG,
			wordoff << XFS_WORDLOG);
}

static inline int
xfbmp_copyout(
	struct xchk_rtbitmap	*rtb,
	xrep_wordoff_t		wordoff,
	union xfs_rtword_raw	*word,
	xrep_wordcnt_t		nr_words)
{
	return xfile_load(rtb->sc->xfile, word, nr_words << XFS_WORDLOG,
			wordoff << XFS_WORDLOG);
}

/* Perform a logical OR operation on an rtword in the incore bitmap. */
static int
xrep_rtbitmap_or(
	struct xchk_rtbitmap	*rtb,
	xrep_wordoff_t		wordoff,
	xfs_rtword_t		mask)
{
	xfs_rtword_t		word;
	int			error;

	error = xfbmp_load(rtb, wordoff, &word);
	if (error)
		return error;

	trace_xrep_rtbitmap_or(rtb->sc->mp, wordoff, mask, word);

	return xfbmp_store(rtb, wordoff, word | mask);
}

/*
 * Mark as free every rt extent between the next rt block we expected to see
 * in the rtrmap records and the given rt block.
 */
STATIC int
xrep_rtbitmap_mark_free(
	struct xchk_rtbitmap	*rtb,
	xfs_rgblock_t		rgbno)
{
	struct xfs_mount	*mp = rtb->sc->mp;
	struct xchk_rt		*sr = &rtb->sc->sr;
	struct xfs_rtgroup	*rtg = sr->rtg;
	xfs_rtxnum_t		startrtx;
	xfs_rtxnum_t		nextrtx;
	xrep_wordoff_t		wordoff, nextwordoff;
	unsigned int		bit;
	unsigned int		bufwsize;
	xfs_extlen_t		mod;
	xfs_rtword_t		mask;
	enum xbtree_recpacking	outcome;
	int			error;

	if (!xfs_verify_rgbext(rtg, rtb->next_rgbno, rgbno - rtb->next_rgbno))
		return -EFSCORRUPTED;

	/*
	 * Convert rt blocks to rt extents  The block range we find must be
	 * aligned to an rtextent boundary on both ends.
	 */
	startrtx = xfs_rgbno_to_rtx(mp, rtb->next_rgbno);
	mod = xfs_rgbno_to_rtxoff(mp, rtb->next_rgbno);
	if (mod)
		return -EFSCORRUPTED;

	nextrtx = xfs_rgbno_to_rtx(mp, rgbno - 1) + 1;
	mod = xfs_rgbno_to_rtxoff(mp, rgbno - 1);
	if (mod != mp->m_sb.sb_rextsize - 1)
		return -EFSCORRUPTED;

	/* Must not be shared or CoW staging. */
	if (sr->refc_cur) {
		error = xfs_refcount_has_records(sr->refc_cur,
				XFS_REFC_DOMAIN_SHARED, rtb->next_rgbno,
				rgbno - rtb->next_rgbno, &outcome);
		if (error)
			return error;
		if (outcome != XBTREE_RECPACKING_EMPTY)
			return -EFSCORRUPTED;

		error = xfs_refcount_has_records(sr->refc_cur,
				XFS_REFC_DOMAIN_COW, rtb->next_rgbno,
				rgbno - rtb->next_rgbno, &outcome);
		if (error)
			return error;
		if (outcome != XBTREE_RECPACKING_EMPTY)
			return -EFSCORRUPTED;
	}

	trace_xrep_rtbitmap_record_free(mp, startrtx, nextrtx - 1);

	/* Set bits as needed to round startrtx up to the nearest word. */
	bit = startrtx & XREP_RTBMP_WORDMASK;
	if (bit) {
		xfs_rtblock_t	len = nextrtx - startrtx;
		unsigned int	lastbit;

		lastbit = min(bit + len, XFS_NBWORD);
		mask = (((xfs_rtword_t)1 << (lastbit - bit)) - 1) << bit;

		error = xrep_rtbitmap_or(rtb, rtx_to_wordoff(mp, startrtx),
				mask);
		if (error || lastbit - bit == len)
			return error;
		startrtx += XFS_NBWORD - bit;
	}

	/* Set bits as needed to round nextrtx down to the nearest word. */
	bit = nextrtx & XREP_RTBMP_WORDMASK;
	if (bit) {
		mask = ((xfs_rtword_t)1 << bit) - 1;

		error = xrep_rtbitmap_or(rtb, rtx_to_wordoff(mp, nextrtx),
				mask);
		if (error || startrtx + bit == nextrtx)
			return error;
		nextrtx -= bit;
	}

	trace_xrep_rtbitmap_record_free_bulk(mp, startrtx, nextrtx - 1);

	/* Set all the words in between, up to a whole fs block at once. */
	wordoff = rtx_to_wordoff(mp, startrtx);
	nextwordoff = rtx_to_wordoff(mp, nextrtx);
	bufwsize = mp->m_sb.sb_blocksize >> XFS_WORDLOG;

	while (wordoff < nextwordoff) {
		xrep_wordoff_t	rem;
		xrep_wordcnt_t	wordcnt;

		wordcnt = min_t(xrep_wordcnt_t, nextwordoff - wordoff,
				bufwsize);

		/*
		 * Try to keep us aligned to the rtwords buffer to reduce the
		 * number of xfile writes.
		 */
		rem = wordoff & (bufwsize - 1);
		if (rem)
			wordcnt = min_t(xrep_wordcnt_t, wordcnt,
					bufwsize - rem);

		error = xfbmp_copyin(rtb, wordoff, rtb->words, wordcnt);
		if (error)
			return error;

		wordoff += wordcnt;
	}

	return 0;
}

/* Set free space in the rtbitmap based on rtrmapbt records. */
STATIC int
xrep_rtbitmap_walk_rtrmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xchk_rtbitmap		*rtb = priv;
	int				error = 0;

	if (xchk_should_terminate(rtb->sc, &error))
		return error;

	if (rtb->next_rgbno < rec->rm_startblock) {
		error = xrep_rtbitmap_mark_free(rtb, rec->rm_startblock);
		if (error)
			return error;
	}

	rtb->next_rgbno = max(rtb->next_rgbno,
			      rec->rm_startblock + rec->rm_blockcount);
	return 0;
}

/*
 * Walk the rtrmapbt to find all the gaps between records, and mark the gaps
 * in the realtime bitmap that we're computing.
 */
STATIC int
xrep_rtbitmap_find_freespace(
	struct xchk_rtbitmap	*rtb)
{
	struct xfs_scrub	*sc = rtb->sc;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_rtgroup	*rtg = sc->sr.rtg;
	uint64_t		blockcount;
	int			error;

	/* Prepare a buffer of ones so that we can accelerate bulk setting. */
	memset(rtb->words, 0xFF, mp->m_sb.sb_blocksize);

	xrep_rtgroup_btcur_init(sc, &sc->sr);
	error = xfs_rmap_query_all(sc->sr.rmap_cur, xrep_rtbitmap_walk_rtrmap,
			rtb);
	if (error)
		goto out;

	/*
	 * Mark as free every possible rt extent from the last one we saw to
	 * the end of the rt group.
	 */
	blockcount = rtg->rtg_extents * mp->m_sb.sb_rextsize;
	if (rtb->next_rgbno < blockcount) {
		error = xrep_rtbitmap_mark_free(rtb, blockcount);
		if (error)
			goto out;
	}

out:
	xchk_rtgroup_btcur_free(&sc->sr);
	return error;
}

static int
xrep_rtbitmap_prep_buf(
	struct xfs_scrub	*sc,
	struct xfs_buf		*bp,
	void			*data)
{
	struct xchk_rtbitmap	*rtb = data;
	struct xfs_mount	*mp = sc->mp;
	union xfs_rtword_raw	*ondisk;
	int			error;

	rtb->args.mp = sc->mp;
	rtb->args.tp = sc->tp;
	rtb->args.rbmbp = bp;
	ondisk = xfs_rbmblock_wordptr(&rtb->args, 0);
	rtb->args.rbmbp = NULL;

	error = xfbmp_copyout(rtb, rtb->prep_wordoff, ondisk,
			mp->m_blockwsize);
	if (error)
		return error;

	if (xfs_has_rtgroups(sc->mp)) {
		struct xfs_rtbuf_blkinfo	*hdr = bp->b_addr;

		hdr->rt_magic = cpu_to_be32(XFS_RTBITMAP_MAGIC);
		hdr->rt_owner = cpu_to_be64(sc->ip->i_ino);
		hdr->rt_blkno = cpu_to_be64(xfs_buf_daddr(bp));
		hdr->rt_lsn = 0;
		uuid_copy(&hdr->rt_uuid, &sc->mp->m_sb.sb_meta_uuid);
		bp->b_ops = &xfs_rtbitmap_buf_ops;
	} else {
		bp->b_ops = &xfs_rtbuf_ops;
	}

	rtb->prep_wordoff += mp->m_blockwsize;
	xfs_trans_buf_set_type(sc->tp, bp, XFS_BLFT_RTBITMAP_BUF);
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
	struct xfs_group	*xg = rtg_group(sc->sr.rtg);
	unsigned long long	blocks = 0;
	unsigned int		busy_gen;
	int			error;

	/* We require the realtime rmapbt to rebuild anything. */
	if (!xfs_has_rtrmapbt(sc->mp))
		return -EOPNOTSUPP;
	/* We require atomic file exchange range to rebuild anything. */
	if (!xfs_has_exchange_range(sc->mp))
		return -EOPNOTSUPP;

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

	/*
	 * Fix inconsistent bitmap geometry.  This function returns with a
	 * clean scrub transaction.
	 */
	error = xrep_rtbitmap_geometry(sc, rtb);
	if (error)
		return error;

	/*
	 * Make sure the busy extent list is clear because we can't put extents
	 * on there twice.
	 */
	if (!xfs_extent_busy_list_empty(xg, &busy_gen)) {
		error = xfs_extent_busy_flush(sc->tp, xg, busy_gen, 0);
		if (error)
			return error;
	}

	/*
	 * Generate the new rtbitmap data.  We don't need the rtbmp information
	 * once this call is finished.
	 */
	error = xrep_rtbitmap_find_freespace(rtb);
	if (error)
		return error;

	/*
	 * Try to take ILOCK_EXCL of the temporary file.  We had better be the
	 * only ones holding onto this inode, but we can't block while holding
	 * the rtbitmap file's ILOCK_EXCL.
	 */
	while (!xrep_tempfile_ilock_nowait(sc)) {
		if (xchk_should_terminate(sc, &error))
			return error;
		delay(1);
	}

	/*
	 * Make sure we have space allocated for the part of the bitmap
	 * file that corresponds to this group.  We already joined sc->ip.
	 */
	xfs_trans_ijoin(sc->tp, sc->tempip, 0);
	error = xrep_tempfile_prealloc(sc, 0, rtb->rbmblocks);
	if (error)
		return error;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		return error;

	/* Copy the bitmap file that we generated. */
	error = xrep_tempfile_copyin(sc, 0, rtb->rbmblocks,
			xrep_rtbitmap_prep_buf, rtb);
	if (error)
		return error;
	error = xrep_tempfile_set_isize(sc,
			XFS_FSB_TO_B(sc->mp, sc->mp->m_sb.sb_rbmblocks));
	if (error)
		return error;

	/*
	 * Now exchange the data fork contents.  We're done with the temporary
	 * buffer, so we can reuse it for the tempfile exchmaps information.
	 */
	error = xrep_tempexch_trans_reserve(sc, XFS_DATA_FORK, 0,
			rtb->rbmblocks, &rtb->tempexch);
	if (error)
		return error;

	error = xrep_tempexch_contents(sc, &rtb->tempexch);
	if (error)
		return error;

	/* Free the old rtbitmap blocks if they're not in use. */
	return xrep_reap_ifork(sc, sc->tempip, XFS_DATA_FORK);
}
