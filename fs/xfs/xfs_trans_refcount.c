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
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_refcount_item.h"
#include "xfs_alloc.h"
#include "xfs_refcount.h"

/*
 * This routine is called to allocate a "refcount update done"
 * log item.
 */
struct xfs_cud_log_item *
xfs_trans_get_cud(
	struct xfs_trans		*tp,
	struct xfs_cui_log_item		*cuip)
{
	struct xfs_cud_log_item		*cudp;

	cudp = xfs_cud_init(tp->t_mountp, cuip);
	xfs_trans_add_item(tp, &cudp->cud_item);
	return cudp;
}

/*
 * Finish an refcount update and log it to the CUD. Note that the
 * transaction is marked dirty regardless of whether the refcount
 * update succeeds or fails to support the CUI/CUD lifecycle rules.
 */
int
xfs_trans_log_finish_refcount_update(
	struct xfs_trans		*tp,
	struct xfs_cud_log_item		*cudp,
	enum xfs_refcount_intent_type	type,
	xfs_fsblock_t			startblock,
	xfs_extlen_t			blockcount,
	struct xfs_btree_cur		**pcur)
{
	int				error;

	/* XXX: leave this empty for now */
	error = -EFSCORRUPTED;

	/*
	 * Mark the transaction dirty, even on error. This ensures the
	 * transaction is aborted, which:
	 *
	 * 1.) releases the CUI and frees the CUD
	 * 2.) shuts down the filesystem
	 */
	tp->t_flags |= XFS_TRANS_DIRTY;
	cudp->cud_item.li_desc->lid_flags |= XFS_LID_DIRTY;

	return error;
}
