// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_refcount_item.h"
#include "xfs_alloc.h"
#include "xfs_refcount.h"

/*
 * This routine is called to allocate a "refcount update done"
 * log item.
 */
struct xfs_cud_log_item *
xfs_trans_get_cud(
	struct xfs_trans		*tp,
	struct xfs_cui_log_item		*cuip)
{
	struct xfs_cud_log_item		*cudp;

	cudp = xfs_cud_init(tp->t_mountp, cuip);
	xfs_trans_add_item(tp, &cudp->cud_item);
	return cudp;
}

/*
 * Finish an refcount update and log it to the CUD. Note that the
 * transaction is marked dirty regardless of whether the refcount
 * update succeeds or fails to support the CUI/CUD lifecycle rules.
 */
int
xfs_trans_log_finish_refcount_update(
	struct xfs_trans		*tp,
	struct xfs_cud_log_item		*cudp,
	struct xfs_defer_ops		*dop,
	enum xfs_refcount_intent_type	type,
	xfs_fsblock_t			startblock,
	xfs_extlen_t			blockcount,
	xfs_fsblock_t			*new_fsb,
	xfs_extlen_t			*new_len,
	struct xfs_btree_cur		**pcur)
{
	int				error;

	error = xfs_refcount_finish_one(tp, dop, type, startblock,
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
	struct xfs_defer_ops		*dop,
	struct list_head		*item,
	void				*done_item,
	void				**state)
{
	struct xfs_refcount_intent	*refc;
	xfs_fsblock_t			new_fsb;
	xfs_extlen_t			new_aglen;
	int				error;

	refc = container_of(item, struct xfs_refcount_intent, ri_list);
	error = xfs_trans_log_finish_refcount_update(tp, done_item, dop,
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

static const struct xfs_defer_op_type xfs_refcount_update_defer_type = {
	.type		= XFS_DEFER_OPS_TYPE_REFCOUNT,
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

/* Register the deferred op type. */
void
xfs_refcount_update_init_defer_op(void)
{
	xfs_defer_init_op_type(&xfs_refcount_update_defer_type);
}
