// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_shared.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_refcount_item.h"
#include "xfs_log.h"
#include "xfs_refcount.h"
#include "xfs_error.h"

kmem_zone_t	*xfs_cui_zone;
kmem_zone_t	*xfs_cud_zone;

static inline struct xfs_cui_log_item *CUI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_cui_log_item, cui_item);
}

void
xfs_cui_item_free(
	struct xfs_cui_log_item	*cuip)
{
	if (cuip->cui_format.cui_nextents > XFS_CUI_MAX_FAST_EXTENTS)
		kmem_free(cuip);
	else
		kmem_zone_free(xfs_cui_zone, cuip);
}

/*
 * Freeing the CUI requires that we remove it from the AIL if it has already
 * been placed there. However, the CUI may not yet have been placed in the AIL
 * when called by xfs_cui_release() from CUD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the CUI.
 */
void
xfs_cui_release(
	struct xfs_cui_log_item	*cuip)
{
	ASSERT(atomic_read(&cuip->cui_refcount) > 0);
	if (atomic_dec_and_test(&cuip->cui_refcount)) {
		xfs_trans_ail_remove(&cuip->cui_item, SHUTDOWN_LOG_IO_ERROR);
		xfs_cui_item_free(cuip);
	}
}


STATIC void
xfs_cui_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_cui_log_item	*cuip = CUI_ITEM(lip);

	*nvecs += 1;
	*nbytes += xfs_cui_log_format_sizeof(cuip->cui_format.cui_nextents);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given cui log item. We use only 1 iovec, and we point that
 * at the cui_log_format structure embedded in the cui item.
 * It is at this point that we assert that all of the extent
 * slots in the cui item have been filled.
 */
STATIC void
xfs_cui_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_cui_log_item	*cuip = CUI_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	ASSERT(atomic_read(&cuip->cui_next_extent) ==
			cuip->cui_format.cui_nextents);

	cuip->cui_format.cui_type = XFS_LI_CUI;
	cuip->cui_format.cui_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_CUI_FORMAT, &cuip->cui_format,
			xfs_cui_log_format_sizeof(cuip->cui_format.cui_nextents));
}

/*
 * The unpin operation is the last place an CUI is manipulated in the log. It is
 * either inserted in the AIL or aborted in the event of a log I/O error. In
 * either case, the CUI transaction has been successfully committed to make it
 * this far. Therefore, we expect whoever committed the CUI to either construct
 * and commit the CUD or drop the CUD's reference in the event of error. Simply
 * drop the log's CUI reference now that the log is done with it.
 */
STATIC void
xfs_cui_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_cui_log_item	*cuip = CUI_ITEM(lip);

	xfs_cui_release(cuip);
}

/*
 * The CUI has been either committed or aborted if the transaction has been
 * cancelled. If the transaction was cancelled, an CUD isn't going to be
 * constructed and thus we free the CUI here directly.
 */
STATIC void
xfs_cui_item_release(
	struct xfs_log_item	*lip)
{
	xfs_cui_release(CUI_ITEM(lip));
}

static const struct xfs_item_ops xfs_cui_item_ops = {
	.iop_size	= xfs_cui_item_size,
	.iop_format	= xfs_cui_item_format,
	.iop_unpin	= xfs_cui_item_unpin,
	.iop_release	= xfs_cui_item_release,
};

/*
 * Allocate and initialize an cui item with the given number of extents.
 */
struct xfs_cui_log_item *
xfs_cui_init(
	struct xfs_mount		*mp,
	uint				nextents)

{
	struct xfs_cui_log_item		*cuip;

	ASSERT(nextents > 0);
	if (nextents > XFS_CUI_MAX_FAST_EXTENTS)
		cuip = kmem_zalloc(xfs_cui_log_item_sizeof(nextents),
				0);
	else
		cuip = kmem_zone_zalloc(xfs_cui_zone, 0);

	xfs_log_item_init(mp, &cuip->cui_item, XFS_LI_CUI, &xfs_cui_item_ops);
	cuip->cui_format.cui_nextents = nextents;
	cuip->cui_format.cui_id = (uintptr_t)(void *)cuip;
	atomic_set(&cuip->cui_next_extent, 0);
	atomic_set(&cuip->cui_refcount, 2);

	return cuip;
}

static inline struct xfs_cud_log_item *CUD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_cud_log_item, cud_item);
}

STATIC void
xfs_cud_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += sizeof(struct xfs_cud_log_format);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given cud log item. We use only 1 iovec, and we point that
 * at the cud_log_format structure embedded in the cud item.
 * It is at this point that we assert that all of the extent
 * slots in the cud item have been filled.
 */
STATIC void
xfs_cud_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_cud_log_item	*cudp = CUD_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	cudp->cud_format.cud_type = XFS_LI_CUD;
	cudp->cud_format.cud_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_CUD_FORMAT, &cudp->cud_format,
			sizeof(struct xfs_cud_log_format));
}

/*
 * The CUD is either committed or aborted if the transaction is cancelled. If
 * the transaction is cancelled, drop our reference to the CUI and free the
 * CUD.
 */
STATIC void
xfs_cud_item_release(
	struct xfs_log_item	*lip)
{
	struct xfs_cud_log_item	*cudp = CUD_ITEM(lip);

	xfs_cui_release(cudp->cud_cuip);
	kmem_zone_free(xfs_cud_zone, cudp);
}

static const struct xfs_item_ops xfs_cud_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED,
	.iop_size	= xfs_cud_item_size,
	.iop_format	= xfs_cud_item_format,
	.iop_release	= xfs_cud_item_release,
};

static struct xfs_cud_log_item *
xfs_trans_get_cud(
	struct xfs_trans		*tp,
	struct xfs_cui_log_item		*cuip)
{
	struct xfs_cud_log_item		*cudp;

	cudp = kmem_zone_zalloc(xfs_cud_zone, 0);
	xfs_log_item_init(tp->t_mountp, &cudp->cud_item, XFS_LI_CUD,
			  &xfs_cud_item_ops);
	cudp->cud_cuip = cuip;
	cudp->cud_format.cud_cui_id = cuip->cui_format.cui_id;

	xfs_trans_add_item(tp, &cudp->cud_item);
	return cudp;
}

/*
 * Finish an refcount update and log it to the CUD. Note that the
 * transaction is marked dirty regardless of whether the refcount
 * update succeeds or fails to support the CUI/CUD lifecycle rules.
 */
static int
xfs_trans_log_finish_refcount_update(
	struct xfs_trans		*tp,
	struct xfs_cud_log_item		*cudp,
	enum xfs_refcount_intent_type	type,
	xfs_fsblock_t			startblock,
	xfs_extlen_t			blockcount,
	xfs_fsblock_t			*new_fsb,
	xfs_extlen_t			*new_len,
	struct xfs_btree_cur		**pcur)
{
	int				error;

	error = xfs_refcount_finish_one(tp, type, startblock,
			blockcount, new_fsb, new_len, pcur);

	/*
	 * Mark the transaction dirty, even on error. This ensures the
	 * transaction is aborted, which:
	 *
	 * 1.) releases the CUI and frees the CUD
	 * 2.) shuts down the filesystem
	 */
	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &cudp->cud_item.li_flags);

	return error;
}

/* Sort refcount intents by AG. */
static int
xfs_refcount_update_diff_items(
	void				*priv,
	struct list_head		*a,
	struct list_head		*b)
{
	struct xfs_mount		*mp = priv;
	struct xfs_refcount_intent	*ra;
	struct xfs_refcount_intent	*rb;

	ra = container_of(a, struct xfs_refcount_intent, ri_list);
	rb = container_of(b, struct xfs_refcount_intent, ri_list);
	return  XFS_FSB_TO_AGNO(mp, ra->ri_startblock) -
		XFS_FSB_TO_AGNO(mp, rb->ri_startblock);
}

/* Get an CUI. */
STATIC void *
xfs_refcount_update_create_intent(
	struct xfs_trans		*tp,
	unsigned int			count)
{
	struct xfs_cui_log_item		*cuip;

	ASSERT(tp != NULL);
	ASSERT(count > 0);

	cuip = xfs_cui_init(tp->t_mountp, count);
	ASSERT(cuip != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	xfs_trans_add_item(tp, &cuip->cui_item);
	return cuip;
}

/* Set the phys extent flags for this reverse mapping. */
static void
xfs_trans_set_refcount_flags(
	struct xfs_phys_extent		*refc,
	enum xfs_refcount_intent_type	type)
{
	refc->pe_flags = 0;
	switch (type) {
	case XFS_REFCOUNT_INCREASE:
	case XFS_REFCOUNT_DECREASE:
	case XFS_REFCOUNT_ALLOC_COW:
	case XFS_REFCOUNT_FREE_COW:
		refc->pe_flags |= type;
		break;
	default:
		ASSERT(0);
	}
}

/* Log refcount updates in the intent item. */
STATIC void
xfs_refcount_update_log_item(
	struct xfs_trans		*tp,
	void				*intent,
	struct list_head		*item)
{
	struct xfs_cui_log_item		*cuip = intent;
	struct xfs_refcount_intent	*refc;
	uint				next_extent;
	struct xfs_phys_extent		*ext;

	refc = container_of(item, struct xfs_refcount_intent, ri_list);

	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &cuip->cui_item.li_flags);

	/*
	 * atomic_inc_return gives us the value after the increment;
	 * we want to use it as an array index so we need to subtract 1 from
	 * it.
	 */
	next_extent = atomic_inc_return(&cuip->cui_next_extent) - 1;
	ASSERT(next_extent < cuip->cui_format.cui_nextents);
	ext = &cuip->cui_format.cui_extents[next_extent];
	ext->pe_startblock = refc->ri_startblock;
	ext->pe_len = refc->ri_blockcount;
	xfs_trans_set_refcount_flags(ext, refc->ri_type);
}

/* Get an CUD so we can process all the deferred refcount updates. */
STATIC void *
xfs_refcount_update_create_done(
	struct xfs_trans		*tp,
	void				*intent,
	unsigned int			count)
{
	return xfs_trans_get_cud(tp, intent);
}

/* Process a deferred refcount update. */
STATIC int
xfs_refcount_update_finish_item(
	struct xfs_trans		*tp,
	struct list_head		*item,
	void				*done_item,
	void				**state)
{
	struct xfs_refcount_intent	*refc;
	xfs_fsblock_t			new_fsb;
	xfs_extlen_t			new_aglen;
	int				error;

	refc = container_of(item, struct xfs_refcount_intent, ri_list);
	error = xfs_trans_log_finish_refcount_update(tp, done_item,
			refc->ri_type,
			refc->ri_startblock,
			refc->ri_blockcount,
			&new_fsb, &new_aglen,
			(struct xfs_btree_cur **)state);
	/* Did we run out of reservation?  Requeue what we didn't finish. */
	if (!error && new_aglen > 0) {
		ASSERT(refc->ri_type == XFS_REFCOUNT_INCREASE ||
		       refc->ri_type == XFS_REFCOUNT_DECREASE);
		refc->ri_startblock = new_fsb;
		refc->ri_blockcount = new_aglen;
		return -EAGAIN;
	}
	kmem_free(refc);
	return error;
}

/* Clean up after processing deferred refcounts. */
STATIC void
xfs_refcount_update_finish_cleanup(
	struct xfs_trans	*tp,
	void			*state,
	int			error)
{
	struct xfs_btree_cur	*rcur = state;

	xfs_refcount_finish_one_cleanup(tp, rcur, error);
}

/* Abort all pending CUIs. */
STATIC void
xfs_refcount_update_abort_intent(
	void				*intent)
{
	xfs_cui_release(intent);
}

/* Cancel a deferred refcount update. */
STATIC void
xfs_refcount_update_cancel_item(
	struct list_head		*item)
{
	struct xfs_refcount_intent	*refc;

	refc = container_of(item, struct xfs_refcount_intent, ri_list);
	kmem_free(refc);
}

const struct xfs_defer_op_type xfs_refcount_update_defer_type = {
	.max_items	= XFS_CUI_MAX_FAST_EXTENTS,
	.diff_items	= xfs_refcount_update_diff_items,
	.create_intent	= xfs_refcount_update_create_intent,
	.abort_intent	= xfs_refcount_update_abort_intent,
	.log_item	= xfs_refcount_update_log_item,
	.create_done	= xfs_refcount_update_create_done,
	.finish_item	= xfs_refcount_update_finish_item,
	.finish_cleanup = xfs_refcount_update_finish_cleanup,
	.cancel_item	= xfs_refcount_update_cancel_item,
};

/*
 * Process a refcount update intent item that was recovered from the log.
 * We need to update the refcountbt.
 */
int
xfs_cui_recover(
	struct xfs_trans		*parent_tp,
	struct xfs_cui_log_item		*cuip)
{
	int				i;
	int				error = 0;
	unsigned int			refc_type;
	struct xfs_phys_extent		*refc;
	xfs_fsblock_t			startblock_fsb;
	bool				op_ok;
	struct xfs_cud_log_item		*cudp;
	struct xfs_trans		*tp;
	struct xfs_btree_cur		*rcur = NULL;
	enum xfs_refcount_intent_type	type;
	xfs_fsblock_t			new_fsb;
	xfs_extlen_t			new_len;
	struct xfs_bmbt_irec		irec;
	bool				requeue_only = false;
	struct xfs_mount		*mp = parent_tp->t_mountp;

	ASSERT(!test_bit(XFS_CUI_RECOVERED, &cuip->cui_flags));

	/*
	 * First check the validity of the extents described by the
	 * CUI.  If any are bad, then assume that all are bad and
	 * just toss the CUI.
	 */
	for (i = 0; i < cuip->cui_format.cui_nextents; i++) {
		refc = &cuip->cui_format.cui_extents[i];
		startblock_fsb = XFS_BB_TO_FSB(mp,
				   XFS_FSB_TO_DADDR(mp, refc->pe_startblock));
		switch (refc->pe_flags & XFS_REFCOUNT_EXTENT_TYPE_MASK) {
		case XFS_REFCOUNT_INCREASE:
		case XFS_REFCOUNT_DECREASE:
		case XFS_REFCOUNT_ALLOC_COW:
		case XFS_REFCOUNT_FREE_COW:
			op_ok = true;
			break;
		default:
			op_ok = false;
			break;
		}
		if (!op_ok || startblock_fsb == 0 ||
		    refc->pe_len == 0 ||
		    startblock_fsb >= mp->m_sb.sb_dblocks ||
		    refc->pe_len >= mp->m_sb.sb_agblocks ||
		    (refc->pe_flags & ~XFS_REFCOUNT_EXTENT_FLAGS)) {
			/*
			 * This will pull the CUI from the AIL and
			 * free the memory associated with it.
			 */
			set_bit(XFS_CUI_RECOVERED, &cuip->cui_flags);
			xfs_cui_release(cuip);
			return -EFSCORRUPTED;
		}
	}

	/*
	 * Under normal operation, refcount updates are deferred, so we
	 * wouldn't be adding them directly to a transaction.  All
	 * refcount updates manage reservation usage internally and
	 * dynamically by deferring work that won't fit in the
	 * transaction.  Normally, any work that needs to be deferred
	 * gets attached to the same defer_ops that scheduled the
	 * refcount update.  However, we're in log recovery here, so we
	 * we use the passed in defer_ops and to finish up any work that
	 * doesn't fit.  We need to reserve enough blocks to handle a
	 * full btree split on either end of the refcount range.
	 */
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate,
			mp->m_refc_maxlevels * 2, 0, XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;
	/*
	 * Recovery stashes all deferred ops during intent processing and
	 * finishes them on completion. Transfer current dfops state to this
	 * transaction and transfer the result back before we return.
	 */
	xfs_defer_move(tp, parent_tp);
	cudp = xfs_trans_get_cud(tp, cuip);

	for (i = 0; i < cuip->cui_format.cui_nextents; i++) {
		refc = &cuip->cui_format.cui_extents[i];
		refc_type = refc->pe_flags & XFS_REFCOUNT_EXTENT_TYPE_MASK;
		switch (refc_type) {
		case XFS_REFCOUNT_INCREASE:
		case XFS_REFCOUNT_DECREASE:
		case XFS_REFCOUNT_ALLOC_COW:
		case XFS_REFCOUNT_FREE_COW:
			type = refc_type;
			break;
		default:
			XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, mp);
			error = -EFSCORRUPTED;
			goto abort_error;
		}
		if (requeue_only) {
			new_fsb = refc->pe_startblock;
			new_len = refc->pe_len;
		} else
			error = xfs_trans_log_finish_refcount_update(tp, cudp,
				type, refc->pe_startblock, refc->pe_len,
				&new_fsb, &new_len, &rcur);
		if (error)
			goto abort_error;

		/* Requeue what we didn't finish. */
		if (new_len > 0) {
			irec.br_startblock = new_fsb;
			irec.br_blockcount = new_len;
			switch (type) {
			case XFS_REFCOUNT_INCREASE:
				xfs_refcount_increase_extent(tp, &irec);
				break;
			case XFS_REFCOUNT_DECREASE:
				xfs_refcount_decrease_extent(tp, &irec);
				break;
			case XFS_REFCOUNT_ALLOC_COW:
				xfs_refcount_alloc_cow_extent(tp,
						irec.br_startblock,
						irec.br_blockcount);
				break;
			case XFS_REFCOUNT_FREE_COW:
				xfs_refcount_free_cow_extent(tp,
						irec.br_startblock,
						irec.br_blockcount);
				break;
			default:
				ASSERT(0);
			}
			requeue_only = true;
		}
	}

	xfs_refcount_finish_one_cleanup(tp, rcur, error);
	set_bit(XFS_CUI_RECOVERED, &cuip->cui_flags);
	xfs_defer_move(parent_tp, tp);
	error = xfs_trans_commit(tp);
	return error;

abort_error:
	xfs_refcount_finish_one_cleanup(tp, rcur, error);
	xfs_defer_move(parent_tp, tp);
	xfs_trans_cancel(tp);
	return error;
}
