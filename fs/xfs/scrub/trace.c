// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
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
#include "xfs_ag.h"
#include "xfs_rtbitmap.h"
#include "xfs_quota.h"
#include "xfs_quota_defs.h"
#include "scrub/scrub.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/quota.h"

/* Figure out which block the btree cursor was pointing to. */
static inline xfs_fsblock_t
xchk_btree_cur_fsbno(
	struct xfs_btree_cur	*cur,
	int			level)
{
	if (level < cur->bc_nlevels && cur->bc_levels[level].bp)
		return XFS_DADDR_TO_FSB(cur->bc_mp,
				xfs_buf_daddr(cur->bc_levels[level].bp));

	if (level == cur->bc_nlevels - 1 &&
	    (cur->bc_flags & XFS_BTREE_ROOT_IN_INODE))
		return XFS_INO_TO_FSB(cur->bc_mp, cur->bc_ino.ip->i_ino);

	return NULLFSBLOCK;
}

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "scrub/trace.h"
