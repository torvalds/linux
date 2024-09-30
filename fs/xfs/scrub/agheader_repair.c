// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
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
#include "xfs_sb.h"
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount_btree.h"
#include "xfs_ag.h"
#include "xfs_inode.h"
#include "xfs_iunlink_item.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
#include "scrub/agb_bitmap.h"
#include "scrub/agino_bitmap.h"
#include "scrub/reap.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"

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

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
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
	return 0;
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
	if (fab[XREP_AGF_RMAPBT].root != be32_to_cpu(old_agf->agf_rmap_root))
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
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_agf		*agf = agf_bp->b_addr;

	memcpy(old_agf, agf, sizeof(*old_agf));
	memset(agf, 0, BBTOB(agf_bp->b_length));
	agf->agf_magicnum = cpu_to_be32(XFS_AGF_MAGIC);
	agf->agf_versionnum = cpu_to_be32(XFS_AGF_VERSION);
	agf->agf_seqno = cpu_to_be32(pag->pag_agno);
	agf->agf_length = cpu_to_be32(pag->block_count);
	agf->agf_flfirst = old_agf->agf_flfirst;
	agf->agf_fllast = old_agf->agf_fllast;
	agf->agf_flcount = old_agf->agf_flcount;
	if (xfs_has_crc(mp))
		uuid_copy(&agf->agf_uuid, &mp->m_sb.sb_meta_uuid);

	/* Mark the incore AGF data stale until we're done fixing things. */
	ASSERT(xfs_perag_initialised_agf(pag));
	clear_bit(XFS_AGSTATE_AGF_INIT, &pag->pag_opstate);
}

/* Set btree root information in an AGF. */
STATIC void
xrep_agf_set_roots(
	struct xfs_scrub		*sc,
	struct xfs_agf			*agf,
	struct xrep_find_ag_btree	*fab)
{
	agf->agf_bno_root = cpu_to_be32(fab[XREP_AGF_BNOBT].root);
	agf->agf_bno_level = cpu_to_be32(fab[XREP_AGF_BNOBT].height);

	agf->agf_cnt_root = cpu_to_be32(fab[XREP_AGF_CNTBT].root);
	agf->agf_cnt_level = cpu_to_be32(fab[XREP_AGF_CNTBT].height);

	agf->agf_rmap_root = cpu_to_be32(fab[XREP_AGF_RMAPBT].root);
	agf->agf_rmap_level = cpu_to_be32(fab[XREP_AGF_RMAPBT].height);

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
	cur = xfs_bnobt_init_cursor(mp, sc->tp, agf_bp, sc->sa.pag);
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
	cur = xfs_cntbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.pag);
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
	pag->pagf_bno_level = be32_to_cpu(agf->agf_bno_level);
	pag->pagf_cnt_level = be32_to_cpu(agf->agf_cnt_level);
	pag->pagf_rmap_level = be32_to_cpu(agf->agf_rmap_level);
	pag->pagf_refcount_level = be32_to_cpu(agf->agf_refcount_level);
	set_bit(XFS_AGSTATE_AGF_INIT, &pag->pag_opstate);

	return xrep_roll_ag_trans(sc);
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

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
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
	clear_bit(XFS_AGSTATE_AGF_INIT, &sc->sa.pag->pag_opstate);
	memcpy(agf, &old_agf, sizeof(old_agf));
	return error;
}

/* AGFL */

struct xrep_agfl {
	/* Bitmap of alleged AGFL blocks that we're not going to add. */
	struct xagb_bitmap	crossed;

	/* Bitmap of other OWN_AG metadata blocks. */
	struct xagb_bitmap	agmetablocks;

	/* Bitmap of free space. */
	struct xagb_bitmap	*freesp;

	/* rmapbt cursor for finding crosslinked blocks */
	struct xfs_btree_cur	*rmap_cur;

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
	int			error = 0;

	if (xchk_should_terminate(ra->sc, &error))
		return error;

	/* Record all the OWN_AG blocks. */
	if (rec->rm_owner == XFS_RMAP_OWN_AG) {
		error = xagb_bitmap_set(ra->freesp, rec->rm_startblock,
				rec->rm_blockcount);
		if (error)
			return error;
	}

	return xagb_bitmap_set_btcur_path(&ra->agmetablocks, cur);
}

/* Strike out the blocks that are cross-linked according to the rmapbt. */
STATIC int
xrep_agfl_check_extent(
	uint32_t		agbno,
	uint32_t		len,
	void			*priv)
{
	struct xrep_agfl	*ra = priv;
	xfs_agblock_t		last_agbno = agbno + len - 1;
	int			error;

	while (agbno <= last_agbno) {
		bool		other_owners;

		error = xfs_rmap_has_other_keys(ra->rmap_cur, agbno, 1,
				&XFS_RMAP_OINFO_AG, &other_owners);
		if (error)
			return error;

		if (other_owners) {
			error = xagb_bitmap_set(&ra->crossed, agbno, 1);
			if (error)
				return error;
		}

		if (xchk_should_terminate(ra->sc, &error))
			return error;
		agbno++;
	}

	return 0;
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
	struct xagb_bitmap	*agfl_extents,
	xfs_agblock_t		*flcount)
{
	struct xrep_agfl	ra;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_btree_cur	*cur;
	int			error;

	ra.sc = sc;
	ra.freesp = agfl_extents;
	xagb_bitmap_init(&ra.agmetablocks);
	xagb_bitmap_init(&ra.crossed);

	/* Find all space used by the free space btrees & rmapbt. */
	cur = xfs_rmapbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.pag);
	error = xfs_rmap_query_all(cur, xrep_agfl_walk_rmap, &ra);
	xfs_btree_del_cursor(cur, error);
	if (error)
		goto out_bmp;

	/* Find all blocks currently being used by the bnobt. */
	cur = xfs_bnobt_init_cursor(mp, sc->tp, agf_bp, sc->sa.pag);
	error = xagb_bitmap_set_btblocks(&ra.agmetablocks, cur);
	xfs_btree_del_cursor(cur, error);
	if (error)
		goto out_bmp;

	/* Find all blocks currently being used by the cntbt. */
	cur = xfs_cntbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.pag);
	error = xagb_bitmap_set_btblocks(&ra.agmetablocks, cur);
	xfs_btree_del_cursor(cur, error);
	if (error)
		goto out_bmp;

	/*
	 * Drop the freesp meta blocks that are in use by btrees.
	 * The remaining blocks /should/ be AGFL blocks.
	 */
	error = xagb_bitmap_disunion(agfl_extents, &ra.agmetablocks);
	if (error)
		goto out_bmp;

	/* Strike out the blocks that are cross-linked. */
	ra.rmap_cur = xfs_rmapbt_init_cursor(mp, sc->tp, agf_bp, sc->sa.pag);
	error = xagb_bitmap_walk(agfl_extents, xrep_agfl_check_extent, &ra);
	xfs_btree_del_cursor(ra.rmap_cur, error);
	if (error)
		goto out_bmp;
	error = xagb_bitmap_disunion(agfl_extents, &ra.crossed);
	if (error)
		goto out_bmp;

	/*
	 * Calculate the new AGFL size.  If we found more blocks than fit in
	 * the AGFL we'll free them later.
	 */
	*flcount = min_t(uint64_t, xagb_bitmap_hweight(agfl_extents),
			 xfs_agfl_size(mp));

out_bmp:
	xagb_bitmap_destroy(&ra.crossed);
	xagb_bitmap_destroy(&ra.agmetablocks);
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
	if (xfs_perag_initialised_agf(sc->sa.pag)) {
		sc->sa.pag->pagf_flcount = flcount;
		clear_bit(XFS_AGSTATE_AGFL_NEEDS_RESET,
				&sc->sa.pag->pag_opstate);
	}
	agf->agf_flfirst = cpu_to_be32(0);
	agf->agf_flcount = cpu_to_be32(flcount);
	if (flcount)
		agf->agf_fllast = cpu_to_be32(flcount - 1);
	else
		agf->agf_fllast = cpu_to_be32(xfs_agfl_size(sc->mp) - 1);

	xfs_alloc_log_agf(sc->tp, agf_bp,
			XFS_AGF_FLFIRST | XFS_AGF_FLLAST | XFS_AGF_FLCOUNT);
}

struct xrep_agfl_fill {
	struct xagb_bitmap	used_extents;
	struct xfs_scrub	*sc;
	__be32			*agfl_bno;
	xfs_agblock_t		flcount;
	unsigned int		fl_off;
};

/* Fill the AGFL with whatever blocks are in this extent. */
static int
xrep_agfl_fill(
	uint32_t		start,
	uint32_t		len,
	void			*priv)
{
	struct xrep_agfl_fill	*af = priv;
	struct xfs_scrub	*sc = af->sc;
	xfs_agblock_t		agbno = start;
	int			error;

	trace_xrep_agfl_insert(sc->sa.pag, agbno, len);

	while (agbno < start + len && af->fl_off < af->flcount)
		af->agfl_bno[af->fl_off++] = cpu_to_be32(agbno++);

	error = xagb_bitmap_set(&af->used_extents, start, agbno - 1);
	if (error)
		return error;

	if (af->fl_off == af->flcount)
		return -ECANCELED;

	return 0;
}

/* Write out a totally new AGFL. */
STATIC int
xrep_agfl_init_header(
	struct xfs_scrub	*sc,
	struct xfs_buf		*agfl_bp,
	struct xagb_bitmap	*agfl_extents,
	xfs_agblock_t		flcount)
{
	struct xrep_agfl_fill	af = {
		.sc		= sc,
		.flcount	= flcount,
	};
	struct xfs_mount	*mp = sc->mp;
	struct xfs_agfl		*agfl;
	int			error;

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
	xagb_bitmap_init(&af.used_extents);
	af.agfl_bno = xfs_buf_to_agfl_bno(agfl_bp);
	xagb_bitmap_walk(agfl_extents, xrep_agfl_fill, &af);
	error = xagb_bitmap_disunion(agfl_extents, &af.used_extents);
	if (error)
		return error;

	/* Write new AGFL to disk. */
	xfs_trans_buf_set_type(sc->tp, agfl_bp, XFS_BLFT_AGFL_BUF);
	xfs_trans_log_buf(sc->tp, agfl_bp, 0, BBTOB(agfl_bp->b_length) - 1);
	xagb_bitmap_destroy(&af.used_extents);
	return 0;
}

/* Repair the AGFL. */
int
xrep_agfl(
	struct xfs_scrub	*sc)
{
	struct xagb_bitmap	agfl_extents;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_buf		*agf_bp;
	struct xfs_buf		*agfl_bp;
	xfs_agblock_t		flcount;
	int			error;

	/* We require the rmapbt to rebuild anything. */
	if (!xfs_has_rmapbt(mp))
		return -EOPNOTSUPP;

	xagb_bitmap_init(&agfl_extents);

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

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		goto err;

	/*
	 * Update AGF and AGFL.  We reset the global free block counter when
	 * we adjust the AGF flcount (which can fail) so avoid updating any
	 * buffers until we know that part works.
	 */
	xrep_agfl_update_agf(sc, agf_bp, flcount);
	error = xrep_agfl_init_header(sc, agfl_bp, &agfl_extents, flcount);
	if (error)
		goto err;

	/*
	 * Ok, the AGFL should be ready to go now.  Roll the transaction to
	 * make the new AGFL permanent before we start using it to return
	 * freespace overflow to the freespace btrees.
	 */
	sc->sa.agf_bp = agf_bp;
	error = xrep_roll_ag_trans(sc);
	if (error)
		goto err;

	/* Dump any AGFL overflow. */
	error = xrep_reap_agblocks(sc, &agfl_extents, &XFS_RMAP_OINFO_AG,
			XFS_AG_RESV_AGFL);
	if (error)
		goto err;

err:
	xagb_bitmap_destroy(&agfl_extents);
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

#define XREP_AGI_LOOKUP_BATCH		32

struct xrep_agi {
	struct xfs_scrub		*sc;

	/* AGI buffer, tracked separately */
	struct xfs_buf			*agi_bp;

	/* context for finding btree roots */
	struct xrep_find_ag_btree	fab[XREP_AGI_MAX];

	/* old AGI contents in case we have to revert */
	struct xfs_agi			old_agi;

	/* bitmap of which inodes are unlinked */
	struct xagino_bitmap		iunlink_bmp;

	/* heads of the unlinked inode bucket lists */
	xfs_agino_t			iunlink_heads[XFS_AGI_UNLINKED_BUCKETS];

	/* scratchpad for batched lookups of the radix tree */
	struct xfs_inode		*lookup_batch[XREP_AGI_LOOKUP_BATCH];

	/* Map of ino -> next_ino for unlinked inode processing. */
	struct xfarray			*iunlink_next;

	/* Map of ino -> prev_ino for unlinked inode processing. */
	struct xfarray			*iunlink_prev;
};

static void
xrep_agi_buf_cleanup(
	void		*buf)
{
	struct xrep_agi	*ragi = buf;

	xfarray_destroy(ragi->iunlink_prev);
	xfarray_destroy(ragi->iunlink_next);
	xagino_bitmap_destroy(&ragi->iunlink_bmp);
}

/*
 * Given the inode btree roots described by *fab, find the roots, check them
 * for sanity, and pass the root data back out via *fab.
 */
STATIC int
xrep_agi_find_btrees(
	struct xrep_agi			*ragi)
{
	struct xfs_scrub		*sc = ragi->sc;
	struct xrep_find_ag_btree	*fab = ragi->fab;
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
	struct xrep_agi		*ragi)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_buf		*agi_bp = ragi->agi_bp;
	struct xfs_agi		*old_agi = &ragi->old_agi;
	struct xfs_agi		*agi = agi_bp->b_addr;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_mount	*mp = sc->mp;

	memcpy(old_agi, agi, sizeof(*old_agi));
	memset(agi, 0, BBTOB(agi_bp->b_length));
	agi->agi_magicnum = cpu_to_be32(XFS_AGI_MAGIC);
	agi->agi_versionnum = cpu_to_be32(XFS_AGI_VERSION);
	agi->agi_seqno = cpu_to_be32(pag->pag_agno);
	agi->agi_length = cpu_to_be32(pag->block_count);
	agi->agi_newino = cpu_to_be32(NULLAGINO);
	agi->agi_dirino = cpu_to_be32(NULLAGINO);
	if (xfs_has_crc(mp))
		uuid_copy(&agi->agi_uuid, &mp->m_sb.sb_meta_uuid);

	/* Mark the incore AGF data stale until we're done fixing things. */
	ASSERT(xfs_perag_initialised_agi(pag));
	clear_bit(XFS_AGSTATE_AGI_INIT, &pag->pag_opstate);
}

/* Set btree root information in an AGI. */
STATIC void
xrep_agi_set_roots(
	struct xrep_agi			*ragi)
{
	struct xfs_scrub		*sc = ragi->sc;
	struct xfs_agi			*agi = ragi->agi_bp->b_addr;
	struct xrep_find_ag_btree	*fab = ragi->fab;

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
	struct xrep_agi		*ragi)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_buf		*agi_bp = ragi->agi_bp;
	struct xfs_btree_cur	*cur;
	struct xfs_agi		*agi = agi_bp->b_addr;
	struct xfs_mount	*mp = sc->mp;
	xfs_agino_t		count;
	xfs_agino_t		freecount;
	int			error;

	cur = xfs_inobt_init_cursor(sc->sa.pag, sc->tp, agi_bp);
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

		cur = xfs_finobt_init_cursor(sc->sa.pag, sc->tp, agi_bp);
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

/*
 * Record a forwards unlinked chain pointer from agino -> next_agino in our
 * staging information.
 */
static inline int
xrep_iunlink_store_next(
	struct xrep_agi		*ragi,
	xfs_agino_t		agino,
	xfs_agino_t		next_agino)
{
	ASSERT(next_agino != 0);

	return xfarray_store(ragi->iunlink_next, agino, &next_agino);
}

/*
 * Record a backwards unlinked chain pointer from prev_ino <- agino in our
 * staging information.
 */
static inline int
xrep_iunlink_store_prev(
	struct xrep_agi		*ragi,
	xfs_agino_t		agino,
	xfs_agino_t		prev_agino)
{
	ASSERT(prev_agino != 0);

	return xfarray_store(ragi->iunlink_prev, agino, &prev_agino);
}

/*
 * Given an @agino, look up the next inode in the iunlink bucket.  Returns
 * NULLAGINO if we're at the end of the chain, 0 if @agino is not in memory
 * like it should be, or a per-AG inode number.
 */
static inline xfs_agino_t
xrep_iunlink_next(
	struct xfs_scrub	*sc,
	xfs_agino_t		agino)
{
	struct xfs_inode	*ip;

	ip = xfs_iunlink_lookup(sc->sa.pag, agino);
	if (!ip)
		return 0;

	return ip->i_next_unlinked;
}

/*
 * Load the inode @agino into memory, set its i_prev_unlinked, and drop the
 * inode so it can be inactivated.  Returns NULLAGINO if we're at the end of
 * the chain or if we should stop walking the chain due to corruption; or a
 * per-AG inode number.
 */
STATIC xfs_agino_t
xrep_iunlink_reload_next(
	struct xrep_agi		*ragi,
	xfs_agino_t		prev_agino,
	xfs_agino_t		agino)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_inode	*ip;
	xfs_ino_t		ino;
	xfs_agino_t		ret = NULLAGINO;
	int			error;

	ino = XFS_AGINO_TO_INO(sc->mp, sc->sa.pag->pag_agno, agino);
	error = xchk_iget(ragi->sc, ino, &ip);
	if (error)
		return ret;

	trace_xrep_iunlink_reload_next(ip, prev_agino);

	/* If this is a linked inode, stop processing the chain. */
	if (VFS_I(ip)->i_nlink != 0) {
		xrep_iunlink_store_next(ragi, agino, NULLAGINO);
		goto rele;
	}

	ip->i_prev_unlinked = prev_agino;
	ret = ip->i_next_unlinked;

	/*
	 * Drop the inode reference that we just took.  We hold the AGI, so
	 * this inode cannot move off the unlinked list and hence cannot be
	 * reclaimed.
	 */
rele:
	xchk_irele(sc, ip);
	return ret;
}

/*
 * Walk an AGI unlinked bucket's list to load incore any unlinked inodes that
 * still existed at mount time.  This can happen if iunlink processing fails
 * during log recovery.
 */
STATIC int
xrep_iunlink_walk_ondisk_bucket(
	struct xrep_agi		*ragi,
	unsigned int		bucket)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_agi		*agi = sc->sa.agi_bp->b_addr;
	xfs_agino_t		prev_agino = NULLAGINO;
	xfs_agino_t		next_agino;
	int			error = 0;

	next_agino = be32_to_cpu(agi->agi_unlinked[bucket]);
	while (next_agino != NULLAGINO) {
		xfs_agino_t	agino = next_agino;

		if (xchk_should_terminate(ragi->sc, &error))
			return error;

		trace_xrep_iunlink_walk_ondisk_bucket(sc->sa.pag, bucket,
				prev_agino, agino);

		if (bucket != agino % XFS_AGI_UNLINKED_BUCKETS)
			break;

		next_agino = xrep_iunlink_next(sc, agino);
		if (!next_agino)
			next_agino = xrep_iunlink_reload_next(ragi, prev_agino,
					agino);

		prev_agino = agino;
	}

	return 0;
}

/* Decide if this is an unlinked inode in this AG. */
STATIC bool
xrep_iunlink_igrab(
	struct xfs_perag	*pag,
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = pag->pag_mount;

	if (XFS_INO_TO_AGNO(mp, ip->i_ino) != pag->pag_agno)
		return false;

	if (!xfs_inode_on_unlinked_list(ip))
		return false;

	return true;
}

/*
 * Mark the given inode in the lookup batch in our unlinked inode bitmap, and
 * remember if this inode is the start of the unlinked chain.
 */
STATIC int
xrep_iunlink_visit(
	struct xrep_agi		*ragi,
	unsigned int		batch_idx)
{
	struct xfs_mount	*mp = ragi->sc->mp;
	struct xfs_inode	*ip = ragi->lookup_batch[batch_idx];
	xfs_agino_t		agino;
	unsigned int		bucket;
	int			error;

	ASSERT(XFS_INO_TO_AGNO(mp, ip->i_ino) == ragi->sc->sa.pag->pag_agno);
	ASSERT(xfs_inode_on_unlinked_list(ip));

	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	bucket = agino % XFS_AGI_UNLINKED_BUCKETS;

	trace_xrep_iunlink_visit(ragi->sc->sa.pag, bucket,
			ragi->iunlink_heads[bucket], ip);

	error = xagino_bitmap_set(&ragi->iunlink_bmp, agino, 1);
	if (error)
		return error;

	if (ip->i_prev_unlinked == NULLAGINO) {
		if (ragi->iunlink_heads[bucket] == NULLAGINO)
			ragi->iunlink_heads[bucket] = agino;
	}

	return 0;
}

/*
 * Find all incore unlinked inodes so that we can rebuild the unlinked buckets.
 * We hold the AGI so there should not be any modifications to the unlinked
 * list.
 */
STATIC int
xrep_iunlink_mark_incore(
	struct xrep_agi		*ragi)
{
	struct xfs_perag	*pag = ragi->sc->sa.pag;
	struct xfs_mount	*mp = pag->pag_mount;
	uint32_t		first_index = 0;
	bool			done = false;
	unsigned int		nr_found = 0;

	do {
		unsigned int	i;
		int		error = 0;

		if (xchk_should_terminate(ragi->sc, &error))
			return error;

		rcu_read_lock();

		nr_found = radix_tree_gang_lookup(&pag->pag_ici_root,
				(void **)&ragi->lookup_batch, first_index,
				XREP_AGI_LOOKUP_BATCH);
		if (!nr_found) {
			rcu_read_unlock();
			return 0;
		}

		for (i = 0; i < nr_found; i++) {
			struct xfs_inode *ip = ragi->lookup_batch[i];

			if (done || !xrep_iunlink_igrab(pag, ip))
				ragi->lookup_batch[i] = NULL;

			/*
			 * Update the index for the next lookup. Catch
			 * overflows into the next AG range which can occur if
			 * we have inodes in the last block of the AG and we
			 * are currently pointing to the last inode.
			 *
			 * Because we may see inodes that are from the wrong AG
			 * due to RCU freeing and reallocation, only update the
			 * index if it lies in this AG. It was a race that lead
			 * us to see this inode, so another lookup from the
			 * same index will not find it again.
			 */
			if (XFS_INO_TO_AGNO(mp, ip->i_ino) != pag->pag_agno)
				continue;
			first_index = XFS_INO_TO_AGINO(mp, ip->i_ino + 1);
			if (first_index < XFS_INO_TO_AGINO(mp, ip->i_ino))
				done = true;
		}

		/* unlock now we've grabbed the inodes. */
		rcu_read_unlock();

		for (i = 0; i < nr_found; i++) {
			if (!ragi->lookup_batch[i])
				continue;
			error = xrep_iunlink_visit(ragi, i);
			if (error)
				return error;
		}
	} while (!done);

	return 0;
}

/* Mark all the unlinked ondisk inodes in this inobt record in iunlink_bmp. */
STATIC int
xrep_iunlink_mark_ondisk_rec(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	struct xfs_inobt_rec_incore	irec;
	struct xrep_agi			*ragi = priv;
	struct xfs_scrub		*sc = ragi->sc;
	struct xfs_mount		*mp = cur->bc_mp;
	xfs_agino_t			agino;
	unsigned int			i;
	int				error = 0;

	xfs_inobt_btrec_to_irec(mp, rec, &irec);

	for (i = 0, agino = irec.ir_startino;
	     i < XFS_INODES_PER_CHUNK;
	     i++, agino++) {
		struct xfs_inode	*ip;
		unsigned int		len = 1;

		/* Skip free inodes */
		if (XFS_INOBT_MASK(i) & irec.ir_free)
			continue;
		/* Skip inodes we've seen before */
		if (xagino_bitmap_test(&ragi->iunlink_bmp, agino, &len))
			continue;

		/*
		 * Skip incore inodes; these were already picked up by
		 * the _mark_incore step.
		 */
		rcu_read_lock();
		ip = radix_tree_lookup(&sc->sa.pag->pag_ici_root, agino);
		rcu_read_unlock();
		if (ip)
			continue;

		/*
		 * Try to look up this inode.  If we can't get it, just move
		 * on because we haven't actually scrubbed the inobt or the
		 * inodes yet.
		 */
		error = xchk_iget(ragi->sc,
				XFS_AGINO_TO_INO(mp, sc->sa.pag->pag_agno,
						 agino),
				&ip);
		if (error)
			continue;

		trace_xrep_iunlink_reload_ondisk(ip);

		if (VFS_I(ip)->i_nlink == 0)
			error = xagino_bitmap_set(&ragi->iunlink_bmp, agino, 1);
		xchk_irele(sc, ip);
		if (error)
			break;
	}

	return error;
}

/*
 * Find ondisk inodes that are unlinked and not in cache, and mark them in
 * iunlink_bmp.   We haven't checked the inobt yet, so we don't error out if
 * the btree is corrupt.
 */
STATIC void
xrep_iunlink_mark_ondisk(
	struct xrep_agi		*ragi)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_buf		*agi_bp = ragi->agi_bp;
	struct xfs_btree_cur	*cur;
	int			error;

	cur = xfs_inobt_init_cursor(sc->sa.pag, sc->tp, agi_bp);
	error = xfs_btree_query_all(cur, xrep_iunlink_mark_ondisk_rec, ragi);
	xfs_btree_del_cursor(cur, error);
}

/*
 * Walk an iunlink bucket's inode list.  For each inode that should be on this
 * chain, clear its entry in in iunlink_bmp because it's ok and we don't need
 * to touch it further.
 */
STATIC int
xrep_iunlink_resolve_bucket(
	struct xrep_agi		*ragi,
	unsigned int		bucket)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_inode	*ip;
	xfs_agino_t		prev_agino = NULLAGINO;
	xfs_agino_t		next_agino = ragi->iunlink_heads[bucket];
	int			error = 0;

	while (next_agino != NULLAGINO) {
		if (xchk_should_terminate(ragi->sc, &error))
			return error;

		/* Find the next inode in the chain. */
		ip = xfs_iunlink_lookup(sc->sa.pag, next_agino);
		if (!ip) {
			/* Inode not incore?  Terminate the chain. */
			trace_xrep_iunlink_resolve_uncached(sc->sa.pag,
					bucket, prev_agino, next_agino);

			next_agino = NULLAGINO;
			break;
		}

		if (next_agino % XFS_AGI_UNLINKED_BUCKETS != bucket) {
			/*
			 * Inode is in the wrong bucket.  Advance the list,
			 * but pretend we didn't see this inode.
			 */
			trace_xrep_iunlink_resolve_wronglist(sc->sa.pag,
					bucket, prev_agino, next_agino);

			next_agino = ip->i_next_unlinked;
			continue;
		}

		if (!xfs_inode_on_unlinked_list(ip)) {
			/*
			 * Incore inode doesn't think this inode is on an
			 * unlinked list.  This is probably because we reloaded
			 * it from disk.  Advance the list, but pretend we
			 * didn't see this inode; we'll fix that later.
			 */
			trace_xrep_iunlink_resolve_nolist(sc->sa.pag,
					bucket, prev_agino, next_agino);
			next_agino = ip->i_next_unlinked;
			continue;
		}

		trace_xrep_iunlink_resolve_ok(sc->sa.pag, bucket, prev_agino,
				next_agino);

		/*
		 * Otherwise, this inode's unlinked pointers are ok.  Clear it
		 * from the unlinked bitmap since we're done with it, and make
		 * sure the chain is still correct.
		 */
		error = xagino_bitmap_clear(&ragi->iunlink_bmp, next_agino, 1);
		if (error)
			return error;

		/* Remember the previous inode's next pointer. */
		if (prev_agino != NULLAGINO) {
			error = xrep_iunlink_store_next(ragi, prev_agino,
					next_agino);
			if (error)
				return error;
		}

		/* Remember this inode's previous pointer. */
		error = xrep_iunlink_store_prev(ragi, next_agino, prev_agino);
		if (error)
			return error;

		/* Advance the list and remember this inode. */
		prev_agino = next_agino;
		next_agino = ip->i_next_unlinked;
	}

	/* Update the previous inode's next pointer. */
	if (prev_agino != NULLAGINO) {
		error = xrep_iunlink_store_next(ragi, prev_agino, next_agino);
		if (error)
			return error;
	}

	return 0;
}

/* Reinsert this unlinked inode into the head of the staged bucket list. */
STATIC int
xrep_iunlink_add_to_bucket(
	struct xrep_agi		*ragi,
	xfs_agino_t		agino)
{
	xfs_agino_t		current_head;
	unsigned int		bucket;
	int			error;

	bucket = agino % XFS_AGI_UNLINKED_BUCKETS;

	/* Point this inode at the current head of the bucket list. */
	current_head = ragi->iunlink_heads[bucket];

	trace_xrep_iunlink_add_to_bucket(ragi->sc->sa.pag, bucket, agino,
			current_head);

	error = xrep_iunlink_store_next(ragi, agino, current_head);
	if (error)
		return error;

	/* Remember the head inode's previous pointer. */
	if (current_head != NULLAGINO) {
		error = xrep_iunlink_store_prev(ragi, current_head, agino);
		if (error)
			return error;
	}

	ragi->iunlink_heads[bucket] = agino;
	return 0;
}

/* Reinsert unlinked inodes into the staged iunlink buckets. */
STATIC int
xrep_iunlink_add_lost_inodes(
	uint32_t		start,
	uint32_t		len,
	void			*priv)
{
	struct xrep_agi		*ragi = priv;
	int			error;

	for (; len > 0; start++, len--) {
		error = xrep_iunlink_add_to_bucket(ragi, start);
		if (error)
			return error;
	}

	return 0;
}

/*
 * Figure out the iunlink bucket values and find inodes that need to be
 * reinserted into the list.
 */
STATIC int
xrep_iunlink_rebuild_buckets(
	struct xrep_agi		*ragi)
{
	unsigned int		i;
	int			error;

	/*
	 * Walk the ondisk AGI unlinked list to find inodes that are on the
	 * list but aren't in memory.  This can happen if a past log recovery
	 * tried to clear the iunlinked list but failed.  Our scan rebuilds the
	 * unlinked list using incore inodes, so we must load and link them
	 * properly.
	 */
	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		error = xrep_iunlink_walk_ondisk_bucket(ragi, i);
		if (error)
			return error;
	}

	/*
	 * Record all the incore unlinked inodes in iunlink_bmp that we didn't
	 * find by walking the ondisk iunlink buckets.  This shouldn't happen,
	 * but we can't risk forgetting an inode somewhere.
	 */
	error = xrep_iunlink_mark_incore(ragi);
	if (error)
		return error;

	/*
	 * If there are ondisk inodes that are unlinked and are not been loaded
	 * into cache, record them in iunlink_bmp.
	 */
	xrep_iunlink_mark_ondisk(ragi);

	/*
	 * Walk each iunlink bucket to (re)construct as much of the incore list
	 * as would be correct.  For each inode that survives this step, mark
	 * it clear in iunlink_bmp; we're done with those inodes.
	 */
	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		error = xrep_iunlink_resolve_bucket(ragi, i);
		if (error)
			return error;
	}

	/*
	 * Any unlinked inodes that we didn't find through the bucket list
	 * walk (or was ignored by the walk) must be inserted into the bucket
	 * list.  Stage this in memory for now.
	 */
	return xagino_bitmap_walk(&ragi->iunlink_bmp,
			xrep_iunlink_add_lost_inodes, ragi);
}

/* Update i_next_iunlinked for the inode @agino. */
STATIC int
xrep_iunlink_relink_next(
	struct xrep_agi		*ragi,
	xfarray_idx_t		idx,
	xfs_agino_t		next_agino)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_inode	*ip;
	xfarray_idx_t		agino = idx - 1;
	bool			want_rele = false;
	int			error = 0;

	ip = xfs_iunlink_lookup(pag, agino);
	if (!ip) {
		xfs_ino_t	ino;
		xfs_agino_t	prev_agino;

		/*
		 * No inode exists in cache.  Load it off the disk so that we
		 * can reinsert it into the incore unlinked list.
		 */
		ino = XFS_AGINO_TO_INO(sc->mp, pag->pag_agno, agino);
		error = xchk_iget(sc, ino, &ip);
		if (error)
			return -EFSCORRUPTED;

		want_rele = true;

		/* Set the backward pointer since this just came off disk. */
		error = xfarray_load(ragi->iunlink_prev, agino, &prev_agino);
		if (error)
			goto out_rele;

		trace_xrep_iunlink_relink_prev(ip, prev_agino);
		ip->i_prev_unlinked = prev_agino;
	}

	/* Update the forward pointer. */
	if (ip->i_next_unlinked != next_agino) {
		error = xfs_iunlink_log_inode(sc->tp, ip, pag, next_agino);
		if (error)
			goto out_rele;

		trace_xrep_iunlink_relink_next(ip, next_agino);
		ip->i_next_unlinked = next_agino;
	}

out_rele:
	/*
	 * The iunlink lookup doesn't igrab because we hold the AGI buffer lock
	 * and the inode cannot be reclaimed.  However, if we used iget to load
	 * a missing inode, we must irele it here.
	 */
	if (want_rele)
		xchk_irele(sc, ip);
	return error;
}

/* Update i_prev_iunlinked for the inode @agino. */
STATIC int
xrep_iunlink_relink_prev(
	struct xrep_agi		*ragi,
	xfarray_idx_t		idx,
	xfs_agino_t		prev_agino)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_inode	*ip;
	xfarray_idx_t		agino = idx - 1;
	bool			want_rele = false;
	int			error = 0;

	ASSERT(prev_agino != 0);

	ip = xfs_iunlink_lookup(pag, agino);
	if (!ip) {
		xfs_ino_t	ino;
		xfs_agino_t	next_agino;

		/*
		 * No inode exists in cache.  Load it off the disk so that we
		 * can reinsert it into the incore unlinked list.
		 */
		ino = XFS_AGINO_TO_INO(sc->mp, pag->pag_agno, agino);
		error = xchk_iget(sc, ino, &ip);
		if (error)
			return -EFSCORRUPTED;

		want_rele = true;

		/* Set the forward pointer since this just came off disk. */
		error = xfarray_load(ragi->iunlink_prev, agino, &next_agino);
		if (error)
			goto out_rele;

		error = xfs_iunlink_log_inode(sc->tp, ip, pag, next_agino);
		if (error)
			goto out_rele;

		trace_xrep_iunlink_relink_next(ip, next_agino);
		ip->i_next_unlinked = next_agino;
	}

	/* Update the backward pointer. */
	if (ip->i_prev_unlinked != prev_agino) {
		trace_xrep_iunlink_relink_prev(ip, prev_agino);
		ip->i_prev_unlinked = prev_agino;
	}

out_rele:
	/*
	 * The iunlink lookup doesn't igrab because we hold the AGI buffer lock
	 * and the inode cannot be reclaimed.  However, if we used iget to load
	 * a missing inode, we must irele it here.
	 */
	if (want_rele)
		xchk_irele(sc, ip);
	return error;
}

/* Log all the iunlink updates we need to finish regenerating the AGI. */
STATIC int
xrep_iunlink_commit(
	struct xrep_agi		*ragi)
{
	struct xfs_agi		*agi = ragi->agi_bp->b_addr;
	xfarray_idx_t		idx = XFARRAY_CURSOR_INIT;
	xfs_agino_t		agino;
	unsigned int		i;
	int			error;

	/* Fix all the forward links */
	while ((error = xfarray_iter(ragi->iunlink_next, &idx, &agino)) == 1) {
		error = xrep_iunlink_relink_next(ragi, idx, agino);
		if (error)
			return error;
	}

	/* Fix all the back links */
	idx = XFARRAY_CURSOR_INIT;
	while ((error = xfarray_iter(ragi->iunlink_prev, &idx, &agino)) == 1) {
		error = xrep_iunlink_relink_prev(ragi, idx, agino);
		if (error)
			return error;
	}

	/* Copy the staged iunlink buckets to the new AGI. */
	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		trace_xrep_iunlink_commit_bucket(ragi->sc->sa.pag, i,
				be32_to_cpu(ragi->old_agi.agi_unlinked[i]),
				ragi->iunlink_heads[i]);

		agi->agi_unlinked[i] = cpu_to_be32(ragi->iunlink_heads[i]);
	}

	return 0;
}

/* Trigger reinitialization of the in-core data. */
STATIC int
xrep_agi_commit_new(
	struct xrep_agi		*ragi)
{
	struct xfs_scrub	*sc = ragi->sc;
	struct xfs_buf		*agi_bp = ragi->agi_bp;
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
	set_bit(XFS_AGSTATE_AGI_INIT, &pag->pag_opstate);

	return xrep_roll_ag_trans(sc);
}

/* Repair the AGI. */
int
xrep_agi(
	struct xfs_scrub	*sc)
{
	struct xrep_agi		*ragi;
	struct xfs_mount	*mp = sc->mp;
	char			*descr;
	unsigned int		i;
	int			error;

	/* We require the rmapbt to rebuild anything. */
	if (!xfs_has_rmapbt(mp))
		return -EOPNOTSUPP;

	sc->buf = kzalloc(sizeof(struct xrep_agi), XCHK_GFP_FLAGS);
	if (!sc->buf)
		return -ENOMEM;
	ragi = sc->buf;
	ragi->sc = sc;

	ragi->fab[XREP_AGI_INOBT] = (struct xrep_find_ag_btree){
		.rmap_owner	= XFS_RMAP_OWN_INOBT,
		.buf_ops	= &xfs_inobt_buf_ops,
		.maxlevels	= M_IGEO(sc->mp)->inobt_maxlevels,
	};
	ragi->fab[XREP_AGI_FINOBT] = (struct xrep_find_ag_btree){
		.rmap_owner	= XFS_RMAP_OWN_INOBT,
		.buf_ops	= &xfs_finobt_buf_ops,
		.maxlevels	= M_IGEO(sc->mp)->inobt_maxlevels,
	};
	ragi->fab[XREP_AGI_END] = (struct xrep_find_ag_btree){
		.buf_ops	= NULL,
	};

	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++)
		ragi->iunlink_heads[i] = NULLAGINO;

	xagino_bitmap_init(&ragi->iunlink_bmp);
	sc->buf_cleanup = xrep_agi_buf_cleanup;

	descr = xchk_xfile_ag_descr(sc, "iunlinked next pointers");
	error = xfarray_create(descr, 0, sizeof(xfs_agino_t),
			&ragi->iunlink_next);
	kfree(descr);
	if (error)
		return error;

	descr = xchk_xfile_ag_descr(sc, "iunlinked prev pointers");
	error = xfarray_create(descr, 0, sizeof(xfs_agino_t),
			&ragi->iunlink_prev);
	kfree(descr);
	if (error)
		return error;

	/*
	 * Make sure we have the AGI buffer, as scrub might have decided it
	 * was corrupt after xfs_ialloc_read_agi failed with -EFSCORRUPTED.
	 */
	error = xfs_trans_read_buf(mp, sc->tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, sc->sa.pag->pag_agno,
						XFS_AGI_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, &ragi->agi_bp, NULL);
	if (error)
		return error;
	ragi->agi_bp->b_ops = &xfs_agi_buf_ops;

	/* Find the AGI btree roots. */
	error = xrep_agi_find_btrees(ragi);
	if (error)
		return error;

	error = xrep_iunlink_rebuild_buckets(ragi);
	if (error)
		return error;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		return error;

	/* Start rewriting the header and implant the btrees we found. */
	xrep_agi_init_header(ragi);
	xrep_agi_set_roots(ragi);
	error = xrep_agi_calc_from_btrees(ragi);
	if (error)
		goto out_revert;
	error = xrep_iunlink_commit(ragi);
	if (error)
		goto out_revert;

	/* Reinitialize in-core state. */
	return xrep_agi_commit_new(ragi);

out_revert:
	/* Mark the incore AGI state stale and revert the AGI. */
	clear_bit(XFS_AGSTATE_AGI_INIT, &sc->sa.pag->pag_opstate);
	memcpy(ragi->agi_bp->b_addr, &ragi->old_agi, sizeof(struct xfs_agi));
	return error;
}
