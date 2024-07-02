// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * Copyright (c) 2012-2013 Red Hat, Inc.
 * All rights reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_dir2.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_bmap_btree.h"
#include "xfs_quota.h"
#include "xfs_symlink.h"
#include "xfs_trans_space.h"
#include "xfs_trace.h"
#include "xfs_trans.h"
#include "xfs_ialloc.h"
#include "xfs_error.h"
#include "xfs_health.h"
#include "xfs_symlink_remote.h"
#include "xfs_parent.h"
#include "xfs_defer.h"

int
xfs_readlink(
	struct xfs_inode	*ip,
	char			*link)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fsize_t		pathlen;
	int			error;

	trace_xfs_readlink(ip);

	if (xfs_is_shutdown(mp))
		return -EIO;
	if (xfs_ifork_zapped(ip, XFS_DATA_FORK))
		return -EIO;

	xfs_ilock(ip, XFS_ILOCK_SHARED);

	pathlen = ip->i_disk_size;
	if (!pathlen)
		goto out_corrupt;

	if (pathlen < 0 || pathlen > XFS_SYMLINK_MAXLEN) {
		xfs_alert(mp, "%s: inode (%llu) bad symlink length (%lld)",
			 __func__, (unsigned long long) ip->i_ino,
			 (long long) pathlen);
		ASSERT(0);
		goto out_corrupt;
	}

	if (ip->i_df.if_format == XFS_DINODE_FMT_LOCAL) {
		/*
		 * The VFS crashes on a NULL pointer, so return -EFSCORRUPTED
		 * if if_data is junk.
		 */
		if (XFS_IS_CORRUPT(ip->i_mount, !ip->i_df.if_data))
			goto out_corrupt;

		memcpy(link, ip->i_df.if_data, pathlen + 1);
		error = 0;
	} else {
		error = xfs_symlink_remote_read(ip, link);
	}

	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return error;
 out_corrupt:
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	xfs_inode_mark_sick(ip, XFS_SICK_INO_SYMLINK);
	return -EFSCORRUPTED;
}

int
xfs_symlink(
	struct mnt_idmap	*idmap,
	struct xfs_inode	*dp,
	struct xfs_name		*link_name,
	const char		*target_path,
	umode_t			mode,
	struct xfs_inode	**ipp)
{
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_icreate_args	args = {
		.idmap		= idmap,
		.pip		= dp,
		.mode		= S_IFLNK | (mode & ~S_IFMT),
	};
	struct xfs_dir_update	du = {
		.dp		= dp,
		.name		= link_name,
	};
	struct xfs_trans	*tp = NULL;
	int			error = 0;
	int			pathlen;
	bool                    unlock_dp_on_error = false;
	xfs_filblks_t		fs_blocks;
	struct xfs_dquot	*udqp;
	struct xfs_dquot	*gdqp;
	struct xfs_dquot	*pdqp;
	uint			resblks;
	xfs_ino_t		ino;

	*ipp = NULL;

	trace_xfs_symlink(dp, link_name);

	if (xfs_is_shutdown(mp))
		return -EIO;

	/*
	 * Check component lengths of the target path name.
	 */
	pathlen = strlen(target_path);
	if (pathlen >= XFS_SYMLINK_MAXLEN)      /* total string too long */
		return -ENAMETOOLONG;
	ASSERT(pathlen > 0);

	/* Make sure that we have allocated dquot(s) on disk. */
	error = xfs_icreate_dqalloc(&args, &udqp, &gdqp, &pdqp);
	if (error)
		return error;

	/*
	 * The symlink will fit into the inode data fork?
	 * If there are no parent pointers, then there wont't be any attributes.
	 * So we get the whole variable part, and do not need to reserve extra
	 * blocks.  Otherwise, we need to reserve the blocks.
	 */
	if (pathlen <= XFS_LITINO(mp) && !xfs_has_parent(mp))
		fs_blocks = 0;
	else
		fs_blocks = xfs_symlink_blocks(mp, pathlen);
	resblks = xfs_symlink_space_res(mp, link_name->len, fs_blocks);

	error = xfs_parent_start(mp, &du.ppargs);
	if (error)
		goto out_release_dquots;

	error = xfs_trans_alloc_icreate(mp, &M_RES(mp)->tr_symlink, udqp, gdqp,
			pdqp, resblks, &tp);
	if (error)
		goto out_parent;

	xfs_ilock(dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	unlock_dp_on_error = true;

	/*
	 * Check whether the directory allows new symlinks or not.
	 */
	if (dp->i_diflags & XFS_DIFLAG_NOSYMLINKS) {
		error = -EPERM;
		goto out_trans_cancel;
	}

	/*
	 * Allocate an inode for the symlink.
	 */
	error = xfs_dialloc(&tp, dp->i_ino, S_IFLNK, &ino);
	if (!error)
		error = xfs_icreate(tp, ino, &args, &du.ip);
	if (error)
		goto out_trans_cancel;

	/*
	 * Now we join the directory inode to the transaction.  We do not do it
	 * earlier because xfs_dir_ialloc might commit the previous transaction
	 * (and release all the locks).  An error from here on will result in
	 * the transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	xfs_trans_ijoin(tp, dp, 0);

	/*
	 * Also attach the dquot(s) to it, if applicable.
	 */
	xfs_qm_vop_create_dqattach(tp, du.ip, udqp, gdqp, pdqp);

	resblks -= XFS_IALLOC_SPACE_RES(mp);
	error = xfs_symlink_write_target(tp, du.ip, du.ip->i_ino, target_path,
			pathlen, fs_blocks, resblks);
	if (error)
		goto out_trans_cancel;
	resblks -= fs_blocks;
	i_size_write(VFS_I(du.ip), du.ip->i_disk_size);

	/*
	 * Create the directory entry for the symlink.
	 */
	error = xfs_dir_create_child(tp, resblks, &du);
	if (error)
		goto out_trans_cancel;

	/*
	 * If this is a synchronous mount, make sure that the
	 * symlink transaction goes to disk before returning to
	 * the user.
	 */
	if (xfs_has_wsync(mp) || xfs_has_dirsync(mp))
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp);
	if (error)
		goto out_release_inode;

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	*ipp = du.ip;
	xfs_iunlock(du.ip, XFS_ILOCK_EXCL);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
	xfs_parent_finish(mp, du.ppargs);
	return 0;

out_trans_cancel:
	xfs_trans_cancel(tp);
out_release_inode:
	/*
	 * Wait until after the current transaction is aborted to finish the
	 * setup of the inode and release the inode.  This prevents recursive
	 * transactions and deadlocks from xfs_inactive.
	 */
	if (du.ip) {
		xfs_iunlock(du.ip, XFS_ILOCK_EXCL);
		xfs_finish_inode_setup(du.ip);
		xfs_irele(du.ip);
	}
out_parent:
	xfs_parent_finish(mp, du.ppargs);
out_release_dquots:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	if (unlock_dp_on_error)
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Free a symlink that has blocks associated with it.
 *
 * Note: zero length symlinks are not allowed to exist. When we set the size to
 * zero, also change it to a regular file so that it does not get written to
 * disk as a zero length symlink. The inode is on the unlinked list already, so
 * userspace cannot find this inode anymore, so this change is not user visible
 * but allows us to catch corrupt zero-length symlinks in the verifiers.
 */
STATIC int
xfs_inactive_symlink_rmt(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	ASSERT(!xfs_need_iread_extents(&ip->i_df));
	/*
	 * We're freeing a symlink that has some
	 * blocks allocated to it.  Free the
	 * blocks here.  We know that we've got
	 * either 1 or 2 extents and that we can
	 * free them all in one bunmapi call.
	 */
	ASSERT(ip->i_df.if_nextents > 0 && ip->i_df.if_nextents <= 2);

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate, 0, 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * Lock the inode, fix the size, turn it into a regular file and join it
	 * to the transaction.  Hold it so in the normal path, we still have it
	 * locked for the second transaction.  In the error paths we need it
	 * held so the cancel won't rele it, see below.
	 */
	ip->i_disk_size = 0;
	VFS_I(ip)->i_mode = (VFS_I(ip)->i_mode & ~S_IFMT) | S_IFREG;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = xfs_symlink_remote_truncate(tp, ip);
	if (error)
		goto error_trans_cancel;

	error = xfs_trans_commit(tp);
	if (error) {
		ASSERT(xfs_is_shutdown(mp));
		goto error_unlock;
	}

	/*
	 * Remove the memory for extent descriptions (just bookkeeping).
	 */
	if (ip->i_df.if_bytes)
		xfs_idata_realloc(ip, -ip->i_df.if_bytes, XFS_DATA_FORK);
	ASSERT(ip->i_df.if_bytes == 0);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;

error_trans_cancel:
	xfs_trans_cancel(tp);
error_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * xfs_inactive_symlink - free a symlink
 */
int
xfs_inactive_symlink(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	int			pathlen;

	trace_xfs_inactive_symlink(ip);

	if (xfs_is_shutdown(mp))
		return -EIO;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	pathlen = (int)ip->i_disk_size;
	ASSERT(pathlen);

	if (pathlen <= 0 || pathlen > XFS_SYMLINK_MAXLEN) {
		xfs_alert(mp, "%s: inode (0x%llx) bad symlink length (%d)",
			 __func__, (unsigned long long)ip->i_ino, pathlen);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		ASSERT(0);
		xfs_inode_mark_sick(ip, XFS_SICK_INO_SYMLINK);
		return -EFSCORRUPTED;
	}

	/*
	 * Inline fork state gets removed by xfs_difree() so we have nothing to
	 * do here in that case.
	 */
	if (ip->i_df.if_format == XFS_DINODE_FMT_LOCAL) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		return 0;
	}

	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	/* remove the remote symlink */
	return xfs_inactive_symlink_rmt(ip);
}
