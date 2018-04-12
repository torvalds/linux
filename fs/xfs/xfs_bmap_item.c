/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_bmap_item.h"
#include "xfs_log.h"
#include "xfs_bmap.h"
#include "xfs_icache.h"
#include "xfs_trace.h"
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
 * Pinning has no meaning for an bui item, so just return.
 */
STATIC void
xfs_bui_item_pin(
	struct xfs_log_item	*lip)
{
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
 * BUI items have no locking or pushing.  However, since BUIs are pulled from
 * the AIL when their corresponding BUDs are committed to disk, their situation
 * is very similar to being pinned.  Return XFS_ITEM_PINNED so that the caller
 * will eventually flush the log.  This should help in getting the BUI out of
 * the AIL.
 */
STATIC uint
xfs_bui_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	return XFS_ITEM_PINNED;
}

/*
 * The BUI has been either committed or aborted if the transaction has been
 * cancelled. If the transaction was cancelled, an BUD isn't going to be
 * constructed and thus we free the BUI here directly.
 */
STATIC void
xfs_bui_item_unlock(
	struct xfs_log_item	*lip)
{
	if (lip->li_flags & XFS_LI_ABORTED)
		xfs_bui_release(BUI_ITEM(lip));
}

/*
 * The BUI is logged only once and cannot be moved in the log, so simply return
 * the lsn at which it's been logged.
 */
STATIC xfs_lsn_t
xfs_bui_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	return lsn;
}

/*
 * The BUI dependency tracking op doesn't do squat.  It can't because
 * it doesn't know where the free extent is coming from.  The dependency
 * tracking has to be handled by the "enclosing" metadata object.  For
 * example, for inodes, the inode is locked throughout the extent freeing
 * so the dependency should be recorded there.
 */
STATIC void
xfs_bui_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
}

/*
 * This is the ops vector shared by all bui log items.
 */
static const struct xfs_item_ops xfs_bui_item_ops = {
	.iop_size	= xfs_bui_item_size,
	.iop_format	= xfs_bui_item_format,
	.iop_pin	= xfs_bui_item_pin,
	.iop_unpin	= xfs_bui_item_unpin,
	.iop_unlock	= xfs_bui_item_unlock,
	.iop_committed	= xfs_bui_item_committed,
	.iop_push	= xfs_bui_item_push,
	.iop_committing = xfs_bui_item_committing,
};

/*
 * Allocate and initialize an bui item with the given number of extents.
 */
struct xfs_bui_log_item *
xfs_bui_init(
	struct xfs_mount		*mp)

{
	struct xfs_bui_log_item		*buip;

	buip = kmem_zone_zalloc(xfs_bui_zone, KM_SLEEP);

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
 * Pinning has no meaning for an bud item, so just return.
 */
STATIC void
xfs_bud_item_pin(
	struct xfs_log_item	*lip)
{
}

/*
 * Since pinning has no meaning for an bud item, unpinning does
 * not either.
 */
STATIC void
xfs_bud_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
}

/*
 * There isn't much you can do to push on an bud item.  It is simply stuck
 * waiting for the log to be flushed to disk.
 */
STATIC uint
xfs_bud_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	return XFS_ITEM_PINNED;
}

/*
 * The BUD is either committed or aborted if the transaction is cancelled. If
 * the transaction is cancelled, drop our reference to the BUI and free the
 * BUD.
 */
STATIC void
xfs_bud_item_unlock(
	struct xfs_log_item	*lip)
{
	struct xfs_bud_log_item	*budp = BUD_ITEM(lip);

	if (lip->li_flags & XFS_LI_ABORTED) {
		xfs_bui_release(budp->bud_buip);
		kmem_zone_free(xfs_bud_zone, budp);
	}
}

/*
 * When the bud item is committed to disk, all we need to do is delete our
 * reference to our partner bui item and then free ourselves. Since we're
 * freeing ourselves we must return -1 to keep the transaction code from
 * further referencing this item.
 */
STATIC xfs_lsn_t
xfs_bud_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_bud_log_item	*budp = BUD_ITEM(lip);

	/*
	 * Drop the BUI reference regardless of whether the BUD has been
	 * aborted. Once the BUD transaction is constructed, it is the sole
	 * responsibility of the BUD to release the BUI (even if the BUI is
	 * aborted due to log I/O error).
	 */
	xfs_bui_release(budp->bud_buip);
	kmem_zone_free(xfs_bud_zone, budp);

	return (xfs_lsn_t)-1;
}

/*
 * The BUD dependency tracking op doesn't do squat.  It can't because
 * it doesn't know where the free extent is coming from.  The dependency
 * tracking has to be handled by the "enclosing" metadata object.  For
 * example, for inodes, the inode is locked throughout the extent freeing
 * so the dependency should be recorded there.
 */
STATIC void
xfs_bud_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
}

/*
 * This is the ops vector shared by all bud log items.
 */
static const struct xfs_item_ops xfs_bud_item_ops = {
	.iop_size	= xfs_bud_item_size,
	.iop_format	= xfs_bud_item_format,
	.iop_pin	= xfs_bud_item_pin,
	.iop_unpin	= xfs_bud_item_unpin,
	.iop_unlock	= xfs_bud_item_unlock,
	.iop_committed	= xfs_bud_item_committed,
	.iop_push	= xfs_bud_item_push,
	.iop_committing = xfs_bud_item_committing,
};

/*
 * Allocate and initialize an bud item with the given number of extents.
 */
struct xfs_bud_log_item *
xfs_bud_init(
	struct xfs_mount		*mp,
	struct xfs_bui_log_item		*buip)

{
	struct xfs_bud_log_item	*budp;

	budp = kmem_zone_zalloc(xfs_bud_zone, KM_SLEEP);
	xfs_log_item_init(mp, &budp->bud_item, XFS_LI_BUD, &xfs_bud_item_ops);
	budp->bud_buip = buip;
	budp->bud_format.bud_bui_id = buip->bui_format.bui_id;

	return budp;
}

/*
 * Process a bmap update intent item that was recovered from the log.
 * We need to update some inode's bmbt.
 */
int
xfs_bui_recover(
	struct xfs_mount		*mp,
	struct xfs_bui_log_item		*buip,
	struct xfs_defer_ops		*dfops)
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
	error = xfs_trans_log_finish_bmap_update(tp, budp, dfops, type,
			ip, whichfork, bmap->me_startoff,
			bmap->me_startblock, &count, state);
	if (error)
		goto err_inode;

	if (count > 0) {
		ASSERT(type == XFS_BMAP_UNMAP);
		irec.br_startblock = bmap->me_startblock;
		irec.br_blockcount = count;
		irec.br_startoff = bmap->me_startoff;
		irec.br_state = state;
		error = xfs_bmap_unmap_extent(tp->t_mountp, dfops, ip, &irec);
		if (error)
			goto err_inode;
	}

	set_bit(XFS_BUI_RECOVERED, &buip->bui_flags);
	error = xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	IRELE(ip);

	return error;

err_inode:
	xfs_trans_cancel(tp);
	if (ip) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		IRELE(ip);
	}
	return error;
}
