// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_rmap.h"
#include "xfs_health.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/fscounters.h"

/*
 * FS Summary Counters
 * ===================
 *
 * We correct errors in the filesystem summary counters by setting them to the
 * values computed during the obligatory scrub phase.  However, we must be
 * careful not to allow any other thread to change the counters while we're
 * computing and setting new values.  To achieve this, we freeze the
 * filesystem for the whole operation if the REPAIR flag is set.  The checking
 * function is stricter when we've frozen the fs.
 */

/*
 * Reset the superblock counters.  Caller is responsible for freezing the
 * filesystem during the calculation and reset phases.
 */
int
xrep_fscounters(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	struct xchk_fscounters	*fsc = sc->buf;

	/*
	 * Reinitialize the in-core counters from what we computed.  We froze
	 * the filesystem, so there shouldn't be anyone else trying to modify
	 * these counters.
	 */
	if (!fsc->frozen) {
		ASSERT(fsc->frozen);
		return -EFSCORRUPTED;
	}

	trace_xrep_reset_counters(mp, fsc);

	percpu_counter_set(&mp->m_icount, fsc->icount);
	percpu_counter_set(&mp->m_ifree, fsc->ifree);
	percpu_counter_set(&mp->m_fdblocks, fsc->fdblocks);
	percpu_counter_set(&mp->m_frextents, fsc->frextents);
	mp->m_sb.sb_frextents = fsc->frextents;

	return 0;
}
