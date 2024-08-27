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
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_ag.h"
#include "xfs_trace.h"

struct kmem_cache	*xfs_bui_cache;
struct kmem_cache	*xfs_bud_cache;

static const struct xfs_item_ops xfs_bui_item_ops;

static inline struct xfs_bui_log_item *BUI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_bui_log_item, bui_item);
}

STATIC void
xfs_bui_item_free(
	struct xfs_bui_log_item	*buip)
{
	kvfree(buip->bui_item.li_lv_shadow);
	kmem_cache_free(xfs_bui_cache, buip);
}

/*
 * Freeing the BUI requires that we remove it from the AIL if it has already
 * been placed there. However, the BUI may not yet have been placed in the AIL
 * when called by xfs_bui_release() from BUD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the BUI.
 */
STATIC void
xfs_bui_release(
	struct xfs_bui_log_item	*buip)
{
	ASSERT(atomic_read(&buip->bui_refcount) > 0);
	if (!atomic_dec_and_test(&buip->bui_refcount))
		return;

	xfs_trans_ail_delete(&buip->bui_item, 0);
	xfs_bui_item_free(buip);
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

/*
 * Allocate and initialize an bui item with the given number of extents.
 */
STATIC struct xfs_bui_log_item *
xfs_bui_init(
	struct xfs_mount		*mp)

{
	struct xfs_bui_log_item		*buip;

	buip = kmem_cache_zalloc(xfs_bui_cache, GFP_KERNEL | __GFP_NOFAIL);

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
	kvfree(budp->bud_item.li_lv_shadow);
	kmem_cache_free(xfs_bud_cache, budp);
}

static struct xfs_log_item *
xfs_bud_item_intent(
	struct xfs_log_item	*lip)
{
	return &BUD_ITEM(lip)->bud_buip->bui_item;
}

static const struct xfs_item_ops xfs_bud_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED |
			  XFS_ITEM_INTENT_DONE,
	.iop_size	= xfs_bud_item_size,
	.iop_format	= xfs_bud_item_format,
	.iop_release	= xfs_bud_item_release,
	.iop_intent	= xfs_bud_item_intent,
};

static inline struct xfs_bmap_intent *bi_entry(const struct list_head *e)
{
	return list_entry(e, struct xfs_bmap_intent, bi_list);
}

/* Sort bmap intents by inode. */
static int
xfs_bmap_update_diff_items(
	void				*priv,
	const struct list_head		*a,
	const struct list_head		*b)
{
	struct xfs_bmap_intent		*ba = bi_entry(a);
	struct xfs_bmap_intent		*bb = bi_entry(b);

	return ba->bi_owner->i_ino - bb->bi_owner->i_ino;
}

/* Log bmap updates in the intent item. */
STATIC void
xfs_bmap_update_log_item(
	struct xfs_trans		*tp,
	struct xfs_bui_log_item		*buip,
	struct xfs_bmap_intent		*bi)
{
	uint				next_extent;
	struct xfs_map_extent		*map;

	/*
	 * atomic_inc_return gives us the value after the increment;
	 * we want to use it as an array index so we need to subtract 1 from
	 * it.
	 */
	next_extent = atomic_inc_return(&buip->bui_next_extent) - 1;
	ASSERT(next_extent < buip->bui_format.bui_nextents);
	map = &buip->bui_format.bui_extents[next_extent];
	map->me_owner = bi->bi_owner->i_ino;
	map->me_startblock = bi->bi_bmap.br_startblock;
	map->me_startoff = bi->bi_bmap.br_startoff;
	map->me_len = bi->bi_bmap.br_blockcount;

	switch (bi->bi_type) {
	case XFS_BMAP_MAP:
	case XFS_BMAP_UNMAP:
		map->me_flags = bi->bi_type;
		break;
	default:
		ASSERT(0);
	}
	if (bi->bi_bmap.br_state == XFS_EXT_UNWRITTEN)
		map->me_flags |= XFS_BMAP_EXTENT_UNWRITTEN;
	if (bi->bi_whichfork == XFS_ATTR_FORK)
		map->me_flags |= XFS_BMAP_EXTENT_ATTR_FORK;
	if (xfs_ifork_is_realtime(bi->bi_owner, bi->bi_whichfork))
		map->me_flags |= XFS_BMAP_EXTENT_REALTIME;
}

static struct xfs_log_item *
xfs_bmap_update_create_intent(
	struct xfs_trans		*tp,
	struct list_head		*items,
	unsigned int			count,
	bool				sort)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_bui_log_item		*buip = xfs_bui_init(mp);
	struct xfs_bmap_intent		*bi;

	ASSERT(count == XFS_BUI_MAX_FAST_EXTENTS);

	if (sort)
		list_sort(mp, items, xfs_bmap_update_diff_items);
	list_for_each_entry(bi, items, bi_list)
		xfs_bmap_update_log_item(tp, buip, bi);
	return &buip->bui_item;
}

/* Get an BUD so we can process all the deferred bmap updates. */
static struct xfs_log_item *
xfs_bmap_update_create_done(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	unsigned int			count)
{
	struct xfs_bui_log_item		*buip = BUI_ITEM(intent);
	struct xfs_bud_log_item		*budp;

	budp = kmem_cache_zalloc(xfs_bud_cache, GFP_KERNEL | __GFP_NOFAIL);
	xfs_log_item_init(tp->t_mountp, &budp->bud_item, XFS_LI_BUD,
			  &xfs_bud_item_ops);
	budp->bud_buip = buip;
	budp->bud_format.bud_bui_id = buip->bui_format.bui_id;

	return &budp->bud_item;
}

/* Take a passive ref to the AG containing the space we're mapping. */
static inline void
xfs_bmap_update_get_group(
	struct xfs_mount	*mp,
	struct xfs_bmap_intent	*bi)
{
	if (xfs_ifork_is_realtime(bi->bi_owner, bi->bi_whichfork))
		return;

	/*
	 * Bump the intent count on behalf of the deferred rmap and refcount
	 * intent items that that we can queue when we finish this bmap work.
	 * This new intent item will bump the intent count before the bmap
	 * intent drops the intent count, ensuring that the intent count
	 * remains nonzero across the transaction roll.
	 */
	bi->bi_pag = xfs_perag_intent_get(mp, bi->bi_bmap.br_startblock);
}

/* Add this deferred BUI to the transaction. */
void
xfs_bmap_defer_add(
	struct xfs_trans	*tp,
	struct xfs_bmap_intent	*bi)
{
	trace_xfs_bmap_defer(bi);

	xfs_bmap_update_get_group(tp->t_mountp, bi);

	/*
	 * Ensure the deferred mapping is pre-recorded in i_delayed_blks.
	 *
	 * Otherwise stat can report zero blocks for an inode that actually has
	 * data when the entire mapping is in the process of being overwritten
	 * using the out of place write path. This is undone in xfs_bmapi_remap
	 * after it has incremented di_nblocks for a successful operation.
	 */
	if (bi->bi_type == XFS_BMAP_MAP)
		bi->bi_owner->i_delayed_blks += bi->bi_bmap.br_blockcount;
	xfs_defer_add(tp, &bi->bi_list, &xfs_bmap_update_defer_type);
}

/* Release a passive AG ref after finishing mapping work. */
static inline void
xfs_bmap_update_put_group(
	struct xfs_bmap_intent	*bi)
{
	if (xfs_ifork_is_realtime(bi->bi_owner, bi->bi_whichfork))
		return;

	xfs_perag_intent_put(bi->bi_pag);
}

/* Cancel a deferred bmap update. */
STATIC void
xfs_bmap_update_cancel_item(
	struct list_head		*item)
{
	struct xfs_bmap_intent		*bi = bi_entry(item);

	if (bi->bi_type == XFS_BMAP_MAP)
		bi->bi_owner->i_delayed_blks -= bi->bi_bmap.br_blockcount;

	xfs_bmap_update_put_group(bi);
	kmem_cache_free(xfs_bmap_intent_cache, bi);
}

/* Process a deferred bmap update. */
STATIC int
xfs_bmap_update_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_bmap_intent		*bi = bi_entry(item);
	int				error;

	error = xfs_bmap_finish_one(tp, bi);
	if (!error && bi->bi_bmap.br_blockcount > 0) {
		ASSERT(bi->bi_type == XFS_BMAP_UNMAP);
		return -EAGAIN;
	}

	xfs_bmap_update_cancel_item(item);
	return error;
}

/* Abort all pending BUIs. */
STATIC void
xfs_bmap_update_abort_intent(
	struct xfs_log_item		*intent)
{
	xfs_bui_release(BUI_ITEM(intent));
}

/* Is this recovered BUI ok? */
static inline bool
xfs_bui_validate(
	struct xfs_mount		*mp,
	struct xfs_bui_log_item		*buip)
{
	struct xfs_map_extent		*map;

	/* Only one mapping operation per BUI... */
	if (buip->bui_format.bui_nextents != XFS_BUI_MAX_FAST_EXTENTS)
		return false;

	map = &buip->bui_format.bui_extents[0];

	if (map->me_flags & ~XFS_BMAP_EXTENT_FLAGS)
		return false;

	switch (map->me_flags & XFS_BMAP_EXTENT_TYPE_MASK) {
	case XFS_BMAP_MAP:
	case XFS_BMAP_UNMAP:
		break;
	default:
		return false;
	}

	if (!xfs_verify_ino(mp, map->me_owner))
		return false;

	if (!xfs_verify_fileext(mp, map->me_startoff, map->me_len))
		return false;

	if (map->me_flags & XFS_BMAP_EXTENT_REALTIME)
		return xfs_verify_rtbext(mp, map->me_startblock, map->me_len);

	return xfs_verify_fsbext(mp, map->me_startblock, map->me_len);
}

static inline struct xfs_bmap_intent *
xfs_bui_recover_work(
	struct xfs_mount		*mp,
	struct xfs_defer_pending	*dfp,
	struct xfs_inode		**ipp,
	struct xfs_map_extent		*map)
{
	struct xfs_bmap_intent		*bi;
	int				error;

	error = xlog_recover_iget(mp, map->me_owner, ipp);
	if (error)
		return ERR_PTR(error);

	bi = kmem_cache_zalloc(xfs_bmap_intent_cache,
			GFP_KERNEL | __GFP_NOFAIL);
	bi->bi_whichfork = (map->me_flags & XFS_BMAP_EXTENT_ATTR_FORK) ?
			XFS_ATTR_FORK : XFS_DATA_FORK;
	bi->bi_type = map->me_flags & XFS_BMAP_EXTENT_TYPE_MASK;
	bi->bi_bmap.br_startblock = map->me_startblock;
	bi->bi_bmap.br_startoff = map->me_startoff;
	bi->bi_bmap.br_blockcount = map->me_len;
	bi->bi_bmap.br_state = (map->me_flags & XFS_BMAP_EXTENT_UNWRITTEN) ?
			XFS_EXT_UNWRITTEN : XFS_EXT_NORM;
	bi->bi_owner = *ipp;
	xfs_bmap_update_get_group(mp, bi);

	/* see xfs_bmap_defer_add for details */
	if (bi->bi_type == XFS_BMAP_MAP)
		bi->bi_owner->i_delayed_blks += bi->bi_bmap.br_blockcount;
	xfs_defer_add_item(dfp, &bi->bi_list);
	return bi;
}

/*
 * Process a bmap update intent item that was recovered from the log.
 * We need to update some inode's bmbt.
 */
STATIC int
xfs_bmap_recover_work(
	struct xfs_defer_pending	*dfp,
	struct list_head		*capture_list)
{
	struct xfs_trans_res		resv;
	struct xfs_log_item		*lip = dfp->dfp_intent;
	struct xfs_bui_log_item		*buip = BUI_ITEM(lip);
	struct xfs_trans		*tp;
	struct xfs_inode		*ip = NULL;
	struct xfs_mount		*mp = lip->li_log->l_mp;
	struct xfs_map_extent		*map;
	struct xfs_bmap_intent		*work;
	int				iext_delta;
	int				error = 0;

	if (!xfs_bui_validate(mp, buip)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				&buip->bui_format, sizeof(buip->bui_format));
		return -EFSCORRUPTED;
	}

	map = &buip->bui_format.bui_extents[0];
	work = xfs_bui_recover_work(mp, dfp, &ip, map);
	if (IS_ERR(work))
		return PTR_ERR(work);

	/* Allocate transaction and do the work. */
	resv = xlog_recover_resv(&M_RES(mp)->tr_itruncate);
	error = xfs_trans_alloc(mp, &resv,
			XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK), 0, 0, &tp);
	if (error)
		goto err_rele;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	if (!!(map->me_flags & XFS_BMAP_EXTENT_REALTIME) !=
	    xfs_ifork_is_realtime(ip, work->bi_whichfork)) {
		error = -EFSCORRUPTED;
		goto err_cancel;
	}

	if (work->bi_type == XFS_BMAP_MAP)
		iext_delta = XFS_IEXT_ADD_NOSPLIT_CNT;
	else
		iext_delta = XFS_IEXT_PUNCH_HOLE_CNT;

	error = xfs_iext_count_extend(tp, ip, work->bi_whichfork, iext_delta);
	if (error)
		goto err_cancel;

	error = xlog_recover_finish_intent(tp, dfp);
	if (error == -EFSCORRUPTED)
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				&buip->bui_format, sizeof(buip->bui_format));
	if (error)
		goto err_cancel;

	/*
	 * Commit transaction, which frees the transaction and saves the inode
	 * for later replay activities.
	 */
	error = xfs_defer_ops_capture_and_commit(tp, capture_list);
	if (error)
		goto err_unlock;

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_irele(ip);
	return 0;

err_cancel:
	xfs_trans_cancel(tp);
err_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
err_rele:
	xfs_irele(ip);
	return error;
}

/* Relog an intent item to push the log tail forward. */
static struct xfs_log_item *
xfs_bmap_relog_intent(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	struct xfs_log_item		*done_item)
{
	struct xfs_bui_log_item		*buip;
	struct xfs_map_extent		*map;
	unsigned int			count;

	count = BUI_ITEM(intent)->bui_format.bui_nextents;
	map = BUI_ITEM(intent)->bui_format.bui_extents;

	buip = xfs_bui_init(tp->t_mountp);
	memcpy(buip->bui_format.bui_extents, map, count * sizeof(*map));
	atomic_set(&buip->bui_next_extent, count);

	return &buip->bui_item;
}

const struct xfs_defer_op_type xfs_bmap_update_defer_type = {
	.name		= "bmap",
	.max_items	= XFS_BUI_MAX_FAST_EXTENTS,
	.create_intent	= xfs_bmap_update_create_intent,
	.abort_intent	= xfs_bmap_update_abort_intent,
	.create_done	= xfs_bmap_update_create_done,
	.finish_item	= xfs_bmap_update_finish_item,
	.cancel_item	= xfs_bmap_update_cancel_item,
	.recover_work	= xfs_bmap_recover_work,
	.relog_intent	= xfs_bmap_relog_intent,
};

STATIC bool
xfs_bui_item_match(
	struct xfs_log_item	*lip,
	uint64_t		intent_id)
{
	return BUI_ITEM(lip)->bui_format.bui_id == intent_id;
}

static const struct xfs_item_ops xfs_bui_item_ops = {
	.flags		= XFS_ITEM_INTENT,
	.iop_size	= xfs_bui_item_size,
	.iop_format	= xfs_bui_item_format,
	.iop_unpin	= xfs_bui_item_unpin,
	.iop_release	= xfs_bui_item_release,
	.iop_match	= xfs_bui_item_match,
};

static inline void
xfs_bui_copy_format(
	struct xfs_bui_log_format	*dst,
	const struct xfs_bui_log_format	*src)
{
	unsigned int			i;

	memcpy(dst, src, offsetof(struct xfs_bui_log_format, bui_extents));

	for (i = 0; i < src->bui_nextents; i++)
		memcpy(&dst->bui_extents[i], &src->bui_extents[i],
				sizeof(struct xfs_map_extent));
}

/*
 * This routine is called to create an in-core extent bmap update
 * item from the bui format structure which was logged on disk.
 * It allocates an in-core bui, copies the extents from the format
 * structure into it, and adds the bui to the AIL with the given
 * LSN.
 */
STATIC int
xlog_recover_bui_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_bui_log_item		*buip;
	struct xfs_bui_log_format	*bui_formatp;
	size_t				len;

	bui_formatp = item->ri_buf[0].i_addr;

	if (item->ri_buf[0].i_len < xfs_bui_log_format_sizeof(0)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	if (bui_formatp->bui_nextents != XFS_BUI_MAX_FAST_EXTENTS) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	len = xfs_bui_log_format_sizeof(bui_formatp->bui_nextents);
	if (item->ri_buf[0].i_len != len) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	buip = xfs_bui_init(mp);
	xfs_bui_copy_format(&buip->bui_format, bui_formatp);
	atomic_set(&buip->bui_next_extent, bui_formatp->bui_nextents);

	xlog_recover_intent_item(log, &buip->bui_item, lsn,
			&xfs_bmap_update_defer_type);
	return 0;
}

const struct xlog_recover_item_ops xlog_bui_item_ops = {
	.item_type		= XFS_LI_BUI,
	.commit_pass2		= xlog_recover_bui_commit_pass2,
};

/*
 * This routine is called when an BUD format structure is found in a committed
 * transaction in the log. Its purpose is to cancel the corresponding BUI if it
 * was still in the log. To do this it searches the AIL for the BUI with an id
 * equal to that in the BUD format structure. If we find it we drop the BUD
 * reference, which removes the BUI from the AIL and frees it.
 */
STATIC int
xlog_recover_bud_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_bud_log_format	*bud_formatp;

	bud_formatp = item->ri_buf[0].i_addr;
	if (item->ri_buf[0].i_len != sizeof(struct xfs_bud_log_format)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	xlog_recover_release_intent(log, XFS_LI_BUI, bud_formatp->bud_bui_id);
	return 0;
}

const struct xlog_recover_item_ops xlog_bud_item_ops = {
	.item_type		= XFS_LI_BUD,
	.commit_pass2		= xlog_recover_bud_commit_pass2,
};
