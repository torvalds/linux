// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
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
#include "xfs_exchmaps_item.h"
#include "xfs_exchmaps.h"
#include "xfs_log.h"
#include "xfs_bmap.h"
#include "xfs_icache.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_exchrange.h"
#include "xfs_trace.h"

struct kmem_cache	*xfs_xmi_cache;
struct kmem_cache	*xfs_xmd_cache;

static const struct xfs_item_ops xfs_xmi_item_ops;

static inline struct xfs_xmi_log_item *XMI_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_xmi_log_item, xmi_item);
}

STATIC void
xfs_xmi_item_free(
	struct xfs_xmi_log_item	*xmi_lip)
{
	kvfree(xmi_lip->xmi_item.li_lv_shadow);
	kmem_cache_free(xfs_xmi_cache, xmi_lip);
}

/*
 * Freeing the XMI requires that we remove it from the AIL if it has already
 * been placed there. However, the XMI may not yet have been placed in the AIL
 * when called by xfs_xmi_release() from XMD processing due to the ordering of
 * committed vs unpin operations in bulk insert operations. Hence the reference
 * count to ensure only the last caller frees the XMI.
 */
STATIC void
xfs_xmi_release(
	struct xfs_xmi_log_item	*xmi_lip)
{
	ASSERT(atomic_read(&xmi_lip->xmi_refcount) > 0);
	if (atomic_dec_and_test(&xmi_lip->xmi_refcount)) {
		xfs_trans_ail_delete(&xmi_lip->xmi_item, 0);
		xfs_xmi_item_free(xmi_lip);
	}
}


STATIC void
xfs_xmi_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += sizeof(struct xfs_xmi_log_format);
}

/*
 * This is called to fill in the vector of log iovecs for the given xmi log
 * item. We use only 1 iovec, and we point that at the xmi_log_format structure
 * embedded in the xmi item.
 */
STATIC void
xfs_xmi_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_xmi_log_item	*xmi_lip = XMI_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	xmi_lip->xmi_format.xmi_type = XFS_LI_XMI;
	xmi_lip->xmi_format.xmi_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_XMI_FORMAT,
			&xmi_lip->xmi_format,
			sizeof(struct xfs_xmi_log_format));
}

/*
 * The unpin operation is the last place an XMI is manipulated in the log. It
 * is either inserted in the AIL or aborted in the event of a log I/O error. In
 * either case, the XMI transaction has been successfully committed to make it
 * this far. Therefore, we expect whoever committed the XMI to either construct
 * and commit the XMD or drop the XMD's reference in the event of error. Simply
 * drop the log's XMI reference now that the log is done with it.
 */
STATIC void
xfs_xmi_item_unpin(
	struct xfs_log_item	*lip,
	int			remove)
{
	struct xfs_xmi_log_item	*xmi_lip = XMI_ITEM(lip);

	xfs_xmi_release(xmi_lip);
}

/*
 * The XMI has been either committed or aborted if the transaction has been
 * cancelled. If the transaction was cancelled, an XMD isn't going to be
 * constructed and thus we free the XMI here directly.
 */
STATIC void
xfs_xmi_item_release(
	struct xfs_log_item	*lip)
{
	xfs_xmi_release(XMI_ITEM(lip));
}

/* Allocate and initialize an xmi item. */
STATIC struct xfs_xmi_log_item *
xfs_xmi_init(
	struct xfs_mount	*mp)

{
	struct xfs_xmi_log_item	*xmi_lip;

	xmi_lip = kmem_cache_zalloc(xfs_xmi_cache, GFP_KERNEL | __GFP_NOFAIL);

	xfs_log_item_init(mp, &xmi_lip->xmi_item, XFS_LI_XMI, &xfs_xmi_item_ops);
	xmi_lip->xmi_format.xmi_id = (uintptr_t)(void *)xmi_lip;
	atomic_set(&xmi_lip->xmi_refcount, 2);

	return xmi_lip;
}

static inline struct xfs_xmd_log_item *XMD_ITEM(struct xfs_log_item *lip)
{
	return container_of(lip, struct xfs_xmd_log_item, xmd_item);
}

STATIC void
xfs_xmd_item_size(
	struct xfs_log_item	*lip,
	int			*nvecs,
	int			*nbytes)
{
	*nvecs += 1;
	*nbytes += sizeof(struct xfs_xmd_log_format);
}

/*
 * This is called to fill in the vector of log iovecs for the given xmd log
 * item. We use only 1 iovec, and we point that at the xmd_log_format structure
 * embedded in the xmd item.
 */
STATIC void
xfs_xmd_item_format(
	struct xfs_log_item	*lip,
	struct xfs_log_vec	*lv)
{
	struct xfs_xmd_log_item	*xmd_lip = XMD_ITEM(lip);
	struct xfs_log_iovec	*vecp = NULL;

	xmd_lip->xmd_format.xmd_type = XFS_LI_XMD;
	xmd_lip->xmd_format.xmd_size = 1;

	xlog_copy_iovec(lv, &vecp, XLOG_REG_TYPE_XMD_FORMAT, &xmd_lip->xmd_format,
			sizeof(struct xfs_xmd_log_format));
}

/*
 * The XMD is either committed or aborted if the transaction is cancelled. If
 * the transaction is cancelled, drop our reference to the XMI and free the
 * XMD.
 */
STATIC void
xfs_xmd_item_release(
	struct xfs_log_item	*lip)
{
	struct xfs_xmd_log_item	*xmd_lip = XMD_ITEM(lip);

	xfs_xmi_release(xmd_lip->xmd_intent_log_item);
	kvfree(xmd_lip->xmd_item.li_lv_shadow);
	kmem_cache_free(xfs_xmd_cache, xmd_lip);
}

static struct xfs_log_item *
xfs_xmd_item_intent(
	struct xfs_log_item	*lip)
{
	return &XMD_ITEM(lip)->xmd_intent_log_item->xmi_item;
}

static const struct xfs_item_ops xfs_xmd_item_ops = {
	.flags		= XFS_ITEM_RELEASE_WHEN_COMMITTED |
			  XFS_ITEM_INTENT_DONE,
	.iop_size	= xfs_xmd_item_size,
	.iop_format	= xfs_xmd_item_format,
	.iop_release	= xfs_xmd_item_release,
	.iop_intent	= xfs_xmd_item_intent,
};

/* Log file mapping exchange information in the intent item. */
STATIC struct xfs_log_item *
xfs_exchmaps_create_intent(
	struct xfs_trans		*tp,
	struct list_head		*items,
	unsigned int			count,
	bool				sort)
{
	struct xfs_xmi_log_item		*xmi_lip;
	struct xfs_exchmaps_intent	*xmi;
	struct xfs_xmi_log_format	*xlf;

	ASSERT(count == 1);

	xmi = list_first_entry_or_null(items, struct xfs_exchmaps_intent,
			xmi_list);

	xmi_lip = xfs_xmi_init(tp->t_mountp);
	xlf = &xmi_lip->xmi_format;

	xlf->xmi_inode1 = xmi->xmi_ip1->i_ino;
	xlf->xmi_igen1 = VFS_I(xmi->xmi_ip1)->i_generation;
	xlf->xmi_inode2 = xmi->xmi_ip2->i_ino;
	xlf->xmi_igen2 = VFS_I(xmi->xmi_ip2)->i_generation;
	xlf->xmi_startoff1 = xmi->xmi_startoff1;
	xlf->xmi_startoff2 = xmi->xmi_startoff2;
	xlf->xmi_blockcount = xmi->xmi_blockcount;
	xlf->xmi_isize1 = xmi->xmi_isize1;
	xlf->xmi_isize2 = xmi->xmi_isize2;
	xlf->xmi_flags = xmi->xmi_flags & XFS_EXCHMAPS_LOGGED_FLAGS;

	return &xmi_lip->xmi_item;
}

STATIC struct xfs_log_item *
xfs_exchmaps_create_done(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	unsigned int			count)
{
	struct xfs_xmi_log_item		*xmi_lip = XMI_ITEM(intent);
	struct xfs_xmd_log_item		*xmd_lip;

	xmd_lip = kmem_cache_zalloc(xfs_xmd_cache, GFP_KERNEL | __GFP_NOFAIL);
	xfs_log_item_init(tp->t_mountp, &xmd_lip->xmd_item, XFS_LI_XMD,
			  &xfs_xmd_item_ops);
	xmd_lip->xmd_intent_log_item = xmi_lip;
	xmd_lip->xmd_format.xmd_xmi_id = xmi_lip->xmi_format.xmi_id;

	return &xmd_lip->xmd_item;
}

/* Add this deferred XMI to the transaction. */
void
xfs_exchmaps_defer_add(
	struct xfs_trans		*tp,
	struct xfs_exchmaps_intent	*xmi)
{
	trace_xfs_exchmaps_defer(tp->t_mountp, xmi);

	xfs_defer_add(tp, &xmi->xmi_list, &xfs_exchmaps_defer_type);
}

static inline struct xfs_exchmaps_intent *xmi_entry(const struct list_head *e)
{
	return list_entry(e, struct xfs_exchmaps_intent, xmi_list);
}

/* Cancel a deferred file mapping exchange. */
STATIC void
xfs_exchmaps_cancel_item(
	struct list_head		*item)
{
	struct xfs_exchmaps_intent	*xmi = xmi_entry(item);

	kmem_cache_free(xfs_exchmaps_intent_cache, xmi);
}

/* Process a deferred file mapping exchange. */
STATIC int
xfs_exchmaps_finish_item(
	struct xfs_trans		*tp,
	struct xfs_log_item		*done,
	struct list_head		*item,
	struct xfs_btree_cur		**state)
{
	struct xfs_exchmaps_intent	*xmi = xmi_entry(item);
	int				error;

	/*
	 * Exchange one more mappings between two files.  If there's still more
	 * work to do, we want to requeue ourselves after all other pending
	 * deferred operations have finished.  This includes all of the dfops
	 * that we queued directly as well as any new ones created in the
	 * process of finishing the others.  Doing so prevents us from queuing
	 * a large number of XMI log items in kernel memory, which in turn
	 * prevents us from pinning the tail of the log (while logging those
	 * new XMI items) until the first XMI items can be processed.
	 */
	error = xfs_exchmaps_finish_one(tp, xmi);
	if (error != -EAGAIN)
		xfs_exchmaps_cancel_item(item);
	return error;
}

/* Abort all pending XMIs. */
STATIC void
xfs_exchmaps_abort_intent(
	struct xfs_log_item		*intent)
{
	xfs_xmi_release(XMI_ITEM(intent));
}

/* Is this recovered XMI ok? */
static inline bool
xfs_xmi_validate(
	struct xfs_mount		*mp,
	struct xfs_xmi_log_item		*xmi_lip)
{
	struct xfs_xmi_log_format	*xlf = &xmi_lip->xmi_format;

	if (!xfs_has_exchange_range(mp))
		return false;

	if (xmi_lip->xmi_format.__pad != 0)
		return false;

	if (xlf->xmi_flags & ~XFS_EXCHMAPS_LOGGED_FLAGS)
		return false;

	if (!xfs_verify_ino(mp, xlf->xmi_inode1) ||
	    !xfs_verify_ino(mp, xlf->xmi_inode2))
		return false;

	if (!xfs_verify_fileext(mp, xlf->xmi_startoff1, xlf->xmi_blockcount))
		return false;

	return xfs_verify_fileext(mp, xlf->xmi_startoff2, xlf->xmi_blockcount);
}

/*
 * Use the recovered log state to create a new request, estimate resource
 * requirements, and create a new incore intent state.
 */
STATIC struct xfs_exchmaps_intent *
xfs_xmi_item_recover_intent(
	struct xfs_mount		*mp,
	struct xfs_defer_pending	*dfp,
	const struct xfs_xmi_log_format	*xlf,
	struct xfs_exchmaps_req		*req,
	struct xfs_inode		**ipp1,
	struct xfs_inode		**ipp2)
{
	struct xfs_inode		*ip1, *ip2;
	struct xfs_exchmaps_intent	*xmi;
	int				error;

	/*
	 * Grab both inodes and set IRECOVERY to prevent trimming of post-eof
	 * mappings and freeing of unlinked inodes until we're totally done
	 * processing files.  The ondisk format of this new log item contains
	 * file handle information, which is why recovery for other items do
	 * not check the inode generation number.
	 */
	error = xlog_recover_iget_handle(mp, xlf->xmi_inode1, xlf->xmi_igen1,
			&ip1);
	if (error) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, xlf,
				sizeof(*xlf));
		return ERR_PTR(error);
	}

	error = xlog_recover_iget_handle(mp, xlf->xmi_inode2, xlf->xmi_igen2,
			&ip2);
	if (error) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, xlf,
				sizeof(*xlf));
		goto err_rele1;
	}

	req->ip1 = ip1;
	req->ip2 = ip2;
	req->startoff1 = xlf->xmi_startoff1;
	req->startoff2 = xlf->xmi_startoff2;
	req->blockcount = xlf->xmi_blockcount;
	req->flags = xlf->xmi_flags & XFS_EXCHMAPS_PARAMS;

	xfs_exchrange_ilock(NULL, ip1, ip2);
	error = xfs_exchmaps_estimate(req);
	xfs_exchrange_iunlock(ip1, ip2);
	if (error)
		goto err_rele2;

	*ipp1 = ip1;
	*ipp2 = ip2;
	xmi = xfs_exchmaps_init_intent(req);
	xfs_defer_add_item(dfp, &xmi->xmi_list);
	return xmi;

err_rele2:
	xfs_irele(ip2);
err_rele1:
	xfs_irele(ip1);
	req->ip2 = req->ip1 = NULL;
	return ERR_PTR(error);
}

/* Process a file mapping exchange item that was recovered from the log. */
STATIC int
xfs_exchmaps_recover_work(
	struct xfs_defer_pending	*dfp,
	struct list_head		*capture_list)
{
	struct xfs_exchmaps_req		req = { .flags = 0 };
	struct xfs_trans_res		resv;
	struct xfs_exchmaps_intent	*xmi;
	struct xfs_log_item		*lip = dfp->dfp_intent;
	struct xfs_xmi_log_item		*xmi_lip = XMI_ITEM(lip);
	struct xfs_mount		*mp = lip->li_log->l_mp;
	struct xfs_trans		*tp;
	struct xfs_inode		*ip1, *ip2;
	int				error = 0;

	if (!xfs_xmi_validate(mp, xmi_lip)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				&xmi_lip->xmi_format,
				sizeof(xmi_lip->xmi_format));
		return -EFSCORRUPTED;
	}

	xmi = xfs_xmi_item_recover_intent(mp, dfp, &xmi_lip->xmi_format, &req,
			&ip1, &ip2);
	if (IS_ERR(xmi))
		return PTR_ERR(xmi);

	trace_xfs_exchmaps_recover(mp, xmi);

	resv = xlog_recover_resv(&M_RES(mp)->tr_write);
	error = xfs_trans_alloc(mp, &resv, req.resblks, 0, 0, &tp);
	if (error)
		goto err_rele;

	xfs_exchrange_ilock(tp, ip1, ip2);

	xfs_exchmaps_ensure_reflink(tp, xmi);
	xfs_exchmaps_upgrade_extent_counts(tp, xmi);
	error = xlog_recover_finish_intent(tp, dfp);
	if (error == -EFSCORRUPTED)
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				&xmi_lip->xmi_format,
				sizeof(xmi_lip->xmi_format));
	if (error)
		goto err_cancel;

	/*
	 * Commit transaction, which frees the transaction and saves the inodes
	 * for later replay activities.
	 */
	error = xfs_defer_ops_capture_and_commit(tp, capture_list);
	goto err_unlock;

err_cancel:
	xfs_trans_cancel(tp);
err_unlock:
	xfs_exchrange_iunlock(ip1, ip2);
err_rele:
	xfs_irele(ip2);
	xfs_irele(ip1);
	return error;
}

/* Relog an intent item to push the log tail forward. */
static struct xfs_log_item *
xfs_exchmaps_relog_intent(
	struct xfs_trans		*tp,
	struct xfs_log_item		*intent,
	struct xfs_log_item		*done_item)
{
	struct xfs_xmi_log_item		*xmi_lip;
	struct xfs_xmi_log_format	*old_xlf, *new_xlf;

	old_xlf = &XMI_ITEM(intent)->xmi_format;

	xmi_lip = xfs_xmi_init(tp->t_mountp);
	new_xlf = &xmi_lip->xmi_format;

	new_xlf->xmi_inode1	= old_xlf->xmi_inode1;
	new_xlf->xmi_inode2	= old_xlf->xmi_inode2;
	new_xlf->xmi_igen1	= old_xlf->xmi_igen1;
	new_xlf->xmi_igen2	= old_xlf->xmi_igen2;
	new_xlf->xmi_startoff1	= old_xlf->xmi_startoff1;
	new_xlf->xmi_startoff2	= old_xlf->xmi_startoff2;
	new_xlf->xmi_blockcount	= old_xlf->xmi_blockcount;
	new_xlf->xmi_flags	= old_xlf->xmi_flags;
	new_xlf->xmi_isize1	= old_xlf->xmi_isize1;
	new_xlf->xmi_isize2	= old_xlf->xmi_isize2;

	return &xmi_lip->xmi_item;
}

const struct xfs_defer_op_type xfs_exchmaps_defer_type = {
	.name		= "exchmaps",
	.max_items	= 1,
	.create_intent	= xfs_exchmaps_create_intent,
	.abort_intent	= xfs_exchmaps_abort_intent,
	.create_done	= xfs_exchmaps_create_done,
	.finish_item	= xfs_exchmaps_finish_item,
	.cancel_item	= xfs_exchmaps_cancel_item,
	.recover_work	= xfs_exchmaps_recover_work,
	.relog_intent	= xfs_exchmaps_relog_intent,
};

STATIC bool
xfs_xmi_item_match(
	struct xfs_log_item	*lip,
	uint64_t		intent_id)
{
	return XMI_ITEM(lip)->xmi_format.xmi_id == intent_id;
}

static const struct xfs_item_ops xfs_xmi_item_ops = {
	.flags		= XFS_ITEM_INTENT,
	.iop_size	= xfs_xmi_item_size,
	.iop_format	= xfs_xmi_item_format,
	.iop_unpin	= xfs_xmi_item_unpin,
	.iop_release	= xfs_xmi_item_release,
	.iop_match	= xfs_xmi_item_match,
};

/*
 * This routine is called to create an in-core file mapping exchange item from
 * the xmi format structure which was logged on disk.  It allocates an in-core
 * xmi, copies the exchange information from the format structure into it, and
 * adds the xmi to the AIL with the given LSN.
 */
STATIC int
xlog_recover_xmi_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_mount		*mp = log->l_mp;
	struct xfs_xmi_log_item		*xmi_lip;
	struct xfs_xmi_log_format	*xmi_formatp;
	size_t				len;

	len = sizeof(struct xfs_xmi_log_format);
	if (item->ri_buf[0].iov_len != len) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, log->l_mp);
		return -EFSCORRUPTED;
	}

	xmi_formatp = item->ri_buf[0].iov_base;
	if (xmi_formatp->__pad != 0) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, log->l_mp);
		return -EFSCORRUPTED;
	}

	xmi_lip = xfs_xmi_init(mp);
	memcpy(&xmi_lip->xmi_format, xmi_formatp, len);

	xlog_recover_intent_item(log, &xmi_lip->xmi_item, lsn,
			&xfs_exchmaps_defer_type);
	return 0;
}

const struct xlog_recover_item_ops xlog_xmi_item_ops = {
	.item_type		= XFS_LI_XMI,
	.commit_pass2		= xlog_recover_xmi_commit_pass2,
};

/*
 * This routine is called when an XMD format structure is found in a committed
 * transaction in the log. Its purpose is to cancel the corresponding XMI if it
 * was still in the log. To do this it searches the AIL for the XMI with an id
 * equal to that in the XMD format structure. If we find it we drop the XMD
 * reference, which removes the XMI from the AIL and frees it.
 */
STATIC int
xlog_recover_xmd_commit_pass2(
	struct xlog			*log,
	struct list_head		*buffer_list,
	struct xlog_recover_item	*item,
	xfs_lsn_t			lsn)
{
	struct xfs_xmd_log_format	*xmd_formatp;

	xmd_formatp = item->ri_buf[0].iov_base;
	if (item->ri_buf[0].iov_len != sizeof(struct xfs_xmd_log_format)) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, log->l_mp);
		return -EFSCORRUPTED;
	}

	xlog_recover_release_intent(log, XFS_LI_XMI, xmd_formatp->xmd_xmi_id);
	return 0;
}

const struct xlog_recover_item_ops xlog_xmd_item_ops = {
	.item_type		= XFS_LI_XMD,
	.commit_pass2		= xlog_recover_xmd_commit_pass2,
};
