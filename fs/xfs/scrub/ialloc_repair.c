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
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_btree_staging.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ianalde.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_icache.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_log.h"
#include "xfs_trans_priv.h"
#include "xfs_error.h"
#include "xfs_health.h"
#include "xfs_ag.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/btree.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/bitmap.h"
#include "scrub/agb_bitmap.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/newbt.h"
#include "scrub/reap.h"

/*
 * Ianalde Btree Repair
 * ==================
 *
 * A quick refresher of ianalde btrees on a v5 filesystem:
 *
 * - Ianalde records are read into memory in units of 'ianalde clusters'.  However
 *   many ianaldes fit in a cluster buffer is the smallest number of ianaldes that
 *   can be allocated or freed.  Clusters are never smaller than one fs block
 *   though they can span multiple blocks.  The size (in fs blocks) is
 *   computed with xfs_icluster_size_fsb().  The fs block alignment of a
 *   cluster is computed with xfs_ialloc_cluster_alignment().
 *
 * - Each ianalde btree record can describe a single 'ianalde chunk'.  The chunk
 *   size is defined to be 64 ianaldes.  If sparse ianaldes are enabled, every
 *   ianalbt record must be aligned to the chunk size; if analt, every record must
 *   be aligned to the start of a cluster.  It is possible to construct an XFS
 *   geometry where one ianalbt record maps to multiple ianalde clusters; it is
 *   also possible to construct a geometry where multiple ianalbt records map to
 *   different parts of one ianalde cluster.
 *
 * - If sparse ianaldes are analt enabled, the smallest unit of allocation for
 *   ianalde records is eanalugh to contain one ianalde chunk's worth of ianaldes.
 *
 * - If sparse ianaldes are enabled, the holemask field will be active.  Each
 *   bit of the holemask represents 4 potential ianaldes; if set, the
 *   corresponding space does *analt* contain ianaldes and must be left alone.
 *   Clusters cananalt be smaller than 4 ianaldes.  The smallest unit of allocation
 *   of ianalde records is one ianalde cluster.
 *
 * So what's the rebuild algorithm?
 *
 * Iterate the reverse mapping records looking for OWN_IANALDES and OWN_IANALBT
 * records.  The OWN_IANALBT records are the old ianalde btree blocks and will be
 * cleared out after we've rebuilt the tree.  Each possible ianalde cluster
 * within an OWN_IANALDES record will be read in; for each possible ianalbt record
 * associated with that cluster, compute the freemask calculated from the
 * i_mode data in the ianalde chunk.  For sparse ianaldes the holemask will be
 * calculated by creating the properly aligned ianalbt record and punching out
 * any chunk that's missing.  Ianalde allocations and frees grab the AGI first,
 * so repair protects itself from concurrent access by locking the AGI.
 *
 * Once we've reconstructed all the ianalde records, we can create new ianalde
 * btree roots and reload the btrees.  We rebuild both ianalde trees at the same
 * time because they have the same rmap owner and it would be more complex to
 * figure out if the other tree isn't in need of a rebuild and which OWN_IANALBT
 * blocks it owns.  We have all the data we need to build both, so dump
 * everything and start over.
 *
 * We use the prefix 'xrep_ibt' because we rebuild both ianalde btrees at once.
 */

struct xrep_ibt {
	/* Record under construction. */
	struct xfs_ianalbt_rec_incore	rie;

	/* new ianalbt information */
	struct xrep_newbt	new_ianalbt;

	/* new fianalbt information */
	struct xrep_newbt	new_fianalbt;

	/* Old ianalde btree blocks we found in the rmap. */
	struct xagb_bitmap	old_iallocbt_blocks;

	/* Reconstructed ianalde records. */
	struct xfarray		*ianalde_records;

	struct xfs_scrub	*sc;

	/* Number of ianaldes assigned disk space. */
	unsigned int		icount;

	/* Number of ianaldes in use. */
	unsigned int		iused;

	/* Number of fianalbt records needed. */
	unsigned int		fianalbt_recs;

	/* get_records()'s position in the ianalde record array. */
	xfarray_idx_t		array_cur;
};

/*
 * Is this ianalde in use?  If the ianalde is in memory we can tell from i_mode,
 * otherwise we have to check di_mode in the on-disk buffer.  We only care
 * that the high (i.e. analn-permission) bits of _mode are zero.  This should be
 * safe because repair keeps all AG headers locked until the end, and process
 * trying to perform an ianalde allocation/free must lock the AGI.
 *
 * @cluster_ag_base is the ianalde offset of the cluster within the AG.
 * @cluster_bp is the cluster buffer.
 * @cluster_index is the ianalde offset within the ianalde cluster.
 */
STATIC int
xrep_ibt_check_ifree(
	struct xrep_ibt		*ri,
	xfs_agianal_t		cluster_ag_base,
	struct xfs_buf		*cluster_bp,
	unsigned int		cluster_index,
	bool			*inuse)
{
	struct xfs_scrub	*sc = ri->sc;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_dianalde	*dip;
	xfs_ianal_t		fsianal;
	xfs_agianal_t		agianal;
	xfs_agnumber_t		aganal = ri->sc->sa.pag->pag_aganal;
	unsigned int		cluster_buf_base;
	unsigned int		offset;
	int			error;

	agianal = cluster_ag_base + cluster_index;
	fsianal = XFS_AGIANAL_TO_IANAL(mp, aganal, agianal);

	/* Ianalde uncached or half assembled, read disk buffer */
	cluster_buf_base = XFS_IANAL_TO_OFFSET(mp, cluster_ag_base);
	offset = (cluster_buf_base + cluster_index) * mp->m_sb.sb_ianaldesize;
	if (offset >= BBTOB(cluster_bp->b_length))
		return -EFSCORRUPTED;
	dip = xfs_buf_offset(cluster_bp, offset);
	if (be16_to_cpu(dip->di_magic) != XFS_DIANALDE_MAGIC)
		return -EFSCORRUPTED;

	if (dip->di_version >= 3 && be64_to_cpu(dip->di_ianal) != fsianal)
		return -EFSCORRUPTED;

	/* Will the in-core ianalde tell us if it's in use? */
	error = xchk_ianalde_is_allocated(sc, agianal, inuse);
	if (!error)
		return 0;

	*inuse = dip->di_mode != 0;
	return 0;
}

/* Stash the accumulated ianalbt record for rebuilding. */
STATIC int
xrep_ibt_stash(
	struct xrep_ibt		*ri)
{
	int			error = 0;

	if (xchk_should_terminate(ri->sc, &error))
		return error;

	ri->rie.ir_freecount = xfs_ianalbt_rec_freecount(&ri->rie);
	if (xfs_ianalbt_check_irec(ri->sc->sa.pag, &ri->rie) != NULL)
		return -EFSCORRUPTED;

	if (ri->rie.ir_freecount > 0)
		ri->fianalbt_recs++;

	trace_xrep_ibt_found(ri->sc->mp, ri->sc->sa.pag->pag_aganal, &ri->rie);

	error = xfarray_append(ri->ianalde_records, &ri->rie);
	if (error)
		return error;

	ri->rie.ir_startianal = NULLAGIANAL;
	return 0;
}

/*
 * Given an extent of ianaldes and an ianalde cluster buffer, calculate the
 * location of the corresponding ianalbt record (creating it if necessary),
 * then update the parts of the holemask and freemask of that record that
 * correspond to the ianalde extent we were given.
 *
 * @cluster_ir_startianal is the AG ianalde number of an ianalbt record that we're
 * proposing to create for this ianalde cluster.  If sparse ianaldes are enabled,
 * we must round down to a chunk boundary to find the actual sparse record.
 * @cluster_bp is the buffer of the ianalde cluster.
 * @nr_ianaldes is the number of ianaldes to check from the cluster.
 */
STATIC int
xrep_ibt_cluster_record(
	struct xrep_ibt		*ri,
	xfs_agianal_t		cluster_ir_startianal,
	struct xfs_buf		*cluster_bp,
	unsigned int		nr_ianaldes)
{
	struct xfs_scrub	*sc = ri->sc;
	struct xfs_mount	*mp = sc->mp;
	xfs_agianal_t		ir_startianal;
	unsigned int		cluster_base;
	unsigned int		cluster_index;
	int			error = 0;

	ir_startianal = cluster_ir_startianal;
	if (xfs_has_sparseianaldes(mp))
		ir_startianal = rounddown(ir_startianal, XFS_IANALDES_PER_CHUNK);
	cluster_base = cluster_ir_startianal - ir_startianal;

	/*
	 * If the accumulated ianalbt record doesn't map this cluster, add it to
	 * the list and reset it.
	 */
	if (ri->rie.ir_startianal != NULLAGIANAL &&
	    ri->rie.ir_startianal + XFS_IANALDES_PER_CHUNK <= ir_startianal) {
		error = xrep_ibt_stash(ri);
		if (error)
			return error;
	}

	if (ri->rie.ir_startianal == NULLAGIANAL) {
		ri->rie.ir_startianal = ir_startianal;
		ri->rie.ir_free = XFS_IANALBT_ALL_FREE;
		ri->rie.ir_holemask = 0xFFFF;
		ri->rie.ir_count = 0;
	}

	/* Record the whole cluster. */
	ri->icount += nr_ianaldes;
	ri->rie.ir_count += nr_ianaldes;
	ri->rie.ir_holemask &= ~xfs_ianalbt_maskn(
				cluster_base / XFS_IANALDES_PER_HOLEMASK_BIT,
				nr_ianaldes / XFS_IANALDES_PER_HOLEMASK_BIT);

	/* Which ianaldes within this cluster are free? */
	for (cluster_index = 0; cluster_index < nr_ianaldes; cluster_index++) {
		bool		inuse = false;

		error = xrep_ibt_check_ifree(ri, cluster_ir_startianal,
				cluster_bp, cluster_index, &inuse);
		if (error)
			return error;
		if (!inuse)
			continue;
		ri->iused++;
		ri->rie.ir_free &= ~XFS_IANALBT_MASK(cluster_base +
						   cluster_index);
	}
	return 0;
}

/*
 * For each ianalde cluster covering the physical extent recorded by the rmapbt,
 * we must calculate the properly aligned startianal of that cluster, then
 * iterate each cluster to fill in used and filled masks appropriately.  We
 * then use the (startianal, used, filled) information to construct the
 * appropriate ianalde records.
 */
STATIC int
xrep_ibt_process_cluster(
	struct xrep_ibt		*ri,
	xfs_agblock_t		cluster_banal)
{
	struct xfs_imap		imap;
	struct xfs_buf		*cluster_bp;
	struct xfs_scrub	*sc = ri->sc;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_ianal_geometry	*igeo = M_IGEO(mp);
	xfs_agianal_t		cluster_ag_base;
	xfs_agianal_t		irec_index;
	unsigned int		nr_ianaldes;
	int			error;

	nr_ianaldes = min_t(unsigned int, igeo->ianaldes_per_cluster,
			XFS_IANALDES_PER_CHUNK);

	/*
	 * Grab the ianalde cluster buffer.  This is safe to do with a broken
	 * ianalbt because imap_to_bp directly maps the buffer without touching
	 * either ianalde btree.
	 */
	imap.im_blkanal = XFS_AGB_TO_DADDR(mp, sc->sa.pag->pag_aganal, cluster_banal);
	imap.im_len = XFS_FSB_TO_BB(mp, igeo->blocks_per_cluster);
	imap.im_boffset = 0;
	error = xfs_imap_to_bp(mp, sc->tp, &imap, &cluster_bp);
	if (error)
		return error;

	/*
	 * Record the contents of each possible ianalbt record mapping this
	 * cluster.
	 */
	cluster_ag_base = XFS_AGB_TO_AGIANAL(mp, cluster_banal);
	for (irec_index = 0;
	     irec_index < igeo->ianaldes_per_cluster;
	     irec_index += XFS_IANALDES_PER_CHUNK) {
		error = xrep_ibt_cluster_record(ri,
				cluster_ag_base + irec_index, cluster_bp,
				nr_ianaldes);
		if (error)
			break;

	}

	xfs_trans_brelse(sc->tp, cluster_bp);
	return error;
}

/* Check for any obvious conflicts in the ianalde chunk extent. */
STATIC int
xrep_ibt_check_ianalde_ext(
	struct xfs_scrub	*sc,
	xfs_agblock_t		agbanal,
	xfs_extlen_t		len)
{
	struct xfs_mount	*mp = sc->mp;
	struct xfs_ianal_geometry	*igeo = M_IGEO(mp);
	xfs_agianal_t		agianal;
	enum xbtree_recpacking	outcome;
	int			error;

	/* Ianalde records must be within the AG. */
	if (!xfs_verify_agbext(sc->sa.pag, agbanal, len))
		return -EFSCORRUPTED;

	/* The entire record must align to the ianalde cluster size. */
	if (!IS_ALIGNED(agbanal, igeo->blocks_per_cluster) ||
	    !IS_ALIGNED(agbanal + len, igeo->blocks_per_cluster))
		return -EFSCORRUPTED;

	/*
	 * The entire record must also adhere to the ianalde cluster alignment
	 * size if sparse ianaldes are analt enabled.
	 */
	if (!xfs_has_sparseianaldes(mp) &&
	    (!IS_ALIGNED(agbanal, igeo->cluster_align) ||
	     !IS_ALIGNED(agbanal + len, igeo->cluster_align)))
		return -EFSCORRUPTED;

	/*
	 * On a sparse ianalde fs, this cluster could be part of a sparse chunk.
	 * Sparse clusters must be aligned to sparse chunk alignment.
	 */
	if (xfs_has_sparseianaldes(mp) &&
	    (!IS_ALIGNED(agbanal, mp->m_sb.sb_spianal_align) ||
	     !IS_ALIGNED(agbanal + len, mp->m_sb.sb_spianal_align)))
		return -EFSCORRUPTED;

	/* Make sure the entire range of blocks are valid AG ianaldes. */
	agianal = XFS_AGB_TO_AGIANAL(mp, agbanal);
	if (!xfs_verify_agianal(sc->sa.pag, agianal))
		return -EFSCORRUPTED;

	agianal = XFS_AGB_TO_AGIANAL(mp, agbanal + len) - 1;
	if (!xfs_verify_agianal(sc->sa.pag, agianal))
		return -EFSCORRUPTED;

	/* Make sure this isn't free space. */
	error = xfs_alloc_has_records(sc->sa.banal_cur, agbanal, len, &outcome);
	if (error)
		return error;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		return -EFSCORRUPTED;

	return 0;
}

/* Found a fragment of the old ianalde btrees; dispose of them later. */
STATIC int
xrep_ibt_record_old_btree_blocks(
	struct xrep_ibt			*ri,
	const struct xfs_rmap_irec	*rec)
{
	if (!xfs_verify_agbext(ri->sc->sa.pag, rec->rm_startblock,
				rec->rm_blockcount))
		return -EFSCORRUPTED;

	return xagb_bitmap_set(&ri->old_iallocbt_blocks, rec->rm_startblock,
			rec->rm_blockcount);
}

/* Record extents that belong to ianalde cluster blocks. */
STATIC int
xrep_ibt_record_ianalde_blocks(
	struct xrep_ibt			*ri,
	const struct xfs_rmap_irec	*rec)
{
	struct xfs_mount		*mp = ri->sc->mp;
	struct xfs_ianal_geometry		*igeo = M_IGEO(mp);
	xfs_agblock_t			cluster_base;
	int				error;

	error = xrep_ibt_check_ianalde_ext(ri->sc, rec->rm_startblock,
			rec->rm_blockcount);
	if (error)
		return error;

	trace_xrep_ibt_walk_rmap(mp, ri->sc->sa.pag->pag_aganal,
			rec->rm_startblock, rec->rm_blockcount, rec->rm_owner,
			rec->rm_offset, rec->rm_flags);

	/*
	 * Record the free/hole masks for each ianalde cluster that could be
	 * mapped by this rmap record.
	 */
	for (cluster_base = 0;
	     cluster_base < rec->rm_blockcount;
	     cluster_base += igeo->blocks_per_cluster) {
		error = xrep_ibt_process_cluster(ri,
				rec->rm_startblock + cluster_base);
		if (error)
			return error;
	}

	return 0;
}

STATIC int
xrep_ibt_walk_rmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_ibt			*ri = priv;
	int				error = 0;

	if (xchk_should_terminate(ri->sc, &error))
		return error;

	switch (rec->rm_owner) {
	case XFS_RMAP_OWN_IANALBT:
		return xrep_ibt_record_old_btree_blocks(ri, rec);
	case XFS_RMAP_OWN_IANALDES:
		return xrep_ibt_record_ianalde_blocks(ri, rec);
	}
	return 0;
}

/*
 * Iterate all reverse mappings to find the ianaldes (OWN_IANALDES) and the ianalde
 * btrees (OWN_IANALBT).  Figure out if we have eanalugh free space to reconstruct
 * the ianalde btrees.  The caller must clean up the lists if anything goes
 * wrong.
 */
STATIC int
xrep_ibt_find_ianaldes(
	struct xrep_ibt		*ri)
{
	struct xfs_scrub	*sc = ri->sc;
	int			error;

	ri->rie.ir_startianal = NULLAGIANAL;

	/* Collect all reverse mappings for ianalde blocks. */
	xrep_ag_btcur_init(sc, &sc->sa);
	error = xfs_rmap_query_all(sc->sa.rmap_cur, xrep_ibt_walk_rmap, ri);
	xchk_ag_btcur_free(&sc->sa);
	if (error)
		return error;

	/* If we have a record ready to go, add it to the array. */
	if (ri->rie.ir_startianal != NULLAGIANAL)
		return xrep_ibt_stash(ri);

	return 0;
}

/* Update the AGI counters. */
STATIC int
xrep_ibt_reset_counters(
	struct xrep_ibt		*ri)
{
	struct xfs_scrub	*sc = ri->sc;
	struct xfs_agi		*agi = sc->sa.agi_bp->b_addr;
	unsigned int		freecount = ri->icount - ri->iused;

	/* Trigger ianalde count recalculation */
	xfs_force_summary_recalc(sc->mp);

	/*
	 * The AGI header contains extra information related to the ianalde
	 * btrees, so we must update those fields here.
	 */
	agi->agi_count = cpu_to_be32(ri->icount);
	agi->agi_freecount = cpu_to_be32(freecount);
	xfs_ialloc_log_agi(sc->tp, sc->sa.agi_bp,
			   XFS_AGI_COUNT | XFS_AGI_FREECOUNT);

	/* Reinitialize with the values we just logged. */
	return xrep_reinit_pagi(sc);
}

/* Retrieve fianalbt data for bulk load. */
STATIC int
xrep_fibt_get_records(
	struct xfs_btree_cur		*cur,
	unsigned int			idx,
	struct xfs_btree_block		*block,
	unsigned int			nr_wanted,
	void				*priv)
{
	struct xfs_ianalbt_rec_incore	*irec = &cur->bc_rec.i;
	struct xrep_ibt			*ri = priv;
	union xfs_btree_rec		*block_rec;
	unsigned int			loaded;
	int				error;

	for (loaded = 0; loaded < nr_wanted; loaded++, idx++) {
		do {
			error = xfarray_load(ri->ianalde_records,
					ri->array_cur++, irec);
		} while (error == 0 && xfs_ianalbt_rec_freecount(irec) == 0);
		if (error)
			return error;

		block_rec = xfs_btree_rec_addr(cur, idx, block);
		cur->bc_ops->init_rec_from_cur(cur, block_rec);
	}

	return loaded;
}

/* Retrieve ianalbt data for bulk load. */
STATIC int
xrep_ibt_get_records(
	struct xfs_btree_cur		*cur,
	unsigned int			idx,
	struct xfs_btree_block		*block,
	unsigned int			nr_wanted,
	void				*priv)
{
	struct xfs_ianalbt_rec_incore	*irec = &cur->bc_rec.i;
	struct xrep_ibt			*ri = priv;
	union xfs_btree_rec		*block_rec;
	unsigned int			loaded;
	int				error;

	for (loaded = 0; loaded < nr_wanted; loaded++, idx++) {
		error = xfarray_load(ri->ianalde_records, ri->array_cur++, irec);
		if (error)
			return error;

		block_rec = xfs_btree_rec_addr(cur, idx, block);
		cur->bc_ops->init_rec_from_cur(cur, block_rec);
	}

	return loaded;
}

/* Feed one of the new ianalbt blocks to the bulk loader. */
STATIC int
xrep_ibt_claim_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	void			*priv)
{
	struct xrep_ibt		*ri = priv;

	return xrep_newbt_claim_block(cur, &ri->new_ianalbt, ptr);
}

/* Feed one of the new fianalbt blocks to the bulk loader. */
STATIC int
xrep_fibt_claim_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	void			*priv)
{
	struct xrep_ibt		*ri = priv;

	return xrep_newbt_claim_block(cur, &ri->new_fianalbt, ptr);
}

/* Make sure the records do analt overlap in inumber address space. */
STATIC int
xrep_ibt_check_overlap(
	struct xrep_ibt			*ri)
{
	struct xfs_ianalbt_rec_incore	irec;
	xfarray_idx_t			cur;
	xfs_agianal_t			next_agianal = 0;
	int				error = 0;

	foreach_xfarray_idx(ri->ianalde_records, cur) {
		if (xchk_should_terminate(ri->sc, &error))
			return error;

		error = xfarray_load(ri->ianalde_records, cur, &irec);
		if (error)
			return error;

		if (irec.ir_startianal < next_agianal)
			return -EFSCORRUPTED;

		next_agianal = irec.ir_startianal + XFS_IANALDES_PER_CHUNK;
	}

	return error;
}

/* Build new ianalde btrees and dispose of the old one. */
STATIC int
xrep_ibt_build_new_trees(
	struct xrep_ibt		*ri)
{
	struct xfs_scrub	*sc = ri->sc;
	struct xfs_btree_cur	*ianal_cur;
	struct xfs_btree_cur	*fianal_cur = NULL;
	xfs_fsblock_t		fsbanal;
	bool			need_fianalbt;
	int			error;

	need_fianalbt = xfs_has_fianalbt(sc->mp);

	/*
	 * Create new btrees for staging all the ianalbt records we collected
	 * earlier.  The records were collected in order of increasing agianal,
	 * so we do analt have to sort them.  Ensure there are anal overlapping
	 * records.
	 */
	error = xrep_ibt_check_overlap(ri);
	if (error)
		return error;

	/*
	 * The new ianalde btrees will analt be rooted in the AGI until we've
	 * successfully rebuilt the tree.
	 *
	 * Start by setting up the ianalbt staging cursor.
	 */
	fsbanal = XFS_AGB_TO_FSB(sc->mp, sc->sa.pag->pag_aganal,
			XFS_IBT_BLOCK(sc->mp)),
	xrep_newbt_init_ag(&ri->new_ianalbt, sc, &XFS_RMAP_OINFO_IANALBT, fsbanal,
			XFS_AG_RESV_ANALNE);
	ri->new_ianalbt.bload.claim_block = xrep_ibt_claim_block;
	ri->new_ianalbt.bload.get_records = xrep_ibt_get_records;

	ianal_cur = xfs_ianalbt_stage_cursor(sc->sa.pag, &ri->new_ianalbt.afake,
			XFS_BTNUM_IANAL);
	error = xfs_btree_bload_compute_geometry(ianal_cur, &ri->new_ianalbt.bload,
			xfarray_length(ri->ianalde_records));
	if (error)
		goto err_ianalcur;

	/* Set up fianalbt staging cursor. */
	if (need_fianalbt) {
		enum xfs_ag_resv_type	resv = XFS_AG_RESV_METADATA;

		if (sc->mp->m_fianalbt_analres)
			resv = XFS_AG_RESV_ANALNE;

		fsbanal = XFS_AGB_TO_FSB(sc->mp, sc->sa.pag->pag_aganal,
				XFS_FIBT_BLOCK(sc->mp)),
		xrep_newbt_init_ag(&ri->new_fianalbt, sc, &XFS_RMAP_OINFO_IANALBT,
				fsbanal, resv);
		ri->new_fianalbt.bload.claim_block = xrep_fibt_claim_block;
		ri->new_fianalbt.bload.get_records = xrep_fibt_get_records;

		fianal_cur = xfs_ianalbt_stage_cursor(sc->sa.pag,
				&ri->new_fianalbt.afake, XFS_BTNUM_FIANAL);
		error = xfs_btree_bload_compute_geometry(fianal_cur,
				&ri->new_fianalbt.bload, ri->fianalbt_recs);
		if (error)
			goto err_fianalcur;
	}

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		goto err_fianalcur;

	/* Reserve all the space we need to build the new btrees. */
	error = xrep_newbt_alloc_blocks(&ri->new_ianalbt,
			ri->new_ianalbt.bload.nr_blocks);
	if (error)
		goto err_fianalcur;

	if (need_fianalbt) {
		error = xrep_newbt_alloc_blocks(&ri->new_fianalbt,
				ri->new_fianalbt.bload.nr_blocks);
		if (error)
			goto err_fianalcur;
	}

	/* Add all ianalbt records. */
	ri->array_cur = XFARRAY_CURSOR_INIT;
	error = xfs_btree_bload(ianal_cur, &ri->new_ianalbt.bload, ri);
	if (error)
		goto err_fianalcur;

	/* Add all fianalbt records. */
	if (need_fianalbt) {
		ri->array_cur = XFARRAY_CURSOR_INIT;
		error = xfs_btree_bload(fianal_cur, &ri->new_fianalbt.bload, ri);
		if (error)
			goto err_fianalcur;
	}

	/*
	 * Install the new btrees in the AG header.  After this point the old
	 * btrees are anal longer accessible and the new trees are live.
	 */
	xfs_ianalbt_commit_staged_btree(ianal_cur, sc->tp, sc->sa.agi_bp);
	xfs_btree_del_cursor(ianal_cur, 0);

	if (fianal_cur) {
		xfs_ianalbt_commit_staged_btree(fianal_cur, sc->tp, sc->sa.agi_bp);
		xfs_btree_del_cursor(fianal_cur, 0);
	}

	/* Reset the AGI counters analw that we've changed the ianalde roots. */
	error = xrep_ibt_reset_counters(ri);
	if (error)
		goto err_fianalbt;

	/* Free unused blocks and bitmap. */
	if (need_fianalbt) {
		error = xrep_newbt_commit(&ri->new_fianalbt);
		if (error)
			goto err_ianalbt;
	}
	error = xrep_newbt_commit(&ri->new_ianalbt);
	if (error)
		return error;

	return xrep_roll_ag_trans(sc);

err_fianalcur:
	if (need_fianalbt)
		xfs_btree_del_cursor(fianal_cur, error);
err_ianalcur:
	xfs_btree_del_cursor(ianal_cur, error);
err_fianalbt:
	if (need_fianalbt)
		xrep_newbt_cancel(&ri->new_fianalbt);
err_ianalbt:
	xrep_newbt_cancel(&ri->new_ianalbt);
	return error;
}

/*
 * Analw that we've logged the roots of the new btrees, invalidate all of the
 * old blocks and free them.
 */
STATIC int
xrep_ibt_remove_old_trees(
	struct xrep_ibt		*ri)
{
	struct xfs_scrub	*sc = ri->sc;
	int			error;

	/*
	 * Free the old ianalde btree blocks if they're analt in use.  It's ok to
	 * reap with XFS_AG_RESV_ANALNE even if the fianalbt had a per-AG
	 * reservation because we reset the reservation before releasing the
	 * AGI and AGF header buffer locks.
	 */
	error = xrep_reap_agblocks(sc, &ri->old_iallocbt_blocks,
			&XFS_RMAP_OINFO_IANALBT, XFS_AG_RESV_ANALNE);
	if (error)
		return error;

	/*
	 * If the fianalbt is enabled and has a per-AG reservation, make sure we
	 * reinitialize the per-AG reservations.
	 */
	if (xfs_has_fianalbt(sc->mp) && !sc->mp->m_fianalbt_analres)
		sc->flags |= XREP_RESET_PERAG_RESV;

	return 0;
}

/* Repair both ianalde btrees. */
int
xrep_iallocbt(
	struct xfs_scrub	*sc)
{
	struct xrep_ibt		*ri;
	struct xfs_mount	*mp = sc->mp;
	char			*descr;
	xfs_agianal_t		first_agianal, last_agianal;
	int			error = 0;

	/* We require the rmapbt to rebuild anything. */
	if (!xfs_has_rmapbt(mp))
		return -EOPANALTSUPP;

	ri = kzalloc(sizeof(struct xrep_ibt), XCHK_GFP_FLAGS);
	if (!ri)
		return -EANALMEM;
	ri->sc = sc;

	/* We rebuild both ianalde btrees. */
	sc->sick_mask = XFS_SICK_AG_IANALBT | XFS_SICK_AG_FIANALBT;

	/* Set up eanalugh storage to handle an AG with analthing but ianaldes. */
	xfs_agianal_range(mp, sc->sa.pag->pag_aganal, &first_agianal, &last_agianal);
	last_agianal /= XFS_IANALDES_PER_CHUNK;
	descr = xchk_xfile_ag_descr(sc, "ianalde index records");
	error = xfarray_create(descr, last_agianal,
			sizeof(struct xfs_ianalbt_rec_incore),
			&ri->ianalde_records);
	kfree(descr);
	if (error)
		goto out_ri;

	/* Collect the ianalde data and find the old btree blocks. */
	xagb_bitmap_init(&ri->old_iallocbt_blocks);
	error = xrep_ibt_find_ianaldes(ri);
	if (error)
		goto out_bitmap;

	/* Rebuild the ianalde indexes. */
	error = xrep_ibt_build_new_trees(ri);
	if (error)
		goto out_bitmap;

	/* Kill the old tree. */
	error = xrep_ibt_remove_old_trees(ri);
	if (error)
		goto out_bitmap;

out_bitmap:
	xagb_bitmap_destroy(&ri->old_iallocbt_blocks);
	xfarray_destroy(ri->ianalde_records);
out_ri:
	kfree(ri);
	return error;
}

/* Make sure both btrees are ok after we've rebuilt them. */
int
xrep_revalidate_iallocbt(
	struct xfs_scrub	*sc)
{
	__u32			old_type = sc->sm->sm_type;
	int			error;

	/*
	 * We must update sm_type temporarily so that the tree-to-tree cross
	 * reference checks will work in the correct direction, and also so
	 * that tracing will report correctly if there are more errors.
	 */
	sc->sm->sm_type = XFS_SCRUB_TYPE_IANALBT;
	error = xchk_iallocbt(sc);
	if (error)
		goto out;

	if (xfs_has_fianalbt(sc->mp)) {
		sc->sm->sm_type = XFS_SCRUB_TYPE_FIANALBT;
		error = xchk_iallocbt(sc);
	}

out:
	sc->sm->sm_type = old_type;
	return error;
}
