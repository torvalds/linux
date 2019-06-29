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
#include "xfs_buf_item.h"
#include "xfs_refcount_item.h"
#include "xfs_log.h"
#include "xfs_refcount.h"


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

/*
 * This is the ops vector shared by all cui log items.
 */
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
				KM_SLEEP);
	else
		cuip = kmem_zone_zalloc(xfs_cui_zone, KM_SLEEP);

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

/*
 * When the cud item is committed to disk, all we need to do is delete our
 * reference to our partner cui item and then free ourselves. Since we're
 * freeing ourselves we must return -1 to keep the transaction code from
 * further referencing this item.
 */
STATIC xfs_lsn_t
xfs_cud_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_cud_log_item	*cudp = CUD_ITEM(lip);

	/*
	 * Drop the CUI reference regardless of whether the CUD has been
	 * aborted. Once the CUD transaction is constructed, it is the sole
	 * responsibility of the CUD to release the CUI (even if the CUI is
	 * aborted due to log I/O error).
	 */
	xfs_cui_release(cudp->cud_cuip);
	kmem_zone_free(xfs_cud_zone, cudp);

	return (xfs_lsn_t)-1;
}

/*
 * This is the ops vector shared by all cud log items.
 */
static const struct xfs_item_ops xfs_cud_item_ops = {
	.iop_size	= xfs_cud_item_size,
	.iop_format	= xfs_cud_item_format,
	.iop_release	= xfs_cud_item_release,
	.iop_committed	= xfs_cud_item_committed,
};

/*
 * Allocate and initialize an cud item with the given number of extents.
 */
struct xfs_cud_log_item *
xfs_cud_init(
	struct xfs_mount		*mp,
	struct xfs_cui_log_item		*cuip)

{
	struct xfs_cud_log_item	*cudp;

	cudp = kmem_zone_zalloc(xfs_cud_zone, KM_SLEEP);
	xfs_log_item_init(mp, &cudp->cud_item, XFS_LI_CUD, &xfs_cud_item_ops);
	cudp->cud_cuip = cuip;
	cudp->cud_format.cud_cui_id = cuip->cui_format.cui_id;

	return cudp;
}

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
			return -EIO;
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
				error = xfs_refcount_increase_extent(tp, &irec);
				break;
			case XFS_REFCOUNT_DECREASE:
				error = xfs_refcount_decrease_extent(tp, &irec);
				break;
			case XFS_REFCOUNT_ALLOC_COW:
				error = xfs_refcount_alloc_cow_extent(tp,
						irec.br_startblock,
						irec.br_blockcount);
				break;
			case XFS_REFCOUNT_FREE_COW:
				error = xfs_refcount_free_cow_extent(tp,
						irec.br_startblock,
						irec.br_blockcount);
				break;
			default:
				ASSERT(0);
			}
			if (error)
				goto abort_error;
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
