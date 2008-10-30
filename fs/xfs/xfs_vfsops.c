/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_quota.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_rw.h"
#include "xfs_buf_item.h"
#include "xfs_log_priv.h"
#include "xfs_dir2_trace.h"
#include "xfs_extfree_item.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_clnt.h"
#include "xfs_mru_cache.h"
#include "xfs_filestream.h"
#include "xfs_fsops.h"
#include "xfs_vnodeops.h"
#include "xfs_vfsops.h"
#include "xfs_utils.h"
#include "xfs_sync.h"


STATIC void
xfs_quiesce_fs(
	xfs_mount_t		*mp)
{
	int			count = 0, pincount;

	xfs_flush_buftarg(mp->m_ddev_targp, 0);
	xfs_finish_reclaim_all(mp, 0, XFS_IFLUSH_DELWRI_ELSE_ASYNC);

	/*
	 * This loop must run at least twice.  The first instance of the loop
	 * will flush most meta data but that will generate more meta data
	 * (typically directory updates).  Which then must be flushed and
	 * logged before we can write the unmount record.
	 */
	do {
		xfs_log_force(mp, 0, XFS_LOG_FORCE|XFS_LOG_SYNC);
		xfs_sync_inodes(mp, SYNC_ATTR|SYNC_WAIT);
		pincount = xfs_flush_buftarg(mp->m_ddev_targp, 1);
		if (!pincount) {
			delay(50);
			count++;
		}
	} while (count < 2);
}

/*
 * Second stage of a quiesce. The data is already synced, now we have to take
 * care of the metadata. New transactions are already blocked, so we need to
 * wait for any remaining transactions to drain out before proceding.
 */
void
xfs_attr_quiesce(
	xfs_mount_t	*mp)
{
	int	error = 0;

	/* wait for all modifications to complete */
	while (atomic_read(&mp->m_active_trans) > 0)
		delay(100);

	/* flush inodes and push all remaining buffers out to disk */
	xfs_quiesce_fs(mp);

	ASSERT_ALWAYS(atomic_read(&mp->m_active_trans) == 0);

	/* Push the superblock and write an unmount record */
	error = xfs_log_sbcount(mp, 1);
	if (error)
		xfs_fs_cmn_err(CE_WARN, mp,
				"xfs_attr_quiesce: failed to log sb changes. "
				"Frozen image may not be consistent.");
	xfs_log_unmount_write(mp);
	xfs_unmountfs_writesb(mp);
}

/*
 * xfs_unmount_flush implements a set of flush operation on special
 * inodes, which are needed as a separate set of operations so that
 * they can be called as part of relocation process.
 */
int
xfs_unmount_flush(
	xfs_mount_t	*mp,		/* Mount structure we are getting
					   rid of. */
	int             relocation)	/* Called from vfs relocation. */
{
	xfs_inode_t	*rip = mp->m_rootip;
	xfs_inode_t	*rbmip;
	xfs_inode_t	*rsumip = NULL;
	int		error;

	xfs_ilock(rip, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	xfs_iflock(rip);

	/*
	 * Flush out the real time inodes.
	 */
	if ((rbmip = mp->m_rbmip) != NULL) {
		xfs_ilock(rbmip, XFS_ILOCK_EXCL);
		xfs_iflock(rbmip);
		error = xfs_iflush(rbmip, XFS_IFLUSH_SYNC);
		xfs_iunlock(rbmip, XFS_ILOCK_EXCL);

		if (error == EFSCORRUPTED)
			goto fscorrupt_out;

		ASSERT(vn_count(VFS_I(rbmip)) == 1);

		rsumip = mp->m_rsumip;
		xfs_ilock(rsumip, XFS_ILOCK_EXCL);
		xfs_iflock(rsumip);
		error = xfs_iflush(rsumip, XFS_IFLUSH_SYNC);
		xfs_iunlock(rsumip, XFS_ILOCK_EXCL);

		if (error == EFSCORRUPTED)
			goto fscorrupt_out;

		ASSERT(vn_count(VFS_I(rsumip)) == 1);
	}

	/*
	 * Synchronously flush root inode to disk
	 */
	error = xfs_iflush(rip, XFS_IFLUSH_SYNC);
	if (error == EFSCORRUPTED)
		goto fscorrupt_out2;

	if (vn_count(VFS_I(rip)) != 1 && !relocation) {
		xfs_iunlock(rip, XFS_ILOCK_EXCL);
		return XFS_ERROR(EBUSY);
	}

	/*
	 * Release dquot that rootinode, rbmino and rsumino might be holding,
	 * flush and purge the quota inodes.
	 */
	error = XFS_QM_UNMOUNT(mp);
	if (error == EFSCORRUPTED)
		goto fscorrupt_out2;

	if (rbmip) {
		IRELE(rbmip);
		IRELE(rsumip);
	}

	xfs_iunlock(rip, XFS_ILOCK_EXCL);
	return 0;

fscorrupt_out:
	xfs_ifunlock(rip);

fscorrupt_out2:
	xfs_iunlock(rip, XFS_ILOCK_EXCL);

	return XFS_ERROR(EFSCORRUPTED);
}

