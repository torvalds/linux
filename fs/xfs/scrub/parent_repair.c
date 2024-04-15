// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_bmap_btree.h"
#include "xfs_dir2_priv.h"
#include "xfs_trans_space.h"
#include "xfs_health.h"
#include "xfs_exchmaps.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/iscan.h"
#include "scrub/findparent.h"
#include "scrub/readdir.h"

/*
 * Repairing The Directory Parent Pointer
 * ======================================
 *
 * Currently, only directories support parent pointers (in the form of '..'
 * entries), so we simply scan the filesystem and update the '..' entry.
 *
 * Note that because the only parent pointer is the dotdot entry, we won't
 * touch an unhealthy directory, since the directory repair code is perfectly
 * capable of rebuilding a directory with the proper parent inode.
 *
 * See the section on locking issues in dir_repair.c for more information about
 * conflicts with the VFS.  The findparent code wll keep our incore parent
 * inode up to date.
 */

struct xrep_parent {
	struct xfs_scrub	*sc;

	/*
	 * Information used to scan the filesystem to find the inumber of the
	 * dotdot entry for this directory.
	 */
	struct xrep_parent_scan_info pscan;
};

/* Tear down all the incore stuff we created. */
static void
xrep_parent_teardown(
	struct xrep_parent	*rp)
{
	xrep_findparent_scan_teardown(&rp->pscan);
}

/* Set up for a parent repair. */
int
xrep_setup_parent(
	struct xfs_scrub	*sc)
{
	struct xrep_parent	*rp;

	xchk_fsgates_enable(sc, XCHK_FSGATES_DIRENTS);

	rp = kvzalloc(sizeof(struct xrep_parent), XCHK_GFP_FLAGS);
	if (!rp)
		return -ENOMEM;
	rp->sc = sc;
	sc->buf = rp;

	return 0;
}

/*
 * Scan all files in the filesystem for a child dirent that we can turn into
 * the dotdot entry for this directory.
 */
STATIC int
xrep_parent_find_dotdot(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	xfs_ino_t		ino;
	unsigned int		sick, checked;
	int			error;

	/*
	 * Avoid sick directories.  There shouldn't be anyone else clearing the
	 * directory's sick status.
	 */
	xfs_inode_measure_sickness(sc->ip, &sick, &checked);
	if (sick & XFS_SICK_INO_DIR)
		return -EFSCORRUPTED;

	ino = xrep_findparent_self_reference(sc);
	if (ino != NULLFSINO) {
		xrep_findparent_scan_finish_early(&rp->pscan, ino);
		return 0;
	}

	/*
	 * Drop the ILOCK on this directory so that we can scan for the dotdot
	 * entry.  Figure out who is going to be the parent of this directory,
	 * then retake the ILOCK so that we can salvage directory entries.
	 */
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/* Does the VFS dcache have an answer for us? */
	ino = xrep_findparent_from_dcache(sc);
	if (ino != NULLFSINO) {
		error = xrep_findparent_confirm(sc, &ino);
		if (!error && ino != NULLFSINO) {
			xrep_findparent_scan_finish_early(&rp->pscan, ino);
			goto out_relock;
		}
	}

	/* Scan the entire filesystem for a parent. */
	error = xrep_findparent_scan(&rp->pscan);
out_relock:
	xchk_ilock(sc, XFS_ILOCK_EXCL);

	return error;
}

/* Reset a directory's dotdot entry, if needed. */
STATIC int
xrep_parent_reset_dotdot(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	xfs_ino_t		ino;
	unsigned int		spaceres;
	int			error = 0;

	ASSERT(sc->ilock_flags & XFS_ILOCK_EXCL);

	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &ino);
	if (error || ino == rp->pscan.parent_ino)
		return error;

	xfs_trans_ijoin(sc->tp, sc->ip, 0);

	trace_xrep_parent_reset_dotdot(sc->ip, rp->pscan.parent_ino);

	/*
	 * Reserve more space just in case we have to expand the dir.  We're
	 * allowed to exceed quota to repair inconsistent metadata.
	 */
	spaceres = XFS_RENAME_SPACE_RES(sc->mp, xfs_name_dotdot.len);
	error = xfs_trans_reserve_more_inode(sc->tp, sc->ip, spaceres, 0,
			true);
	if (error)
		return error;

	error = xfs_dir_replace(sc->tp, sc->ip, &xfs_name_dotdot,
			rp->pscan.parent_ino, spaceres);
	if (error)
		return error;

	/*
	 * Roll transaction to detach the inode from the transaction but retain
	 * ILOCK_EXCL.
	 */
	return xfs_trans_roll(&sc->tp);
}

/*
 * Commit the new parent pointer structure (currently only the dotdot entry) to
 * the file that we're repairing.
 */
STATIC int
xrep_parent_rebuild_tree(
	struct xrep_parent	*rp)
{
	if (rp->pscan.parent_ino == NULLFSINO) {
		/* Cannot fix orphaned directories yet. */
		return -EFSCORRUPTED;
	}

	return xrep_parent_reset_dotdot(rp);
}

/* Set up the filesystem scan so we can look for parents. */
STATIC int
xrep_parent_setup_scan(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;

	return xrep_findparent_scan_start(sc, &rp->pscan);
}

int
xrep_parent(
	struct xfs_scrub	*sc)
{
	struct xrep_parent	*rp = sc->buf;
	int			error;

	error = xrep_parent_setup_scan(rp);
	if (error)
		return error;

	error = xrep_parent_find_dotdot(rp);
	if (error)
		goto out_teardown;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		goto out_teardown;

	error = xrep_parent_rebuild_tree(rp);
	if (error)
		goto out_teardown;

out_teardown:
	xrep_parent_teardown(rp);
	return error;
}
