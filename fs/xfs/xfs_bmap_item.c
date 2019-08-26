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
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_bmap_item.h"
#include "xfs_log.h"
#include "xfs_bmap.h"
#include "xfs_icache.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"


kmem_zone_t	*xfs_bui_zone;
kmem_zone_t	*xfs_bud_zone;

static inline struct xfs_bui_log_item *BUI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_bui_log_item, bui_item);
}

void
xfs_bui_item_free(
	struct xfs_bui_log_item	*buip)
{
	kmem_zone_free(xfs_bui_zone, buip);
}

/*
 * Freeing the BUI requires that we remove it from the AIL if it has already
 * been placed there. However, the BUI may not yet have been placed in the AIL
 * when called by xfs_bui_release() from BUD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the BUI.
 */
void
xfs_bui_release(
	struct xfs_bui_log_item	*buip)
{
	ASSERT(atomic_read(&buip->bui_refcount) > 0);
	if (atomic_dec_and_test(&buip->bui_refcount)) {
		xfs_trans_ail_remove(&buip->bui_item, SHUTDOWN_LOG_IO_ERROR);
		xfs_bui_item_free(buip);
	}
}


STATIC void
xfs_bui_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_bui_log_item	*buip = BUI_ITEM(lip);

	*nvecs += 1;
	*nbytes += xfs_bui_log_format_sizeof(buip->bui_format.bui_nextents);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given bui log item. We use only 1 iovec, and we point that
 * at the bui_log_format structure embedded in the bui item.
 * It is at this point that we assert that all of the extent
 * slots in the bui item have been filled.
 */
STATIC void
xfs_bui_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_bui_log_item	*buip = BUI_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	ASSERT(atomic_read(&buip->bui_next_extent) ==
			buip->bui_format.bui_nextents);

	buip->bui_format.bui_type = XFS_LI_BUI;
	buip->bui_format.bui_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_BUI_FORMAT, &buip->bui_format,
			xfs_bui_log_format_sizeof(buip->bui_format.bui_nextents));
}

/*
 * The unpin operation is the last place an BUI is manipulated in the log. It is
 * either inserted in the AIL or aborted in the event of a log I/O error. In
 * either case, the BUI transaction has been successfully committed to make it
 * this far. Therefore, we expect whoever committed the BUI to either construct
 * and commit the BUD or drop the BUD's reference in the event of error. Simply
 * drop the log's BUI reference now that the log is done with it.
 */
STATIC void
xfs_bui_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_bui_log_item	*buip = BUI_ITEM(lip);

	xfs_bui_release(buip);
}

/*
 * The BUI has been either committed or aborted if the transaction has been
 * cancelled. If the transaction was cancelled, an BUD isn't going to be
 * constructed and thus we free the BUI here directly.
 */
STATIC void
xfs_bui_item_release(
	struct xfs_log_item	*lip)
{
	xfs_bui_release(BUI_ITEM(lip));
}

static const struct xfs_item_ops xfs_bui_item_ops = {
	.iop_size	= xfs_bui_item_size,
	.iop_format	= xfs_bui_item_format,
	.iop_unpin	= xfs_bui_item_unpin,
	.iop_release	= xfs_bui_item_release,
};

/*
 * Allocate and initialize an bui item with the given number of extents.
 */
struct xfs_bui_log_item *
xfs_bui_init(
	struct xfs_mount		*mp)

{
	struct xfs_bui_log_item		*buip;

	buip = kmem_zone_zalloc(xfs_bui_zone, 0);

	xfs_log_item_init(mp, &buip->bui_item, XFS_LI_BUI, &xfs_bui_item_ops);
	buip->bui_format.bui_nextents = XFS_BUI_MAX_FAST_EXTENTS;
	buip->bui_format.bui_id = (uintptr_t)(void *)buip;
	atomic_set(&buip->bui_next_extent, 0);
	atomic_set(&buip->bui_refcount, 2);

	return buip;
}

static inline struct xfs_bud_log_item *BUD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_bud_log_item, bud_item);
}

STATIC void
xfs_bud_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += sizeof(struct xfs_bud_log_format);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given bud log item. We use only 1 iovec, and we point that
 * at the bud_log_format structure embedded in the bud item.
 * It is at this point that we assert that all of the extent
 * slots in the bud item have been filled.
 */
STATIC void
xfs_bud_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_bud_log_item	*budp = BUD_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	budp->bud_format.bud_type = XFS_LI_BUD;
	budp->bud_format.bud_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_BUD_FORMAT, &budp->bud_format,
			sizeof(struct xfs_bud_log_format));
}

/*
 * The BUD is either committed or aborted if the transaction is cancelled. If
 * the transaction is cancelled, drop our reference to the BUI and free the
 * BUD.
 */
STATIC void
xfs_bud_item_release(
	struct xfs_log_item	*lip)
{
	struct xfs_bud_log_item	*budp = BUD_ITEM(lip);

	xfs_bui_release(budp->bud_buip);
	kmem_zone_free(xfs_bud_zone, budp);
}

static const struct xfs_item_ops xfs_bud_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED,
	.iop_size	= xfs_bud_item_size,
	.iop_format	= xfs_bud_item_format,
	.iop_release	= xfs_bud_item_release,
};

static struct xfs_bud_log_item *
xfs_trans_get_bud(
	struct xfs_trans		*tp,
	struct xfs_bui_log_item		*buip)
{
	struct xfs_bud_log_item		*budp;

	budp = kmem_zone_zalloc(xfs_bud_zone, 0);
	xfs_log_item_init(tp->t_mountp, &budp->bud_item, XFS_LI_BUD,
			  &xfs_bud_item_ops);
	budp->bud_buip = buip;
	budp->bud_format.bud_bui_id = buip->bui_format.bui_id;

	xfs_trans_add_item(tp, &budp->bud_item);
	return budp;
}

/*
 * Finish an bmap update and log it to the BUD. Note that the
 * transaction is marked dirty regardless of whether the bmap update
 * succeeds or fails to support the BUI/BUD lifecycle rules.
 */
static int
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

/*
 * Process a bmap update intent item that was recovered from the log.
 * We need to update some inode's bmbt.
 */
int
xfs_bui_recover(
	struct xfs_trans		*parent_tp,
	struct xfs_bui_log_item		*buip)
{
	int				error = 0;
	unsigned int			bui_type;
	struct xfs_map_extent		*bmap;
	xfs_fsblock_t			startblock_fsb;
	xfs_fsblock_t			inode_fsb;
	xfs_filblks_t			count;
	bool				op_ok;
	struct xfs_bud_log_item		*budp;
	enum xfs_bmap_intent_type	type;
	int				whichfork;
	xfs_exntst_t			state;
	struct xfs_trans		*tp;
	struct xfs_inode		*ip = NULL;
	struct xfs_bmbt_irec		irec;
	struct xfs_mount		*mp = parent_tp->t_mountp;

	ASSERT(!test_bit(XFS_BUI_RECOVERED, &buip->bui_flags));

	/* Only one mapping operation per BUI... */
	if (buip->bui_format.bui_nextents != XFS_BUI_MAX_FAST_EXTENTS) {
		set_bit(XFS_BUI_RECOVERED, &buip->bui_flags);
		xfs_bui_release(buip);
		return -EIO;
	}

	/*
	 * First check the validity of the extent described by the
	 * BUI.  If anything is bad, then toss the BUI.
	 */
	bmap = &buip->bui_format.bui_extents[0];
	startblock_fsb = XFS_BB_TO_FSB(mp,
			   XFS_FSB_TO_DADDR(mp, bmap->me_startblock));
	inode_fsb = XFS_BB_TO_FSB(mp, XFS_FSB_TO_DADDR(mp,
			XFS_INO_TO_FSB(mp, bmap->me_owner)));
	switch (bmap->me_flags & XFS_BMAP_EXTENT_TYPE_MASK) {
	case XFS_BMAP_MAP:
	case XFS_BMAP_UNMAP:
		op_ok = true;
		break;
	default:
		op_ok = false;
		break;
	}
	if (!op_ok || startblock_fsb == 0 ||
	    bmap->me_len == 0 ||
	    inode_fsb == 0 ||
	    startblock_fsb >= mp->m_sb.sb_dblocks ||
	    bmap->me_len >= mp->m_sb.sb_agblocks ||
	    inode_fsb >= mp->m_sb.sb_dblocks ||
	    (bmap->me_flags & ~XFS_BMAP_EXTENT_FLAGS)) {
		/*
		 * This will pull the BUI from the AIL and
		 * free the memory associated with it.
		 */
		set_bit(XFS_BUI_RECOVERED, &buip->bui_flags);
		xfs_bui_release(buip);
		return -EIO;
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate,
			XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK), 0, 0, &tp);
	if (error)
		return error;
	/*
	 * Recovery stashes all deferred ops during intent processing and
	 * finishes them on completion. Transfer current dfops state to this
	 * transaction and transfer the result back before we return.
	 */
	xfs_defer_move(tp, parent_tp);
	budp = xfs_trans_get_bud(tp, buip);

	/* Grab the inode. */
	error = xfs_iget(mp, tp, bmap->me_owner, 0, XFS_ILOCK_EXCL, &ip);
	if (error)
		goto err_inode;

	if (VFS_I(ip)->i_nlink == 0)
		xfs_iflags_set(ip, XFS_IRECOVERY);

	/* Process deferred bmap item. */
	state = (bmap->me_flags & XFS_BMAP_EXTENT_UNWRITTEN) ?
			XFS_EXT_UNWRITTEN : XFS_EXT_NORM;
	whichfork = (bmap->me_flags & XFS_BMAP_EXTENT_ATTR_FORK) ?
			XFS_ATTR_FORK : XFS_DATA_FORK;
	bui_type = bmap->me_flags & XFS_BMAP_EXTENT_TYPE_MASK;
	switch (bui_type) {
	case XFS_BMAP_MAP:
	case XFS_BMAP_UNMAP:
		type = bui_type;
		break;
	default:
		error = -EFSCORRUPTED;
		goto err_inode;
	}
	xfs_trans_ijoin(tp, ip, 0);

	count = bmap->me_len;
	error = xfs_trans_log_finish_bmap_update(tp, budp, type, ip, whichfork,
			bmap->me_startoff, bmap->me_startblock, &count, state);
	if (error)
		goto err_inode;

	if (count > 0) {
		ASSERT(type == XFS_BMAP_UNMAP);
		irec.br_startblock = bmap->me_startblock;
		irec.br_blockcount = count;
		irec.br_startoff = bmap->me_startoff;
		irec.br_state = state;
		error = xfs_bmap_unmap_extent(tp, ip, &irec);
		if (error)
			goto err_inode;
	}

	set_bit(XFS_BUI_RECOVERED, &buip->bui_flags);
	xfs_defer_move(parent_tp, tp);
	error = xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_irele(ip);

	return error;

err_inode:
	xfs_defer_move(parent_tp, tp);
	xfs_trans_cancel(tp);
	if (ip) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_irele(ip);
	}
	return error;
}
