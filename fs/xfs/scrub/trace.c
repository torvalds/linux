/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
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
#include "xfs_da_format.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_trans.h"
#include "xfs_bit.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"

/* Figure out which block the btree cursor was pointing to. */
static inline xfs_fsblock_t
xfs_scrub_btree_cur_fsbno(
	struct xfs_btree_cur		*cur,
	int				level)
{
	if (level < cur->bc_nlevels && cur->bc_bufs[level])
		return XFS_DADDR_TO_FSB(cur->bc_mp, cur->bc_bufs[level]->b_bn);
	else if (level == cur->bc_nlevels - 1 &&
		 cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return XFS_INO_TO_FSB(cur->bc_mp, cur->bc_private.b.ip->i_ino);
	else if (!(cur->bc_flags & XFS_BTREE_LONG_PTRS))
		return XFS_AGB_TO_FSB(cur->bc_mp, cur->bc_private.a.agno, 0);
	return NULLFSBLOCK;
}

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "scrub/trace.h"
