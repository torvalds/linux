// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_btree_staging.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"
#include "xfs_defer.h"
#include "xfs_metafile.h"
#include "xfs_quota.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/newbt.h"

/*
 * This is the maximum number of deferred extent freeing item extents (EFIs)
 * that we'll attach to a transaction without rolling the transaction to avoid
 * overrunning a tr_itruncate reservation.  The newbt code should reserve
 * exactly the correct number of blocks to rebuild the btree, so there should
 * not be any excess blocks to free when committing a new btree.
 */
#define XREP_MAX_ITRUNCATE_EFIS	(128)

/*
 * Estimate proper slack values for a btree that's being reloaded.
 *
 * Under most circumstances, we'll take whatever default loading value the
 * btree bulk loading code calculates for us.  However, there are some
 * exceptions to this rule:
 *
 * (0) If someone turned one of the debug knobs.
 * (1) If this is a per-AG btree and the AG has less than 10% space free.
 * (2) If this is an inode btree and the FS has less than 10% space free.

 * In either case, format the new btree blocks almost completely full to
 * minimize space usage.
 */
static void
xrep_newbt_estimate_slack(
	struct xrep_newbt	*xnr)
{
	struct xfs_scrub	*sc = xnr->sc;
	struct xfs_btree_bload	*bload = &xnr->bload;
	uint64_t		free;
	uint64_t		sz;

	/*
	 * The xfs_globals values are set to -1 (i.e. take the bload defaults)
	 * unless someone has set them otherwise, so we just pull the values
	 * here.
	 */
	bload->leaf_slack = xfs_globals.bload_leaf_slack;
	bload->node_slack = xfs_globals.bload_node_slack;

	if (sc->ops->type == ST_PERAG) {
		free = sc->sa.pag->pagf_freeblks;
		sz = xfs_ag_block_count(sc->mp, pag_agno(sc->sa.pag));
	} else {
		free = xfs_sum_freecounter_raw(sc->mp, XC_FREE_BLOCKS);
		sz = sc->mp->m_sb.sb_dblocks;
	}

	/* No further changes if there's more than 10% free space left. */
	if (free >= div_u64(sz, 10))
		return;

	/*
	 * We're low on space; load the btrees as tightly as possible.  Leave
	 * a couple of open slots in each btree block so that we don't end up
	 * splitting the btrees like crazy after a mount.
	 */
	if (bload->leaf_slack < 0)
		bload->leaf_slack = 2;
	if (bload->node_slack < 0)
		bload->node_slack = 2;
}

/* Initialize accounting resources for staging a new AG btree. */
void
xrep_newbt_init_ag(
	struct xrep_newbt		*xnr,
	struct xfs_scrub		*sc,
	const struct xfs_owner_info	*oinfo,
	xfs_fsblock_t			alloc_hint,
	enum xfs_ag_resv_type		resv)
{
	memset(xnr, 0, sizeof(struct xrep_newbt));
	xnr->sc = sc;
	xnr->oinfo = *oinfo; /* structure copy */
	xnr->alloc_hint = alloc_hint;
	xnr->resv = resv;
	INIT_LIST_HEAD(&xnr->resv_list);
	xnr->bload.max_dirty = XFS_B_TO_FSBT(sc->mp, 256U << 10); /* 256K */
	xrep_newbt_estimate_slack(xnr);
}

/* Initialize accounting resources for staging a new inode fork btree. */
int
xrep_newbt_init_inode(
	struct xrep_newbt		*xnr,
	struct xfs_scrub		*sc,
	int				whichfork,
	const struct xfs_owner_info	*oinfo)
{
	struct xfs_ifork		*ifp;

	ifp = kmem_cache_zalloc(xfs_ifork_cache, XCHK_GFP_FLAGS);
	if (!ifp)
		return -ENOMEM;

	xrep_newbt_init_ag(xnr, sc, oinfo,
			XFS_INO_TO_FSB(sc->mp, sc->ip->i_ino),
			XFS_AG_RESV_NONE);
	xnr->ifake.if_fork = ifp;
	xnr->ifake.if_fork_size = xfs_inode_fork_size(sc->ip, whichfork);
	return 0;
}

/*
 * Initialize accounting resources for staging a new metadata inode btree.
 * If the metadata file has a space reservation, the caller must adjust that
 * reservation when committing the new ondisk btree.
 */
int
xrep_newbt_init_metadir_inode(
	struct xrep_newbt		*xnr,
	struct xfs_scrub		*sc)
{
	struct xfs_owner_info		oinfo;
	struct xfs_ifork		*ifp;

	ASSERT(xfs_is_metadir_inode(sc->ip));

	xfs_rmap_ino_bmbt_owner(&oinfo, sc->ip->i_ino, XFS_DATA_FORK);

	ifp = kmem_cache_zalloc(xfs_ifork_cache, XCHK_GFP_FLAGS);
	if (!ifp)
		return -ENOMEM;

	/*
	 * Allocate new metadir btree blocks with XFS_AG_RESV_NONE because the
	 * inode metadata space reservations can only account allocated space
	 * to the i_nblocks.  We do not want to change the inode core fields
	 * until we're ready to commit the new tree, so we allocate the blocks
	 * as if they were regular file blocks.  This exposes us to a higher
	 * risk of the repair being cancelled due to ENOSPC.
	 */
	xrep_newbt_init_ag(xnr, sc, &oinfo,
			XFS_INO_TO_FSB(sc->mp, sc->ip->i_ino),
			XFS_AG_RESV_NONE);
	xnr->ifake.if_fork = ifp;
	xnr->ifake.if_fork_size = xfs_inode_fork_size(sc->ip, XFS_DATA_FORK);
	return 0;
}

/*
 * Initialize accounting resources for staging a new btree.  Callers are
 * expected to add their own reservations (and clean them up) manually.
 */
void
xrep_newbt_init_bare(
	struct xrep_newbt		*xnr,
	struct xfs_scrub		*sc)
{
	xrep_newbt_init_ag(xnr, sc, &XFS_RMAP_OINFO_ANY_OWNER, NULLFSBLOCK,
			XFS_AG_RESV_NONE);
}

/*
 * Designate specific blocks to be used to build our new btree.  @pag must be
 * a passive reference.
 */
STATIC int
xrep_newbt_add_blocks(
	struct xrep_newbt		*xnr,
	struct xfs_perag		*pag,
	const struct xfs_alloc_arg	*args)
{
	struct xfs_mount		*mp = xnr->sc->mp;
	struct xrep_newbt_resv		*resv;
	int				error;

	resv = kmalloc(sizeof(struct xrep_newbt_resv), XCHK_GFP_FLAGS);
	if (!resv)
		return -ENOMEM;

	INIT_LIST_HEAD(&resv->list);
	resv->agbno = XFS_FSB_TO_AGBNO(mp, args->fsbno);
	resv->len = args->len;
	resv->used = 0;
	resv->pag = xfs_perag_hold(pag);

	if (args->tp) {
		ASSERT(xnr->oinfo.oi_offset == 0);

		error = xfs_alloc_schedule_autoreap(args,
				XFS_FREE_EXTENT_SKIP_DISCARD, &resv->autoreap);
		if (error)
			goto out_pag;
	}

	list_add_tail(&resv->list, &xnr->resv_list);
	return 0;
out_pag:
	xfs_perag_put(resv->pag);
	kfree(resv);
	return error;
}

/*
 * Add an extent to the new btree reservation pool.  Callers are required to
 * reap this reservation manually if the repair is cancelled.  @pag must be a
 * passive reference.
 */
int
xrep_newbt_add_extent(
	struct xrep_newbt	*xnr,
	struct xfs_perag	*pag,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len)
{
	struct xfs_alloc_arg	args = {
		.tp		= NULL, /* no autoreap */
		.oinfo		= xnr->oinfo,
		.fsbno		= xfs_agbno_to_fsb(pag, agbno),
		.len		= len,
		.resv		= xnr->resv,
	};

	return xrep_newbt_add_blocks(xnr, pag, &args);
}

/* Don't let our allocation hint take us beyond this AG */
static inline void
xrep_newbt_validate_ag_alloc_hint(
	struct xrep_newbt	*xnr)
{
	struct xfs_scrub	*sc = xnr->sc;
	xfs_agnumber_t		agno = XFS_FSB_TO_AGNO(sc->mp, xnr->alloc_hint);

	if (agno == pag_agno(sc->sa.pag) &&
	    xfs_verify_fsbno(sc->mp, xnr->alloc_hint))
		return;

	xnr->alloc_hint =
		xfs_agbno_to_fsb(sc->sa.pag, XFS_AGFL_BLOCK(sc->mp) + 1);
}

/* Allocate disk space for a new per-AG btree. */
STATIC int
xrep_newbt_alloc_ag_blocks(
	struct xrep_newbt	*xnr,
	uint64_t		nr_blocks)
{
	struct xfs_scrub	*sc = xnr->sc;
	struct xfs_mount	*mp = sc->mp;
	int			error = 0;

	ASSERT(sc->sa.pag != NULL);
	ASSERT(xnr->resv != XFS_AG_RESV_METAFILE);

	while (nr_blocks > 0) {
		struct xfs_alloc_arg	args = {
			.tp		= sc->tp,
			.mp		= mp,
			.oinfo		= xnr->oinfo,
			.minlen		= 1,
			.maxlen		= nr_blocks,
			.prod		= 1,
			.resv		= xnr->resv,
		};
		xfs_agnumber_t		agno;

		xrep_newbt_validate_ag_alloc_hint(xnr);

		if (xnr->alloc_vextent)
			error = xnr->alloc_vextent(sc, &args, xnr->alloc_hint);
		else
			error = xfs_alloc_vextent_near_bno(&args,
					xnr->alloc_hint);
		if (error)
			return error;
		if (args.fsbno == NULLFSBLOCK)
			return -ENOSPC;

		agno = XFS_FSB_TO_AGNO(mp, args.fsbno);
		if (agno != pag_agno(sc->sa.pag)) {
			ASSERT(agno == pag_agno(sc->sa.pag));
			return -EFSCORRUPTED;
		}

		trace_xrep_newbt_alloc_ag_blocks(sc->sa.pag,
				XFS_FSB_TO_AGBNO(mp, args.fsbno), args.len,
				xnr->oinfo.oi_owner);

		error = xrep_newbt_add_blocks(xnr, sc->sa.pag, &args);
		if (error)
			return error;

		nr_blocks -= args.len;
		xnr->alloc_hint = args.fsbno + args.len;

		error = xrep_defer_finish(sc);
		if (error)
			return error;
	}

	return 0;
}

/* Don't let our allocation hint take us beyond EOFS */
static inline void
xrep_newbt_validate_file_alloc_hint(
	struct xrep_newbt	*xnr)
{
	struct xfs_scrub	*sc = xnr->sc;

	if (xfs_verify_fsbno(sc->mp, xnr->alloc_hint))
		return;

	xnr->alloc_hint = XFS_AGB_TO_FSB(sc->mp, 0, XFS_AGFL_BLOCK(sc->mp) + 1);
}

/* Allocate disk space for our new file-based btree. */
STATIC int
xrep_newbt_alloc_file_blocks(
	struct xrep_newbt	*xnr,
	uint64_t		nr_blocks)
{
	struct xfs_scrub	*sc = xnr->sc;
	struct xfs_mount	*mp = sc->mp;
	int			error = 0;

	ASSERT(xnr->resv != XFS_AG_RESV_METAFILE);

	while (nr_blocks > 0) {
		struct xfs_alloc_arg	args = {
			.tp		= sc->tp,
			.mp		= mp,
			.oinfo		= xnr->oinfo,
			.minlen		= 1,
			.maxlen		= nr_blocks,
			.prod		= 1,
			.resv		= xnr->resv,
		};
		struct xfs_perag	*pag;
		xfs_agnumber_t		agno;

		xrep_newbt_validate_file_alloc_hint(xnr);

		if (xnr->alloc_vextent)
			error = xnr->alloc_vextent(sc, &args, xnr->alloc_hint);
		else
			error = xfs_alloc_vextent_start_ag(&args,
					xnr->alloc_hint);
		if (error)
			return error;
		if (args.fsbno == NULLFSBLOCK)
			return -ENOSPC;

		agno = XFS_FSB_TO_AGNO(mp, args.fsbno);

		pag = xfs_perag_get(mp, agno);
		if (!pag) {
			ASSERT(0);
			return -EFSCORRUPTED;
		}

		trace_xrep_newbt_alloc_file_blocks(pag,
				XFS_FSB_TO_AGBNO(mp, args.fsbno), args.len,
				xnr->oinfo.oi_owner);

		error = xrep_newbt_add_blocks(xnr, pag, &args);
		xfs_perag_put(pag);
		if (error)
			return error;

		nr_blocks -= args.len;
		xnr->alloc_hint = args.fsbno + args.len;

		error = xrep_defer_finish(sc);
		if (error)
			return error;
	}

	return 0;
}

/* Allocate disk space for our new btree. */
int
xrep_newbt_alloc_blocks(
	struct xrep_newbt	*xnr,
	uint64_t		nr_blocks)
{
	if (xnr->sc->ip)
		return xrep_newbt_alloc_file_blocks(xnr, nr_blocks);
	return xrep_newbt_alloc_ag_blocks(xnr, nr_blocks);
}

/*
 * Free the unused part of a space extent that was reserved for a new ondisk
 * structure.  Returns the number of EFIs logged or a negative errno.
 */
STATIC int
xrep_newbt_free_extent(
	struct xrep_newbt	*xnr,
	struct xrep_newbt_resv	*resv,
	bool			btree_committed)
{
	struct xfs_scrub	*sc = xnr->sc;
	xfs_agblock_t		free_agbno = resv->agbno;
	xfs_extlen_t		free_aglen = resv->len;
	int			error;

	if (!btree_committed || resv->used == 0) {
		/*
		 * If we're not committing a new btree or we didn't use the
		 * space reservation, let the existing EFI free the entire
		 * space extent.
		 */
		trace_xrep_newbt_free_blocks(resv->pag, free_agbno, free_aglen,
				xnr->oinfo.oi_owner);
		xfs_alloc_commit_autoreap(sc->tp, &resv->autoreap);
		return 1;
	}

	/*
	 * We used space and committed the btree.  Cancel the autoreap, remove
	 * the written blocks from the reservation, and possibly log a new EFI
	 * to free any unused reservation space.
	 */
	xfs_alloc_cancel_autoreap(sc->tp, &resv->autoreap);
	free_agbno += resv->used;
	free_aglen -= resv->used;

	if (free_aglen == 0)
		return 0;

	trace_xrep_newbt_free_blocks(resv->pag, free_agbno, free_aglen,
			xnr->oinfo.oi_owner);

	ASSERT(xnr->resv != XFS_AG_RESV_AGFL);
	ASSERT(xnr->resv != XFS_AG_RESV_IGNORE);

	/*
	 * Use EFIs to free the reservations.  This reduces the chance
	 * that we leak blocks if the system goes down.
	 */
	error = xfs_free_extent_later(sc->tp,
			xfs_agbno_to_fsb(resv->pag, free_agbno), free_aglen,
			&xnr->oinfo, xnr->resv, XFS_FREE_EXTENT_SKIP_DISCARD);
	if (error)
		return error;

	return 1;
}

/* Free all the accounting info and disk space we reserved for a new btree. */
STATIC int
xrep_newbt_free(
	struct xrep_newbt	*xnr,
	bool			btree_committed)
{
	struct xfs_scrub	*sc = xnr->sc;
	struct xrep_newbt_resv	*resv, *n;
	unsigned int		freed = 0;
	int			error = 0;

	/*
	 * If the filesystem already went down, we can't free the blocks.  Skip
	 * ahead to freeing the incore metadata because we can't fix anything.
	 */
	if (xfs_is_shutdown(sc->mp))
		goto junkit;

	list_for_each_entry_safe(resv, n, &xnr->resv_list, list) {
		int		ret;

		ret = xrep_newbt_free_extent(xnr, resv, btree_committed);
		list_del(&resv->list);
		xfs_perag_put(resv->pag);
		kfree(resv);
		if (ret < 0) {
			error = ret;
			goto junkit;
		}

		freed += ret;
		if (freed >= XREP_MAX_ITRUNCATE_EFIS) {
			error = xrep_defer_finish(sc);
			if (error)
				goto junkit;
			freed = 0;
		}
	}

	if (freed)
		error = xrep_defer_finish(sc);

junkit:
	/*
	 * If we still have reservations attached to @newbt, cleanup must have
	 * failed and the filesystem is about to go down.  Clean up the incore
	 * reservations and try to commit to freeing the space we used.
	 */
	list_for_each_entry_safe(resv, n, &xnr->resv_list, list) {
		xfs_alloc_commit_autoreap(sc->tp, &resv->autoreap);
		list_del(&resv->list);
		xfs_perag_put(resv->pag);
		kfree(resv);
	}

	if (sc->ip) {
		kmem_cache_free(xfs_ifork_cache, xnr->ifake.if_fork);
		xnr->ifake.if_fork = NULL;
	}

	return error;
}

/*
 * Free all the accounting info and unused disk space allocations after
 * committing a new btree.
 */
int
xrep_newbt_commit(
	struct xrep_newbt	*xnr)
{
	return xrep_newbt_free(xnr, true);
}

/*
 * Free all the accounting info and all of the disk space we reserved for a new
 * btree that we're not going to commit.  We want to try to roll things back
 * cleanly for things like ENOSPC midway through allocation.
 */
void
xrep_newbt_cancel(
	struct xrep_newbt	*xnr)
{
	xrep_newbt_free(xnr, false);
}

/* Feed one of the reserved btree blocks to the bulk loader. */
int
xrep_newbt_claim_block(
	struct xfs_btree_cur	*cur,
	struct xrep_newbt	*xnr,
	union xfs_btree_ptr	*ptr)
{
	struct xrep_newbt_resv	*resv;
	xfs_agblock_t		agbno;

	/*
	 * The first item in the list should always have a free block unless
	 * we're completely out.
	 */
	resv = list_first_entry(&xnr->resv_list, struct xrep_newbt_resv, list);
	if (resv->used == resv->len)
		return -ENOSPC;

	/*
	 * Peel off a block from the start of the reservation.  We allocate
	 * blocks in order to place blocks on disk in increasing record or key
	 * order.  The block reservations tend to end up on the list in
	 * decreasing order, which hopefully results in leaf blocks ending up
	 * together.
	 */
	agbno = resv->agbno + resv->used;
	resv->used++;

	/* If we used all the blocks in this reservation, move it to the end. */
	if (resv->used == resv->len)
		list_move_tail(&resv->list, &xnr->resv_list);

	trace_xrep_newbt_claim_block(resv->pag, agbno, 1, xnr->oinfo.oi_owner);

	if (cur->bc_ops->ptr_len == XFS_BTREE_LONG_PTR_LEN)
		ptr->l = cpu_to_be64(xfs_agbno_to_fsb(resv->pag, agbno));
	else
		ptr->s = cpu_to_be32(agbno);

	/* Relog all the EFIs. */
	return xrep_defer_finish(xnr->sc);
}

/* How many reserved blocks are unused? */
unsigned int
xrep_newbt_unused_blocks(
	struct xrep_newbt	*xnr)
{
	struct xrep_newbt_resv	*resv;
	unsigned int		unused = 0;

	list_for_each_entry(resv, &xnr->resv_list, list)
		unused += resv->len - resv->used;
	return unused;
}
