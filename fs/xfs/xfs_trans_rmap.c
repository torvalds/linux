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
#include "xfs_rmap_item.h"
#include "xfs_alloc.h"
#include "xfs_rmap.h"
#include "xfs_defer.h"

/* Set the map extent flags for this reverse mapping. */
static void
xfs_trans_set_rmap_flags(
	struct xfs_map_extent		*rmap,
	enum xfs_rmap_intent_type	type,
	int				whichfork,
	xfs_exntst_t			state)
{
	rmap->me_flags = 0;
	if (state == XFS_EXT_UNWRITTEN)
		rmap->me_flags |= XFS_RMAP_EXTENT_UNWRITTEN;
	if (whichfork == XFS_ATTR_FORK)
		rmap->me_flags |= XFS_RMAP_EXTENT_ATTR_FORK;
	switch (type) {
	case XFS_RMAP_MAP:
		rmap->me_flags |= XFS_RMAP_EXTENT_MAP;
		break;
	case XFS_RMAP_MAP_SHARED:
		rmap->me_flags |= XFS_RMAP_EXTENT_MAP_SHARED;
		break;
	case XFS_RMAP_UNMAP:
		rmap->me_flags |= XFS_RMAP_EXTENT_UNMAP;
		break;
	case XFS_RMAP_UNMAP_SHARED:
		rmap->me_flags |= XFS_RMAP_EXTENT_UNMAP_SHARED;
		break;
	case XFS_RMAP_CONVERT:
		rmap->me_flags |= XFS_RMAP_EXTENT_CONVERT;
		break;
	case XFS_RMAP_CONVERT_SHARED:
		rmap->me_flags |= XFS_RMAP_EXTENT_CONVERT_SHARED;
		break;
	case XFS_RMAP_ALLOC:
		rmap->me_flags |= XFS_RMAP_EXTENT_ALLOC;
		break;
	case XFS_RMAP_FREE:
		rmap->me_flags |= XFS_RMAP_EXTENT_FREE;
		break;
	default:
		ASSERT(0);
	}
}

struct xfs_rud_log_item *
xfs_trans_get_rud(
	struct xfs_trans		*tp,
	struct xfs_rui_log_item		*ruip)
{
	struct xfs_rud_log_item		*rudp;

	rudp = xfs_rud_init(tp->t_mountp, ruip);
	xfs_trans_add_item(tp, &rudp->rud_item);
	return rudp;
}

/*
 * Finish an rmap update and log it to the RUD. Note that the transaction is
 * marked dirty regardless of whether the rmap update succeeds or fails to
 * support the RUI/RUD lifecycle rules.
 */
int
xfs_trans_log_finish_rmap_update(
	struct xfs_trans		*tp,
	struct xfs_rud_log_item		*rudp,
	enum xfs_rmap_intent_type	type,
	uint64_t			owner,
	int				whichfork,
	xfs_fileoff_t			startoff,
	xfs_fsblock_t			startblock,
	xfs_filblks_t			blockcount,
	xfs_exntst_t			state,
	struct xfs_btree_cur		**pcur)
{
	int				error;

	error = xfs_rmap_finish_one(tp, type, owner, whichfork, startoff,
			startblock, blockcount, state, pcur);

	/*
	 * Mark the transaction dirty, even on error. This ensures the
	 * transaction is aborted, which:
	 *
	 * 1.) releases the RUI and frees the RUD
	 * 2.) shuts down the filesystem
	 */
	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &rudp->rud_item.li_flags);

	return error;
}

/* Sort rmap intents by AG. */
static int
xfs_rmap_update_diff_items(
	void				*priv,
	struct list_head		*a,
	struct list_head		*b)
{
	struct xfs_mount		*mp = priv;
	struct xfs_rmap_intent		*ra;
	struct xfs_rmap_intent		*rb;

	ra = container_of(a, struct xfs_rmap_intent, ri_list);
	rb = container_of(b, struct xfs_rmap_intent, ri_list);
	return  XFS_FSB_TO_AGNO(mp, ra->ri_bmap.br_startblock) -
		XFS_FSB_TO_AGNO(mp, rb->ri_bmap.br_startblock);
}

/* Get an RUI. */
STATIC void *
xfs_rmap_update_create_intent(
	struct xfs_trans		*tp,
	unsigned int			count)
{
	struct xfs_rui_log_item		*ruip;

	ASSERT(tp != NULL);
	ASSERT(count > 0);

	ruip = xfs_rui_init(tp->t_mountp, count);
	ASSERT(ruip != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	xfs_trans_add_item(tp, &ruip->rui_item);
	return ruip;
}

/* Log rmap updates in the intent item. */
STATIC void
xfs_rmap_update_log_item(
	struct xfs_trans		*tp,
	void				*intent,
	struct list_head		*item)
{
	struct xfs_rui_log_item		*ruip = intent;
	struct xfs_rmap_intent		*rmap;
	uint				next_extent;
	struct xfs_map_extent		*map;

	rmap = container_of(item, struct xfs_rmap_intent, ri_list);

	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &ruip->rui_item.li_flags);

	/*
	 * atomic_inc_return gives us the value after the increment;
	 * we want to use it as an array index so we need to subtract 1 from
	 * it.
	 */
	next_extent = atomic_inc_return(&ruip->rui_next_extent) - 1;
	ASSERT(next_extent < ruip->rui_format.rui_nextents);
	map = &ruip->rui_format.rui_extents[next_extent];
	map->me_owner = rmap->ri_owner;
	map->me_startblock = rmap->ri_bmap.br_startblock;
	map->me_startoff = rmap->ri_bmap.br_startoff;
	map->me_len = rmap->ri_bmap.br_blockcount;
	xfs_trans_set_rmap_flags(map, rmap->ri_type, rmap->ri_whichfork,
			rmap->ri_bmap.br_state);
}

/* Get an RUD so we can process all the deferred rmap updates. */
STATIC void *
xfs_rmap_update_create_done(
	struct xfs_trans		*tp,
	void				*intent,
	unsigned int			count)
{
	return xfs_trans_get_rud(tp, intent);
}

/* Process a deferred rmap update. */
STATIC int
xfs_rmap_update_finish_item(
	struct xfs_trans		*tp,
	struct list_head		*item,
	void				*done_item,
	void				**state)
{
	struct xfs_rmap_intent		*rmap;
	int				error;

	rmap = container_of(item, struct xfs_rmap_intent, ri_list);
	error = xfs_trans_log_finish_rmap_update(tp, done_item,
			rmap->ri_type,
			rmap->ri_owner, rmap->ri_whichfork,
			rmap->ri_bmap.br_startoff,
			rmap->ri_bmap.br_startblock,
			rmap->ri_bmap.br_blockcount,
			rmap->ri_bmap.br_state,
			(struct xfs_btree_cur **)state);
	kmem_free(rmap);
	return error;
}

/* Clean up after processing deferred rmaps. */
STATIC void
xfs_rmap_update_finish_cleanup(
	struct xfs_trans	*tp,
	void			*state,
	int			error)
{
	struct xfs_btree_cur	*rcur = state;

	xfs_rmap_finish_one_cleanup(tp, rcur, error);
}

/* Abort all pending RUIs. */
STATIC void
xfs_rmap_update_abort_intent(
	void				*intent)
{
	xfs_rui_release(intent);
}

/* Cancel a deferred rmap update. */
STATIC void
xfs_rmap_update_cancel_item(
	struct list_head		*item)
{
	struct xfs_rmap_intent		*rmap;

	rmap = container_of(item, struct xfs_rmap_intent, ri_list);
	kmem_free(rmap);
}

const struct xfs_defer_op_type xfs_rmap_update_defer_type = {
	.max_items	= XFS_RUI_MAX_FAST_EXTENTS,
	.diff_items	= xfs_rmap_update_diff_items,
	.create_intent	= xfs_rmap_update_create_intent,
	.abort_intent	= xfs_rmap_update_abort_intent,
	.log_item	= xfs_rmap_update_log_item,
	.create_done	= xfs_rmap_update_create_done,
	.finish_item	= xfs_rmap_update_finish_item,
	.finish_cleanup = xfs_rmap_update_finish_cleanup,
	.cancel_item	= xfs_rmap_update_cancel_item,
};
