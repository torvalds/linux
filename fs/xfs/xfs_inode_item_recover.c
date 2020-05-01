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
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_trace.h"
#include "xfs_trans_priv.h"
#include "xfs_buf_item.h"
#include "xfs_log.h"
#include "xfs_error.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"

STATIC void
xlog_recover_inode_ra_pass2(
	struct xlog                     *log,
	struct xlog_recover_item        *item)
{
	if (item->ri_buf[0].i_len == sizeof(struct xfs_inode_log_format)) {
		struct xfs_inode_log_format	*ilfp = item->ri_buf[0].i_addr;

		xlog_buf_readahead(log, ilfp->ilf_blkno, ilfp->ilf_len,
				   &xfs_inode_buf_ra_ops);
	} else {
		struct xfs_inode_log_format_32	*ilfp = item->ri_buf[0].i_addr;

		xlog_buf_readahead(log, ilfp->ilf_blkno, ilfp->ilf_len,
				   &xfs_inode_buf_ra_ops);
	}
}

const struct xlog_recover_item_ops xlog_inode_item_ops = {
	.item_type		= XFS_LI_INODE,
	.ra_pass2		= xlog_recover_inode_ra_pass2,
};
