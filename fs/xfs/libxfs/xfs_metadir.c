// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_trans.h"
#include "xfs_metafile.h"
#include "xfs_metadir.h"
#include "xfs_trace.h"
#include "xfs_inode.h"
#include "xfs_quota.h"
#include "xfs_ialloc.h"
#include "xfs_bmap_btree.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_trans_space.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_parent.h"
#include "xfs_health.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"

/*
 * Metadata Directory Tree
 * =======================
 *
 * These functions provide an abstraction layer for looking up, creating, and
 * deleting metadata inodes that live within a special metadata directory tree.
 *
 * This code does not manage the five existing metadata inodes: real time
 * bitmap & summary; and the user, group, and quotas.  All other metadata
 * inodes must use only the xfs_meta{dir,file}_* functions.
 *
 * Callers wishing to create or hardlink a metadata inode must create an
 * xfs_metadir_update structure, call the appropriate xfs_metadir* function,
 * and then call xfs_metadir_commit or xfs_metadir_cancel to commit or cancel
 * the update.  Files in the metadata directory tree currently cannot be
 * unlinked.
 *
 * When the metadir feature is enabled, all metadata inodes must have the
 * "metadata" inode flag set to prevent them from being exposed to the outside
 * world.
 *
 * Callers must take the ILOCK of any inode in the metadata directory tree to
 * synchronize access to that inode.  It is never necessary to take the IOLOCK
 * or the MMAPLOCK since metadata inodes must not be exposed to user space.
 */

static inline void
xfs_metadir_set_xname(
	struct xfs_name		*xname,
	const char		*path,
	unsigned char		ftype)
{
	xname->name = (const unsigned char *)path;
	xname->len = strlen(path);
	xname->type = ftype;
}

/*
 * Given a parent directory @dp and a metadata inode path component @xname,
 * Look up the inode number in the directory, returning it in @ino.
 * @xname.type must match the directory entry's ftype.
 *
 * Caller must hold ILOCK_EXCL.
 */
static inline int
xfs_metadir_lookup(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	struct xfs_name		*xname,
	xfs_ino_t		*ino)
{
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_da_args	args = {
		.trans		= tp,
		.dp		= dp,
		.geo		= mp->m_dir_geo,
		.name		= xname->name,
		.namelen	= xname->len,
		.hashval	= xfs_dir2_hashname(mp, xname),
		.whichfork	= XFS_DATA_FORK,
		.op_flags	= XFS_DA_OP_OKNOENT,
		.owner		= dp->i_ino,
	};
	int			error;

	if (!S_ISDIR(VFS_I(dp)->i_mode)) {
		xfs_fs_mark_sick(mp, XFS_SICK_FS_METADIR);
		return -EFSCORRUPTED;
	}
	if (xfs_is_shutdown(mp))
		return -EIO;

	error = xfs_dir_lookup_args(&args);
	if (error)
		return error;

	if (!xfs_verify_ino(mp, args.inumber)) {
		xfs_fs_mark_sick(mp, XFS_SICK_FS_METADIR);
		return -EFSCORRUPTED;
	}
	if (xname->type != XFS_DIR3_FT_UNKNOWN && xname->type != args.filetype) {
		xfs_fs_mark_sick(mp, XFS_SICK_FS_METADIR);
		return -EFSCORRUPTED;
	}

	trace_xfs_metadir_lookup(dp, xname, args.inumber);
	*ino = args.inumber;
	return 0;
}

/*
 * Look up and read a metadata inode from the metadata directory.  If the path
 * component doesn't exist, return -ENOENT.
 */
int
xfs_metadir_load(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp,
	const char		*path,
	enum xfs_metafile_type	metafile_type,
	struct xfs_inode	**ipp)
{
	struct xfs_name		xname;
	xfs_ino_t		ino;
	int			error;

	xfs_metadir_set_xname(&xname, path, XFS_DIR3_FT_UNKNOWN);

	xfs_ilock(dp, XFS_ILOCK_EXCL);
	error = xfs_metadir_lookup(tp, dp, &xname, &ino);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
	if (error)
		return error;
	return xfs_trans_metafile_iget(tp, ino, metafile_type, ipp);
}

/*
 * Unlock and release resources after committing (or cancelling) a metadata
 * directory tree operation.  The caller retains its reference to @upd->ip
 * and must release it explicitly.
 */
static inline void
xfs_metadir_teardown(
	struct xfs_metadir_update	*upd,
	int				error)
{
	trace_xfs_metadir_teardown(upd, error);

	if (upd->ppargs) {
		xfs_parent_finish(upd->dp->i_mount, upd->ppargs);
		upd->ppargs = NULL;
	}

	if (upd->ip) {
		if (upd->ip_locked)
			xfs_iunlock(upd->ip, XFS_ILOCK_EXCL);
		upd->ip_locked = false;
	}

	if (upd->dp_locked)
		xfs_iunlock(upd->dp, XFS_ILOCK_EXCL);
	upd->dp_locked = false;
}

/*
 * Begin the process of creating a metadata file by allocating transactions
 * and taking whatever resources we're going to need.
 */
int
xfs_metadir_start_create(
	struct xfs_metadir_update	*upd)
{
	struct xfs_mount		*mp = upd->dp->i_mount;
	int				error;

	ASSERT(upd->dp != NULL);
	ASSERT(upd->ip == NULL);
	ASSERT(xfs_has_metadir(mp));
	ASSERT(upd->metafile_type != XFS_METAFILE_UNKNOWN);

	error = xfs_parent_start(mp, &upd->ppargs);
	if (error)
		return error;

	/*
	 * If we ever need the ability to create rt metadata files on a
	 * pre-metadir filesystem, we'll need to dqattach the parent here.
	 * Currently we assume that mkfs will create the files and quotacheck
	 * will account for them.
	 */

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_create,
			xfs_create_space_res(mp, MAXNAMELEN), 0, 0, &upd->tp);
	if (error)
		goto out_teardown;

	/*
	 * Lock the parent directory if there is one.  We can't ijoin it to
	 * the transaction until after the child file has been created.
	 */
	xfs_ilock(upd->dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	upd->dp_locked = true;

	trace_xfs_metadir_start_create(upd);
	return 0;
out_teardown:
	xfs_metadir_teardown(upd, error);
	return error;
}

/*
 * Create a metadata inode with the given @mode, and insert it into the
 * metadata directory tree at the given @upd->path.  The path up to the final
 * component must already exist.  The final path component must not exist.
 *
 * The new metadata inode will be attached to the update structure @upd->ip,
 * with the ILOCK held until the caller releases it.
 *
 * NOTE: This function may return a new inode to the caller even if it returns
 * a negative error code.  If an inode is passed back, the caller must finish
 * setting up the inode before releasing it.
 */
int
xfs_metadir_create(
	struct xfs_metadir_update	*upd,
	umode_t				mode)
{
	struct xfs_icreate_args		args = {
		.pip			= upd->dp,
		.mode			= mode,
	};
	struct xfs_name			xname;
	struct xfs_dir_update		du = {
		.dp			= upd->dp,
		.name			= &xname,
		.ppargs			= upd->ppargs,
	};
	struct xfs_mount		*mp = upd->dp->i_mount;
	xfs_ino_t			ino;
	unsigned int			resblks;
	int				error;

	xfs_assert_ilocked(upd->dp, XFS_ILOCK_EXCL);

	/* Check that the name does not already exist in the directory. */
	xfs_metadir_set_xname(&xname, upd->path, XFS_DIR3_FT_UNKNOWN);
	error = xfs_metadir_lookup(upd->tp, upd->dp, &xname, &ino);
	switch (error) {
	case -ENOENT:
		break;
	case 0:
		error = -EEXIST;
		fallthrough;
	default:
		return error;
	}

	/*
	 * A newly created regular or special file just has one directory
	 * entry pointing to them, but a directory also the "." entry
	 * pointing to itself.
	 */
	error = xfs_dialloc(&upd->tp, &args, &ino);
	if (error)
		return error;
	error = xfs_icreate(upd->tp, ino, &args, &upd->ip);
	if (error)
		return error;
	du.ip = upd->ip;
	xfs_metafile_set_iflag(upd->tp, upd->ip, upd->metafile_type);
	upd->ip_locked = true;

	/*
	 * Join the directory inode to the transaction.  We do not do it
	 * earlier because xfs_dialloc rolls the transaction.
	 */
	xfs_trans_ijoin(upd->tp, upd->dp, 0);

	/* Create the entry. */
	if (S_ISDIR(args.mode))
		resblks = xfs_mkdir_space_res(mp, xname.len);
	else
		resblks = xfs_create_space_res(mp, xname.len);
	xname.type = xfs_mode_to_ftype(args.mode);

	trace_xfs_metadir_try_create(upd);

	error = xfs_dir_create_child(upd->tp, resblks, &du);
	if (error)
		return error;

	/* Metadir files are not accounted to quota. */

	trace_xfs_metadir_create(upd);

	return 0;
}

#ifndef __KERNEL__
/*
 * Begin the process of linking a metadata file by allocating transactions
 * and locking whatever resources we're going to need.
 */
int
xfs_metadir_start_link(
	struct xfs_metadir_update	*upd)
{
	struct xfs_mount		*mp = upd->dp->i_mount;
	unsigned int			resblks;
	int				nospace_error = 0;
	int				error;

	ASSERT(upd->dp != NULL);
	ASSERT(upd->ip != NULL);
	ASSERT(xfs_has_metadir(mp));

	error = xfs_parent_start(mp, &upd->ppargs);
	if (error)
		return error;

	resblks = xfs_link_space_res(mp, MAXNAMELEN);
	error = xfs_trans_alloc_dir(upd->dp, &M_RES(mp)->tr_link, upd->ip,
			&resblks, &upd->tp, &nospace_error);
	if (error)
		goto out_teardown;
	if (!resblks) {
		/* We don't allow reservationless updates. */
		xfs_trans_cancel(upd->tp);
		upd->tp = NULL;
		xfs_iunlock(upd->dp, XFS_ILOCK_EXCL);
		xfs_iunlock(upd->ip, XFS_ILOCK_EXCL);
		error = nospace_error;
		goto out_teardown;
	}

	upd->dp_locked = true;
	upd->ip_locked = true;

	trace_xfs_metadir_start_link(upd);
	return 0;
out_teardown:
	xfs_metadir_teardown(upd, error);
	return error;
}

/*
 * Link the metadata directory given by @path to the inode @upd->ip.
 * The path (up to the final component) must already exist, but the final
 * component must not already exist.
 */
int
xfs_metadir_link(
	struct xfs_metadir_update	*upd)
{
	struct xfs_name			xname;
	struct xfs_dir_update		du = {
		.dp			= upd->dp,
		.name			= &xname,
		.ip			= upd->ip,
		.ppargs			= upd->ppargs,
	};
	struct xfs_mount		*mp = upd->dp->i_mount;
	xfs_ino_t			ino;
	unsigned int			resblks;
	int				error;

	xfs_assert_ilocked(upd->dp, XFS_ILOCK_EXCL);
	xfs_assert_ilocked(upd->ip, XFS_ILOCK_EXCL);

	/* Look up the name in the current directory. */
	xfs_metadir_set_xname(&xname, upd->path,
			xfs_mode_to_ftype(VFS_I(upd->ip)->i_mode));
	error = xfs_metadir_lookup(upd->tp, upd->dp, &xname, &ino);
	switch (error) {
	case -ENOENT:
		break;
	case 0:
		error = -EEXIST;
		fallthrough;
	default:
		return error;
	}

	resblks = xfs_link_space_res(mp, xname.len);
	error = xfs_dir_add_child(upd->tp, resblks, &du);
	if (error)
		return error;

	trace_xfs_metadir_link(upd);

	return 0;
}
#endif /* ! __KERNEL__ */

/* Commit a metadir update and unlock/drop all resources. */
int
xfs_metadir_commit(
	struct xfs_metadir_update	*upd)
{
	int				error;

	trace_xfs_metadir_commit(upd);

	error = xfs_trans_commit(upd->tp);
	upd->tp = NULL;

	xfs_metadir_teardown(upd, error);
	return error;
}

/* Cancel a metadir update and unlock/drop all resources. */
void
xfs_metadir_cancel(
	struct xfs_metadir_update	*upd,
	int				error)
{
	trace_xfs_metadir_cancel(upd);

	xfs_trans_cancel(upd->tp);
	upd->tp = NULL;

	xfs_metadir_teardown(upd, error);
}

/* Create a metadata for the last component of the path. */
int
xfs_metadir_mkdir(
	struct xfs_inode		*dp,
	const char			*path,
	struct xfs_inode		**ipp)
{
	struct xfs_metadir_update	upd = {
		.dp			= dp,
		.path			= path,
		.metafile_type		= XFS_METAFILE_DIR,
	};
	int				error;

	if (xfs_is_shutdown(dp->i_mount))
		return -EIO;

	/* Allocate a transaction to create the last directory. */
	error = xfs_metadir_start_create(&upd);
	if (error)
		return error;

	/* Create the subdirectory and take our reference. */
	error = xfs_metadir_create(&upd, S_IFDIR);
	if (error)
		goto out_cancel;

	error = xfs_metadir_commit(&upd);
	if (error)
		goto out_irele;

	xfs_finish_inode_setup(upd.ip);
	*ipp = upd.ip;
	return 0;

out_cancel:
	xfs_metadir_cancel(&upd, error);
out_irele:
	/* Have to finish setting up the inode to ensure it's deleted. */
	if (upd.ip) {
		xfs_finish_inode_setup(upd.ip);
		xfs_irele(upd.ip);
	}
	return error;
}
