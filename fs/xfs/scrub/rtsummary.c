// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_inode.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_rtalloc.h"
#include "scrub/scrub.h"
#include "scrub/common.h"

/* Scrub the realtime summary. */
int
xchk_rtsummary(
	struct xfs_scrub	*sc)
{
	struct xfs_inode	*rsumip = sc->mp->m_rsumip;
	struct xfs_inode	*old_ip = sc->ip;
	uint			old_ilock_flags = sc->ilock_flags;
	int			error = 0;

	/*
	 * We ILOCK'd the rt bitmap ip in the setup routine, now lock the
	 * rt summary ip in compliance with the rt inode locking rules.
	 *
	 * Since we switch sc->ip to rsumip we have to save the old ilock
	 * flags so that we don't mix up the inode state that @sc tracks.
	 */
	sc->ip = rsumip;
	sc->ilock_flags = 0;
	xchk_ilock(sc, XFS_ILOCK_EXCL | XFS_ILOCK_RTSUM);

	/* Invoke the fork scrubber. */
	error = xchk_metadata_inode_forks(sc);
	if (error || (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		goto out;

	/* XXX: implement this some day */
	xchk_set_incomplete(sc);
out:
	/* Switch back to the rtbitmap inode and lock flags. */
	xchk_iunlock(sc, XFS_ILOCK_EXCL | XFS_ILOCK_RTSUM);
	sc->ilock_flags = old_ilock_flags;
	sc->ip = old_ip;
	return error;
}
