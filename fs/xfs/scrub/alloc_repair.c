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
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_inode.h"
#include "xfs_refcount.h"
#include "xfs_extent_busy.h"
#include "xfs_health.h"
#include "xfs_bmap.h"
#include "xfs_ialloc.h"
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
 * Free Space Btree Repair
 * =======================
 *
 * The reverse mappings are supposed to record all space usage for the entire
 * AG.  Therefore, we can recreate the free extent records in an AG by looking
 * for gaps in the physical extents recorded in the rmapbt.  These records are
 * staged in @free_records.  Identifying the gaps is more difficult on a
 * reflink filesystem because rmap records are allowed to overlap.
 *
 * Because the final step of building a new index is to free the space used by
 * the old index, repair needs to find that space.  Unfortunately, all
 * structures that live in the free space (bnobt, cntbt, rmapbt, agfl) share
 * the same rmapbt owner code (OWN_AG), so this is not straightforward.
 *
 * The scan of the reverse mapping information records the space used by OWN_AG
 * in @old_allocbt_blocks, which (at this stage) is somewhat misnamed.  While
 * walking the rmapbt records, we create a second bitmap @not_allocbt_blocks to
 * record all visited rmap btree blocks and all blocks owned by the AGFL.
 *
 * After that is where the definitions of old_allocbt_blocks shifts.  This
 * expression identifies possible former bnobt/cntbt blocks:
 *
 *	(OWN_AG blocks) & ~(rmapbt blocks | agfl blocks);
 *
 * Substituting from above definitions, that becomes:
 *
 *	old_allocbt_blocks & ~not_allocbt_blocks
 *
 * The OWN_AG bitmap itself isn't needed after this point, so what we really do
 * instead is:
 *
 *	old_allocbt_blocks &= ~not_allocbt_blocks;
 *
 * After this point, @old_allocbt_blocks is a bitmap of alleged former
 * bnobt/cntbt blocks.  The xagb_bitmap_disunion operation modifies its first
 * parameter in place to avoid copying records around.
 *
 * Next, some of the space described by @free_records are diverted to the newbt
 * reservation and used to format new btree blocks.  The remaining records are
 * written to the new btree indices.  We reconstruct both bnobt and cntbt at
 * the same time since we've already done all the work.
 *
 * We use the prefix 'xrep_abt' here because we regenerate both free space
 * allocation btrees at the same time.
 */

struct xrep_abt {
	/* Blocks owned by the rmapbt or the agfl. */
	struct xagb_bitmap	not_allocbt_blocks;

	/* All OWN_AG blocks. */
	struct xagb_bitmap	old_allocbt_blocks;

	/*
	 * New bnobt information.  All btree block reservations are added to
	 * the reservation list in new_bnobt.
	 */
	struct xrep_newbt	new_bnobt;

	/* new cntbt information */
	struct xrep_newbt	new_cntbt;

	/* Free space extents. */
	struct xfarray		*free_records;

	struct xfs_scrub	*sc;

	/* Number of non-null records in @free_records. */
	uint64_t		nr_real_records;

	/* get_records()'s position in the free space record array. */
	xfarray_idx_t		array_cur;

	/*
	 * Next block we anticipate seeing in the rmap records.  If the next
	 * rmap record is greater than next_agbno, we have found unused space.
	 */
	xfs_agblock_t		next_agbno;

	/* Number of free blocks in this AG. */
	xfs_agblock_t		nr_blocks;

	/* Longest free extent we found in the AG. */
	xfs_agblock_t		longest;
};

/* Set up to repair AG free space btrees. */
int
xrep_setup_ag_allocbt(
	struct xfs_scrub	*sc)
{
	unsigned int		busy_gen;

	/*
	 * Make sure the busy extent list is clear because we can't put extents
	 * on there twice.
	 */
	busy_gen = READ_ONCE(sc->sa.pag->pagb_gen);
	if (xfs_extent_busy_list_empty(sc->sa.pag))
		return 0;

	return xfs_extent_busy_flush(sc->tp, sc->sa.pag, busy_gen, 0);
}

/* Check for any obvious conflicts in the free extent. */
STATIC int
xrep_abt_check_free_ext(
	struct xfs_scrub	*sc,
	const struct xfs_alloc_rec_incore *rec)
{
	enum xbtree_recpacking	outcome;
	int			error;

	if (xfs_alloc_check_irec(sc->sa.pag, rec) != NULL)
		return -EFSCORRUPTED;

	/* Must not be an inode chunk. */
	error = xfs_ialloc_has_inodes_at_extent(sc->sa.ino_cur,
			rec->ar_startblock, rec->ar_blockcount, &outcome);
	if (error)
		return error;
	if (outcome != XBTREE_RECPACKING_EMPTY)
		return -EFSCORRUPTED;

	/* Must not be shared or CoW staging. */
	if (sc->sa.refc_cur) {
		error = xfs_refcount_has_records(sc->sa.refc_cur,
				XFS_REFC_DOMAIN_SHARED, rec->ar_startblock,
				rec->ar_blockcount, &outcome);
		if (error)
			return error;
		if (outcome != XBTREE_RECPACKING_EMPTY)
			return -EFSCORRUPTED;

		error = xfs_refcount_has_records(sc->sa.refc_cur,
				XFS_REFC_DOMAIN_COW, rec->ar_startblock,
				rec->ar_blockcount, &outcome);
		if (error)
			return error;
		if (outcome != XBTREE_RECPACKING_EMPTY)
			return -EFSCORRUPTED;
	}

	return 0;
}

/*
 * Stash a free space record for all the space since the last bno we found
 * all the way up to @end.
 */
static int
xrep_abt_stash(
	struct xrep_abt		*ra,
	xfs_agblock_t		end)
{
	struct xfs_alloc_rec_incore arec = {
		.ar_startblock	= ra->next_agbno,
		.ar_blockcount	= end - ra->next_agbno,
	};
	struct xfs_scrub	*sc = ra->sc;
	int			error = 0;

	if (xchk_should_terminate(sc, &error))
		return error;

	error = xrep_abt_check_free_ext(ra->sc, &arec);
	if (error)
		return error;

	trace_xrep_abt_found(sc->mp, sc->sa.pag->pag_agno, &arec);

	error = xfarray_append(ra->free_records, &arec);
	if (error)
		return error;

	ra->nr_blocks += arec.ar_blockcount;
	return 0;
}

/* Record extents that aren't in use from gaps in the rmap records. */
STATIC int
xrep_abt_walk_rmap(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*priv)
{
	struct xrep_abt			*ra = priv;
	int				error;

	/* Record all the OWN_AG blocks... */
	if (rec->rm_owner == XFS_RMAP_OWN_AG) {
		error = xagb_bitmap_set(&ra->old_allocbt_blocks,
				rec->rm_startblock, rec->rm_blockcount);
		if (error)
			return error;
	}

	/* ...and all the rmapbt blocks... */
	error = xagb_bitmap_set_btcur_path(&ra->not_allocbt_blocks, cur);
	if (error)
		return error;

	/* ...and all the free space. */
	if (rec->rm_startblock > ra->next_agbno) {
		error = xrep_abt_stash(ra, rec->rm_startblock);
		if (error)
			return error;
	}

	/*
	 * rmap records can overlap on reflink filesystems, so project
	 * next_agbno as far out into the AG space as we currently know about.
	 */
	ra->next_agbno = max_t(xfs_agblock_t, ra->next_agbno,
			rec->rm_startblock + rec->rm_blockcount);
	return 0;
}

/* Collect an AGFL block for the not-to-release list. */
static int
xrep_abt_walk_agfl(
	struct xfs_mount	*mp,
	xfs_agblock_t		agbno,
	void			*priv)
{
	struct xrep_abt		*ra = priv;

	return xagb_bitmap_set(&ra->not_allocbt_blocks, agbno, 1);
}

/*
 * Compare two free space extents by block number.  We want to sort in order of
 * increasing block number.
 */
static int
xrep_bnobt_extent_cmp(
	const void		*a,
	const void		*b)
{
	const struct xfs_alloc_rec_incore *ap = a;
	const struct xfs_alloc_rec_incore *bp = b;

	if (ap->ar_startblock > bp->ar_startblock)
		return 1;
	else if (ap->ar_startblock < bp->ar_startblock)
		return -1;
	return 0;
}

/*
 * Re-sort the free extents by block number so that we can put the records into
 * the bnobt in the correct order.  Make sure the records do not overlap in
 * physical space.
 */
STATIC int
xrep_bnobt_sort_records(
	struct xrep_abt			*ra)
{
	struct xfs_alloc_rec_incore	arec;
	xfarray_idx_t			cur = XFARRAY_CURSOR_INIT;
	xfs_agblock_t			next_agbno = 0;
	int				error;

	error = xfarray_sort(ra->free_records, xrep_bnobt_extent_cmp, 0);
	if (error)
		return error;

	while ((error = xfarray_iter(ra->free_records, &cur, &arec)) == 1) {
		if (arec.ar_startblock < next_agbno)
			return -EFSCORRUPTED;

		next_agbno = arec.ar_startblock + arec.ar_blockcount;
	}

	return error;
}

/*
 * Compare two free space extents by length and then block number.  We want
 * to sort first in order of increasing length and then in order of increasing
 * block number.
 */
static int
xrep_cntbt_extent_cmp(
	const void			*a,
	const void			*b)
{
	const struct xfs_alloc_rec_incore *ap = a;
	const struct xfs_alloc_rec_incore *bp = b;

	if (ap->ar_blockcount > bp->ar_blockcount)
		return 1;
	else if (ap->ar_blockcount < bp->ar_blockcount)
		return -1;
	return xrep_bnobt_extent_cmp(a, b);
}

/*
 * Sort the free extents by length so so that we can put the records into the
 * cntbt in the correct order.  Don't let userspace kill us if we're resorting
 * after allocating btree blocks.
 */
STATIC int
xrep_cntbt_sort_records(
	struct xrep_abt			*ra,
	bool				is_resort)
{
	return xfarray_sort(ra->free_records, xrep_cntbt_extent_cmp,
			is_resort ? 0 : XFARRAY_SORT_KILLABLE);
}

/*
 * Iterate all reverse mappings to find (1) the gaps between rmap records (all
 * unowned space), (2) the OWN_AG extents (which encompass the free space
 * btrees, the rmapbt, and the agfl), (3) the rmapbt blocks, and (4) the AGFL
 * blocks.  The free space is (1) + (2) - (3) - (4).
 */
STATIC int
xrep_abt_find_freespace(
	struct xrep_abt		*ra)
{
	struct xfs_scrub	*sc = ra->sc;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_agf		*agf = sc->sa.agf_bp->b_addr;
	struct xfs_buf		*agfl_bp;
	xfs_agblock_t		agend;
	int			error;

	xagb_bitmap_init(&ra->not_allocbt_blocks);

	xrep_ag_btcur_init(sc, &sc->sa);

	/*
	 * Iterate all the reverse mappings to find gaps in the physical
	 * mappings, all the OWN_AG blocks, and all the rmapbt extents.
	 */
	error = xfs_rmap_query_all(sc->sa.rmap_cur, xrep_abt_walk_rmap, ra);
	if (error)
		goto err;

	/* Insert a record for space between the last rmap and EOAG. */
	agend = be32_to_cpu(agf->agf_length);
	if (ra->next_agbno < agend) {
		error = xrep_abt_stash(ra, agend);
		if (error)
			goto err;
	}

	/* Collect all the AGFL blocks. */
	error = xfs_alloc_read_agfl(sc->sa.pag, sc->tp, &agfl_bp);
	if (error)
		goto err;

	error = xfs_agfl_walk(mp, agf, agfl_bp, xrep_abt_walk_agfl, ra);
	if (error)
		goto err_agfl;

	/* Compute the old bnobt/cntbt blocks. */
	error = xagb_bitmap_disunion(&ra->old_allocbt_blocks,
			&ra->not_allocbt_blocks);
	if (error)
		goto err_agfl;

	ra->nr_real_records = xfarray_length(ra->free_records);
err_agfl:
	xfs_trans_brelse(sc->tp, agfl_bp);
err:
	xchk_ag_btcur_free(&sc->sa);
	xagb_bitmap_destroy(&ra->not_allocbt_blocks);
	return error;
}

/*
 * We're going to use the observed free space records to reserve blocks for the
 * new free space btrees, so we play an iterative game where we try to converge
 * on the number of blocks we need:
 *
 * 1. Estimate how many blocks we'll need to store the records.
 * 2. If the first free record has more blocks than we need, we're done.
 *    We will have to re-sort the records prior to building the cntbt.
 * 3. If that record has exactly the number of blocks we need, null out the
 *    record.  We're done.
 * 4. Otherwise, we still need more blocks.  Null out the record, subtract its
 *    length from the number of blocks we need, and go back to step 1.
 *
 * Fortunately, we don't have to do any transaction work to play this game, so
 * we don't have to tear down the staging cursors.
 */
STATIC int
xrep_abt_reserve_space(
	struct xrep_abt		*ra,
	struct xfs_btree_cur	*bno_cur,
	struct xfs_btree_cur	*cnt_cur,
	bool			*needs_resort)
{
	struct xfs_scrub	*sc = ra->sc;
	xfarray_idx_t		record_nr;
	unsigned int		allocated = 0;
	int			error = 0;

	record_nr = xfarray_length(ra->free_records) - 1;
	do {
		struct xfs_alloc_rec_incore arec;
		uint64_t		required;
		unsigned int		desired;
		unsigned int		len;

		/* Compute how many blocks we'll need. */
		error = xfs_btree_bload_compute_geometry(cnt_cur,
				&ra->new_cntbt.bload, ra->nr_real_records);
		if (error)
			break;

		error = xfs_btree_bload_compute_geometry(bno_cur,
				&ra->new_bnobt.bload, ra->nr_real_records);
		if (error)
			break;

		/* How many btree blocks do we need to store all records? */
		required = ra->new_bnobt.bload.nr_blocks +
			   ra->new_cntbt.bload.nr_blocks;
		ASSERT(required < INT_MAX);

		/* If we've reserved enough blocks, we're done. */
		if (allocated >= required)
			break;

		desired = required - allocated;

		/* We need space but there's none left; bye! */
		if (ra->nr_real_records == 0) {
			error = -ENOSPC;
			break;
		}

		/* Grab the first record from the list. */
		error = xfarray_load(ra->free_records, record_nr, &arec);
		if (error)
			break;

		ASSERT(arec.ar_blockcount <= UINT_MAX);
		len = min_t(unsigned int, arec.ar_blockcount, desired);

		trace_xrep_newbt_alloc_ag_blocks(sc->mp, sc->sa.pag->pag_agno,
				arec.ar_startblock, len, XFS_RMAP_OWN_AG);

		error = xrep_newbt_add_extent(&ra->new_bnobt, sc->sa.pag,
				arec.ar_startblock, len);
		if (error)
			break;
		allocated += len;
		ra->nr_blocks -= len;

		if (arec.ar_blockcount > desired) {
			/*
			 * Record has more space than we need.  The number of
			 * free records doesn't change, so shrink the free
			 * record, inform the caller that the records are no
			 * longer sorted by length, and exit.
			 */
			arec.ar_startblock += desired;
			arec.ar_blockcount -= desired;
			error = xfarray_store(ra->free_records, record_nr,
					&arec);
			if (error)
				break;

			*needs_resort = true;
			return 0;
		}

		/*
		 * We're going to use up the entire record, so unset it and
		 * move on to the next one.  This changes the number of free
		 * records (but doesn't break the sorting order), so we must
		 * go around the loop once more to re-run _bload_init.
		 */
		error = xfarray_unset(ra->free_records, record_nr);
		if (error)
			break;
		ra->nr_real_records--;
		record_nr--;
	} while (1);

	return error;
}

STATIC int
xrep_abt_dispose_one(
	struct xrep_abt		*ra,
	struct xrep_newbt_resv	*resv)
{
	struct xfs_scrub	*sc = ra->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	xfs_agblock_t		free_agbno = resv->agbno + resv->used;
	xfs_extlen_t		free_aglen = resv->len - resv->used;
	int			error;

	ASSERT(pag == resv->pag);

	/* Add a deferred rmap for each extent we used. */
	if (resv->used > 0)
		xfs_rmap_alloc_extent(sc->tp, pag->pag_agno, resv->agbno,
				resv->used, XFS_RMAP_OWN_AG);

	/*
	 * For each reserved btree block we didn't use, add it to the free
	 * space btree.  We didn't touch fdblocks when we reserved them, so
	 * we don't touch it now.
	 */
	if (free_aglen == 0)
		return 0;

	trace_xrep_newbt_free_blocks(sc->mp, resv->pag->pag_agno, free_agbno,
			free_aglen, ra->new_bnobt.oinfo.oi_owner);

	error = __xfs_free_extent(sc->tp, resv->pag, free_agbno, free_aglen,
			&ra->new_bnobt.oinfo, XFS_AG_RESV_IGNORE, true);
	if (error)
		return error;

	return xrep_defer_finish(sc);
}

/*
 * Deal with all the space we reserved.  Blocks that were allocated for the
 * free space btrees need to have a (deferred) rmap added for the OWN_AG
 * allocation, and blocks that didn't get used can be freed via the usual
 * (deferred) means.
 */
STATIC void
xrep_abt_dispose_reservations(
	struct xrep_abt		*ra,
	int			error)
{
	struct xrep_newbt_resv	*resv, *n;

	if (error)
		goto junkit;

	list_for_each_entry_safe(resv, n, &ra->new_bnobt.resv_list, list) {
		error = xrep_abt_dispose_one(ra, resv);
		if (error)
			goto junkit;
	}

junkit:
	list_for_each_entry_safe(resv, n, &ra->new_bnobt.resv_list, list) {
		xfs_perag_put(resv->pag);
		list_del(&resv->list);
		kfree(resv);
	}

	xrep_newbt_cancel(&ra->new_bnobt);
	xrep_newbt_cancel(&ra->new_cntbt);
}

/* Retrieve free space data for bulk load. */
STATIC int
xrep_abt_get_records(
	struct xfs_btree_cur		*cur,
	unsigned int			idx,
	struct xfs_btree_block		*block,
	unsigned int			nr_wanted,
	void				*priv)
{
	struct xfs_alloc_rec_incore	*arec = &cur->bc_rec.a;
	struct xrep_abt			*ra = priv;
	union xfs_btree_rec		*block_rec;
	unsigned int			loaded;
	int				error;

	for (loaded = 0; loaded < nr_wanted; loaded++, idx++) {
		error = xfarray_load_next(ra->free_records, &ra->array_cur,
				arec);
		if (error)
			return error;

		ra->longest = max(ra->longest, arec->ar_blockcount);

		block_rec = xfs_btree_rec_addr(cur, idx, block);
		cur->bc_ops->init_rec_from_cur(cur, block_rec);
	}

	return loaded;
}

/* Feed one of the new btree blocks to the bulk loader. */
STATIC int
xrep_abt_claim_block(
	struct xfs_btree_cur	*cur,
	union xfs_btree_ptr	*ptr,
	void			*priv)
{
	struct xrep_abt		*ra = priv;

	return xrep_newbt_claim_block(cur, &ra->new_bnobt, ptr);
}

/*
 * Reset the AGF counters to reflect the free space btrees that we just
 * rebuilt, then reinitialize the per-AG data.
 */
STATIC int
xrep_abt_reset_counters(
	struct xrep_abt		*ra)
{
	struct xfs_scrub	*sc = ra->sc;
	struct xfs_perag	*pag = sc->sa.pag;
	struct xfs_agf		*agf = sc->sa.agf_bp->b_addr;
	unsigned int		freesp_btreeblks = 0;

	/*
	 * Compute the contribution to agf_btreeblks for the new free space
	 * btrees.  This is the computed btree size minus anything we didn't
	 * use.
	 */
	freesp_btreeblks += ra->new_bnobt.bload.nr_blocks - 1;
	freesp_btreeblks += ra->new_cntbt.bload.nr_blocks - 1;

	freesp_btreeblks -= xrep_newbt_unused_blocks(&ra->new_bnobt);
	freesp_btreeblks -= xrep_newbt_unused_blocks(&ra->new_cntbt);

	/*
	 * The AGF header contains extra information related to the free space
	 * btrees, so we must update those fields here.
	 */
	agf->agf_btreeblks = cpu_to_be32(freesp_btreeblks +
				(be32_to_cpu(agf->agf_rmap_blocks) - 1));
	agf->agf_freeblks = cpu_to_be32(ra->nr_blocks);
	agf->agf_longest = cpu_to_be32(ra->longest);
	xfs_alloc_log_agf(sc->tp, sc->sa.agf_bp, XFS_AGF_BTREEBLKS |
						 XFS_AGF_LONGEST |
						 XFS_AGF_FREEBLKS);

	/*
	 * After we commit the new btree to disk, it is possible that the
	 * process to reap the old btree blocks will race with the AIL trying
	 * to checkpoint the old btree blocks into the filesystem.  If the new
	 * tree is shorter than the old one, the allocbt write verifier will
	 * fail and the AIL will shut down the filesystem.
	 *
	 * To avoid this, save the old incore btree height values as the alt
	 * height values before re-initializing the perag info from the updated
	 * AGF to capture all the new values.
	 */
	pag->pagf_repair_bno_level = pag->pagf_bno_level;
	pag->pagf_repair_cnt_level = pag->pagf_cnt_level;

	/* Reinitialize with the values we just logged. */
	return xrep_reinit_pagf(sc);
}

/*
 * Use the collected free space information to stage new free space btrees.
 * If this is successful we'll return with the new btree root
 * information logged to the repair transaction but not yet committed.
 */
STATIC int
xrep_abt_build_new_trees(
	struct xrep_abt		*ra)
{
	struct xfs_scrub	*sc = ra->sc;
	struct xfs_btree_cur	*bno_cur;
	struct xfs_btree_cur	*cnt_cur;
	struct xfs_perag	*pag = sc->sa.pag;
	bool			needs_resort = false;
	int			error;

	/*
	 * Sort the free extents by length so that we can set up the free space
	 * btrees in as few extents as possible.  This reduces the amount of
	 * deferred rmap / free work we have to do at the end.
	 */
	error = xrep_cntbt_sort_records(ra, false);
	if (error)
		return error;

	/*
	 * Prepare to construct the new btree by reserving disk space for the
	 * new btree and setting up all the accounting information we'll need
	 * to root the new btree while it's under construction and before we
	 * attach it to the AG header.
	 */
	xrep_newbt_init_bare(&ra->new_bnobt, sc);
	xrep_newbt_init_bare(&ra->new_cntbt, sc);

	ra->new_bnobt.bload.get_records = xrep_abt_get_records;
	ra->new_cntbt.bload.get_records = xrep_abt_get_records;

	ra->new_bnobt.bload.claim_block = xrep_abt_claim_block;
	ra->new_cntbt.bload.claim_block = xrep_abt_claim_block;

	/* Allocate cursors for the staged btrees. */
	bno_cur = xfs_bnobt_init_cursor(sc->mp, NULL, NULL, pag);
	xfs_btree_stage_afakeroot(bno_cur, &ra->new_bnobt.afake);

	cnt_cur = xfs_cntbt_init_cursor(sc->mp, NULL, NULL, pag);
	xfs_btree_stage_afakeroot(cnt_cur, &ra->new_cntbt.afake);

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		goto err_cur;

	/* Reserve the space we'll need for the new btrees. */
	error = xrep_abt_reserve_space(ra, bno_cur, cnt_cur, &needs_resort);
	if (error)
		goto err_cur;

	/*
	 * If we need to re-sort the free extents by length, do so so that we
	 * can put the records into the cntbt in the correct order.
	 */
	if (needs_resort) {
		error = xrep_cntbt_sort_records(ra, needs_resort);
		if (error)
			goto err_cur;
	}

	/*
	 * Due to btree slack factors, it's possible for a new btree to be one
	 * level taller than the old btree.  Update the alternate incore btree
	 * height so that we don't trip the verifiers when writing the new
	 * btree blocks to disk.
	 */
	pag->pagf_repair_bno_level = ra->new_bnobt.bload.btree_height;
	pag->pagf_repair_cnt_level = ra->new_cntbt.bload.btree_height;

	/* Load the free space by length tree. */
	ra->array_cur = XFARRAY_CURSOR_INIT;
	ra->longest = 0;
	error = xfs_btree_bload(cnt_cur, &ra->new_cntbt.bload, ra);
	if (error)
		goto err_levels;

	error = xrep_bnobt_sort_records(ra);
	if (error)
		goto err_levels;

	/* Load the free space by block number tree. */
	ra->array_cur = XFARRAY_CURSOR_INIT;
	error = xfs_btree_bload(bno_cur, &ra->new_bnobt.bload, ra);
	if (error)
		goto err_levels;

	/*
	 * Install the new btrees in the AG header.  After this point the old
	 * btrees are no longer accessible and the new trees are live.
	 */
	xfs_allocbt_commit_staged_btree(bno_cur, sc->tp, sc->sa.agf_bp);
	xfs_btree_del_cursor(bno_cur, 0);
	xfs_allocbt_commit_staged_btree(cnt_cur, sc->tp, sc->sa.agf_bp);
	xfs_btree_del_cursor(cnt_cur, 0);

	/* Reset the AGF counters now that we've changed the btree shape. */
	error = xrep_abt_reset_counters(ra);
	if (error)
		goto err_newbt;

	/* Dispose of any unused blocks and the accounting information. */
	xrep_abt_dispose_reservations(ra, error);

	return xrep_roll_ag_trans(sc);

err_levels:
	pag->pagf_repair_bno_level = 0;
	pag->pagf_repair_cnt_level = 0;
err_cur:
	xfs_btree_del_cursor(cnt_cur, error);
	xfs_btree_del_cursor(bno_cur, error);
err_newbt:
	xrep_abt_dispose_reservations(ra, error);
	return error;
}

/*
 * Now that we've logged the roots of the new btrees, invalidate all of the
 * old blocks and free them.
 */
STATIC int
xrep_abt_remove_old_trees(
	struct xrep_abt		*ra)
{
	struct xfs_perag	*pag = ra->sc->sa.pag;
	int			error;

	/* Free the old btree blocks if they're not in use. */
	error = xrep_reap_agblocks(ra->sc, &ra->old_allocbt_blocks,
			&XFS_RMAP_OINFO_AG, XFS_AG_RESV_IGNORE);
	if (error)
		return error;

	/*
	 * Now that we've zapped all the old allocbt blocks we can turn off
	 * the alternate height mechanism.
	 */
	pag->pagf_repair_bno_level = 0;
	pag->pagf_repair_cnt_level = 0;
	return 0;
}

/* Repair the freespace btrees for some AG. */
int
xrep_allocbt(
	struct xfs_scrub	*sc)
{
	struct xrep_abt		*ra;
	struct xfs_mount	*mp = sc->mp;
	char			*descr;
	int			error;

	/* We require the rmapbt to rebuild anything. */
	if (!xfs_has_rmapbt(mp))
		return -EOPNOTSUPP;

	ra = kzalloc(sizeof(struct xrep_abt), XCHK_GFP_FLAGS);
	if (!ra)
		return -ENOMEM;
	ra->sc = sc;

	/* We rebuild both data structures. */
	sc->sick_mask = XFS_SICK_AG_BNOBT | XFS_SICK_AG_CNTBT;

	/*
	 * Make sure the busy extent list is clear because we can't put extents
	 * on there twice.  In theory we cleared this before we started, but
	 * let's not risk the filesystem.
	 */
	if (!xfs_extent_busy_list_empty(sc->sa.pag)) {
		error = -EDEADLOCK;
		goto out_ra;
	}

	/* Set up enough storage to handle maximally fragmented free space. */
	descr = xchk_xfile_ag_descr(sc, "free space records");
	error = xfarray_create(descr, mp->m_sb.sb_agblocks / 2,
			sizeof(struct xfs_alloc_rec_incore),
			&ra->free_records);
	kfree(descr);
	if (error)
		goto out_ra;

	/* Collect the free space data and find the old btree blocks. */
	xagb_bitmap_init(&ra->old_allocbt_blocks);
	error = xrep_abt_find_freespace(ra);
	if (error)
		goto out_bitmap;

	/* Rebuild the free space information. */
	error = xrep_abt_build_new_trees(ra);
	if (error)
		goto out_bitmap;

	/* Kill the old trees. */
	error = xrep_abt_remove_old_trees(ra);
	if (error)
		goto out_bitmap;

out_bitmap:
	xagb_bitmap_destroy(&ra->old_allocbt_blocks);
	xfarray_destroy(ra->free_records);
out_ra:
	kfree(ra);
	return error;
}

/* Make sure both btrees are ok after we've rebuilt them. */
int
xrep_revalidate_allocbt(
	struct xfs_scrub	*sc)
{
	__u32			old_type = sc->sm->sm_type;
	int			error;

	/*
	 * We must update sm_type temporarily so that the tree-to-tree cross
	 * reference checks will work in the correct direction, and also so
	 * that tracing will report correctly if there are more errors.
	 */
	sc->sm->sm_type = XFS_SCRUB_TYPE_BNOBT;
	error = xchk_allocbt(sc);
	if (error)
		goto out;

	sc->sm->sm_type = XFS_SCRUB_TYPE_CNTBT;
	error = xchk_allocbt(sc);
out:
	sc->sm->sm_type = old_type;
	return error;
}
