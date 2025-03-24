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
#include "xfs_parent.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/iscan.h"
#include "scrub/findparent.h"
#include "scrub/readdir.h"
#include "scrub/tempfile.h"
#include "scrub/listxattr.h"

/*
 * Finding the Parent of a Directory
 * =================================
 *
 * Directories have parent pointers, in the sense that each directory contains
 * a dotdot entry that points to the single allowed parent.  The brute force
 * way to find the parent of a given directory is to scan every directory in
 * the filesystem looking for a child dirent that references this directory.
 *
 * This module wraps the process of scanning the directory tree.  It requires
 * that @sc->ip is the directory whose parent we want to find, and that the
 * caller hold only the IOLOCK on that directory.  The scan itself needs to
 * take the ILOCK of each directory visited.
 *
 * Because we cannot hold @sc->ip's ILOCK during a scan of the whole fs, it is
 * necessary to use dirent hook to update the parent scan results.  Callers
 * must not read the scan results without re-taking @sc->ip's ILOCK.
 *
 * There are a few shortcuts that we can take to avoid scanning the entire
 * filesystem, such as noticing directory tree roots and querying the dentry
 * cache for parent information.
 */

struct xrep_findparent_info {
	/* The directory currently being scanned. */
	struct xfs_inode	*dp;

	/*
	 * Scrub context.  We're looking for a @dp containing a directory
	 * entry pointing to sc->ip->i_ino.
	 */
	struct xfs_scrub	*sc;

	/* Optional scan information for a xrep_findparent_scan call. */
	struct xrep_parent_scan_info *parent_scan;

	/*
	 * Parent that we've found for sc->ip.  If we're scanning the entire
	 * directory tree, we need this to ensure that we only find /one/
	 * parent directory.
	 */
	xfs_ino_t		found_parent;

	/*
	 * This is set to true if @found_parent was not observed directly from
	 * the directory scan but by noticing a change in dotdot entries after
	 * cycling the sc->ip IOLOCK.
	 */
	bool			parent_tentative;
};

/*
 * If this directory entry points to the scrub target inode, then the directory
 * we're scanning is the parent of the scrub target inode.
 */
STATIC int
xrep_findparent_dirent(
	struct xfs_scrub		*sc,
	struct xfs_inode		*dp,
	xfs_dir2_dataptr_t		dapos,
	const struct xfs_name		*name,
	xfs_ino_t			ino,
	void				*priv)
{
	struct xrep_findparent_info	*fpi = priv;
	int				error = 0;

	if (xchk_should_terminate(fpi->sc, &error))
		return error;

	if (ino != fpi->sc->ip->i_ino)
		return 0;

	/* Ignore garbage directory entry names. */
	if (name->len == 0 || !xfs_dir2_namecheck(name->name, name->len))
		return -EFSCORRUPTED;

	/*
	 * Ignore dotdot and dot entries -- we're looking for parent -> child
	 * links only.
	 */
	if (name->name[0] == '.' && (name->len == 1 ||
				     (name->len == 2 && name->name[1] == '.')))
		return 0;

	/* Uhoh, more than one parent for a dir? */
	if (fpi->found_parent != NULLFSINO &&
	    !(fpi->parent_tentative && fpi->found_parent == fpi->dp->i_ino)) {
		trace_xrep_findparent_dirent(fpi->sc->ip, 0);
		return -EFSCORRUPTED;
	}

	/* We found a potential parent; remember this. */
	trace_xrep_findparent_dirent(fpi->sc->ip, fpi->dp->i_ino);
	fpi->found_parent = fpi->dp->i_ino;
	fpi->parent_tentative = false;

	if (fpi->parent_scan)
		xrep_findparent_scan_found(fpi->parent_scan, fpi->dp->i_ino);

	return 0;
}

/*
 * If this is a directory, walk the dirents looking for any that point to the
 * scrub target inode.
 */
STATIC int
xrep_findparent_walk_directory(
	struct xrep_findparent_info	*fpi)
{
	struct xfs_scrub		*sc = fpi->sc;
	struct xfs_inode		*dp = fpi->dp;
	unsigned int			lock_mode;
	int				error = 0;

	/*
	 * The inode being scanned cannot be its own parent, nor can any
	 * temporary directory we created to stage this repair.
	 */
	if (dp == sc->ip || dp == sc->tempip)
		return 0;

	/*
	 * Similarly, temporary files created to stage a repair cannot be the
	 * parent of this inode.
	 */
	if (xrep_is_tempfile(dp))
		return 0;

	/*
	 * Scan the directory to see if there it contains an entry pointing to
	 * the directory that we are repairing.
	 */
	lock_mode = xfs_ilock_data_map_shared(dp);

	/* Don't mix metadata and regular directory trees. */
	if (xfs_is_metadir_inode(dp) != xfs_is_metadir_inode(sc->ip))
		goto out_unlock;

	/*
	 * If this directory is known to be sick, we cannot scan it reliably
	 * and must abort.
	 */
	if (xfs_inode_has_sickness(dp, XFS_SICK_INO_CORE |
				       XFS_SICK_INO_BMBTD |
				       XFS_SICK_INO_DIR)) {
		error = -EFSCORRUPTED;
		goto out_unlock;
	}

	/*
	 * We cannot complete our parent pointer scan if a directory looks as
	 * though it has been zapped by the inode record repair code.
	 */
	if (xchk_dir_looks_zapped(dp)) {
		error = -EBUSY;
		goto out_unlock;
	}

	error = xchk_dir_walk(sc, dp, xrep_findparent_dirent, fpi);
	if (error)
		goto out_unlock;

out_unlock:
	xfs_iunlock(dp, lock_mode);
	return error;
}

/*
 * Update this directory's dotdot pointer based on ongoing dirent updates.
 */
STATIC int
xrep_findparent_live_update(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_dir_update_params	*p = data;
	struct xrep_parent_scan_info	*pscan;
	struct xfs_scrub		*sc;

	pscan = container_of(nb, struct xrep_parent_scan_info,
			dhook.dirent_hook.nb);
	sc = pscan->sc;

	/*
	 * If @p->ip is the subdirectory that we're interested in and we've
	 * already scanned @p->dp, update the dotdot target inumber to the
	 * parent inode.
	 */
	if (p->ip->i_ino == sc->ip->i_ino &&
	    xchk_iscan_want_live_update(&pscan->iscan, p->dp->i_ino)) {
		if (p->delta > 0) {
			xrep_findparent_scan_found(pscan, p->dp->i_ino);
		} else {
			xrep_findparent_scan_found(pscan, NULLFSINO);
		}
	}

	return NOTIFY_DONE;
}

/*
 * Set up a scan to find the parent of a directory.  The provided dirent hook
 * will be called when there is a dotdot update for the inode being repaired.
 */
int
__xrep_findparent_scan_start(
	struct xfs_scrub		*sc,
	struct xrep_parent_scan_info	*pscan,
	notifier_fn_t			custom_fn)
{
	int				error;

	if (!(sc->flags & XCHK_FSGATES_DIRENTS)) {
		ASSERT(sc->flags & XCHK_FSGATES_DIRENTS);
		return -EINVAL;
	}

	pscan->sc = sc;
	pscan->parent_ino = NULLFSINO;

	mutex_init(&pscan->lock);

	xchk_iscan_start(sc, 30000, 100, &pscan->iscan);

	/*
	 * Hook into the dirent update code.  The hook only operates on inodes
	 * that were already scanned, and the scanner thread takes each inode's
	 * ILOCK, which means that any in-progress inode updates will finish
	 * before we can scan the inode.
	 */
	if (custom_fn)
		xfs_dir_hook_setup(&pscan->dhook, custom_fn);
	else
		xfs_dir_hook_setup(&pscan->dhook, xrep_findparent_live_update);
	error = xfs_dir_hook_add(sc->mp, &pscan->dhook);
	if (error)
		goto out_iscan;

	return 0;
out_iscan:
	xchk_iscan_teardown(&pscan->iscan);
	mutex_destroy(&pscan->lock);
	return error;
}

/*
 * Scan the entire filesystem looking for a parent inode for the inode being
 * scrubbed.  @sc->ip must not be the root of a directory tree.  Callers must
 * not hold a dirty transaction or any lock that would interfere with taking
 * an ILOCK.
 *
 * Returns 0 with @pscan->parent_ino set to the parent that we found.
 * Returns 0 with @pscan->parent_ino set to NULLFSINO if we found no parents.
 * Returns the usual negative errno if something else happened.
 */
int
xrep_findparent_scan(
	struct xrep_parent_scan_info	*pscan)
{
	struct xrep_findparent_info	fpi = {
		.sc			= pscan->sc,
		.found_parent		= NULLFSINO,
		.parent_scan		= pscan,
	};
	struct xfs_scrub		*sc = pscan->sc;
	int				ret;

	ASSERT(S_ISDIR(VFS_IC(sc->ip)->i_mode));

	while ((ret = xchk_iscan_iter(&pscan->iscan, &fpi.dp)) == 1) {
		if (S_ISDIR(VFS_I(fpi.dp)->i_mode))
			ret = xrep_findparent_walk_directory(&fpi);
		else
			ret = 0;
		xchk_iscan_mark_visited(&pscan->iscan, fpi.dp);
		xchk_irele(sc, fpi.dp);
		if (ret)
			break;

		if (xchk_should_terminate(sc, &ret))
			break;
	}
	xchk_iscan_iter_finish(&pscan->iscan);

	return ret;
}

/* Tear down a parent scan. */
void
xrep_findparent_scan_teardown(
	struct xrep_parent_scan_info	*pscan)
{
	xfs_dir_hook_del(pscan->sc->mp, &pscan->dhook);
	xchk_iscan_teardown(&pscan->iscan);
	mutex_destroy(&pscan->lock);
}

/* Finish a parent scan early. */
void
xrep_findparent_scan_finish_early(
	struct xrep_parent_scan_info	*pscan,
	xfs_ino_t			ino)
{
	xrep_findparent_scan_found(pscan, ino);
	xchk_iscan_finish_early(&pscan->iscan);
}

/*
 * Confirm that the directory @parent_ino actually contains a directory entry
 * pointing to the child @sc->ip->ino.  This function returns one of several
 * ways:
 *
 * Returns 0 with @parent_ino unchanged if the parent was confirmed.
 * Returns 0 with @parent_ino set to NULLFSINO if the parent was not valid.
 * Returns the usual negative errno if something else happened.
 */
int
xrep_findparent_confirm(
	struct xfs_scrub	*sc,
	xfs_ino_t		*parent_ino)
{
	struct xrep_findparent_info fpi = {
		.sc		= sc,
		.found_parent	= NULLFSINO,
	};
	int			error;

	/* The root directory always points to itself. */
	if (sc->ip == sc->mp->m_rootip) {
		*parent_ino = sc->mp->m_sb.sb_rootino;
		return 0;
	}

	/* The metadata root directory always points to itself. */
	if (sc->ip == sc->mp->m_metadirip) {
		*parent_ino = sc->mp->m_sb.sb_metadirino;
		return 0;
	}

	/* Unlinked dirs can point anywhere; point them up to the root dir. */
	if (VFS_I(sc->ip)->i_nlink == 0) {
		*parent_ino = xchk_inode_rootdir_inum(sc->ip);
		return 0;
	}

	/* Reject garbage parent inode numbers and self-referential parents. */
	if (*parent_ino == NULLFSINO)
	       return 0;
	if (!xfs_verify_dir_ino(sc->mp, *parent_ino) ||
	    *parent_ino == sc->ip->i_ino) {
		*parent_ino = NULLFSINO;
		return 0;
	}

	error = xchk_iget(sc, *parent_ino, &fpi.dp);
	if (error)
		return error;

	if (!S_ISDIR(VFS_I(fpi.dp)->i_mode)) {
		*parent_ino = NULLFSINO;
		goto out_rele;
	}

	error = xrep_findparent_walk_directory(&fpi);
	if (error)
		goto out_rele;

	*parent_ino = fpi.found_parent;
out_rele:
	xchk_irele(sc, fpi.dp);
	return error;
}

/*
 * If we're the root of a directory tree, we are our own parent.  If we're an
 * unlinked directory, the parent /won't/ have a link to us.  Set the parent
 * directory to the root for both cases.  Returns NULLFSINO if we don't know
 * what to do.
 */
xfs_ino_t
xrep_findparent_self_reference(
	struct xfs_scrub	*sc)
{
	if (sc->ip->i_ino == sc->mp->m_sb.sb_rootino)
		return sc->mp->m_sb.sb_rootino;

	if (sc->ip->i_ino == sc->mp->m_sb.sb_metadirino)
		return sc->mp->m_sb.sb_metadirino;

	if (VFS_I(sc->ip)->i_nlink == 0)
		return xchk_inode_rootdir_inum(sc->ip);

	return NULLFSINO;
}

/* Check the dentry cache to see if knows of a parent for the scrub target. */
xfs_ino_t
xrep_findparent_from_dcache(
	struct xfs_scrub	*sc)
{
	struct inode		*pip = NULL;
	struct dentry		*dentry, *parent;
	xfs_ino_t		ret = NULLFSINO;

	dentry = d_find_alias(VFS_I(sc->ip));
	if (!dentry)
		goto out;

	parent = dget_parent(dentry);
	if (!parent)
		goto out_dput;

	ASSERT(parent->d_sb == sc->ip->i_mount->m_super);

	pip = igrab(d_inode(parent));
	dput(parent);

	if (S_ISDIR(pip->i_mode)) {
		trace_xrep_findparent_from_dcache(sc->ip, XFS_I(pip)->i_ino);
		ret = XFS_I(pip)->i_ino;
	}

	xchk_irele(sc, XFS_I(pip));

out_dput:
	dput(dentry);
out:
	return ret;
}
