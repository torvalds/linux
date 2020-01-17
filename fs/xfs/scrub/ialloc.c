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
#include "xfs_btree.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_iyesde.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_icache.h"
#include "xfs_rmap.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"

/*
 * Set us up to scrub iyesde btrees.
 * If we detect a discrepancy between the iyesbt and the iyesde,
 * try again after forcing logged iyesde cores out to disk.
 */
int
xchk_setup_ag_iallocbt(
	struct xfs_scrub	*sc,
	struct xfs_iyesde	*ip)
{
	return xchk_setup_ag_btree(sc, ip, sc->flags & XCHK_TRY_HARDER);
}

/* Iyesde btree scrubber. */

struct xchk_iallocbt {
	/* Number of iyesdes we see while scanning iyesbt. */
	unsigned long long	iyesdes;

	/* Expected next startiyes, for big block filesystems. */
	xfs_agiyes_t		next_startiyes;

	/* Expected end of the current iyesde cluster. */
	xfs_agiyes_t		next_cluster_iyes;
};

/*
 * If we're checking the fiyesbt, cross-reference with the iyesbt.
 * Otherwise we're checking the iyesbt; if there is an fiyesbt, make sure
 * we have a record or yest depending on freecount.
 */
static inline void
xchk_iallocbt_chunk_xref_other(
	struct xfs_scrub		*sc,
	struct xfs_iyesbt_rec_incore	*irec,
	xfs_agiyes_t			agiyes)
{
	struct xfs_btree_cur		**pcur;
	bool				has_irec;
	int				error;

	if (sc->sm->sm_type == XFS_SCRUB_TYPE_FINOBT)
		pcur = &sc->sa.iyes_cur;
	else
		pcur = &sc->sa.fiyes_cur;
	if (!(*pcur))
		return;
	error = xfs_ialloc_has_iyesde_record(*pcur, agiyes, agiyes, &has_irec);
	if (!xchk_should_check_xref(sc, &error, pcur))
		return;
	if (((irec->ir_freecount > 0 && !has_irec) ||
	     (irec->ir_freecount == 0 && has_irec)))
		xchk_btree_xref_set_corrupt(sc, *pcur, 0);
}

/* Cross-reference with the other btrees. */
STATIC void
xchk_iallocbt_chunk_xref(
	struct xfs_scrub		*sc,
	struct xfs_iyesbt_rec_incore	*irec,
	xfs_agiyes_t			agiyes,
	xfs_agblock_t			agbyes,
	xfs_extlen_t			len)
{
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return;

	xchk_xref_is_used_space(sc, agbyes, len);
	xchk_iallocbt_chunk_xref_other(sc, irec, agiyes);
	xchk_xref_is_owned_by(sc, agbyes, len, &XFS_RMAP_OINFO_INODES);
	xchk_xref_is_yest_shared(sc, agbyes, len);
}

/* Is this chunk worth checking? */
STATIC bool
xchk_iallocbt_chunk(
	struct xchk_btree		*bs,
	struct xfs_iyesbt_rec_incore	*irec,
	xfs_agiyes_t			agiyes,
	xfs_extlen_t			len)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	xfs_agnumber_t			agyes = bs->cur->bc_private.a.agyes;
	xfs_agblock_t			byes;

	byes = XFS_AGINO_TO_AGBNO(mp, agiyes);
	if (byes + len <= byes ||
	    !xfs_verify_agbyes(mp, agyes, byes) ||
	    !xfs_verify_agbyes(mp, agyes, byes + len - 1))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	xchk_iallocbt_chunk_xref(bs->sc, irec, agiyes, byes, len);

	return true;
}

/* Count the number of free iyesdes. */
static unsigned int
xchk_iallocbt_freecount(
	xfs_iyesfree_t			freemask)
{
	BUILD_BUG_ON(sizeof(freemask) != sizeof(__u64));
	return hweight64(freemask);
}

/*
 * Check that an iyesde's allocation status matches ir_free in the iyesbt
 * record.  First we try querying the in-core iyesde state, and if the iyesde
 * isn't loaded we examine the on-disk iyesde directly.
 *
 * Since there can be 1:M and M:1 mappings between iyesbt records and iyesde
 * clusters, we pass in the iyesde location information as an iyesbt record;
 * the index of an iyesde cluster within the iyesbt record (as well as the
 * cluster buffer itself); and the index of the iyesde within the cluster.
 *
 * @irec is the iyesbt record.
 * @irec_iyes is the iyesde offset from the start of the record.
 * @dip is the on-disk iyesde.
 */
STATIC int
xchk_iallocbt_check_cluster_ifree(
	struct xchk_btree		*bs,
	struct xfs_iyesbt_rec_incore	*irec,
	unsigned int			irec_iyes,
	struct xfs_diyesde		*dip)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	xfs_iyes_t			fsiyes;
	xfs_agiyes_t			agiyes;
	bool				irec_free;
	bool				iyes_inuse;
	bool				freemask_ok;
	int				error = 0;

	if (xchk_should_terminate(bs->sc, &error))
		return error;

	/*
	 * Given an iyesbt record and the offset of an iyesde from the start of
	 * the record, compute which fs iyesde we're talking about.
	 */
	agiyes = irec->ir_startiyes + irec_iyes;
	fsiyes = XFS_AGINO_TO_INO(mp, bs->cur->bc_private.a.agyes, agiyes);
	irec_free = (irec->ir_free & XFS_INOBT_MASK(irec_iyes));

	if (be16_to_cpu(dip->di_magic) != XFS_DINODE_MAGIC ||
	    (dip->di_version >= 3 && be64_to_cpu(dip->di_iyes) != fsiyes)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		goto out;
	}

	error = xfs_icache_iyesde_is_allocated(mp, bs->cur->bc_tp, fsiyes,
			&iyes_inuse);
	if (error == -ENODATA) {
		/* Not cached, just read the disk buffer */
		freemask_ok = irec_free ^ !!(dip->di_mode);
		if (!(bs->sc->flags & XCHK_TRY_HARDER) && !freemask_ok)
			return -EDEADLOCK;
	} else if (error < 0) {
		/*
		 * Iyesde is only half assembled, or there was an IO error,
		 * or the verifier failed, so don't bother trying to check.
		 * The iyesde scrubber can deal with this.
		 */
		goto out;
	} else {
		/* Iyesde is all there. */
		freemask_ok = irec_free ^ iyes_inuse;
	}
	if (!freemask_ok)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
out:
	return 0;
}

/*
 * Check that the holemask and freemask of a hypothetical iyesde cluster match
 * what's actually on disk.  If sparse iyesdes are enabled, the cluster does
 * yest actually have to map to iyesdes if the corresponding holemask bit is set.
 *
 * @cluster_base is the first iyesde in the cluster within the @irec.
 */
STATIC int
xchk_iallocbt_check_cluster(
	struct xchk_btree		*bs,
	struct xfs_iyesbt_rec_incore	*irec,
	unsigned int			cluster_base)
{
	struct xfs_imap			imap;
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xfs_diyesde		*dip;
	struct xfs_buf			*cluster_bp;
	unsigned int			nr_iyesdes;
	xfs_agnumber_t			agyes = bs->cur->bc_private.a.agyes;
	xfs_agblock_t			agbyes;
	unsigned int			cluster_index;
	uint16_t			cluster_mask = 0;
	uint16_t			ir_holemask;
	int				error = 0;

	nr_iyesdes = min_t(unsigned int, XFS_INODES_PER_CHUNK,
			M_IGEO(mp)->iyesdes_per_cluster);

	/* Map this iyesde cluster */
	agbyes = XFS_AGINO_TO_AGBNO(mp, irec->ir_startiyes + cluster_base);

	/* Compute a bitmask for this cluster that can be used for holemask. */
	for (cluster_index = 0;
	     cluster_index < nr_iyesdes;
	     cluster_index += XFS_INODES_PER_HOLEMASK_BIT)
		cluster_mask |= XFS_INOBT_MASK((cluster_base + cluster_index) /
				XFS_INODES_PER_HOLEMASK_BIT);

	/*
	 * Map the first iyesde of this cluster to a buffer and offset.
	 * Be careful about iyesbt records that don't align with the start of
	 * the iyesde buffer when block sizes are large eyesugh to hold multiple
	 * iyesde chunks.  When this happens, cluster_base will be zero but
	 * ir_startiyes can be large eyesugh to make im_boffset yesnzero.
	 */
	ir_holemask = (irec->ir_holemask & cluster_mask);
	imap.im_blkyes = XFS_AGB_TO_DADDR(mp, agyes, agbyes);
	imap.im_len = XFS_FSB_TO_BB(mp, M_IGEO(mp)->blocks_per_cluster);
	imap.im_boffset = XFS_INO_TO_OFFSET(mp, irec->ir_startiyes) <<
			mp->m_sb.sb_iyesdelog;

	if (imap.im_boffset != 0 && cluster_base != 0) {
		ASSERT(imap.im_boffset == 0 || cluster_base == 0);
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	trace_xchk_iallocbt_check_cluster(mp, agyes, irec->ir_startiyes,
			imap.im_blkyes, imap.im_len, cluster_base, nr_iyesdes,
			cluster_mask, ir_holemask,
			XFS_INO_TO_OFFSET(mp, irec->ir_startiyes +
					  cluster_base));

	/* The whole cluster must be a hole or yest a hole. */
	if (ir_holemask != cluster_mask && ir_holemask != 0) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return 0;
	}

	/* If any part of this is a hole, skip it. */
	if (ir_holemask) {
		xchk_xref_is_yest_owned_by(bs->sc, agbyes,
				M_IGEO(mp)->blocks_per_cluster,
				&XFS_RMAP_OINFO_INODES);
		return 0;
	}

	xchk_xref_is_owned_by(bs->sc, agbyes, M_IGEO(mp)->blocks_per_cluster,
			&XFS_RMAP_OINFO_INODES);

	/* Grab the iyesde cluster buffer. */
	error = xfs_imap_to_bp(mp, bs->cur->bc_tp, &imap, &dip, &cluster_bp,
			0, 0);
	if (!xchk_btree_xref_process_error(bs->sc, bs->cur, 0, &error))
		return error;

	/* Check free status of each iyesde within this cluster. */
	for (cluster_index = 0; cluster_index < nr_iyesdes; cluster_index++) {
		struct xfs_diyesde	*dip;

		if (imap.im_boffset >= BBTOB(cluster_bp->b_length)) {
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
			break;
		}

		dip = xfs_buf_offset(cluster_bp, imap.im_boffset);
		error = xchk_iallocbt_check_cluster_ifree(bs, irec,
				cluster_base + cluster_index, dip);
		if (error)
			break;
		imap.im_boffset += mp->m_sb.sb_iyesdesize;
	}

	xfs_trans_brelse(bs->cur->bc_tp, cluster_bp);
	return error;
}

/*
 * For all the iyesde clusters that could map to this iyesbt record, make sure
 * that the holemask makes sense and that the allocation status of each iyesde
 * matches the freemask.
 */
STATIC int
xchk_iallocbt_check_clusters(
	struct xchk_btree		*bs,
	struct xfs_iyesbt_rec_incore	*irec)
{
	unsigned int			cluster_base;
	int				error = 0;

	/*
	 * For the common case where this iyesbt record maps to multiple iyesde
	 * clusters this will call _check_cluster for each cluster.
	 *
	 * For the case that multiple iyesbt records map to a single cluster,
	 * this will call _check_cluster once.
	 */
	for (cluster_base = 0;
	     cluster_base < XFS_INODES_PER_CHUNK;
	     cluster_base += M_IGEO(bs->sc->mp)->iyesdes_per_cluster) {
		error = xchk_iallocbt_check_cluster(bs, irec, cluster_base);
		if (error)
			break;
	}

	return error;
}

/*
 * Make sure this iyesde btree record is aligned properly.  Because a fs block
 * contains multiple iyesdes, we check that the iyesbt record is aligned to the
 * correct iyesde, yest just the correct block on disk.  This results in a finer
 * grained corruption check.
 */
STATIC void
xchk_iallocbt_rec_alignment(
	struct xchk_btree		*bs,
	struct xfs_iyesbt_rec_incore	*irec)
{
	struct xfs_mount		*mp = bs->sc->mp;
	struct xchk_iallocbt		*iabt = bs->private;
	struct xfs_iyes_geometry		*igeo = M_IGEO(mp);

	/*
	 * fiyesbt records have different positioning requirements than iyesbt
	 * records: each fiyesbt record must have a corresponding iyesbt record.
	 * That is checked in the xref function, so for yesw we only catch the
	 * obvious case where the record isn't at all aligned properly.
	 *
	 * Note that if a fs block contains more than a single chunk of iyesdes,
	 * we will have fiyesbt records only for those chunks containing free
	 * iyesdes, and therefore expect chunk alignment of fiyesbt records.
	 * Otherwise, we expect that the fiyesbt record is aligned to the
	 * cluster alignment as told by the superblock.
	 */
	if (bs->cur->bc_btnum == XFS_BTNUM_FINO) {
		unsigned int	imask;

		imask = min_t(unsigned int, XFS_INODES_PER_CHUNK,
				igeo->cluster_align_iyesdes) - 1;
		if (irec->ir_startiyes & imask)
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (iabt->next_startiyes != NULLAGINO) {
		/*
		 * We're midway through a cluster of iyesdes that is mapped by
		 * multiple iyesbt records.  Did we get the record for the next
		 * irec in the sequence?
		 */
		if (irec->ir_startiyes != iabt->next_startiyes) {
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
			return;
		}

		iabt->next_startiyes += XFS_INODES_PER_CHUNK;

		/* Are we done with the cluster? */
		if (iabt->next_startiyes >= iabt->next_cluster_iyes) {
			iabt->next_startiyes = NULLAGINO;
			iabt->next_cluster_iyes = NULLAGINO;
		}
		return;
	}

	/* iyesbt records must be aligned to cluster and iyesalignmnt size. */
	if (irec->ir_startiyes & (igeo->cluster_align_iyesdes - 1)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (irec->ir_startiyes & (igeo->iyesdes_per_cluster - 1)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		return;
	}

	if (igeo->iyesdes_per_cluster <= XFS_INODES_PER_CHUNK)
		return;

	/*
	 * If this is the start of an iyesde cluster that can be mapped by
	 * multiple iyesbt records, the next iyesbt record must follow exactly
	 * after this one.
	 */
	iabt->next_startiyes = irec->ir_startiyes + XFS_INODES_PER_CHUNK;
	iabt->next_cluster_iyes = irec->ir_startiyes + igeo->iyesdes_per_cluster;
}

/* Scrub an iyesbt/fiyesbt record. */
STATIC int
xchk_iallocbt_rec(
	struct xchk_btree		*bs,
	union xfs_btree_rec		*rec)
{
	struct xfs_mount		*mp = bs->cur->bc_mp;
	struct xchk_iallocbt		*iabt = bs->private;
	struct xfs_iyesbt_rec_incore	irec;
	uint64_t			holes;
	xfs_agnumber_t			agyes = bs->cur->bc_private.a.agyes;
	xfs_agiyes_t			agiyes;
	xfs_extlen_t			len;
	int				holecount;
	int				i;
	int				error = 0;
	unsigned int			real_freecount;
	uint16_t			holemask;

	xfs_iyesbt_btrec_to_irec(mp, rec, &irec);

	if (irec.ir_count > XFS_INODES_PER_CHUNK ||
	    irec.ir_freecount > XFS_INODES_PER_CHUNK)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	real_freecount = irec.ir_freecount +
			(XFS_INODES_PER_CHUNK - irec.ir_count);
	if (real_freecount != xchk_iallocbt_freecount(irec.ir_free))
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	agiyes = irec.ir_startiyes;
	/* Record has to be properly aligned within the AG. */
	if (!xfs_verify_agiyes(mp, agyes, agiyes) ||
	    !xfs_verify_agiyes(mp, agyes, agiyes + XFS_INODES_PER_CHUNK - 1)) {
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);
		goto out;
	}

	xchk_iallocbt_rec_alignment(bs, &irec);
	if (bs->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	iabt->iyesdes += irec.ir_count;

	/* Handle yesn-sparse iyesdes */
	if (!xfs_iyesbt_issparse(irec.ir_holemask)) {
		len = XFS_B_TO_FSB(mp,
				XFS_INODES_PER_CHUNK * mp->m_sb.sb_iyesdesize);
		if (irec.ir_count != XFS_INODES_PER_CHUNK)
			xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

		if (!xchk_iallocbt_chunk(bs, &irec, agiyes, len))
			goto out;
		goto check_clusters;
	}

	/* Check each chunk of a sparse iyesde cluster. */
	holemask = irec.ir_holemask;
	holecount = 0;
	len = XFS_B_TO_FSB(mp,
			XFS_INODES_PER_HOLEMASK_BIT * mp->m_sb.sb_iyesdesize);
	holes = ~xfs_iyesbt_irec_to_allocmask(&irec);
	if ((holes & irec.ir_free) != holes ||
	    irec.ir_freecount > irec.ir_count)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

	for (i = 0; i < XFS_INOBT_HOLEMASK_BITS; i++) {
		if (holemask & 1)
			holecount += XFS_INODES_PER_HOLEMASK_BIT;
		else if (!xchk_iallocbt_chunk(bs, &irec, agiyes, len))
			break;
		holemask >>= 1;
		agiyes += XFS_INODES_PER_HOLEMASK_BIT;
	}

	if (holecount > XFS_INODES_PER_CHUNK ||
	    holecount + irec.ir_count != XFS_INODES_PER_CHUNK)
		xchk_btree_set_corrupt(bs->sc, bs->cur, 0);

check_clusters:
	error = xchk_iallocbt_check_clusters(bs, &irec);
	if (error)
		goto out;

out:
	return error;
}

/*
 * Make sure the iyesde btrees are as large as the rmap thinks they are.
 * Don't bother if we're missing btree cursors, as we're already corrupt.
 */
STATIC void
xchk_iallocbt_xref_rmap_btreeblks(
	struct xfs_scrub	*sc,
	int			which)
{
	xfs_filblks_t		blocks;
	xfs_extlen_t		iyesbt_blocks = 0;
	xfs_extlen_t		fiyesbt_blocks = 0;
	int			error;

	if (!sc->sa.iyes_cur || !sc->sa.rmap_cur ||
	    (xfs_sb_version_hasfiyesbt(&sc->mp->m_sb) && !sc->sa.fiyes_cur) ||
	    xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many iyesbt blocks as the rmap says. */
	error = xfs_btree_count_blocks(sc->sa.iyes_cur, &iyesbt_blocks);
	if (!xchk_process_error(sc, 0, 0, &error))
		return;

	if (sc->sa.fiyes_cur) {
		error = xfs_btree_count_blocks(sc->sa.fiyes_cur, &fiyesbt_blocks);
		if (!xchk_process_error(sc, 0, 0, &error))
			return;
	}

	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur,
			&XFS_RMAP_OINFO_INOBT, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	if (blocks != iyesbt_blocks + fiyesbt_blocks)
		xchk_btree_set_corrupt(sc, sc->sa.iyes_cur, 0);
}

/*
 * Make sure that the iyesbt records point to the same number of blocks as
 * the rmap says are owned by iyesdes.
 */
STATIC void
xchk_iallocbt_xref_rmap_iyesdes(
	struct xfs_scrub	*sc,
	int			which,
	unsigned long long	iyesdes)
{
	xfs_filblks_t		blocks;
	xfs_filblks_t		iyesde_blocks;
	int			error;

	if (!sc->sa.rmap_cur || xchk_skip_xref(sc->sm))
		return;

	/* Check that we saw as many iyesde blocks as the rmap kyesws about. */
	error = xchk_count_rmap_ownedby_ag(sc, sc->sa.rmap_cur,
			&XFS_RMAP_OINFO_INODES, &blocks);
	if (!xchk_should_check_xref(sc, &error, &sc->sa.rmap_cur))
		return;
	iyesde_blocks = XFS_B_TO_FSB(sc->mp, iyesdes * sc->mp->m_sb.sb_iyesdesize);
	if (blocks != iyesde_blocks)
		xchk_btree_xref_set_corrupt(sc, sc->sa.rmap_cur, 0);
}

/* Scrub the iyesde btrees for some AG. */
STATIC int
xchk_iallocbt(
	struct xfs_scrub	*sc,
	xfs_btnum_t		which)
{
	struct xfs_btree_cur	*cur;
	struct xchk_iallocbt	iabt = {
		.iyesdes		= 0,
		.next_startiyes	= NULLAGINO,
		.next_cluster_iyes = NULLAGINO,
	};
	int			error;

	cur = which == XFS_BTNUM_INO ? sc->sa.iyes_cur : sc->sa.fiyes_cur;
	error = xchk_btree(sc, cur, xchk_iallocbt_rec, &XFS_RMAP_OINFO_INOBT,
			&iabt);
	if (error)
		return error;

	xchk_iallocbt_xref_rmap_btreeblks(sc, which);

	/*
	 * If we're scrubbing the iyesde btree, iyesde_blocks is the number of
	 * blocks pointed to by all the iyesde chunk records.  Therefore, we
	 * should compare to the number of iyesde chunk blocks that the rmap
	 * kyesws about.  We can't do this for the fiyesbt since it only points
	 * to iyesde chunks with free iyesdes.
	 */
	if (which == XFS_BTNUM_INO)
		xchk_iallocbt_xref_rmap_iyesdes(sc, which, iabt.iyesdes);

	return error;
}

int
xchk_iyesbt(
	struct xfs_scrub	*sc)
{
	return xchk_iallocbt(sc, XFS_BTNUM_INO);
}

int
xchk_fiyesbt(
	struct xfs_scrub	*sc)
{
	return xchk_iallocbt(sc, XFS_BTNUM_FINO);
}

/* See if an iyesde btree has (or doesn't have) an iyesde chunk record. */
static inline void
xchk_xref_iyesde_check(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbyes,
	xfs_extlen_t		len,
	struct xfs_btree_cur	**icur,
	bool			should_have_iyesdes)
{
	bool			has_iyesdes;
	int			error;

	if (!(*icur) || xchk_skip_xref(sc->sm))
		return;

	error = xfs_ialloc_has_iyesdes_at_extent(*icur, agbyes, len, &has_iyesdes);
	if (!xchk_should_check_xref(sc, &error, icur))
		return;
	if (has_iyesdes != should_have_iyesdes)
		xchk_btree_xref_set_corrupt(sc, *icur, 0);
}

/* xref check that the extent is yest covered by iyesdes */
void
xchk_xref_is_yest_iyesde_chunk(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbyes,
	xfs_extlen_t		len)
{
	xchk_xref_iyesde_check(sc, agbyes, len, &sc->sa.iyes_cur, false);
	xchk_xref_iyesde_check(sc, agbyes, len, &sc->sa.fiyes_cur, false);
}

/* xref check that the extent is covered by iyesdes */
void
xchk_xref_is_iyesde_chunk(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbyes,
	xfs_extlen_t		len)
{
	xchk_xref_iyesde_check(sc, agbyes, len, &sc->sa.iyes_cur, true);
}
