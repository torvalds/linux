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
#include "xfs_iwalk.h"
#include "xfs_ialloc.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_ag.h"
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
#include "scrub/readdir.h"
#include "scrub/tempfile.h"
#include "scrub/listxattr.h"

/*
 * Live Inode Link Count Checking
 * ==============================
 *
 * Inode link counts are "summary" metadata, in the sense that they are
 * computed as the number of directory entries referencing each file on the
 * filesystem.  Therefore, we compute the correct link counts by creating a
 * shadow link count structure and walking every inode.
 */

/* Set us up to scrub inode link counts. */
int
xchk_setup_nlinks(
	struct xfs_scrub	*sc)
{
	struct xchk_nlink_ctrs	*xnc;
	int			error;

	xchk_fsgates_enable(sc, XCHK_FSGATES_DIRENTS);

	if (xchk_could_repair(sc)) {
		error = xrep_setup_nlinks(sc);
		if (error)
			return error;
	}

	xnc = kvzalloc(sizeof(struct xchk_nlink_ctrs), XCHK_GFP_FLAGS);
	if (!xnc)
		return -ENOMEM;
	xnc->xname.name = xnc->namebuf;
	xnc->sc = sc;
	sc->buf = xnc;

	return xchk_setup_fs(sc);
}

/*
 * Part 1: Collecting file link counts.  For each file, we create a shadow link
 * counting structure, then walk the entire directory tree, incrementing parent
 * and child link counts for each directory entry seen.
 *
 * To avoid false corruption reports in part 2, any failure in this part must
 * set the INCOMPLETE flag even when a negative errno is returned.  This care
 * must be taken with certain errno values (i.e. EFSBADCRC, EFSCORRUPTED,
 * ECANCELED) that are absorbed into a scrub state flag update by
 * xchk_*_process_error.  Scrub and repair share the same incore data
 * structures, so the INCOMPLETE flag is critical to prevent a repair based on
 * insufficient information.
 *
 * Because we are scanning a live filesystem, it's possible that another thread
 * will try to update the link counts for an inode that we've already scanned.
 * This will cause our counts to be incorrect.  Therefore, we hook all
 * directory entry updates because that is when link count updates occur.  By
 * shadowing transaction updates in this manner, live nlink check can ensure by
 * locking the inode and the shadow structure that its own copies are not out
 * of date.  Because the hook code runs in a different process context from the
 * scrub code and the scrub state flags are not accessed atomically, failures
 * in the hook code must abort the iscan and the scrubber must notice the
 * aborted scan and set the incomplete flag.
 *
 * Note that we use jump labels and srcu notifier hooks to minimize the
 * overhead when live nlinks is /not/ running.  Locking order for nlink
 * observations is inode ILOCK -> iscan_lock/xchk_nlink_ctrs lock.
 */

/*
 * Add a delta to an nlink counter, clamping the value to U32_MAX.  Because
 * XFS_MAXLINK < U32_MAX, the checking code will produce the correct results
 * even if we lose some precision.
 */
static inline void
careful_add(
	xfs_nlink_t	*nlinkp,
	int		delta)
{
	uint64_t	new_value = (uint64_t)(*nlinkp) + delta;

	BUILD_BUG_ON(XFS_MAXLINK > U32_MAX);
	*nlinkp = min_t(uint64_t, new_value, U32_MAX);
}

/* Update incore link count information.  Caller must hold the nlinks lock. */
STATIC int
xchk_nlinks_update_incore(
	struct xchk_nlink_ctrs	*xnc,
	xfs_ino_t		ino,
	int			parents_delta,
	int			backrefs_delta,
	int			children_delta)
{
	struct xchk_nlink	nl;
	int			error;

	if (!xnc->nlinks)
		return 0;

	error = xfarray_load_sparse(xnc->nlinks, ino, &nl);
	if (error)
		return error;

	trace_xchk_nlinks_update_incore(xnc->sc->mp, ino, &nl, parents_delta,
			backrefs_delta, children_delta);

	careful_add(&nl.parents, parents_delta);
	careful_add(&nl.backrefs, backrefs_delta);
	careful_add(&nl.children, children_delta);

	nl.flags |= XCHK_NLINK_WRITTEN;
	error = xfarray_store(xnc->nlinks, ino, &nl);
	if (error == -EFBIG) {
		/*
		 * EFBIG means we tried to store data at too high a byte offset
		 * in the sparse array.  IOWs, we cannot complete the check and
		 * must notify userspace that the check was incomplete.
		 */
		error = -ECANCELED;
	}
	return error;
}

/*
 * Apply a link count change from the regular filesystem into our shadow link
 * count structure based on a directory update in progress.
 */
STATIC int
xchk_nlinks_live_update(
	struct notifier_block		*nb,
	unsigned long			action,
	void				*data)
{
	struct xfs_dir_update_params	*p = data;
	struct xchk_nlink_ctrs		*xnc;
	int				error;

	xnc = container_of(nb, struct xchk_nlink_ctrs, dhook.dirent_hook.nb);

	/*
	 * Ignore temporary directories being used to stage dir repairs, since
	 * we don't bump the link counts of the children.
	 */
	if (xrep_is_tempfile(p->dp))
		return NOTIFY_DONE;

	trace_xchk_nlinks_live_update(xnc->sc->mp, p->dp, action, p->ip->i_ino,
			p->delta, p->name->name, p->name->len);

	/*
	 * If we've already scanned @dp, update the number of parents that link
	 * to @ip.  If @ip is a subdirectory, update the number of child links
	 * going out of @dp.
	 */
	if (xchk_iscan_want_live_update(&xnc->collect_iscan, p->dp->i_ino)) {
		mutex_lock(&xnc->lock);
		error = xchk_nlinks_update_incore(xnc, p->ip->i_ino, p->delta,
				0, 0);
		if (!error && S_ISDIR(VFS_IC(p->ip)->i_mode))
			error = xchk_nlinks_update_incore(xnc, p->dp->i_ino, 0,
					0, p->delta);
		mutex_unlock(&xnc->lock);
		if (error)
			goto out_abort;
	}

	/*
	 * If @ip is a subdirectory and we've already scanned it, update the
	 * number of backrefs pointing to @dp.
	 */
	if (S_ISDIR(VFS_IC(p->ip)->i_mode) &&
	    xchk_iscan_want_live_update(&xnc->collect_iscan, p->ip->i_ino)) {
		mutex_lock(&xnc->lock);
		error = xchk_nlinks_update_incore(xnc, p->dp->i_ino, 0,
				p->delta, 0);
		mutex_unlock(&xnc->lock);
		if (error)
			goto out_abort;
	}

	return NOTIFY_DONE;

out_abort:
	xchk_iscan_abort(&xnc->collect_iscan);
	return NOTIFY_DONE;
}

/* Bump the observed link count for the inode referenced by this entry. */
STATIC int
xchk_nlinks_collect_dirent(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xfs_dir2_dataptr_t	dapos,
	const struct xfs_name	*name,
	xfs_ino_t		ino,
	void			*priv)
{
	struct xchk_nlink_ctrs	*xnc = priv;
	bool			dot = false, dotdot = false;
	int			error;

	/* Does this name make sense? */
	if (name->len == 0 || !xfs_dir2_namecheck(name->name, name->len)) {
		error = -ECANCELED;
		goto out_abort;
	}

	if (name->len == 1 && name->name[0] == '.')
		dot = true;
	else if (name->len == 2 && name->name[0] == '.' &&
				   name->name[1] == '.')
		dotdot = true;

	/* Don't accept a '.' entry that points somewhere else. */
	if (dot && ino != dp->i_ino) {
		error = -ECANCELED;
		goto out_abort;
	}

	/* Don't accept an invalid inode number. */
	if (!xfs_verify_dir_ino(sc->mp, ino)) {
		error = -ECANCELED;
		goto out_abort;
	}

	/* Update the shadow link counts if we haven't already failed. */

	if (xchk_iscan_aborted(&xnc->collect_iscan)) {
		error = -ECANCELED;
		goto out_incomplete;
	}

	trace_xchk_nlinks_collect_dirent(sc->mp, dp, ino, name);

	mutex_lock(&xnc->lock);

	/*
	 * If this is a dotdot entry, it is a back link from dp to ino.  How
	 * we handle this depends on whether or not dp is the root directory.
	 *
	 * The root directory is its own parent, so we pretend the dotdot entry
	 * establishes the "parent" of the root directory.  Increment the
	 * number of parents of the root directory.
	 *
	 * Otherwise, increment the number of backrefs pointing back to ino.
	 *
	 * If the filesystem has parent pointers, we walk the pptrs to
	 * determine the backref count.
	 */
	if (dotdot) {
		if (xchk_inode_is_dirtree_root(dp))
			error = xchk_nlinks_update_incore(xnc, ino, 1, 0, 0);
		else if (!xfs_has_parent(sc->mp))
			error = xchk_nlinks_update_incore(xnc, ino, 0, 1, 0);
		else
			error = 0;
		if (error)
			goto out_unlock;
	}

	/*
	 * If this dirent is a forward link from dp to ino, increment the
	 * number of parents linking into ino.
	 */
	if (!dot && !dotdot) {
		error = xchk_nlinks_update_incore(xnc, ino, 1, 0, 0);
		if (error)
			goto out_unlock;
	}

	/*
	 * If this dirent is a forward link to a subdirectory, increment the
	 * number of child links of dp.
	 */
	if (!dot && !dotdot && name->type == XFS_DIR3_FT_DIR) {
		error = xchk_nlinks_update_incore(xnc, dp->i_ino, 0, 0, 1);
		if (error)
			goto out_unlock;
	}

	mutex_unlock(&xnc->lock);
	return 0;

out_unlock:
	mutex_unlock(&xnc->lock);
out_abort:
	xchk_iscan_abort(&xnc->collect_iscan);
out_incomplete:
	xchk_set_incomplete(sc);
	return error;
}

/* Bump the backref count for the inode referenced by this parent pointer. */
STATIC int
xchk_nlinks_collect_pptr(
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
	struct xchk_nlink_ctrs		*xnc = priv;
	const struct xfs_parent_rec	*pptr_rec = value;
	xfs_ino_t			parent_ino;
	int				error;

	/* Update the shadow link counts if we haven't already failed. */

	if (xchk_iscan_aborted(&xnc->collect_iscan)) {
		error = -ECANCELED;
		goto out_incomplete;
	}

	if (!(attr_flags & XFS_ATTR_PARENT))
		return 0;

	error = xfs_parent_from_attr(sc->mp, attr_flags, name, namelen, value,
			valuelen, &parent_ino, NULL);
	if (error)
		return error;

	trace_xchk_nlinks_collect_pptr(sc->mp, ip, &xname, pptr_rec);

	mutex_lock(&xnc->lock);

	error = xchk_nlinks_update_incore(xnc, parent_ino, 0, 1, 0);
	if (error)
		goto out_unlock;

	mutex_unlock(&xnc->lock);
	return 0;

out_unlock:
	mutex_unlock(&xnc->lock);
	xchk_iscan_abort(&xnc->collect_iscan);
out_incomplete:
	xchk_set_incomplete(sc);
	return error;
}

/* Walk a directory to bump the observed link counts of the children. */
STATIC int
xchk_nlinks_collect_dir(
	struct xchk_nlink_ctrs	*xnc,
	struct xfs_inode	*dp)
{
	struct xfs_scrub	*sc = xnc->sc;
	unsigned int		lock_mode;
	int			error = 0;

	/*
	 * Ignore temporary directories being used to stage dir repairs, since
	 * we don't bump the link counts of the children.
	 */
	if (xrep_is_tempfile(dp))
		return 0;

	/* Prevent anyone from changing this directory while we walk it. */
	xfs_ilock(dp, XFS_IOLOCK_SHARED);
	lock_mode = xfs_ilock_data_map_shared(dp);

	/*
	 * The dotdot entry of an unlinked directory still points to the last
	 * parent, but the parent no longer links to this directory.  Skip the
	 * directory to avoid overcounting.
	 */
	if (VFS_I(dp)->i_nlink == 0)
		goto out_unlock;

	/*
	 * We cannot count file links if the directory looks as though it has
	 * been zapped by the inode record repair code.
	 */
	if (xchk_dir_looks_zapped(dp)) {
		error = -EBUSY;
		goto out_abort;
	}

	error = xchk_dir_walk(sc, dp, xchk_nlinks_collect_dirent, xnc);
	if (error == -ECANCELED) {
		error = 0;
		goto out_unlock;
	}
	if (error)
		goto out_abort;

	/* Walk the parent pointers to get real backref counts. */
	if (xfs_has_parent(sc->mp)) {
		/*
		 * If the extended attributes look as though they has been
		 * zapped by the inode record repair code, we cannot scan for
		 * parent pointers.
		 */
		if (xchk_pptr_looks_zapped(dp)) {
			error = -EBUSY;
			goto out_unlock;
		}

		error = xchk_xattr_walk(sc, dp, xchk_nlinks_collect_pptr, NULL,
				xnc);
		if (error == -ECANCELED) {
			error = 0;
			goto out_unlock;
		}
		if (error)
			goto out_abort;
	}

	xchk_iscan_mark_visited(&xnc->collect_iscan, dp);
	goto out_unlock;

out_abort:
	xchk_set_incomplete(sc);
	xchk_iscan_abort(&xnc->collect_iscan);
out_unlock:
	xfs_iunlock(dp, lock_mode);
	xfs_iunlock(dp, XFS_IOLOCK_SHARED);
	return error;
}

/* If this looks like a valid pointer, count it. */
static inline int
xchk_nlinks_collect_metafile(
	struct xchk_nlink_ctrs	*xnc,
	xfs_ino_t		ino)
{
	if (!xfs_verify_ino(xnc->sc->mp, ino))
		return 0;

	trace_xchk_nlinks_collect_metafile(xnc->sc->mp, ino);
	return xchk_nlinks_update_incore(xnc, ino, 1, 0, 0);
}

/* Bump the link counts of metadata files rooted in the superblock. */
STATIC int
xchk_nlinks_collect_metafiles(
	struct xchk_nlink_ctrs	*xnc)
{
	struct xfs_mount	*mp = xnc->sc->mp;
	int			error = -ECANCELED;


	if (xchk_iscan_aborted(&xnc->collect_iscan))
		goto out_incomplete;

	mutex_lock(&xnc->lock);
	error = xchk_nlinks_collect_metafile(xnc, mp->m_sb.sb_rbmino);
	if (error)
		goto out_abort;

	error = xchk_nlinks_collect_metafile(xnc, mp->m_sb.sb_rsumino);
	if (error)
		goto out_abort;

	error = xchk_nlinks_collect_metafile(xnc, mp->m_sb.sb_uquotino);
	if (error)
		goto out_abort;

	error = xchk_nlinks_collect_metafile(xnc, mp->m_sb.sb_gquotino);
	if (error)
		goto out_abort;

	error = xchk_nlinks_collect_metafile(xnc, mp->m_sb.sb_pquotino);
	if (error)
		goto out_abort;
	mutex_unlock(&xnc->lock);

	return 0;

out_abort:
	mutex_unlock(&xnc->lock);
	xchk_iscan_abort(&xnc->collect_iscan);
out_incomplete:
	xchk_set_incomplete(xnc->sc);
	return error;
}

/* Advance the collection scan cursor for this non-directory file. */
static inline int
xchk_nlinks_collect_file(
	struct xchk_nlink_ctrs	*xnc,
	struct xfs_inode	*ip)
{
	xfs_ilock(ip, XFS_IOLOCK_SHARED);
	xchk_iscan_mark_visited(&xnc->collect_iscan, ip);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return 0;
}

/* Walk all directories and count inode links. */
STATIC int
xchk_nlinks_collect(
	struct xchk_nlink_ctrs	*xnc)
{
	struct xfs_scrub	*sc = xnc->sc;
	struct xfs_inode	*ip;
	int			error;

	/* Count the rt and quota files that are rooted in the superblock. */
	error = xchk_nlinks_collect_metafiles(xnc);
	if (error)
		return error;

	/*
	 * Set up for a potentially lengthy filesystem scan by reducing our
	 * transaction resource usage for the duration.  Specifically:
	 *
	 * Cancel the transaction to release the log grant space while we scan
	 * the filesystem.
	 *
	 * Create a new empty transaction to eliminate the possibility of the
	 * inode scan deadlocking on cyclical metadata.
	 *
	 * We pass the empty transaction to the file scanning function to avoid
	 * repeatedly cycling empty transactions.  This can be done even though
	 * we take the IOLOCK to quiesce the file because empty transactions
	 * do not take sb_internal.
	 */
	xchk_trans_cancel(sc);
	error = xchk_trans_alloc_empty(sc);
	if (error)
		return error;

	while ((error = xchk_iscan_iter(&xnc->collect_iscan, &ip)) == 1) {
		if (S_ISDIR(VFS_I(ip)->i_mode))
			error = xchk_nlinks_collect_dir(xnc, ip);
		else
			error = xchk_nlinks_collect_file(xnc, ip);
		xchk_irele(sc, ip);
		if (error)
			break;

		if (xchk_should_terminate(sc, &error))
			break;
	}
	xchk_iscan_iter_finish(&xnc->collect_iscan);
	if (error) {
		xchk_set_incomplete(sc);
		/*
		 * If we couldn't grab an inode that was busy with a state
		 * change, change the error code so that we exit to userspace
		 * as quickly as possible.
		 */
		if (error == -EBUSY)
			return -ECANCELED;
		return error;
	}

	/*
	 * Switch out for a real transaction in preparation for building a new
	 * tree.
	 */
	xchk_trans_cancel(sc);
	return xchk_setup_fs(sc);
}

/*
 * Part 2: Comparing file link counters.  Walk each inode and compare the link
 * counts against our shadow information; and then walk each shadow link count
 * structure (that wasn't covered in the first part), comparing it against the
 * file.
 */

/* Read the observed link count for comparison with the actual inode. */
STATIC int
xchk_nlinks_comparison_read(
	struct xchk_nlink_ctrs	*xnc,
	xfs_ino_t		ino,
	struct xchk_nlink	*obs)
{
	struct xchk_nlink	nl;
	int			error;

	error = xfarray_load_sparse(xnc->nlinks, ino, &nl);
	if (error)
		return error;

	nl.flags |= (XCHK_NLINK_COMPARE_SCANNED | XCHK_NLINK_WRITTEN);

	error = xfarray_store(xnc->nlinks, ino, &nl);
	if (error == -EFBIG) {
		/*
		 * EFBIG means we tried to store data at too high a byte offset
		 * in the sparse array.  IOWs, we cannot complete the check and
		 * must notify userspace that the check was incomplete.  This
		 * shouldn't really happen outside of the collection phase.
		 */
		xchk_set_incomplete(xnc->sc);
		return -ECANCELED;
	}
	if (error)
		return error;

	/* Copy the counters, but do not expose the internal state. */
	obs->parents = nl.parents;
	obs->backrefs = nl.backrefs;
	obs->children = nl.children;
	obs->flags = 0;
	return 0;
}

/* Check our link count against an inode. */
STATIC int
xchk_nlinks_compare_inode(
	struct xchk_nlink_ctrs	*xnc,
	struct xfs_inode	*ip)
{
	struct xchk_nlink	obs;
	struct xfs_scrub	*sc = xnc->sc;
	uint64_t		total_links;
	unsigned int		actual_nlink;
	int			error;

	/*
	 * Ignore temporary files being used to stage repairs, since we assume
	 * they're correct for non-directories, and the directory repair code
	 * doesn't bump the link counts for the children.
	 */
	if (xrep_is_tempfile(ip))
		return 0;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	mutex_lock(&xnc->lock);

	if (xchk_iscan_aborted(&xnc->collect_iscan)) {
		xchk_set_incomplete(xnc->sc);
		error = -ECANCELED;
		goto out_scanlock;
	}

	error = xchk_nlinks_comparison_read(xnc, ip->i_ino, &obs);
	if (error)
		goto out_scanlock;

	/*
	 * If we don't have ftype to get an accurate count of the subdirectory
	 * entries in this directory, take advantage of the fact that on a
	 * consistent ftype=0 filesystem, the number of subdirectory
	 * backreferences (dotdot entries) pointing towards this directory
	 * should be equal to the number of subdirectory entries in the
	 * directory.
	 */
	if (!xfs_has_ftype(sc->mp) && S_ISDIR(VFS_I(ip)->i_mode))
		obs.children = obs.backrefs;

	total_links = xchk_nlink_total(ip, &obs);
	actual_nlink = VFS_I(ip)->i_nlink;

	trace_xchk_nlinks_compare_inode(sc->mp, ip, &obs);

	/*
	 * If we found so many parents that we'd overflow i_nlink, we must flag
	 * this as a corruption.  The VFS won't let users increase the link
	 * count, but it will let them decrease it.
	 */
	if (total_links > XFS_NLINK_PINNED) {
		xchk_ino_set_corrupt(sc, ip->i_ino);
		goto out_corrupt;
	} else if (total_links > XFS_MAXLINK) {
		xchk_ino_set_warning(sc, ip->i_ino);
	}

	/* Link counts should match. */
	if (total_links != actual_nlink) {
		xchk_ino_set_corrupt(sc, ip->i_ino);
		goto out_corrupt;
	}

	if (S_ISDIR(VFS_I(ip)->i_mode) && actual_nlink > 0) {
		/*
		 * The collection phase ignores directories with zero link
		 * count, so we ignore them here too.
		 *
		 * The number of subdirectory backreferences (dotdot entries)
		 * pointing towards this directory should be equal to the
		 * number of subdirectory entries in the directory.
		 */
		if (obs.children != obs.backrefs)
			xchk_ino_xref_set_corrupt(sc, ip->i_ino);
	} else {
		/*
		 * Non-directories and unlinked directories should not have
		 * back references.
		 */
		if (obs.backrefs != 0) {
			xchk_ino_set_corrupt(sc, ip->i_ino);
			goto out_corrupt;
		}

		/*
		 * Non-directories and unlinked directories should not have
		 * children.
		 */
		if (obs.children != 0) {
			xchk_ino_set_corrupt(sc, ip->i_ino);
			goto out_corrupt;
		}
	}

	if (xchk_inode_is_dirtree_root(ip)) {
		/*
		 * For the root of a directory tree, both the '.' and '..'
		 * entries should point to the root directory.  The dotdot
		 * entry is counted as a parent of the root /and/ a backref of
		 * the root directory.
		 */
		if (obs.parents != 1) {
			xchk_ino_set_corrupt(sc, ip->i_ino);
			goto out_corrupt;
		}
	} else if (actual_nlink > 0) {
		/*
		 * Linked files that are not the root directory should have at
		 * least one parent.
		 */
		if (obs.parents == 0) {
			xchk_ino_set_corrupt(sc, ip->i_ino);
			goto out_corrupt;
		}
	}

out_corrupt:
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		error = -ECANCELED;
out_scanlock:
	mutex_unlock(&xnc->lock);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return error;
}

/*
 * Check our link count against an inode that wasn't checked previously.  This
 * is intended to catch directories with dangling links, though we could be
 * racing with inode allocation in other threads.
 */
STATIC int
xchk_nlinks_compare_inum(
	struct xchk_nlink_ctrs	*xnc,
	xfs_ino_t		ino)
{
	struct xchk_nlink	obs;
	struct xfs_mount	*mp = xnc->sc->mp;
	struct xfs_trans	*tp = xnc->sc->tp;
	struct xfs_buf		*agi_bp;
	struct xfs_inode	*ip;
	int			error;

	/*
	 * The first iget failed, so try again with the variant that returns
	 * either an incore inode or the AGI buffer.  If the function returns
	 * EINVAL/ENOENT, it should have passed us the AGI buffer so that we
	 * can guarantee that the inode won't be allocated while we check for
	 * a zero link count in the observed link count data.
	 */
	error = xchk_iget_agi(xnc->sc, ino, &agi_bp, &ip);
	if (!error) {
		/* Actually got an inode, so use the inode compare. */
		error = xchk_nlinks_compare_inode(xnc, ip);
		xchk_irele(xnc->sc, ip);
		return error;
	}
	if (error == -ENOENT || error == -EINVAL) {
		/* No inode was found.  Check for zero link count below. */
		error = 0;
	}
	if (error)
		goto out_agi;

	/* Ensure that we have protected against inode allocation/freeing. */
	if (agi_bp == NULL) {
		ASSERT(agi_bp != NULL);
		xchk_set_incomplete(xnc->sc);
		return -ECANCELED;
	}

	if (xchk_iscan_aborted(&xnc->collect_iscan)) {
		xchk_set_incomplete(xnc->sc);
		error = -ECANCELED;
		goto out_agi;
	}

	mutex_lock(&xnc->lock);
	error = xchk_nlinks_comparison_read(xnc, ino, &obs);
	if (error)
		goto out_scanlock;

	trace_xchk_nlinks_check_zero(mp, ino, &obs);

	/*
	 * If we can't grab the inode, the link count had better be zero.  We
	 * still hold the AGI to prevent inode allocation/freeing.
	 */
	if (xchk_nlink_total(NULL, &obs) != 0) {
		xchk_ino_set_corrupt(xnc->sc, ino);
		error = -ECANCELED;
	}

out_scanlock:
	mutex_unlock(&xnc->lock);
out_agi:
	if (agi_bp)
		xfs_trans_brelse(tp, agi_bp);
	return error;
}

/*
 * Try to visit every inode in the filesystem to compare the link count.  Move
 * on if we can't grab an inode, since we'll revisit unchecked nlink records in
 * the second part.
 */
static int
xchk_nlinks_compare_iter(
	struct xchk_nlink_ctrs	*xnc,
	struct xfs_inode	**ipp)
{
	int			error;

	do {
		error = xchk_iscan_iter(&xnc->compare_iscan, ipp);
	} while (error == -EBUSY);

	return error;
}

/* Compare the link counts we observed against the live information. */
STATIC int
xchk_nlinks_compare(
	struct xchk_nlink_ctrs	*xnc)
{
	struct xchk_nlink	nl;
	struct xfs_scrub	*sc = xnc->sc;
	struct xfs_inode	*ip;
	xfarray_idx_t		cur = XFARRAY_CURSOR_INIT;
	int			error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/*
	 * Create a new empty transaction so that we can advance the iscan
	 * cursor without deadlocking if the inobt has a cycle and push on the
	 * inactivation workqueue.
	 */
	xchk_trans_cancel(sc);
	error = xchk_trans_alloc_empty(sc);
	if (error)
		return error;

	/*
	 * Use the inobt to walk all allocated inodes to compare the link
	 * counts.  Inodes skipped by _compare_iter will be tried again in the
	 * next phase of the scan.
	 */
	xchk_iscan_start(sc, 0, 0, &xnc->compare_iscan);
	while ((error = xchk_nlinks_compare_iter(xnc, &ip)) == 1) {
		error = xchk_nlinks_compare_inode(xnc, ip);
		xchk_iscan_mark_visited(&xnc->compare_iscan, ip);
		xchk_irele(sc, ip);
		if (error)
			break;

		if (xchk_should_terminate(sc, &error))
			break;
	}
	xchk_iscan_iter_finish(&xnc->compare_iscan);
	xchk_iscan_teardown(&xnc->compare_iscan);
	if (error)
		return error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/*
	 * Walk all the non-null nlink observations that weren't checked in the
	 * previous step.
	 */
	mutex_lock(&xnc->lock);
	while ((error = xfarray_iter(xnc->nlinks, &cur, &nl)) == 1) {
		xfs_ino_t	ino = cur - 1;

		if (nl.flags & XCHK_NLINK_COMPARE_SCANNED)
			continue;

		mutex_unlock(&xnc->lock);

		error = xchk_nlinks_compare_inum(xnc, ino);
		if (error)
			return error;

		if (xchk_should_terminate(xnc->sc, &error))
			return error;

		mutex_lock(&xnc->lock);
	}
	mutex_unlock(&xnc->lock);

	return error;
}

/* Tear down everything associated with a nlinks check. */
static void
xchk_nlinks_teardown_scan(
	void			*priv)
{
	struct xchk_nlink_ctrs	*xnc = priv;

	/* Discourage any hook functions that might be running. */
	xchk_iscan_abort(&xnc->collect_iscan);

	xfs_dir_hook_del(xnc->sc->mp, &xnc->dhook);

	xfarray_destroy(xnc->nlinks);
	xnc->nlinks = NULL;

	xchk_iscan_teardown(&xnc->collect_iscan);
	mutex_destroy(&xnc->lock);
	xnc->sc = NULL;
}

/*
 * Scan all inodes in the entire filesystem to generate link count data.  If
 * the scan is successful, the counts will be left alive for a repair.  If any
 * error occurs, we'll tear everything down.
 */
STATIC int
xchk_nlinks_setup_scan(
	struct xfs_scrub	*sc,
	struct xchk_nlink_ctrs	*xnc)
{
	struct xfs_mount	*mp = sc->mp;
	char			*descr;
	unsigned long long	max_inos;
	xfs_agnumber_t		last_agno = mp->m_sb.sb_agcount - 1;
	xfs_agino_t		first_agino, last_agino;
	int			error;

	mutex_init(&xnc->lock);

	/* Retry iget every tenth of a second for up to 30 seconds. */
	xchk_iscan_start(sc, 30000, 100, &xnc->collect_iscan);

	/*
	 * Set up enough space to store an nlink record for the highest
	 * possible inode number in this system.
	 */
	xfs_agino_range(mp, last_agno, &first_agino, &last_agino);
	max_inos = XFS_AGINO_TO_INO(mp, last_agno, last_agino) + 1;
	descr = xchk_xfile_descr(sc, "file link counts");
	error = xfarray_create(descr, min(XFS_MAXINUMBER + 1, max_inos),
			sizeof(struct xchk_nlink), &xnc->nlinks);
	kfree(descr);
	if (error)
		goto out_teardown;

	/*
	 * Hook into the directory entry code so that we can capture updates to
	 * file link counts.  The hook only triggers for inodes that were
	 * already scanned, and the scanner thread takes each inode's ILOCK,
	 * which means that any in-progress inode updates will finish before we
	 * can scan the inode.
	 */
	ASSERT(sc->flags & XCHK_FSGATES_DIRENTS);
	xfs_dir_hook_setup(&xnc->dhook, xchk_nlinks_live_update);
	error = xfs_dir_hook_add(mp, &xnc->dhook);
	if (error)
		goto out_teardown;

	/* Use deferred cleanup to pass the inode link count data to repair. */
	sc->buf_cleanup = xchk_nlinks_teardown_scan;
	return 0;

out_teardown:
	xchk_nlinks_teardown_scan(xnc);
	return error;
}

/* Scrub the link count of all inodes on the filesystem. */
int
xchk_nlinks(
	struct xfs_scrub	*sc)
{
	struct xchk_nlink_ctrs	*xnc = sc->buf;
	int			error = 0;

	/* Set ourselves up to check link counts on the live filesystem. */
	error = xchk_nlinks_setup_scan(sc, xnc);
	if (error)
		return error;

	/* Walk all inodes, picking up link count information. */
	error = xchk_nlinks_collect(xnc);
	if (!xchk_xref_process_error(sc, 0, 0, &error))
		return error;

	/* Fail fast if we're not playing with a full dataset. */
	if (xchk_iscan_aborted(&xnc->collect_iscan))
		xchk_set_incomplete(sc);
	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_INCOMPLETE)
		return 0;

	/* Compare link counts. */
	error = xchk_nlinks_compare(xnc);
	if (!xchk_xref_process_error(sc, 0, 0, &error))
		return error;

	/* Check one last time for an incomplete dataset. */
	if (xchk_iscan_aborted(&xnc->collect_iscan))
		xchk_set_incomplete(sc);

	return 0;
}
