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
#include "xfs_inode.h"
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
 * Set us up to scrub inode btrees.
 * If we detect a discrepancy between the inobt and the inode,
 * try again after forcing logged inode cores out to disk.
 */
int
xchk_setup_ag_iallocbt(
	struct xfs_scrub	*sc)
{
	if (xchk_need_intent_drain(sc))
		xchk_fsgates_enable(sc, XCHK_FSGATES_DRAIN);
	return xchk_setup_ag_btree(sc, sc->flags & XCHK_TRY_HARDER);
}

/* Inode btree scrubber. */

struct xchk_iallocbt {
	/* Number of inodes we see while scanning inobt. */
	unsigned long long	inodes;

	/* Expected next startino, for big block filesystems. */
	xfs_agino_t		next_startino;

	/* Expected end of the current inode cluster. */
	xfs_agino_t		next_cluster_ino;
};

/*
 * Does the finobt have a record for this inode with the same hole/free state?
 * This is a bit complicated because of the following:
 *
 * - The finobt need not have a record if all inodes in the inobt record are
 *   allocated.
 * - The finobt need not have a record if all inodes in the inobt record are
 *   free.
 * - The finobt need not have a record if the inobt record says this is a hole.
 *   This likely doesn't happen in practice.
 */
STATIC int
xchk_inobt_xref_finobt(
	struct xfs_scrub	*sc,
	struct xfs_inobt_rec_incore *irec,
	xfs_agino_t		agino,
	bool			free,
	bool			hole)
{
	struct xfs_inobt_rec_incore frec;
	struct xfs_btree_cur	*cur = sc->sa.fino_cur;
	bool			ffree, fhole;
	unsigned int		frec_idx, fhole_idx;
	int			has_record;
	int			error;

	ASSERT(cur->bc_btnum == XFS_BTNUM_FINO);

	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &has_record);
	if (error)
		return error;
	if (!has_record)
		goto no_record;

	error = xfs_inobt_get_rec(cur, &frec, &has_record);
	if (!has_record)
		return -EFSCORRUPTED;

	if (frec.ir_startino + XFS_INODES_PER_CHUNK <= agino)
		goto no_record;

	/* There's a finobt record; free and hole status must match. */
	frec_idx = agino - frec.ir_startino;
	ffree = frec.ir_free & (1ULL << frec_idx);
	fhole_idx = frec_idx / XFS_INODES_PER_HOLEMASK_BIT;
	fhole = frec.ir_holemask & (1U << fhole_idx);

	if (ffree != free)
		xchk_btree_xref_set_corrupt(sc, cur, 0);
	if (fhole != hole)
		xchk_btree_xref_set_corrupt(sc, cur, 0);
	return 0;

no_record:
	/* inobt record is fully allocated */
	if (irec->ir_free == 0)
		return 0;

	/* inobt record is totally unallocated */
	if (irec->ir_free == XFS_INOBT_ALL_FREE)
		return 0;

	/* inobt record says this is a hole */
	if (hole)
		return 0;

	/* finobt doesn't care about allocated inodes */
	if (!free)
		return 0;

	xchk_btree_xref_set_corrupt(sc, cur, 0);
	return 0;
}

/*
 * Make sure that each inode of this part of an inobt record has the same
 * sparse and free status as the finobt.
 */
STATIC void
xchk_inobt_chunk_xref_finobt(
	struct xfs_scrub		*sc,
	struct xfs_inobt_rec_incore	*irec,
	xfs_agino_t			agino,
	unsigned int			nr_inodes)
{
	xfs_agino_t			i;
	unsigned int			rec_idx;
	int				error;

	ASSERT(sc->sm->sm_type == XFS_SCRUB_TYPE_INOBT);

	if (!sc->sa.fino_cur || xchk_skip_xref(sc->sm))
		return;

	for (i = agino, rec_idx = agino - irec->ir_startino;
	     i < agino + nr_inodes;
	     i++, rec_idx++) {
		bool			free, hole;
		unsigned int		hole_idx;

		free = irec->ir_free & (1ULL << rec_idx);
		hole_idx = rec_idx / XFS_INODES_PER_HOLEMASK_BIT;
		hole = irec->ir_holemask & (1U << hole_idx);

		error = xchk_inobt_xref_finobt(sc, irec, i, free, hole);
		if (!xchk_should_check_xref(sc, &error, &sc->sa.fino_cur))
			return;
	}
}

/*
 * Does the inobt have a record for this inode with the same hole/free state?
 * The inobt must always have a record if there's a finobt record.
 */
STATIC int
xchk_finobt_xref_inobt(
	struct xfs_scrub	*sc,
	struct xfs_inobt_rec_incore *frec,
	xfs_agino_t		agino,
	bool			ffree,
	bool			fhole)
{
	struct xfs_inobt_rec_incore irec;
	struct xfs_btree_cur	*cur = sc->sa.ino_cur;
	bool			free, hole;
	unsigned int		rec_idx, hole_idx;
	int			has_record;
	int			error;

	ASSERT(cur->bc_btnum == XFS_BTNUM_INO);

	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &has_record);
	if (error)
		return error;
	if (!has_record)
		goto no_record;

	error = xfs_inobt_get_rec(cur, &irec, &has_record);
	if (!has_record)
		return -EFSCORRUPTED;

	if (irec.ir_startino + XFS_INODES_PER_CHUNK <= agino)
		goto no_record;

	/* There's an inobt record; free and hole status must match. */
	rec_idx = agino - irec.ir_startino;
	free = irec.ir_free & (1ULL << rec_idx);
	hole_idx = rec_idx / XFS_INODES_PER_HOLEMASK_BIT;
	hole = irec.ir_holemask & (1U << hole_idx);

	if (ffree != free)
		xchk_btree_xref_set_corrupt(sc, cur, 0);
	if (fhole != hole)
		xchk_btree_xref_set_corrupt(sc, cur, 0);
	return 0;

no_record:
	/* finobt should never have a record for which the inobt does not */
	xchk_btree_xref_set_corrupt(sc, cur, 0);
	return 0;
}

/*
 * Make sure that each inode of this part of an finobt record has the same
 * sparse and free status as the inobt.
 */
STATIC void
xchk_finobt_chunk_xref_inobt(
	struct xfs_scrub		*sc,
	struct xfs_inobt_rec_incore	*frec,
	xfs_agino_t			agino,
	unsigned int			nr_inodes)
{
	xfs_agino_t			i;
	unsigned int			rec_idx;
	int				error;

	ASSERT(sc->sm->sm_type == XFS_SCRUB_TYPE_FINOBT);

	if (!sc->sa.ino_cur || xchk_skip_xref(sc->sm))
		return;

	for (i = agino, rec_idx = agino - frec->ir_startino;
	     i < agino + nr_inodes;
	     i++, rec_idx++) {
		bool			ffree, fhole;
		unsigned int		hole_idx;

		ffree = frec->ir_free & (1ULL << rec_idx);
		hole_idx = rec_idx / XFS_INODES_PER_HOLEMASK_BIT;
		fhole = frec->ir_holemask & (1U << hole_idx);

		error = xchk_finobt_xref_inobt(sc, frec, i, ffree, fhole);
		if (!xchk_should_check_xref(sc, &error, &sc->sa.ino_cur))
			return;
	}
}

/* Is this chunk worth checking and cross-referencing? */
STATIC bool
xchk_iallocbt_chunk(
	struct xchk_btree		*bs,
	struct xfs_inobt_rec_incore	*irec,
	xfs_agino_t			agino,
	unsigned int			nr_inodes)
{
	struct xfs_scrub		*sc = bs->sc;
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xfs_perag		*pag = bs->cur->bc_ag.pag;
	xfs_agblock_t			agbno;
	xfs_extlen_t			len;

	agbno = XFS_AGINO_TO_AGBNO(mp, agino);
	len = XFS_B_TO_FSB(mp, nr_inodes * mp->m_sb.sb_inodesize);

	if (!xfs_verify_agbext(pag, agbno, len))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return false;

	xchk_xref_is_used_space(sc, agbno, len);
	if (sc->sm->sm_type == XFS_SCRUB_TYPE_INOBT)
		xchk_inobt_chunk_xref_finobt(sc, irec, agino, nr_inodes);
	else
		xchk_finobt_chunk_xref_inobt(sc, irec, agino, nr_inodes);
	xchk_xref_is_only_owned_by(sc, agbno, len, &XFS_RMAP_OINFO_INODES);
	xchk_xref_is_not_shared(sc, agbno, len);
	xchk_xref_is_not_cow_staging(sc, agbno, len);
	return true;
}

/*
 * Check that an inode's allocation status matches ir_free in the inobt
 * record.  First we try querying the in-core inode state, and if the inode
 * isn't loaded we examine the on-disk inode directly.
 *
 * Since there can be 1:M and M:1 mappings between inobt records and inode
 * clusters, we pass in the inode location information as an inobt record;
 * the index of an inode cluster within the inobt record (as well as the
 * cluster buffer itself); and the index of the inode within the cluster.
 *
 * @irec is the inobt record.
 * @irec_ino is the inode offset from the start of the record.
 * @dip is the on-disk inode.
 */
STATIC int
xchk_iallocbt_check_cluster_ifree(
	struct xchk_btree		*bs,
	struct xfs_inobt_rec_incore	*irec,
	unsigned int			irec_ino,
	struct xfs_dinode		*dip)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	xfs_ino_t			fsino;
	xfs_agino_t			agino;
	bool				irec_free;
	bool				ino_inuse;
	bool				freemask_ok;
	int				error = 0;

	if (xchk_should_terminate(bs->sc, &error))
		return error;

	/*
	 * Given an inobt record and the offset of an inode from the start of
	 * the record, compute which fs inode we're talking about.
	 */
	agino = irec->ir_startino + irec_ino;
	fsino = XFS_AGINO_TO_INO(mp, bs->cur->bc_ag.pag->pag_agno, agino);
	irec_free = (irec->ir_free & XFS_INOBT_MASK(irec_ino));

	if (be16_to_cpu(dip->di_magic) != XFS_DINODE_MAGIC ||
	    (dip->di_version >= 3 && be64_to_cpu(dip->di_ino) != fsino)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		goto out;
	}

	error = xchk_inode_is_allocated(bs->sc, agino, &ino_inuse);
	if (error == -ENODATA) {
		/* Not cached, just read the disk buffer */
		freemask_ok = irec_free ^ !!(dip->di_mode);
		if (!(bs->sc->flags & XCHK_TRY_HARDER) && !freemask_ok)
			return -EDEADLOCK;
	} else if (error < 0) {
		/*
		 * Inode is only half assembled, or there was an IO error,
		 * or the verifier failed, so don't bother trying to check.
		 * The inode scrubber can deal with this.
		 */
		goto out;
	} else {
		/* Inode is all there. */
		freemask_ok = irec_free ^ ino_inuse;
	}
	if (!freemask_ok)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
out:
	return 0;
}

/*
 * Check that the holemask and freemask of a hypothetical inode cluster match
 * what's actually on disk.  If sparse inodes are enabled, the cluster does
 * not actually have to map to inodes if the corresponding holemask bit is set.
 *
 * @cluster_base is the first inode in the cluster within the @irec.
 */
STATIC int
xchk_iallocbt_check_cluster(
	struct xchk_btree		*bs,
	struct xfs_inobt_rec_incore	*irec,
	unsigned int			cluster_base)
{
	struct xfs_imap			imap;
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xfs_buf			*cluster_bp;
	unsigned int			nr_inodes;
	xfs_agnumber_t			agno = bs->cur->bc_ag.pag->pag_agno;
	xfs_agblock_t			agbno;
	unsigned int			cluster_index;
	uint16_t			cluster_mask = 0;
	uint16_t			ir_holemask;
	int				error = 0;

	nr_inodes = min_t(unsigned int, XFS_INODES_PER_CHUNK,
			M_IGEO(mp)->inodes_per_cluster);

	/* Map this inode cluster */
	agbno = XFS_AGINO_TO_AGBNO(mp, irec->ir_startino + cluster_base);

	/* Compute a bitmask for this cluster that can be used for holemask. */
	for (cluster_index = 0;
	     cluster_index < nr_inodes;
	     cluster_index += XFS_INODES_PER_HOLEMASK_BIT)
		cluster_mask |= XFS_INOBT_MASK((cluster_base + cluster_index) /
				XFS_INODES_PER_HOLEMASK_BIT);

	/*
	 * Map the first inode of this cluster to a buffer and offset.
	 * Be careful about inobt records that don't align with the start of
	 * the inode buffer when block sizes are large enough to hold multiple
	 * inode chunks.  When this happens, cluster_base will be zero but
	 * ir_startino can be large enough to make im_boffset nonzero.
	 */
	ir_holemask = (irec->ir_holemask & cluster_mask);
	imap.im_blkno = XFS_AGB_TO_DADDR(mp, agno, agbno);
	imap.im_len = XFS_FSB_TO_BB(mp, M_IGEO(mp)->blocks_per_cluster);
	imap.im_boffset = XFS_INO_TO_OFFSET(mp, irec->ir_startino) <<
			mp->m_sb.sb_inodelog;

	if (imap.im_boffset != 0 && cluster_base != 0) {
		ASSERT(imap.im_boffset == 0 || cluster_base == 0);
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	trace_xchk_iallocbt_check_cluster(mp, agno, irec->ir_startino,
			imap.im_blkno, imap.im_len, cluster_base, nr_inodes,
			cluster_mask, ir_holemask,
			XFS_INO_TO_OFFSET(mp, irec->ir_startino +
					  cluster_base));

	/* The whole cluster must be a hole or not a hole. */
	if (ir_holemask != cluster_mask && ir_holemask != 0) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	/* If any part of this is a hole, skip it. */
	if (ir_holemask) {
		xchk_xref_is_not_owned_by(bs->sc, agbno,
				M_IGEO(mp)->blocks_per_cluster,
				&XFS_RMAP_OINFO_INODES);
		return 0;
	}

	xchk_xref_is_only_owned_by(bs->sc, agbno, M_IGEO(mp)->blocks_per_cluster,
			&XFS_RMAP_OINFO_INODES);

	/* Grab the inode cluster buffer. */
	error = xfs_imap_to_bp(mp, bs->cur->bc_tp, &imap, &cluster_bp);
	if (!xchk_btree_xref_process_error(bs->sc, bs->cur, 0, &error))
		return error;

	/* Check free status of each inode within this cluster. */
	for (cluster_index = 0; cluster_index < nr_inodes; cluster_index++) {
		struct xfs_dinode	*dip;

		if (imap.im_boffset >= BBTOB(cluster_bp->b_length)) {
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
			break;
		}

		dip = xfs_buf_offset(cluster_bp, imap.im_boffset);
		error = xchk_iallocbt_check_cluster_ifree(bs, irec,
				cluster_base + cluster_index, dip);
		if (error)
			break;
		imap.im_boffset += mp->m_sb.sb_inodesize;
	}

	xfs_trans_brelse(bs->cur->bc_tp, cluster_bp);
	return error;
}

/*
 * For all the inode clusters that could map to this inobt record, make sure
 * that the holemask makes sense and that the allocation status of each inode
 * matches the freemask.
 */
STATIC int
xchk_iallocbt_check_clusters(
	struct xchk_btree		*bs,
	struct xfs_inobt_rec_incore	*irec)
{
	unsigned int			cluster_base;
	int				error = 0;

	/*
	 * For the common case where this inobt record maps to multiple inode
	 * clusters this will call _check_cluster for each cluster.
	 *
	 * For the case that multiple inobt records map to a single cluster,
	 * this will call _check_cluster once.
	 */
	for (cluster_base = 0;
	     cluster_base < XFS_INODES_PER_CHUNK;
	     cluster_base += M_IGEO(bs->sc->mp)->inodes_per_cluster) {
		error = xchk_iallocbt_check_cluster(bs, irec, cluster_base);
		if (error)
			break;
	}

	return error;
}

/*
 * Make sure this inode btree record is aligned properly.  Because a fs block
 * contains multiple inodes, we check that the inobt record is aligned to the
 * correct inode, not just the correct block on disk.  This results in a finer
 * grained corruption check.
 */
STATIC void
xchk_iallocbt_rec_alignment(
	struct xchk_btree		*bs,
	struct xfs_inobt_rec_incore	*irec)
{
	struct xfs_mount		*mp = bs->sc->mp;
	struct xchk_iallocbt		*iabt = bs->private;
	struct xfs_ino_geometry		*igeo = M_IGEO(mp);

	/*
	 * finobt records have different positioning requirements than inobt
	 * records: each finobt record must have a corresponding inobt record.
	 * That is checked in the xref function, so for now we only catch the
	 * obvious case where the record isn't at all aligned properly.
	 *
	 * Note that if a fs block contains more than a single chunk of inodes,
	 * we will have finobt records only for those chunks containing free
	 * inodes, and therefore expect chunk alignment of finobt records.
	 * Otherwise, we expect that the finobt record is aligned to the
	 * cluster alignment as told by the superblock.
	 */
	if (bs->cur->bc_btnum == XFS_BTNUM_FINO) {
		unsigned int	imask;

		imask = min_t(unsigned int, XFS_INODES_PER_CHUNK,
				igeo->cluster_align_inodes) - 1;
		if (irec->ir_startino & imask)
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (iabt->next_startino != NULLAGINO) {
		/*
		 * We're midway through a cluster of inodes that is mapped by
		 * multiple inobt records.  Did we get the record for the next
		 * irec in the sequence?
		 */
		if (irec->ir_startino != iabt->next_startino) {
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
			return;
		}

		iabt->next_startino += XFS_INODES_PER_CHUNK;

		/* Are we done with the cluster? */
		if (iabt->next_startino >= iabt->next_cluster_ino) {
			iabt->next_startino = NULLAGINO;
			iabt->next_cluster_ino = NULLAGINO;
		}
		return;
	}

	/* inobt records must be aligned to cluster and inoalignmnt size. */
	if (irec->ir_startino & (igeo->cluster_align_inodes - 1)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (irec->ir_startino & (igeo->inodes_per_cluster - 1)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (igeo->inodes_per_cluster <= XFS_INODES_PER_CHUNK)
		return;

	/*
	 * If this is the start of an inode cluster that can be mapped by
	 * multiple inobt records, the next inobt record must follow exactly
	 * after this one.
	 */
	iabt->next_startino = irec->ir_startino + XFS_INODES_PER_CHUNK;
	iabt->next_cluster_ino = irec->ir_startino + igeo->inodes_per_cluster;
}

/* Scrub an inobt/finobt record. */
STATIC int
xchk_iallocbt_rec(
	struct xchk_btree		*bs,
	const union xfs_btree_rec	*rec)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xchk_iallocbt		*iabt = bs->private;
	struct xfs_inobt_rec_incore	irec;
	uint64_t			holes;
	xfs_agino_t			agino;
	int				holecount;
	int				i;
	int				error = 0;
	uint16_t			holemask;

	xfs_inobt_btrec_to_irec(mp, rec, &irec);
	if (xfs_inobt_check_irec(bs->cur->bc_ag.pag, &irec) != NULL) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	agino = irec.ir_startino;

	xchk_iallocbt_rec_alignment(bs, &irec);
	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	iabt->inodes += irec.ir_count;

	/* Handle non-sparse inodes */
	if (!xfs_inobt_issparse(irec.ir_holemask)) {
		if (irec.ir_count != XFS_INODES_PER_CHUNK)
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

		if (!xchk_iallocbt_chunk(bs, &irec, agino,
					XFS_INODES_PER_CHUNK))
			goto out;
		goto check_clusters;
	}

	/* Check each chunk of a sparse inode cluster. */
	holemask = irec.ir_holemask;
	holecount = 0;
	holes = ~xfs_inobt_irec_to_allocmask(&irec);
	if ((holes & irec.ir_free) != holes ||
	    irec.ir_freecount > irec.ir_count)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	for (i = 0; i < XFS_INOBT_HOLEMASK_BITS; i++) {
		if (holemask & 1)
			holecount += XFS_INODES_PER_HOLEMASK_BIT;
		else if (!xchk_iallocbt_chunk(bs, &irec, agino,
					XFS_INODES_PER_HOLEMASK_BIT))
			goto out;
		holemask >>= 1;
		agino += XFS_INODES_PER_HOLEMASK_BIT;
	}

	if (holecount > XFS_INODES_PER_CHUNK ||
	    holecount + irec.ir_count != XFS_INODES_PER_CHUNK)
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
 * Make sure the inode btrees are as large as the rmap thinks they are.
 * Don't bother if we're missing btree cursors, as we're already corrupt.
 */
STATIC void
xchk_iallocbt_xref_rmap_btreeblks(
	struct xfs_scrub	*sc,
	int			which)
{
	xfs_filblks_t		blocks;
	xfs_extlen_t		inobt_blocks = 0;
	xfs_extlen_t		finobt_blocks = 0;
	int			error;

	if (!sc->sa.ino_cur || !sc->sa.rmap_cur ||
	    (xfs_has_finobt(sc->mp) && !sc->sa.fino_cur) ||
	    xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many inobt blocks as the rmap says. */
	error = xfs_btree_count_blocks(sc->sa.ino_cur, &inobt_blocks);
	if (!xchk_process_error(sc, 0, 0, &error))
		return;

	if (sc->sa.fino_cur) {
		error = xfs_btree_count_blocks(sc->sa.fino_cur, &finobt_blocks);
		if (!xchk_process_error(sc, 0, 0, &error))
			return;
	}

	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur,
			&XFS_RMAP_OINFO_INOBT, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	if (blocks != inobt_blocks + finobt_blocks)
		xchk_btree_set_corrupt(sc, sc->sa.ino_cur, 0);
}

/*
 * Make sure that the inobt records point to the same number of blocks as
 * the rmap says are owned by inodes.
 */
STATIC void
xchk_iallocbt_xref_rmap_inodes(
	struct xfs_scrub	*sc,
	int			which,
	unsigned long long	inodes)
{
	xfs_filblks_t		blocks;
	xfs_filblks_t		inode_blocks;
	int			error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many inode blocks as the rmap knows about. */
	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur,
			&XFS_RMAP_OINFO_INODES, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	inode_blocks = XFS_B_TO_FSB(sc->mp, inodes * sc->mp->m_sb.sb_inodesize);
	if (blocks != inode_blocks)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);
}

/* Scrub one of the inode btrees for some AG. */
int
xchk_iallocbt(
	struct xfs_scrub	*sc)
{
	struct xfs_btree_cur	*cur;
	struct xchk_iallocbt	iabt = {
		.inodes		= 0,
		.next_startino	= NULLAGINO,
		.next_cluster_ino = NULLAGINO,
	};
	xfs_btnum_t		which;
	int			error;

	switch (sc->sm->sm_type) {
	case XFS_SCRUB_TYPE_INOBT:
		cur = sc->sa.ino_cur;
		which = XFS_BTNUM_INO;
		break;
	case XFS_SCRUB_TYPE_FINOBT:
		cur = sc->sa.fino_cur;
		which = XFS_BTNUM_FINO;
		break;
	default:
		ASSERT(0);
		return -EIO;
	}

	error = xchk_btree(sc, cur, xchk_iallocbt_rec, &XFS_RMAP_OINFO_INOBT,
			&iabt);
	if (error)
		return error;

	xchk_iallocbt_xref_rmap_btreeblks(sc, which);

	/*
	 * If we're scrubbing the inode btree, inode_blocks is the number of
	 * blocks pointed to by all the inode chunk records.  Therefore, we
	 * should compare to the number of inode chunk blocks that the rmap
	 * knows about.  We can't do this for the finobt since it only points
	 * to inode chunks with free inodes.
	 */
	if (which == XFS_BTNUM_INO)
		xchk_iallocbt_xref_rmap_inodes(sc, which, iabt.inodes);

	return error;
}

/* See if an inode btree has (or doesn't have) an inode chunk record. */
static inline void
xchk_xref_inode_check(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len,
	struct xfs_btree_cur	**icur,
	enum xbtree_recpacking	expected)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (!(*icur) || xchk_skip_xref(sc->sm))
		return;

	error = xfs_ialloc_has_inodes_at_extent(*icur, agbno, len, &outcome);
	if (!xchk_should_check_xref(sc, &error, icur))
		return;
	if (outcome != expected)
		xchk_btree_xref_set_corrupt(sc, *icur, 0);
}

/* xref check that the extent is not covered by inodes */
void
xchk_xref_is_not_inode_chunk(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len)
{
	xchk_xref_inode_check(sc, agbno, len, &sc->sa.ino_cur,
			XBTREE_RECPACKING_EMPTY);
	xchk_xref_inode_check(sc, agbno, len, &sc->sa.fino_cur,
			XBTREE_RECPACKING_EMPTY);
}

/* xref check that the extent is covered by inodes */
void
xchk_xref_is_inode_chunk(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len)
{
	xchk_xref_inode_check(sc, agbno, len, &sc->sa.ino_cur,
			XBTREE_RECPACKING_FULL);
}
