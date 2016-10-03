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
#include "xfs_bmap_item.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_inode.h"

/*
 * This routine is called to allocate a "bmap update done"
 * log item.
 */
struct xfs_bud_log_item *
xfs_trans_get_bud(
	struct xfs_trans		*tp,
	struct xfs_bui_log_item		*buip)
{
	struct xfs_bud_log_item		*budp;

	budp = xfs_bud_init(tp->t_mountp, buip);
	xfs_trans_add_item(tp, &budp->bud_item);
	return budp;
}

/*
 * Finish an bmap update and log it to the BUD. Note that the
 * transaction is marked dirty regardless of whether the bmap update
 * succeeds or fails to support the BUI/BUD lifecycle rules.
 */
int
xfs_trans_log_finish_bmap_update(
	struct xfs_trans		*tp,
	struct xfs_bud_log_item		*budp,
	struct xfs_defer_ops		*dop,
	enum xfs_bmap_intent_type	type,
	struct xfs_inode		*ip,
	int				whichfork,
	xfs_fileoff_t			startoff,
	xfs_fsblock_t			startblock,
	xfs_filblks_t			blockcount,
	xfs_exntst_t			state)
{
	int				error;

	error = -EFSCORRUPTED;

	/*
	 * Mark the transaction dirty, even on error. This ensures the
	 * transaction is aborted, which:
	 *
	 * 1.) releases the BUI and frees the BUD
	 * 2.) shuts down the filesystem
	 */
	tp->t_flags |= XFS_TRANS_DIRTY;
	budp->bud_item.li_desc->lid_flags |= XFS_LID_DIRTY;

	return error;
}
