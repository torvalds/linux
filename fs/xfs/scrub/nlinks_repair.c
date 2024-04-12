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
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/repair.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/iscan.h"
#include "scrub/nlinks.h"
#include "scrub/trace.h"

/*
 * Live Inode Link Count Repair
 * ============================
 *
 * Use the live inode link count information that we collected to replace the
 * nlink values of the incore inodes.  A scrub->repair cycle should have left
 * the live data and hooks active, so this is safe so long as we make sure the
 * inode is locked.
 */

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
	bool			dirty = false;
	int			error;

	xchk_ilock(sc, XFS_IOLOCK_EXCL);

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_link, 0, 0, 0, &sc->tp);
	if (error)
		return error;

	xchk_ilock(sc, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(sc->tp, ip, 0);

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
	 * We did not find any links to this inode.  If the inode agrees, we
	 * have nothing further to do.  If not, the inode has a nonzero link
	 * count and we don't have anywhere to graft the child onto.  Dropping
	 * a live inode's link count to zero can cause unexpected shutdowns in
	 * inactivation, so leave it alone.
	 */
	if (total_links == 0) {
		if (actual_nlink != 0)
			trace_xrep_nlinks_unfixable_inode(mp, ip, &obs);
		goto out_trans;
	}

	/* Commit the new link count if it changed. */
	if (total_links != actual_nlink) {
		if (total_links > XFS_MAXLINK) {
			trace_xrep_nlinks_unfixable_inode(mp, ip, &obs);
			goto out_trans;
		}

		trace_xrep_nlinks_update_inode(mp, ip, &obs);

		set_nlink(VFS_I(ip), total_links);
		dirty = true;
	}

	if (!dirty) {
		error = 0;
		goto out_trans;
	}

	xfs_trans_log_inode(sc->tp, ip, XFS_ILOG_CORE);

	error = xrep_trans_commit(sc);
	xchk_iunlock(sc, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	return error;

out_scanlock:
	mutex_unlock(&xnc->lock);
out_trans:
	xchk_trans_cancel(sc);
	xchk_iunlock(sc, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
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
	 * entry) but no forward link are unfixable, so we cannot repair the
	 * link count of the parent directory based on the back link count
	 * alone.  Filesystems without ftype support are rare (old V4) so we
	 * just skip out here.
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
		error = xchk_trans_alloc_empty(sc);
		if (error)
			break;
	}
	xchk_iscan_iter_finish(&xnc->compare_iscan);
	xchk_iscan_teardown(&xnc->compare_iscan);

	return error;
}
