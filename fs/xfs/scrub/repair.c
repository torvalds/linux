/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
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
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_alloc.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_refcount.h"
#include "xfs_refcount_btree.h"
#include "xfs_extent_busy.h"
#include "xfs_ag_resv.h"
#include "xfs_trans_space.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"

/*
 * Attempt to repair some metadata, if the metadata is corrupt and userspace
 * told us to fix it.  This function returns -EAGAIN to mean "re-run scrub",
 * and will set *fixed to true if it thinks it repaired anything.
 */
int
xfs_repair_attempt(
	struct xfs_inode		*ip,
	struct xfs_scrub_context	*sc,
	bool				*fixed)
{
	int				error = 0;

	trace_xfs_repair_attempt(ip, sc->sm, error);

	xfs_scrub_ag_btcur_free(&sc->sa);

	/* Repair whatever's broken. */
	ASSERT(sc->ops->repair);
	error = sc->ops->repair(sc);
	trace_xfs_repair_done(ip, sc->sm, error);
	switch (error) {
	case 0:
		/*
		 * Repair succeeded.  Commit the fixes and perform a second
		 * scrub so that we can tell userspace if we fixed the problem.
		 */
		sc->sm->sm_flags &= ~XFS_SCRUB_FLAGS_OUT;
		*fixed = true;
		return -EAGAIN;
	case -EDEADLOCK:
	case -EAGAIN:
		/* Tell the caller to try again having grabbed all the locks. */
		if (!sc->try_harder) {
			sc->try_harder = true;
			return -EAGAIN;
		}
		/*
		 * We tried harder but still couldn't grab all the resources
		 * we needed to fix it.  The corruption has not been fixed,
		 * so report back to userspace.
		 */
		return -EFSCORRUPTED;
	default:
		return error;
	}
}

/*
 * Complain about unfixable problems in the filesystem.  We don't log
 * corruptions when IFLAG_REPAIR wasn't set on the assumption that the driver
 * program is xfs_scrub, which will call back with IFLAG_REPAIR set if the
 * administrator isn't running xfs_scrub in no-repairs mode.
 *
 * Use this helper function because _ratelimited silently declares a static
 * structure to track rate limiting information.
 */
void
xfs_repair_failure(
	struct xfs_mount		*mp)
{
	xfs_alert_ratelimited(mp,
"Corruption not fixed during online repair.  Unmount and run xfs_repair.");
}

/*
 * Repair probe -- userspace uses this to probe if we're willing to repair a
 * given mountpoint.
 */
int
xfs_repair_probe(
	struct xfs_scrub_context	*sc)
{
	int				error = 0;

	if (xfs_scrub_should_terminate(sc, &error))
		return error;

	return 0;
}
