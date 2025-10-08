// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_bmap_util.h"
#include "xfs_iwalk.h"
#include "xfs_ialloc.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_parent.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/repair.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/iscan.h"
#include "scrub/orphanage.h"
#include "scrub/nlinks.h"
#include "scrub/trace.h"
#include "scrub/tempfile.h"

/*
 * Live Inode Link Count Repair
 * ============================
 *
 * Use the live inode link count information that we collected to replace the
 * nlink values of the incore inodes.  A scrub->repair cycle should have left
 * the live data and hooks active, so this is safe so long as we make sure the
 * inode is locked.
 */

/* Set up to repair inode link counts. */
int
xrep_setup_nlinks(
	struct xfs_scrub	*sc)
{
	return xrep_orphanage_try_create(sc);
}

/*
 * Inodes that aren't the root directory or the orphanage, have a nonzero link
 * count, and no observed parents should be moved to the orphanage.
 */
static inline bool
xrep_nlinks_is_orphaned(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	unsigned int		actual_nlink,
	const struct xchk_nlink	*obs)
{
	if (obs->parents != 0)
		return false;
	if (xchk_inode_is_dirtree_root(ip) || ip == sc->orphanage)
		return false;
	return actual_nlink != 0;
}

/* Remove an inode from the unlinked list. */
STATIC int
xrep_nlinks_iunlink_remove(
	struct xfs_scrub	*sc)
{
	struct xfs_perag	*pag;
	int			error;

	pag = xfs_perag_get(sc->mp, XFS_INO_TO_AGNO(sc->mp, sc->ip->i_ino));
	error = xfs_iunlink_remove(sc->tp, pag, sc->ip);
	xfs_perag_put(pag);
	return error;
}

/*
 * Correct the link count of the given inode.  Because we have to grab locks
 * and resources in a certain order, it's possible that this will be a no-op.
 */
STATIC int
xrep_nlinks_repair_inode(
	struct xchk_nlink_ctrs	*xnc)
{
	struct xchk_nlink	obs;
	struct xfs_scrub	*sc = xnc->sc;
	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*ip = sc->ip;
	uint64_t		total_links;
	uint64_t		actual_nlink;
	bool			orphanage_available = false;
	bool			dirty = false;
	int			error;

	/*
	 * Ignore temporary files being used to stage repairs, since we assume
	 * they're correct for non-directories, and the directory repair code
	 * doesn't bump the link counts for the children.
	 */
	if (xrep_is_tempfile(ip))
		return 0;

	/*
	 * If the filesystem has an orphanage attached to the scrub context,
	 * prepare for a link count repair that could involve @ip being adopted
	 * by the lost+found.
	 */
	if (xrep_orphanage_can_adopt(sc)) {
		error = xrep_orphanage_iolock_two(sc);
		if (error)
			return error;

		error = xrep_adoption_trans_alloc(sc, &xnc->adoption);
		if (error) {
			xchk_iunlock(sc, XFS_IOLOCK_EXCL);
			xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);
		} else {
			orphanage_available = true;
		}
	}

	/*
	 * Either there is no orphanage or we couldn't allocate resources for
	 * that kind of update.  Let's try again with only the resources we
	 * need for a simple link count update, since that's much more common.
	 */
	if (!orphanage_available) {
		xchk_ilock(sc, XFS_IOLOCK_EXCL);

		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_link, 0, 0, 0,
				&sc->tp);
		if (error) {
			xchk_iunlock(sc, XFS_IOLOCK_EXCL);
			return error;
		}

		xchk_ilock(sc, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(sc->tp, ip, 0);
	}

	mutex_lock(&xnc->lock);

	if (xchk_iscan_aborted(&xnc->collect_iscan)) {
		error = -ECANCELED;
		goto out_scanlock;
	}

	error = xfarray_load_sparse(xnc->nlinks, ip->i_ino, &obs);
	if (error)
		goto out_scanlock;

	/*
	 * We're done accessing the shared scan data, so we can drop the lock.
	 * We still hold @ip's ILOCK, so its link count cannot change.
	 */
	mutex_unlock(&xnc->lock);

	total_links = xchk_nlink_total(ip, &obs);
	actual_nlink = VFS_I(ip)->i_nlink;

	/*
	 * Non-directories cannot have directories pointing up to them.
	 *
	 * We previously set error to zero, but set it again because one static
	 * checker author fears that programmers will fail to maintain this
	 * invariant and built their tool to flag this as a security risk.  A
	 * different tool author made their bot complain about the redundant
	 * store.  This is a never-ending and stupid battle; both tools missed
	 * *actual bugs* elsewhere; and I no longer care.
	 */
	if (!S_ISDIR(VFS_I(ip)->i_mode) && obs.children != 0) {
		trace_xrep_nlinks_unfixable_inode(mp, ip, &obs);
		error = 0;
		goto out_trans;
	}

	/*
	 * Decide if we're going to move this file to the orphanage, and fix
	 * up the incore link counts if we are.
	 */
	if (orphanage_available &&
	    xrep_nlinks_is_orphaned(sc, ip, actual_nlink, &obs)) {
		/* Figure out what name we're going to use here. */
		error = xrep_adoption_compute_name(&xnc->adoption, &xnc->xname);
		if (error)
			goto out_trans;

		/*
		 * Reattach this file to the directory tree by moving it to
		 * the orphanage per the adoption parameters that we already
		 * computed.
		 */
		error = xrep_adoption_move(&xnc->adoption);
		if (error)
			goto out_trans;

		/*
		 * Re-read the link counts since the reparenting will have
		 * updated our scan info.
		 */
		mutex_lock(&xnc->lock);
		error = xfarray_load_sparse(xnc->nlinks, ip->i_ino, &obs);
		mutex_unlock(&xnc->lock);
		if (error)
			goto out_trans;

		total_links = xchk_nlink_total(ip, &obs);
		actual_nlink = VFS_I(ip)->i_nlink;
		dirty = true;
	}

	/*
	 * If this inode is linked from the directory tree and on the unlinked
	 * list, remove it from the unlinked list.
	 */
	if (total_links > 0 && xfs_inode_on_unlinked_list(ip)) {
		error = xrep_nlinks_iunlink_remove(sc);
		if (error)
			goto out_trans;
		dirty = true;
	}

	/*
	 * If this inode is not linked from the directory tree yet not on the
	 * unlinked list, put it on the unlinked list.
	 */
	if (total_links == 0 && !xfs_inode_on_unlinked_list(ip)) {
		error = xfs_iunlink(sc->tp, ip);
		if (error)
			goto out_trans;
		dirty = true;
	}

	/* Commit the new link count if it changed. */
	if (total_links != actual_nlink) {
		trace_xrep_nlinks_update_inode(mp, ip, &obs);

		set_nlink(VFS_I(ip), min_t(unsigned long long, total_links,
					   XFS_NLINK_PINNED));
		dirty = true;
	}

	if (!dirty) {
		error = 0;
		goto out_trans;
	}

	xfs_trans_log_inode(sc->tp, ip, XFS_ILOG_CORE);

	error = xrep_trans_commit(sc);
	goto out_unlock;

out_scanlock:
	mutex_unlock(&xnc->lock);
out_trans:
	xchk_trans_cancel(sc);
out_unlock:
	xchk_iunlock(sc, XFS_ILOCK_EXCL);
	if (orphanage_available) {
		xrep_orphanage_iunlock(sc, XFS_ILOCK_EXCL);
		xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);
	}
	xchk_iunlock(sc, XFS_IOLOCK_EXCL);
	return error;
}

/*
 * Try to visit every inode in the filesystem for repairs.  Move on if we can't
 * grab an inode, since we're still making forward progress.
 */
static int
xrep_nlinks_iter(
	struct xchk_nlink_ctrs	*xnc,
	struct xfs_inode	**ipp)
{
	int			error;

	do {
		error = xchk_iscan_iter(&xnc->compare_iscan, ipp);
	} while (error == -EBUSY);

	return error;
}

/* Commit the new inode link counters. */
int
xrep_nlinks(
	struct xfs_scrub	*sc)
{
	struct xchk_nlink_ctrs	*xnc = sc->buf;
	int			error;

	/*
	 * We need ftype for an accurate count of the number of child
	 * subdirectory links.  Child subdirectories with a back link (dotdot
	 * entry) but no forward link are moved to the orphanage, so we cannot
	 * repair the link count of the parent directory based on the back link
	 * count alone.  Filesystems without ftype support are rare (old V4) so
	 * we just skip out here.
	 */
	if (!xfs_has_ftype(sc->mp))
		return -EOPNOTSUPP;

	/*
	 * Use the inobt to walk all allocated inodes to compare and fix the
	 * link counts.  Retry iget every tenth of a second for up to 30
	 * seconds -- even if repair misses a few inodes, we still try to fix
	 * as many of them as we can.
	 */
	xchk_iscan_start(sc, 30000, 100, &xnc->compare_iscan);
	ASSERT(sc->ip == NULL);

	while ((error = xrep_nlinks_iter(xnc, &sc->ip)) == 1) {
		/*
		 * Commit the scrub transaction so that we can create repair
		 * transactions with the correct reservations.
		 */
		xchk_trans_cancel(sc);

		error = xrep_nlinks_repair_inode(xnc);
		xchk_iscan_mark_visited(&xnc->compare_iscan, sc->ip);
		xchk_irele(sc, sc->ip);
		sc->ip = NULL;
		if (error)
			break;

		if (xchk_should_terminate(sc, &error))
			break;

		/*
		 * Create a new empty transaction so that we can advance the
		 * iscan cursor without deadlocking if the inobt has a cycle.
		 * We can only push the inactivation workqueues with an empty
		 * transaction.
		 */
		xchk_trans_alloc_empty(sc);
	}
	xchk_iscan_iter_finish(&xnc->compare_iscan);
	xchk_iscan_teardown(&xnc->compare_iscan);

	return error;
}
