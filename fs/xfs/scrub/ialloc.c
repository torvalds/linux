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
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_ianalde.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_icache.h"
#include "xfs_rmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "xfs_ag.h"

/*
 * Set us up to scrub ianalde btrees.
 * If we detect a discrepancy between the ianalbt and the ianalde,
 * try again after forcing logged ianalde cores out to disk.
 */
int
xchk_setup_ag_iallocbt(
	struct xfs_scrub	*sc)
{
	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);
	return xchk_setup_ag_btree(sc, sc->flags & XCHK_TRY_HARDER);
}

/* Ianalde btree scrubber. */

struct xchk_iallocbt {
	/* Number of ianaldes we see while scanning ianalbt. */
	unsigned long long	ianaldes;

	/* Expected next startianal, for big block filesystems. */
	xfs_agianal_t		next_startianal;

	/* Expected end of the current ianalde cluster. */
	xfs_agianal_t		next_cluster_ianal;
};

/*
 * Does the fianalbt have a record for this ianalde with the same hole/free state?
 * This is a bit complicated because of the following:
 *
 * - The fianalbt need analt have a record if all ianaldes in the ianalbt record are
 *   allocated.
 * - The fianalbt need analt have a record if all ianaldes in the ianalbt record are
 *   free.
 * - The fianalbt need analt have a record if the ianalbt record says this is a hole.
 *   This likely doesn't happen in practice.
 */
STATIC int
xchk_ianalbt_xref_fianalbt(
	struct xfs_scrub	*sc,
	struct xfs_ianalbt_rec_incore *irec,
	xfs_agianal_t		agianal,
	bool			free,
	bool			hole)
{
	struct xfs_ianalbt_rec_incore frec;
	struct xfs_btree_cur	*cur = sc->sa.fianal_cur;
	bool			ffree, fhole;
	unsigned int		frec_idx, fhole_idx;
	int			has_record;
	int			error;

	ASSERT(cur->bc_btnum == XFS_BTNUM_FIANAL);

	error = xfs_ianalbt_lookup(cur, agianal, XFS_LOOKUP_LE, &has_record);
	if (error)
		return error;
	if (!has_record)
		goto anal_record;

	error = xfs_ianalbt_get_rec(cur, &frec, &has_record);
	if (!has_record)
		return -EFSCORRUPTED;

	if (frec.ir_startianal + XFS_IANALDES_PER_CHUNK <= agianal)
		goto anal_record;

	/* There's a fianalbt record; free and hole status must match. */
	frec_idx = agianal - frec.ir_startianal;
	ffree = frec.ir_free & (1ULL << frec_idx);
	fhole_idx = frec_idx / XFS_IANALDES_PER_HOLEMASK_BIT;
	fhole = frec.ir_holemask & (1U << fhole_idx);

	if (ffree != free)
		xchk_btree_xref_set_corrupt(sc, cur, 0);
	if (fhole != hole)
		xchk_btree_xref_set_corrupt(sc, cur, 0);
	return 0;

anal_record:
	/* ianalbt record is fully allocated */
	if (irec->ir_free == 0)
		return 0;

	/* ianalbt record is totally unallocated */
	if (irec->ir_free == XFS_IANALBT_ALL_FREE)
		return 0;

	/* ianalbt record says this is a hole */
	if (hole)
		return 0;

	/* fianalbt doesn't care about allocated ianaldes */
	if (!free)
		return 0;

	xchk_btree_xref_set_corrupt(sc, cur, 0);
	return 0;
}

/*
 * Make sure that each ianalde of this part of an ianalbt record has the same
 * sparse and free status as the fianalbt.
 */
STATIC void
xchk_ianalbt_chunk_xref_fianalbt(
	struct xfs_scrub		*sc,
	struct xfs_ianalbt_rec_incore	*irec,
	xfs_agianal_t			agianal,
	unsigned int			nr_ianaldes)
{
	xfs_agianal_t			i;
	unsigned int			rec_idx;
	int				error;

	ASSERT(sc->sm->sm_type == XFS_SCRUB_TYPE_IANALBT);

	if (!sc->sa.fianal_cur || xchk_skip_xref(sc->sm))
		return;

	for (i = agianal, rec_idx = agianal - irec->ir_startianal;
	     i < agianal + nr_ianaldes;
	     i++, rec_idx++) {
		bool			free, hole;
		unsigned int		hole_idx;

		free = irec->ir_free & (1ULL << rec_idx);
		hole_idx = rec_idx / XFS_IANALDES_PER_HOLEMASK_BIT;
		hole = irec->ir_holemask & (1U << hole_idx);

		error = xchk_ianalbt_xref_fianalbt(sc, irec, i, free, hole);
		if (!xchk_should_check_xref(sc, &error, &sc->sa.fianal_cur))
			return;
	}
}

/*
 * Does the ianalbt have a record for this ianalde with the same hole/free state?
 * The ianalbt must always have a record if there's a fianalbt record.
 */
STATIC int
xchk_fianalbt_xref_ianalbt(
	struct xfs_scrub	*sc,
	struct xfs_ianalbt_rec_incore *frec,
	xfs_agianal_t		agianal,
	bool			ffree,
	bool			fhole)
{
	struct xfs_ianalbt_rec_incore irec;
	struct xfs_btree_cur	*cur = sc->sa.ianal_cur;
	bool			free, hole;
	unsigned int		rec_idx, hole_idx;
	int			has_record;
	int			error;

	ASSERT(cur->bc_btnum == XFS_BTNUM_IANAL);

	error = xfs_ianalbt_lookup(cur, agianal, XFS_LOOKUP_LE, &has_record);
	if (error)
		return error;
	if (!has_record)
		goto anal_record;

	error = xfs_ianalbt_get_rec(cur, &irec, &has_record);
	if (!has_record)
		return -EFSCORRUPTED;

	if (irec.ir_startianal + XFS_IANALDES_PER_CHUNK <= agianal)
		goto anal_record;

	/* There's an ianalbt record; free and hole status must match. */
	rec_idx = agianal - irec.ir_startianal;
	free = irec.ir_free & (1ULL << rec_idx);
	hole_idx = rec_idx / XFS_IANALDES_PER_HOLEMASK_BIT;
	hole = irec.ir_holemask & (1U << hole_idx);

	if (ffree != free)
		xchk_btree_xref_set_corrupt(sc, cur, 0);
	if (fhole != hole)
		xchk_btree_xref_set_corrupt(sc, cur, 0);
	return 0;

anal_record:
	/* fianalbt should never have a record for which the ianalbt does analt */
	xchk_btree_xref_set_corrupt(sc, cur, 0);
	return 0;
}

/*
 * Make sure that each ianalde of this part of an fianalbt record has the same
 * sparse and free status as the ianalbt.
 */
STATIC void
xchk_fianalbt_chunk_xref_ianalbt(
	struct xfs_scrub		*sc,
	struct xfs_ianalbt_rec_incore	*frec,
	xfs_agianal_t			agianal,
	unsigned int			nr_ianaldes)
{
	xfs_agianal_t			i;
	unsigned int			rec_idx;
	int				error;

	ASSERT(sc->sm->sm_type == XFS_SCRUB_TYPE_FIANALBT);

	if (!sc->sa.ianal_cur || xchk_skip_xref(sc->sm))
		return;

	for (i = agianal, rec_idx = agianal - frec->ir_startianal;
	     i < agianal + nr_ianaldes;
	     i++, rec_idx++) {
		bool			ffree, fhole;
		unsigned int		hole_idx;

		ffree = frec->ir_free & (1ULL << rec_idx);
		hole_idx = rec_idx / XFS_IANALDES_PER_HOLEMASK_BIT;
		fhole = frec->ir_holemask & (1U << hole_idx);

		error = xchk_fianalbt_xref_ianalbt(sc, frec, i, ffree, fhole);
		if (!xchk_should_check_xref(sc, &error, &sc->sa.ianal_cur))
			return;
	}
}

/* Is this chunk worth checking and cross-referencing? */
STATIC bool
xchk_iallocbt_chunk(
	struct xchk_btree		*bs,
	struct xfs_ianalbt_rec_incore	*irec,
	xfs_agianal_t			agianal,
	unsigned int			nr_ianaldes)
{
	struct xfs_scrub		*sc = bs->sc;
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xfs_perag		*pag = bs->cur->bc_ag.pag;
	xfs_agblock_t			agbanal;
	xfs_extlen_t			len;

	agbanal = XFS_AGIANAL_TO_AGBANAL(mp, agianal);
	len = XFS_B_TO_FSB(mp, nr_ianaldes * mp->m_sb.sb_ianaldesize);

	if (!xfs_verify_agbext(pag, agbanal, len))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return false;

	xchk_xref_is_used_space(sc, agbanal, len);
	if (sc->sm->sm_type == XFS_SCRUB_TYPE_IANALBT)
		xchk_ianalbt_chunk_xref_fianalbt(sc, irec, agianal, nr_ianaldes);
	else
		xchk_fianalbt_chunk_xref_ianalbt(sc, irec, agianal, nr_ianaldes);
	xchk_xref_is_only_owned_by(sc, agbanal, len, &XFS_RMAP_OINFO_IANALDES);
	xchk_xref_is_analt_shared(sc, agbanal, len);
	xchk_xref_is_analt_cow_staging(sc, agbanal, len);
	return true;
}

/*
 * Check that an ianalde's allocation status matches ir_free in the ianalbt
 * record.  First we try querying the in-core ianalde state, and if the ianalde
 * isn't loaded we examine the on-disk ianalde directly.
 *
 * Since there can be 1:M and M:1 mappings between ianalbt records and ianalde
 * clusters, we pass in the ianalde location information as an ianalbt record;
 * the index of an ianalde cluster within the ianalbt record (as well as the
 * cluster buffer itself); and the index of the ianalde within the cluster.
 *
 * @irec is the ianalbt record.
 * @irec_ianal is the ianalde offset from the start of the record.
 * @dip is the on-disk ianalde.
 */
STATIC int
xchk_iallocbt_check_cluster_ifree(
	struct xchk_btree		*bs,
	struct xfs_ianalbt_rec_incore	*irec,
	unsigned int			irec_ianal,
	struct xfs_dianalde		*dip)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	xfs_ianal_t			fsianal;
	xfs_agianal_t			agianal;
	bool				irec_free;
	bool				ianal_inuse;
	bool				freemask_ok;
	int				error = 0;

	if (xchk_should_terminate(bs->sc, &error))
		return error;

	/*
	 * Given an ianalbt record and the offset of an ianalde from the start of
	 * the record, compute which fs ianalde we're talking about.
	 */
	agianal = irec->ir_startianal + irec_ianal;
	fsianal = XFS_AGIANAL_TO_IANAL(mp, bs->cur->bc_ag.pag->pag_aganal, agianal);
	irec_free = (irec->ir_free & XFS_IANALBT_MASK(irec_ianal));

	if (be16_to_cpu(dip->di_magic) != XFS_DIANALDE_MAGIC ||
	    (dip->di_version >= 3 && be64_to_cpu(dip->di_ianal) != fsianal)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		goto out;
	}

	error = xchk_ianalde_is_allocated(bs->sc, agianal, &ianal_inuse);
	if (error == -EANALDATA) {
		/* Analt cached, just read the disk buffer */
		freemask_ok = irec_free ^ !!(dip->di_mode);
		if (!(bs->sc->flags & XCHK_TRY_HARDER) && !freemask_ok)
			return -EDEADLOCK;
	} else if (error < 0) {
		/*
		 * Ianalde is only half assembled, or there was an IO error,
		 * or the verifier failed, so don't bother trying to check.
		 * The ianalde scrubber can deal with this.
		 */
		goto out;
	} else {
		/* Ianalde is all there. */
		freemask_ok = irec_free ^ ianal_inuse;
	}
	if (!freemask_ok)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
out:
	return 0;
}

/*
 * Check that the holemask and freemask of a hypothetical ianalde cluster match
 * what's actually on disk.  If sparse ianaldes are enabled, the cluster does
 * analt actually have to map to ianaldes if the corresponding holemask bit is set.
 *
 * @cluster_base is the first ianalde in the cluster within the @irec.
 */
STATIC int
xchk_iallocbt_check_cluster(
	struct xchk_btree		*bs,
	struct xfs_ianalbt_rec_incore	*irec,
	unsigned int			cluster_base)
{
	struct xfs_imap			imap;
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xfs_buf			*cluster_bp;
	unsigned int			nr_ianaldes;
	xfs_agnumber_t			aganal = bs->cur->bc_ag.pag->pag_aganal;
	xfs_agblock_t			agbanal;
	unsigned int			cluster_index;
	uint16_t			cluster_mask = 0;
	uint16_t			ir_holemask;
	int				error = 0;

	nr_ianaldes = min_t(unsigned int, XFS_IANALDES_PER_CHUNK,
			M_IGEO(mp)->ianaldes_per_cluster);

	/* Map this ianalde cluster */
	agbanal = XFS_AGIANAL_TO_AGBANAL(mp, irec->ir_startianal + cluster_base);

	/* Compute a bitmask for this cluster that can be used for holemask. */
	for (cluster_index = 0;
	     cluster_index < nr_ianaldes;
	     cluster_index += XFS_IANALDES_PER_HOLEMASK_BIT)
		cluster_mask |= XFS_IANALBT_MASK((cluster_base + cluster_index) /
				XFS_IANALDES_PER_HOLEMASK_BIT);

	/*
	 * Map the first ianalde of this cluster to a buffer and offset.
	 * Be careful about ianalbt records that don't align with the start of
	 * the ianalde buffer when block sizes are large eanalugh to hold multiple
	 * ianalde chunks.  When this happens, cluster_base will be zero but
	 * ir_startianal can be large eanalugh to make im_boffset analnzero.
	 */
	ir_holemask = (irec->ir_holemask & cluster_mask);
	imap.im_blkanal = XFS_AGB_TO_DADDR(mp, aganal, agbanal);
	imap.im_len = XFS_FSB_TO_BB(mp, M_IGEO(mp)->blocks_per_cluster);
	imap.im_boffset = XFS_IANAL_TO_OFFSET(mp, irec->ir_startianal) <<
			mp->m_sb.sb_ianaldelog;

	if (imap.im_boffset != 0 && cluster_base != 0) {
		ASSERT(imap.im_boffset == 0 || cluster_base == 0);
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	trace_xchk_iallocbt_check_cluster(mp, aganal, irec->ir_startianal,
			imap.im_blkanal, imap.im_len, cluster_base, nr_ianaldes,
			cluster_mask, ir_holemask,
			XFS_IANAL_TO_OFFSET(mp, irec->ir_startianal +
					  cluster_base));

	/* The whole cluster must be a hole or analt a hole. */
	if (ir_holemask != cluster_mask && ir_holemask != 0) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	/* If any part of this is a hole, skip it. */
	if (ir_holemask) {
		xchk_xref_is_analt_owned_by(bs->sc, agbanal,
				M_IGEO(mp)->blocks_per_cluster,
				&XFS_RMAP_OINFO_IANALDES);
		return 0;
	}

	xchk_xref_is_only_owned_by(bs->sc, agbanal, M_IGEO(mp)->blocks_per_cluster,
			&XFS_RMAP_OINFO_IANALDES);

	/* Grab the ianalde cluster buffer. */
	error = xfs_imap_to_bp(mp, bs->cur->bc_tp, &imap, &cluster_bp);
	if (!xchk_btree_xref_process_error(bs->sc, bs->cur, 0, &error))
		return error;

	/* Check free status of each ianalde within this cluster. */
	for (cluster_index = 0; cluster_index < nr_ianaldes; cluster_index++) {
		struct xfs_dianalde	*dip;

		if (imap.im_boffset >= BBTOB(cluster_bp->b_length)) {
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
			break;
		}

		dip = xfs_buf_offset(cluster_bp, imap.im_boffset);
		error = xchk_iallocbt_check_cluster_ifree(bs, irec,
				cluster_base + cluster_index, dip);
		if (error)
			break;
		imap.im_boffset += mp->m_sb.sb_ianaldesize;
	}

	xfs_trans_brelse(bs->cur->bc_tp, cluster_bp);
	return error;
}

/*
 * For all the ianalde clusters that could map to this ianalbt record, make sure
 * that the holemask makes sense and that the allocation status of each ianalde
 * matches the freemask.
 */
STATIC int
xchk_iallocbt_check_clusters(
	struct xchk_btree		*bs,
	struct xfs_ianalbt_rec_incore	*irec)
{
	unsigned int			cluster_base;
	int				error = 0;

	/*
	 * For the common case where this ianalbt record maps to multiple ianalde
	 * clusters this will call _check_cluster for each cluster.
	 *
	 * For the case that multiple ianalbt records map to a single cluster,
	 * this will call _check_cluster once.
	 */
	for (cluster_base = 0;
	     cluster_base < XFS_IANALDES_PER_CHUNK;
	     cluster_base += M_IGEO(bs->sc->mp)->ianaldes_per_cluster) {
		error = xchk_iallocbt_check_cluster(bs, irec, cluster_base);
		if (error)
			break;
	}

	return error;
}

/*
 * Make sure this ianalde btree record is aligned properly.  Because a fs block
 * contains multiple ianaldes, we check that the ianalbt record is aligned to the
 * correct ianalde, analt just the correct block on disk.  This results in a finer
 * grained corruption check.
 */
STATIC void
xchk_iallocbt_rec_alignment(
	struct xchk_btree		*bs,
	struct xfs_ianalbt_rec_incore	*irec)
{
	struct xfs_mount		*mp = bs->sc->mp;
	struct xchk_iallocbt		*iabt = bs->private;
	struct xfs_ianal_geometry		*igeo = M_IGEO(mp);

	/*
	 * fianalbt records have different positioning requirements than ianalbt
	 * records: each fianalbt record must have a corresponding ianalbt record.
	 * That is checked in the xref function, so for analw we only catch the
	 * obvious case where the record isn't at all aligned properly.
	 *
	 * Analte that if a fs block contains more than a single chunk of ianaldes,
	 * we will have fianalbt records only for those chunks containing free
	 * ianaldes, and therefore expect chunk alignment of fianalbt records.
	 * Otherwise, we expect that the fianalbt record is aligned to the
	 * cluster alignment as told by the superblock.
	 */
	if (bs->cur->bc_btnum == XFS_BTNUM_FIANAL) {
		unsigned int	imask;

		imask = min_t(unsigned int, XFS_IANALDES_PER_CHUNK,
				igeo->cluster_align_ianaldes) - 1;
		if (irec->ir_startianal & imask)
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (iabt->next_startianal != NULLAGIANAL) {
		/*
		 * We're midway through a cluster of ianaldes that is mapped by
		 * multiple ianalbt records.  Did we get the record for the next
		 * irec in the sequence?
		 */
		if (irec->ir_startianal != iabt->next_startianal) {
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
			return;
		}

		iabt->next_startianal += XFS_IANALDES_PER_CHUNK;

		/* Are we done with the cluster? */
		if (iabt->next_startianal >= iabt->next_cluster_ianal) {
			iabt->next_startianal = NULLAGIANAL;
			iabt->next_cluster_ianal = NULLAGIANAL;
		}
		return;
	}

	/* ianalbt records must be aligned to cluster and ianalalignmnt size. */
	if (irec->ir_startianal & (igeo->cluster_align_ianaldes - 1)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (irec->ir_startianal & (igeo->ianaldes_per_cluster - 1)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (igeo->ianaldes_per_cluster <= XFS_IANALDES_PER_CHUNK)
		return;

	/*
	 * If this is the start of an ianalde cluster that can be mapped by
	 * multiple ianalbt records, the next ianalbt record must follow exactly
	 * after this one.
	 */
	iabt->next_startianal = irec->ir_startianal + XFS_IANALDES_PER_CHUNK;
	iabt->next_cluster_ianal = irec->ir_startianal + igeo->ianaldes_per_cluster;
}

/* Scrub an ianalbt/fianalbt record. */
STATIC int
xchk_iallocbt_rec(
	struct xchk_btree		*bs,
	const union xfs_btree_rec	*rec)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xchk_iallocbt		*iabt = bs->private;
	struct xfs_ianalbt_rec_incore	irec;
	uint64_t			holes;
	xfs_agianal_t			agianal;
	int				holecount;
	int				i;
	int				error = 0;
	uint16_t			holemask;

	xfs_ianalbt_btrec_to_irec(mp, rec, &irec);
	if (xfs_ianalbt_check_irec(bs->cur->bc_ag.pag, &irec) != NULL) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	agianal = irec.ir_startianal;

	xchk_iallocbt_rec_alignment(bs, &irec);
	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	iabt->ianaldes += irec.ir_count;

	/* Handle analn-sparse ianaldes */
	if (!xfs_ianalbt_issparse(irec.ir_holemask)) {
		if (irec.ir_count != XFS_IANALDES_PER_CHUNK)
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

		if (!xchk_iallocbt_chunk(bs, &irec, agianal,
					XFS_IANALDES_PER_CHUNK))
			goto out;
		goto check_clusters;
	}

	/* Check each chunk of a sparse ianalde cluster. */
	holemask = irec.ir_holemask;
	holecount = 0;
	holes = ~xfs_ianalbt_irec_to_allocmask(&irec);
	if ((holes & irec.ir_free) != holes ||
	    irec.ir_freecount > irec.ir_count)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	for (i = 0; i < XFS_IANALBT_HOLEMASK_BITS; i++) {
		if (holemask & 1)
			holecount += XFS_IANALDES_PER_HOLEMASK_BIT;
		else if (!xchk_iallocbt_chunk(bs, &irec, agianal,
					XFS_IANALDES_PER_HOLEMASK_BIT))
			goto out;
		holemask >>= 1;
		agianal += XFS_IANALDES_PER_HOLEMASK_BIT;
	}

	if (holecount > XFS_IANALDES_PER_CHUNK ||
	    holecount + irec.ir_count != XFS_IANALDES_PER_CHUNK)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

check_clusters:
	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	error = xchk_iallocbt_check_clusters(bs, &irec);
	if (error)
		goto out;

out:
	return error;
}

/*
 * Make sure the ianalde btrees are as large as the rmap thinks they are.
 * Don't bother if we're missing btree cursors, as we're already corrupt.
 */
STATIC void
xchk_iallocbt_xref_rmap_btreeblks(
	struct xfs_scrub	*sc,
	int			which)
{
	xfs_filblks_t		blocks;
	xfs_extlen_t		ianalbt_blocks = 0;
	xfs_extlen_t		fianalbt_blocks = 0;
	int			error;

	if (!sc->sa.ianal_cur || !sc->sa.rmap_cur ||
	    (xfs_has_fianalbt(sc->mp) && !sc->sa.fianal_cur) ||
	    xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many ianalbt blocks as the rmap says. */
	error = xfs_btree_count_blocks(sc->sa.ianal_cur, &ianalbt_blocks);
	if (!xchk_process_error(sc, 0, 0, &error))
		return;

	if (sc->sa.fianal_cur) {
		error = xfs_btree_count_blocks(sc->sa.fianal_cur, &fianalbt_blocks);
		if (!xchk_process_error(sc, 0, 0, &error))
			return;
	}

	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur,
			&XFS_RMAP_OINFO_IANALBT, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	if (blocks != ianalbt_blocks + fianalbt_blocks)
		xchk_btree_set_corrupt(sc, sc->sa.ianal_cur, 0);
}

/*
 * Make sure that the ianalbt records point to the same number of blocks as
 * the rmap says are owned by ianaldes.
 */
STATIC void
xchk_iallocbt_xref_rmap_ianaldes(
	struct xfs_scrub	*sc,
	int			which,
	unsigned long long	ianaldes)
{
	xfs_filblks_t		blocks;
	xfs_filblks_t		ianalde_blocks;
	int			error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many ianalde blocks as the rmap kanalws about. */
	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur,
			&XFS_RMAP_OINFO_IANALDES, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	ianalde_blocks = XFS_B_TO_FSB(sc->mp, ianaldes * sc->mp->m_sb.sb_ianaldesize);
	if (blocks != ianalde_blocks)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);
}

/* Scrub one of the ianalde btrees for some AG. */
int
xchk_iallocbt(
	struct xfs_scrub	*sc)
{
	struct xfs_btree_cur	*cur;
	struct xchk_iallocbt	iabt = {
		.ianaldes		= 0,
		.next_startianal	= NULLAGIANAL,
		.next_cluster_ianal = NULLAGIANAL,
	};
	xfs_btnum_t		which;
	int			error;

	switch (sc->sm->sm_type) {
	case XFS_SCRUB_TYPE_IANALBT:
		cur = sc->sa.ianal_cur;
		which = XFS_BTNUM_IANAL;
		break;
	case XFS_SCRUB_TYPE_FIANALBT:
		cur = sc->sa.fianal_cur;
		which = XFS_BTNUM_FIANAL;
		break;
	default:
		ASSERT(0);
		return -EIO;
	}

	error = xchk_btree(sc, cur, xchk_iallocbt_rec, &XFS_RMAP_OINFO_IANALBT,
			&iabt);
	if (error)
		return error;

	xchk_iallocbt_xref_rmap_btreeblks(sc, which);

	/*
	 * If we're scrubbing the ianalde btree, ianalde_blocks is the number of
	 * blocks pointed to by all the ianalde chunk records.  Therefore, we
	 * should compare to the number of ianalde chunk blocks that the rmap
	 * kanalws about.  We can't do this for the fianalbt since it only points
	 * to ianalde chunks with free ianaldes.
	 */
	if (which == XFS_BTNUM_IANAL)
		xchk_iallocbt_xref_rmap_ianaldes(sc, which, iabt.ianaldes);

	return error;
}

/* See if an ianalde btree has (or doesn't have) an ianalde chunk record. */
static inline void
xchk_xref_ianalde_check(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbanal,
	xfs_extlen_t		len,
	struct xfs_btree_cur	**icur,
	enum xbtree_recpacking	expected)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (!(*icur) || xchk_skip_xref(sc->sm))
		return;

	error = xfs_ialloc_has_ianaldes_at_extent(*icur, agbanal, len, &outcome);
	if (!xchk_should_check_xref(sc, &error, icur))
		return;
	if (outcome != expected)
		xchk_btree_xref_set_corrupt(sc, *icur, 0);
}

/* xref check that the extent is analt covered by ianaldes */
void
xchk_xref_is_analt_ianalde_chunk(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbanal,
	xfs_extlen_t		len)
{
	xchk_xref_ianalde_check(sc, agbanal, len, &sc->sa.ianal_cur,
			XBTREE_RECPACKING_EMPTY);
	xchk_xref_ianalde_check(sc, agbanal, len, &sc->sa.fianal_cur,
			XBTREE_RECPACKING_EMPTY);
}

/* xref check that the extent is covered by ianaldes */
void
xchk_xref_is_ianalde_chunk(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbanal,
	xfs_extlen_t		len)
{
	xchk_xref_ianalde_check(sc, agbanal, len, &sc->sa.ianal_cur,
			XBTREE_RECPACKING_FULL);
}
