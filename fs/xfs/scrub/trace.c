// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "scrub/scrub.h"

/* Figure out which block the btree cursor was pointing to. */
static inline xfs_fsblock_t
xchk_btree_cur_fsbno(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level < cur->bc_nlevels && cur->bc_bufs[level])
		return XFS_DADDR_TO_FSB(cur->bc_mp, cur->bc_bufs[level]->b_bn);
	else if (level == cur->bc_nlevels - 1 &&
		 cur->bc_flags & XFS_BTREE_LONG_PTRS)
		return XFS_INO_TO_FSB(cur->bc_mp, cur->bc_ino.ip->i_ino);
	else if (!(cur->bc_flags & XFS_BTREE_LONG_PTRS))
		return XFS_AGB_TO_FSB(cur->bc_mp, cur->bc_ag.agno, 0);
	return NULLFSBLOCK;
}

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "scrub/trace.h"
