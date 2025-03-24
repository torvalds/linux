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

/*
 * Directory Tree Structure Validation
 * ===================================
 *
 * Validating the tree qualities of the directory tree structure can be
 * difficult.  If the tree is frozen, running a depth (or breadth) first search
 * and marking a bitmap suffices to determine if there is a cycle.  XORing the
 * mark bitmap with the inode bitmap afterwards tells us if there are
 * disconnected cycles.  If the tree is not frozen, directory updates can move
 * subtrees across the scanner wavefront, which complicates the design greatly.
 *
 * Directory parent pointers change that by enabling an incremental approach to
 * validation of the tree structure.  Instead of using one thread to scan the
 * entire filesystem, we instead can have multiple threads walking individual
 * subdirectories upwards to the root.  In a perfect world, the IOLOCK would
 * suffice to stabilize two directories in a parent -> child relationship.
 * Unfortunately, the VFS does not take the IOLOCK when moving a child
 * subdirectory, so we instead synchronize on ILOCK and use dirent update hooks
 * to detect a race.  If a race occurs in a path, we restart the scan.
 *
 * If the walk terminates without reaching the root, we know the path is
 * disconnected and ought to be attached to the lost and found.  If on the walk
 * we find the same subdir that we're scanning, we know this is a cycle and
 * should delete an incoming edge.  If we find multiple paths to the root, we
 * know to delete an incoming edge.
 *
 * There are two big hitches with this approach: first, all file link counts
 * must be correct to prevent other writers from doing the wrong thing with the
 * directory tree structure.  Second, because we're walking upwards in a tree
 * of arbitrary depth, we cannot hold all the ILOCKs.  Instead, we will use a
 * directory update hook to invalidate the scan results if one of the paths
 * we've scanned has changed.
 */

/* Clean up the dirtree checking resources. */
STATIC void
xchk_dirtree_buf_cleanup(
	void			*buf)
{
	struct xchk_dirtree	*dl = buf;
	struct xchk_dirpath	*path, *n;

	if (dl->scan_ino != NULLFSINO)
		xfs_dir_hook_del(dl->sc->mp, &dl->dhook);

	xchk_dirtree_for_each_path_safe(dl, path, n) {
		list_del_init(&path->list);
		xino_bitmap_destroy(&path->seen_inodes);
		kfree(path);
	}

	xfblob_destroy(dl->path_names);
	xfarray_destroy(dl->path_steps);
	mutex_destroy(&dl->lock);
}

/* Set us up to look for directory loops. */
int
xchk_setup_dirtree(
	struct xfs_scrub	*sc)
{
	struct xchk_dirtree	*dl;
	char			*descr;
	int			error;

	xchk_fsgates_enable(sc, XCHK_FSGATES_DIRENTS);

	if (xchk_could_repair(sc)) {
		error = xrep_setup_dirtree(sc);
		if (error)
			return error;
	}

	dl = kvzalloc(sizeof(struct xchk_dirtree), XCHK_GFP_FLAGS);
	if (!dl)
		return -ENOMEM;
	dl->sc = sc;
	dl->xname.name = dl->namebuf;
	dl->hook_xname.name = dl->hook_namebuf;
	INIT_LIST_HEAD(&dl->path_list);
	dl->root_ino = NULLFSINO;
	dl->scan_ino = NULLFSINO;
	dl->parent_ino = NULLFSINO;

	mutex_init(&dl->lock);

	descr = xchk_xfile_ino_descr(sc, "dirtree path steps");
	error = xfarray_create(descr, 0, sizeof(struct xchk_dirpath_step),
			&dl->path_steps);
	kfree(descr);
	if (error)
		goto out_dl;

	descr = xchk_xfile_ino_descr(sc, "dirtree path names");
	error = xfblob_create(descr, &dl->path_names);
	kfree(descr);
	if (error)
		goto out_steps;

	error = xchk_setup_inode_contents(sc, 0);
	if (error)
		goto out_names;

	sc->buf = dl;
	sc->buf_cleanup = xchk_dirtree_buf_cleanup;
	return 0;

out_names:
	xfblob_destroy(dl->path_names);
out_steps:
	xfarray_destroy(dl->path_steps);
out_dl:
	mutex_destroy(&dl->lock);
	kvfree(dl);
	return error;
}

/*
 * Add the parent pointer described by @dl->pptr to the given path as a new
 * step.  Returns -ELNRNG if the path is too deep.
 */
int
xchk_dirpath_append(
	struct xchk_dirtree		*dl,
	struct xfs_inode		*ip,
	struct xchk_dirpath		*path,
	const struct xfs_name		*name,
	const struct xfs_parent_rec	*pptr)
{
	struct xchk_dirpath_step	step = {
		.pptr_rec		= *pptr, /* struct copy */
		.name_len		= name->len,
	};
	int				error;

	/*
	 * If this path is more than 2 billion steps long, this directory tree
	 * is too far gone to fix.
	 */
	if (path->nr_steps >= XFS_MAXLINK)
		return -ELNRNG;

	error = xfblob_storename(dl->path_names, &step.name_cookie, name);
	if (error)
		return error;

	error = xino_bitmap_set(&path->seen_inodes, ip->i_ino);
	if (error)
		return error;

	error = xfarray_append(dl->path_steps, &step);
	if (error)
		return error;

	path->nr_steps++;
	return 0;
}

/*
 * Create an xchk_path for each parent pointer of the directory that we're
 * scanning.  For each path created, we will eventually try to walk towards the
 * root with the goal of deleting all parents except for one that leads to the
 * root.
 *
 * Returns -EFSCORRUPTED to signal that the inode being scanned has a corrupt
 * parent pointer and hence there's no point in continuing; or -ENOSR if there
 * are too many parent pointers for this directory.
 */
STATIC int
xchk_dirtree_create_path(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	unsigned int			attr_flags,
	const unsigned char		*name,
	unsigned int			namelen,
	const void			*value,
	unsigned int			valuelen,
	void				*priv)
{
	struct xfs_name			xname = {
		.name			= name,
		.len			= namelen,
	};
	struct xchk_dirtree		*dl = priv;
	struct xchk_dirpath		*path;
	const struct xfs_parent_rec	*rec = value;
	int				error;

	if (!(attr_flags & XFS_ATTR_PARENT))
		return 0;

	error = xfs_parent_from_attr(sc->mp, attr_flags, name, namelen, value,
			valuelen, NULL, NULL);
	if (error)
		return error;

	/*
	 * If there are more than 2 billion actual parent pointers for this
	 * subdirectory, this fs is too far gone to fix.
	 */
	if (dl->nr_paths >= XFS_MAXLINK)
		return -ENOSR;

	trace_xchk_dirtree_create_path(sc, ip, dl->nr_paths, &xname, rec);

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
	path->outcome = XCHK_DIRPATH_SCANNING;

	error = xchk_dirpath_append(dl, sc->ip, path, &xname, rec);
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
 * Validate that the first step of this path still has a corresponding
 * parent pointer in @sc->ip.  We probably dropped @sc->ip's ILOCK while
 * walking towards the roots, which is why this is necessary.
 *
 * This function has a side effect of loading the first parent pointer of this
 * path into the parent pointer scratch pad.  This prepares us to walk up the
 * directory tree towards the root.  Returns -ESTALE if the scan data is now
 * out of date.
 */
STATIC int
xchk_dirpath_revalidate(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path)
{
	struct xfs_scrub		*sc = dl->sc;
	int				error;

	/*
	 * Look up the parent pointer that corresponds to the start of this
	 * path.  If the parent pointer has disappeared on us, dump all the
	 * scan results and try again.
	 */
	error = xfs_parent_lookup(sc->tp, sc->ip, &dl->xname, &dl->pptr_rec,
			&dl->pptr_args);
	if (error == -ENOATTR) {
		trace_xchk_dirpath_disappeared(dl->sc, sc->ip, path->path_nr,
				path->first_step, &dl->xname, &dl->pptr_rec);
		dl->stale = true;
		return -ESTALE;
	}

	return error;
}

/*
 * Walk the parent pointers of a directory at the end of a path and record
 * the parent that we find in @dl->xname/pptr_rec.
 */
STATIC int
xchk_dirpath_find_next_step(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	unsigned int			attr_flags,
	const unsigned char		*name,
	unsigned int			namelen,
	const void			*value,
	unsigned int			valuelen,
	void				*priv)
{
	struct xchk_dirtree		*dl = priv;
	const struct xfs_parent_rec	*rec = value;
	int				error;

	if (!(attr_flags & XFS_ATTR_PARENT))
		return 0;

	error = xfs_parent_from_attr(sc->mp, attr_flags, name, namelen, value,
			valuelen, NULL, NULL);
	if (error)
		return error;

	/*
	 * If we've already set @dl->pptr_rec, then this directory has multiple
	 * parents.  Signal this back to the caller via -EMLINK.
	 */
	if (dl->parents_found > 0)
		return -EMLINK;

	dl->parents_found++;
	memcpy(dl->namebuf, name, namelen);
	dl->xname.len = namelen;
	dl->pptr_rec = *rec; /* struct copy */
	return 0;
}

/* Set and log the outcome of a path walk. */
static inline void
xchk_dirpath_set_outcome(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path,
	enum xchk_dirpath_outcome	outcome)
{
	trace_xchk_dirpath_set_outcome(dl->sc, path->path_nr, path->nr_steps,
			outcome);

	path->outcome = outcome;
}

/*
 * Scan the directory at the end of this path for its parent directory link.
 * If we find one, extend the path.  Returns -ESTALE if the scan data out of
 * date.  Returns -EFSCORRUPTED if the parent pointer is bad; or -ELNRNG if
 * the path got too deep.
 */
STATIC int
xchk_dirpath_step_up(
	struct xchk_dirtree	*dl,
	struct xchk_dirpath	*path,
	bool			is_metadir)
{
	struct xfs_scrub	*sc = dl->sc;
	struct xfs_inode	*dp;
	xfs_ino_t		parent_ino = be64_to_cpu(dl->pptr_rec.p_ino);
	unsigned int		lock_mode;
	int			error;

	/* Grab and lock the parent directory. */
	error = xchk_iget(sc, parent_ino, &dp);
	if (error)
		return error;

	lock_mode = xfs_ilock_attr_map_shared(dp);
	mutex_lock(&dl->lock);

	if (dl->stale) {
		error = -ESTALE;
		goto out_scanlock;
	}

	/* We've reached the root directory; the path is ok. */
	if (parent_ino == dl->root_ino) {
		xchk_dirpath_set_outcome(dl, path, XCHK_DIRPATH_OK);
		error = 0;
		goto out_scanlock;
	}

	/*
	 * The inode being scanned is its own distant ancestor!  Get rid of
	 * this path.
	 */
	if (parent_ino == sc->ip->i_ino) {
		xchk_dirpath_set_outcome(dl, path, XCHK_DIRPATH_DELETE);
		error = 0;
		goto out_scanlock;
	}

	/*
	 * We've seen this inode before during the path walk.  There's a loop
	 * above us in the directory tree.  This probably means that we cannot
	 * continue, but let's keep walking paths to get a full picture.
	 */
	if (xino_bitmap_test(&path->seen_inodes, parent_ino)) {
		xchk_dirpath_set_outcome(dl, path, XCHK_DIRPATH_LOOP);
		error = 0;
		goto out_scanlock;
	}

	/* The handle encoded in the parent pointer must match. */
	if (VFS_I(dp)->i_generation != be32_to_cpu(dl->pptr_rec.p_gen)) {
		trace_xchk_dirpath_badgen(dl->sc, dp, path->path_nr,
				path->nr_steps, &dl->xname, &dl->pptr_rec);
		error = -EFSCORRUPTED;
		goto out_scanlock;
	}

	/* Parent pointer must point up to a directory. */
	if (!S_ISDIR(VFS_I(dp)->i_mode)) {
		trace_xchk_dirpath_nondir_parent(dl->sc, dp, path->path_nr,
				path->nr_steps, &dl->xname, &dl->pptr_rec);
		error = -EFSCORRUPTED;
		goto out_scanlock;
	}

	/* Parent cannot be an unlinked directory. */
	if (VFS_I(dp)->i_nlink == 0) {
		trace_xchk_dirpath_unlinked_parent(dl->sc, dp, path->path_nr,
				path->nr_steps, &dl->xname, &dl->pptr_rec);
		error = -EFSCORRUPTED;
		goto out_scanlock;
	}

	/* Parent must be in the same directory tree. */
	if (is_metadir != xfs_is_metadir_inode(dp)) {
		trace_xchk_dirpath_crosses_tree(dl->sc, dp, path->path_nr,
				path->nr_steps, &dl->xname, &dl->pptr_rec);
		error = -EFSCORRUPTED;
		goto out_scanlock;
	}

	/*
	 * If the extended attributes look as though they has been zapped by
	 * the inode record repair code, we cannot scan for parent pointers.
	 */
	if (xchk_pptr_looks_zapped(dp)) {
		error = -EBUSY;
		xchk_set_incomplete(sc);
		goto out_scanlock;
	}

	/*
	 * Walk the parent pointers of @dp to find the parent of this directory
	 * to find the next step in our walk.  If we find that @dp has exactly
	 * one parent, the parent pointer information will be stored in
	 * @dl->pptr_rec.  This prepares us for the next step of the walk.
	 */
	mutex_unlock(&dl->lock);
	dl->parents_found = 0;
	error = xchk_xattr_walk(sc, dp, xchk_dirpath_find_next_step, NULL, dl);
	mutex_lock(&dl->lock);
	if (error == -EFSCORRUPTED || error == -EMLINK ||
	    (!error && dl->parents_found == 0)) {
		/*
		 * Further up the directory tree from @sc->ip, we found a
		 * corrupt parent pointer, multiple parent pointers while
		 * finding this directory's parent, or zero parents despite
		 * having a nonzero link count.  Keep looking for other paths.
		 */
		xchk_dirpath_set_outcome(dl, path, XCHK_DIRPATH_CORRUPT);
		error = 0;
		goto out_scanlock;
	}
	if (error)
		goto out_scanlock;

	if (dl->stale) {
		error = -ESTALE;
		goto out_scanlock;
	}

	trace_xchk_dirpath_found_next_step(sc, dp, path->path_nr,
			path->nr_steps, &dl->xname, &dl->pptr_rec);

	/* Append to the path steps */
	error = xchk_dirpath_append(dl, dp, path, &dl->xname, &dl->pptr_rec);
	if (error)
		goto out_scanlock;

	if (path->second_step == XFARRAY_NULLIDX)
		path->second_step = xfarray_length(dl->path_steps) - 1;

out_scanlock:
	mutex_unlock(&dl->lock);
	xfs_iunlock(dp, lock_mode);
	xchk_irele(sc, dp);
	return error;
}

/*
 * Walk the directory tree upwards towards what is hopefully the root
 * directory, recording path steps as we go.  The current path components are
 * stored in dl->pptr_rec and dl->xname.
 *
 * Returns -ESTALE if the scan data are out of date.  Returns -EFSCORRUPTED
 * only if the direct parent pointer of @sc->ip associated with this path is
 * corrupt.
 */
STATIC int
xchk_dirpath_walk_upwards(
	struct xchk_dirtree	*dl,
	struct xchk_dirpath	*path)
{
	struct xfs_scrub	*sc = dl->sc;
	bool			is_metadir;
	int			error;

	ASSERT(sc->ilock_flags & XFS_ILOCK_EXCL);

	/* Reload the start of this path and make sure it's still there. */
	error = xchk_dirpath_revalidate(dl, path);
	if (error)
		return error;

	trace_xchk_dirpath_walk_upwards(sc, sc->ip, path->path_nr, &dl->xname,
			&dl->pptr_rec);

	/*
	 * The inode being scanned is its own direct ancestor!
	 * Get rid of this path.
	 */
	if (be64_to_cpu(dl->pptr_rec.p_ino) == sc->ip->i_ino) {
		xchk_dirpath_set_outcome(dl, path, XCHK_DIRPATH_DELETE);
		return 0;
	}

	/*
	 * Drop ILOCK_EXCL on the inode being scanned.  We still hold
	 * IOLOCK_EXCL on it, so it cannot move around or be renamed.
	 *
	 * Beyond this point we're walking up the directory tree, which means
	 * that we can acquire and drop the ILOCK on an alias of sc->ip.  The
	 * ILOCK state is no longer tracked in the scrub context.  Hence we
	 * must drop @sc->ip's ILOCK during the walk.
	 */
	is_metadir = xfs_is_metadir_inode(sc->ip);
	mutex_unlock(&dl->lock);
	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/*
	 * Take the first step in the walk towards the root by checking the
	 * start of this path, which is a direct parent pointer of @sc->ip.
	 * If we see any kind of error here (including corruptions), the parent
	 * pointer of @sc->ip is corrupt.  Stop the whole scan.
	 */
	error = xchk_dirpath_step_up(dl, path, is_metadir);
	if (error) {
		xchk_ilock(sc, XFS_ILOCK_EXCL);
		mutex_lock(&dl->lock);
		return error;
	}

	/*
	 * Take steps upward from the second step in this path towards the
	 * root.  If we hit corruption errors here, there's a problem
	 * *somewhere* in the path, but we don't need to stop scanning.
	 */
	while (!error && path->outcome == XCHK_DIRPATH_SCANNING)
		error = xchk_dirpath_step_up(dl, path, is_metadir);

	/* Retake the locks we had, mark paths, etc. */
	xchk_ilock(sc, XFS_ILOCK_EXCL);
	mutex_lock(&dl->lock);
	if (error == -EFSCORRUPTED) {
		xchk_dirpath_set_outcome(dl, path, XCHK_DIRPATH_CORRUPT);
		error = 0;
	}
	if (!error && dl->stale)
		return -ESTALE;
	return error;
}

/*
 * Decide if this path step has been touched by this live update.  Returns
 * 1 for yes, 0 for no, or a negative errno.
 */
STATIC int
xchk_dirpath_step_is_stale(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path,
	unsigned int			step_nr,
	xfarray_idx_t			step_idx,
	struct xfs_dir_update_params	*p,
	xfs_ino_t			*cursor)
{
	struct xchk_dirpath_step	step;
	xfs_ino_t			child_ino = *cursor;
	int				error;

	error = xfarray_load(dl->path_steps, step_idx, &step);
	if (error)
		return error;
	*cursor = be64_to_cpu(step.pptr_rec.p_ino);

	/*
	 * If the parent and child being updated are not the ones mentioned in
	 * this path step, the scan data is still ok.
	 */
	if (p->ip->i_ino != child_ino || p->dp->i_ino != *cursor)
		return 0;

	/*
	 * If the dirent name lengths or byte sequences are different, the scan
	 * data is still ok.
	 */
	if (p->name->len != step.name_len)
		return 0;

	error = xfblob_loadname(dl->path_names, step.name_cookie,
			&dl->hook_xname, step.name_len);
	if (error)
		return error;

	if (memcmp(dl->hook_xname.name, p->name->name, p->name->len) != 0)
		return 0;

	/*
	 * If the update comes from the repair code itself, walk the state
	 * machine forward.
	 */
	if (p->ip->i_ino == dl->scan_ino &&
	    path->outcome == XREP_DIRPATH_ADOPTING) {
		xchk_dirpath_set_outcome(dl, path, XREP_DIRPATH_ADOPTED);
		return 0;
	}

	if (p->ip->i_ino == dl->scan_ino &&
	    path->outcome == XREP_DIRPATH_DELETING) {
		xchk_dirpath_set_outcome(dl, path, XREP_DIRPATH_DELETED);
		return 0;
	}

	/* Exact match, scan data is out of date. */
	trace_xchk_dirpath_changed(dl->sc, path->path_nr, step_nr, p->dp,
			p->ip, p->name);
	return 1;
}

/*
 * Decide if this path has been touched by this live update.  Returns 1 for
 * yes, 0 for no, or a negative errno.
 */
STATIC int
xchk_dirpath_is_stale(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path,
	struct xfs_dir_update_params	*p)
{
	xfs_ino_t			cursor = dl->scan_ino;
	xfarray_idx_t			idx = path->first_step;
	unsigned int			i;
	int				ret;

	/*
	 * The child being updated has not been seen by this path at all; this
	 * path cannot be stale.
	 */
	if (!xino_bitmap_test(&path->seen_inodes, p->ip->i_ino))
		return 0;

	ret = xchk_dirpath_step_is_stale(dl, path, 0, idx, p, &cursor);
	if (ret != 0)
		return ret;

	for (i = 1, idx = path->second_step; i < path->nr_steps; i++, idx++) {
		ret = xchk_dirpath_step_is_stale(dl, path, i, idx, p, &cursor);
		if (ret != 0)
			return ret;
	}

	return 0;
}

/*
 * Decide if a directory update from the regular filesystem touches any of the
 * paths we've scanned, and invalidate the scan data if true.
 */
STATIC int
xchk_dirtree_live_update(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_dir_update_params	*p = data;
	struct xchk_dirtree		*dl;
	struct xchk_dirpath		*path;
	int				ret;

	dl = container_of(nb, struct xchk_dirtree, dhook.dirent_hook.nb);

	trace_xchk_dirtree_live_update(dl->sc, p->dp, action, p->ip, p->delta,
			p->name);

	mutex_lock(&dl->lock);

	if (dl->stale || dl->aborted)
		goto out_unlock;

	xchk_dirtree_for_each_path(dl, path) {
		ret = xchk_dirpath_is_stale(dl, path, p);
		if (ret < 0) {
			dl->aborted = true;
			break;
		}
		if (ret == 1) {
			dl->stale = true;
			break;
		}
	}

out_unlock:
	mutex_unlock(&dl->lock);
	return NOTIFY_DONE;
}

/* Delete all the collected path information. */
STATIC void
xchk_dirtree_reset(
	void			*buf)
{
	struct xchk_dirtree	*dl = buf;
	struct xchk_dirpath	*path, *n;

	ASSERT(dl->sc->ilock_flags & XFS_ILOCK_EXCL);

	xchk_dirtree_for_each_path_safe(dl, path, n) {
		list_del_init(&path->list);
		xino_bitmap_destroy(&path->seen_inodes);
		kfree(path);
	}
	dl->nr_paths = 0;

	xfarray_truncate(dl->path_steps);
	xfblob_truncate(dl->path_names);

	dl->stale = false;
}

/*
 * Load the name/pptr from the first step in this path into @dl->pptr_rec and
 * @dl->xname.
 */
STATIC int
xchk_dirtree_load_path(
	struct xchk_dirtree		*dl,
	struct xchk_dirpath		*path)
{
	struct xchk_dirpath_step	step;
	int				error;

	error = xfarray_load(dl->path_steps, path->first_step, &step);
	if (error)
		return error;

	error = xfblob_loadname(dl->path_names, step.name_cookie, &dl->xname,
			step.name_len);
	if (error)
		return error;

	dl->pptr_rec = step.pptr_rec; /* struct copy */
	return 0;
}

/*
 * For each parent pointer of this subdir, trace a path upwards towards the
 * root directory and record what we find.  Returns 0 for success;
 * -EFSCORRUPTED if walking the parent pointers of @sc->ip failed, -ELNRNG if a
 * path was too deep; -ENOSR if there were too many parent pointers; or
 * a negative errno.
 */
int
xchk_dirtree_find_paths_to_root(
	struct xchk_dirtree	*dl)
{
	struct xfs_scrub	*sc = dl->sc;
	struct xchk_dirpath	*path;
	int			error = 0;

	do {
		if (xchk_should_terminate(sc, &error))
			return error;

		xchk_dirtree_reset(dl);

		/*
		 * If the extended attributes look as though they has been
		 * zapped by the inode record repair code, we cannot scan for
		 * parent pointers.
		 */
		if (xchk_pptr_looks_zapped(sc->ip)) {
			xchk_set_incomplete(sc);
			return -EBUSY;
		}

		/*
		 * Create path walk contexts for each parent of the directory
		 * that is being scanned.  Directories are supposed to have
		 * only one parent, but this is how we detect multiple parents.
		 */
		error = xchk_xattr_walk(sc, sc->ip, xchk_dirtree_create_path,
				NULL, dl);
		if (error)
			return error;

		xchk_dirtree_for_each_path(dl, path) {
			/* Load path components into dl->pptr/xname */
			error = xchk_dirtree_load_path(dl, path);
			if (error)
				return error;

			/*
			 * Try to walk up each path to the root.  This enables
			 * us to find directory loops in ancestors, and the
			 * like.
			 */
			error = xchk_dirpath_walk_upwards(dl, path);
			if (error == -EFSCORRUPTED) {
				/*
				 * A parent pointer of @sc->ip is bad, don't
				 * bother continuing.
				 */
				break;
			}
			if (error == -ESTALE) {
				/* This had better be an invalidation. */
				ASSERT(dl->stale);
				break;
			}
			if (error)
				return error;
			if (dl->aborted)
				return 0;
		}
	} while (dl->stale);

	return error;
}

/*
 * Figure out what to do with the paths we tried to find.  Do not call this
 * if the scan results are stale.
 */
void
xchk_dirtree_evaluate(
	struct xchk_dirtree		*dl,
	struct xchk_dirtree_outcomes	*oc)
{
	struct xchk_dirpath		*path;

	ASSERT(!dl->stale);

	/* Scan the paths we have to decide what to do. */
	memset(oc, 0, sizeof(struct xchk_dirtree_outcomes));
	xchk_dirtree_for_each_path(dl, path) {
		trace_xchk_dirpath_evaluate_path(dl->sc, path->path_nr,
				path->nr_steps, path->outcome);

		switch (path->outcome) {
		case XCHK_DIRPATH_SCANNING:
			/* shouldn't get here */
			ASSERT(0);
			break;
		case XCHK_DIRPATH_DELETE:
			/* This one is already going away. */
			oc->bad++;
			break;
		case XCHK_DIRPATH_CORRUPT:
		case XCHK_DIRPATH_LOOP:
			/* Couldn't find the end of this path. */
			oc->suspect++;
			break;
		case XCHK_DIRPATH_STALE:
			/* shouldn't get here either */
			ASSERT(0);
			break;
		case XCHK_DIRPATH_OK:
			/* This path got all the way to the root. */
			oc->good++;
			break;
		case XREP_DIRPATH_DELETING:
		case XREP_DIRPATH_DELETED:
		case XREP_DIRPATH_ADOPTING:
		case XREP_DIRPATH_ADOPTED:
			/* These should not be in progress! */
			ASSERT(0);
			break;
		}
	}

	trace_xchk_dirtree_evaluate(dl, oc);
}

/* Look for directory loops. */
int
xchk_dirtree(
	struct xfs_scrub		*sc)
{
	struct xchk_dirtree_outcomes	oc;
	struct xchk_dirtree		*dl = sc->buf;
	int				error;

	/*
	 * Nondirectories do not point downwards to other files, so they cannot
	 * cause a cycle in the directory tree.
	 */
	if (!S_ISDIR(VFS_I(sc->ip)->i_mode))
		return -ENOENT;

	ASSERT(xfs_has_parent(sc->mp));

	/*
	 * Find the root of the directory tree.  Remember which directory to
	 * scan, because the hook doesn't detach until after sc->ip gets
	 * released during teardown.
	 */
	dl->root_ino = xchk_inode_rootdir_inum(sc->ip);
	dl->scan_ino = sc->ip->i_ino;

	trace_xchk_dirtree_start(sc->ip, sc->sm, 0);

	/*
	 * Hook into the directory entry code so that we can capture updates to
	 * paths that we have already scanned.  The scanner thread takes each
	 * directory's ILOCK, which means that any in-progress directory update
	 * will finish before we can scan the directory.
	 */
	ASSERT(sc->flags & XCHK_FSGATES_DIRENTS);
	xfs_dir_hook_setup(&dl->dhook, xchk_dirtree_live_update);
	error = xfs_dir_hook_add(sc->mp, &dl->dhook);
	if (error)
		goto out;

	mutex_lock(&dl->lock);

	/* Trace each parent pointer's path to the root. */
	error = xchk_dirtree_find_paths_to_root(dl);
	if (error == -EFSCORRUPTED || error == -ELNRNG || error == -ENOSR) {
		/*
		 * Don't bother walking the paths if the xattr structure or the
		 * parent pointers are corrupt; this scan cannot be completed
		 * without full information.
		 */
		xchk_ino_xref_set_corrupt(sc, sc->ip->i_ino);
		error = 0;
		goto out_scanlock;
	}
	if (error == -EBUSY) {
		/*
		 * We couldn't scan some directory's parent pointers because
		 * the attr fork looked like it had been zapped.  The
		 * scan was marked incomplete, so no further error code
		 * is necessary.
		 */
		error = 0;
		goto out_scanlock;
	}
	if (error)
		goto out_scanlock;
	if (dl->aborted) {
		xchk_set_incomplete(sc);
		goto out_scanlock;
	}

	/* Assess what we found in our path evaluation. */
	xchk_dirtree_evaluate(dl, &oc);
	if (xchk_dirtree_parentless(dl)) {
		if (oc.good || oc.bad || oc.suspect)
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
	} else {
		if (oc.bad || oc.good + oc.suspect != 1)
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		if (oc.suspect)
			xchk_ino_xref_set_corrupt(sc, sc->ip->i_ino);
	}

out_scanlock:
	mutex_unlock(&dl->lock);
out:
	trace_xchk_dirtree_done(sc->ip, sc->sm, error);
	return error;
}

/* Does the directory targetted by this scrub have no parents? */
bool
xchk_dirtree_parentless(const struct xchk_dirtree *dl)
{
	struct xfs_scrub	*sc = dl->sc;

	if (xchk_inode_is_dirtree_root(sc->ip))
		return true;
	if (VFS_I(sc->ip)->i_nlink == 0)
		return true;
	return false;
}
