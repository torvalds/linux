// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"

/*
 * Sort buffer items for log recovery.  Most buffer items should end up on the
 * buffer list and are recovered first, with the following exceptions:
 *
 * 1. XFS_BLF_CANCEL buffers must be processed last because some log items
 *    might depend on the incor ecancellation record, and replaying a cancelled
 *    buffer item can remove the incore record.
 *
 * 2. XFS_BLF_INODE_BUF buffers are handled after most regular items so that
 *    we replay di_next_unlinked only after flushing the inode 'free' state
 *    to the inode buffer.
 *
 * See xlog_recover_reorder_trans for more details.
 */
STATIC enum xlog_recover_reorder
xlog_recover_buf_reorder(
	struct xlog_recover_item	*item)
{
	struct xfs_buf_log_format	*buf_f = item->ri_buf[0].i_addr;

	if (buf_f->blf_flags & XFS_BLF_CANCEL)
		return XLOG_REORDER_CANCEL_LIST;
	if (buf_f->blf_flags & XFS_BLF_INODE_BUF)
		return XLOG_REORDER_INODE_BUFFER_LIST;
	return XLOG_REORDER_BUFFER_LIST;
}

STATIC void
xlog_recover_buf_ra_pass2(
	struct xlog                     *log,
	struct xlog_recover_item        *item)
{
	struct xfs_buf_log_format	*buf_f = item->ri_buf[0].i_addr;

	xlog_buf_readahead(log, buf_f->blf_blkno, buf_f->blf_len, NULL);
}

/*
 * Build up the table of buf cancel records so that we don't replay cancelled
 * data in the second pass.
 */
static int
xlog_recover_buf_commit_pass1(
	struct xlog			*log,
	struct xlog_recover_item	*item)
{
	struct xfs_buf_log_format	*bf = item->ri_buf[0].i_addr;

	if (!xfs_buf_log_check_iovec(&item->ri_buf[0])) {
		xfs_err(log->l_mp, "bad buffer log item size (%d)",
				item->ri_buf[0].i_len);
		return -EFSCORRUPTED;
	}

	if (!(bf->blf_flags & XFS_BLF_CANCEL))
		trace_xfs_log_recover_buf_not_cancel(log, bf);
	else if (xlog_add_buffer_cancelled(log, bf->blf_blkno, bf->blf_len))
		trace_xfs_log_recover_buf_cancel_add(log, bf);
	else
		trace_xfs_log_recover_buf_cancel_ref_inc(log, bf);
	return 0;
}

const struct xlog_recover_item_ops xlog_buf_item_ops = {
	.item_type		= XFS_LI_BUF,
	.reorder		= xlog_recover_buf_reorder,
	.ra_pass2		= xlog_recover_buf_ra_pass2,
	.commit_pass1		= xlog_recover_buf_commit_pass1,
};
