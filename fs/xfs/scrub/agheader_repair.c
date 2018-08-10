// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount.h"
#include "xfs_refcount_btree.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"

/* Superblock */

/* Repair the superblock. */
int
xrep_superblock(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_buf		*bp;
	xfs_agnumber_t		agno;
	int			error;

	/* Don't try to repair AG 0's sb; let xfs_repair deal with it. */
	agno = sc->sm->sm_agno;
	if (agno == 0)
		return -EOPNOTSUPP;

	error = xfs_sb_get_secondary(mp, sc->tp, agno, &bp);
	if (error)
		return error;

	/* Copy AG 0's superblock to this one. */
	xfs_buf_zero(bp, 0, BBTOB(bp->b_length));
	xfs_sb_to_disk(XFS_BUF_TO_SBP(bp), &mp->m_sb);

	/* Write this to disk. */
	xfs_trans_buf_set_type(sc->tp, bp, XFS_BLFT_SB_BUF);
	xfs_trans_log_buf(sc->tp, bp, 0, BBTOB(bp->b_length) - 1);
	return error;
}

/* AGF */

struct xrep_agf_allocbt {
	struct xfs_scrub	*sc;
	xfs_agblock_t		freeblks;
	xfs_agblock_t		longest;
};

/* Record free space shape information. */
STATIC int
xrep_agf_walk_allocbt(
	struct xfs_btree_cur		*cur,
	struct xfs_alloc_rec_incore	*rec,
	void				*priv)
{
	struct xrep_agf_allocbt		*raa = priv;
	int				error = 0;

	if (xchk_should_terminate(raa->sc, &error))
		return error;

	raa->freeblks += rec->ar_blockcount;
	if (rec->ar_blockcount > raa->longest)
		raa->longest = rec->ar_blockcount;
	return error;
}

/* Does this AGFL block look sane? */
STATIC int
xrep_agf_check_agfl_block(
	struct xfs_mount	*mp,
	xfs_agblock_t		agbno,
	void			*priv)
{
	struct xfs_scrub	*sc = priv;

	if (!xfs_verify_agbno(mp, sc->sa.agno, agbno))
		return -EFSCORRUPTED;
	return 0;
}

/*
 * Offset within the xrep_find_ag_btree array for each btree type.  Avoid the
 * XFS_BTNUM_ names here to avoid creating a sparse array.
 */
enum {
	XREP_AGF_BNOBT = 0,
	XREP_AGF_CNTBT,
	XREP_AGF_RMAPBT,
	XREP_AGF_REFCOUNTBT,
	XREP_AGF_END,
	XREP_AGF_MAX
};

/* Check a btree root candidate. */
static inline bool
xrep_check_btree_root(
	struct xfs_scrub		*sc,
	struct xrep_find_ag_btree	*fab)
{
	struct xfs_mount		*mp = sc->mp;
	xfs_agnumber_t			agno = sc->sm->sm_agno;

	return xfs_verify_agbno(mp, agno, fab->root) &&
	       fab->height <= XFS_BTREE_MAXLEVELS;
}

/*
 * Given the btree roots described by *fab, find the roots, check them for
 * sanity, and pass the root data back out via *fab.
 *
 * This is /also/ a chicken and egg problem because we have to use the rmapbt
 * (rooted in the AGF) to find the btrees rooted in the AGF.  We also have no
 * idea if the btrees make any sense.  If we hit obvious corruptions in those
 * btrees we'll bail out.
 */
STATIC int
xrep_agf_find_btrees(
	struct xfs_scrub		*sc,
	struct xfs_buf			*agf_bp,
	struct xrep_find_ag_btree	*fab,
	struct xfs_buf			*agfl_bp)
{
	struct xfs_agf			*old_agf = XFS_BUF_TO_AGF(agf_bp);
	int				error;

	/* Go find the root data. */
	error = xrep_find_ag_btree_roots(sc, agf_bp, fab, agfl_bp);
	if (error)
		return error;

	/* We must find the bnobt, cntbt, and rmapbt roots. */
	if (!xrep_check_btree_root(sc, &fab[XREP_AGF_BNOBT]) ||
	    !xrep_check_btree_root(sc, &fab[XREP_AGF_CNTBT]) ||
	    !xrep_check_btree_root(sc, &fab[XREP_AGF_RMAPBT]))
		return -EFSCORRUPTED;

	/*
	 * We relied on the rmapbt to reconstruct the AGF.  If we get a
	 * different root then something's seriously wrong.
	 */
	if (fab[XREP_AGF_RMAPBT].root !=
	    be32_to_cpu(old_agf->agf_roots[XFS_BTNUM_RMAPi]))
		return -EFSCORRUPTED;

	/* We must find the refcountbt root if that feature is enabled. */
	if (xfs_sb_version_hasreflink(&sc->mp->m_sb) &&
	    !xrep_check_btree_root(sc, &fab[XREP_AGF_REFCOUNTBT]))
		return -EFSCORRUPTED;

	return 0;
}

/*
 * Reinitialize the AGF header, making an in-core copy of the old contents so
 * that we know which in-core state needs to be reinitialized.
 */
STATIC void
xrep_agf_init_header(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agf_bp,
	struct xfs_agf		*old_agf)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agf_bp);

	memcpy(old_agf, agf, sizeof(*old_agf));
	memset(agf, 0, BBTOB(agf_bp->b_length));
	agf->agf_magicnum = cpu_to_be32(XFS_AGF_MAGIC);
	agf->agf_versionnum = cpu_to_be32(XFS_AGF_VERSION);
	agf->agf_seqno = cpu_to_be32(sc->sa.agno);
	agf->agf_length = cpu_to_be32(xfs_ag_block_count(mp, sc->sa.agno));
	agf->agf_flfirst = old_agf->agf_flfirst;
	agf->agf_fllast = old_agf->agf_fllast;
	agf->agf_flcount = old_agf->agf_flcount;
	if (xfs_sb_version_hascrc(&mp->m_sb))
		uuid_copy(&agf->agf_uuid, &mp->m_sb.sb_meta_uuid);

	/* Mark the incore AGF data stale until we're done fixing things. */
	ASSERT(sc->sa.pag->pagf_init);
	sc->sa.pag->pagf_init = 0;
}

/* Set btree root information in an AGF. */
STATIC void
xrep_agf_set_roots(
	struct xfs_scrub		*sc,
	struct xfs_agf			*agf,
	struct xrep_find_ag_btree	*fab)
{
	agf->agf_roots[XFS_BTNUM_BNOi] =
			cpu_to_be32(fab[XREP_AGF_BNOBT].root);
	agf->agf_levels[XFS_BTNUM_BNOi] =
			cpu_to_be32(fab[XREP_AGF_BNOBT].height);

	agf->agf_roots[XFS_BTNUM_CNTi] =
			cpu_to_be32(fab[XREP_AGF_CNTBT].root);
	agf->agf_levels[XFS_BTNUM_CNTi] =
			cpu_to_be32(fab[XREP_AGF_CNTBT].height);

	agf->agf_roots[XFS_BTNUM_RMAPi] =
			cpu_to_be32(fab[XREP_AGF_RMAPBT].root);
	agf->agf_levels[XFS_BTNUM_RMAPi] =
			cpu_to_be32(fab[XREP_AGF_RMAPBT].height);

	if (xfs_sb_version_hasreflink(&sc->mp->m_sb)) {
		agf->agf_refcount_root =
				cpu_to_be32(fab[XREP_AGF_REFCOUNTBT].root);
		agf->agf_refcount_level =
				cpu_to_be32(fab[XREP_AGF_REFCOUNTBT].height);
	}
}

/* Update all AGF fields which derive from btree contents. */
STATIC int
xrep_agf_calc_from_btrees(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agf_bp)
{
	struct xrep_agf_allocbt	raa = { .sc = sc };
	struct xfs_btree_cur	*cur = NULL;
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agf_bp);
	struct xfs_mount	*mp = sc->mp;
	xfs_agblock_t		btreeblks;
	xfs_agblock_t		blocks;
	int			error;

	/* Update the AGF counters from the bnobt. */
	cur = xfs_allocbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.agno,
			XFS_BTNUM_BNO);
	error = xfs_alloc_query_all(cur, xrep_agf_walk_allocbt, &raa);
	if (error)
		goto err;
	error = xfs_btree_count_blocks(cur, &blocks);
	if (error)
		goto err;
	xfs_btree_del_cursor(cur, error);
	btreeblks = blocks - 1;
	agf->agf_freeblks = cpu_to_be32(raa.freeblks);
	agf->agf_longest = cpu_to_be32(raa.longest);

	/* Update the AGF counters from the cntbt. */
	cur = xfs_allocbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.agno,
			XFS_BTNUM_CNT);
	error = xfs_btree_count_blocks(cur, &blocks);
	if (error)
		goto err;
	xfs_btree_del_cursor(cur, error);
	btreeblks += blocks - 1;

	/* Update the AGF counters from the rmapbt. */
	cur = xfs_rmapbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.agno);
	error = xfs_btree_count_blocks(cur, &blocks);
	if (error)
		goto err;
	xfs_btree_del_cursor(cur, error);
	agf->agf_rmap_blocks = cpu_to_be32(blocks);
	btreeblks += blocks - 1;

	agf->agf_btreeblks = cpu_to_be32(btreeblks);

	/* Update the AGF counters from the refcountbt. */
	if (xfs_sb_version_hasreflink(&mp->m_sb)) {
		cur = xfs_refcountbt_init_cursor(mp, sc->tp, agf_bp,
				sc->sa.agno);
		error = xfs_btree_count_blocks(cur, &blocks);
		if (error)
			goto err;
		xfs_btree_del_cursor(cur, error);
		agf->agf_refcount_blocks = cpu_to_be32(blocks);
	}

	return 0;
err:
	xfs_btree_del_cursor(cur, error);
	return error;
}

/* Commit the new AGF and reinitialize the incore state. */
STATIC int
xrep_agf_commit_new(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agf_bp)
{
	struct xfs_perag	*pag;
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(agf_bp);

	/* Trigger fdblocks recalculation */
	xfs_force_summary_recalc(sc->mp);

	/* Write this to disk. */
	xfs_trans_buf_set_type(sc->tp, agf_bp, XFS_BLFT_AGF_BUF);
	xfs_trans_log_buf(sc->tp, agf_bp, 0, BBTOB(agf_bp->b_length) - 1);

	/* Now reinitialize the in-core counters we changed. */
	pag = sc->sa.pag;
	pag->pagf_btreeblks = be32_to_cpu(agf->agf_btreeblks);
	pag->pagf_freeblks = be32_to_cpu(agf->agf_freeblks);
	pag->pagf_longest = be32_to_cpu(agf->agf_longest);
	pag->pagf_levels[XFS_BTNUM_BNOi] =
			be32_to_cpu(agf->agf_levels[XFS_BTNUM_BNOi]);
	pag->pagf_levels[XFS_BTNUM_CNTi] =
			be32_to_cpu(agf->agf_levels[XFS_BTNUM_CNTi]);
	pag->pagf_levels[XFS_BTNUM_RMAPi] =
			be32_to_cpu(agf->agf_levels[XFS_BTNUM_RMAPi]);
	pag->pagf_refcount_level = be32_to_cpu(agf->agf_refcount_level);
	pag->pagf_init = 1;

	return 0;
}

/* Repair the AGF. v5 filesystems only. */
int
xrep_agf(
	struct xfs_scrub		*sc)
{
	struct xrep_find_ag_btree	fab[XREP_AGF_MAX] = {
		[XREP_AGF_BNOBT] = {
			.rmap_owner = XFS_RMAP_OWN_AG,
			.buf_ops = &xfs_allocbt_buf_ops,
			.magic = XFS_ABTB_CRC_MAGIC,
		},
		[XREP_AGF_CNTBT] = {
			.rmap_owner = XFS_RMAP_OWN_AG,
			.buf_ops = &xfs_allocbt_buf_ops,
			.magic = XFS_ABTC_CRC_MAGIC,
		},
		[XREP_AGF_RMAPBT] = {
			.rmap_owner = XFS_RMAP_OWN_AG,
			.buf_ops = &xfs_rmapbt_buf_ops,
			.magic = XFS_RMAP_CRC_MAGIC,
		},
		[XREP_AGF_REFCOUNTBT] = {
			.rmap_owner = XFS_RMAP_OWN_REFC,
			.buf_ops = &xfs_refcountbt_buf_ops,
			.magic = XFS_REFC_CRC_MAGIC,
		},
		[XREP_AGF_END] = {
			.buf_ops = NULL,
		},
	};
	struct xfs_agf			old_agf;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_buf			*agf_bp;
	struct xfs_buf			*agfl_bp;
	struct xfs_agf			*agf;
	int				error;

	/* We require the rmapbt to rebuild anything. */
	if (!xfs_sb_version_hasrmapbt(&mp->m_sb))
		return -EOPNOTSUPP;

	xchk_perag_get(sc->mp, &sc->sa);
	/*
	 * Make sure we have the AGF buffer, as scrub might have decided it
	 * was corrupt after xfs_alloc_read_agf failed with -EFSCORRUPTED.
	 */
	error = xfs_trans_read_buf(mp, sc->tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, sc->sa.agno, XFS_AGF_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, &agf_bp, NULL);
	if (error)
		return error;
	agf_bp->b_ops = &xfs_agf_buf_ops;
	agf = XFS_BUF_TO_AGF(agf_bp);

	/*
	 * Load the AGFL so that we can screen out OWN_AG blocks that are on
	 * the AGFL now; these blocks might have once been part of the
	 * bno/cnt/rmap btrees but are not now.  This is a chicken and egg
	 * problem: the AGF is corrupt, so we have to trust the AGFL contents
	 * because we can't do any serious cross-referencing with any of the
	 * btrees rooted in the AGF.  If the AGFL contents are obviously bad
	 * then we'll bail out.
	 */
	error = xfs_alloc_read_agfl(mp, sc->tp, sc->sa.agno, &agfl_bp);
	if (error)
		return error;

	/*
	 * Spot-check the AGFL blocks; if they're obviously corrupt then
	 * there's nothing we can do but bail out.
	 */
	error = xfs_agfl_walk(sc->mp, XFS_BUF_TO_AGF(agf_bp), agfl_bp,
			xrep_agf_check_agfl_block, sc);
	if (error)
		return error;

	/*
	 * Find the AGF btree roots.  This is also a chicken-and-egg situation;
	 * see the function for more details.
	 */
	error = xrep_agf_find_btrees(sc, agf_bp, fab, agfl_bp);
	if (error)
		return error;

	/* Start rewriting the header and implant the btrees we found. */
	xrep_agf_init_header(sc, agf_bp, &old_agf);
	xrep_agf_set_roots(sc, agf, fab);
	error = xrep_agf_calc_from_btrees(sc, agf_bp);
	if (error)
		goto out_revert;

	/* Commit the changes and reinitialize incore state. */
	return xrep_agf_commit_new(sc, agf_bp);

out_revert:
	/* Mark the incore AGF state stale and revert the AGF. */
	sc->sa.pag->pagf_init = 0;
	memcpy(agf, &old_agf, sizeof(old_agf));
	return error;
}
