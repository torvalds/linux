/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_error.h"
#include "xfs_rw.h"

/*
 * Force a shutdown of the filesystem instantly while keeping
 * the filesystem consistent. We don't do an unmount here; just shutdown
 * the shop, make sure that absolutely nothing persistent happens to
 * this filesystem after this point.
 */
void
xfs_do_force_shutdown(
	xfs_mount_t	*mp,
	int		flags,
	char		*fname,
	int		lnnum)
{
	int		logerror;

	logerror = flags & SHUTDOWN_LOG_IO_ERROR;

	if (!(flags & SHUTDOWN_FORCE_UMOUNT)) {
		xfs_notice(mp,
	"%s(0x%x) called from line %d of file %s.  Return address = 0x%p",
			__func__, flags, lnnum, fname, __return_address);
	}
	/*
	 * No need to duplicate efforts.
	 */
	if (XFS_FORCED_SHUTDOWN(mp) && !logerror)
		return;

	/*
	 * This flags XFS_MOUNT_FS_SHUTDOWN, makes sure that we don't
	 * queue up anybody new on the log reservations, and wakes up
	 * everybody who's sleeping on log reservations to tell them
	 * the bad news.
	 */
	if (xfs_log_force_umount(mp, logerror))
		return;

	if (flags & SHUTDOWN_CORRUPT_INCORE) {
		xfs_alert_tag(mp, XFS_PTAG_SHUTDOWN_CORRUPT,
    "Corruption of in-memory data detected.  Shutting down filesystem");
		if (XFS_ERRLEVEL_HIGH <= xfs_error_level)
			xfs_stack_trace();
	} else if (!(flags & SHUTDOWN_FORCE_UMOUNT)) {
		if (logerror) {
			xfs_alert_tag(mp, XFS_PTAG_SHUTDOWN_LOGERROR,
		"Log I/O Error Detected.  Shutting down filesystem");
		} else if (flags & SHUTDOWN_DEVICE_REQ) {
			xfs_alert_tag(mp, XFS_PTAG_SHUTDOWN_IOERROR,
		"All device paths lost.  Shutting down filesystem");
		} else if (!(flags & SHUTDOWN_REMOTE_REQ)) {
			xfs_alert_tag(mp, XFS_PTAG_SHUTDOWN_IOERROR,
		"I/O Error Detected. Shutting down filesystem");
		}
	}
	if (!(flags & SHUTDOWN_FORCE_UMOUNT)) {
		xfs_alert(mp,
	"Please umount the filesystem and rectify the problem(s)");
	}
}

/*
 * helper function to extract extent size hint from inode
 */
xfs_extlen_t
xfs_get_extsz_hint(
	struct xfs_inode	*ip)
{
	if ((ip->i_d.di_flags & XFS_DIFLAG_EXTSIZE) && ip->i_d.di_extsize)
		return ip->i_d.di_extsize;
	if (XFS_IS_REALTIME_INODE(ip))
		return ip->i_mount->m_sb.sb_rextsize;
	return 0;
}
