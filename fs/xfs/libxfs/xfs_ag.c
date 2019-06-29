/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2018 Red Hat, Inc.
 * All rights reserved.
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_rmap_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_health.h"

static struct xfs_buf *
xfs_get_aghdr_buf(
	struct xfs_mount	*mp,
	xfs_daddr_t		blkno,
	size_t			numblks,
	int			flags,
	const struct xfs_buf_ops *ops)
{
	struct xfs_buf		*bp;

	bp = xfs_buf_get_uncached(mp->m_ddev_targp, numblks, flags);
	if (!bp)
		return NULL;

	xfs_buf_zero(bp, 0, BBTOB(bp->b_length));
	bp->b_bn = blkno;
	bp->b_maps[0].bm_bn = blkno;
	bp->b_ops = ops;

	return bp;
}

/*
 * Generic btree root block init function
 */
static void
xfs_btroot_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	xfs_btree_init_block(mp, bp, id->type, 0, 0, id->agno);
}

/* Finish initializing a free space btree. */
static void
xfs_freesp_init_recs(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_alloc_rec	*arec;

	arec = XFS_ALLOC_REC_ADDR(mp, XFS_BUF_TO_BLOCK(bp), 1);
	arec->ar_startblock = cpu_to_be32(mp->m_ag_prealloc_blocks);
	arec->ar_blockcount = cpu_to_be32(id->agsize -
					  be32_to_cpu(arec->ar_startblock));
}

/*
 * Alloc btree root block init functions
 */
static void
xfs_bnoroot_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	xfs_btree_init_block(mp, bp, XFS_BTNUM_BNO, 0, 1, id->agno);
	xfs_freesp_init_recs(mp, bp, id);
}

static void
xfs_cntroot_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	xfs_btree_init_block(mp, bp, XFS_BTNUM_CNT, 0, 1, id->agno);
	xfs_freesp_init_recs(mp, bp, id);
}

/*
 * Reverse map root block init
 */
static void
xfs_rmaproot_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_btree_block	*block = XFS_BUF_TO_BLOCK(bp);
	struct xfs_rmap_rec	*rrec;

	xfs_btree_init_block(mp, bp, XFS_BTNUM_RMAP, 0, 4, id->agno);

	/*
	 * mark the AG header regions as static metadata The BNO
	 * btree block is the first block after the headers, so
	 * it's location defines the size of region the static
	 * metadata consumes.
	 *
	 * Note: unlike mkfs, we never have to account for log
	 * space when growing the data regions
	 */
	rrec = XFS_RMAP_REC_ADDR(block, 1);
	rrec->rm_startblock = 0;
	rrec->rm_blockcount = cpu_to_be32(XFS_BNO_BLOCK(mp));
	rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_FS);
	rrec->rm_offset = 0;

	/* account freespace btree root blocks */
	rrec = XFS_RMAP_REC_ADDR(block, 2);
	rrec->rm_startblock = cpu_to_be32(XFS_BNO_BLOCK(mp));
	rrec->rm_blockcount = cpu_to_be32(2);
	rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_AG);
	rrec->rm_offset = 0;

	/* account inode btree root blocks */
	rrec = XFS_RMAP_REC_ADDR(block, 3);
	rrec->rm_startblock = cpu_to_be32(XFS_IBT_BLOCK(mp));
	rrec->rm_blockcount = cpu_to_be32(XFS_RMAP_BLOCK(mp) -
					  XFS_IBT_BLOCK(mp));
	rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_INOBT);
	rrec->rm_offset = 0;

	/* account for rmap btree root */
	rrec = XFS_RMAP_REC_ADDR(block, 4);
	rrec->rm_startblock = cpu_to_be32(XFS_RMAP_BLOCK(mp));
	rrec->rm_blockcount = cpu_to_be32(1);
	rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_AG);
	rrec->rm_offset = 0;

	/* account for refc btree root */
	if (xfs_sb_version_hasreflink(&mp->m_sb)) {
		rrec = XFS_RMAP_REC_ADDR(block, 5);
		rrec->rm_startblock = cpu_to_be32(xfs_refc_block(mp));
		rrec->rm_blockcount = cpu_to_be32(1);
		rrec->rm_owner = cpu_to_be64(XFS_RMAP_OWN_REFC);
		rrec->rm_offset = 0;
		be16_add_cpu(&block->bb_numrecs, 1);
	}
}

/*
 * Initialise new secondary superblocks with the pre-grow geometry, but mark
 * them as "in progress" so we know they haven't yet been activated. This will
 * get cleared when the update with the new geometry information is done after
 * changes to the primary are committed. This isn't strictly necessary, but we
 * get it for free with the delayed buffer write lists and it means we can tell
 * if a grow operation didn't complete properly after the fact.
 */
static void
xfs_sbblock_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_dsb		*dsb = XFS_BUF_TO_SBP(bp);

	xfs_sb_to_disk(dsb, &mp->m_sb);
	dsb->sb_inprogress = 1;
}

static void
xfs_agfblock_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_agf		*agf = XFS_BUF_TO_AGF(bp);
	xfs_extlen_t		tmpsize;

	agf->agf_magicnum = cpu_to_be32(XFS_AGF_MAGIC);
	agf->agf_versionnum = cpu_to_be32(XFS_AGF_VERSION);
	agf->agf_seqno = cpu_to_be32(id->agno);
	agf->agf_length = cpu_to_be32(id->agsize);
	agf->agf_roots[XFS_BTNUM_BNOi] = cpu_to_be32(XFS_BNO_BLOCK(mp));
	agf->agf_roots[XFS_BTNUM_CNTi] = cpu_to_be32(XFS_CNT_BLOCK(mp));
	agf->agf_levels[XFS_BTNUM_BNOi] = cpu_to_be32(1);
	agf->agf_levels[XFS_BTNUM_CNTi] = cpu_to_be32(1);
	if (xfs_sb_version_hasrmapbt(&mp->m_sb)) {
		agf->agf_roots[XFS_BTNUM_RMAPi] =
					cpu_to_be32(XFS_RMAP_BLOCK(mp));
		agf->agf_levels[XFS_BTNUM_RMAPi] = cpu_to_be32(1);
		agf->agf_rmap_blocks = cpu_to_be32(1);
	}

	agf->agf_flfirst = cpu_to_be32(1);
	agf->agf_fllast = 0;
	agf->agf_flcount = 0;
	tmpsize = id->agsize - mp->m_ag_prealloc_blocks;
	agf->agf_freeblks = cpu_to_be32(tmpsize);
	agf->agf_longest = cpu_to_be32(tmpsize);
	if (xfs_sb_version_hascrc(&mp->m_sb))
		uuid_copy(&agf->agf_uuid, &mp->m_sb.sb_meta_uuid);
	if (xfs_sb_version_hasreflink(&mp->m_sb)) {
		agf->agf_refcount_root = cpu_to_be32(
				xfs_refc_block(mp));
		agf->agf_refcount_level = cpu_to_be32(1);
		agf->agf_refcount_blocks = cpu_to_be32(1);
	}
}

static void
xfs_agflblock_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_agfl		*agfl = XFS_BUF_TO_AGFL(bp);
	__be32			*agfl_bno;
	int			bucket;

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		agfl->agfl_magicnum = cpu_to_be32(XFS_AGFL_MAGIC);
		agfl->agfl_seqno = cpu_to_be32(id->agno);
		uuid_copy(&agfl->agfl_uuid, &mp->m_sb.sb_meta_uuid);
	}

	agfl_bno = XFS_BUF_TO_AGFL_BNO(mp, bp);
	for (bucket = 0; bucket < xfs_agfl_size(mp); bucket++)
		agfl_bno[bucket] = cpu_to_be32(NULLAGBLOCK);
}

static void
xfs_agiblock_init(
	struct xfs_mount	*mp,
	struct xfs_buf		*bp,
	struct aghdr_init_data	*id)
{
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(bp);
	int			bucket;

	agi->agi_magicnum = cpu_to_be32(XFS_AGI_MAGIC);
	agi->agi_versionnum = cpu_to_be32(XFS_AGI_VERSION);
	agi->agi_seqno = cpu_to_be32(id->agno);
	agi->agi_length = cpu_to_be32(id->agsize);
	agi->agi_count = 0;
	agi->agi_root = cpu_to_be32(XFS_IBT_BLOCK(mp));
	agi->agi_level = cpu_to_be32(1);
	agi->agi_freecount = 0;
	agi->agi_newino = cpu_to_be32(NULLAGINO);
	agi->agi_dirino = cpu_to_be32(NULLAGINO);
	if (xfs_sb_version_hascrc(&mp->m_sb))
		uuid_copy(&agi->agi_uuid, &mp->m_sb.sb_meta_uuid);
	if (xfs_sb_version_hasfinobt(&mp->m_sb)) {
		agi->agi_free_root = cpu_to_be32(XFS_FIBT_BLOCK(mp));
		agi->agi_free_level = cpu_to_be32(1);
	}
	for (bucket = 0; bucket < XFS_AGI_UNLINKED_BUCKETS; bucket++)
		agi->agi_unlinked[bucket] = cpu_to_be32(NULLAGINO);
}

typedef void (*aghdr_init_work_f)(struct xfs_mount *mp, struct xfs_buf *bp,
				  struct aghdr_init_data *id);
static int
xfs_ag_init_hdr(
	struct xfs_mount	*mp,
	struct aghdr_init_data	*id,
	aghdr_init_work_f	work,
	const struct xfs_buf_ops *ops)

{
	struct xfs_buf		*bp;

	bp = xfs_get_aghdr_buf(mp, id->daddr, id->numblks, 0, ops);
	if (!bp)
		return -ENOMEM;

	(*work)(mp, bp, id);

	xfs_buf_delwri_queue(bp, &id->buffer_list);
	xfs_buf_relse(bp);
	return 0;
}

struct xfs_aghdr_grow_data {
	xfs_daddr_t		daddr;
	size_t			numblks;
	const struct xfs_buf_ops *ops;
	aghdr_init_work_f	work;
	xfs_btnum_t		type;
	bool			need_init;
};

/*
 * Prepare new AG headers to be written to disk. We use uncached buffers here,
 * as it is assumed these new AG headers are currently beyond the currently
 * valid filesystem address space. Using cached buffers would trip over EOFS
 * corruption detection alogrithms in the buffer cache lookup routines.
 *
 * This is a non-transactional function, but the prepared buffers are added to a
 * delayed write buffer list supplied by the caller so they can submit them to
 * disk and wait on them as required.
 */
int
xfs_ag_init_headers(
	struct xfs_mount	*mp,
	struct aghdr_init_data	*id)

{
	struct xfs_aghdr_grow_data aghdr_data[] = {
	{ /* SB */
		.daddr = XFS_AG_DADDR(mp, id->agno, XFS_SB_DADDR),
		.numblks = XFS_FSS_TO_BB(mp, 1),
		.ops = &xfs_sb_buf_ops,
		.work = &xfs_sbblock_init,
		.need_init = true
	},
	{ /* AGF */
		.daddr = XFS_AG_DADDR(mp, id->agno, XFS_AGF_DADDR(mp)),
		.numblks = XFS_FSS_TO_BB(mp, 1),
		.ops = &xfs_agf_buf_ops,
		.work = &xfs_agfblock_init,
		.need_init = true
	},
	{ /* AGFL */
		.daddr = XFS_AG_DADDR(mp, id->agno, XFS_AGFL_DADDR(mp)),
		.numblks = XFS_FSS_TO_BB(mp, 1),
		.ops = &xfs_agfl_buf_ops,
		.work = &xfs_agflblock_init,
		.need_init = true
	},
	{ /* AGI */
		.daddr = XFS_AG_DADDR(mp, id->agno, XFS_AGI_DADDR(mp)),
		.numblks = XFS_FSS_TO_BB(mp, 1),
		.ops = &xfs_agi_buf_ops,
		.work = &xfs_agiblock_init,
		.need_init = true
	},
	{ /* BNO root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_BNO_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_bnobt_buf_ops,
		.work = &xfs_bnoroot_init,
		.need_init = true
	},
	{ /* CNT root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_CNT_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_cntbt_buf_ops,
		.work = &xfs_cntroot_init,
		.need_init = true
	},
	{ /* INO root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_IBT_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_inobt_buf_ops,
		.work = &xfs_btroot_init,
		.type = XFS_BTNUM_INO,
		.need_init = true
	},
	{ /* FINO root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_FIBT_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_finobt_buf_ops,
		.work = &xfs_btroot_init,
		.type = XFS_BTNUM_FINO,
		.need_init =  xfs_sb_version_hasfinobt(&mp->m_sb)
	},
	{ /* RMAP root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, XFS_RMAP_BLOCK(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_rmapbt_buf_ops,
		.work = &xfs_rmaproot_init,
		.need_init = xfs_sb_version_hasrmapbt(&mp->m_sb)
	},
	{ /* REFC root block */
		.daddr = XFS_AGB_TO_DADDR(mp, id->agno, xfs_refc_block(mp)),
		.numblks = BTOBB(mp->m_sb.sb_blocksize),
		.ops = &xfs_refcountbt_buf_ops,
		.work = &xfs_btroot_init,
		.type = XFS_BTNUM_REFC,
		.need_init = xfs_sb_version_hasreflink(&mp->m_sb)
	},
	{ /* NULL terminating block */
		.daddr = XFS_BUF_DADDR_NULL,
	}
	};
	struct  xfs_aghdr_grow_data *dp;
	int			error = 0;

	/* Account for AG free space in new AG */
	id->nfree += id->agsize - mp->m_ag_prealloc_blocks;
	for (dp = &aghdr_data[0]; dp->daddr != XFS_BUF_DADDR_NULL; dp++) {
		if (!dp->need_init)
			continue;

		id->daddr = dp->daddr;
		id->numblks = dp->numblks;
		id->type = dp->type;
		error = xfs_ag_init_hdr(mp, id, dp->work, dp->ops);
		if (error)
			break;
	}
	return error;
}

/*
 * Extent the AG indicated by the @id by the length passed in
 */
int
xfs_ag_extend_space(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct aghdr_init_data	*id,
	xfs_extlen_t		len)
{
	struct xfs_buf		*bp;
	struct xfs_agi		*agi;
	struct xfs_agf		*agf;
	int			error;

	/*
	 * Change the agi length.
	 */
	error = xfs_ialloc_read_agi(mp, tp, id->agno, &bp);
	if (error)
		return error;

	agi = XFS_BUF_TO_AGI(bp);
	be32_add_cpu(&agi->agi_length, len);
	ASSERT(id->agno == mp->m_sb.sb_agcount - 1 ||
	       be32_to_cpu(agi->agi_length) == mp->m_sb.sb_agblocks);
	xfs_ialloc_log_agi(tp, bp, XFS_AGI_LENGTH);

	/*
	 * Change agf length.
	 */
	error = xfs_alloc_read_agf(mp, tp, id->agno, 0, &bp);
	if (error)
		return error;

	agf = XFS_BUF_TO_AGF(bp);
	be32_add_cpu(&agf->agf_length, len);
	ASSERT(agf->agf_length == agi->agi_length);
	xfs_alloc_log_agf(tp, bp, XFS_AGF_LENGTH);

	/*
	 * Free the new space.
	 *
	 * XFS_RMAP_OINFO_SKIP_UPDATE is used here to tell the rmap btree that
	 * this doesn't actually exist in the rmap btree.
	 */
	error = xfs_rmap_free(tp, bp, id->agno,
				be32_to_cpu(agf->agf_length) - len,
				len, &XFS_RMAP_OINFO_SKIP_UPDATE);
	if (error)
		return error;

	return  xfs_free_extent(tp, XFS_AGB_TO_FSB(mp, id->agno,
					be32_to_cpu(agf->agf_length) - len),
				len, &XFS_RMAP_OINFO_SKIP_UPDATE,
				XFS_AG_RESV_NONE);
}

/* Retrieve AG geometry. */
int
xfs_ag_get_geometry(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	struct xfs_ag_geometry	*ageo)
{
	struct xfs_buf		*agi_bp;
	struct xfs_buf		*agf_bp;
	struct xfs_agi		*agi;
	struct xfs_agf		*agf;
	struct xfs_perag	*pag;
	unsigned int		freeblks;
	int			error;

	if (agno >= mp->m_sb.sb_agcount)
		return -EINVAL;

	/* Lock the AG headers. */
	error = xfs_ialloc_read_agi(mp, NULL, agno, &agi_bp);
	if (error)
		return error;
	error = xfs_alloc_read_agf(mp, NULL, agno, 0, &agf_bp);
	if (error)
		goto out_agi;
	pag = xfs_perag_get(mp, agno);

	/* Fill out form. */
	memset(ageo, 0, sizeof(*ageo));
	ageo->ag_number = agno;

	agi = XFS_BUF_TO_AGI(agi_bp);
	ageo->ag_icount = be32_to_cpu(agi->agi_count);
	ageo->ag_ifree = be32_to_cpu(agi->agi_freecount);

	agf = XFS_BUF_TO_AGF(agf_bp);
	ageo->ag_length = be32_to_cpu(agf->agf_length);
	freeblks = pag->pagf_freeblks +
		   pag->pagf_flcount +
		   pag->pagf_btreeblks -
		   xfs_ag_resv_needed(pag, XFS_AG_RESV_NONE);
	ageo->ag_freeblks = freeblks;
	xfs_ag_geom_health(pag, ageo);

	/* Release resources. */
	xfs_perag_put(pag);
	xfs_buf_relse(agf_bp);
out_agi:
	xfs_buf_relse(agi_bp);
	return error;
}
