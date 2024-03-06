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
#include "xfs_ag.h"

struct kmem_cache	*xfs_rui_cache;
struct kmem_cache	*xfs_rud_cache;

static const struct xfs_item_ops xfs_rui_item_ops;

static inline struct xfs_rui_log_item *RUI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_rui_log_item, rui_item);
}

STATIC void
xfs_rui_item_free(
	struct xfs_rui_log_item	*ruip)
{
	kmem_free(ruip->rui_item.li_lv_shadow);
	if (ruip->rui_format.rui_nextents > XFS_RUI_MAX_FAST_EXTENTS)
		kmem_free(ruip);
	else
		kmem_cache_free(xfs_rui_cache, ruip);
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
	if (!atomic_dec_and_test(&ruip->rui_refcount))
		return;

	xfs_trans_ail_delete(&ruip->rui_item, 0);
	xfs_rui_item_free(ruip);
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
		ruip = kmem_cache_zalloc(xfs_rui_cache,
					 GFP_KERNEL | __GFP_NOFAIL);

	xfs_log_item_init(mp, &ruip->rui_item, XFS_LI_RUI, &xfs_rui_item_ops);
	ruip->rui_format.rui_nextents = nextents;
	ruip->rui_format.rui_id = (uintptr_t)(void *)ruip;
	atomic_set(&ruip->rui_next_extent, 0);
	atomic_set(&ruip->rui_refcount, 2);

	return ruip;
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
	kmem_free(rudp->rud_item.li_lv_shadow);
	kmem_cache_free(xfs_rud_cache, rudp);
}

static struct xfs_log_item *
xfs_rud_item_intent(
	struct xfs_log_item	*lip)
{
	return &RUD_ITEM(lip)->rud_ruip->rui_item;
}

static const struct xfs_item_ops xfs_rud_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED |
			  XFS_ITEM_INTENT_DONE,
	.iop_size	= xfs_rud_item_size,
	.iop_format	= xfs_rud_item_format,
	.iop_release	= xfs_rud_item_release,
	.iop_intent	= xfs_rud_item_intent,
};

/* Set the map extent flags for this reverse mapping. */
static void
xfs_trans_set_rmap_flags(
	struct xfs_map_extent		*map,
	enum xfs_rmap_intent_type	type,
	int				whichfork,
	xfs_exntst_t			state)
{
	map->me_flags = 0;
	if (state == XFS_EXT_UNWRITTEN)
		map->me_flags |= XFS_RMAP_EXTENT_UNWRITTEN;
	if (whichfork == XFS_ATTR_FORK)
		map->me_flags |= XFS_RMAP_EXTENT_ATTR_FORK;
	switch (type) {
	case XFS_RMAP_MAP:
		map->me_flags |= XFS_RMAP_EXTENT_MAP;
		break;
	case XFS_RMAP_MAP_SHARED:
		map->me_flags |= XFS_RMAP_EXTENT_MAP_SHARED;
		break;
	case XFS_RMAP_UNMAP:
		map->me_flags |= XFS_RMAP_EXTENT_UNMAP;
		break;
	case XFS_RMAP_UNMAP_SHARED:
		map->me_flags |= XFS_RMAP_EXTENT_UNMAP_SHARED;
		break;
	case XFS_RMAP_CONVERT:
		map->me_flags |= XFS_RMAP_EXTENT_CONVERT;
		break;
	case XFS_RMAP_CONVERT_SHARED:
		map->me_flags |= XFS_RMAP_EXTENT_CONVERT_SHARED;
		break;
	case XFS_RMAP_ALLOC:
		map->me_flags |= XFS_RMAP_EXTENT_ALLOC;
		break;
	case XFS_RMAP_FREE:
		map->me_flags |= XFS_RMAP_EXTENT_FREE;
		break;
	default:
		ASSERT(0);
	}
}

/* Sort rmap intents by AG. */
static int
xfs_rmap_update_diff_items(
	void				*priv,
	const struct list_head		*a,
	const struct list_head		*b)
{
	struct xfs_rmap_intent		*ra;
	struct xfs_rmap_intent		*rb;

	ra = container_of(a, struct xfs_rmap_intent, ri_list);
	rb = container_of(b, struct xfs_rmap_intent, ri_list);

	return ra->ri_pag->pag_agno - rb->ri_pag->pag_agno;
}

/* Log rmap updates in the intent item. */
STATIC void
xfs_rmap_update_log_item(
	struct xfs_trans		*tp,
	struct xfs_rui_log_item		*ruip,
	struct xfs_rmap_intent		*ri)
{
	uint				next_extent;
	struct xfs_map_extent		*map;

	/*
	 * atomic_inc_return gives us the value after the increment;
	 * we want to use it as an array index so we need to subtract 1 from
	 * it.
	 */
	next_extent = atomic_inc_return(&ruip->rui_next_extent) - 1;
	ASSERT(next_extent < ruip->rui_format.rui_nextents);
	map = &ruip->rui_format.rui_extents[next_extent];
	map->me_owner = ri->ri_owner;
	map->me_startblock = ri->ri_bmap.br_startblock;
	map->me_startoff = ri->ri_bmap.br_startoff;
	map->me_len = ri->ri_bmap.br_blockcount;
	xfs_trans_set_rmap_flags(map, ri->ri_type, ri->ri_whichfork,
			ri->ri_bmap.br_state);
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
	struct xfs_rmap_intent		*ri;

	ASSERT(count > 0);

	if (sort)
		list_sort(mp, items, xfs_rmap_update_diff_items);
	list_for_each_entry(ri, items, ri_list)
		xfs_rmap_update_log_item(tp, ruip, ri);
	return &ruip->rui_item;
}

/* Get an RUD so we can process all the deferred rmap updates. */
static struct xfs_log_item *
xfs_rmap_update_create_done(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	unsigned int			count)
{
	struct xfs_rui_log_item		*ruip = RUI_ITEM(intent);
	struct xfs_rud_log_item		*rudp;

	rudp = kmem_cache_zalloc(xfs_rud_cache, GFP_KERNEL | __GFP_NOFAIL);
	xfs_log_item_init(tp->t_mountp, &rudp->rud_item, XFS_LI_RUD,
			  &xfs_rud_item_ops);
	rudp->rud_ruip = ruip;
	rudp->rud_format.rud_rui_id = ruip->rui_format.rui_id;

	return &rudp->rud_item;
}

/* Take a passive ref to the AG containing the space we're rmapping. */
void
xfs_rmap_update_get_group(
	struct xfs_mount	*mp,
	struct xfs_rmap_intent	*ri)
{
	xfs_agnumber_t		agno;

	agno = XFS_FSB_TO_AGNO(mp, ri->ri_bmap.br_startblock);
	ri->ri_pag = xfs_perag_intent_get(mp, agno);
}

/* Release a passive AG ref after finishing rmapping work. */
static inline void
xfs_rmap_update_put_group(
	struct xfs_rmap_intent	*ri)
{
	xfs_perag_intent_put(ri->ri_pag);
}

/* Process a deferred rmap update. */
STATIC int
xfs_rmap_update_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_rmap_intent		*ri;
	int				error;

	ri = container_of(item, struct xfs_rmap_intent, ri_list);

	error = xfs_rmap_finish_one(tp, ri, state);

	xfs_rmap_update_put_group(ri);
	kmem_cache_free(xfs_rmap_intent_cache, ri);
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
	struct xfs_rmap_intent		*ri;

	ri = container_of(item, struct xfs_rmap_intent, ri_list);

	xfs_rmap_update_put_group(ri);
	kmem_cache_free(xfs_rmap_intent_cache, ri);
}

/* Is this recovered RUI ok? */
static inline bool
xfs_rui_validate_map(
	struct xfs_mount		*mp,
	struct xfs_map_extent		*map)
{
	if (!xfs_has_rmapbt(mp))
		return false;

	if (map->me_flags & ~XFS_RMAP_EXTENT_FLAGS)
		return false;

	switch (map->me_flags & XFS_RMAP_EXTENT_TYPE_MASK) {
	case XFS_RMAP_EXTENT_MAP:
	case XFS_RMAP_EXTENT_MAP_SHARED:
	case XFS_RMAP_EXTENT_UNMAP:
	case XFS_RMAP_EXTENT_UNMAP_SHARED:
	case XFS_RMAP_EXTENT_CONVERT:
	case XFS_RMAP_EXTENT_CONVERT_SHARED:
	case XFS_RMAP_EXTENT_ALLOC:
	case XFS_RMAP_EXTENT_FREE:
		break;
	default:
		return false;
	}

	if (!XFS_RMAP_NON_INODE_OWNER(map->me_owner) &&
	    !xfs_verify_ino(mp, map->me_owner))
		return false;

	if (!xfs_verify_fileext(mp, map->me_startoff, map->me_len))
		return false;

	return xfs_verify_fsbext(mp, map->me_startblock, map->me_len);
}

static inline void
xfs_rui_recover_work(
	struct xfs_mount		*mp,
	struct xfs_defer_pending	*dfp,
	const struct xfs_map_extent	*map)
{
	struct xfs_rmap_intent		*ri;

	ri = kmem_cache_alloc(xfs_rmap_intent_cache, GFP_NOFS | __GFP_NOFAIL);

	switch (map->me_flags & XFS_RMAP_EXTENT_TYPE_MASK) {
	case XFS_RMAP_EXTENT_MAP:
		ri->ri_type = XFS_RMAP_MAP;
		break;
	case XFS_RMAP_EXTENT_MAP_SHARED:
		ri->ri_type = XFS_RMAP_MAP_SHARED;
		break;
	case XFS_RMAP_EXTENT_UNMAP:
		ri->ri_type = XFS_RMAP_UNMAP;
		break;
	case XFS_RMAP_EXTENT_UNMAP_SHARED:
		ri->ri_type = XFS_RMAP_UNMAP_SHARED;
		break;
	case XFS_RMAP_EXTENT_CONVERT:
		ri->ri_type = XFS_RMAP_CONVERT;
		break;
	case XFS_RMAP_EXTENT_CONVERT_SHARED:
		ri->ri_type = XFS_RMAP_CONVERT_SHARED;
		break;
	case XFS_RMAP_EXTENT_ALLOC:
		ri->ri_type = XFS_RMAP_ALLOC;
		break;
	case XFS_RMAP_EXTENT_FREE:
		ri->ri_type = XFS_RMAP_FREE;
		break;
	default:
		ASSERT(0);
		return;
	}

	ri->ri_owner = map->me_owner;
	ri->ri_whichfork = (map->me_flags & XFS_RMAP_EXTENT_ATTR_FORK) ?
			XFS_ATTR_FORK : XFS_DATA_FORK;
	ri->ri_bmap.br_startblock = map->me_startblock;
	ri->ri_bmap.br_startoff = map->me_startoff;
	ri->ri_bmap.br_blockcount = map->me_len;
	ri->ri_bmap.br_state = (map->me_flags & XFS_RMAP_EXTENT_UNWRITTEN) ?
			XFS_EXT_UNWRITTEN : XFS_EXT_NORM;
	xfs_rmap_update_get_group(mp, ri);

	xfs_defer_add_item(dfp, &ri->ri_list);
}

/*
 * Process an rmap update intent item that was recovered from the log.
 * We need to update the rmapbt.
 */
STATIC int
xfs_rmap_recover_work(
	struct xfs_defer_pending	*dfp,
	struct list_head		*capture_list)
{
	struct xfs_trans_res		resv;
	struct xfs_log_item		*lip = dfp->dfp_intent;
	struct xfs_rui_log_item		*ruip = RUI_ITEM(lip);
	struct xfs_trans		*tp;
	struct xfs_mount		*mp = lip->li_log->l_mp;
	int				i;
	int				error = 0;

	/*
	 * First check the validity of the extents described by the
	 * RUI.  If any are bad, then assume that all are bad and
	 * just toss the RUI.
	 */
	for (i = 0; i < ruip->rui_format.rui_nextents; i++) {
		if (!xfs_rui_validate_map(mp,
					&ruip->rui_format.rui_extents[i])) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					&ruip->rui_format,
					sizeof(ruip->rui_format));
			return -EFSCORRUPTED;
		}

		xfs_rui_recover_work(mp, dfp, &ruip->rui_format.rui_extents[i]);
	}

	resv = xlog_recover_resv(&M_RES(mp)->tr_itruncate);
	error = xfs_trans_alloc(mp, &resv, mp->m_rmap_maxlevels, 0,
			XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;

	error = xlog_recover_finish_intent(tp, dfp);
	if (error == -EFSCORRUPTED)
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				&ruip->rui_format,
				sizeof(ruip->rui_format));
	if (error)
		goto abort_error;

	return xfs_defer_ops_capture_and_commit(tp, capture_list);

abort_error:
	xfs_trans_cancel(tp);
	return error;
}

/* Relog an intent item to push the log tail forward. */
static struct xfs_log_item *
xfs_rmap_relog_intent(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	struct xfs_log_item		*done_item)
{
	struct xfs_rui_log_item		*ruip;
	struct xfs_map_extent		*map;
	unsigned int			count;

	count = RUI_ITEM(intent)->rui_format.rui_nextents;
	map = RUI_ITEM(intent)->rui_format.rui_extents;

	ruip = xfs_rui_init(tp->t_mountp, count);
	memcpy(ruip->rui_format.rui_extents, map, count * sizeof(*map));
	atomic_set(&ruip->rui_next_extent, count);

	return &ruip->rui_item;
}

const struct xfs_defer_op_type xfs_rmap_update_defer_type = {
	.name		= "rmap",
	.max_items	= XFS_RUI_MAX_FAST_EXTENTS,
	.create_intent	= xfs_rmap_update_create_intent,
	.abort_intent	= xfs_rmap_update_abort_intent,
	.create_done	= xfs_rmap_update_create_done,
	.finish_item	= xfs_rmap_update_finish_item,
	.finish_cleanup = xfs_rmap_finish_one_cleanup,
	.cancel_item	= xfs_rmap_update_cancel_item,
	.recover_work	= xfs_rmap_recover_work,
	.relog_intent	= xfs_rmap_relog_intent,
};

STATIC bool
xfs_rui_item_match(
	struct xfs_log_item	*lip,
	uint64_t		intent_id)
{
	return RUI_ITEM(lip)->rui_format.rui_id == intent_id;
}

static const struct xfs_item_ops xfs_rui_item_ops = {
	.flags		= XFS_ITEM_INTENT,
	.iop_size	= xfs_rui_item_size,
	.iop_format	= xfs_rui_item_format,
	.iop_unpin	= xfs_rui_item_unpin,
	.iop_release	= xfs_rui_item_release,
	.iop_match	= xfs_rui_item_match,
};

static inline void
xfs_rui_copy_format(
	struct xfs_rui_log_format	*dst,
	const struct xfs_rui_log_format	*src)
{
	unsigned int			i;

	memcpy(dst, src, offsetof(struct xfs_rui_log_format, rui_extents));

	for (i = 0; i < src->rui_nextents; i++)
		memcpy(&dst->rui_extents[i], &src->rui_extents[i],
				sizeof(struct xfs_map_extent));
}

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
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_rui_log_item		*ruip;
	struct xfs_rui_log_format	*rui_formatp;
	size_t				len;

	rui_formatp = item->ri_buf[0].i_addr;

	if (item->ri_buf[0].i_len < xfs_rui_log_format_sizeof(0)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	len = xfs_rui_log_format_sizeof(rui_formatp->rui_nextents);
	if (item->ri_buf[0].i_len != len) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	ruip = xfs_rui_init(mp, rui_formatp->rui_nextents);
	xfs_rui_copy_format(&ruip->rui_format, rui_formatp);
	atomic_set(&ruip->rui_next_extent, rui_formatp->rui_nextents);

	xlog_recover_intent_item(log, &ruip->rui_item, lsn,
			&xfs_rmap_update_defer_type);
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
	if (item->ri_buf[0].i_len != sizeof(struct xfs_rud_log_format)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				rud_formatp, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	xlog_recover_release_intent(log, XFS_LI_RUI, rud_formatp->rud_rui_id);
	return 0;
}

const struct xlog_recover_item_ops xlog_rud_item_ops = {
	.item_type		= XFS_LI_RUD,
	.commit_pass2		= xlog_recover_rud_commit_pass2,
};
