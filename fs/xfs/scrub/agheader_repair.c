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
#include "xfs_btree.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount_btree.h"
#include "xfs_ag.h"
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
	xfs_sb_to_disk(bp->b_addr, &mp->m_sb);

	/*
	 * Don't write out a secondary super with NEEDSREPAIR or log incompat
	 * features set, since both are ignored when set on a secondary.
	 */
	if (xfs_has_crc(mp)) {
		struct xfs_dsb		*sb = bp->b_addr;

		sb->sb_features_incompat &=
				~cpu_to_be32(XFS_SB_FEAT_INCOMPAT_NEEDSREPAIR);
		sb->sb_features_log_incompat = 0;
	}

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
	const struct xfs_alloc_rec_incore *rec,
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

	if (!xfs_verify_agbno(sc->sa.pag, agbno))
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
	return xfs_verify_agbno(sc->sa.pag, fab->root) &&
	       fab->height <= fab->maxlevels;
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
	struct xfs_agf			*old_agf = agf_bp->b_addr;
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
	if (xfs_has_reflink(sc->mp) &&
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
	struct xfs_agf		*agf = agf_bp->b_addr;

	memcpy(old_agf, agf, sizeof(*old_agf));
	memset(agf, 0, BBTOB(agf_bp->b_length));
	agf->agf_magicnum = cpu_to_be32(XFS_AGF_MAGIC);
	agf->agf_versionnum = cpu_to_be32(XFS_AGF_VERSION);
	agf->agf_seqno = cpu_to_be32(sc->sa.pag->pag_agno);
	agf->agf_length = cpu_to_be32(sc->sa.pag->block_count);
	agf->agf_flfirst = old_agf->agf_flfirst;
	agf->agf_fllast = old_agf->agf_fllast;
	agf->agf_flcount = old_agf->agf_flcount;
	if (xfs_has_crc(mp))
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

	if (xfs_has_reflink(sc->mp)) {
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
	struct xfs_agf		*agf = agf_bp->b_addr;
	struct xfs_mount	*mp = sc->mp;
	xfs_agblock_t		btreeblks;
	xfs_agblock_t		blocks;
	int			error;

	/* Update the AGF counters from the bnobt. */
	cur = xfs_allocbt_init_cursor(mp, sc->tp, agf_bp,
			sc->sa.pag, XFS_BTNUM_BNO);
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
	cur = xfs_allocbt_init_cursor(mp, sc->tp, agf_bp,
			sc->sa.pag, XFS_BTNUM_CNT);
	error = xfs_btree_count_blocks(cur, &blocks);
	if (error)
		goto err;
	xfs_btree_del_cursor(cur, error);
	btreeblks += blocks - 1;

	/* Update the AGF counters from the rmapbt. */
	cur = xfs_rmapbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.pag);
	error = xfs_btree_count_blocks(cur, &blocks);
	if (error)
		goto err;
	xfs_btree_del_cursor(cur, error);
	agf->agf_rmap_blocks = cpu_to_be32(blocks);
	btreeblks += blocks - 1;

	agf->agf_btreeblks = cpu_to_be32(btreeblks);

	/* Update the AGF counters from the refcountbt. */
	if (xfs_has_reflink(mp)) {
		cur = xfs_refcountbt_init_cursor(mp, sc->tp, agf_bp,
				sc->sa.pag);
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
	struct xfs_agf		*agf = agf_bp->b_addr;

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
			.buf_ops = &xfs_bnobt_buf_ops,
			.maxlevels = sc->mp->m_alloc_maxlevels,
		},
		[XREP_AGF_CNTBT] = {
			.rmap_owner = XFS_RMAP_OWN_AG,
			.buf_ops = &xfs_cntbt_buf_ops,
			.maxlevels = sc->mp->m_alloc_maxlevels,
		},
		[XREP_AGF_RMAPBT] = {
			.rmap_owner = XFS_RMAP_OWN_AG,
			.buf_ops = &xfs_rmapbt_buf_ops,
			.maxlevels = sc->mp->m_rmap_maxlevels,
		},
		[XREP_AGF_REFCOUNTBT] = {
			.rmap_owner = XFS_RMAP_OWN_REFC,
			.buf_ops = &xfs_refcountbt_buf_ops,
			.maxlevels = sc->mp->m_refc_maxlevels,
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
	if (!xfs_has_rmapbt(mp))
		return -EOPNOTSUPP;

	/*
	 * Make sure we have the AGF buffer, as scrub might have decided it
	 * was corrupt after xfs_alloc_read_agf failed with -EFSCORRUPTED.
	 */
	error = xfs_trans_read_buf(mp, sc->tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, sc->sa.pag->pag_agno,
						XFS_AGF_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, &agf_bp, NULL);
	if (error)
		return error;
	agf_bp->b_ops = &xfs_agf_buf_ops;
	agf = agf_bp->b_addr;

	/*
	 * Load the AGFL so that we can screen out OWN_AG blocks that are on
	 * the AGFL now; these blocks might have once been part of the
	 * bno/cnt/rmap btrees but are not now.  This is a chicken and egg
	 * problem: the AGF is corrupt, so we have to trust the AGFL contents
	 * because we can't do any serious cross-referencing with any of the
	 * btrees rooted in the AGF.  If the AGFL contents are obviously bad
	 * then we'll bail out.
	 */
	error = xfs_alloc_read_agfl(sc->sa.pag, sc->tp, &agfl_bp);
	if (error)
		return error;

	/*
	 * Spot-check the AGFL blocks; if they're obviously corrupt then
	 * there's nothing we can do but bail out.
	 */
	error = xfs_agfl_walk(sc->mp, agf_bp->b_addr, agfl_bp,
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

/* AGFL */

struct xrep_agfl {
	/* Bitmap of other OWN_AG metadata blocks. */
	struct xbitmap		agmetablocks;

	/* Bitmap of free space. */
	struct xbitmap		*freesp;

	struct xfs_scrub	*sc;
};

/* Record all OWN_AG (free space btree) information from the rmap data. */
STATIC int
xrep_agfl_walk_rmap(
	struct xfs_btree_cur	*cur,
	const struct xfs_rmap_irec *rec,
	void			*priv)
{
	struct xrep_agfl	*ra = priv;
	xfs_fsblock_t		fsb;
	int			error = 0;

	if (xchk_should_terminate(ra->sc, &error))
		return error;

	/* Record all the OWN_AG blocks. */
	if (rec->rm_owner == XFS_RMAP_OWN_AG) {
		fsb = XFS_AGB_TO_FSB(cur->bc_mp, cur->bc_ag.pag->pag_agno,
				rec->rm_startblock);
		error = xbitmap_set(ra->freesp, fsb, rec->rm_blockcount);
		if (error)
			return error;
	}

	return xbitmap_set_btcur_path(&ra->agmetablocks, cur);
}

/*
 * Map out all the non-AGFL OWN_AG space in this AG so that we can deduce
 * which blocks belong to the AGFL.
 *
 * Compute the set of old AGFL blocks by subtracting from the list of OWN_AG
 * blocks the list of blocks owned by all other OWN_AG metadata (bnobt, cntbt,
 * rmapbt).  These are the old AGFL blocks, so return that list and the number
 * of blocks we're actually going to put back on the AGFL.
 */
STATIC int
xrep_agfl_collect_blocks(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agf_bp,
	struct xbitmap		*agfl_extents,
	xfs_agblock_t		*flcount)
{
	struct xrep_agfl	ra;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_btree_cur	*cur;
	int			error;

	ra.sc = sc;
	ra.freesp = agfl_extents;
	xbitmap_init(&ra.agmetablocks);

	/* Find all space used by the free space btrees & rmapbt. */
	cur = xfs_rmapbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.pag);
	error = xfs_rmap_query_all(cur, xrep_agfl_walk_rmap, &ra);
	if (error)
		goto err;
	xfs_btree_del_cursor(cur, error);

	/* Find all blocks currently being used by the bnobt. */
	cur = xfs_allocbt_init_cursor(mp, sc->tp, agf_bp,
			sc->sa.pag, XFS_BTNUM_BNO);
	error = xbitmap_set_btblocks(&ra.agmetablocks, cur);
	if (error)
		goto err;
	xfs_btree_del_cursor(cur, error);

	/* Find all blocks currently being used by the cntbt. */
	cur = xfs_allocbt_init_cursor(mp, sc->tp, agf_bp,
			sc->sa.pag, XFS_BTNUM_CNT);
	error = xbitmap_set_btblocks(&ra.agmetablocks, cur);
	if (error)
		goto err;

	xfs_btree_del_cursor(cur, error);

	/*
	 * Drop the freesp meta blocks that are in use by btrees.
	 * The remaining blocks /should/ be AGFL blocks.
	 */
	error = xbitmap_disunion(agfl_extents, &ra.agmetablocks);
	xbitmap_destroy(&ra.agmetablocks);
	if (error)
		return error;

	/*
	 * Calculate the new AGFL size.  If we found more blocks than fit in
	 * the AGFL we'll free them later.
	 */
	*flcount = min_t(uint64_t, xbitmap_hweight(agfl_extents),
			 xfs_agfl_size(mp));
	return 0;

err:
	xbitmap_destroy(&ra.agmetablocks);
	xfs_btree_del_cursor(cur, error);
	return error;
}

/* Update the AGF and reset the in-core state. */
STATIC void
xrep_agfl_update_agf(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agf_bp,
	xfs_agblock_t		flcount)
{
	struct xfs_agf		*agf = agf_bp->b_addr;

	ASSERT(flcount <= xfs_agfl_size(sc->mp));

	/* Trigger fdblocks recalculation */
	xfs_force_summary_recalc(sc->mp);

	/* Update the AGF counters. */
	if (sc->sa.pag->pagf_init)
		sc->sa.pag->pagf_flcount = flcount;
	agf->agf_flfirst = cpu_to_be32(0);
	agf->agf_flcount = cpu_to_be32(flcount);
	agf->agf_fllast = cpu_to_be32(flcount - 1);

	xfs_alloc_log_agf(sc->tp, agf_bp,
			XFS_AGF_FLFIRST | XFS_AGF_FLLAST | XFS_AGF_FLCOUNT);
}

/* Write out a totally new AGFL. */
STATIC void
xrep_agfl_init_header(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agfl_bp,
	struct xbitmap		*agfl_extents,
	xfs_agblock_t		flcount)
{
	struct xfs_mount	*mp = sc->mp;
	__be32			*agfl_bno;
	struct xbitmap_range	*br;
	struct xbitmap_range	*n;
	struct xfs_agfl		*agfl;
	xfs_agblock_t		agbno;
	unsigned int		fl_off;

	ASSERT(flcount <= xfs_agfl_size(mp));

	/*
	 * Start rewriting the header by setting the bno[] array to
	 * NULLAGBLOCK, then setting AGFL header fields.
	 */
	agfl = XFS_BUF_TO_AGFL(agfl_bp);
	memset(agfl, 0xFF, BBTOB(agfl_bp->b_length));
	agfl->agfl_magicnum = cpu_to_be32(XFS_AGFL_MAGIC);
	agfl->agfl_seqno = cpu_to_be32(sc->sa.pag->pag_agno);
	uuid_copy(&agfl->agfl_uuid, &mp->m_sb.sb_meta_uuid);

	/*
	 * Fill the AGFL with the remaining blocks.  If agfl_extents has more
	 * blocks than fit in the AGFL, they will be freed in a subsequent
	 * step.
	 */
	fl_off = 0;
	agfl_bno = xfs_buf_to_agfl_bno(agfl_bp);
	for_each_xbitmap_extent(br, n, agfl_extents) {
		agbno = XFS_FSB_TO_AGBNO(mp, br->start);

		trace_xrep_agfl_insert(mp, sc->sa.pag->pag_agno, agbno,
				br->len);

		while (br->len > 0 && fl_off < flcount) {
			agfl_bno[fl_off] = cpu_to_be32(agbno);
			fl_off++;
			agbno++;

			/*
			 * We've now used br->start by putting it in the AGFL,
			 * so bump br so that we don't reap the block later.
			 */
			br->start++;
			br->len--;
		}

		if (br->len)
			break;
		list_del(&br->list);
		kmem_free(br);
	}

	/* Write new AGFL to disk. */
	xfs_trans_buf_set_type(sc->tp, agfl_bp, XFS_BLFT_AGFL_BUF);
	xfs_trans_log_buf(sc->tp, agfl_bp, 0, BBTOB(agfl_bp->b_length) - 1);
}

/* Repair the AGFL. */
int
xrep_agfl(
	struct xfs_scrub	*sc)
{
	struct xbitmap		agfl_extents;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_buf		*agf_bp;
	struct xfs_buf		*agfl_bp;
	xfs_agblock_t		flcount;
	int			error;

	/* We require the rmapbt to rebuild anything. */
	if (!xfs_has_rmapbt(mp))
		return -EOPNOTSUPP;

	xbitmap_init(&agfl_extents);

	/*
	 * Read the AGF so that we can query the rmapbt.  We hope that there's
	 * nothing wrong with the AGF, but all the AG header repair functions
	 * have this chicken-and-egg problem.
	 */
	error = xfs_alloc_read_agf(sc->sa.pag, sc->tp, 0, &agf_bp);
	if (error)
		return error;

	/*
	 * Make sure we have the AGFL buffer, as scrub might have decided it
	 * was corrupt after xfs_alloc_read_agfl failed with -EFSCORRUPTED.
	 */
	error = xfs_trans_read_buf(mp, sc->tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, sc->sa.pag->pag_agno,
						XFS_AGFL_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, &agfl_bp, NULL);
	if (error)
		return error;
	agfl_bp->b_ops = &xfs_agfl_buf_ops;

	/* Gather all the extents we're going to put on the new AGFL. */
	error = xrep_agfl_collect_blocks(sc, agf_bp, &agfl_extents, &flcount);
	if (error)
		goto err;

	/*
	 * Update AGF and AGFL.  We reset the global free block counter when
	 * we adjust the AGF flcount (which can fail) so avoid updating any
	 * buffers until we know that part works.
	 */
	xrep_agfl_update_agf(sc, agf_bp, flcount);
	xrep_agfl_init_header(sc, agfl_bp, &agfl_extents, flcount);

	/*
	 * Ok, the AGFL should be ready to go now.  Roll the transaction to
	 * make the new AGFL permanent before we start using it to return
	 * freespace overflow to the freespace btrees.
	 */
	sc->sa.agf_bp = agf_bp;
	sc->sa.agfl_bp = agfl_bp;
	error = xrep_roll_ag_trans(sc);
	if (error)
		goto err;

	/* Dump any AGFL overflow. */
	error = xrep_reap_extents(sc, &agfl_extents, &XFS_RMAP_OINFO_AG,
			XFS_AG_RESV_AGFL);
err:
	xbitmap_destroy(&agfl_extents);
	return error;
}

/* AGI */

/*
 * Offset within the xrep_find_ag_btree array for each btree type.  Avoid the
 * XFS_BTNUM_ names here to avoid creating a sparse array.
 */
enum {
	XREP_AGI_INOBT = 0,
	XREP_AGI_FINOBT,
	XREP_AGI_END,
	XREP_AGI_MAX
};

/*
 * Given the inode btree roots described by *fab, find the roots, check them
 * for sanity, and pass the root data back out via *fab.
 */
STATIC int
xrep_agi_find_btrees(
	struct xfs_scrub		*sc,
	struct xrep_find_ag_btree	*fab)
{
	struct xfs_buf			*agf_bp;
	struct xfs_mount		*mp = sc->mp;
	int				error;

	/* Read the AGF. */
	error = xfs_alloc_read_agf(sc->sa.pag, sc->tp, 0, &agf_bp);
	if (error)
		return error;

	/* Find the btree roots. */
	error = xrep_find_ag_btree_roots(sc, agf_bp, fab, NULL);
	if (error)
		return error;

	/* We must find the inobt root. */
	if (!xrep_check_btree_root(sc, &fab[XREP_AGI_INOBT]))
		return -EFSCORRUPTED;

	/* We must find the finobt root if that feature is enabled. */
	if (xfs_has_finobt(mp) &&
	    !xrep_check_btree_root(sc, &fab[XREP_AGI_FINOBT]))
		return -EFSCORRUPTED;

	return 0;
}

/*
 * Reinitialize the AGI header, making an in-core copy of the old contents so
 * that we know which in-core state needs to be reinitialized.
 */
STATIC void
xrep_agi_init_header(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agi_bp,
	struct xfs_agi		*old_agi)
{
	struct xfs_agi		*agi = agi_bp->b_addr;
	struct xfs_mount	*mp = sc->mp;

	memcpy(old_agi, agi, sizeof(*old_agi));
	memset(agi, 0, BBTOB(agi_bp->b_length));
	agi->agi_magicnum = cpu_to_be32(XFS_AGI_MAGIC);
	agi->agi_versionnum = cpu_to_be32(XFS_AGI_VERSION);
	agi->agi_seqno = cpu_to_be32(sc->sa.pag->pag_agno);
	agi->agi_length = cpu_to_be32(sc->sa.pag->block_count);
	agi->agi_newino = cpu_to_be32(NULLAGINO);
	agi->agi_dirino = cpu_to_be32(NULLAGINO);
	if (xfs_has_crc(mp))
		uuid_copy(&agi->agi_uuid, &mp->m_sb.sb_meta_uuid);

	/* We don't know how to fix the unlinked list yet. */
	memcpy(&agi->agi_unlinked, &old_agi->agi_unlinked,
			sizeof(agi->agi_unlinked));

	/* Mark the incore AGF data stale until we're done fixing things. */
	ASSERT(sc->sa.pag->pagi_init);
	sc->sa.pag->pagi_init = 0;
}

/* Set btree root information in an AGI. */
STATIC void
xrep_agi_set_roots(
	struct xfs_scrub		*sc,
	struct xfs_agi			*agi,
	struct xrep_find_ag_btree	*fab)
{
	agi->agi_root = cpu_to_be32(fab[XREP_AGI_INOBT].root);
	agi->agi_level = cpu_to_be32(fab[XREP_AGI_INOBT].height);

	if (xfs_has_finobt(sc->mp)) {
		agi->agi_free_root = cpu_to_be32(fab[XREP_AGI_FINOBT].root);
		agi->agi_free_level = cpu_to_be32(fab[XREP_AGI_FINOBT].height);
	}
}

/* Update the AGI counters. */
STATIC int
xrep_agi_calc_from_btrees(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agi_bp)
{
	struct xfs_btree_cur	*cur;
	struct xfs_agi		*agi = agi_bp->b_addr;
	struct xfs_mount	*mp = sc->mp;
	xfs_agino_t		count;
	xfs_agino_t		freecount;
	int			error;

	cur = xfs_inobt_init_cursor(mp, sc->tp, agi_bp,
			sc->sa.pag, XFS_BTNUM_INO);
	error = xfs_ialloc_count_inodes(cur, &count, &freecount);
	if (error)
		goto err;
	if (xfs_has_inobtcounts(mp)) {
		xfs_agblock_t	blocks;

		error = xfs_btree_count_blocks(cur, &blocks);
		if (error)
			goto err;
		agi->agi_iblocks = cpu_to_be32(blocks);
	}
	xfs_btree_del_cursor(cur, error);

	agi->agi_count = cpu_to_be32(count);
	agi->agi_freecount = cpu_to_be32(freecount);

	if (xfs_has_finobt(mp) && xfs_has_inobtcounts(mp)) {
		xfs_agblock_t	blocks;

		cur = xfs_inobt_init_cursor(mp, sc->tp, agi_bp,
				sc->sa.pag, XFS_BTNUM_FINO);
		error = xfs_btree_count_blocks(cur, &blocks);
		if (error)
			goto err;
		xfs_btree_del_cursor(cur, error);
		agi->agi_fblocks = cpu_to_be32(blocks);
	}

	return 0;
err:
	xfs_btree_del_cursor(cur, error);
	return error;
}

/* Trigger reinitialization of the in-core data. */
STATIC int
xrep_agi_commit_new(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agi_bp)
{
	struct xfs_perag	*pag;
	struct xfs_agi		*agi = agi_bp->b_addr;

	/* Trigger inode count recalculation */
	xfs_force_summary_recalc(sc->mp);

	/* Write this to disk. */
	xfs_trans_buf_set_type(sc->tp, agi_bp, XFS_BLFT_AGI_BUF);
	xfs_trans_log_buf(sc->tp, agi_bp, 0, BBTOB(agi_bp->b_length) - 1);

	/* Now reinitialize the in-core counters if necessary. */
	pag = sc->sa.pag;
	pag->pagi_count = be32_to_cpu(agi->agi_count);
	pag->pagi_freecount = be32_to_cpu(agi->agi_freecount);
	pag->pagi_init = 1;

	return 0;
}

/* Repair the AGI. */
int
xrep_agi(
	struct xfs_scrub		*sc)
{
	struct xrep_find_ag_btree	fab[XREP_AGI_MAX] = {
		[XREP_AGI_INOBT] = {
			.rmap_owner = XFS_RMAP_OWN_INOBT,
			.buf_ops = &xfs_inobt_buf_ops,
			.maxlevels = M_IGEO(sc->mp)->inobt_maxlevels,
		},
		[XREP_AGI_FINOBT] = {
			.rmap_owner = XFS_RMAP_OWN_INOBT,
			.buf_ops = &xfs_finobt_buf_ops,
			.maxlevels = M_IGEO(sc->mp)->inobt_maxlevels,
		},
		[XREP_AGI_END] = {
			.buf_ops = NULL
		},
	};
	struct xfs_agi			old_agi;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_buf			*agi_bp;
	struct xfs_agi			*agi;
	int				error;

	/* We require the rmapbt to rebuild anything. */
	if (!xfs_has_rmapbt(mp))
		return -EOPNOTSUPP;

	/*
	 * Make sure we have the AGI buffer, as scrub might have decided it
	 * was corrupt after xfs_ialloc_read_agi failed with -EFSCORRUPTED.
	 */
	error = xfs_trans_read_buf(mp, sc->tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, sc->sa.pag->pag_agno,
						XFS_AGI_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, &agi_bp, NULL);
	if (error)
		return error;
	agi_bp->b_ops = &xfs_agi_buf_ops;
	agi = agi_bp->b_addr;

	/* Find the AGI btree roots. */
	error = xrep_agi_find_btrees(sc, fab);
	if (error)
		return error;

	/* Start rewriting the header and implant the btrees we found. */
	xrep_agi_init_header(sc, agi_bp, &old_agi);
	xrep_agi_set_roots(sc, agi, fab);
	error = xrep_agi_calc_from_btrees(sc, agi_bp);
	if (error)
		goto out_revert;

	/* Reinitialize in-core state. */
	return xrep_agi_commit_new(sc, agi_bp);

out_revert:
	/* Mark the incore AGI state stale and revert the AGI. */
	sc->sa.pag->pagi_init = 0;
	memcpy(agi, &old_agi, sizeof(old_agi));
	return error;
}
