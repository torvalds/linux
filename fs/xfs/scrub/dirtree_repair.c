// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2023-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_trans_space.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_attr.h"
#include "xfs_parent.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/bitmap.h"
#include "scrub/ino_bitmap.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/xfblob.h"
#include "scrub/listxattr.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/orphanage.h"
#include "scrub/dirtree.h"
#include "scrub/readdir.h"

/*
 * Directory Tree Structure Repairs
 * ================================
 *
 * If we decide that the directory being scanned is participating in a
 * directory loop, the only change we can make is to remove directory entries
 * pointing down to @sc->ip.  If that leaves it with no parents, the directory
 * should be adopted by the orphanage.
 */

/* Set up to repair directory loops. */
int
xrep_setup_dirtree(
	struct xfs_scrub	*sc)
{
	return xrep_orphanage_try_create(sc);
}

/* Change the outcome of this path. */
static inline void
xrep_dirpath_set_outcome(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path,
	enum xchk_dirpath_outcome	outcome)
{
	trace_xrep_dirpath_set_outcome(dl->sc, path->path_nr, path->nr_steps,
			outcome);

	path->outcome = outcome;
}

/* Delete all paths. */
STATIC void
xrep_dirtree_delete_all_paths(
	struct xchk_dirtree		*dl,
	struct xchk_dirtree_outcomes	*oc)
{
	struct xchk_dirpath		*path;

	xchk_dirtree_for_each_path(dl, path) {
		switch (path->outcome) {
		case XCHK_DIRPATH_CORRUPT:
		case XCHK_DIRPATH_LOOP:
			oc->suspect--;
			oc->bad++;
			xrep_dirpath_set_outcome(dl, path, XCHK_DIRPATH_DELETE);
			break;
		case XCHK_DIRPATH_OK:
			oc->good--;
			oc->bad++;
			xrep_dirpath_set_outcome(dl, path, XCHK_DIRPATH_DELETE);
			break;
		default:
			break;
		}
	}

	ASSERT(oc->suspect == 0);
	ASSERT(oc->good == 0);
}

/* Since this is the surviving path, set the dotdot entry to this value. */
STATIC void
xrep_dirpath_retain_parent(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path)
{
	struct xchk_dirpath_step	step;
	int				error;

	error = xfarray_load(dl->path_steps, path->first_step, &step);
	if (error)
		return;

	dl->parent_ino = be64_to_cpu(step.pptr_rec.p_ino);
}

/* Find the one surviving path so we know how to set dotdot. */
STATIC void
xrep_dirtree_find_surviving_path(
	struct xchk_dirtree		*dl,
	struct xchk_dirtree_outcomes	*oc)
{
	struct xchk_dirpath		*path;
	bool				foundit = false;

	xchk_dirtree_for_each_path(dl, path) {
		switch (path->outcome) {
		case XCHK_DIRPATH_CORRUPT:
		case XCHK_DIRPATH_LOOP:
		case XCHK_DIRPATH_OK:
			if (!foundit) {
				xrep_dirpath_retain_parent(dl, path);
				foundit = true;
				continue;
			}
			ASSERT(foundit == false);
			break;
		default:
			break;
		}
	}

	ASSERT(oc->suspect + oc->good == 1);
}

/* Delete all paths except for the one good one. */
STATIC void
xrep_dirtree_keep_one_good_path(
	struct xchk_dirtree		*dl,
	struct xchk_dirtree_outcomes	*oc)
{
	struct xchk_dirpath		*path;
	bool				foundit = false;

	xchk_dirtree_for_each_path(dl, path) {
		switch (path->outcome) {
		case XCHK_DIRPATH_CORRUPT:
		case XCHK_DIRPATH_LOOP:
			oc->suspect--;
			oc->bad++;
			xrep_dirpath_set_outcome(dl, path, XCHK_DIRPATH_DELETE);
			break;
		case XCHK_DIRPATH_OK:
			if (!foundit) {
				xrep_dirpath_retain_parent(dl, path);
				foundit = true;
				continue;
			}
			oc->good--;
			oc->bad++;
			xrep_dirpath_set_outcome(dl, path, XCHK_DIRPATH_DELETE);
			break;
		default:
			break;
		}
	}

	ASSERT(oc->suspect == 0);
	ASSERT(oc->good < 2);
}

/* Delete all paths except for one suspect one. */
STATIC void
xrep_dirtree_keep_one_suspect_path(
	struct xchk_dirtree		*dl,
	struct xchk_dirtree_outcomes	*oc)
{
	struct xchk_dirpath		*path;
	bool				foundit = false;

	xchk_dirtree_for_each_path(dl, path) {
		switch (path->outcome) {
		case XCHK_DIRPATH_CORRUPT:
		case XCHK_DIRPATH_LOOP:
			if (!foundit) {
				xrep_dirpath_retain_parent(dl, path);
				foundit = true;
				continue;
			}
			oc->suspect--;
			oc->bad++;
			xrep_dirpath_set_outcome(dl, path, XCHK_DIRPATH_DELETE);
			break;
		case XCHK_DIRPATH_OK:
			ASSERT(0);
			break;
		default:
			break;
		}
	}

	ASSERT(oc->suspect == 1);
	ASSERT(oc->good == 0);
}

/*
 * Figure out what to do with the paths we tried to find.  Returns -EDEADLOCK
 * if the scan results have become stale.
 */
STATIC void
xrep_dirtree_decide_fate(
	struct xchk_dirtree		*dl,
	struct xchk_dirtree_outcomes	*oc)
{
	xchk_dirtree_evaluate(dl, oc);

	/* Parentless directories should not have any paths at all. */
	if (xchk_dirtree_parentless(dl)) {
		xrep_dirtree_delete_all_paths(dl, oc);
		return;
	}

	/* One path is exactly the number of paths we want. */
	if (oc->good + oc->suspect == 1) {
		xrep_dirtree_find_surviving_path(dl, oc);
		return;
	}

	/* Zero paths means we should reattach the subdir to the orphanage. */
	if (oc->good + oc->suspect == 0) {
		if (dl->sc->orphanage)
			oc->needs_adoption = true;
		return;
	}

	/*
	 * Otherwise, this subdirectory has too many parents.  If there's at
	 * least one good path, keep it and delete the others.
	 */
	if (oc->good > 0) {
		xrep_dirtree_keep_one_good_path(dl, oc);
		return;
	}

	/*
	 * There are no good paths and there are too many suspect paths.
	 * Keep the first suspect path and delete the rest.
	 */
	xrep_dirtree_keep_one_suspect_path(dl, oc);
}

/*
 * Load the first step of this path into @step and @dl->xname/pptr
 * for later repair work.
 */
STATIC int
xrep_dirtree_prep_path(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path,
	struct xchk_dirpath_step	*step)
{
	int				error;

	error = xfarray_load(dl->path_steps, path->first_step, step);
	if (error)
		return error;

	error = xfblob_loadname(dl->path_names, step->name_cookie, &dl->xname,
			step->name_len);
	if (error)
		return error;

	dl->pptr_rec = step->pptr_rec; /* struct copy */
	return 0;
}

/* Delete the VFS dentry for a removed child. */
STATIC int
xrep_dirtree_purge_dentry(
	struct xchk_dirtree	*dl,
	struct xfs_inode	*dp,
	const struct xfs_name	*name)
{
	struct qstr		qname = QSTR_INIT(name->name, name->len);
	struct dentry		*parent_dentry, *child_dentry;
	int			error = 0;

	/*
	 * Find the dentry for the parent directory.  If there isn't one, we're
	 * done.  Caller already holds i_rwsem for parent and child.
	 */
	parent_dentry = d_find_alias(VFS_I(dp));
	if (!parent_dentry)
		return 0;

	/* The VFS thinks the parent is a directory, right? */
	if (!d_is_dir(parent_dentry)) {
		ASSERT(d_is_dir(parent_dentry));
		error = -EFSCORRUPTED;
		goto out_dput_parent;
	}

	/*
	 * Try to find the dirent pointing to the child.  If there isn't one,
	 * we're done.
	 */
	qname.hash = full_name_hash(parent_dentry, name->name, name->len);
	child_dentry = d_lookup(parent_dentry, &qname);
	if (!child_dentry) {
		error = 0;
		goto out_dput_parent;
	}

	trace_xrep_dirtree_delete_child(dp->i_mount, child_dentry);

	/* Child is not a directory?  We're screwed. */
	if (!d_is_dir(child_dentry)) {
		ASSERT(d_is_dir(child_dentry));
		error = -EFSCORRUPTED;
		goto out_dput_child;
	}

	/* Replace the child dentry with a negative one. */
	d_delete(child_dentry);

out_dput_child:
	dput(child_dentry);
out_dput_parent:
	dput(parent_dentry);
	return error;
}

/*
 * Prepare to delete a link by taking the IOLOCK of the parent and the child
 * (scrub target).  Caller must hold IOLOCK_EXCL on @sc->ip.  Returns 0 if we
 * took both locks, or a negative errno if we couldn't lock the parent in time.
 */
static inline int
xrep_dirtree_unlink_iolock(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp)
{
	int			error;

	ASSERT(sc->ilock_flags & XFS_IOLOCK_EXCL);

	if (xfs_ilock_nowait(dp, XFS_IOLOCK_EXCL))
		return 0;

	xchk_iunlock(sc, XFS_IOLOCK_EXCL);
	do {
		xfs_ilock(dp, XFS_IOLOCK_EXCL);
		if (xchk_ilock_nowait(sc, XFS_IOLOCK_EXCL))
			break;
		xfs_iunlock(dp, XFS_IOLOCK_EXCL);

		if (xchk_should_terminate(sc, &error)) {
			xchk_ilock(sc, XFS_IOLOCK_EXCL);
			return error;
		}

		delay(1);
	} while (1);

	return 0;
}

/*
 * Remove a link from the directory tree and update the dcache.  Returns
 * -ESTALE if the scan data are now out of date.
 */
STATIC int
xrep_dirtree_unlink(
	struct xchk_dirtree		*dl,
	struct xfs_inode		*dp,
	struct xchk_dirpath		*path,
	struct xchk_dirpath_step	*step)
{
	struct xfs_scrub		*sc = dl->sc;
	struct xfs_mount		*mp = sc->mp;
	xfs_ino_t			dotdot_ino;
	xfs_ino_t			parent_ino = dl->parent_ino;
	unsigned int			resblks;
	int				dontcare;
	int				error;

	/* Take IOLOCK_EXCL of the parent and child. */
	error = xrep_dirtree_unlink_iolock(sc, dp);
	if (error)
		return error;

	/*
	 * Create the transaction that we need to sever the path.  Ignore
	 * EDQUOT and ENOSPC being returned via nospace_error because the
	 * directory code can handle a reservationless update.
	 */
	resblks = xfs_remove_space_res(mp, step->name_len);
	error = xfs_trans_alloc_dir(dp, &M_RES(mp)->tr_remove, sc->ip,
			&resblks, &sc->tp, &dontcare);
	if (error)
		goto out_iolock;

	/*
	 * Cancel if someone invalidate the paths while we were trying to get
	 * the ILOCK.
	 */
	mutex_lock(&dl->lock);
	if (dl->stale) {
		mutex_unlock(&dl->lock);
		error = -ESTALE;
		goto out_trans_cancel;
	}
	xrep_dirpath_set_outcome(dl, path, XREP_DIRPATH_DELETING);
	mutex_unlock(&dl->lock);

	trace_xrep_dirtree_delete_path(dl->sc, sc->ip, path->path_nr,
			&dl->xname, &dl->pptr_rec);

	/*
	 * Decide if we need to reset the dotdot entry.  Rules:
	 *
	 * - If there's a surviving parent, we want dotdot to point there.
	 * - If we don't have any surviving parents, then point dotdot at the
	 *   root dir.
	 * - If dotdot is already set to the value we want, pass in NULLFSINO
	 *   for no change necessary.
	 *
	 * Do this /before/ we dirty anything, in case the dotdot lookup
	 * fails.
	 */
	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &dotdot_ino);
	if (error)
		goto out_trans_cancel;
	if (parent_ino == NULLFSINO)
		parent_ino = dl->root_ino;
	if (dotdot_ino == parent_ino)
		parent_ino = NULLFSINO;

	/* Drop the link from sc->ip's dotdot entry.  */
	error = xfs_droplink(sc->tp, dp);
	if (error)
		goto out_trans_cancel;

	/* Reset the dotdot entry to a surviving parent. */
	if (parent_ino != NULLFSINO) {
		error = xfs_dir_replace(sc->tp, sc->ip, &xfs_name_dotdot,
				parent_ino, 0);
		if (error)
			goto out_trans_cancel;
	}

	/* Drop the link from dp to sc->ip. */
	error = xfs_droplink(sc->tp, sc->ip);
	if (error)
		goto out_trans_cancel;

	error = xfs_dir_removename(sc->tp, dp, &dl->xname, sc->ip->i_ino,
			resblks);
	if (error) {
		ASSERT(error != -ENOENT);
		goto out_trans_cancel;
	}

	if (xfs_has_parent(sc->mp)) {
		error = xfs_parent_removename(sc->tp, &dl->ppargs, dp,
				&dl->xname, sc->ip);
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * Notify dirent hooks that we removed the bad link, invalidate the
	 * dcache, and commit the repair.
	 */
	xfs_dir_update_hook(dp, sc->ip, -1, &dl->xname);
	error = xrep_dirtree_purge_dentry(dl, dp, &dl->xname);
	if (error)
		goto out_trans_cancel;

	error = xrep_trans_commit(sc);
	goto out_ilock;

out_trans_cancel:
	xchk_trans_cancel(sc);
out_ilock:
	xfs_iunlock(sc->ip, XFS_ILOCK_EXCL);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
out_iolock:
	xfs_iunlock(dp, XFS_IOLOCK_EXCL);
	return error;
}

/*
 * Delete a directory entry that points to this directory.  Returns -ESTALE
 * if the scan data are now out of date.
 */
STATIC int
xrep_dirtree_delete_path(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path)
{
	struct xchk_dirpath_step	step;
	struct xfs_scrub		*sc = dl->sc;
	struct xfs_inode		*dp;
	int				error;

	/*
	 * Load the parent pointer and directory inode for this path, then
	 * drop the scan lock, the ILOCK, and the transaction so that
	 * _delete_path can reserve the proper transaction.  This sets up
	 * @dl->xname for the deletion.
	 */
	error = xrep_dirtree_prep_path(dl, path, &step);
	if (error)
		return error;

	error = xchk_iget(sc, be64_to_cpu(step.pptr_rec.p_ino), &dp);
	if (error)
		return error;

	mutex_unlock(&dl->lock);
	xchk_trans_cancel(sc);
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/* Delete the directory link and release the parent. */
	error = xrep_dirtree_unlink(dl, dp, path, &step);
	xchk_irele(sc, dp);

	/*
	 * Retake all the resources we had at the beginning even if the repair
	 * failed or the scan data are now stale.  This keeps things simple for
	 * the caller.
	 */
	xchk_trans_alloc_empty(sc);
	xchk_ilock(sc, XFS_ILOCK_EXCL);
	mutex_lock(&dl->lock);

	if (!error && dl->stale)
		error = -ESTALE;
	return error;
}

/* Add a new path to represent our in-progress adoption. */
STATIC int
xrep_dirtree_create_adoption_path(
	struct xchk_dirtree		*dl)
{
	struct xfs_scrub		*sc = dl->sc;
	struct xchk_dirpath		*path;
	int				error;

	/*
	 * We should have capped the number of paths at XFS_MAXLINK-1 in the
	 * scanner.
	 */
	if (dl->nr_paths > XFS_MAXLINK) {
		ASSERT(dl->nr_paths <= XFS_MAXLINK);
		return -EFSCORRUPTED;
	}

	/*
	 * Create a new xchk_path structure to remember this parent pointer
	 * and record the first name step.
	 */
	path = kmalloc(sizeof(struct xchk_dirpath), XCHK_GFP_FLAGS);
	if (!path)
		return -ENOMEM;

	INIT_LIST_HEAD(&path->list);
	xino_bitmap_init(&path->seen_inodes);
	path->nr_steps = 0;
	path->outcome = XREP_DIRPATH_ADOPTING;

	/*
	 * Record the new link that we just created in the orphanage.  Because
	 * adoption is the last repair that we perform, we don't bother filling
	 * in the path all the way back to the root.
	 */
	xfs_inode_to_parent_rec(&dl->pptr_rec, sc->orphanage);

	error = xino_bitmap_set(&path->seen_inodes, sc->orphanage->i_ino);
	if (error)
		goto out_path;

	trace_xrep_dirtree_create_adoption(sc, sc->ip, dl->nr_paths,
			&dl->xname, &dl->pptr_rec);

	error = xchk_dirpath_append(dl, sc->ip, path, &dl->xname,
			&dl->pptr_rec);
	if (error)
		goto out_path;

	path->first_step = xfarray_length(dl->path_steps) - 1;
	path->second_step = XFARRAY_NULLIDX;
	path->path_nr = dl->nr_paths;

	list_add_tail(&path->list, &dl->path_list);
	dl->nr_paths++;
	return 0;

out_path:
	kfree(path);
	return error;
}

/*
 * Prepare to move a file to the orphanage by taking the IOLOCK of the
 * orphanage and the child (scrub target).  Caller must hold IOLOCK_EXCL on
 * @sc->ip.  Returns 0 if we took both locks, or a negative errno if we
 * couldn't lock the orphanage in time.
 */
static inline int
xrep_dirtree_adopt_iolock(
	struct xfs_scrub	*sc)
{
	int			error;

	ASSERT(sc->ilock_flags & XFS_IOLOCK_EXCL);

	if (xrep_orphanage_ilock_nowait(sc, XFS_IOLOCK_EXCL))
		return 0;

	xchk_iunlock(sc, XFS_IOLOCK_EXCL);
	do {
		xrep_orphanage_ilock(sc, XFS_IOLOCK_EXCL);
		if (xchk_ilock_nowait(sc, XFS_IOLOCK_EXCL))
			break;
		xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);

		if (xchk_should_terminate(sc, &error)) {
			xchk_ilock(sc, XFS_IOLOCK_EXCL);
			return error;
		}

		delay(1);
	} while (1);

	return 0;
}

/*
 * Reattach this orphaned directory to the orphanage.  Do not call this with
 * any resources held.  Returns -ESTALE if the scan data have become out of
 * date.
 */
STATIC int
xrep_dirtree_adopt(
	struct xchk_dirtree		*dl)
{
	struct xfs_scrub		*sc = dl->sc;
	int				error;

	/* Take the IOLOCK of the orphanage and the scrub target. */
	error = xrep_dirtree_adopt_iolock(sc);
	if (error)
		return error;

	/*
	 * Set up for an adoption.  The directory tree fixer runs after the
	 * link counts have been corrected.  Therefore, we must bump the
	 * child's link count since there will be no further opportunity to fix
	 * errors.
	 */
	error = xrep_adoption_trans_alloc(sc, &dl->adoption);
	if (error)
		goto out_iolock;
	dl->adoption.bump_child_nlink = true;

	/* Figure out what name we're going to use here. */
	error = xrep_adoption_compute_name(&dl->adoption, &dl->xname);
	if (error)
		goto out_trans;

	/*
	 * Now that we have a proposed name for the orphanage entry, create
	 * a faux path so that the live update hook will see it.
	 */
	mutex_lock(&dl->lock);
	if (dl->stale) {
		mutex_unlock(&dl->lock);
		error = -ESTALE;
		goto out_trans;
	}
	error = xrep_dirtree_create_adoption_path(dl);
	mutex_unlock(&dl->lock);
	if (error)
		goto out_trans;

	/* Reparent the directory. */
	error = xrep_adoption_move(&dl->adoption);
	if (error)
		goto out_trans;

	/*
	 * Commit the name and release all inode locks except for the scrub
	 * target's IOLOCK.
	 */
	error = xrep_trans_commit(sc);
	goto out_ilock;

out_trans:
	xchk_trans_cancel(sc);
out_ilock:
	xchk_iunlock(sc, XFS_ILOCK_EXCL);
	xrep_orphanage_iunlock(sc, XFS_ILOCK_EXCL);
out_iolock:
	xrep_orphanage_iunlock(sc, XFS_IOLOCK_EXCL);
	return error;
}

/*
 * This newly orphaned directory needs to be adopted by the orphanage.
 * Make this happen.
 */
STATIC int
xrep_dirtree_move_to_orphanage(
	struct xchk_dirtree		*dl)
{
	struct xfs_scrub		*sc = dl->sc;
	int				error;

	/*
	 * Start by dropping all the resources that we hold so that we can grab
	 * all the resources that we need for the adoption.
	 */
	mutex_unlock(&dl->lock);
	xchk_trans_cancel(sc);
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/* Perform the adoption. */
	error = xrep_dirtree_adopt(dl);

	/*
	 * Retake all the resources we had at the beginning even if the repair
	 * failed or the scan data are now stale.  This keeps things simple for
	 * the caller.
	 */
	xchk_trans_alloc_empty(sc);
	xchk_ilock(sc, XFS_ILOCK_EXCL);
	mutex_lock(&dl->lock);

	if (!error && dl->stale)
		error = -ESTALE;
	return error;
}

/*
 * Try to fix all the problems.  Returns -ESTALE if the scan data have become
 * out of date.
 */
STATIC int
xrep_dirtree_fix_problems(
	struct xchk_dirtree		*dl,
	struct xchk_dirtree_outcomes	*oc)
{
	struct xchk_dirpath		*path;
	int				error;

	/* Delete all the paths we don't want. */
	xchk_dirtree_for_each_path(dl, path) {
		if (path->outcome != XCHK_DIRPATH_DELETE)
			continue;

		error = xrep_dirtree_delete_path(dl, path);
		if (error)
			return error;
	}

	/* Reparent this directory to the orphanage. */
	if (oc->needs_adoption) {
		if (xrep_orphanage_can_adopt(dl->sc))
			return xrep_dirtree_move_to_orphanage(dl);
		return -EFSCORRUPTED;
	}

	return 0;
}

/* Fix directory loops involving this directory. */
int
xrep_dirtree(
	struct xfs_scrub		*sc)
{
	struct xchk_dirtree		*dl = sc->buf;
	struct xchk_dirtree_outcomes	oc;
	int				error;

	/*
	 * Prepare to fix the directory tree by retaking the scan lock.  The
	 * order of resource acquisition is still IOLOCK -> transaction ->
	 * ILOCK -> scan lock.
	 */
	mutex_lock(&dl->lock);
	do {
		/*
		 * Decide what we're going to do, then do it.  An -ESTALE
		 * return here means the scan results are invalid and we have
		 * to walk again.
		 */
		if (!dl->stale) {
			xrep_dirtree_decide_fate(dl, &oc);

			trace_xrep_dirtree_decided_fate(dl, &oc);

			error = xrep_dirtree_fix_problems(dl, &oc);
			if (!error || error != -ESTALE)
				break;
		}
		error = xchk_dirtree_find_paths_to_root(dl);
		if (error == -ELNRNG || error == -ENOSR)
			error = -EFSCORRUPTED;
	} while (!error);
	mutex_unlock(&dl->lock);

	return error;
}
