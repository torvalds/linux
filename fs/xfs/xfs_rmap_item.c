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
#include "xfs_rmap_item.h"
#include "xfs_log.h"
#include "xfs_rmap.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"

kmem_zone_t	*xfs_rui_zone;
kmem_zone_t	*xfs_rud_zone;

static const struct xfs_item_ops xfs_rui_item_ops;

static inline struct xfs_rui_log_item *RUI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_rui_log_item, rui_item);
}

STATIC void
xfs_rui_item_free(
	struct xfs_rui_log_item	*ruip)
{
	if (ruip->rui_format.rui_nextents > XFS_RUI_MAX_FAST_EXTENTS)
		kmem_free(ruip);
	else
		kmem_cache_free(xfs_rui_zone, ruip);
}

/*
 * Freeing the RUI requires that we remove it from the AIL if it has already
 * been placed there. However, the RUI may not yet have been placed in the AIL
 * when called by xfs_rui_release() from RUD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the RUI.
 */
STATIC void
xfs_rui_release(
	struct xfs_rui_log_item	*ruip)
{
	ASSERT(atomic_read(&ruip->rui_refcount) > 0);
	if (atomic_dec_and_test(&ruip->rui_refcount)) {
		xfs_trans_ail_delete(&ruip->rui_item, SHUTDOWN_LOG_IO_ERROR);
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
 * The RUI has been either committed or aborted if the transaction has been
 * cancelled. If the transaction was cancelled, an RUD isn't going to be
 * constructed and thus we free the RUI here directly.
 */
STATIC void
xfs_rui_item_release(
	struct xfs_log_item	*lip)
{
	xfs_rui_release(RUI_ITEM(lip));
}

/*
 * Allocate and initialize an rui item with the given number of extents.
 */
STATIC struct xfs_rui_log_item *
xfs_rui_init(
	struct xfs_mount		*mp,
	uint				nextents)

{
	struct xfs_rui_log_item		*ruip;

	ASSERT(nextents > 0);
	if (nextents > XFS_RUI_MAX_FAST_EXTENTS)
		ruip = kmem_zalloc(xfs_rui_log_item_sizeof(nextents), 0);
	else
		ruip = kmem_zone_zalloc(xfs_rui_zone, 0);

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
STATIC int
xfs_rui_copy_format(
	struct xfs_log_iovec		*buf,
	struct xfs_rui_log_format	*dst_rui_fmt)
{
	struct xfs_rui_log_format	*src_rui_fmt;
	uint				len;

	src_rui_fmt = buf->i_addr;
	len = xfs_rui_log_format_sizeof(src_rui_fmt->rui_nextents);

	if (buf->i_len != len) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, NULL);
		return -EFSCORRUPTED;
	}

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
 * The RUD is either committed or aborted if the transaction is cancelled. If
 * the transaction is cancelled, drop our reference to the RUI and free the
 * RUD.
 */
STATIC void
xfs_rud_item_release(
	struct xfs_log_item	*lip)
{
	struct xfs_rud_log_item	*rudp = RUD_ITEM(lip);

	xfs_rui_release(rudp->rud_ruip);
	kmem_cache_free(xfs_rud_zone, rudp);
}

static const struct xfs_item_ops xfs_rud_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED,
	.iop_size	= xfs_rud_item_size,
	.iop_format	= xfs_rud_item_format,
	.iop_release	= xfs_rud_item_release,
};

static struct xfs_rud_log_item *
xfs_trans_get_rud(
	struct xfs_trans		*tp,
	struct xfs_rui_log_item		*ruip)
{
	struct xfs_rud_log_item		*rudp;

	rudp = kmem_zone_zalloc(xfs_rud_zone, 0);
	xfs_log_item_init(tp->t_mountp, &rudp->rud_item, XFS_LI_RUD,
			  &xfs_rud_item_ops);
	rudp->rud_ruip = ruip;
	rudp->rud_format.rud_rui_id = ruip->rui_format.rui_id;

	xfs_trans_add_item(tp, &rudp->rud_item);
	return rudp;
}

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

/*
 * Finish an rmap update and log it to the RUD. Note that the transaction is
 * marked dirty regardless of whether the rmap update succeeds or fails to
 * support the RUI/RUD lifecycle rules.
 */
static int
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

/* Log rmap updates in the intent item. */
STATIC void
xfs_rmap_update_log_item(
	struct xfs_trans		*tp,
	struct xfs_rui_log_item		*ruip,
	struct xfs_rmap_intent		*rmap)
{
	uint				next_extent;
	struct xfs_map_extent		*map;

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

static struct xfs_log_item *
xfs_rmap_update_create_intent(
	struct xfs_trans		*tp,
	struct list_head		*items,
	unsigned int			count,
	bool				sort)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_rui_log_item		*ruip = xfs_rui_init(mp, count);
	struct xfs_rmap_intent		*rmap;

	ASSERT(count > 0);

	xfs_trans_add_item(tp, &ruip->rui_item);
	if (sort)
		list_sort(mp, items, xfs_rmap_update_diff_items);
	list_for_each_entry(rmap, items, ri_list)
		xfs_rmap_update_log_item(tp, ruip, rmap);
	return &ruip->rui_item;
}

/* Get an RUD so we can process all the deferred rmap updates. */
static struct xfs_log_item *
xfs_rmap_update_create_done(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	unsigned int			count)
{
	return &xfs_trans_get_rud(tp, RUI_ITEM(intent))->rud_item;
}

/* Process a deferred rmap update. */
STATIC int
xfs_rmap_update_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_rmap_intent		*rmap;
	int				error;

	rmap = container_of(item, struct xfs_rmap_intent, ri_list);
	error = xfs_trans_log_finish_rmap_update(tp, RUD_ITEM(done),
			rmap->ri_type, rmap->ri_owner, rmap->ri_whichfork,
			rmap->ri_bmap.br_startoff, rmap->ri_bmap.br_startblock,
			rmap->ri_bmap.br_blockcount, rmap->ri_bmap.br_state,
			state);
	kmem_free(rmap);
	return error;
}

/* Abort all pending RUIs. */
STATIC void
xfs_rmap_update_abort_intent(
	struct xfs_log_item	*intent)
{
	xfs_rui_release(RUI_ITEM(intent));
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
	.create_intent	= xfs_rmap_update_create_intent,
	.abort_intent	= xfs_rmap_update_abort_intent,
	.create_done	= xfs_rmap_update_create_done,
	.finish_item	= xfs_rmap_update_finish_item,
	.finish_cleanup = xfs_rmap_finish_one_cleanup,
	.cancel_item	= xfs_rmap_update_cancel_item,
};

/*
 * Process an rmap update intent item that was recovered from the log.
 * We need to update the rmapbt.
 */
STATIC int
xfs_rui_item_recover(
	struct xfs_log_item		*lip,
	struct xfs_trans		*parent_tp)
{
	struct xfs_rui_log_item		*ruip = RUI_ITEM(lip);
	struct xfs_map_extent		*rmap;
	struct xfs_rud_log_item		*rudp;
	struct xfs_trans		*tp;
	struct xfs_btree_cur		*rcur = NULL;
	struct xfs_mount		*mp = parent_tp->t_mountp;
	xfs_fsblock_t			startblock_fsb;
	enum xfs_rmap_intent_type	type;
	xfs_exntst_t			state;
	bool				op_ok;
	int				i;
	int				whichfork;
	int				error = 0;

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
			xfs_rui_release(ruip);
			return -EFSCORRUPTED;
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
			XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, NULL);
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
	error = xfs_trans_commit(tp);
	return error;

abort_error:
	xfs_rmap_finish_one_cleanup(tp, rcur, error);
	xfs_trans_cancel(tp);
	return error;
}

STATIC bool
xfs_rui_item_match(
	struct xfs_log_item	*lip,
	uint64_t		intent_id)
{
	return RUI_ITEM(lip)->rui_format.rui_id == intent_id;
}

static const struct xfs_item_ops xfs_rui_item_ops = {
	.iop_size	= xfs_rui_item_size,
	.iop_format	= xfs_rui_item_format,
	.iop_unpin	= xfs_rui_item_unpin,
	.iop_release	= xfs_rui_item_release,
	.iop_recover	= xfs_rui_item_recover,
	.iop_match	= xfs_rui_item_match,
};

/*
 * This routine is called to create an in-core extent rmap update
 * item from the rui format structure which was logged on disk.
 * It allocates an in-core rui, copies the extents from the format
 * structure into it, and adds the rui to the AIL with the given
 * LSN.
 */
STATIC int
xlog_recover_rui_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	int				error;
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_rui_log_item		*ruip;
	struct xfs_rui_log_format	*rui_formatp;

	rui_formatp = item->ri_buf[0].i_addr;

	ruip = xfs_rui_init(mp, rui_formatp->rui_nextents);
	error = xfs_rui_copy_format(&item->ri_buf[0], &ruip->rui_format);
	if (error) {
		xfs_rui_item_free(ruip);
		return error;
	}
	atomic_set(&ruip->rui_next_extent, rui_formatp->rui_nextents);
	/*
	 * Insert the intent into the AIL directly and drop one reference so
	 * that finishing or canceling the work will drop the other.
	 */
	xfs_trans_ail_insert(log->l_ailp, &ruip->rui_item, lsn);
	xfs_rui_release(ruip);
	return 0;
}

const struct xlog_recover_item_ops xlog_rui_item_ops = {
	.item_type		= XFS_LI_RUI,
	.commit_pass2		= xlog_recover_rui_commit_pass2,
};

/*
 * This routine is called when an RUD format structure is found in a committed
 * transaction in the log. Its purpose is to cancel the corresponding RUI if it
 * was still in the log. To do this it searches the AIL for the RUI with an id
 * equal to that in the RUD format structure. If we find it we drop the RUD
 * reference, which removes the RUI from the AIL and frees it.
 */
STATIC int
xlog_recover_rud_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_rud_log_format	*rud_formatp;

	rud_formatp = item->ri_buf[0].i_addr;
	ASSERT(item->ri_buf[0].i_len == sizeof(struct xfs_rud_log_format));

	xlog_recover_release_intent(log, XFS_LI_RUI, rud_formatp->rud_rui_id);
	return 0;
}

const struct xlog_recover_item_ops xlog_rud_item_ops = {
	.item_type		= XFS_LI_RUD,
	.commit_pass2		= xlog_recover_rud_commit_pass2,
};
