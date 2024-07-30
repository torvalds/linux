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
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_ag.h"
#include "xfs_btree.h"
#include "xfs_trace.h"

struct kmem_cache	*xfs_cui_cache;
struct kmem_cache	*xfs_cud_cache;

static const struct xfs_item_ops xfs_cui_item_ops;

static inline struct xfs_cui_log_item *CUI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_cui_log_item, cui_item);
}

STATIC void
xfs_cui_item_free(
	struct xfs_cui_log_item	*cuip)
{
	kvfree(cuip->cui_item.li_lv_shadow);
	if (cuip->cui_format.cui_nextents > XFS_CUI_MAX_FAST_EXTENTS)
		kfree(cuip);
	else
		kmem_cache_free(xfs_cui_cache, cuip);
}

/*
 * Freeing the CUI requires that we remove it from the AIL if it has already
 * been placed there. However, the CUI may not yet have been placed in the AIL
 * when called by xfs_cui_release() from CUD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the CUI.
 */
STATIC void
xfs_cui_release(
	struct xfs_cui_log_item	*cuip)
{
	ASSERT(atomic_read(&cuip->cui_refcount) > 0);
	if (!atomic_dec_and_test(&cuip->cui_refcount))
		return;

	xfs_trans_ail_delete(&cuip->cui_item, 0);
	xfs_cui_item_free(cuip);
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
 * Allocate and initialize an cui item with the given number of extents.
 */
STATIC struct xfs_cui_log_item *
xfs_cui_init(
	struct xfs_mount		*mp,
	uint				nextents)

{
	struct xfs_cui_log_item		*cuip;

	ASSERT(nextents > 0);
	if (nextents > XFS_CUI_MAX_FAST_EXTENTS)
		cuip = kzalloc(xfs_cui_log_item_sizeof(nextents),
				GFP_KERNEL | __GFP_NOFAIL);
	else
		cuip = kmem_cache_zalloc(xfs_cui_cache,
					 GFP_KERNEL | __GFP_NOFAIL);

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
	kvfree(cudp->cud_item.li_lv_shadow);
	kmem_cache_free(xfs_cud_cache, cudp);
}

static struct xfs_log_item *
xfs_cud_item_intent(
	struct xfs_log_item	*lip)
{
	return &CUD_ITEM(lip)->cud_cuip->cui_item;
}

static const struct xfs_item_ops xfs_cud_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED |
			  XFS_ITEM_INTENT_DONE,
	.iop_size	= xfs_cud_item_size,
	.iop_format	= xfs_cud_item_format,
	.iop_release	= xfs_cud_item_release,
	.iop_intent	= xfs_cud_item_intent,
};

static inline struct xfs_refcount_intent *ci_entry(const struct list_head *e)
{
	return list_entry(e, struct xfs_refcount_intent, ri_list);
}

/* Sort refcount intents by AG. */
static int
xfs_refcount_update_diff_items(
	void				*priv,
	const struct list_head		*a,
	const struct list_head		*b)
{
	struct xfs_refcount_intent	*ra = ci_entry(a);
	struct xfs_refcount_intent	*rb = ci_entry(b);

	return ra->ri_pag->pag_agno - rb->ri_pag->pag_agno;
}

/* Log refcount updates in the intent item. */
STATIC void
xfs_refcount_update_log_item(
	struct xfs_trans		*tp,
	struct xfs_cui_log_item		*cuip,
	struct xfs_refcount_intent	*ri)
{
	uint				next_extent;
	struct xfs_phys_extent		*pmap;

	/*
	 * atomic_inc_return gives us the value after the increment;
	 * we want to use it as an array index so we need to subtract 1 from
	 * it.
	 */
	next_extent = atomic_inc_return(&cuip->cui_next_extent) - 1;
	ASSERT(next_extent < cuip->cui_format.cui_nextents);
	pmap = &cuip->cui_format.cui_extents[next_extent];
	pmap->pe_startblock = ri->ri_startblock;
	pmap->pe_len = ri->ri_blockcount;

	pmap->pe_flags = 0;
	switch (ri->ri_type) {
	case XFS_REFCOUNT_INCREASE:
	case XFS_REFCOUNT_DECREASE:
	case XFS_REFCOUNT_ALLOC_COW:
	case XFS_REFCOUNT_FREE_COW:
		pmap->pe_flags |= ri->ri_type;
		break;
	default:
		ASSERT(0);
	}
}

static struct xfs_log_item *
xfs_refcount_update_create_intent(
	struct xfs_trans		*tp,
	struct list_head		*items,
	unsigned int			count,
	bool				sort)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_cui_log_item		*cuip = xfs_cui_init(mp, count);
	struct xfs_refcount_intent	*ri;

	ASSERT(count > 0);

	if (sort)
		list_sort(mp, items, xfs_refcount_update_diff_items);
	list_for_each_entry(ri, items, ri_list)
		xfs_refcount_update_log_item(tp, cuip, ri);
	return &cuip->cui_item;
}

/* Get an CUD so we can process all the deferred refcount updates. */
static struct xfs_log_item *
xfs_refcount_update_create_done(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	unsigned int			count)
{
	struct xfs_cui_log_item		*cuip = CUI_ITEM(intent);
	struct xfs_cud_log_item		*cudp;

	cudp = kmem_cache_zalloc(xfs_cud_cache, GFP_KERNEL | __GFP_NOFAIL);
	xfs_log_item_init(tp->t_mountp, &cudp->cud_item, XFS_LI_CUD,
			  &xfs_cud_item_ops);
	cudp->cud_cuip = cuip;
	cudp->cud_format.cud_cui_id = cuip->cui_format.cui_id;

	return &cudp->cud_item;
}

/* Add this deferred CUI to the transaction. */
void
xfs_refcount_defer_add(
	struct xfs_trans		*tp,
	struct xfs_refcount_intent	*ri)
{
	struct xfs_mount		*mp = tp->t_mountp;

	trace_xfs_refcount_defer(mp, ri);

	ri->ri_pag = xfs_perag_intent_get(mp, ri->ri_startblock);
	xfs_defer_add(tp, &ri->ri_list, &xfs_refcount_update_defer_type);
}

/* Cancel a deferred refcount update. */
STATIC void
xfs_refcount_update_cancel_item(
	struct list_head		*item)
{
	struct xfs_refcount_intent	*ri = ci_entry(item);

	xfs_perag_intent_put(ri->ri_pag);
	kmem_cache_free(xfs_refcount_intent_cache, ri);
}

/* Process a deferred refcount update. */
STATIC int
xfs_refcount_update_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_refcount_intent	*ri = ci_entry(item);
	int				error;

	/* Did we run out of reservation?  Requeue what we didn't finish. */
	error = xfs_refcount_finish_one(tp, ri, state);
	if (!error && ri->ri_blockcount > 0) {
		ASSERT(ri->ri_type == XFS_REFCOUNT_INCREASE ||
		       ri->ri_type == XFS_REFCOUNT_DECREASE);
		return -EAGAIN;
	}

	xfs_refcount_update_cancel_item(item);
	return error;
}

/* Clean up after calling xfs_refcount_finish_one. */
STATIC void
xfs_refcount_finish_one_cleanup(
	struct xfs_trans	*tp,
	struct xfs_btree_cur	*rcur,
	int			error)
{
	struct xfs_buf		*agbp;

	if (rcur == NULL)
		return;
	agbp = rcur->bc_ag.agbp;
	xfs_btree_del_cursor(rcur, error);
	if (error)
		xfs_trans_brelse(tp, agbp);
}

/* Abort all pending CUIs. */
STATIC void
xfs_refcount_update_abort_intent(
	struct xfs_log_item		*intent)
{
	xfs_cui_release(CUI_ITEM(intent));
}

/* Is this recovered CUI ok? */
static inline bool
xfs_cui_validate_phys(
	struct xfs_mount		*mp,
	struct xfs_phys_extent		*pmap)
{
	if (!xfs_has_reflink(mp))
		return false;

	if (pmap->pe_flags & ~XFS_REFCOUNT_EXTENT_FLAGS)
		return false;

	switch (pmap->pe_flags & XFS_REFCOUNT_EXTENT_TYPE_MASK) {
	case XFS_REFCOUNT_INCREASE:
	case XFS_REFCOUNT_DECREASE:
	case XFS_REFCOUNT_ALLOC_COW:
	case XFS_REFCOUNT_FREE_COW:
		break;
	default:
		return false;
	}

	return xfs_verify_fsbext(mp, pmap->pe_startblock, pmap->pe_len);
}

static inline void
xfs_cui_recover_work(
	struct xfs_mount		*mp,
	struct xfs_defer_pending	*dfp,
	struct xfs_phys_extent		*pmap)
{
	struct xfs_refcount_intent	*ri;

	ri = kmem_cache_alloc(xfs_refcount_intent_cache,
			GFP_KERNEL | __GFP_NOFAIL);
	ri->ri_type = pmap->pe_flags & XFS_REFCOUNT_EXTENT_TYPE_MASK;
	ri->ri_startblock = pmap->pe_startblock;
	ri->ri_blockcount = pmap->pe_len;
	ri->ri_pag = xfs_perag_intent_get(mp, pmap->pe_startblock);

	xfs_defer_add_item(dfp, &ri->ri_list);
}

/*
 * Process a refcount update intent item that was recovered from the log.
 * We need to update the refcountbt.
 */
STATIC int
xfs_refcount_recover_work(
	struct xfs_defer_pending	*dfp,
	struct list_head		*capture_list)
{
	struct xfs_trans_res		resv;
	struct xfs_log_item		*lip = dfp->dfp_intent;
	struct xfs_cui_log_item		*cuip = CUI_ITEM(lip);
	struct xfs_trans		*tp;
	struct xfs_mount		*mp = lip->li_log->l_mp;
	int				i;
	int				error = 0;

	/*
	 * First check the validity of the extents described by the
	 * CUI.  If any are bad, then assume that all are bad and
	 * just toss the CUI.
	 */
	for (i = 0; i < cuip->cui_format.cui_nextents; i++) {
		if (!xfs_cui_validate_phys(mp,
					&cuip->cui_format.cui_extents[i])) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					&cuip->cui_format,
					sizeof(cuip->cui_format));
			return -EFSCORRUPTED;
		}

		xfs_cui_recover_work(mp, dfp, &cuip->cui_format.cui_extents[i]);
	}

	/*
	 * Under normal operation, refcount updates are deferred, so we
	 * wouldn't be adding them directly to a transaction.  All
	 * refcount updates manage reservation usage internally and
	 * dynamically by deferring work that won't fit in the
	 * transaction.  Normally, any work that needs to be deferred
	 * gets attached to the same defer_ops that scheduled the
	 * refcount update.  However, we're in log recovery here, so we
	 * use the passed in defer_ops and to finish up any work that
	 * doesn't fit.  We need to reserve enough blocks to handle a
	 * full btree split on either end of the refcount range.
	 */
	resv = xlog_recover_resv(&M_RES(mp)->tr_itruncate);
	error = xfs_trans_alloc(mp, &resv, mp->m_refc_maxlevels * 2, 0,
			XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;

	error = xlog_recover_finish_intent(tp, dfp);
	if (error == -EFSCORRUPTED)
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				&cuip->cui_format,
				sizeof(cuip->cui_format));
	if (error)
		goto abort_error;

	return xfs_defer_ops_capture_and_commit(tp, capture_list);

abort_error:
	xfs_trans_cancel(tp);
	return error;
}

/* Relog an intent item to push the log tail forward. */
static struct xfs_log_item *
xfs_refcount_relog_intent(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	struct xfs_log_item		*done_item)
{
	struct xfs_cui_log_item		*cuip;
	struct xfs_phys_extent		*pmap;
	unsigned int			count;

	count = CUI_ITEM(intent)->cui_format.cui_nextents;
	pmap = CUI_ITEM(intent)->cui_format.cui_extents;

	cuip = xfs_cui_init(tp->t_mountp, count);
	memcpy(cuip->cui_format.cui_extents, pmap, count * sizeof(*pmap));
	atomic_set(&cuip->cui_next_extent, count);

	return &cuip->cui_item;
}

const struct xfs_defer_op_type xfs_refcount_update_defer_type = {
	.name		= "refcount",
	.max_items	= XFS_CUI_MAX_FAST_EXTENTS,
	.create_intent	= xfs_refcount_update_create_intent,
	.abort_intent	= xfs_refcount_update_abort_intent,
	.create_done	= xfs_refcount_update_create_done,
	.finish_item	= xfs_refcount_update_finish_item,
	.finish_cleanup = xfs_refcount_finish_one_cleanup,
	.cancel_item	= xfs_refcount_update_cancel_item,
	.recover_work	= xfs_refcount_recover_work,
	.relog_intent	= xfs_refcount_relog_intent,
};

STATIC bool
xfs_cui_item_match(
	struct xfs_log_item	*lip,
	uint64_t		intent_id)
{
	return CUI_ITEM(lip)->cui_format.cui_id == intent_id;
}

static const struct xfs_item_ops xfs_cui_item_ops = {
	.flags		= XFS_ITEM_INTENT,
	.iop_size	= xfs_cui_item_size,
	.iop_format	= xfs_cui_item_format,
	.iop_unpin	= xfs_cui_item_unpin,
	.iop_release	= xfs_cui_item_release,
	.iop_match	= xfs_cui_item_match,
};

static inline void
xfs_cui_copy_format(
	struct xfs_cui_log_format	*dst,
	const struct xfs_cui_log_format	*src)
{
	unsigned int			i;

	memcpy(dst, src, offsetof(struct xfs_cui_log_format, cui_extents));

	for (i = 0; i < src->cui_nextents; i++)
		memcpy(&dst->cui_extents[i], &src->cui_extents[i],
				sizeof(struct xfs_phys_extent));
}

/*
 * This routine is called to create an in-core extent refcount update
 * item from the cui format structure which was logged on disk.
 * It allocates an in-core cui, copies the extents from the format
 * structure into it, and adds the cui to the AIL with the given
 * LSN.
 */
STATIC int
xlog_recover_cui_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_cui_log_item		*cuip;
	struct xfs_cui_log_format	*cui_formatp;
	size_t				len;

	cui_formatp = item->ri_buf[0].i_addr;

	if (item->ri_buf[0].i_len < xfs_cui_log_format_sizeof(0)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	len = xfs_cui_log_format_sizeof(cui_formatp->cui_nextents);
	if (item->ri_buf[0].i_len != len) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	cuip = xfs_cui_init(mp, cui_formatp->cui_nextents);
	xfs_cui_copy_format(&cuip->cui_format, cui_formatp);
	atomic_set(&cuip->cui_next_extent, cui_formatp->cui_nextents);

	xlog_recover_intent_item(log, &cuip->cui_item, lsn,
			&xfs_refcount_update_defer_type);
	return 0;
}

const struct xlog_recover_item_ops xlog_cui_item_ops = {
	.item_type		= XFS_LI_CUI,
	.commit_pass2		= xlog_recover_cui_commit_pass2,
};

/*
 * This routine is called when an CUD format structure is found in a committed
 * transaction in the log. Its purpose is to cancel the corresponding CUI if it
 * was still in the log. To do this it searches the AIL for the CUI with an id
 * equal to that in the CUD format structure. If we find it we drop the CUD
 * reference, which removes the CUI from the AIL and frees it.
 */
STATIC int
xlog_recover_cud_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_cud_log_format	*cud_formatp;

	cud_formatp = item->ri_buf[0].i_addr;
	if (item->ri_buf[0].i_len != sizeof(struct xfs_cud_log_format)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	xlog_recover_release_intent(log, XFS_LI_CUI, cud_formatp->cud_cui_id);
	return 0;
}

const struct xlog_recover_item_ops xlog_cud_item_ops = {
	.item_type		= XFS_LI_CUD,
	.commit_pass2		= xlog_recover_cud_commit_pass2,
};
