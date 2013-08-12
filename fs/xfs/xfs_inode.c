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
#include <linux/log2.h>

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_buf_item.h"
#include "xfs_inode_item.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_utils.h"
#include "xfs_quota.h"
#include "xfs_filestream.h"
#include "xfs_vnodeops.h"
#include "xfs_cksum.h"
#include "xfs_trace.h"
#include "xfs_icache.h"

kmem_zone_t *xfs_inode_zone;

/*
 * Used in xfs_itruncate_extents().  This is the maximum number of extents
 * freed from a file in a single transaction.
 */
#define	XFS_ITRUNC_MAX_EXTENTS	2

STATIC int xfs_iflush_int(xfs_inode_t *, xfs_buf_t *);

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

/*
 * This is a wrapper routine around the xfs_ilock() routine used to centralize
 * some grungy code.  It is used in places that wish to lock the inode solely
 * for reading the extents.  The reason these places can't just call
 * xfs_ilock(SHARED) is that the inode lock also guards to bringing in of the
 * extents from disk for a file in b-tree format.  If the inode is in b-tree
 * format, then we need to lock the inode exclusively until the extents are read
 * in.  Locking it exclusively all the time would limit our parallelism
 * unnecessarily, though.  What we do instead is check to see if the extents
 * have been read in yet, and only lock the inode exclusively if they have not.
 *
 * The function returns a value which should be given to the corresponding
 * xfs_iunlock_map_shared().  This value is the mode in which the lock was
 * actually taken.
 */
uint
xfs_ilock_map_shared(
	xfs_inode_t	*ip)
{
	uint	lock_mode;

	if ((ip->i_d.di_format == XFS_DINODE_FMT_BTREE) &&
	    ((ip->i_df.if_flags & XFS_IFEXTENTS) == 0)) {
		lock_mode = XFS_ILOCK_EXCL;
	} else {
		lock_mode = XFS_ILOCK_SHARED;
	}

	xfs_ilock(ip, lock_mode);

	return lock_mode;
}

/*
 * This is simply the unlock routine to go with xfs_ilock_map_shared().
 * All it does is call xfs_iunlock() with the given lock_mode.
 */
void
xfs_iunlock_map_shared(
	xfs_inode_t	*ip,
	unsigned int	lock_mode)
{
	xfs_iunlock(ip, lock_mode);
}

/*
 * The xfs inode contains 2 locks: a multi-reader lock called the
 * i_iolock and a multi-reader lock called the i_lock.  This routine
 * allows either or both of the locks to be obtained.
 *
 * The 2 locks should always be ordered so that the IO lock is
 * obtained first in order to prevent deadlock.
 *
 * ip -- the inode being locked
 * lock_flags -- this parameter indicates the inode's locks
 *       to be locked.  It can be:
 *		XFS_IOLOCK_SHARED,
 *		XFS_IOLOCK_EXCL,
 *		XFS_ILOCK_SHARED,
 *		XFS_ILOCK_EXCL,
 *		XFS_IOLOCK_SHARED | XFS_ILOCK_SHARED,
 *		XFS_IOLOCK_SHARED | XFS_ILOCK_EXCL,
 *		XFS_IOLOCK_EXCL | XFS_ILOCK_SHARED,
 *		XFS_IOLOCK_EXCL | XFS_ILOCK_EXCL
 */
void
xfs_ilock(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	trace_xfs_ilock(ip, lock_flags, _RET_IP_);

	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_DEP_MASK)) == 0);

	if (lock_flags & XFS_IOLOCK_EXCL)
		mrupdate_nested(&ip->i_iolock, XFS_IOLOCK_DEP(lock_flags));
	else if (lock_flags & XFS_IOLOCK_SHARED)
		mraccess_nested(&ip->i_iolock, XFS_IOLOCK_DEP(lock_flags));

	if (lock_flags & XFS_ILOCK_EXCL)
		mrupdate_nested(&ip->i_lock, XFS_ILOCK_DEP(lock_flags));
	else if (lock_flags & XFS_ILOCK_SHARED)
		mraccess_nested(&ip->i_lock, XFS_ILOCK_DEP(lock_flags));
}

/*
 * This is just like xfs_ilock(), except that the caller
 * is guaranteed not to sleep.  It returns 1 if it gets
 * the requested locks and 0 otherwise.  If the IO lock is
 * obtained but the inode lock cannot be, then the IO lock
 * is dropped before returning.
 *
 * ip -- the inode being locked
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be locked.  See the comment for xfs_ilock() for a list
 *	 of valid values.
 */
int
xfs_ilock_nowait(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	trace_xfs_ilock_nowait(ip, lock_flags, _RET_IP_);

	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_DEP_MASK)) == 0);

	if (lock_flags & XFS_IOLOCK_EXCL) {
		if (!mrtryupdate(&ip->i_iolock))
			goto out;
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		if (!mrtryaccess(&ip->i_iolock))
			goto out;
	}
	if (lock_flags & XFS_ILOCK_EXCL) {
		if (!mrtryupdate(&ip->i_lock))
			goto out_undo_iolock;
	} else if (lock_flags & XFS_ILOCK_SHARED) {
		if (!mrtryaccess(&ip->i_lock))
			goto out_undo_iolock;
	}
	return 1;

 out_undo_iolock:
	if (lock_flags & XFS_IOLOCK_EXCL)
		mrunlock_excl(&ip->i_iolock);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		mrunlock_shared(&ip->i_iolock);
 out:
	return 0;
}

/*
 * xfs_iunlock() is used to drop the inode locks acquired with
 * xfs_ilock() and xfs_ilock_nowait().  The caller must pass
 * in the flags given to xfs_ilock() or xfs_ilock_nowait() so
 * that we know which locks to drop.
 *
 * ip -- the inode being unlocked
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be unlocked.  See the comment for xfs_ilock() for a list
 *	 of valid values for this parameter.
 *
 */
void
xfs_iunlock(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	/*
	 * You can't set both SHARED and EXCL for the same lock,
	 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_ILOCK_SHARED,
	 * and XFS_ILOCK_EXCL are valid values to set in lock_flags.
	 */
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
	       (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_DEP_MASK)) == 0);
	ASSERT(lock_flags != 0);

	if (lock_flags & XFS_IOLOCK_EXCL)
		mrunlock_excl(&ip->i_iolock);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		mrunlock_shared(&ip->i_iolock);

	if (lock_flags & XFS_ILOCK_EXCL)
		mrunlock_excl(&ip->i_lock);
	else if (lock_flags & XFS_ILOCK_SHARED)
		mrunlock_shared(&ip->i_lock);

	trace_xfs_iunlock(ip, lock_flags, _RET_IP_);
}

/*
 * give up write locks.  the i/o lock cannot be held nested
 * if it is being demoted.
 */
void
xfs_ilock_demote(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	ASSERT(lock_flags & (XFS_IOLOCK_EXCL|XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_IOLOCK_EXCL|XFS_ILOCK_EXCL)) == 0);

	if (lock_flags & XFS_ILOCK_EXCL)
		mrdemote(&ip->i_lock);
	if (lock_flags & XFS_IOLOCK_EXCL)
		mrdemote(&ip->i_iolock);

	trace_xfs_ilock_demote(ip, lock_flags, _RET_IP_);
}

#if defined(DEBUG) || defined(XFS_WARN)
int
xfs_isilocked(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	if (lock_flags & (XFS_ILOCK_EXCL|XFS_ILOCK_SHARED)) {
		if (!(lock_flags & XFS_ILOCK_SHARED))
			return !!ip->i_lock.mr_writer;
		return rwsem_is_locked(&ip->i_lock.mr_lock);
	}

	if (lock_flags & (XFS_IOLOCK_EXCL|XFS_IOLOCK_SHARED)) {
		if (!(lock_flags & XFS_IOLOCK_SHARED))
			return !!ip->i_iolock.mr_writer;
		return rwsem_is_locked(&ip->i_iolock.mr_lock);
	}

	ASSERT(0);
	return 0;
}
#endif

void
__xfs_iflock(
	struct xfs_inode	*ip)
{
	wait_queue_head_t *wq = bit_waitqueue(&ip->i_flags, __XFS_IFLOCK_BIT);
	DEFINE_WAIT_BIT(wait, &ip->i_flags, __XFS_IFLOCK_BIT);

	do {
		prepare_to_wait_exclusive(wq, &wait.wait, TASK_UNINTERRUPTIBLE);
		if (xfs_isiflocked(ip))
			io_schedule();
	} while (!xfs_iflock_nowait(ip));

	finish_wait(wq, &wait.wait);
}

STATIC uint
_xfs_dic2xflags(
	__uint16_t		di_flags)
{
	uint			flags = 0;

	if (di_flags & XFS_DIFLAG_ANY) {
		if (di_flags & XFS_DIFLAG_REALTIME)
			flags |= XFS_XFLAG_REALTIME;
		if (di_flags & XFS_DIFLAG_PREALLOC)
			flags |= XFS_XFLAG_PREALLOC;
		if (di_flags & XFS_DIFLAG_IMMUTABLE)
			flags |= XFS_XFLAG_IMMUTABLE;
		if (di_flags & XFS_DIFLAG_APPEND)
			flags |= XFS_XFLAG_APPEND;
		if (di_flags & XFS_DIFLAG_SYNC)
			flags |= XFS_XFLAG_SYNC;
		if (di_flags & XFS_DIFLAG_NOATIME)
			flags |= XFS_XFLAG_NOATIME;
		if (di_flags & XFS_DIFLAG_NODUMP)
			flags |= XFS_XFLAG_NODUMP;
		if (di_flags & XFS_DIFLAG_RTINHERIT)
			flags |= XFS_XFLAG_RTINHERIT;
		if (di_flags & XFS_DIFLAG_PROJINHERIT)
			flags |= XFS_XFLAG_PROJINHERIT;
		if (di_flags & XFS_DIFLAG_NOSYMLINKS)
			flags |= XFS_XFLAG_NOSYMLINKS;
		if (di_flags & XFS_DIFLAG_EXTSIZE)
			flags |= XFS_XFLAG_EXTSIZE;
		if (di_flags & XFS_DIFLAG_EXTSZINHERIT)
			flags |= XFS_XFLAG_EXTSZINHERIT;
		if (di_flags & XFS_DIFLAG_NODEFRAG)
			flags |= XFS_XFLAG_NODEFRAG;
		if (di_flags & XFS_DIFLAG_FILESTREAM)
			flags |= XFS_XFLAG_FILESTREAM;
	}

	return flags;
}

uint
xfs_ip2xflags(
	xfs_inode_t		*ip)
{
	xfs_icdinode_t		*dic = &ip->i_d;

	return _xfs_dic2xflags(dic->di_flags) |
				(XFS_IFORK_Q(ip) ? XFS_XFLAG_HASATTR : 0);
}

uint
xfs_dic2xflags(
	xfs_dinode_t		*dip)
{
	return _xfs_dic2xflags(be16_to_cpu(dip->di_flags)) |
				(XFS_DFORK_Q(dip) ? XFS_XFLAG_HASATTR : 0);
}

/*
 * Allocate an inode on disk and return a copy of its in-core version.
 * The in-core inode is locked exclusively.  Set mode, nlink, and rdev
 * appropriately within the inode.  The uid and gid for the inode are
 * set according to the contents of the given cred structure.
 *
 * Use xfs_dialloc() to allocate the on-disk inode. If xfs_dialloc()
 * has a free inode available, call xfs_iget() to obtain the in-core
 * version of the allocated inode.  Finally, fill in the inode and
 * log its initial contents.  In this case, ialloc_context would be
 * set to NULL.
 *
 * If xfs_dialloc() does not have an available inode, it will replenish
 * its supply by doing an allocation. Since we can only do one
 * allocation within a transaction without deadlocks, we must commit
 * the current transaction before returning the inode itself.
 * In this case, therefore, we will set ialloc_context and return.
 * The caller should then commit the current transaction, start a new
 * transaction, and call xfs_ialloc() again to actually get the inode.
 *
 * To ensure that some other process does not grab the inode that
 * was allocated during the first call to xfs_ialloc(), this routine
 * also returns the [locked] bp pointing to the head of the freelist
 * as ialloc_context.  The caller should hold this buffer across
 * the commit and pass it back into this routine on the second call.
 *
 * If we are allocating quota inodes, we do not have a parent inode
 * to attach to or associate with (i.e. pip == NULL) because they
 * are not linked into the directory structure - they are attached
 * directly to the superblock - and so have no parent.
 */
int
xfs_ialloc(
	xfs_trans_t	*tp,
	xfs_inode_t	*pip,
	umode_t		mode,
	xfs_nlink_t	nlink,
	xfs_dev_t	rdev,
	prid_t		prid,
	int		okalloc,
	xfs_buf_t	**ialloc_context,
	xfs_inode_t	**ipp)
{
	struct xfs_mount *mp = tp->t_mountp;
	xfs_ino_t	ino;
	xfs_inode_t	*ip;
	uint		flags;
	int		error;
	timespec_t	tv;
	int		filestreams = 0;

	/*
	 * Call the space management code to pick
	 * the on-disk inode to be allocated.
	 */
	error = xfs_dialloc(tp, pip ? pip->i_ino : 0, mode, okalloc,
			    ialloc_context, &ino);
	if (error)
		return error;
	if (*ialloc_context || ino == NULLFSINO) {
		*ipp = NULL;
		return 0;
	}
	ASSERT(*ialloc_context == NULL);

	/*
	 * Get the in-core inode with the lock held exclusively.
	 * This is because we're setting fields here we need
	 * to prevent others from looking at until we're done.
	 */
	error = xfs_iget(mp, tp, ino, XFS_IGET_CREATE,
			 XFS_ILOCK_EXCL, &ip);
	if (error)
		return error;
	ASSERT(ip != NULL);

	ip->i_d.di_mode = mode;
	ip->i_d.di_onlink = 0;
	ip->i_d.di_nlink = nlink;
	ASSERT(ip->i_d.di_nlink == nlink);
	ip->i_d.di_uid = current_fsuid();
	ip->i_d.di_gid = current_fsgid();
	xfs_set_projid(ip, prid);
	memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));

	/*
	 * If the superblock version is up to where we support new format
	 * inodes and this is currently an old format inode, then change
	 * the inode version number now.  This way we only do the conversion
	 * here rather than here and in the flush/logging code.
	 */
	if (xfs_sb_version_hasnlink(&mp->m_sb) &&
	    ip->i_d.di_version == 1) {
		ip->i_d.di_version = 2;
		/*
		 * We've already zeroed the old link count, the projid field,
		 * and the pad field.
		 */
	}

	/*
	 * Project ids won't be stored on disk if we are using a version 1 inode.
	 */
	if ((prid != 0) && (ip->i_d.di_version == 1))
		xfs_bump_ino_vers2(tp, ip);

	if (pip && XFS_INHERIT_GID(pip)) {
		ip->i_d.di_gid = pip->i_d.di_gid;
		if ((pip->i_d.di_mode & S_ISGID) && S_ISDIR(mode)) {
			ip->i_d.di_mode |= S_ISGID;
		}
	}

	/*
	 * If the group ID of the new file does not match the effective group
	 * ID or one of the supplementary group IDs, the S_ISGID bit is cleared
	 * (and only if the irix_sgid_inherit compatibility variable is set).
	 */
	if ((irix_sgid_inherit) &&
	    (ip->i_d.di_mode & S_ISGID) &&
	    (!in_group_p((gid_t)ip->i_d.di_gid))) {
		ip->i_d.di_mode &= ~S_ISGID;
	}

	ip->i_d.di_size = 0;
	ip->i_d.di_nextents = 0;
	ASSERT(ip->i_d.di_nblocks == 0);

	nanotime(&tv);
	ip->i_d.di_mtime.t_sec = (__int32_t)tv.tv_sec;
	ip->i_d.di_mtime.t_nsec = (__int32_t)tv.tv_nsec;
	ip->i_d.di_atime = ip->i_d.di_mtime;
	ip->i_d.di_ctime = ip->i_d.di_mtime;

	/*
	 * di_gen will have been taken care of in xfs_iread.
	 */
	ip->i_d.di_extsize = 0;
	ip->i_d.di_dmevmask = 0;
	ip->i_d.di_dmstate = 0;
	ip->i_d.di_flags = 0;

	if (ip->i_d.di_version == 3) {
		ASSERT(ip->i_d.di_ino == ino);
		ASSERT(uuid_equal(&ip->i_d.di_uuid, &mp->m_sb.sb_uuid));
		ip->i_d.di_crc = 0;
		ip->i_d.di_changecount = 1;
		ip->i_d.di_lsn = 0;
		ip->i_d.di_flags2 = 0;
		memset(&(ip->i_d.di_pad2[0]), 0, sizeof(ip->i_d.di_pad2));
		ip->i_d.di_crtime = ip->i_d.di_mtime;
	}


	flags = XFS_ILOG_CORE;
	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		ip->i_d.di_format = XFS_DINODE_FMT_DEV;
		ip->i_df.if_u2.if_rdev = rdev;
		ip->i_df.if_flags = 0;
		flags |= XFS_ILOG_DEV;
		break;
	case S_IFREG:
		/*
		 * we can't set up filestreams until after the VFS inode
		 * is set up properly.
		 */
		if (pip && xfs_inode_is_filestream(pip))
			filestreams = 1;
		/* fall through */
	case S_IFDIR:
		if (pip && (pip->i_d.di_flags & XFS_DIFLAG_ANY)) {
			uint	di_flags = 0;

			if (S_ISDIR(mode)) {
				if (pip->i_d.di_flags & XFS_DIFLAG_RTINHERIT)
					di_flags |= XFS_DIFLAG_RTINHERIT;
				if (pip->i_d.di_flags & XFS_DIFLAG_EXTSZINHERIT) {
					di_flags |= XFS_DIFLAG_EXTSZINHERIT;
					ip->i_d.di_extsize = pip->i_d.di_extsize;
				}
			} else if (S_ISREG(mode)) {
				if (pip->i_d.di_flags & XFS_DIFLAG_RTINHERIT)
					di_flags |= XFS_DIFLAG_REALTIME;
				if (pip->i_d.di_flags & XFS_DIFLAG_EXTSZINHERIT) {
					di_flags |= XFS_DIFLAG_EXTSIZE;
					ip->i_d.di_extsize = pip->i_d.di_extsize;
				}
			}
			if ((pip->i_d.di_flags & XFS_DIFLAG_NOATIME) &&
			    xfs_inherit_noatime)
				di_flags |= XFS_DIFLAG_NOATIME;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NODUMP) &&
			    xfs_inherit_nodump)
				di_flags |= XFS_DIFLAG_NODUMP;
			if ((pip->i_d.di_flags & XFS_DIFLAG_SYNC) &&
			    xfs_inherit_sync)
				di_flags |= XFS_DIFLAG_SYNC;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NOSYMLINKS) &&
			    xfs_inherit_nosymlinks)
				di_flags |= XFS_DIFLAG_NOSYMLINKS;
			if (pip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
				di_flags |= XFS_DIFLAG_PROJINHERIT;
			if ((pip->i_d.di_flags & XFS_DIFLAG_NODEFRAG) &&
			    xfs_inherit_nodefrag)
				di_flags |= XFS_DIFLAG_NODEFRAG;
			if (pip->i_d.di_flags & XFS_DIFLAG_FILESTREAM)
				di_flags |= XFS_DIFLAG_FILESTREAM;
			ip->i_d.di_flags |= di_flags;
		}
		/* FALLTHROUGH */
	case S_IFLNK:
		ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
		ip->i_df.if_flags = XFS_IFEXTENTS;
		ip->i_df.if_bytes = ip->i_df.if_real_bytes = 0;
		ip->i_df.if_u1.if_extents = NULL;
		break;
	default:
		ASSERT(0);
	}
	/*
	 * Attribute fork settings for new inode.
	 */
	ip->i_d.di_aformat = XFS_DINODE_FMT_EXTENTS;
	ip->i_d.di_anextents = 0;

	/*
	 * Log the new values stuffed into the inode.
	 */
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, flags);

	/* now that we have an i_mode we can setup inode ops and unlock */
	xfs_setup_inode(ip);

	/* now we have set up the vfs inode we can associate the filestream */
	if (filestreams) {
		error = xfs_filestream_associate(pip, ip);
		if (error < 0)
			return -error;
		if (!error)
			xfs_iflags_set(ip, XFS_IFILESTREAM);
	}

	*ipp = ip;
	return 0;
}

/*
 * Free up the underlying blocks past new_size.  The new size must be smaller
 * than the current size.  This routine can be used both for the attribute and
 * data fork, and does not modify the inode size, which is left to the caller.
 *
 * The transaction passed to this routine must have made a permanent log
 * reservation of at least XFS_ITRUNCATE_LOG_RES.  This routine may commit the
 * given transaction and start new ones, so make sure everything involved in
 * the transaction is tidy before calling here.  Some transaction will be
 * returned to the caller to be committed.  The incoming transaction must
 * already include the inode, and both inode locks must be held exclusively.
 * The inode must also be "held" within the transaction.  On return the inode
 * will be "held" within the returned transaction.  This routine does NOT
 * require any disk space to be reserved for it within the transaction.
 *
 * If we get an error, we must return with the inode locked and linked into the
 * current transaction. This keeps things simple for the higher level code,
 * because it always knows that the inode is locked and held in the transaction
 * that returns to it whether errors occur or not.  We don't mark the inode
 * dirty on error so that transactions can be easily aborted if possible.
 */
int
xfs_itruncate_extents(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fsize_t		new_size)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp = *tpp;
	struct xfs_trans	*ntp;
	xfs_bmap_free_t		free_list;
	xfs_fsblock_t		first_block;
	xfs_fileoff_t		first_unmap_block;
	xfs_fileoff_t		last_block;
	xfs_filblks_t		unmap_len;
	int			committed;
	int			error = 0;
	int			done = 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(!atomic_read(&VFS_I(ip)->i_count) ||
	       xfs_isilocked(ip, XFS_IOLOCK_EXCL));
	ASSERT(new_size <= XFS_ISIZE(ip));
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ip->i_itemp->ili_lock_flags == 0);
	ASSERT(!XFS_NOT_DQATTACHED(mp, ip));

	trace_xfs_itruncate_extents_start(ip, new_size);

	/*
	 * Since it is possible for space to become allocated beyond
	 * the end of the file (in a crash where the space is allocated
	 * but the inode size is not yet updated), simply remove any
	 * blocks which show up between the new EOF and the maximum
	 * possible file size.  If the first block to be removed is
	 * beyond the maximum file size (ie it is the same as last_block),
	 * then there is nothing to do.
	 */
	first_unmap_block = XFS_B_TO_FSB(mp, (xfs_ufsize_t)new_size);
	last_block = XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes);
	if (first_unmap_block == last_block)
		return 0;

	ASSERT(first_unmap_block < last_block);
	unmap_len = last_block - first_unmap_block + 1;
	while (!done) {
		xfs_bmap_init(&free_list, &first_block);
		error = xfs_bunmapi(tp, ip,
				    first_unmap_block, unmap_len,
				    xfs_bmapi_aflag(whichfork),
				    XFS_ITRUNC_MAX_EXTENTS,
				    &first_block, &free_list,
				    &done);
		if (error)
			goto out_bmap_cancel;

		/*
		 * Duplicate the transaction that has the permanent
		 * reservation and commit the old transaction.
		 */
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (committed)
			xfs_trans_ijoin(tp, ip, 0);
		if (error)
			goto out_bmap_cancel;

		if (committed) {
			/*
			 * Mark the inode dirty so it will be logged and
			 * moved forward in the log as part of every commit.
			 */
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		}

		ntp = xfs_trans_dup(tp);
		error = xfs_trans_commit(tp, 0);
		tp = ntp;

		xfs_trans_ijoin(tp, ip, 0);

		if (error)
			goto out;

		/*
		 * Transaction commit worked ok so we can drop the extra ticket
		 * reference that we gained in xfs_trans_dup()
		 */
		xfs_log_ticket_put(tp->t_ticket);
		error = xfs_trans_reserve(tp, 0,
					XFS_ITRUNCATE_LOG_RES(mp), 0,
					XFS_TRANS_PERM_LOG_RES,
					XFS_ITRUNCATE_LOG_COUNT);
		if (error)
			goto out;
	}

	/*
	 * Always re-log the inode so that our permanent transaction can keep
	 * on rolling it forward in the log.
	 */
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	trace_xfs_itruncate_extents_end(ip, new_size);

out:
	*tpp = tp;
	return error;
out_bmap_cancel:
	/*
	 * If the bunmapi call encounters an error, return to the caller where
	 * the transaction can be properly aborted.  We just need to make sure
	 * we're not holding any resources that we were not when we came in.
	 */
	xfs_bmap_cancel(&free_list);
	goto out;
}

/*
 * This is called when the inode's link count goes to 0.
 * We place the on-disk inode on a list in the AGI.  It
 * will be pulled from this list when the inode is freed.
 */
int
xfs_iunlink(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	xfs_buf_t	*agibp;
	xfs_buf_t	*ibp;
	xfs_agino_t	agino;
	short		bucket_index;
	int		offset;
	int		error;

	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_mode != 0);

	mp = tp->t_mountp;

	/*
	 * Get the agi buffer first.  It ensures lock ordering
	 * on the list.
	 */
	error = xfs_read_agi(mp, tp, XFS_INO_TO_AGNO(mp, ip->i_ino), &agibp);
	if (error)
		return error;
	agi = XFS_BUF_TO_AGI(agibp);

	/*
	 * Get the index into the agi hash table for the
	 * list this inode will go on.
	 */
	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	ASSERT(agino != 0);
	bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	ASSERT(agi->agi_unlinked[bucket_index]);
	ASSERT(be32_to_cpu(agi->agi_unlinked[bucket_index]) != agino);

	if (agi->agi_unlinked[bucket_index] != cpu_to_be32(NULLAGINO)) {
		/*
		 * There is already another inode in the bucket we need
		 * to add ourselves to.  Add us at the front of the list.
		 * Here we put the head pointer into our next pointer,
		 * and then we fall through to point the head at us.
		 */
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error)
			return error;

		ASSERT(dip->di_next_unlinked == cpu_to_be32(NULLAGINO));
		dip->di_next_unlinked = agi->agi_unlinked[bucket_index];
		offset = ip->i_imap.im_boffset +
			offsetof(xfs_dinode_t, di_next_unlinked);

		/* need to recalc the inode CRC if appropriate */
		xfs_dinode_calc_crc(mp, dip);

		xfs_trans_inode_buf(tp, ibp);
		xfs_trans_log_buf(tp, ibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
		xfs_inobp_check(mp, ibp);
	}

	/*
	 * Point the bucket head pointer at the inode being inserted.
	 */
	ASSERT(agino != 0);
	agi->agi_unlinked[bucket_index] = cpu_to_be32(agino);
	offset = offsetof(xfs_agi_t, agi_unlinked) +
		(sizeof(xfs_agino_t) * bucket_index);
	xfs_trans_log_buf(tp, agibp, offset,
			  (offset + sizeof(xfs_agino_t) - 1));
	return 0;
}

/*
 * Pull the on-disk inode from the AGI unlinked list.
 */
STATIC int
xfs_iunlink_remove(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	xfs_ino_t	next_ino;
	xfs_mount_t	*mp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	xfs_buf_t	*agibp;
	xfs_buf_t	*ibp;
	xfs_agnumber_t	agno;
	xfs_agino_t	agino;
	xfs_agino_t	next_agino;
	xfs_buf_t	*last_ibp;
	xfs_dinode_t	*last_dip = NULL;
	short		bucket_index;
	int		offset, last_offset = 0;
	int		error;

	mp = tp->t_mountp;
	agno = XFS_INO_TO_AGNO(mp, ip->i_ino);

	/*
	 * Get the agi buffer first.  It ensures lock ordering
	 * on the list.
	 */
	error = xfs_read_agi(mp, tp, agno, &agibp);
	if (error)
		return error;

	agi = XFS_BUF_TO_AGI(agibp);

	/*
	 * Get the index into the agi hash table for the
	 * list this inode will go on.
	 */
	agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	ASSERT(agino != 0);
	bucket_index = agino % XFS_AGI_UNLINKED_BUCKETS;
	ASSERT(agi->agi_unlinked[bucket_index] != cpu_to_be32(NULLAGINO));
	ASSERT(agi->agi_unlinked[bucket_index]);

	if (be32_to_cpu(agi->agi_unlinked[bucket_index]) == agino) {
		/*
		 * We're at the head of the list.  Get the inode's on-disk
		 * buffer to see if there is anyone after us on the list.
		 * Only modify our next pointer if it is not already NULLAGINO.
		 * This saves us the overhead of dealing with the buffer when
		 * there is no need to change it.
		 */
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error) {
			xfs_warn(mp, "%s: xfs_imap_to_bp returned error %d.",
				__func__, error);
			return error;
		}
		next_agino = be32_to_cpu(dip->di_next_unlinked);
		ASSERT(next_agino != 0);
		if (next_agino != NULLAGINO) {
			dip->di_next_unlinked = cpu_to_be32(NULLAGINO);
			offset = ip->i_imap.im_boffset +
				offsetof(xfs_dinode_t, di_next_unlinked);

			/* need to recalc the inode CRC if appropriate */
			xfs_dinode_calc_crc(mp, dip);

			xfs_trans_inode_buf(tp, ibp);
			xfs_trans_log_buf(tp, ibp, offset,
					  (offset + sizeof(xfs_agino_t) - 1));
			xfs_inobp_check(mp, ibp);
		} else {
			xfs_trans_brelse(tp, ibp);
		}
		/*
		 * Point the bucket head pointer at the next inode.
		 */
		ASSERT(next_agino != 0);
		ASSERT(next_agino != agino);
		agi->agi_unlinked[bucket_index] = cpu_to_be32(next_agino);
		offset = offsetof(xfs_agi_t, agi_unlinked) +
			(sizeof(xfs_agino_t) * bucket_index);
		xfs_trans_log_buf(tp, agibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
	} else {
		/*
		 * We need to search the list for the inode being freed.
		 */
		next_agino = be32_to_cpu(agi->agi_unlinked[bucket_index]);
		last_ibp = NULL;
		while (next_agino != agino) {
			struct xfs_imap	imap;

			if (last_ibp)
				xfs_trans_brelse(tp, last_ibp);

			imap.im_blkno = 0;
			next_ino = XFS_AGINO_TO_INO(mp, agno, next_agino);

			error = xfs_imap(mp, tp, next_ino, &imap, 0);
			if (error) {
				xfs_warn(mp,
	"%s: xfs_imap returned error %d.",
					 __func__, error);
				return error;
			}

			error = xfs_imap_to_bp(mp, tp, &imap, &last_dip,
					       &last_ibp, 0, 0);
			if (error) {
				xfs_warn(mp,
	"%s: xfs_imap_to_bp returned error %d.",
					__func__, error);
				return error;
			}

			last_offset = imap.im_boffset;
			next_agino = be32_to_cpu(last_dip->di_next_unlinked);
			ASSERT(next_agino != NULLAGINO);
			ASSERT(next_agino != 0);
		}

		/*
		 * Now last_ibp points to the buffer previous to us on the
		 * unlinked list.  Pull us from the list.
		 */
		error = xfs_imap_to_bp(mp, tp, &ip->i_imap, &dip, &ibp,
				       0, 0);
		if (error) {
			xfs_warn(mp, "%s: xfs_imap_to_bp(2) returned error %d.",
				__func__, error);
			return error;
		}
		next_agino = be32_to_cpu(dip->di_next_unlinked);
		ASSERT(next_agino != 0);
		ASSERT(next_agino != agino);
		if (next_agino != NULLAGINO) {
			dip->di_next_unlinked = cpu_to_be32(NULLAGINO);
			offset = ip->i_imap.im_boffset +
				offsetof(xfs_dinode_t, di_next_unlinked);

			/* need to recalc the inode CRC if appropriate */
			xfs_dinode_calc_crc(mp, dip);

			xfs_trans_inode_buf(tp, ibp);
			xfs_trans_log_buf(tp, ibp, offset,
					  (offset + sizeof(xfs_agino_t) - 1));
			xfs_inobp_check(mp, ibp);
		} else {
			xfs_trans_brelse(tp, ibp);
		}
		/*
		 * Point the previous inode on the list to the next inode.
		 */
		last_dip->di_next_unlinked = cpu_to_be32(next_agino);
		ASSERT(next_agino != 0);
		offset = last_offset + offsetof(xfs_dinode_t, di_next_unlinked);

		/* need to recalc the inode CRC if appropriate */
		xfs_dinode_calc_crc(mp, last_dip);

		xfs_trans_inode_buf(tp, last_ibp);
		xfs_trans_log_buf(tp, last_ibp, offset,
				  (offset + sizeof(xfs_agino_t) - 1));
		xfs_inobp_check(mp, last_ibp);
	}
	return 0;
}

/*
 * A big issue when freeing the inode cluster is is that we _cannot_ skip any
 * inodes that are in memory - they all must be marked stale and attached to
 * the cluster buffer.
 */
STATIC int
xfs_ifree_cluster(
	xfs_inode_t	*free_ip,
	xfs_trans_t	*tp,
	xfs_ino_t	inum)
{
	xfs_mount_t		*mp = free_ip->i_mount;
	int			blks_per_cluster;
	int			nbufs;
	int			ninodes;
	int			i, j;
	xfs_daddr_t		blkno;
	xfs_buf_t		*bp;
	xfs_inode_t		*ip;
	xfs_inode_log_item_t	*iip;
	xfs_log_item_t		*lip;
	struct xfs_perag	*pag;

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, inum));
	if (mp->m_sb.sb_blocksize >= XFS_INODE_CLUSTER_SIZE(mp)) {
		blks_per_cluster = 1;
		ninodes = mp->m_sb.sb_inopblock;
		nbufs = XFS_IALLOC_BLOCKS(mp);
	} else {
		blks_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) /
					mp->m_sb.sb_blocksize;
		ninodes = blks_per_cluster * mp->m_sb.sb_inopblock;
		nbufs = XFS_IALLOC_BLOCKS(mp) / blks_per_cluster;
	}

	for (j = 0; j < nbufs; j++, inum += ninodes) {
		blkno = XFS_AGB_TO_DADDR(mp, XFS_INO_TO_AGNO(mp, inum),
					 XFS_INO_TO_AGBNO(mp, inum));

		/*
		 * We obtain and lock the backing buffer first in the process
		 * here, as we have to ensure that any dirty inode that we
		 * can't get the flush lock on is attached to the buffer.
		 * If we scan the in-memory inodes first, then buffer IO can
		 * complete before we get a lock on it, and hence we may fail
		 * to mark all the active inodes on the buffer stale.
		 */
		bp = xfs_trans_get_buf(tp, mp->m_ddev_targp, blkno,
					mp->m_bsize * blks_per_cluster,
					XBF_UNMAPPED);

		if (!bp)
			return ENOMEM;

		/*
		 * This buffer may not have been correctly initialised as we
		 * didn't read it from disk. That's not important because we are
		 * only using to mark the buffer as stale in the log, and to
		 * attach stale cached inodes on it. That means it will never be
		 * dispatched for IO. If it is, we want to know about it, and we
		 * want it to fail. We can acheive this by adding a write
		 * verifier to the buffer.
		 */
		 bp->b_ops = &xfs_inode_buf_ops;

		/*
		 * Walk the inodes already attached to the buffer and mark them
		 * stale. These will all have the flush locks held, so an
		 * in-memory inode walk can't lock them. By marking them all
		 * stale first, we will not attempt to lock them in the loop
		 * below as the XFS_ISTALE flag will be set.
		 */
		lip = bp->b_fspriv;
		while (lip) {
			if (lip->li_type == XFS_LI_INODE) {
				iip = (xfs_inode_log_item_t *)lip;
				ASSERT(iip->ili_logged == 1);
				lip->li_cb = xfs_istale_done;
				xfs_trans_ail_copy_lsn(mp->m_ail,
							&iip->ili_flush_lsn,
							&iip->ili_item.li_lsn);
				xfs_iflags_set(iip->ili_inode, XFS_ISTALE);
			}
			lip = lip->li_bio_list;
		}


		/*
		 * For each inode in memory attempt to add it to the inode
		 * buffer and set it up for being staled on buffer IO
		 * completion.  This is safe as we've locked out tail pushing
		 * and flushing by locking the buffer.
		 *
		 * We have already marked every inode that was part of a
		 * transaction stale above, which means there is no point in
		 * even trying to lock them.
		 */
		for (i = 0; i < ninodes; i++) {
retry:
			rcu_read_lock();
			ip = radix_tree_lookup(&pag->pag_ici_root,
					XFS_INO_TO_AGINO(mp, (inum + i)));

			/* Inode not in memory, nothing to do */
			if (!ip) {
				rcu_read_unlock();
				continue;
			}

			/*
			 * because this is an RCU protected lookup, we could
			 * find a recently freed or even reallocated inode
			 * during the lookup. We need to check under the
			 * i_flags_lock for a valid inode here. Skip it if it
			 * is not valid, the wrong inode or stale.
			 */
			spin_lock(&ip->i_flags_lock);
			if (ip->i_ino != inum + i ||
			    __xfs_iflags_test(ip, XFS_ISTALE)) {
				spin_unlock(&ip->i_flags_lock);
				rcu_read_unlock();
				continue;
			}
			spin_unlock(&ip->i_flags_lock);

			/*
			 * Don't try to lock/unlock the current inode, but we
			 * _cannot_ skip the other inodes that we did not find
			 * in the list attached to the buffer and are not
			 * already marked stale. If we can't lock it, back off
			 * and retry.
			 */
			if (ip != free_ip &&
			    !xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
				rcu_read_unlock();
				delay(1);
				goto retry;
			}
			rcu_read_unlock();

			xfs_iflock(ip);
			xfs_iflags_set(ip, XFS_ISTALE);

			/*
			 * we don't need to attach clean inodes or those only
			 * with unlogged changes (which we throw away, anyway).
			 */
			iip = ip->i_itemp;
			if (!iip || xfs_inode_clean(ip)) {
				ASSERT(ip != free_ip);
				xfs_ifunlock(ip);
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
				continue;
			}

			iip->ili_last_fields = iip->ili_fields;
			iip->ili_fields = 0;
			iip->ili_logged = 1;
			xfs_trans_ail_copy_lsn(mp->m_ail, &iip->ili_flush_lsn,
						&iip->ili_item.li_lsn);

			xfs_buf_attach_iodone(bp, xfs_istale_done,
						  &iip->ili_item);

			if (ip != free_ip)
				xfs_iunlock(ip, XFS_ILOCK_EXCL);
		}

		xfs_trans_stale_inode_buf(tp, bp);
		xfs_trans_binval(tp, bp);
	}

	xfs_perag_put(pag);
	return 0;
}

/*
 * This is called to return an inode to the inode free list.
 * The inode should already be truncated to 0 length and have
 * no pages associated with it.  This routine also assumes that
 * the inode is already a part of the transaction.
 *
 * The on-disk copy of the inode will have been added to the list
 * of unlinked inodes in the AGI. We need to remove the inode from
 * that list atomically with respect to freeing it here.
 */
int
xfs_ifree(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	xfs_bmap_free_t	*flist)
{
	int			error;
	int			delete;
	xfs_ino_t		first_ino;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(ip->i_d.di_nlink == 0);
	ASSERT(ip->i_d.di_nextents == 0);
	ASSERT(ip->i_d.di_anextents == 0);
	ASSERT(ip->i_d.di_size == 0 || !S_ISREG(ip->i_d.di_mode));
	ASSERT(ip->i_d.di_nblocks == 0);

	/*
	 * Pull the on-disk inode from the AGI unlinked list.
	 */
	error = xfs_iunlink_remove(tp, ip);
	if (error)
		return error;

	error = xfs_difree(tp, ip->i_ino, flist, &delete, &first_ino);
	if (error)
		return error;

	ip->i_d.di_mode = 0;		/* mark incore inode as free */
	ip->i_d.di_flags = 0;
	ip->i_d.di_dmevmask = 0;
	ip->i_d.di_forkoff = 0;		/* mark the attr fork not in use */
	ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
	ip->i_d.di_aformat = XFS_DINODE_FMT_EXTENTS;
	/*
	 * Bump the generation count so no one will be confused
	 * by reincarnations of this inode.
	 */
	ip->i_d.di_gen++;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	if (delete)
		error = xfs_ifree_cluster(ip, tp, first_ino);

	return error;
}

/*
 * This is called to unpin an inode.  The caller must have the inode locked
 * in at least shared mode so that the buffer cannot be subsequently pinned
 * once someone is waiting for it to be unpinned.
 */
static void
xfs_iunpin(
	struct xfs_inode	*ip)
{
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));

	trace_xfs_inode_unpin_nowait(ip, _RET_IP_);

	/* Give the log a push to start the unpinning I/O */
	xfs_log_force_lsn(ip->i_mount, ip->i_itemp->ili_last_lsn, 0);

}

static void
__xfs_iunpin_wait(
	struct xfs_inode	*ip)
{
	wait_queue_head_t *wq = bit_waitqueue(&ip->i_flags, __XFS_IPINNED_BIT);
	DEFINE_WAIT_BIT(wait, &ip->i_flags, __XFS_IPINNED_BIT);

	xfs_iunpin(ip);

	do {
		prepare_to_wait(wq, &wait.wait, TASK_UNINTERRUPTIBLE);
		if (xfs_ipincount(ip))
			io_schedule();
	} while (xfs_ipincount(ip));
	finish_wait(wq, &wait.wait);
}

void
xfs_iunpin_wait(
	struct xfs_inode	*ip)
{
	if (xfs_ipincount(ip))
		__xfs_iunpin_wait(ip);
}

STATIC int
xfs_iflush_cluster(
	xfs_inode_t	*ip,
	xfs_buf_t	*bp)
{
	xfs_mount_t		*mp = ip->i_mount;
	struct xfs_perag	*pag;
	unsigned long		first_index, mask;
	unsigned long		inodes_per_cluster;
	int			ilist_size;
	xfs_inode_t		**ilist;
	xfs_inode_t		*iq;
	int			nr_found;
	int			clcount = 0;
	int			bufwasdelwri;
	int			i;

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));

	inodes_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_inodelog;
	ilist_size = inodes_per_cluster * sizeof(xfs_inode_t *);
	ilist = kmem_alloc(ilist_size, KM_MAYFAIL|KM_NOFS);
	if (!ilist)
		goto out_put;

	mask = ~(((XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_inodelog)) - 1);
	first_index = XFS_INO_TO_AGINO(mp, ip->i_ino) & mask;
	rcu_read_lock();
	/* really need a gang lookup range call here */
	nr_found = radix_tree_gang_lookup(&pag->pag_ici_root, (void**)ilist,
					first_index, inodes_per_cluster);
	if (nr_found == 0)
		goto out_free;

	for (i = 0; i < nr_found; i++) {
		iq = ilist[i];
		if (iq == ip)
			continue;

		/*
		 * because this is an RCU protected lookup, we could find a
		 * recently freed or even reallocated inode during the lookup.
		 * We need to check under the i_flags_lock for a valid inode
		 * here. Skip it if it is not valid or the wrong inode.
		 */
		spin_lock(&ip->i_flags_lock);
		if (!ip->i_ino ||
		    (XFS_INO_TO_AGINO(mp, iq->i_ino) & mask) != first_index) {
			spin_unlock(&ip->i_flags_lock);
			continue;
		}
		spin_unlock(&ip->i_flags_lock);

		/*
		 * Do an un-protected check to see if the inode is dirty and
		 * is a candidate for flushing.  These checks will be repeated
		 * later after the appropriate locks are acquired.
		 */
		if (xfs_inode_clean(iq) && xfs_ipincount(iq) == 0)
			continue;

		/*
		 * Try to get locks.  If any are unavailable or it is pinned,
		 * then this inode cannot be flushed and is skipped.
		 */

		if (!xfs_ilock_nowait(iq, XFS_ILOCK_SHARED))
			continue;
		if (!xfs_iflock_nowait(iq)) {
			xfs_iunlock(iq, XFS_ILOCK_SHARED);
			continue;
		}
		if (xfs_ipincount(iq)) {
			xfs_ifunlock(iq);
			xfs_iunlock(iq, XFS_ILOCK_SHARED);
			continue;
		}

		/*
		 * arriving here means that this inode can be flushed.  First
		 * re-check that it's dirty before flushing.
		 */
		if (!xfs_inode_clean(iq)) {
			int	error;
			error = xfs_iflush_int(iq, bp);
			if (error) {
				xfs_iunlock(iq, XFS_ILOCK_SHARED);
				goto cluster_corrupt_out;
			}
			clcount++;
		} else {
			xfs_ifunlock(iq);
		}
		xfs_iunlock(iq, XFS_ILOCK_SHARED);
	}

	if (clcount) {
		XFS_STATS_INC(xs_icluster_flushcnt);
		XFS_STATS_ADD(xs_icluster_flushinode, clcount);
	}

out_free:
	rcu_read_unlock();
	kmem_free(ilist);
out_put:
	xfs_perag_put(pag);
	return 0;


cluster_corrupt_out:
	/*
	 * Corruption detected in the clustering loop.  Invalidate the
	 * inode buffer and shut down the filesystem.
	 */
	rcu_read_unlock();
	/*
	 * Clean up the buffer.  If it was delwri, just release it --
	 * brelse can handle it with no problems.  If not, shut down the
	 * filesystem before releasing the buffer.
	 */
	bufwasdelwri = (bp->b_flags & _XBF_DELWRI_Q);
	if (bufwasdelwri)
		xfs_buf_relse(bp);

	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);

	if (!bufwasdelwri) {
		/*
		 * Just like incore_relse: if we have b_iodone functions,
		 * mark the buffer as an error and call them.  Otherwise
		 * mark it as stale and brelse.
		 */
		if (bp->b_iodone) {
			XFS_BUF_UNDONE(bp);
			xfs_buf_stale(bp);
			xfs_buf_ioerror(bp, EIO);
			xfs_buf_ioend(bp, 0);
		} else {
			xfs_buf_stale(bp);
			xfs_buf_relse(bp);
		}
	}

	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(iq, false);
	kmem_free(ilist);
	xfs_perag_put(pag);
	return XFS_ERROR(EFSCORRUPTED);
}

/*
 * Flush dirty inode metadata into the backing buffer.
 *
 * The caller must have the inode lock and the inode flush lock held.  The
 * inode lock will still be held upon return to the caller, and the inode
 * flush lock will be released after the inode has reached the disk.
 *
 * The caller must write out the buffer returned in *bpp and release it.
 */
int
xfs_iflush(
	struct xfs_inode	*ip,
	struct xfs_buf		**bpp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_buf		*bp;
	struct xfs_dinode	*dip;
	int			error;

	XFS_STATS_INC(xs_iflush_count);

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(xfs_isiflocked(ip));
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_nextents > XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK));

	*bpp = NULL;

	xfs_iunpin_wait(ip);

	/*
	 * For stale inodes we cannot rely on the backing buffer remaining
	 * stale in cache for the remaining life of the stale inode and so
	 * xfs_imap_to_bp() below may give us a buffer that no longer contains
	 * inodes below. We have to check this after ensuring the inode is
	 * unpinned so that it is safe to reclaim the stale inode after the
	 * flush call.
	 */
	if (xfs_iflags_test(ip, XFS_ISTALE)) {
		xfs_ifunlock(ip);
		return 0;
	}

	/*
	 * This may have been unpinned because the filesystem is shutting
	 * down forcibly. If that's the case we must not write this inode
	 * to disk, because the log record didn't make it to disk.
	 *
	 * We also have to remove the log item from the AIL in this case,
	 * as we wait for an empty AIL as part of the unmount process.
	 */
	if (XFS_FORCED_SHUTDOWN(mp)) {
		error = XFS_ERROR(EIO);
		goto abort_out;
	}

	/*
	 * Get the buffer containing the on-disk inode.
	 */
	error = xfs_imap_to_bp(mp, NULL, &ip->i_imap, &dip, &bp, XBF_TRYLOCK,
			       0);
	if (error || !bp) {
		xfs_ifunlock(ip);
		return error;
	}

	/*
	 * First flush out the inode that xfs_iflush was called with.
	 */
	error = xfs_iflush_int(ip, bp);
	if (error)
		goto corrupt_out;

	/*
	 * If the buffer is pinned then push on the log now so we won't
	 * get stuck waiting in the write for too long.
	 */
	if (xfs_buf_ispinned(bp))
		xfs_log_force(mp, 0);

	/*
	 * inode clustering:
	 * see if other inodes can be gathered into this write
	 */
	error = xfs_iflush_cluster(ip, bp);
	if (error)
		goto cluster_corrupt_out;

	*bpp = bp;
	return 0;

corrupt_out:
	xfs_buf_relse(bp);
	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
cluster_corrupt_out:
	error = XFS_ERROR(EFSCORRUPTED);
abort_out:
	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(ip, false);
	return error;
}


STATIC int
xfs_iflush_int(
	struct xfs_inode	*ip,
	struct xfs_buf		*bp)
{
	struct xfs_inode_log_item *iip = ip->i_itemp;
	struct xfs_dinode	*dip;
	struct xfs_mount	*mp = ip->i_mount;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(xfs_isiflocked(ip));
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_d.di_nextents > XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK));
	ASSERT(iip != NULL && iip->ili_fields != 0);

	/* set *dip = inode's place in the buffer */
	dip = (xfs_dinode_t *)xfs_buf_offset(bp, ip->i_imap.im_boffset);

	if (XFS_TEST_ERROR(dip->di_magic != cpu_to_be16(XFS_DINODE_MAGIC),
			       mp, XFS_ERRTAG_IFLUSH_1, XFS_RANDOM_IFLUSH_1)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad inode %Lu magic number 0x%x, ptr 0x%p",
			__func__, ip->i_ino, be16_to_cpu(dip->di_magic), dip);
		goto corrupt_out;
	}
	if (XFS_TEST_ERROR(ip->i_d.di_magic != XFS_DINODE_MAGIC,
				mp, XFS_ERRTAG_IFLUSH_2, XFS_RANDOM_IFLUSH_2)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad inode %Lu, ptr 0x%p, magic number 0x%x",
			__func__, ip->i_ino, ip, ip->i_d.di_magic);
		goto corrupt_out;
	}
	if (S_ISREG(ip->i_d.di_mode)) {
		if (XFS_TEST_ERROR(
		    (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_BTREE),
		    mp, XFS_ERRTAG_IFLUSH_3, XFS_RANDOM_IFLUSH_3)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad regular inode %Lu, ptr 0x%p",
				__func__, ip->i_ino, ip);
			goto corrupt_out;
		}
	} else if (S_ISDIR(ip->i_d.di_mode)) {
		if (XFS_TEST_ERROR(
		    (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_BTREE) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_LOCAL),
		    mp, XFS_ERRTAG_IFLUSH_4, XFS_RANDOM_IFLUSH_4)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad directory inode %Lu, ptr 0x%p",
				__func__, ip->i_ino, ip);
			goto corrupt_out;
		}
	}
	if (XFS_TEST_ERROR(ip->i_d.di_nextents + ip->i_d.di_anextents >
				ip->i_d.di_nblocks, mp, XFS_ERRTAG_IFLUSH_5,
				XFS_RANDOM_IFLUSH_5)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: detected corrupt incore inode %Lu, "
			"total extents = %d, nblocks = %Ld, ptr 0x%p",
			__func__, ip->i_ino,
			ip->i_d.di_nextents + ip->i_d.di_anextents,
			ip->i_d.di_nblocks, ip);
		goto corrupt_out;
	}
	if (XFS_TEST_ERROR(ip->i_d.di_forkoff > mp->m_sb.sb_inodesize,
				mp, XFS_ERRTAG_IFLUSH_6, XFS_RANDOM_IFLUSH_6)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: bad inode %Lu, forkoff 0x%x, ptr 0x%p",
			__func__, ip->i_ino, ip->i_d.di_forkoff, ip);
		goto corrupt_out;
	}

	/*
	 * Inode item log recovery for v1/v2 inodes are dependent on the
	 * di_flushiter count for correct sequencing. We bump the flush
	 * iteration count so we can detect flushes which postdate a log record
	 * during recovery. This is redundant as we now log every change and
	 * hence this can't happen but we need to still do it to ensure
	 * backwards compatibility with old kernels that predate logging all
	 * inode changes.
	 */
	if (ip->i_d.di_version < 3)
		ip->i_d.di_flushiter++;

	/*
	 * Copy the dirty parts of the inode into the on-disk
	 * inode.  We always copy out the core of the inode,
	 * because if the inode is dirty at all the core must
	 * be.
	 */
	xfs_dinode_to_disk(dip, &ip->i_d);

	/* Wrap, we never let the log put out DI_MAX_FLUSH */
	if (ip->i_d.di_flushiter == DI_MAX_FLUSH)
		ip->i_d.di_flushiter = 0;

	/*
	 * If this is really an old format inode and the superblock version
	 * has not been updated to support only new format inodes, then
	 * convert back to the old inode format.  If the superblock version
	 * has been updated, then make the conversion permanent.
	 */
	ASSERT(ip->i_d.di_version == 1 || xfs_sb_version_hasnlink(&mp->m_sb));
	if (ip->i_d.di_version == 1) {
		if (!xfs_sb_version_hasnlink(&mp->m_sb)) {
			/*
			 * Convert it back.
			 */
			ASSERT(ip->i_d.di_nlink <= XFS_MAXLINK_1);
			dip->di_onlink = cpu_to_be16(ip->i_d.di_nlink);
		} else {
			/*
			 * The superblock version has already been bumped,
			 * so just make the conversion to the new inode
			 * format permanent.
			 */
			ip->i_d.di_version = 2;
			dip->di_version = 2;
			ip->i_d.di_onlink = 0;
			dip->di_onlink = 0;
			memset(&(ip->i_d.di_pad[0]), 0, sizeof(ip->i_d.di_pad));
			memset(&(dip->di_pad[0]), 0,
			      sizeof(dip->di_pad));
			ASSERT(xfs_get_projid(ip) == 0);
		}
	}

	xfs_iflush_fork(ip, dip, iip, XFS_DATA_FORK, bp);
	if (XFS_IFORK_Q(ip))
		xfs_iflush_fork(ip, dip, iip, XFS_ATTR_FORK, bp);
	xfs_inobp_check(mp, bp);

	/*
	 * We've recorded everything logged in the inode, so we'd like to clear
	 * the ili_fields bits so we don't log and flush things unnecessarily.
	 * However, we can't stop logging all this information until the data
	 * we've copied into the disk buffer is written to disk.  If we did we
	 * might overwrite the copy of the inode in the log with all the data
	 * after re-logging only part of it, and in the face of a crash we
	 * wouldn't have all the data we need to recover.
	 *
	 * What we do is move the bits to the ili_last_fields field.  When
	 * logging the inode, these bits are moved back to the ili_fields field.
	 * In the xfs_iflush_done() routine we clear ili_last_fields, since we
	 * know that the information those bits represent is permanently on
	 * disk.  As long as the flush completes before the inode is logged
	 * again, then both ili_fields and ili_last_fields will be cleared.
	 *
	 * We can play with the ili_fields bits here, because the inode lock
	 * must be held exclusively in order to set bits there and the flush
	 * lock protects the ili_last_fields bits.  Set ili_logged so the flush
	 * done routine can tell whether or not to look in the AIL.  Also, store
	 * the current LSN of the inode so that we can tell whether the item has
	 * moved in the AIL from xfs_iflush_done().  In order to read the lsn we
	 * need the AIL lock, because it is a 64 bit value that cannot be read
	 * atomically.
	 */
	iip->ili_last_fields = iip->ili_fields;
	iip->ili_fields = 0;
	iip->ili_logged = 1;

	xfs_trans_ail_copy_lsn(mp->m_ail, &iip->ili_flush_lsn,
				&iip->ili_item.li_lsn);

	/*
	 * Attach the function xfs_iflush_done to the inode's
	 * buffer.  This will remove the inode from the AIL
	 * and unlock the inode's flush lock when the inode is
	 * completely written to disk.
	 */
	xfs_buf_attach_iodone(bp, xfs_iflush_done, &iip->ili_item);

	/* update the lsn in the on disk inode if required */
	if (ip->i_d.di_version == 3)
		dip->di_lsn = cpu_to_be64(iip->ili_item.li_lsn);

	/* generate the checksum. */
	xfs_dinode_calc_crc(mp, dip);

	ASSERT(bp->b_fspriv != NULL);
	ASSERT(bp->b_iodone != NULL);
	return 0;

corrupt_out:
	return XFS_ERROR(EFSCORRUPTED);
}

/*
 * Test whether it is appropriate to check an inode for and free post EOF
 * blocks. The 'force' parameter determines whether we should also consider
 * regular files that are marked preallocated or append-only.
 */
bool
xfs_can_free_eofblocks(struct xfs_inode *ip, bool force)
{
	/* prealloc/delalloc exists only on regular files */
	if (!S_ISREG(ip->i_d.di_mode))
		return false;

	/*
	 * Zero sized files with no cached pages and delalloc blocks will not
	 * have speculative prealloc/delalloc blocks to remove.
	 */
	if (VFS_I(ip)->i_size == 0 &&
	    VN_CACHED(VFS_I(ip)) == 0 &&
	    ip->i_delayed_blks == 0)
		return false;

	/* If we haven't read in the extent list, then don't do it now. */
	if (!(ip->i_df.if_flags & XFS_IFEXTENTS))
		return false;

	/*
	 * Do not free real preallocated or append-only files unless the file
	 * has delalloc blocks and we are forced to remove them.
	 */
	if (ip->i_d.di_flags & (XFS_DIFLAG_PREALLOC | XFS_DIFLAG_APPEND))
		if (!force || ip->i_delayed_blks == 0)
			return false;

	return true;
}
