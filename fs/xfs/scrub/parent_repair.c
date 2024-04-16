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
#include "scrub/tempfile.h"
#include "scrub/orphanage.h"

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

	/* Orphanage reparenting request. */
	struct xrep_adoption	adoption;

	/* Directory entry name, plus the trailing null. */
	struct xfs_name		xname;
	unsigned char		namebuf[MAXNAMELEN];
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
	rp->xname.name = rp->namebuf;
	sc->buf = rp;

	return xrep_orphanage_try_create(sc);
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
 * Move the current file to the orphanage.
 *
 * Caller must hold IOLOCK_EXCL on @sc->ip, and no other inode locks.  Upon
 * successful return, the scrub transaction will have enough extra reservation
 * to make the move; it will hold IOLOCK_EXCL and ILOCK_EXCL of @sc->ip and the
 * orphanage; and both inodes will be ijoined.
 */
STATIC int
xrep_parent_move_to_orphanage(
	struct xrep_parent	*rp)
{
	struct xfs_scrub	*sc = rp->sc;
	xfs_ino_t		orig_parent, new_parent;
	int			error;

	/*
	 * We are about to drop the ILOCK on sc->ip to lock the orphanage and
	 * prepare for the adoption.  Therefore, look up the old dotdot entry
	 * for sc->ip so that we can compare it after we re-lock sc->ip.
	 */
	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &orig_parent);
	if (error)
		return error;

	/*
	 * Drop the ILOCK on the scrub target and commit the transaction.
	 * Adoption computes its own resource requirements and gathers the
	 * necessary components.
	 */
	error = xrep_trans_commit(sc);
	if (error)
		return error;
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/* If we can take the orphanage's iolock then we're ready to move. */
	if (!xrep_orphanage_ilock_nowait(sc, XFS_IOLOCK_EXCL)) {
		xchk_iunlock(sc, sc->ilock_flags);
		error = xrep_orphanage_iolock_two(sc);
		if (error)
			return error;
	}

	/* Grab transaction and ILOCK the two files. */
	error = xrep_adoption_trans_alloc(sc, &rp->adoption);
	if (error)
		return error;

	error = xrep_adoption_compute_name(&rp->adoption, &rp->xname);
	if (error)
		return error;

	/*
	 * Now that we've reacquired the ILOCK on sc->ip, look up the dotdot
	 * entry again.  If the parent changed or the child was unlinked while
	 * the child directory was unlocked, we don't need to move the child to
	 * the orphanage after all.
	 */
	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &new_parent);
	if (error)
		return error;

	/*
	 * Attach to the orphanage if we still have a linked directory and it
	 * hasn't been moved.
	 */
	if (orig_parent == new_parent && VFS_I(sc->ip)->i_nlink > 0) {
		error = xrep_adoption_move(&rp->adoption);
		if (error)
			return error;
	}

	/*
	 * Launder the scrub transaction so we can drop the orphanage ILOCK
	 * and IOLOCK.  Return holding the scrub target's ILOCK and IOLOCK.
	 */
	error = xrep_adoption_trans_roll(&rp->adoption);
	if (error)
		return error;

	xrep_orphanage_iunlock(sc, XFS_ILOCK_EXCL);
	xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);
	return 0;
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
		if (xrep_orphanage_can_adopt(rp->sc))
			return xrep_parent_move_to_orphanage(rp);
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
