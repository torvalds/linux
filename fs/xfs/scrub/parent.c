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
#include "scrub/readdir.h"
#include "scrub/tempfile.h"
#include "scrub/repair.h"
#include "scrub/listxattr.h"
#include "scrub/trace.h"

/* Set us up to scrub parents. */
int
xchk_setup_parent(
	struct xfs_scrub	*sc)
{
	int			error;

	if (xchk_could_repair(sc)) {
		error = xrep_setup_parent(sc);
		if (error)
			return error;
	}

	return xchk_setup_inode_contents(sc, 0);
}

/* Parent pointers */

/* Look for an entry in a parent pointing to this inode. */

struct xchk_parent_ctx {
	struct xfs_scrub	*sc;
	xfs_nlink_t		nlink;
};

/* Look for a single entry in a directory pointing to an inode. */
STATIC int
xchk_parent_actor(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp,
	xfs_dir2_dataptr_t	dapos,
	const struct xfs_name	*name,
	xfs_ino_t		ino,
	void			*priv)
{
	struct xchk_parent_ctx	*spc = priv;
	int			error = 0;

	/* Does this name make sense? */
	if (!xfs_dir2_namecheck(name->name, name->len))
		error = -EFSCORRUPTED;
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		return error;

	if (sc->ip->i_ino == ino)
		spc->nlink++;

	if (xchk_should_terminate(spc->sc, &error))
		return error;

	return 0;
}

/*
 * Try to lock a parent directory for checking dirents.  Returns the inode
 * flags for the locks we now hold, or zero if we failed.
 */
STATIC unsigned int
xchk_parent_ilock_dir(
	struct xfs_inode	*dp)
{
	if (!xfs_ilock_nowait(dp, XFS_ILOCK_SHARED))
		return 0;

	if (!xfs_need_iread_extents(&dp->i_df))
		return XFS_ILOCK_SHARED;

	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	if (!xfs_ilock_nowait(dp, XFS_ILOCK_EXCL))
		return 0;

	return XFS_ILOCK_EXCL;
}

/*
 * Given the inode number of the alleged parent of the inode being scrubbed,
 * try to validate that the parent has exactly one directory entry pointing
 * back to the inode being scrubbed.  Returns -EAGAIN if we need to revalidate
 * the dotdot entry.
 */
STATIC int
xchk_parent_validate(
	struct xfs_scrub	*sc,
	xfs_ino_t		parent_ino)
{
	struct xchk_parent_ctx	spc = {
		.sc		= sc,
		.nlink		= 0,
	};
	struct xfs_mount	*mp = sc->mp;
	struct xfs_inode	*dp = NULL;
	xfs_nlink_t		expected_nlink;
	unsigned int		lock_mode;
	int			error = 0;

	/* Is this the root dir?  Then '..' must point to itself. */
	if (sc->ip == mp->m_rootip) {
		if (sc->ip->i_ino != mp->m_sb.sb_rootino ||
		    sc->ip->i_ino != parent_ino)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/* '..' must not point to ourselves. */
	if (sc->ip->i_ino == parent_ino) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/*
	 * If we're an unlinked directory, the parent /won't/ have a link
	 * to us.  Otherwise, it should have one link.
	 */
	expected_nlink = VFS_I(sc->ip)->i_nlink == 0 ? 0 : 1;

	/*
	 * Grab the parent directory inode.  This must be released before we
	 * cancel the scrub transaction.
	 *
	 * If _iget returns -EINVAL or -ENOENT then the parent inode number is
	 * garbage and the directory is corrupt.  If the _iget returns
	 * -EFSCORRUPTED or -EFSBADCRC then the parent is corrupt which is a
	 *  cross referencing error.  Any other error is an operational error.
	 */
	error = xchk_iget(sc, parent_ino, &dp);
	if (error == -EINVAL || error == -ENOENT) {
		error = -EFSCORRUPTED;
		xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error);
		return error;
	}
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		return error;
	if (dp == sc->ip || xrep_is_tempfile(dp) ||
	    !S_ISDIR(VFS_I(dp)->i_mode)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out_rele;
	}

	lock_mode = xchk_parent_ilock_dir(dp);
	if (!lock_mode) {
		xchk_iunlock(sc, XFS_ILOCK_EXCL);
		xchk_ilock(sc, XFS_ILOCK_EXCL);
		error = -EAGAIN;
		goto out_rele;
	}

	/*
	 * We cannot yet validate this parent pointer if the directory looks as
	 * though it has been zapped by the inode record repair code.
	 */
	if (xchk_dir_looks_zapped(dp)) {
		error = -EBUSY;
		xchk_set_incomplete(sc);
		goto out_unlock;
	}

	/* Look for a directory entry in the parent pointing to the child. */
	error = xchk_dir_walk(sc, dp, xchk_parent_actor, &spc);
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out_unlock;

	/*
	 * Ensure that the parent has as many links to the child as the child
	 * thinks it has to the parent.
	 */
	if (spc.nlink != expected_nlink)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);

out_unlock:
	xfs_iunlock(dp, lock_mode);
out_rele:
	xchk_irele(sc, dp);
	return error;
}

/*
 * Checking of Parent Pointers
 * ===========================
 *
 * On filesystems with directory parent pointers, we check the referential
 * integrity by visiting each parent pointer of a child file and checking that
 * the directory referenced by the pointer actually has a dirent pointing
 * forward to the child file.
 */

struct xchk_pptrs {
	struct xfs_scrub	*sc;

	/* How many parent pointers did we find at the end? */
	unsigned long long	pptrs_found;

	/* Parent of this directory. */
	xfs_ino_t		parent_ino;
};

/* Does this parent pointer match the dotdot entry? */
STATIC int
xchk_parent_scan_dotdot(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	unsigned int			attr_flags,
	const unsigned char		*name,
	unsigned int			namelen,
	const void			*value,
	unsigned int			valuelen,
	void				*priv)
{
	struct xchk_pptrs		*pp = priv;
	xfs_ino_t			parent_ino;
	int				error;

	if (!(attr_flags & XFS_ATTR_PARENT))
		return 0;

	error = xfs_parent_from_attr(sc->mp, attr_flags, name, namelen, value,
			valuelen, &parent_ino, NULL);
	if (error)
		return error;

	if (pp->parent_ino == parent_ino)
		return -ECANCELED;

	return 0;
}

/* Look up the dotdot entry so that we can check it as we walk the pptrs. */
STATIC int
xchk_parent_pptr_and_dotdot(
	struct xchk_pptrs	*pp)
{
	struct xfs_scrub	*sc = pp->sc;
	int			error;

	/* Look up '..' */
	error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot, &pp->parent_ino);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		return error;
	if (!xfs_verify_dir_ino(sc->mp, pp->parent_ino)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/* Is this the root dir?  Then '..' must point to itself. */
	if (sc->ip == sc->mp->m_rootip) {
		if (sc->ip->i_ino != pp->parent_ino)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/*
	 * If this is now an unlinked directory, the dotdot value is
	 * meaningless as long as it points to a valid inode.
	 */
	if (VFS_I(sc->ip)->i_nlink == 0)
		return 0;

	if (pp->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/* Otherwise, walk the pptrs again, and check. */
	error = xchk_xattr_walk(sc, sc->ip, xchk_parent_scan_dotdot, pp);
	if (error == -ECANCELED) {
		/* Found a parent pointer that matches dotdot. */
		return 0;
	}
	if (!error || error == -EFSCORRUPTED) {
		/* Found a broken parent pointer or no match. */
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
		return 0;
	}
	return error;
}

/*
 * Try to lock a parent directory for checking dirents.  Returns the inode
 * flags for the locks we now hold, or zero if we failed.
 */
STATIC unsigned int
xchk_parent_lock_dir(
	struct xfs_scrub	*sc,
	struct xfs_inode	*dp)
{
	if (!xfs_ilock_nowait(dp, XFS_IOLOCK_SHARED))
		return 0;

	if (!xfs_ilock_nowait(dp, XFS_ILOCK_SHARED)) {
		xfs_iunlock(dp, XFS_IOLOCK_SHARED);
		return 0;
	}

	if (!xfs_need_iread_extents(&dp->i_df))
		return XFS_IOLOCK_SHARED | XFS_ILOCK_SHARED;

	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	if (!xfs_ilock_nowait(dp, XFS_ILOCK_EXCL)) {
		xfs_iunlock(dp, XFS_IOLOCK_SHARED);
		return 0;
	}

	return XFS_IOLOCK_SHARED | XFS_ILOCK_EXCL;
}

/* Check the forward link (dirent) associated with this parent pointer. */
STATIC int
xchk_parent_dirent(
	struct xchk_pptrs	*pp,
	const struct xfs_name	*xname,
	struct xfs_inode	*dp)
{
	struct xfs_scrub	*sc = pp->sc;
	xfs_ino_t		child_ino;
	int			error;

	/*
	 * Use the name attached to this parent pointer to look up the
	 * directory entry in the alleged parent.
	 */
	error = xchk_dir_lookup(sc, dp, xname, &child_ino);
	if (error == -ENOENT) {
		xchk_fblock_xref_set_corrupt(sc, XFS_ATTR_FORK, 0);
		return 0;
	}
	if (!xchk_fblock_xref_process_error(sc, XFS_ATTR_FORK, 0, &error))
		return error;

	/* Does the inode number match? */
	if (child_ino != sc->ip->i_ino) {
		xchk_fblock_xref_set_corrupt(sc, XFS_ATTR_FORK, 0);
		return 0;
	}

	return 0;
}

/* Try to grab a parent directory. */
STATIC int
xchk_parent_iget(
	struct xchk_pptrs	*pp,
	const struct xfs_parent_rec	*pptr,
	struct xfs_inode	**dpp)
{
	struct xfs_scrub	*sc = pp->sc;
	struct xfs_inode	*ip;
	xfs_ino_t		parent_ino = be64_to_cpu(pptr->p_ino);
	int			error;

	/* Validate inode number. */
	error = xfs_dir_ino_validate(sc->mp, parent_ino);
	if (error) {
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
		return -ECANCELED;
	}

	error = xchk_iget(sc, parent_ino, &ip);
	if (error == -EINVAL || error == -ENOENT) {
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
		return -ECANCELED;
	}
	if (!xchk_fblock_xref_process_error(sc, XFS_ATTR_FORK, 0, &error))
		return error;

	/* The parent must be a directory. */
	if (!S_ISDIR(VFS_I(ip)->i_mode)) {
		xchk_fblock_xref_set_corrupt(sc, XFS_ATTR_FORK, 0);
		goto out_rele;
	}

	/* Validate generation number. */
	if (VFS_I(ip)->i_generation != be32_to_cpu(pptr->p_gen)) {
		xchk_fblock_xref_set_corrupt(sc, XFS_ATTR_FORK, 0);
		goto out_rele;
	}

	*dpp = ip;
	return 0;
out_rele:
	xchk_irele(sc, ip);
	return 0;
}

/*
 * Walk an xattr of a file.  If this xattr is a parent pointer, follow it up
 * to a parent directory and check that the parent has a dirent pointing back
 * to us.
 */
STATIC int
xchk_parent_scan_attr(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	unsigned int		attr_flags,
	const unsigned char	*name,
	unsigned int		namelen,
	const void		*value,
	unsigned int		valuelen,
	void			*priv)
{
	struct xfs_name		xname = {
		.name		= name,
		.len		= namelen,
	};
	struct xchk_pptrs	*pp = priv;
	struct xfs_inode	*dp = NULL;
	const struct xfs_parent_rec *pptr_rec = value;
	xfs_ino_t		parent_ino;
	unsigned int		lockmode;
	int			error;

	if (!(attr_flags & XFS_ATTR_PARENT))
		return 0;

	error = xfs_parent_from_attr(sc->mp, attr_flags, name, namelen, value,
			valuelen, &parent_ino, NULL);
	if (error) {
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
		return error;
	}

	/* No self-referential parent pointers. */
	if (parent_ino == sc->ip->i_ino) {
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
		return -ECANCELED;
	}

	pp->pptrs_found++;

	error = xchk_parent_iget(pp, pptr_rec, &dp);
	if (error)
		return error;
	if (!dp)
		return 0;

	/* Try to lock the inode. */
	lockmode = xchk_parent_lock_dir(sc, dp);
	if (!lockmode) {
		xchk_set_incomplete(sc);
		error = -ECANCELED;
		goto out_rele;
	}

	error = xchk_parent_dirent(pp, &xname, dp);
	if (error)
		goto out_unlock;

out_unlock:
	xfs_iunlock(dp, lockmode);
out_rele:
	xchk_irele(sc, dp);
	return error;
}

/*
 * Compare the number of parent pointers to the link count.  For
 * non-directories these should be the same.  For unlinked directories the
 * count should be zero; for linked directories, it should be nonzero.
 */
STATIC int
xchk_parent_count_pptrs(
	struct xchk_pptrs	*pp)
{
	struct xfs_scrub	*sc = pp->sc;

	if (S_ISDIR(VFS_I(sc->ip)->i_mode)) {
		if (sc->ip == sc->mp->m_rootip)
			pp->pptrs_found++;

		if (VFS_I(sc->ip)->i_nlink == 0 && pp->pptrs_found > 0)
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
		else if (VFS_I(sc->ip)->i_nlink > 0 &&
			 pp->pptrs_found == 0)
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
	} else {
		if (VFS_I(sc->ip)->i_nlink != pp->pptrs_found)
			xchk_ino_set_corrupt(sc, sc->ip->i_ino);
	}

	return 0;
}

/* Check parent pointers of a file. */
STATIC int
xchk_parent_pptr(
	struct xfs_scrub	*sc)
{
	struct xchk_pptrs	*pp;
	int			error;

	pp = kvzalloc(sizeof(struct xchk_pptrs), XCHK_GFP_FLAGS);
	if (!pp)
		return -ENOMEM;
	pp->sc = sc;

	error = xchk_xattr_walk(sc, sc->ip, xchk_parent_scan_attr, pp);
	if (error == -ECANCELED) {
		error = 0;
		goto out_pp;
	}
	if (error)
		goto out_pp;

	if (pp->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out_pp;

	/*
	 * For subdirectories, make sure the dotdot entry references the same
	 * inode as the parent pointers.
	 *
	 * If we're scanning a /consistent/ directory, there should only be
	 * one parent pointer, and it should point to the same directory as
	 * the dotdot entry.
	 *
	 * However, a corrupt directory tree might feature a subdirectory with
	 * multiple parents.  The directory loop scanner is responsible for
	 * correcting that kind of problem, so for now we only validate that
	 * the dotdot entry matches /one/ of the parents.
	 */
	if (S_ISDIR(VFS_I(sc->ip)->i_mode)) {
		error = xchk_parent_pptr_and_dotdot(pp);
		if (error)
			goto out_pp;
	}

	if (pp->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out_pp;

	/*
	 * Complain if the number of parent pointers doesn't match the link
	 * count.  This could be a sign of missing parent pointers (or an
	 * incorrect link count).
	 */
	error = xchk_parent_count_pptrs(pp);
	if (error)
		goto out_pp;

out_pp:
	kvfree(pp);
	return error;
}

/* Scrub a parent pointer. */
int
xchk_parent(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	xfs_ino_t		parent_ino;
	int			error = 0;

	if (xfs_has_parent(mp))
		return xchk_parent_pptr(sc);

	/*
	 * If we're a directory, check that the '..' link points up to
	 * a directory that has one entry pointing to us.
	 */
	if (!S_ISDIR(VFS_I(sc->ip)->i_mode))
		return -ENOENT;

	/* We're not a special inode, are we? */
	if (!xfs_verify_dir_ino(mp, sc->ip->i_ino)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	do {
		if (xchk_should_terminate(sc, &error))
			break;

		/* Look up '..' */
		error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot,
				&parent_ino);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
			return error;
		if (!xfs_verify_dir_ino(mp, parent_ino)) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
			return 0;
		}

		/*
		 * Check that the dotdot entry points to a parent directory
		 * containing a dirent pointing to this subdirectory.
		 */
		error = xchk_parent_validate(sc, parent_ino);
	} while (error == -EAGAIN);
	if (error == -EBUSY) {
		/*
		 * We could not scan a directory, so we marked the check
		 * incomplete.  No further error return is necessary.
		 */
		return 0;
	}

	return error;
}
