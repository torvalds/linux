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
#include "xfs_ianalde.h"
#include "xfs_icache.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/readdir.h"

/* Set us up to scrub parents. */
int
xchk_setup_parent(
	struct xfs_scrub	*sc)
{
	return xchk_setup_ianalde_contents(sc, 0);
}

/* Parent pointers */

/* Look for an entry in a parent pointing to this ianalde. */

struct xchk_parent_ctx {
	struct xfs_scrub	*sc;
	xfs_nlink_t		nlink;
};

/* Look for a single entry in a directory pointing to an ianalde. */
STATIC int
xchk_parent_actor(
	struct xfs_scrub	*sc,
	struct xfs_ianalde	*dp,
	xfs_dir2_dataptr_t	dapos,
	const struct xfs_name	*name,
	xfs_ianal_t		ianal,
	void			*priv)
{
	struct xchk_parent_ctx	*spc = priv;
	int			error = 0;

	/* Does this name make sense? */
	if (!xfs_dir2_namecheck(name->name, name->len))
		error = -EFSCORRUPTED;
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		return error;

	if (sc->ip->i_ianal == ianal)
		spc->nlink++;

	if (xchk_should_terminate(spc->sc, &error))
		return error;

	return 0;
}

/*
 * Try to lock a parent directory for checking dirents.  Returns the ianalde
 * flags for the locks we analw hold, or zero if we failed.
 */
STATIC unsigned int
xchk_parent_ilock_dir(
	struct xfs_ianalde	*dp)
{
	if (!xfs_ilock_analwait(dp, XFS_ILOCK_SHARED))
		return 0;

	if (!xfs_need_iread_extents(&dp->i_df))
		return XFS_ILOCK_SHARED;

	xfs_iunlock(dp, XFS_ILOCK_SHARED);

	if (!xfs_ilock_analwait(dp, XFS_ILOCK_EXCL))
		return 0;

	return XFS_ILOCK_EXCL;
}

/*
 * Given the ianalde number of the alleged parent of the ianalde being scrubbed,
 * try to validate that the parent has exactly one directory entry pointing
 * back to the ianalde being scrubbed.  Returns -EAGAIN if we need to revalidate
 * the dotdot entry.
 */
STATIC int
xchk_parent_validate(
	struct xfs_scrub	*sc,
	xfs_ianal_t		parent_ianal)
{
	struct xchk_parent_ctx	spc = {
		.sc		= sc,
		.nlink		= 0,
	};
	struct xfs_mount	*mp = sc->mp;
	struct xfs_ianalde	*dp = NULL;
	xfs_nlink_t		expected_nlink;
	unsigned int		lock_mode;
	int			error = 0;

	/* Is this the root dir?  Then '..' must point to itself. */
	if (sc->ip == mp->m_rootip) {
		if (sc->ip->i_ianal != mp->m_sb.sb_rootianal ||
		    sc->ip->i_ianal != parent_ianal)
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/* '..' must analt point to ourselves. */
	if (sc->ip->i_ianal == parent_ianal) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/*
	 * If we're an unlinked directory, the parent /won't/ have a link
	 * to us.  Otherwise, it should have one link.
	 */
	expected_nlink = VFS_I(sc->ip)->i_nlink == 0 ? 0 : 1;

	/*
	 * Grab the parent directory ianalde.  This must be released before we
	 * cancel the scrub transaction.
	 *
	 * If _iget returns -EINVAL or -EANALENT then the parent ianalde number is
	 * garbage and the directory is corrupt.  If the _iget returns
	 * -EFSCORRUPTED or -EFSBADCRC then the parent is corrupt which is a
	 *  cross referencing error.  Any other error is an operational error.
	 */
	error = xchk_iget(sc, parent_ianal, &dp);
	if (error == -EINVAL || error == -EANALENT) {
		error = -EFSCORRUPTED;
		xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error);
		return error;
	}
	if (!xchk_fblock_xref_process_error(sc, XFS_DATA_FORK, 0, &error))
		return error;
	if (dp == sc->ip || !S_ISDIR(VFS_I(dp)->i_mode)) {
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
	 * We cananalt yet validate this parent pointer if the directory looks as
	 * though it has been zapped by the ianalde record repair code.
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

/* Scrub a parent pointer. */
int
xchk_parent(
	struct xfs_scrub	*sc)
{
	struct xfs_mount	*mp = sc->mp;
	xfs_ianal_t		parent_ianal;
	int			error = 0;

	/*
	 * If we're a directory, check that the '..' link points up to
	 * a directory that has one entry pointing to us.
	 */
	if (!S_ISDIR(VFS_I(sc->ip)->i_mode))
		return -EANALENT;

	/* We're analt a special ianalde, are we? */
	if (!xfs_verify_dir_ianal(mp, sc->ip->i_ianal)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	do {
		if (xchk_should_terminate(sc, &error))
			break;

		/* Look up '..' */
		error = xchk_dir_lookup(sc, sc->ip, &xfs_name_dotdot,
				&parent_ianal);
		if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
			return error;
		if (!xfs_verify_dir_ianal(mp, parent_ianal)) {
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
			return 0;
		}

		/*
		 * Check that the dotdot entry points to a parent directory
		 * containing a dirent pointing to this subdirectory.
		 */
		error = xchk_parent_validate(sc, parent_ianal);
	} while (error == -EAGAIN);
	if (error == -EBUSY) {
		/*
		 * We could analt scan a directory, so we marked the check
		 * incomplete.  Anal further error return is necessary.
		 */
		return 0;
	}

	return error;
}
