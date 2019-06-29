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
#include "xfs_bmap_item.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_inode.h"

/*
 * Finish an bmap update and log it to the BUD. Note that the
 * transaction is marked dirty regardless of whether the bmap update
 * succeeds or fails to support the BUI/BUD lifecycle rules.
 */
int
xfs_trans_log_finish_bmap_update(
	struct xfs_trans		*tp,
	struct xfs_bud_log_item		*budp,
	enum xfs_bmap_intent_type	type,
	struct xfs_inode		*ip,
	int				whichfork,
	xfs_fileoff_t			startoff,
	xfs_fsblock_t			startblock,
	xfs_filblks_t			*blockcount,
	xfs_exntst_t			state)
{
	int				error;

	error = xfs_bmap_finish_one(tp, ip, type, whichfork, startoff,
			startblock, blockcount, state);

	/*
	 * Mark the transaction dirty, even on error. This ensures the
	 * transaction is aborted, which:
	 *
	 * 1.) releases the BUI and frees the BUD
	 * 2.) shuts down the filesystem
	 */
	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &budp->bud_item.li_flags);

	return error;
}

/* Sort bmap intents by inode. */
static int
xfs_bmap_update_diff_items(
	void				*priv,
	struct list_head		*a,
	struct list_head		*b)
{
	struct xfs_bmap_intent		*ba;
	struct xfs_bmap_intent		*bb;

	ba = container_of(a, struct xfs_bmap_intent, bi_list);
	bb = container_of(b, struct xfs_bmap_intent, bi_list);
	return ba->bi_owner->i_ino - bb->bi_owner->i_ino;
}

/* Get an BUI. */
STATIC void *
xfs_bmap_update_create_intent(
	struct xfs_trans		*tp,
	unsigned int			count)
{
	struct xfs_bui_log_item		*buip;

	ASSERT(count == XFS_BUI_MAX_FAST_EXTENTS);
	ASSERT(tp != NULL);

	buip = xfs_bui_init(tp->t_mountp);
	ASSERT(buip != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	xfs_trans_add_item(tp, &buip->bui_item);
	return buip;
}

/* Set the map extent flags for this mapping. */
static void
xfs_trans_set_bmap_flags(
	struct xfs_map_extent		*bmap,
	enum xfs_bmap_intent_type	type,
	int				whichfork,
	xfs_exntst_t			state)
{
	bmap->me_flags = 0;
	switch (type) {
	case XFS_BMAP_MAP:
	case XFS_BMAP_UNMAP:
		bmap->me_flags = type;
		break;
	default:
		ASSERT(0);
	}
	if (state == XFS_EXT_UNWRITTEN)
		bmap->me_flags |= XFS_BMAP_EXTENT_UNWRITTEN;
	if (whichfork == XFS_ATTR_FORK)
		bmap->me_flags |= XFS_BMAP_EXTENT_ATTR_FORK;
}

/* Log bmap updates in the intent item. */
STATIC void
xfs_bmap_update_log_item(
	struct xfs_trans		*tp,
	void				*intent,
	struct list_head		*item)
{
	struct xfs_bui_log_item		*buip = intent;
	struct xfs_bmap_intent		*bmap;
	uint				next_extent;
	struct xfs_map_extent		*map;

	bmap = container_of(item, struct xfs_bmap_intent, bi_list);

	tp->t_flags |= XFS_TRANS_DIRTY;
	set_bit(XFS_LI_DIRTY, &buip->bui_item.li_flags);

	/*
	 * atomic_inc_return gives us the value after the increment;
	 * we want to use it as an array index so we need to subtract 1 from
	 * it.
	 */
	next_extent = atomic_inc_return(&buip->bui_next_extent) - 1;
	ASSERT(next_extent < buip->bui_format.bui_nextents);
	map = &buip->bui_format.bui_extents[next_extent];
	map->me_owner = bmap->bi_owner->i_ino;
	map->me_startblock = bmap->bi_bmap.br_startblock;
	map->me_startoff = bmap->bi_bmap.br_startoff;
	map->me_len = bmap->bi_bmap.br_blockcount;
	xfs_trans_set_bmap_flags(map, bmap->bi_type, bmap->bi_whichfork,
			bmap->bi_bmap.br_state);
}

/* Get an BUD so we can process all the deferred rmap updates. */
STATIC void *
xfs_bmap_update_create_done(
	struct xfs_trans		*tp,
	void				*intent,
	unsigned int			count)
{
	return xfs_trans_get_bud(tp, intent);
}

/* Process a deferred rmap update. */
STATIC int
xfs_bmap_update_finish_item(
	struct xfs_trans		*tp,
	struct list_head		*item,
	void				*done_item,
	void				**state)
{
	struct xfs_bmap_intent		*bmap;
	xfs_filblks_t			count;
	int				error;

	bmap = container_of(item, struct xfs_bmap_intent, bi_list);
	count = bmap->bi_bmap.br_blockcount;
	error = xfs_trans_log_finish_bmap_update(tp, done_item,
			bmap->bi_type,
			bmap->bi_owner, bmap->bi_whichfork,
			bmap->bi_bmap.br_startoff,
			bmap->bi_bmap.br_startblock,
			&count,
			bmap->bi_bmap.br_state);
	if (!error && count > 0) {
		ASSERT(bmap->bi_type == XFS_BMAP_UNMAP);
		bmap->bi_bmap.br_blockcount = count;
		return -EAGAIN;
	}
	kmem_free(bmap);
	return error;
}

/* Abort all pending BUIs. */
STATIC void
xfs_bmap_update_abort_intent(
	void				*intent)
{
	xfs_bui_release(intent);
}

/* Cancel a deferred rmap update. */
STATIC void
xfs_bmap_update_cancel_item(
	struct list_head		*item)
{
	struct xfs_bmap_intent		*bmap;

	bmap = container_of(item, struct xfs_bmap_intent, bi_list);
	kmem_free(bmap);
}

const struct xfs_defer_op_type xfs_bmap_update_defer_type = {
	.max_items	= XFS_BUI_MAX_FAST_EXTENTS,
	.diff_items	= xfs_bmap_update_diff_items,
	.create_intent	= xfs_bmap_update_create_intent,
	.abort_intent	= xfs_bmap_update_abort_intent,
	.log_item	= xfs_bmap_update_log_item,
	.create_done	= xfs_bmap_update_create_done,
	.finish_item	= xfs_bmap_update_finish_item,
	.cancel_item	= xfs_bmap_update_cancel_item,
};
