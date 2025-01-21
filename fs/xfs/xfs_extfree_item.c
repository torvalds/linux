// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2001,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_shared.h"
#include "xfs_mount.h"
#include "xfs_ag.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_extfree_item.h"
#include "xfs_log.h"
#include "xfs_btree.h"
#include "xfs_rmap.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_trace.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_rtalloc.h"
#include "xfs_inode.h"
#include "xfs_rtbitmap.h"
#include "xfs_rtgroup.h"

struct kmem_cache	*xfs_efi_cache;
struct kmem_cache	*xfs_efd_cache;

static const struct xfs_item_ops xfs_efi_item_ops;

static inline struct xfs_efi_log_item *EFI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_efi_log_item, efi_item);
}

STATIC void
xfs_efi_item_free(
	struct xfs_efi_log_item	*efip)
{
	kvfree(efip->efi_item.li_lv_shadow);
	if (efip->efi_format.efi_nextents > XFS_EFI_MAX_FAST_EXTENTS)
		kfree(efip);
	else
		kmem_cache_free(xfs_efi_cache, efip);
}

/*
 * Freeing the efi requires that we remove it from the AIL if it has already
 * been placed there. However, the EFI may not yet have been placed in the AIL
 * when called by xfs_efi_release() from EFD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the EFI.
 */
STATIC void
xfs_efi_release(
	struct xfs_efi_log_item	*efip)
{
	ASSERT(atomic_read(&efip->efi_refcount) > 0);
	if (!atomic_dec_and_test(&efip->efi_refcount))
		return;

	xfs_trans_ail_delete(&efip->efi_item, 0);
	xfs_efi_item_free(efip);
}

STATIC void
xfs_efi_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_efi_log_item	*efip = EFI_ITEM(lip);

	*nvecs += 1;
	*nbytes += xfs_efi_log_format_sizeof(efip->efi_format.efi_nextents);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given efi log item. We use only 1 iovec, and we point that
 * at the efi_log_format structure embedded in the efi item.
 * It is at this point that we assert that all of the extent
 * slots in the efi item have been filled.
 */
STATIC void
xfs_efi_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_efi_log_item	*efip = EFI_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	ASSERT(atomic_read(&efip->efi_next_extent) ==
				efip->efi_format.efi_nextents);
	ASSERT(lip->li_type == XFS_LI_EFI || lip->li_type == XFS_LI_EFI_RT);

	efip->efi_format.efi_type = lip->li_type;
	efip->efi_format.efi_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_EFI_FORMAT, &efip->efi_format,
			xfs_efi_log_format_sizeof(efip->efi_format.efi_nextents));
}

/*
 * The unpin operation is the last place an EFI is manipulated in the log. It is
 * either inserted in the AIL or aborted in the event of a log I/O error. In
 * either case, the EFI transaction has been successfully committed to make it
 * this far. Therefore, we expect whoever committed the EFI to either construct
 * and commit the EFD or drop the EFD's reference in the event of error. Simply
 * drop the log's EFI reference now that the log is done with it.
 */
STATIC void
xfs_efi_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_efi_log_item	*efip = EFI_ITEM(lip);
	xfs_efi_release(efip);
}

/*
 * The EFI has been either committed or aborted if the transaction has been
 * cancelled. If the transaction was cancelled, an EFD isn't going to be
 * constructed and thus we free the EFI here directly.
 */
STATIC void
xfs_efi_item_release(
	struct xfs_log_item	*lip)
{
	xfs_efi_release(EFI_ITEM(lip));
}

/*
 * Allocate and initialize an efi item with the given number of extents.
 */
STATIC struct xfs_efi_log_item *
xfs_efi_init(
	struct xfs_mount	*mp,
	unsigned short		item_type,
	uint			nextents)
{
	struct xfs_efi_log_item	*efip;

	ASSERT(item_type == XFS_LI_EFI || item_type == XFS_LI_EFI_RT);
	ASSERT(nextents > 0);

	if (nextents > XFS_EFI_MAX_FAST_EXTENTS) {
		efip = kzalloc(xfs_efi_log_item_sizeof(nextents),
				GFP_KERNEL | __GFP_NOFAIL);
	} else {
		efip = kmem_cache_zalloc(xfs_efi_cache,
					 GFP_KERNEL | __GFP_NOFAIL);
	}

	xfs_log_item_init(mp, &efip->efi_item, item_type, &xfs_efi_item_ops);
	efip->efi_format.efi_nextents = nextents;
	efip->efi_format.efi_id = (uintptr_t)(void *)efip;
	atomic_set(&efip->efi_next_extent, 0);
	atomic_set(&efip->efi_refcount, 2);

	return efip;
}

/*
 * Copy an EFI format buffer from the given buf, and into the destination
 * EFI format structure.
 * The given buffer can be in 32 bit or 64 bit form (which has different padding),
 * one of which will be the native format for this kernel.
 * It will handle the conversion of formats if necessary.
 */
STATIC int
xfs_efi_copy_format(xfs_log_iovec_t *buf, xfs_efi_log_format_t *dst_efi_fmt)
{
	xfs_efi_log_format_t *src_efi_fmt = buf->i_addr;
	uint i;
	uint len = xfs_efi_log_format_sizeof(src_efi_fmt->efi_nextents);
	uint len32 = xfs_efi_log_format32_sizeof(src_efi_fmt->efi_nextents);
	uint len64 = xfs_efi_log_format64_sizeof(src_efi_fmt->efi_nextents);

	if (buf->i_len == len) {
		memcpy(dst_efi_fmt, src_efi_fmt,
		       offsetof(struct xfs_efi_log_format, efi_extents));
		for (i = 0; i < src_efi_fmt->efi_nextents; i++)
			memcpy(&dst_efi_fmt->efi_extents[i],
			       &src_efi_fmt->efi_extents[i],
			       sizeof(struct xfs_extent));
		return 0;
	} else if (buf->i_len == len32) {
		xfs_efi_log_format_32_t *src_efi_fmt_32 = buf->i_addr;

		dst_efi_fmt->efi_type     = src_efi_fmt_32->efi_type;
		dst_efi_fmt->efi_size     = src_efi_fmt_32->efi_size;
		dst_efi_fmt->efi_nextents = src_efi_fmt_32->efi_nextents;
		dst_efi_fmt->efi_id       = src_efi_fmt_32->efi_id;
		for (i = 0; i < dst_efi_fmt->efi_nextents; i++) {
			dst_efi_fmt->efi_extents[i].ext_start =
				src_efi_fmt_32->efi_extents[i].ext_start;
			dst_efi_fmt->efi_extents[i].ext_len =
				src_efi_fmt_32->efi_extents[i].ext_len;
		}
		return 0;
	} else if (buf->i_len == len64) {
		xfs_efi_log_format_64_t *src_efi_fmt_64 = buf->i_addr;

		dst_efi_fmt->efi_type     = src_efi_fmt_64->efi_type;
		dst_efi_fmt->efi_size     = src_efi_fmt_64->efi_size;
		dst_efi_fmt->efi_nextents = src_efi_fmt_64->efi_nextents;
		dst_efi_fmt->efi_id       = src_efi_fmt_64->efi_id;
		for (i = 0; i < dst_efi_fmt->efi_nextents; i++) {
			dst_efi_fmt->efi_extents[i].ext_start =
				src_efi_fmt_64->efi_extents[i].ext_start;
			dst_efi_fmt->efi_extents[i].ext_len =
				src_efi_fmt_64->efi_extents[i].ext_len;
		}
		return 0;
	}
	XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, NULL, buf->i_addr,
			buf->i_len);
	return -EFSCORRUPTED;
}

static inline struct xfs_efd_log_item *EFD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_efd_log_item, efd_item);
}

STATIC void
xfs_efd_item_free(struct xfs_efd_log_item *efdp)
{
	kvfree(efdp->efd_item.li_lv_shadow);
	if (efdp->efd_format.efd_nextents > XFS_EFD_MAX_FAST_EXTENTS)
		kfree(efdp);
	else
		kmem_cache_free(xfs_efd_cache, efdp);
}

STATIC void
xfs_efd_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	struct xfs_efd_log_item	*efdp = EFD_ITEM(lip);

	*nvecs += 1;
	*nbytes += xfs_efd_log_format_sizeof(efdp->efd_format.efd_nextents);
}

/*
 * This is called to fill in the vector of log iovecs for the
 * given efd log item. We use only 1 iovec, and we point that
 * at the efd_log_format structure embedded in the efd item.
 * It is at this point that we assert that all of the extent
 * slots in the efd item have been filled.
 */
STATIC void
xfs_efd_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_efd_log_item	*efdp = EFD_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	ASSERT(efdp->efd_next_extent == efdp->efd_format.efd_nextents);
	ASSERT(lip->li_type == XFS_LI_EFD || lip->li_type == XFS_LI_EFD_RT);

	efdp->efd_format.efd_type = lip->li_type;
	efdp->efd_format.efd_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_EFD_FORMAT, &efdp->efd_format,
			xfs_efd_log_format_sizeof(efdp->efd_format.efd_nextents));
}

/*
 * The EFD is either committed or aborted if the transaction is cancelled. If
 * the transaction is cancelled, drop our reference to the EFI and free the EFD.
 */
STATIC void
xfs_efd_item_release(
	struct xfs_log_item	*lip)
{
	struct xfs_efd_log_item	*efdp = EFD_ITEM(lip);

	xfs_efi_release(efdp->efd_efip);
	xfs_efd_item_free(efdp);
}

static struct xfs_log_item *
xfs_efd_item_intent(
	struct xfs_log_item	*lip)
{
	return &EFD_ITEM(lip)->efd_efip->efi_item;
}

static const struct xfs_item_ops xfs_efd_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED |
			  XFS_ITEM_INTENT_DONE,
	.iop_size	= xfs_efd_item_size,
	.iop_format	= xfs_efd_item_format,
	.iop_release	= xfs_efd_item_release,
	.iop_intent	= xfs_efd_item_intent,
};

static inline struct xfs_extent_free_item *xefi_entry(const struct list_head *e)
{
	return list_entry(e, struct xfs_extent_free_item, xefi_list);
}

static inline bool
xfs_efi_item_isrt(const struct xfs_log_item *lip)
{
	ASSERT(lip->li_type == XFS_LI_EFI || lip->li_type == XFS_LI_EFI_RT);

	return lip->li_type == XFS_LI_EFI_RT;
}

/*
 * Fill the EFD with all extents from the EFI when we need to roll the
 * transaction and continue with a new EFI.
 *
 * This simply copies all the extents in the EFI to the EFD rather than make
 * assumptions about which extents in the EFI have already been processed. We
 * currently keep the xefi list in the same order as the EFI extent list, but
 * that may not always be the case. Copying everything avoids leaving a landmine
 * were we fail to cancel all the extents in an EFI if the xefi list is
 * processed in a different order to the extents in the EFI.
 */
static void
xfs_efd_from_efi(
	struct xfs_efd_log_item	*efdp)
{
	struct xfs_efi_log_item *efip = efdp->efd_efip;
	uint                    i;

	ASSERT(efip->efi_format.efi_nextents > 0);
	ASSERT(efdp->efd_next_extent < efip->efi_format.efi_nextents);

	for (i = 0; i < efip->efi_format.efi_nextents; i++) {
	       efdp->efd_format.efd_extents[i] =
		       efip->efi_format.efi_extents[i];
	}
	efdp->efd_next_extent = efip->efi_format.efi_nextents;
}

static void
xfs_efd_add_extent(
	struct xfs_efd_log_item		*efdp,
	struct xfs_extent_free_item	*xefi)
{
	struct xfs_extent		*extp;

	ASSERT(efdp->efd_next_extent < efdp->efd_format.efd_nextents);

	extp = &efdp->efd_format.efd_extents[efdp->efd_next_extent];
	extp->ext_start = xefi->xefi_startblock;
	extp->ext_len = xefi->xefi_blockcount;

	efdp->efd_next_extent++;
}

/* Sort bmap items by AG. */
static int
xfs_extent_free_diff_items(
	void				*priv,
	const struct list_head		*a,
	const struct list_head		*b)
{
	struct xfs_extent_free_item	*ra = xefi_entry(a);
	struct xfs_extent_free_item	*rb = xefi_entry(b);

	return ra->xefi_group->xg_gno - rb->xefi_group->xg_gno;
}

/* Log a free extent to the intent item. */
STATIC void
xfs_extent_free_log_item(
	struct xfs_trans		*tp,
	struct xfs_efi_log_item		*efip,
	struct xfs_extent_free_item	*xefi)
{
	uint				next_extent;
	struct xfs_extent		*extp;

	/*
	 * atomic_inc_return gives us the value after the increment;
	 * we want to use it as an array index so we need to subtract 1 from
	 * it.
	 */
	next_extent = atomic_inc_return(&efip->efi_next_extent) - 1;
	ASSERT(next_extent < efip->efi_format.efi_nextents);
	extp = &efip->efi_format.efi_extents[next_extent];
	extp->ext_start = xefi->xefi_startblock;
	extp->ext_len = xefi->xefi_blockcount;
}

static struct xfs_log_item *
__xfs_extent_free_create_intent(
	struct xfs_trans		*tp,
	struct list_head		*items,
	unsigned int			count,
	bool				sort,
	unsigned short			item_type)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_efi_log_item		*efip;
	struct xfs_extent_free_item	*xefi;

	ASSERT(count > 0);

	efip = xfs_efi_init(mp, item_type, count);
	if (sort)
		list_sort(mp, items, xfs_extent_free_diff_items);
	list_for_each_entry(xefi, items, xefi_list)
		xfs_extent_free_log_item(tp, efip, xefi);
	return &efip->efi_item;
}

static struct xfs_log_item *
xfs_extent_free_create_intent(
	struct xfs_trans		*tp,
	struct list_head		*items,
	unsigned int			count,
	bool				sort)
{
	return __xfs_extent_free_create_intent(tp, items, count, sort,
			XFS_LI_EFI);
}

static inline unsigned short
xfs_efd_type_from_efi(const struct xfs_efi_log_item *efip)
{
	return xfs_efi_item_isrt(&efip->efi_item) ?  XFS_LI_EFD_RT : XFS_LI_EFD;
}

/* Get an EFD so we can process all the free extents. */
static struct xfs_log_item *
xfs_extent_free_create_done(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	unsigned int			count)
{
	struct xfs_efi_log_item		*efip = EFI_ITEM(intent);
	struct xfs_efd_log_item		*efdp;

	ASSERT(count > 0);

	if (count > XFS_EFD_MAX_FAST_EXTENTS) {
		efdp = kzalloc(xfs_efd_log_item_sizeof(count),
				GFP_KERNEL | __GFP_NOFAIL);
	} else {
		efdp = kmem_cache_zalloc(xfs_efd_cache,
					GFP_KERNEL | __GFP_NOFAIL);
	}

	xfs_log_item_init(tp->t_mountp, &efdp->efd_item,
			xfs_efd_type_from_efi(efip), &xfs_efd_item_ops);
	efdp->efd_efip = efip;
	efdp->efd_format.efd_nextents = count;
	efdp->efd_format.efd_efi_id = efip->efi_format.efi_id;

	return &efdp->efd_item;
}

static inline const struct xfs_defer_op_type *
xefi_ops(
	struct xfs_extent_free_item	*xefi)
{
	if (xfs_efi_is_realtime(xefi))
		return &xfs_rtextent_free_defer_type;
	if (xefi->xefi_agresv == XFS_AG_RESV_AGFL)
		return &xfs_agfl_free_defer_type;
	return &xfs_extent_free_defer_type;
}

/* Add this deferred EFI to the transaction. */
void
xfs_extent_free_defer_add(
	struct xfs_trans		*tp,
	struct xfs_extent_free_item	*xefi,
	struct xfs_defer_pending	**dfpp)
{
	struct xfs_mount		*mp = tp->t_mountp;

	xefi->xefi_group = xfs_group_intent_get(mp, xefi->xefi_startblock,
			xfs_efi_is_realtime(xefi) ? XG_TYPE_RTG : XG_TYPE_AG);

	trace_xfs_extent_free_defer(mp, xefi);
	*dfpp = xfs_defer_add(tp, &xefi->xefi_list, xefi_ops(xefi));
}

/* Cancel a free extent. */
STATIC void
xfs_extent_free_cancel_item(
	struct list_head		*item)
{
	struct xfs_extent_free_item	*xefi = xefi_entry(item);

	xfs_group_intent_put(xefi->xefi_group);
	kmem_cache_free(xfs_extfree_item_cache, xefi);
}

/* Process a free extent. */
STATIC int
xfs_extent_free_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_owner_info		oinfo = { };
	struct xfs_extent_free_item	*xefi = xefi_entry(item);
	struct xfs_efd_log_item		*efdp = EFD_ITEM(done);
	struct xfs_mount		*mp = tp->t_mountp;
	xfs_agblock_t			agbno;
	int				error = 0;

	agbno = XFS_FSB_TO_AGBNO(mp, xefi->xefi_startblock);

	oinfo.oi_owner = xefi->xefi_owner;
	if (xefi->xefi_flags & XFS_EFI_ATTR_FORK)
		oinfo.oi_flags |= XFS_OWNER_INFO_ATTR_FORK;
	if (xefi->xefi_flags & XFS_EFI_BMBT_BLOCK)
		oinfo.oi_flags |= XFS_OWNER_INFO_BMBT_BLOCK;

	trace_xfs_extent_free_deferred(mp, xefi);

	/*
	 * If we need a new transaction to make progress, the caller will log a
	 * new EFI with the current contents. It will also log an EFD to cancel
	 * the existing EFI, and so we need to copy all the unprocessed extents
	 * in this EFI to the EFD so this works correctly.
	 */
	if (!(xefi->xefi_flags & XFS_EFI_CANCELLED))
		error = __xfs_free_extent(tp, to_perag(xefi->xefi_group), agbno,
				xefi->xefi_blockcount, &oinfo, xefi->xefi_agresv,
				xefi->xefi_flags & XFS_EFI_SKIP_DISCARD);
	if (error == -EAGAIN) {
		xfs_efd_from_efi(efdp);
		return error;
	}

	xfs_efd_add_extent(efdp, xefi);
	xfs_extent_free_cancel_item(item);
	return error;
}

/* Abort all pending EFIs. */
STATIC void
xfs_extent_free_abort_intent(
	struct xfs_log_item		*intent)
{
	xfs_efi_release(EFI_ITEM(intent));
}

/*
 * AGFL blocks are accounted differently in the reserve pools and are not
 * inserted into the busy extent list.
 */
STATIC int
xfs_agfl_free_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_owner_info		oinfo = { };
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_efd_log_item		*efdp = EFD_ITEM(done);
	struct xfs_extent_free_item	*xefi = xefi_entry(item);
	struct xfs_buf			*agbp;
	int				error;
	xfs_agblock_t			agbno;

	ASSERT(xefi->xefi_blockcount == 1);
	agbno = XFS_FSB_TO_AGBNO(mp, xefi->xefi_startblock);
	oinfo.oi_owner = xefi->xefi_owner;

	trace_xfs_agfl_free_deferred(mp, xefi);

	error = xfs_alloc_read_agf(to_perag(xefi->xefi_group), tp, 0, &agbp);
	if (!error)
		error = xfs_free_ag_extent(tp, agbp, agbno, 1, &oinfo,
				XFS_AG_RESV_AGFL);

	xfs_efd_add_extent(efdp, xefi);
	xfs_extent_free_cancel_item(&xefi->xefi_list);
	return error;
}

/* Is this recovered EFI ok? */
static inline bool
xfs_efi_validate_ext(
	struct xfs_mount		*mp,
	bool				isrt,
	struct xfs_extent		*extp)
{
	if (isrt)
		return xfs_verify_rtbext(mp, extp->ext_start, extp->ext_len);

	return xfs_verify_fsbext(mp, extp->ext_start, extp->ext_len);
}

static inline void
xfs_efi_recover_work(
	struct xfs_mount		*mp,
	struct xfs_defer_pending	*dfp,
	bool				isrt,
	struct xfs_extent		*extp)
{
	struct xfs_extent_free_item	*xefi;

	xefi = kmem_cache_zalloc(xfs_extfree_item_cache,
			       GFP_KERNEL | __GFP_NOFAIL);
	xefi->xefi_startblock = extp->ext_start;
	xefi->xefi_blockcount = extp->ext_len;
	xefi->xefi_agresv = XFS_AG_RESV_NONE;
	xefi->xefi_owner = XFS_RMAP_OWN_UNKNOWN;
	xefi->xefi_group = xfs_group_intent_get(mp, extp->ext_start,
			isrt ? XG_TYPE_RTG : XG_TYPE_AG);
	if (isrt)
		xefi->xefi_flags |= XFS_EFI_REALTIME;

	xfs_defer_add_item(dfp, &xefi->xefi_list);
}

/*
 * Process an extent free intent item that was recovered from
 * the log.  We need to free the extents that it describes.
 */
STATIC int
xfs_extent_free_recover_work(
	struct xfs_defer_pending	*dfp,
	struct list_head		*capture_list)
{
	struct xfs_trans_res		resv;
	struct xfs_log_item		*lip = dfp->dfp_intent;
	struct xfs_efi_log_item		*efip = EFI_ITEM(lip);
	struct xfs_mount		*mp = lip->li_log->l_mp;
	struct xfs_trans		*tp;
	int				i;
	int				error = 0;
	bool				isrt = xfs_efi_item_isrt(lip);

	/*
	 * First check the validity of the extents described by the EFI.  If
	 * any are bad, then assume that all are bad and just toss the EFI.
	 * Mixing RT and non-RT extents in the same EFI item is not allowed.
	 */
	for (i = 0; i < efip->efi_format.efi_nextents; i++) {
		if (!xfs_efi_validate_ext(mp, isrt,
					&efip->efi_format.efi_extents[i])) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					&efip->efi_format,
					sizeof(efip->efi_format));
			return -EFSCORRUPTED;
		}

		xfs_efi_recover_work(mp, dfp, isrt,
				&efip->efi_format.efi_extents[i]);
	}

	resv = xlog_recover_resv(&M_RES(mp)->tr_itruncate);
	error = xfs_trans_alloc(mp, &resv, 0, 0, 0, &tp);
	if (error)
		return error;

	error = xlog_recover_finish_intent(tp, dfp);
	if (error == -EFSCORRUPTED)
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				&efip->efi_format,
				sizeof(efip->efi_format));
	if (error)
		goto abort_error;

	return xfs_defer_ops_capture_and_commit(tp, capture_list);

abort_error:
	xfs_trans_cancel(tp);
	return error;
}

/* Relog an intent item to push the log tail forward. */
static struct xfs_log_item *
xfs_extent_free_relog_intent(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	struct xfs_log_item		*done_item)
{
	struct xfs_efd_log_item		*efdp = EFD_ITEM(done_item);
	struct xfs_efi_log_item		*efip;
	struct xfs_extent		*extp;
	unsigned int			count;

	count = EFI_ITEM(intent)->efi_format.efi_nextents;
	extp = EFI_ITEM(intent)->efi_format.efi_extents;

	ASSERT(intent->li_type == XFS_LI_EFI || intent->li_type == XFS_LI_EFI_RT);

	efdp->efd_next_extent = count;
	memcpy(efdp->efd_format.efd_extents, extp, count * sizeof(*extp));

	efip = xfs_efi_init(tp->t_mountp, intent->li_type, count);
	memcpy(efip->efi_format.efi_extents, extp, count * sizeof(*extp));
	atomic_set(&efip->efi_next_extent, count);

	return &efip->efi_item;
}

const struct xfs_defer_op_type xfs_extent_free_defer_type = {
	.name		= "extent_free",
	.max_items	= XFS_EFI_MAX_FAST_EXTENTS,
	.create_intent	= xfs_extent_free_create_intent,
	.abort_intent	= xfs_extent_free_abort_intent,
	.create_done	= xfs_extent_free_create_done,
	.finish_item	= xfs_extent_free_finish_item,
	.cancel_item	= xfs_extent_free_cancel_item,
	.recover_work	= xfs_extent_free_recover_work,
	.relog_intent	= xfs_extent_free_relog_intent,
};

/* sub-type with special handling for AGFL deferred frees */
const struct xfs_defer_op_type xfs_agfl_free_defer_type = {
	.name		= "agfl_free",
	.max_items	= XFS_EFI_MAX_FAST_EXTENTS,
	.create_intent	= xfs_extent_free_create_intent,
	.abort_intent	= xfs_extent_free_abort_intent,
	.create_done	= xfs_extent_free_create_done,
	.finish_item	= xfs_agfl_free_finish_item,
	.cancel_item	= xfs_extent_free_cancel_item,
	.recover_work	= xfs_extent_free_recover_work,
	.relog_intent	= xfs_extent_free_relog_intent,
};

#ifdef CONFIG_XFS_RT
/* Create a realtime extent freeing */
static struct xfs_log_item *
xfs_rtextent_free_create_intent(
	struct xfs_trans		*tp,
	struct list_head		*items,
	unsigned int			count,
	bool				sort)
{
	return __xfs_extent_free_create_intent(tp, items, count, sort,
			XFS_LI_EFI_RT);
}

/* Process a free realtime extent. */
STATIC int
xfs_rtextent_free_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_extent_free_item	*xefi = xefi_entry(item);
	struct xfs_efd_log_item		*efdp = EFD_ITEM(done);
	struct xfs_rtgroup		**rtgp = (struct xfs_rtgroup **)state;
	int				error = 0;

	trace_xfs_extent_free_deferred(mp, xefi);

	if (!(xefi->xefi_flags & XFS_EFI_CANCELLED)) {
		if (*rtgp != to_rtg(xefi->xefi_group)) {
			*rtgp = to_rtg(xefi->xefi_group);
			xfs_rtgroup_lock(*rtgp, XFS_RTGLOCK_BITMAP);
			xfs_rtgroup_trans_join(tp, *rtgp,
					XFS_RTGLOCK_BITMAP);
		}
		error = xfs_rtfree_blocks(tp, *rtgp,
				xefi->xefi_startblock, xefi->xefi_blockcount);
	}
	if (error == -EAGAIN) {
		xfs_efd_from_efi(efdp);
		return error;
	}

	xfs_efd_add_extent(efdp, xefi);
	xfs_extent_free_cancel_item(item);
	return error;
}

const struct xfs_defer_op_type xfs_rtextent_free_defer_type = {
	.name		= "rtextent_free",
	.max_items	= XFS_EFI_MAX_FAST_EXTENTS,
	.create_intent	= xfs_rtextent_free_create_intent,
	.abort_intent	= xfs_extent_free_abort_intent,
	.create_done	= xfs_extent_free_create_done,
	.finish_item	= xfs_rtextent_free_finish_item,
	.cancel_item	= xfs_extent_free_cancel_item,
	.recover_work	= xfs_extent_free_recover_work,
	.relog_intent	= xfs_extent_free_relog_intent,
};
#else
const struct xfs_defer_op_type xfs_rtextent_free_defer_type = {
	.name		= "rtextent_free",
};
#endif /* CONFIG_XFS_RT */

STATIC bool
xfs_efi_item_match(
	struct xfs_log_item	*lip,
	uint64_t		intent_id)
{
	return EFI_ITEM(lip)->efi_format.efi_id == intent_id;
}

static const struct xfs_item_ops xfs_efi_item_ops = {
	.flags		= XFS_ITEM_INTENT,
	.iop_size	= xfs_efi_item_size,
	.iop_format	= xfs_efi_item_format,
	.iop_unpin	= xfs_efi_item_unpin,
	.iop_release	= xfs_efi_item_release,
	.iop_match	= xfs_efi_item_match,
};

/*
 * This routine is called to create an in-core extent free intent
 * item from the efi format structure which was logged on disk.
 * It allocates an in-core efi, copies the extents from the format
 * structure into it, and adds the efi to the AIL with the given
 * LSN.
 */
STATIC int
xlog_recover_efi_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_efi_log_item		*efip;
	struct xfs_efi_log_format	*efi_formatp;
	int				error;

	efi_formatp = item->ri_buf[0].i_addr;

	if (item->ri_buf[0].i_len < xfs_efi_log_format_sizeof(0)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	efip = xfs_efi_init(mp, ITEM_TYPE(item), efi_formatp->efi_nextents);
	error = xfs_efi_copy_format(&item->ri_buf[0], &efip->efi_format);
	if (error) {
		xfs_efi_item_free(efip);
		return error;
	}
	atomic_set(&efip->efi_next_extent, efi_formatp->efi_nextents);

	xlog_recover_intent_item(log, &efip->efi_item, lsn,
			&xfs_extent_free_defer_type);
	return 0;
}

const struct xlog_recover_item_ops xlog_efi_item_ops = {
	.item_type		= XFS_LI_EFI,
	.commit_pass2		= xlog_recover_efi_commit_pass2,
};

#ifdef CONFIG_XFS_RT
STATIC int
xlog_recover_rtefi_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_efi_log_item		*efip;
	struct xfs_efi_log_format	*efi_formatp;
	int				error;

	efi_formatp = item->ri_buf[0].i_addr;

	if (item->ri_buf[0].i_len < xfs_efi_log_format_sizeof(0)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	efip = xfs_efi_init(mp, ITEM_TYPE(item), efi_formatp->efi_nextents);
	error = xfs_efi_copy_format(&item->ri_buf[0], &efip->efi_format);
	if (error) {
		xfs_efi_item_free(efip);
		return error;
	}
	atomic_set(&efip->efi_next_extent, efi_formatp->efi_nextents);

	xlog_recover_intent_item(log, &efip->efi_item, lsn,
			&xfs_rtextent_free_defer_type);
	return 0;
}
#else
STATIC int
xlog_recover_rtefi_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
			item->ri_buf[0].i_addr, item->ri_buf[0].i_len);
	return -EFSCORRUPTED;
}
#endif

const struct xlog_recover_item_ops xlog_rtefi_item_ops = {
	.item_type		= XFS_LI_EFI_RT,
	.commit_pass2		= xlog_recover_rtefi_commit_pass2,
};

/*
 * This routine is called when an EFD format structure is found in a committed
 * transaction in the log. Its purpose is to cancel the corresponding EFI if it
 * was still in the log. To do this it searches the AIL for the EFI with an id
 * equal to that in the EFD format structure. If we find it we drop the EFD
 * reference, which removes the EFI from the AIL and frees it.
 */
STATIC int
xlog_recover_efd_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_efd_log_format	*efd_formatp;
	int				buflen = item->ri_buf[0].i_len;

	efd_formatp = item->ri_buf[0].i_addr;

	if (buflen < sizeof(struct xfs_efd_log_format)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				efd_formatp, buflen);
		return -EFSCORRUPTED;
	}

	if (item->ri_buf[0].i_len != xfs_efd_log_format32_sizeof(
						efd_formatp->efd_nextents) &&
	    item->ri_buf[0].i_len != xfs_efd_log_format64_sizeof(
						efd_formatp->efd_nextents)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				efd_formatp, buflen);
		return -EFSCORRUPTED;
	}

	xlog_recover_release_intent(log, XFS_LI_EFI, efd_formatp->efd_efi_id);
	return 0;
}

const struct xlog_recover_item_ops xlog_efd_item_ops = {
	.item_type		= XFS_LI_EFD,
	.commit_pass2		= xlog_recover_efd_commit_pass2,
};

#ifdef CONFIG_XFS_RT
STATIC int
xlog_recover_rtefd_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_efd_log_format	*efd_formatp;
	int				buflen = item->ri_buf[0].i_len;

	efd_formatp = item->ri_buf[0].i_addr;

	if (buflen < sizeof(struct xfs_efd_log_format)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				efd_formatp, buflen);
		return -EFSCORRUPTED;
	}

	if (item->ri_buf[0].i_len != xfs_efd_log_format32_sizeof(
						efd_formatp->efd_nextents) &&
	    item->ri_buf[0].i_len != xfs_efd_log_format64_sizeof(
						efd_formatp->efd_nextents)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, log->l_mp,
				efd_formatp, buflen);
		return -EFSCORRUPTED;
	}

	xlog_recover_release_intent(log, XFS_LI_EFI_RT,
			efd_formatp->efd_efi_id);
	return 0;
}
#else
# define xlog_recover_rtefd_commit_pass2	xlog_recover_rtefi_commit_pass2
#endif

const struct xlog_recover_item_ops xlog_rtefd_item_ops = {
	.item_type		= XFS_LI_EFD_RT,
	.commit_pass2		= xlog_recover_rtefd_commit_pass2,
};
