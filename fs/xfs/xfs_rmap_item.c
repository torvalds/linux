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
#include "xfs_shared.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_rmap_item.h"
#include "xfs_log.h"
#include "xfs_rmap.h"


kmem_zone_t	*xfs_rui_zone;
kmem_zone_t	*xfs_rud_zone;

static inline struct xfs_rui_log_item *RUI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_rui_log_item, rui_item);
}

void
xfs_rui_item_free(
	struct xfs_rui_log_item	*ruip)
{
	if (ruip->rui_format.rui_nextents > XFS_RUI_MAX_FAST_EXTENTS)
		kmem_free(ruip);
	else
		kmem_zone_free(xfs_rui_zone, ruip);
}

/*
 * Freeing the RUI requires that we remove it from the AIL if it has already
 * been placed there. However, the RUI may not yet have been placed in the AIL
 * when called by xfs_rui_release() from RUD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the RUI.
 */
void
xfs_rui_release(
	struct xfs_rui_log_item	*ruip)
{
	ASSERT(atomic_read(&ruip->rui_refcount) > 0);
	if (atomic_dec_and_test(&ruip->rui_refcount)) {
		xfs_trans_ail_remove(&ruip->rui_item, SHUTDOWN_LOG_IO_ERROR);
		xfs_rui_item_free(ruip);
	}
}

STATIC void
xfs_rui_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_rui_log_item	*ruip = RUI_ITEM(lip);

	*nvecs += 1;
	*nbytes += xfs_rui_log_format_sizeof(ruip->rui_format.rui_nextents);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given rui log item. We use only 1 iovec, and we point that
 * at the rui_log_format structure embedded in the rui item.
 * It is at this point that we assert that all of the extent
 * slots in the rui item have been filled.
 */
STATIC void
xfs_rui_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_rui_log_item	*ruip = RUI_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	ASSERT(atomic_read(&ruip->rui_next_extent) ==
			ruip->rui_format.rui_nextents);

	ruip->rui_format.rui_type = XFS_LI_RUI;
	ruip->rui_format.rui_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_RUI_FORMAT, &ruip->rui_format,
			xfs_rui_log_format_sizeof(ruip->rui_format.rui_nextents));
}

/*
 * Pinning has no meaning for an rui item, so just return.
 */
STATIC void
xfs_rui_item_pin(
	struct xfs_log_item	*lip)
{
}

/*
 * The unpin operation is the last place an RUI is manipulated in the log. It is
 * either inserted in the AIL or aborted in the event of a log I/O error. In
 * either case, the RUI transaction has been successfully committed to make it
 * this far. Therefore, we expect whoever committed the RUI to either construct
 * and commit the RUD or drop the RUD's reference in the event of error. Simply
 * drop the log's RUI reference now that the log is done with it.
 */
STATIC void
xfs_rui_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_rui_log_item	*ruip = RUI_ITEM(lip);

	xfs_rui_release(ruip);
}

/*
 * RUI items have no locking or pushing.  However, since RUIs are pulled from
 * the AIL when their corresponding RUDs are committed to disk, their situation
 * is very similar to being pinned.  Return XFS_ITEM_PINNED so that the caller
 * will eventually flush the log.  This should help in getting the RUI out of
 * the AIL.
 */
STATIC uint
xfs_rui_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	return XFS_ITEM_PINNED;
}

/*
 * The RUI has been either committed or aborted if the transaction has been
 * cancelled. If the transaction was cancelled, an RUD isn't going to be
 * constructed and thus we free the RUI here directly.
 */
STATIC void
xfs_rui_item_unlock(
	struct xfs_log_item	*lip)
{
	if (test_bit(XFS_LI_ABORTED, &lip->li_flags))
		xfs_rui_release(RUI_ITEM(lip));
}

/*
 * The RUI is logged only once and cannot be moved in the log, so simply return
 * the lsn at which it's been logged.
 */
STATIC xfs_lsn_t
xfs_rui_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	return lsn;
}

/*
 * The RUI dependency tracking op doesn't do squat.  It can't because
 * it doesn't know where the free extent is coming from.  The dependency
 * tracking has to be handled by the "enclosing" metadata object.  For
 * example, for inodes, the inode is locked throughout the extent freeing
 * so the dependency should be recorded there.
 */
STATIC void
xfs_rui_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
}

/*
 * This is the ops vector shared by all rui log items.
 */
static const struct xfs_item_ops xfs_rui_item_ops = {
	.iop_size	= xfs_rui_item_size,
	.iop_format	= xfs_rui_item_format,
	.iop_pin	= xfs_rui_item_pin,
	.iop_unpin	= xfs_rui_item_unpin,
	.iop_unlock	= xfs_rui_item_unlock,
	.iop_committed	= xfs_rui_item_committed,
	.iop_push	= xfs_rui_item_push,
	.iop_committing = xfs_rui_item_committing,
};

/*
 * Allocate and initialize an rui item with the given number of extents.
 */
struct xfs_rui_log_item *
xfs_rui_init(
	struct xfs_mount		*mp,
	uint				nextents)

{
	struct xfs_rui_log_item		*ruip;

	ASSERT(nextents > 0);
	if (nextents > XFS_RUI_MAX_FAST_EXTENTS)
		ruip = kmem_zalloc(xfs_rui_log_item_sizeof(nextents), KM_SLEEP);
	else
		ruip = kmem_zone_zalloc(xfs_rui_zone, KM_SLEEP);

	xfs_log_item_init(mp, &ruip->rui_item, XFS_LI_RUI, &xfs_rui_item_ops);
	ruip->rui_format.rui_nextents = nextents;
	ruip->rui_format.rui_id = (uintptr_t)(void *)ruip;
	atomic_set(&ruip->rui_next_extent, 0);
	atomic_set(&ruip->rui_refcount, 2);

	return ruip;
}

/*
 * Copy an RUI format buffer from the given buf, and into the destination
 * RUI format structure.  The RUI/RUD items were designed not to need any
 * special alignment handling.
 */
int
xfs_rui_copy_format(
	struct xfs_log_iovec		*buf,
	struct xfs_rui_log_format	*dst_rui_fmt)
{
	struct xfs_rui_log_format	*src_rui_fmt;
	uint				len;

	src_rui_fmt = buf->i_addr;
	len = xfs_rui_log_format_sizeof(src_rui_fmt->rui_nextents);

	if (buf->i_len != len)
		return -EFSCORRUPTED;

	memcpy(dst_rui_fmt, src_rui_fmt, len);
	return 0;
}

static inline struct xfs_rud_log_item *RUD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_rud_log_item, rud_item);
}

STATIC void
xfs_rud_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += sizeof(struct xfs_rud_log_format);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given rud log item. We use only 1 iovec, and we point that
 * at the rud_log_format structure embedded in the rud item.
 * It is at this point that we assert that all of the extent
 * slots in the rud item have been filled.
 */
STATIC void
xfs_rud_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_rud_log_item	*rudp = RUD_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	rudp->rud_format.rud_type = XFS_LI_RUD;
	rudp->rud_format.rud_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_RUD_FORMAT, &rudp->rud_format,
			sizeof(struct xfs_rud_log_format));
}

/*
 * Pinning has no meaning for an rud item, so just return.
 */
STATIC void
xfs_rud_item_pin(
	struct xfs_log_item	*lip)
{
}

/*
 * Since pinning has no meaning for an rud item, unpinning does
 * not either.
 */
STATIC void
xfs_rud_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
}

/*
 * There isn't much you can do to push on an rud item.  It is simply stuck
 * waiting for the log to be flushed to disk.
 */
STATIC uint
xfs_rud_item_push(
	struct xfs_log_item	*lip,
	struct list_head	*buffer_list)
{
	return XFS_ITEM_PINNED;
}

/*
 * The RUD is either committed or aborted if the transaction is cancelled. If
 * the transaction is cancelled, drop our reference to the RUI and free the
 * RUD.
 */
STATIC void
xfs_rud_item_unlock(
	struct xfs_log_item	*lip)
{
	struct xfs_rud_log_item	*rudp = RUD_ITEM(lip);

	if (test_bit(XFS_LI_ABORTED, &lip->li_flags)) {
		xfs_rui_release(rudp->rud_ruip);
		kmem_zone_free(xfs_rud_zone, rudp);
	}
}

/*
 * When the rud item is committed to disk, all we need to do is delete our
 * reference to our partner rui item and then free ourselves. Since we're
 * freeing ourselves we must return -1 to keep the transaction code from
 * further referencing this item.
 */
STATIC xfs_lsn_t
xfs_rud_item_committed(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
	struct xfs_rud_log_item	*rudp = RUD_ITEM(lip);

	/*
	 * Drop the RUI reference regardless of whether the RUD has been
	 * aborted. Once the RUD transaction is constructed, it is the sole
	 * responsibility of the RUD to release the RUI (even if the RUI is
	 * aborted due to log I/O error).
	 */
	xfs_rui_release(rudp->rud_ruip);
	kmem_zone_free(xfs_rud_zone, rudp);

	return (xfs_lsn_t)-1;
}

/*
 * The RUD dependency tracking op doesn't do squat.  It can't because
 * it doesn't know where the free extent is coming from.  The dependency
 * tracking has to be handled by the "enclosing" metadata object.  For
 * example, for inodes, the inode is locked throughout the extent freeing
 * so the dependency should be recorded there.
 */
STATIC void
xfs_rud_item_committing(
	struct xfs_log_item	*lip,
	xfs_lsn_t		lsn)
{
}

/*
 * This is the ops vector shared by all rud log items.
 */
static const struct xfs_item_ops xfs_rud_item_ops = {
	.iop_size	= xfs_rud_item_size,
	.iop_format	= xfs_rud_item_format,
	.iop_pin	= xfs_rud_item_pin,
	.iop_unpin	= xfs_rud_item_unpin,
	.iop_unlock	= xfs_rud_item_unlock,
	.iop_committed	= xfs_rud_item_committed,
	.iop_push	= xfs_rud_item_push,
	.iop_committing = xfs_rud_item_committing,
};

/*
 * Allocate and initialize an rud item with the given number of extents.
 */
struct xfs_rud_log_item *
xfs_rud_init(
	struct xfs_mount		*mp,
	struct xfs_rui_log_item		*ruip)

{
	struct xfs_rud_log_item	*rudp;

	rudp = kmem_zone_zalloc(xfs_rud_zone, KM_SLEEP);
	xfs_log_item_init(mp, &rudp->rud_item, XFS_LI_RUD, &xfs_rud_item_ops);
	rudp->rud_ruip = ruip;
	rudp->rud_format.rud_rui_id = ruip->rui_format.rui_id;

	return rudp;
}

/*
 * Process an rmap update intent item that was recovered from the log.
 * We need to update the rmapbt.
 */
int
xfs_rui_recover(
	struct xfs_mount		*mp,
	struct xfs_rui_log_item		*ruip)
{
	int				i;
	int				error = 0;
	struct xfs_map_extent		*rmap;
	xfs_fsblock_t			startblock_fsb;
	bool				op_ok;
	struct xfs_rud_log_item		*rudp;
	enum xfs_rmap_intent_type	type;
	int				whichfork;
	xfs_exntst_t			state;
	struct xfs_trans		*tp;
	struct xfs_btree_cur		*rcur = NULL;

	ASSERT(!test_bit(XFS_RUI_RECOVERED, &ruip->rui_flags));

	/*
	 * First check the validity of the extents described by the
	 * RUI.  If any are bad, then assume that all are bad and
	 * just toss the RUI.
	 */
	for (i = 0; i < ruip->rui_format.rui_nextents; i++) {
		rmap = &ruip->rui_format.rui_extents[i];
		startblock_fsb = XFS_BB_TO_FSB(mp,
				   XFS_FSB_TO_DADDR(mp, rmap->me_startblock));
		switch (rmap->me_flags & XFS_RMAP_EXTENT_TYPE_MASK) {
		case XFS_RMAP_EXTENT_MAP:
		case XFS_RMAP_EXTENT_MAP_SHARED:
		case XFS_RMAP_EXTENT_UNMAP:
		case XFS_RMAP_EXTENT_UNMAP_SHARED:
		case XFS_RMAP_EXTENT_CONVERT:
		case XFS_RMAP_EXTENT_CONVERT_SHARED:
		case XFS_RMAP_EXTENT_ALLOC:
		case XFS_RMAP_EXTENT_FREE:
			op_ok = true;
			break;
		default:
			op_ok = false;
			break;
		}
		if (!op_ok || startblock_fsb == 0 ||
		    rmap->me_len == 0 ||
		    startblock_fsb >= mp->m_sb.sb_dblocks ||
		    rmap->me_len >= mp->m_sb.sb_agblocks ||
		    (rmap->me_flags & ~XFS_RMAP_EXTENT_FLAGS)) {
			/*
			 * This will pull the RUI from the AIL and
			 * free the memory associated with it.
			 */
			set_bit(XFS_RUI_RECOVERED, &ruip->rui_flags);
			xfs_rui_release(ruip);
			return -EIO;
		}
	}

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate,
			mp->m_rmap_maxlevels, 0, XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;
	rudp = xfs_trans_get_rud(tp, ruip);

	for (i = 0; i < ruip->rui_format.rui_nextents; i++) {
		rmap = &ruip->rui_format.rui_extents[i];
		state = (rmap->me_flags & XFS_RMAP_EXTENT_UNWRITTEN) ?
				XFS_EXT_UNWRITTEN : XFS_EXT_NORM;
		whichfork = (rmap->me_flags & XFS_RMAP_EXTENT_ATTR_FORK) ?
				XFS_ATTR_FORK : XFS_DATA_FORK;
		switch (rmap->me_flags & XFS_RMAP_EXTENT_TYPE_MASK) {
		case XFS_RMAP_EXTENT_MAP:
			type = XFS_RMAP_MAP;
			break;
		case XFS_RMAP_EXTENT_MAP_SHARED:
			type = XFS_RMAP_MAP_SHARED;
			break;
		case XFS_RMAP_EXTENT_UNMAP:
			type = XFS_RMAP_UNMAP;
			break;
		case XFS_RMAP_EXTENT_UNMAP_SHARED:
			type = XFS_RMAP_UNMAP_SHARED;
			break;
		case XFS_RMAP_EXTENT_CONVERT:
			type = XFS_RMAP_CONVERT;
			break;
		case XFS_RMAP_EXTENT_CONVERT_SHARED:
			type = XFS_RMAP_CONVERT_SHARED;
			break;
		case XFS_RMAP_EXTENT_ALLOC:
			type = XFS_RMAP_ALLOC;
			break;
		case XFS_RMAP_EXTENT_FREE:
			type = XFS_RMAP_FREE;
			break;
		default:
			error = -EFSCORRUPTED;
			goto abort_error;
		}
		error = xfs_trans_log_finish_rmap_update(tp, rudp, type,
				rmap->me_owner, whichfork,
				rmap->me_startoff, rmap->me_startblock,
				rmap->me_len, state, &rcur);
		if (error)
			goto abort_error;

	}

	xfs_rmap_finish_one_cleanup(tp, rcur, error);
	set_bit(XFS_RUI_RECOVERED, &ruip->rui_flags);
	error = xfs_trans_commit(tp);
	return error;

abort_error:
	xfs_rmap_finish_one_cleanup(tp, rcur, error);
	xfs_trans_cancel(tp);
	return error;
}
