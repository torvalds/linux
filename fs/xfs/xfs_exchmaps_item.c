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
#include "xfs_log.h"
#include "xfs_bmap.h"
#include "xfs_icache.h"
#include "xfs_trans_space.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"

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
	if (item->ri_buf[0].i_len != len) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, log->l_mp);
		return -EFSCORRUPTED;
	}

	xmi_formatp = item->ri_buf[0].i_addr;
	if (xmi_formatp->__pad != 0) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, log->l_mp);
		return -EFSCORRUPTED;
	}

	xmi_lip = xfs_xmi_init(mp);
	memcpy(&xmi_lip->xmi_format, xmi_formatp, len);

	/* not implemented yet */
	return -EIO;
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

	xmd_formatp = item->ri_buf[0].i_addr;
	if (item->ri_buf[0].i_len != sizeof(struct xfs_xmd_log_format)) {
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
