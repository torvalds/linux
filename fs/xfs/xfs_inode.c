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
#include <linux/iversion.h>

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_attr_sf.h"
#include "xfs_attr.h"
#include "xfs_trans_space.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_inode_item.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_filestream.h"
#include "xfs_cksum.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_symlink.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_bmap_btree.h"
#include "xfs_reflink.h"
#include "xfs_dir2_priv.h"

kmem_zone_t *xfs_inode_zone;

/*
 * Used in xfs_itruncate_extents().  This is the maximum number of extents
 * freed from a file in a single transaction.
 */
#define	XFS_ITRUNC_MAX_EXTENTS	2

STATIC int xfs_iflush_int(struct xfs_inode *, struct xfs_buf *);
STATIC int xfs_iunlink(struct xfs_trans *, struct xfs_inode *);
STATIC int xfs_iunlink_remove(struct xfs_trans *, struct xfs_inode *);

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
 * Helper function to extract CoW extent size hint from inode.
 * Between the extent size hint and the CoW extent size hint, we
 * return the greater of the two.  If the value is zero (automatic),
 * use the default size.
 */
xfs_extlen_t
xfs_get_cowextsz_hint(
	struct xfs_inode	*ip)
{
	xfs_extlen_t		a, b;

	a = 0;
	if (ip->i_d.di_flags2 & XFS_DIFLAG2_COWEXTSIZE)
		a = ip->i_d.di_cowextsize;
	b = xfs_get_extsz_hint(ip);

	a = max(a, b);
	if (a == 0)
		return XFS_DEFAULT_COWEXTSZ_HINT;
	return a;
}

/*
 * These two are wrapper routines around the xfs_ilock() routine used to
 * centralize some grungy code.  They are used in places that wish to lock the
 * inode solely for reading the extents.  The reason these places can't just
 * call xfs_ilock(ip, XFS_ILOCK_SHARED) is that the inode lock also guards to
 * bringing in of the extents from disk for a file in b-tree format.  If the
 * inode is in b-tree format, then we need to lock the inode exclusively until
 * the extents are read in.  Locking it exclusively all the time would limit
 * our parallelism unnecessarily, though.  What we do instead is check to see
 * if the extents have been read in yet, and only lock the inode exclusively
 * if they have not.
 *
 * The functions return a value which should be given to the corresponding
 * xfs_iunlock() call.
 */
uint
xfs_ilock_data_map_shared(
	struct xfs_inode	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE &&
	    (ip->i_df.if_flags & XFS_IFEXTENTS) == 0)
		lock_mode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

uint
xfs_ilock_attr_map_shared(
	struct xfs_inode	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	if (ip->i_d.di_aformat == XFS_DINODE_FMT_BTREE &&
	    (ip->i_afp->if_flags & XFS_IFEXTENTS) == 0)
		lock_mode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

/*
 * In addition to i_rwsem in the VFS inode, the xfs inode contains 2
 * multi-reader locks: i_mmap_lock and the i_lock.  This routine allows
 * various combinations of the locks to be obtained.
 *
 * The 3 locks should always be ordered so that the IO lock is obtained first,
 * the mmap lock second and the ilock last in order to prevent deadlock.
 *
 * Basic locking order:
 *
 * i_rwsem -> i_mmap_lock -> page_lock -> i_ilock
 *
 * mmap_sem locking order:
 *
 * i_rwsem -> page lock -> mmap_sem
 * mmap_sem -> i_mmap_lock -> page_lock
 *
 * The difference in mmap_sem locking order mean that we cannot hold the
 * i_mmap_lock over syscall based read(2)/write(2) based IO. These IO paths can
 * fault in pages during copy in/out (for buffered IO) or require the mmap_sem
 * in get_user_pages() to map the user pages into the kernel address space for
 * direct IO. Similarly the i_rwsem cannot be taken inside a page fault because
 * page faults already hold the mmap_sem.
 *
 * Hence to serialise fully against both syscall and mmap based IO, we need to
 * take both the i_rwsem and the i_mmap_lock. These locks should *only* be both
 * taken in places where we need to invalidate the page cache in a race
 * free manner (e.g. truncate, hole punch and other extent manipulation
 * functions).
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
	ASSERT((lock_flags & (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL)) !=
	       (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_SUBCLASS_MASK)) == 0);

	if (lock_flags & XFS_IOLOCK_EXCL) {
		down_write_nested(&VFS_I(ip)->i_rwsem,
				  XFS_IOLOCK_DEP(lock_flags));
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		down_read_nested(&VFS_I(ip)->i_rwsem,
				 XFS_IOLOCK_DEP(lock_flags));
	}

	if (lock_flags & XFS_MMAPLOCK_EXCL)
		mrupdate_nested(&ip->i_mmaplock, XFS_MMAPLOCK_DEP(lock_flags));
	else if (lock_flags & XFS_MMAPLOCK_SHARED)
		mraccess_nested(&ip->i_mmaplock, XFS_MMAPLOCK_DEP(lock_flags));

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
	ASSERT((lock_flags & (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL)) !=
	       (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_SUBCLASS_MASK)) == 0);

	if (lock_flags & XFS_IOLOCK_EXCL) {
		if (!down_write_trylock(&VFS_I(ip)->i_rwsem))
			goto out;
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		if (!down_read_trylock(&VFS_I(ip)->i_rwsem))
			goto out;
	}

	if (lock_flags & XFS_MMAPLOCK_EXCL) {
		if (!mrtryupdate(&ip->i_mmaplock))
			goto out_undo_iolock;
	} else if (lock_flags & XFS_MMAPLOCK_SHARED) {
		if (!mrtryaccess(&ip->i_mmaplock))
			goto out_undo_iolock;
	}

	if (lock_flags & XFS_ILOCK_EXCL) {
		if (!mrtryupdate(&ip->i_lock))
			goto out_undo_mmaplock;
	} else if (lock_flags & XFS_ILOCK_SHARED) {
		if (!mrtryaccess(&ip->i_lock))
			goto out_undo_mmaplock;
	}
	return 1;

out_undo_mmaplock:
	if (lock_flags & XFS_MMAPLOCK_EXCL)
		mrunlock_excl(&ip->i_mmaplock);
	else if (lock_flags & XFS_MMAPLOCK_SHARED)
		mrunlock_shared(&ip->i_mmaplock);
out_undo_iolock:
	if (lock_flags & XFS_IOLOCK_EXCL)
		up_write(&VFS_I(ip)->i_rwsem);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		up_read(&VFS_I(ip)->i_rwsem);
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
	ASSERT((lock_flags & (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL)) !=
	       (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
	       (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_SUBCLASS_MASK)) == 0);
	ASSERT(lock_flags != 0);

	if (lock_flags & XFS_IOLOCK_EXCL)
		up_write(&VFS_I(ip)->i_rwsem);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		up_read(&VFS_I(ip)->i_rwsem);

	if (lock_flags & XFS_MMAPLOCK_EXCL)
		mrunlock_excl(&ip->i_mmaplock);
	else if (lock_flags & XFS_MMAPLOCK_SHARED)
		mrunlock_shared(&ip->i_mmaplock);

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
	ASSERT(lock_flags & (XFS_IOLOCK_EXCL|XFS_MMAPLOCK_EXCL|XFS_ILOCK_EXCL));
	ASSERT((lock_flags &
		~(XFS_IOLOCK_EXCL|XFS_MMAPLOCK_EXCL|XFS_ILOCK_EXCL)) == 0);

	if (lock_flags & XFS_ILOCK_EXCL)
		mrdemote(&ip->i_lock);
	if (lock_flags & XFS_MMAPLOCK_EXCL)
		mrdemote(&ip->i_mmaplock);
	if (lock_flags & XFS_IOLOCK_EXCL)
		downgrade_write(&VFS_I(ip)->i_rwsem);

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

	if (lock_flags & (XFS_MMAPLOCK_EXCL|XFS_MMAPLOCK_SHARED)) {
		if (!(lock_flags & XFS_MMAPLOCK_SHARED))
			return !!ip->i_mmaplock.mr_writer;
		return rwsem_is_locked(&ip->i_mmaplock.mr_lock);
	}

	if (lock_flags & (XFS_IOLOCK_EXCL|XFS_IOLOCK_SHARED)) {
		if (!(lock_flags & XFS_IOLOCK_SHARED))
			return !debug_locks ||
				lockdep_is_held_type(&VFS_I(ip)->i_rwsem, 0);
		return rwsem_is_locked(&VFS_I(ip)->i_rwsem);
	}

	ASSERT(0);
	return 0;
}
#endif

/*
 * xfs_lockdep_subclass_ok() is only used in an ASSERT, so is only called when
 * DEBUG or XFS_WARN is set. And MAX_LOCKDEP_SUBCLASSES is then only defined
 * when CONFIG_LOCKDEP is set. Hence the complex define below to avoid build
 * errors and warnings.
 */
#if (defined(DEBUG) || defined(XFS_WARN)) && defined(CONFIG_LOCKDEP)
static bool
xfs_lockdep_subclass_ok(
	int subclass)
{
	return subclass < MAX_LOCKDEP_SUBCLASSES;
}
#else
#define xfs_lockdep_subclass_ok(subclass)	(true)
#endif

/*
 * Bump the subclass so xfs_lock_inodes() acquires each lock with a different
 * value. This can be called for any type of inode lock combination, including
 * parent locking. Care must be taken to ensure we don't overrun the subclass
 * storage fields in the class mask we build.
 */
static inline int
xfs_lock_inumorder(int lock_mode, int subclass)
{
	int	class = 0;

	ASSERT(!(lock_mode & (XFS_ILOCK_PARENT | XFS_ILOCK_RTBITMAP |
			      XFS_ILOCK_RTSUM)));
	ASSERT(xfs_lockdep_subclass_ok(subclass));

	if (lock_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL)) {
		ASSERT(subclass <= XFS_IOLOCK_MAX_SUBCLASS);
		class += subclass << XFS_IOLOCK_SHIFT;
	}

	if (lock_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)) {
		ASSERT(subclass <= XFS_MMAPLOCK_MAX_SUBCLASS);
		class += subclass << XFS_MMAPLOCK_SHIFT;
	}

	if (lock_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)) {
		ASSERT(subclass <= XFS_ILOCK_MAX_SUBCLASS);
		class += subclass << XFS_ILOCK_SHIFT;
	}

	return (lock_mode & ~XFS_LOCK_SUBCLASS_MASK) | class;
}

/*
 * The following routine will lock n inodes in exclusive mode.  We assume the
 * caller calls us with the inodes in i_ino order.
 *
 * We need to detect deadlock where an inode that we lock is in the AIL and we
 * start waiting for another inode that is locked by a thread in a long running
 * transaction (such as truncate). This can result in deadlock since the long
 * running trans might need to wait for the inode we just locked in order to
 * push the tail and free space in the log.
 *
 * xfs_lock_inodes() can only be used to lock one type of lock at a time -
 * the iolock, the mmaplock or the ilock, but not more than one at a time. If we
 * lock more than one at a time, lockdep will report false positives saying we
 * have violated locking orders.
 */
static void
xfs_lock_inodes(
	xfs_inode_t	**ips,
	int		inodes,
	uint		lock_mode)
{
	int		attempts = 0, i, j, try_lock;
	xfs_log_item_t	*lp;

	/*
	 * Currently supports between 2 and 5 inodes with exclusive locking.  We
	 * support an arbitrary depth of locking here, but absolute limits on
	 * inodes depend on the the type of locking and the limits placed by
	 * lockdep annotations in xfs_lock_inumorder.  These are all checked by
	 * the asserts.
	 */
	ASSERT(ips && inodes >= 2 && inodes <= 5);
	ASSERT(lock_mode & (XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL |
			    XFS_ILOCK_EXCL));
	ASSERT(!(lock_mode & (XFS_IOLOCK_SHARED | XFS_MMAPLOCK_SHARED |
			      XFS_ILOCK_SHARED)));
	ASSERT(!(lock_mode & XFS_MMAPLOCK_EXCL) ||
		inodes <= XFS_MMAPLOCK_MAX_SUBCLASS + 1);
	ASSERT(!(lock_mode & XFS_ILOCK_EXCL) ||
		inodes <= XFS_ILOCK_MAX_SUBCLASS + 1);

	if (lock_mode & XFS_IOLOCK_EXCL) {
		ASSERT(!(lock_mode & (XFS_MMAPLOCK_EXCL | XFS_ILOCK_EXCL)));
	} else if (lock_mode & XFS_MMAPLOCK_EXCL)
		ASSERT(!(lock_mode & XFS_ILOCK_EXCL));

	try_lock = 0;
	i = 0;
again:
	for (; i < inodes; i++) {
		ASSERT(ips[i]);

		if (i && (ips[i] == ips[i - 1]))	/* Already locked */
			continue;

		/*
		 * If try_lock is not set yet, make sure all locked inodes are
		 * not in the AIL.  If any are, set try_lock to be used later.
		 */
		if (!try_lock) {
			for (j = (i - 1); j >= 0 && !try_lock; j--) {
				lp = (xfs_log_item_t *)ips[j]->i_itemp;
				if (lp && test_bit(XFS_LI_IN_AIL, &lp->li_flags))
					try_lock++;
			}
		}

		/*
		 * If any of the previous locks we have locked is in the AIL,
		 * we must TRY to get the second and subsequent locks. If
		 * we can't get any, we must release all we have
		 * and try again.
		 */
		if (!try_lock) {
			xfs_ilock(ips[i], xfs_lock_inumorder(lock_mode, i));
			continue;
		}

		/* try_lock means we have an inode locked that is in the AIL. */
		ASSERT(i != 0);
		if (xfs_ilock_nowait(ips[i], xfs_lock_inumorder(lock_mode, i)))
			continue;

		/*
		 * Unlock all previous guys and try again.  xfs_iunlock will try
		 * to push the tail if the inode is in the AIL.
		 */
		attempts++;
		for (j = i - 1; j >= 0; j--) {
			/*
			 * Check to see if we've already unlocked this one.  Not
			 * the first one going back, and the inode ptr is the
			 * same.
			 */
			if (j != (i - 1) && ips[j] == ips[j + 1])
				continue;

			xfs_iunlock(ips[j], lock_mode);
		}

		if ((attempts % 5) == 0) {
			delay(1); /* Don't just spin the CPU */
		}
		i = 0;
		try_lock = 0;
		goto again;
	}
}

/*
 * xfs_lock_two_inodes() can only be used to lock one type of lock at a time -
 * the mmaplock or the ilock, but not more than one type at a time. If we lock
 * more than one at a time, lockdep will report false positives saying we have
 * violated locking orders.  The iolock must be double-locked separately since
 * we use i_rwsem for that.  We now support taking one lock EXCL and the other
 * SHARED.
 */
void
xfs_lock_two_inodes(
	struct xfs_inode	*ip0,
	uint			ip0_mode,
	struct xfs_inode	*ip1,
	uint			ip1_mode)
{
	struct xfs_inode	*temp;
	uint			mode_temp;
	int			attempts = 0;
	xfs_log_item_t		*lp;

	ASSERT(hweight32(ip0_mode) == 1);
	ASSERT(hweight32(ip1_mode) == 1);
	ASSERT(!(ip0_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL)));
	ASSERT(!(ip1_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL)));
	ASSERT(!(ip0_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)) ||
	       !(ip0_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)));
	ASSERT(!(ip1_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)) ||
	       !(ip1_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)));
	ASSERT(!(ip1_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)) ||
	       !(ip0_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)));
	ASSERT(!(ip0_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)) ||
	       !(ip1_mode & (XFS_ILOCK_SHARED|XFS_ILOCK_EXCL)));

	ASSERT(ip0->i_ino != ip1->i_ino);

	if (ip0->i_ino > ip1->i_ino) {
		temp = ip0;
		ip0 = ip1;
		ip1 = temp;
		mode_temp = ip0_mode;
		ip0_mode = ip1_mode;
		ip1_mode = mode_temp;
	}

 again:
	xfs_ilock(ip0, xfs_lock_inumorder(ip0_mode, 0));

	/*
	 * If the first lock we have locked is in the AIL, we must TRY to get
	 * the second lock. If we can't get it, we must release the first one
	 * and try again.
	 */
	lp = (xfs_log_item_t *)ip0->i_itemp;
	if (lp && test_bit(XFS_LI_IN_AIL, &lp->li_flags)) {
		if (!xfs_ilock_nowait(ip1, xfs_lock_inumorder(ip1_mode, 1))) {
			xfs_iunlock(ip0, ip0_mode);
			if ((++attempts % 5) == 0)
				delay(1); /* Don't just spin the CPU */
			goto again;
		}
	} else {
		xfs_ilock(ip1, xfs_lock_inumorder(ip1_mode, 1));
	}
}

void
__xfs_iflock(
	struct xfs_inode	*ip)
{
	wait_queue_head_t *wq = bit_waitqueue(&ip->i_flags, __XFS_IFLOCK_BIT);
	DEFINE_WAIT_BIT(wait, &ip->i_flags, __XFS_IFLOCK_BIT);

	do {
		prepare_to_wait_exclusive(wq, &wait.wq_entry, TASK_UNINTERRUPTIBLE);
		if (xfs_isiflocked(ip))
			io_schedule();
	} while (!xfs_iflock_nowait(ip));

	finish_wait(wq, &wait.wq_entry);
}

STATIC uint
_xfs_dic2xflags(
	uint16_t		di_flags,
	uint64_t		di_flags2,
	bool			has_attr)
{
	uint			flags = 0;

	if (di_flags & XFS_DIFLAG_ANY) {
		if (di_flags & XFS_DIFLAG_REALTIME)
			flags |= FS_XFLAG_REALTIME;
		if (di_flags & XFS_DIFLAG_PREALLOC)
			flags |= FS_XFLAG_PREALLOC;
		if (di_flags & XFS_DIFLAG_IMMUTABLE)
			flags |= FS_XFLAG_IMMUTABLE;
		if (di_flags & XFS_DIFLAG_APPEND)
			flags |= FS_XFLAG_APPEND;
		if (di_flags & XFS_DIFLAG_SYNC)
			flags |= FS_XFLAG_SYNC;
		if (di_flags & XFS_DIFLAG_NOATIME)
			flags |= FS_XFLAG_NOATIME;
		if (di_flags & XFS_DIFLAG_NODUMP)
			flags |= FS_XFLAG_NODUMP;
		if (di_flags & XFS_DIFLAG_RTINHERIT)
			flags |= FS_XFLAG_RTINHERIT;
		if (di_flags & XFS_DIFLAG_PROJINHERIT)
			flags |= FS_XFLAG_PROJINHERIT;
		if (di_flags & XFS_DIFLAG_NOSYMLINKS)
			flags |= FS_XFLAG_NOSYMLINKS;
		if (di_flags & XFS_DIFLAG_EXTSIZE)
			flags |= FS_XFLAG_EXTSIZE;
		if (di_flags & XFS_DIFLAG_EXTSZINHERIT)
			flags |= FS_XFLAG_EXTSZINHERIT;
		if (di_flags & XFS_DIFLAG_NODEFRAG)
			flags |= FS_XFLAG_NODEFRAG;
		if (di_flags & XFS_DIFLAG_FILESTREAM)
			flags |= FS_XFLAG_FILESTREAM;
	}

	if (di_flags2 & XFS_DIFLAG2_ANY) {
		if (di_flags2 & XFS_DIFLAG2_DAX)
			flags |= FS_XFLAG_DAX;
		if (di_flags2 & XFS_DIFLAG2_COWEXTSIZE)
			flags |= FS_XFLAG_COWEXTSIZE;
	}

	if (has_attr)
		flags |= FS_XFLAG_HASATTR;

	return flags;
}

uint
xfs_ip2xflags(
	struct xfs_inode	*ip)
{
	struct xfs_icdinode	*dic = &ip->i_d;

	return _xfs_dic2xflags(dic->di_flags, dic->di_flags2, XFS_IFORK_Q(ip));
}

/*
 * Lookups up an inode from "name". If ci_name is not NULL, then a CI match
 * is allowed, otherwise it has to be an exact match. If a CI match is found,
 * ci_name->name will point to a the actual name (caller must free) or
 * will be set to NULL if an exact match is found.
 */
int
xfs_lookup(
	xfs_inode_t		*dp,
	struct xfs_name		*name,
	xfs_inode_t		**ipp,
	struct xfs_name		*ci_name)
{
	xfs_ino_t		inum;
	int			error;

	trace_xfs_lookup(dp, name);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return -EIO;

	error = xfs_dir_lookup(NULL, dp, name, &inum, ci_name);
	if (error)
		goto out_unlock;

	error = xfs_iget(dp->i_mount, NULL, inum, 0, 0, ipp);
	if (error)
		goto out_free_name;

	return 0;

out_free_name:
	if (ci_name)
		kmem_free(ci_name->name);
out_unlock:
	*ipp = NULL;
	return error;
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
static int
xfs_ialloc(
	xfs_trans_t	*tp,
	xfs_inode_t	*pip,
	umode_t		mode,
	xfs_nlink_t	nlink,
	dev_t		rdev,
	prid_t		prid,
	xfs_buf_t	**ialloc_context,
	xfs_inode_t	**ipp)
{
	struct xfs_mount *mp = tp->t_mountp;
	xfs_ino_t	ino;
	xfs_inode_t	*ip;
	uint		flags;
	int		error;
	struct timespec	tv;
	struct inode	*inode;

	/*
	 * Call the space management code to pick
	 * the on-disk inode to be allocated.
	 */
	error = xfs_dialloc(tp, pip ? pip->i_ino : 0, mode,
			    ialloc_context, &ino);
	if (error)
		return error;
	if (*ialloc_context || ino == NULLFSINO) {
		*ipp = NULL;
		return 0;
	}
	ASSERT(*ialloc_context == NULL);

	/*
	 * Protect against obviously corrupt allocation btree records. Later
	 * xfs_iget checks will catch re-allocation of other active in-memory
	 * and on-disk inodes. If we don't catch reallocating the parent inode
	 * here we will deadlock in xfs_iget() so we have to do these checks
	 * first.
	 */
	if ((pip && ino == pip->i_ino) || !xfs_verify_dir_ino(mp, ino)) {
		xfs_alert(mp, "Allocated a known in-use inode 0x%llx!", ino);
		return -EFSCORRUPTED;
	}

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
	inode = VFS_I(ip);

	/*
	 * We always convert v1 inodes to v2 now - we only support filesystems
	 * with >= v2 inode capability, so there is no reason for ever leaving
	 * an inode in v1 format.
	 */
	if (ip->i_d.di_version == 1)
		ip->i_d.di_version = 2;

	inode->i_mode = mode;
	set_nlink(inode, nlink);
	ip->i_d.di_uid = xfs_kuid_to_uid(current_fsuid());
	ip->i_d.di_gid = xfs_kgid_to_gid(current_fsgid());
	inode->i_rdev = rdev;
	xfs_set_projid(ip, prid);

	if (pip && XFS_INHERIT_GID(pip)) {
		ip->i_d.di_gid = pip->i_d.di_gid;
		if ((VFS_I(pip)->i_mode & S_ISGID) && S_ISDIR(mode))
			inode->i_mode |= S_ISGID;
	}

	/*
	 * If the group ID of the new file does not match the effective group
	 * ID or one of the supplementary group IDs, the S_ISGID bit is cleared
	 * (and only if the irix_sgid_inherit compatibility variable is set).
	 */
	if ((irix_sgid_inherit) &&
	    (inode->i_mode & S_ISGID) &&
	    (!in_group_p(xfs_gid_to_kgid(ip->i_d.di_gid))))
		inode->i_mode &= ~S_ISGID;

	ip->i_d.di_size = 0;
	ip->i_d.di_nextents = 0;
	ASSERT(ip->i_d.di_nblocks == 0);

	tv = current_time(inode);
	inode->i_mtime = tv;
	inode->i_atime = tv;
	inode->i_ctime = tv;

	ip->i_d.di_extsize = 0;
	ip->i_d.di_dmevmask = 0;
	ip->i_d.di_dmstate = 0;
	ip->i_d.di_flags = 0;

	if (ip->i_d.di_version == 3) {
		inode_set_iversion(inode, 1);
		ip->i_d.di_flags2 = 0;
		ip->i_d.di_cowextsize = 0;
		ip->i_d.di_crtime.t_sec = (int32_t)tv.tv_sec;
		ip->i_d.di_crtime.t_nsec = (int32_t)tv.tv_nsec;
	}


	flags = XFS_ILOG_CORE;
	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		ip->i_d.di_format = XFS_DINODE_FMT_DEV;
		ip->i_df.if_flags = 0;
		flags |= XFS_ILOG_DEV;
		break;
	case S_IFREG:
	case S_IFDIR:
		if (pip && (pip->i_d.di_flags & XFS_DIFLAG_ANY)) {
			uint		di_flags = 0;

			if (S_ISDIR(mode)) {
				if (pip->i_d.di_flags & XFS_DIFLAG_RTINHERIT)
					di_flags |= XFS_DIFLAG_RTINHERIT;
				if (pip->i_d.di_flags & XFS_DIFLAG_EXTSZINHERIT) {
					di_flags |= XFS_DIFLAG_EXTSZINHERIT;
					ip->i_d.di_extsize = pip->i_d.di_extsize;
				}
				if (pip->i_d.di_flags & XFS_DIFLAG_PROJINHERIT)
					di_flags |= XFS_DIFLAG_PROJINHERIT;
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
			if ((pip->i_d.di_flags & XFS_DIFLAG_NODEFRAG) &&
			    xfs_inherit_nodefrag)
				di_flags |= XFS_DIFLAG_NODEFRAG;
			if (pip->i_d.di_flags & XFS_DIFLAG_FILESTREAM)
				di_flags |= XFS_DIFLAG_FILESTREAM;

			ip->i_d.di_flags |= di_flags;
		}
		if (pip &&
		    (pip->i_d.di_flags2 & XFS_DIFLAG2_ANY) &&
		    pip->i_d.di_version == 3 &&
		    ip->i_d.di_version == 3) {
			uint64_t	di_flags2 = 0;

			if (pip->i_d.di_flags2 & XFS_DIFLAG2_COWEXTSIZE) {
				di_flags2 |= XFS_DIFLAG2_COWEXTSIZE;
				ip->i_d.di_cowextsize = pip->i_d.di_cowextsize;
			}
			if (pip->i_d.di_flags2 & XFS_DIFLAG2_DAX)
				di_flags2 |= XFS_DIFLAG2_DAX;

			ip->i_d.di_flags2 |= di_flags2;
		}
		/* FALLTHROUGH */
	case S_IFLNK:
		ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
		ip->i_df.if_flags = XFS_IFEXTENTS;
		ip->i_df.if_bytes = ip->i_df.if_real_bytes = 0;
		ip->i_df.if_u1.if_root = NULL;
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

	/* now that we have an i_mode we can setup the inode structure */
	xfs_setup_inode(ip);

	*ipp = ip;
	return 0;
}

/*
 * Allocates a new inode from disk and return a pointer to the
 * incore copy. This routine will internally commit the current
 * transaction and allocate a new one if the Space Manager needed
 * to do an allocation to replenish the inode free-list.
 *
 * This routine is designed to be called from xfs_create and
 * xfs_create_dir.
 *
 */
int
xfs_dir_ialloc(
	xfs_trans_t	**tpp,		/* input: current transaction;
					   output: may be a new transaction. */
	xfs_inode_t	*dp,		/* directory within whose allocate
					   the inode. */
	umode_t		mode,
	xfs_nlink_t	nlink,
	dev_t		rdev,
	prid_t		prid,		/* project id */
	xfs_inode_t	**ipp)		/* pointer to inode; it will be
					   locked. */
{
	xfs_trans_t	*tp;
	xfs_inode_t	*ip;
	xfs_buf_t	*ialloc_context = NULL;
	int		code;
	void		*dqinfo;
	uint		tflags;

	tp = *tpp;
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);

	/*
	 * xfs_ialloc will return a pointer to an incore inode if
	 * the Space Manager has an available inode on the free
	 * list. Otherwise, it will do an allocation and replenish
	 * the freelist.  Since we can only do one allocation per
	 * transaction without deadlocks, we will need to commit the
	 * current transaction and start a new one.  We will then
	 * need to call xfs_ialloc again to get the inode.
	 *
	 * If xfs_ialloc did an allocation to replenish the freelist,
	 * it returns the bp containing the head of the freelist as
	 * ialloc_context. We will hold a lock on it across the
	 * transaction commit so that no other process can steal
	 * the inode(s) that we've just allocated.
	 */
	code = xfs_ialloc(tp, dp, mode, nlink, rdev, prid, &ialloc_context,
			&ip);

	/*
	 * Return an error if we were unable to allocate a new inode.
	 * This should only happen if we run out of space on disk or
	 * encounter a disk error.
	 */
	if (code) {
		*ipp = NULL;
		return code;
	}
	if (!ialloc_context && !ip) {
		*ipp = NULL;
		return -ENOSPC;
	}

	/*
	 * If the AGI buffer is non-NULL, then we were unable to get an
	 * inode in one operation.  We need to commit the current
	 * transaction and call xfs_ialloc() again.  It is guaranteed
	 * to succeed the second time.
	 */
	if (ialloc_context) {
		/*
		 * Normally, xfs_trans_commit releases all the locks.
		 * We call bhold to hang on to the ialloc_context across
		 * the commit.  Holding this buffer prevents any other
		 * processes from doing any allocations in this
		 * allocation group.
		 */
		xfs_trans_bhold(tp, ialloc_context);

		/*
		 * We want the quota changes to be associated with the next
		 * transaction, NOT this one. So, detach the dqinfo from this
		 * and attach it to the next transaction.
		 */
		dqinfo = NULL;
		tflags = 0;
		if (tp->t_dqinfo) {
			dqinfo = (void *)tp->t_dqinfo;
			tp->t_dqinfo = NULL;
			tflags = tp->t_flags & XFS_TRANS_DQ_DIRTY;
			tp->t_flags &= ~(XFS_TRANS_DQ_DIRTY);
		}

		code = xfs_trans_roll(&tp);

		/*
		 * Re-attach the quota info that we detached from prev trx.
		 */
		if (dqinfo) {
			tp->t_dqinfo = dqinfo;
			tp->t_flags |= tflags;
		}

		if (code) {
			xfs_buf_relse(ialloc_context);
			*tpp = tp;
			*ipp = NULL;
			return code;
		}
		xfs_trans_bjoin(tp, ialloc_context);

		/*
		 * Call ialloc again. Since we've locked out all
		 * other allocations in this allocation group,
		 * this call should always succeed.
		 */
		code = xfs_ialloc(tp, dp, mode, nlink, rdev, prid,
				  &ialloc_context, &ip);

		/*
		 * If we get an error at this point, return to the caller
		 * so that the current transaction can be aborted.
		 */
		if (code) {
			*tpp = tp;
			*ipp = NULL;
			return code;
		}
		ASSERT(!ialloc_context && ip);

	}

	*ipp = ip;
	*tpp = tp;

	return 0;
}

/*
 * Decrement the link count on an inode & log the change.  If this causes the
 * link count to go to zero, move the inode to AGI unlinked list so that it can
 * be freed when the last active reference goes away via xfs_inactive().
 */
static int			/* error */
xfs_droplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	drop_nlink(VFS_I(ip));
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	if (VFS_I(ip)->i_nlink)
		return 0;

	return xfs_iunlink(tp, ip);
}

/*
 * Increment the link count on an inode & log the change.
 */
static int
xfs_bumplink(
	xfs_trans_t *tp,
	xfs_inode_t *ip)
{
	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	ASSERT(ip->i_d.di_version > 1);
	inc_nlink(VFS_I(ip));
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	return 0;
}

int
xfs_create(
	xfs_inode_t		*dp,
	struct xfs_name		*name,
	umode_t			mode,
	dev_t			rdev,
	xfs_inode_t		**ipp)
{
	int			is_dir = S_ISDIR(mode);
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_inode	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	int			error;
	struct xfs_defer_ops	dfops;
	xfs_fsblock_t		first_block;
	bool                    unlock_dp_on_error = false;
	prid_t			prid;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_trans_res	*tres;
	uint			resblks;

	trace_xfs_create(dp, name);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	prid = xfs_get_initial_prid(dp);

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, xfs_kuid_to_uid(current_fsuid()),
					xfs_kgid_to_gid(current_fsgid()), prid,
					XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT,
					&udqp, &gdqp, &pdqp);
	if (error)
		return error;

	if (is_dir) {
		resblks = XFS_MKDIR_SPACE_RES(mp, name->len);
		tres = &M_RES(mp)->tr_mkdir;
	} else {
		resblks = XFS_CREATE_SPACE_RES(mp, name->len);
		tres = &M_RES(mp)->tr_create;
	}

	/*
	 * Initially assume that the file does not exist and
	 * reserve the resources for that case.  If that is not
	 * the case we'll drop the one we have and get a more
	 * appropriate transaction later.
	 */
	error = xfs_trans_alloc(mp, tres, resblks, 0, 0, &tp);
	if (error == -ENOSPC) {
		/* flush outstanding delalloc blocks and retry */
		xfs_flush_inodes(mp);
		error = xfs_trans_alloc(mp, tres, resblks, 0, 0, &tp);
	}
	if (error)
		goto out_release_inode;

	xfs_ilock(dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	unlock_dp_on_error = true;

	xfs_defer_init(&dfops, &first_block);
	tp->t_agfl_dfops = &dfops;

	/*
	 * Reserve disk quota and the inode.
	 */
	error = xfs_trans_reserve_quota(tp, mp, udqp, gdqp,
						pdqp, resblks, 1, 0);
	if (error)
		goto out_trans_cancel;

	/*
	 * A newly created regular or special file just has one directory
	 * entry pointing to them, but a directory also the "." entry
	 * pointing to itself.
	 */
	error = xfs_dir_ialloc(&tp, dp, mode, is_dir ? 2 : 1, rdev, prid, &ip);
	if (error)
		goto out_trans_cancel;

	/*
	 * Now we join the directory inode to the transaction.  We do not do it
	 * earlier because xfs_dir_ialloc might commit the previous transaction
	 * (and release all the locks).  An error from here on will result in
	 * the transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	unlock_dp_on_error = false;

	error = xfs_dir_createname(tp, dp, name, ip->i_ino,
					&first_block, &dfops, resblks ?
					resblks - XFS_IALLOC_SPACE_RES(mp) : 0);
	if (error) {
		ASSERT(error != -ENOSPC);
		goto out_trans_cancel;
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);

	if (is_dir) {
		error = xfs_dir_init(tp, ip, dp);
		if (error)
			goto out_bmap_cancel;

		error = xfs_bumplink(tp, dp);
		if (error)
			goto out_bmap_cancel;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * create transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp, pdqp);

	error = xfs_defer_finish(&tp, &dfops);
	if (error)
		goto out_bmap_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto out_release_inode;

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	*ipp = ip;
	return 0;

 out_bmap_cancel:
	xfs_defer_cancel(&dfops);
 out_trans_cancel:
	xfs_trans_cancel(tp);
 out_release_inode:
	/*
	 * Wait until after the current transaction is aborted to finish the
	 * setup of the inode and release the inode.  This prevents recursive
	 * transactions and deadlocks from xfs_inactive.
	 */
	if (ip) {
		xfs_finish_inode_setup(ip);
		IRELE(ip);
	}

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	if (unlock_dp_on_error)
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	return error;
}

int
xfs_create_tmpfile(
	struct xfs_inode	*dp,
	umode_t			mode,
	struct xfs_inode	**ipp)
{
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_inode	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	int			error;
	prid_t                  prid;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_trans_res	*tres;
	uint			resblks;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	prid = xfs_get_initial_prid(dp);

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, xfs_kuid_to_uid(current_fsuid()),
				xfs_kgid_to_gid(current_fsgid()), prid,
				XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT,
				&udqp, &gdqp, &pdqp);
	if (error)
		return error;

	resblks = XFS_IALLOC_SPACE_RES(mp);
	tres = &M_RES(mp)->tr_create_tmpfile;

	error = xfs_trans_alloc(mp, tres, resblks, 0, 0, &tp);
	if (error)
		goto out_release_inode;

	error = xfs_trans_reserve_quota(tp, mp, udqp, gdqp,
						pdqp, resblks, 1, 0);
	if (error)
		goto out_trans_cancel;

	error = xfs_dir_ialloc(&tp, dp, mode, 1, 0, prid, &ip);
	if (error)
		goto out_trans_cancel;

	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp, pdqp);

	error = xfs_iunlink(tp, ip);
	if (error)
		goto out_trans_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto out_release_inode;

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	*ipp = ip;
	return 0;

 out_trans_cancel:
	xfs_trans_cancel(tp);
 out_release_inode:
	/*
	 * Wait until after the current transaction is aborted to finish the
	 * setup of the inode and release the inode.  This prevents recursive
	 * transactions and deadlocks from xfs_inactive.
	 */
	if (ip) {
		xfs_finish_inode_setup(ip);
		IRELE(ip);
	}

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	return error;
}

int
xfs_link(
	xfs_inode_t		*tdp,
	xfs_inode_t		*sip,
	struct xfs_name		*target_name)
{
	xfs_mount_t		*mp = tdp->i_mount;
	xfs_trans_t		*tp;
	int			error;
	struct xfs_defer_ops	dfops;
	xfs_fsblock_t           first_block;
	int			resblks;

	trace_xfs_link(tdp, target_name);

	ASSERT(!S_ISDIR(VFS_I(sip)->i_mode));

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	error = xfs_qm_dqattach(sip);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(tdp);
	if (error)
		goto std_return;

	resblks = XFS_LINK_SPACE_RES(mp, target_name->len);
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_link, resblks, 0, 0, &tp);
	if (error == -ENOSPC) {
		resblks = 0;
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_link, 0, 0, 0, &tp);
	}
	if (error)
		goto std_return;

	xfs_lock_two_inodes(sip, XFS_ILOCK_EXCL, tdp, XFS_ILOCK_EXCL);

	xfs_trans_ijoin(tp, sip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, tdp, XFS_ILOCK_EXCL);

	/*
	 * If we are using project inheritance, we only allow hard link
	 * creation in our tree when the project IDs are the same; else
	 * the tree quota mechanism could be circumvented.
	 */
	if (unlikely((tdp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
		     (xfs_get_projid(tdp) != xfs_get_projid(sip)))) {
		error = -EXDEV;
		goto error_return;
	}

	if (!resblks) {
		error = xfs_dir_canenter(tp, tdp, target_name);
		if (error)
			goto error_return;
	}

	xfs_defer_init(&dfops, &first_block);
	tp->t_agfl_dfops = &dfops;

	/*
	 * Handle initial link state of O_TMPFILE inode
	 */
	if (VFS_I(sip)->i_nlink == 0) {
		error = xfs_iunlink_remove(tp, sip);
		if (error)
			goto error_return;
	}

	error = xfs_dir_createname(tp, tdp, target_name, sip->i_ino,
					&first_block, &dfops, resblks);
	if (error)
		goto error_return;
	xfs_trans_ichgtime(tp, tdp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, tdp, XFS_ILOG_CORE);

	error = xfs_bumplink(tp, sip);
	if (error)
		goto error_return;

	/*
	 * If this is a synchronous mount, make sure that the
	 * link transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	error = xfs_defer_finish(&tp, &dfops);
	if (error) {
		xfs_defer_cancel(&dfops);
		goto error_return;
	}

	return xfs_trans_commit(tp);

 error_return:
	xfs_trans_cancel(tp);
 std_return:
	return error;
}

/* Clear the reflink flag and the cowblocks tag if possible. */
static void
xfs_itruncate_clear_reflink_flags(
	struct xfs_inode	*ip)
{
	struct xfs_ifork	*dfork;
	struct xfs_ifork	*cfork;

	if (!xfs_is_reflink_inode(ip))
		return;
	dfork = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
	cfork = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	if (dfork->if_bytes == 0 && cfork->if_bytes == 0)
		ip->i_d.di_flags2 &= ~XFS_DIFLAG2_REFLINK;
	if (cfork->if_bytes == 0)
		xfs_inode_clear_cowblocks_tag(ip);
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
__xfs_itruncate_extents(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fsize_t		new_size,
	bool			skip_discard)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp = *tpp;
	struct xfs_defer_ops	dfops;
	xfs_fsblock_t		first_block;
	xfs_fileoff_t		first_unmap_block;
	xfs_fileoff_t		last_block;
	xfs_filblks_t		unmap_len;
	int			error = 0;
	int			done = 0;
	int			flags;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(!atomic_read(&VFS_I(ip)->i_count) ||
	       xfs_isilocked(ip, XFS_IOLOCK_EXCL));
	ASSERT(new_size <= XFS_ISIZE(ip));
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ip->i_itemp->ili_lock_flags == 0);
	ASSERT(!XFS_NOT_DQATTACHED(mp, ip));

	trace_xfs_itruncate_extents_start(ip, new_size);

	flags = xfs_bmapi_aflag(whichfork);
	if (skip_discard)
		flags |= XFS_BMAPI_NODISCARD;

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
		xfs_defer_init(&dfops, &first_block);
		error = xfs_bunmapi(tp, ip, first_unmap_block, unmap_len, flags,
				    XFS_ITRUNC_MAX_EXTENTS, &first_block,
				    &dfops, &done);
		if (error)
			goto out_bmap_cancel;

		/*
		 * Duplicate the transaction that has the permanent
		 * reservation and commit the old transaction.
		 */
		xfs_defer_ijoin(&dfops, ip);
		error = xfs_defer_finish(&tp, &dfops);
		if (error)
			goto out_bmap_cancel;

		error = xfs_trans_roll_inode(&tp, ip);
		if (error)
			goto out;
	}

	if (whichfork == XFS_DATA_FORK) {
		/* Remove all pending CoW reservations. */
		error = xfs_reflink_cancel_cow_blocks(ip, &tp,
				first_unmap_block, last_block, true);
		if (error)
			goto out;

		xfs_itruncate_clear_reflink_flags(ip);
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
	xfs_defer_cancel(&dfops);
	goto out;
}

int
xfs_release(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		error;

	if (!S_ISREG(VFS_I(ip)->i_mode) || (VFS_I(ip)->i_mode == 0))
		return 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return 0;

	if (!XFS_FORCED_SHUTDOWN(mp)) {
		int truncated;

		/*
		 * If we previously truncated this file and removed old data
		 * in the process, we want to initiate "early" writeout on
		 * the last close.  This is an attempt to combat the notorious
		 * NULL files problem which is particularly noticeable from a
		 * truncate down, buffered (re-)write (delalloc), followed by
		 * a crash.  What we are effectively doing here is
		 * significantly reducing the time window where we'd otherwise
		 * be exposed to that problem.
		 */
		truncated = xfs_iflags_test_and_clear(ip, XFS_ITRUNCATED);
		if (truncated) {
			xfs_iflags_clear(ip, XFS_IDIRTY_RELEASE);
			if (ip->i_delayed_blks > 0) {
				error = filemap_flush(VFS_I(ip)->i_mapping);
				if (error)
					return error;
			}
		}
	}

	if (VFS_I(ip)->i_nlink == 0)
		return 0;

	if (xfs_can_free_eofblocks(ip, false)) {

		/*
		 * Check if the inode is being opened, written and closed
		 * frequently and we have delayed allocation blocks outstanding
		 * (e.g. streaming writes from the NFS server), truncating the
		 * blocks past EOF will cause fragmentation to occur.
		 *
		 * In this case don't do the truncation, but we have to be
		 * careful how we detect this case. Blocks beyond EOF show up as
		 * i_delayed_blks even when the inode is clean, so we need to
		 * truncate them away first before checking for a dirty release.
		 * Hence on the first dirty close we will still remove the
		 * speculative allocation, but after that we will leave it in
		 * place.
		 */
		if (xfs_iflags_test(ip, XFS_IDIRTY_RELEASE))
			return 0;
		/*
		 * If we can't get the iolock just skip truncating the blocks
		 * past EOF because we could deadlock with the mmap_sem
		 * otherwise. We'll get another chance to drop them once the
		 * last reference to the inode is dropped, so we'll never leak
		 * blocks permanently.
		 */
		if (xfs_ilock_nowait(ip, XFS_IOLOCK_EXCL)) {
			error = xfs_free_eofblocks(ip);
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			if (error)
				return error;
		}

		/* delalloc blocks after truncation means it really is dirty */
		if (ip->i_delayed_blks)
			xfs_iflags_set(ip, XFS_IDIRTY_RELEASE);
	}
	return 0;
}

/*
 * xfs_inactive_truncate
 *
 * Called to perform a truncate when an inode becomes unlinked.
 */
STATIC int
xfs_inactive_truncate(
	struct xfs_inode *ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate, 0, 0, 0, &tp);
	if (error) {
		ASSERT(XFS_FORCED_SHUTDOWN(mp));
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * Log the inode size first to prevent stale data exposure in the event
	 * of a system crash before the truncate completes. See the related
	 * comment in xfs_vn_setattr_size() for details.
	 */
	ip->i_d.di_size = 0;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK, 0);
	if (error)
		goto error_trans_cancel;

	ASSERT(ip->i_d.di_nextents == 0);

	error = xfs_trans_commit(tp);
	if (error)
		goto error_unlock;

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;

error_trans_cancel:
	xfs_trans_cancel(tp);
error_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * xfs_inactive_ifree()
 *
 * Perform the inode free when an inode is unlinked.
 */
STATIC int
xfs_inactive_ifree(
	struct xfs_inode *ip)
{
	struct xfs_defer_ops	dfops;
	xfs_fsblock_t		first_block;
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	/*
	 * We try to use a per-AG reservation for any block needed by the finobt
	 * tree, but as the finobt feature predates the per-AG reservation
	 * support a degraded file system might not have enough space for the
	 * reservation at mount time.  In that case try to dip into the reserved
	 * pool and pray.
	 *
	 * Send a warning if the reservation does happen to fail, as the inode
	 * now remains allocated and sits on the unlinked list until the fs is
	 * repaired.
	 */
	if (unlikely(mp->m_inotbt_nores)) {
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ifree,
				XFS_IFREE_SPACE_RES(mp), 0, XFS_TRANS_RESERVE,
				&tp);
	} else {
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ifree, 0, 0, 0, &tp);
	}
	if (error) {
		if (error == -ENOSPC) {
			xfs_warn_ratelimited(mp,
			"Failed to remove inode(s) from unlinked list. "
			"Please free space, unmount and run xfs_repair.");
		} else {
			ASSERT(XFS_FORCED_SHUTDOWN(mp));
		}
		return error;
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	xfs_defer_init(&dfops, &first_block);
	tp->t_agfl_dfops = &dfops;
	error = xfs_ifree(tp, ip, &dfops);
	if (error) {
		/*
		 * If we fail to free the inode, shut down.  The cancel
		 * might do that, we need to make sure.  Otherwise the
		 * inode might be lost for a long time or forever.
		 */
		if (!XFS_FORCED_SHUTDOWN(mp)) {
			xfs_notice(mp, "%s: xfs_ifree returned error %d",
				__func__, error);
			xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		}
		xfs_trans_cancel(tp);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		return error;
	}

	/*
	 * Credit the quota account(s). The inode is gone.
	 */
	xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_ICOUNT, -1);

	/*
	 * Just ignore errors at this point.  There is nothing we can do except
	 * to try to keep going. Make sure it's not a silent error.
	 */
	error = xfs_defer_finish(&tp, &dfops);
	if (error) {
		xfs_notice(mp, "%s: xfs_defer_finish returned error %d",
			__func__, error);
		xfs_defer_cancel(&dfops);
	}
	error = xfs_trans_commit(tp);
	if (error)
		xfs_notice(mp, "%s: xfs_trans_commit returned error %d",
			__func__, error);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * xfs_inactive
 *
 * This is called when the vnode reference count for the vnode
 * goes to zero.  If the file has been unlinked, then it must
 * now be truncated.  Also, we clear all of the read-ahead state
 * kept for the inode here since the file is now closed.
 */
void
xfs_inactive(
	xfs_inode_t	*ip)
{
	struct xfs_mount	*mp;
	struct xfs_ifork	*cow_ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	int			error;
	int			truncate = 0;

	/*
	 * If the inode is already free, then there can be nothing
	 * to clean up here.
	 */
	if (VFS_I(ip)->i_mode == 0) {
		ASSERT(ip->i_df.if_real_bytes == 0);
		ASSERT(ip->i_df.if_broot_bytes == 0);
		return;
	}

	mp = ip->i_mount;
	ASSERT(!xfs_iflags_test(ip, XFS_IRECOVERY));

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (mp->m_flags & XFS_MOUNT_RDONLY)
		return;

	/* Try to clean out the cow blocks if there are any. */
	if (xfs_is_reflink_inode(ip) && cow_ifp->if_bytes > 0)
		xfs_reflink_cancel_cow_range(ip, 0, NULLFILEOFF, true);

	if (VFS_I(ip)->i_nlink != 0) {
		/*
		 * force is true because we are evicting an inode from the
		 * cache. Post-eof blocks must be freed, lest we end up with
		 * broken free space accounting.
		 *
		 * Note: don't bother with iolock here since lockdep complains
		 * about acquiring it in reclaim context. We have the only
		 * reference to the inode at this point anyways.
		 */
		if (xfs_can_free_eofblocks(ip, true))
			xfs_free_eofblocks(ip);

		return;
	}

	if (S_ISREG(VFS_I(ip)->i_mode) &&
	    (ip->i_d.di_size != 0 || XFS_ISIZE(ip) != 0 ||
	     ip->i_d.di_nextents > 0 || ip->i_delayed_blks > 0))
		truncate = 1;

	error = xfs_qm_dqattach(ip);
	if (error)
		return;

	if (S_ISLNK(VFS_I(ip)->i_mode))
		error = xfs_inactive_symlink(ip);
	else if (truncate)
		error = xfs_inactive_truncate(ip);
	if (error)
		return;

	/*
	 * If there are attributes associated with the file then blow them away
	 * now.  The code calls a routine that recursively deconstructs the
	 * attribute fork. If also blows away the in-core attribute fork.
	 */
	if (XFS_IFORK_Q(ip)) {
		error = xfs_attr_inactive(ip);
		if (error)
			return;
	}

	ASSERT(!ip->i_afp);
	ASSERT(ip->i_d.di_anextents == 0);
	ASSERT(ip->i_d.di_forkoff == 0);

	/*
	 * Free the inode.
	 */
	error = xfs_inactive_ifree(ip);
	if (error)
		return;

	/*
	 * Release the dquots held by inode, if any.
	 */
	xfs_qm_dqdetach(ip);
}

/*
 * This is called when the inode's link count goes to 0 or we are creating a
 * tmpfile via O_TMPFILE. In the case of a tmpfile, @ignore_linkcount will be
 * set to true as the link count is dropped to zero by the VFS after we've
 * created the file successfully, so we have to add it to the unlinked list
 * while the link count is non-zero.
 *
 * We place the on-disk inode on a list in the AGI.  It will be pulled from this
 * list when the inode is freed.
 */
STATIC int
xfs_iunlink(
	struct xfs_trans *tp,
	struct xfs_inode *ip)
{
	xfs_mount_t	*mp = tp->t_mountp;
	xfs_agi_t	*agi;
	xfs_dinode_t	*dip;
	xfs_buf_t	*agibp;
	xfs_buf_t	*ibp;
	xfs_agino_t	agino;
	short		bucket_index;
	int		offset;
	int		error;

	ASSERT(VFS_I(ip)->i_mode != 0);

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
 * A big issue when freeing the inode cluster is that we _cannot_ skip any
 * inodes that are in memory - they all must be marked stale and attached to
 * the cluster buffer.
 */
STATIC int
xfs_ifree_cluster(
	xfs_inode_t		*free_ip,
	xfs_trans_t		*tp,
	struct xfs_icluster	*xic)
{
	xfs_mount_t		*mp = free_ip->i_mount;
	int			blks_per_cluster;
	int			inodes_per_cluster;
	int			nbufs;
	int			i, j;
	int			ioffset;
	xfs_daddr_t		blkno;
	xfs_buf_t		*bp;
	xfs_inode_t		*ip;
	xfs_inode_log_item_t	*iip;
	struct xfs_log_item	*lip;
	struct xfs_perag	*pag;
	xfs_ino_t		inum;

	inum = xic->first_ino;
	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, inum));
	blks_per_cluster = xfs_icluster_size_fsb(mp);
	inodes_per_cluster = blks_per_cluster << mp->m_sb.sb_inopblog;
	nbufs = mp->m_ialloc_blks / blks_per_cluster;

	for (j = 0; j < nbufs; j++, inum += inodes_per_cluster) {
		/*
		 * The allocation bitmap tells us which inodes of the chunk were
		 * physically allocated. Skip the cluster if an inode falls into
		 * a sparse region.
		 */
		ioffset = inum - xic->first_ino;
		if ((xic->alloc & XFS_INOBT_MASK(ioffset)) == 0) {
			ASSERT(do_mod(ioffset, inodes_per_cluster) == 0);
			continue;
		}

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
			return -ENOMEM;

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
		list_for_each_entry(lip, &bp->b_li_list, li_bio_list) {
			if (lip->li_type == XFS_LI_INODE) {
				iip = (xfs_inode_log_item_t *)lip;
				ASSERT(iip->ili_logged == 1);
				lip->li_cb = xfs_istale_done;
				xfs_trans_ail_copy_lsn(mp->m_ail,
							&iip->ili_flush_lsn,
							&iip->ili_item.li_lsn);
				xfs_iflags_set(iip->ili_inode, XFS_ISTALE);
			}
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
		for (i = 0; i < inodes_per_cluster; i++) {
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
			if (ip != free_ip) {
				if (!xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
					rcu_read_unlock();
					delay(1);
					goto retry;
				}

				/*
				 * Check the inode number again in case we're
				 * racing with freeing in xfs_reclaim_inode().
				 * See the comments in that function for more
				 * information as to why the initial check is
				 * not sufficient.
				 */
				if (ip->i_ino != inum + i) {
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					rcu_read_unlock();
					continue;
				}
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
			iip->ili_fsync_fields = 0;
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
 * Free any local-format buffers sitting around before we reset to
 * extents format.
 */
static inline void
xfs_ifree_local_data(
	struct xfs_inode	*ip,
	int			whichfork)
{
	struct xfs_ifork	*ifp;

	if (XFS_IFORK_FORMAT(ip, whichfork) != XFS_DINODE_FMT_LOCAL)
		return;

	ifp = XFS_IFORK_PTR(ip, whichfork);
	xfs_idata_realloc(ip, -ifp->if_bytes, whichfork);
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
	struct xfs_defer_ops	*dfops)
{
	int			error;
	struct xfs_icluster	xic = { 0 };

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(VFS_I(ip)->i_nlink == 0);
	ASSERT(ip->i_d.di_nextents == 0);
	ASSERT(ip->i_d.di_anextents == 0);
	ASSERT(ip->i_d.di_size == 0 || !S_ISREG(VFS_I(ip)->i_mode));
	ASSERT(ip->i_d.di_nblocks == 0);

	/*
	 * Pull the on-disk inode from the AGI unlinked list.
	 */
	error = xfs_iunlink_remove(tp, ip);
	if (error)
		return error;

	error = xfs_difree(tp, ip->i_ino, dfops, &xic);
	if (error)
		return error;

	xfs_ifree_local_data(ip, XFS_DATA_FORK);
	xfs_ifree_local_data(ip, XFS_ATTR_FORK);

	VFS_I(ip)->i_mode = 0;		/* mark incore inode as free */
	ip->i_d.di_flags = 0;
	ip->i_d.di_flags2 = 0;
	ip->i_d.di_dmevmask = 0;
	ip->i_d.di_forkoff = 0;		/* mark the attr fork not in use */
	ip->i_d.di_format = XFS_DINODE_FMT_EXTENTS;
	ip->i_d.di_aformat = XFS_DINODE_FMT_EXTENTS;

	/* Don't attempt to replay owner changes for a deleted inode */
	ip->i_itemp->ili_fields &= ~(XFS_ILOG_AOWNER|XFS_ILOG_DOWNER);

	/*
	 * Bump the generation count so no one will be confused
	 * by reincarnations of this inode.
	 */
	VFS_I(ip)->i_generation++;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	if (xic.deleted)
		error = xfs_ifree_cluster(ip, tp, &xic);

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
	xfs_log_force_lsn(ip->i_mount, ip->i_itemp->ili_last_lsn, 0, NULL);

}

static void
__xfs_iunpin_wait(
	struct xfs_inode	*ip)
{
	wait_queue_head_t *wq = bit_waitqueue(&ip->i_flags, __XFS_IPINNED_BIT);
	DEFINE_WAIT_BIT(wait, &ip->i_flags, __XFS_IPINNED_BIT);

	xfs_iunpin(ip);

	do {
		prepare_to_wait(wq, &wait.wq_entry, TASK_UNINTERRUPTIBLE);
		if (xfs_ipincount(ip))
			io_schedule();
	} while (xfs_ipincount(ip));
	finish_wait(wq, &wait.wq_entry);
}

void
xfs_iunpin_wait(
	struct xfs_inode	*ip)
{
	if (xfs_ipincount(ip))
		__xfs_iunpin_wait(ip);
}

/*
 * Removing an inode from the namespace involves removing the directory entry
 * and dropping the link count on the inode. Removing the directory entry can
 * result in locking an AGF (directory blocks were freed) and removing a link
 * count can result in placing the inode on an unlinked list which results in
 * locking an AGI.
 *
 * The big problem here is that we have an ordering constraint on AGF and AGI
 * locking - inode allocation locks the AGI, then can allocate a new extent for
 * new inodes, locking the AGF after the AGI. Similarly, freeing the inode
 * removes the inode from the unlinked list, requiring that we lock the AGI
 * first, and then freeing the inode can result in an inode chunk being freed
 * and hence freeing disk space requiring that we lock an AGF.
 *
 * Hence the ordering that is imposed by other parts of the code is AGI before
 * AGF. This means we cannot remove the directory entry before we drop the inode
 * reference count and put it on the unlinked list as this results in a lock
 * order of AGF then AGI, and this can deadlock against inode allocation and
 * freeing. Therefore we must drop the link counts before we remove the
 * directory entry.
 *
 * This is still safe from a transactional point of view - it is not until we
 * get to xfs_defer_finish() that we have the possibility of multiple
 * transactions in this operation. Hence as long as we remove the directory
 * entry and drop the link count in the first transaction of the remove
 * operation, there are no transactional constraints on the ordering here.
 */
int
xfs_remove(
	xfs_inode_t             *dp,
	struct xfs_name		*name,
	xfs_inode_t		*ip)
{
	xfs_mount_t		*mp = dp->i_mount;
	xfs_trans_t             *tp = NULL;
	int			is_dir = S_ISDIR(VFS_I(ip)->i_mode);
	int                     error = 0;
	struct xfs_defer_ops	dfops;
	xfs_fsblock_t           first_block;
	uint			resblks;

	trace_xfs_remove(dp, name);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	error = xfs_qm_dqattach(dp);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(ip);
	if (error)
		goto std_return;

	/*
	 * We try to get the real space reservation first,
	 * allowing for directory btree deletion(s) implying
	 * possible bmap insert(s).  If we can't get the space
	 * reservation then we use 0 instead, and avoid the bmap
	 * btree insert(s) in the directory code by, if the bmap
	 * insert tries to happen, instead trimming the LAST
	 * block from the directory.
	 */
	resblks = XFS_REMOVE_SPACE_RES(mp);
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_remove, resblks, 0, 0, &tp);
	if (error == -ENOSPC) {
		resblks = 0;
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_remove, 0, 0, 0,
				&tp);
	}
	if (error) {
		ASSERT(error != -ENOSPC);
		goto std_return;
	}

	xfs_lock_two_inodes(dp, XFS_ILOCK_EXCL, ip, XFS_ILOCK_EXCL);

	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	/*
	 * If we're removing a directory perform some additional validation.
	 */
	if (is_dir) {
		ASSERT(VFS_I(ip)->i_nlink >= 2);
		if (VFS_I(ip)->i_nlink != 2) {
			error = -ENOTEMPTY;
			goto out_trans_cancel;
		}
		if (!xfs_dir_isempty(ip)) {
			error = -ENOTEMPTY;
			goto out_trans_cancel;
		}

		/* Drop the link from ip's "..".  */
		error = xfs_droplink(tp, dp);
		if (error)
			goto out_trans_cancel;

		/* Drop the "." link from ip to self.  */
		error = xfs_droplink(tp, ip);
		if (error)
			goto out_trans_cancel;
	} else {
		/*
		 * When removing a non-directory we need to log the parent
		 * inode here.  For a directory this is done implicitly
		 * by the xfs_droplink call for the ".." entry.
		 */
		xfs_trans_log_inode(tp, dp, XFS_ILOG_CORE);
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	/* Drop the link from dp to ip. */
	error = xfs_droplink(tp, ip);
	if (error)
		goto out_trans_cancel;

	xfs_defer_init(&dfops, &first_block);
	tp->t_agfl_dfops = &dfops;
	error = xfs_dir_removename(tp, dp, name, ip->i_ino,
					&first_block, &dfops, resblks);
	if (error) {
		ASSERT(error != -ENOENT);
		goto out_bmap_cancel;
	}

	/*
	 * If this is a synchronous mount, make sure that the
	 * remove transaction goes to disk before returning to
	 * the user.
	 */
	if (mp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	error = xfs_defer_finish(&tp, &dfops);
	if (error)
		goto out_bmap_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto std_return;

	if (is_dir && xfs_inode_is_filestream(ip))
		xfs_filestream_deassociate(ip);

	return 0;

 out_bmap_cancel:
	xfs_defer_cancel(&dfops);
 out_trans_cancel:
	xfs_trans_cancel(tp);
 std_return:
	return error;
}

/*
 * Enter all inodes for a rename transaction into a sorted array.
 */
#define __XFS_SORT_INODES	5
STATIC void
xfs_sort_for_rename(
	struct xfs_inode	*dp1,	/* in: old (source) directory inode */
	struct xfs_inode	*dp2,	/* in: new (target) directory inode */
	struct xfs_inode	*ip1,	/* in: inode of old entry */
	struct xfs_inode	*ip2,	/* in: inode of new entry */
	struct xfs_inode	*wip,	/* in: whiteout inode */
	struct xfs_inode	**i_tab,/* out: sorted array of inodes */
	int			*num_inodes)  /* in/out: inodes in array */
{
	int			i, j;

	ASSERT(*num_inodes == __XFS_SORT_INODES);
	memset(i_tab, 0, *num_inodes * sizeof(struct xfs_inode *));

	/*
	 * i_tab contains a list of pointers to inodes.  We initialize
	 * the table here & we'll sort it.  We will then use it to
	 * order the acquisition of the inode locks.
	 *
	 * Note that the table may contain duplicates.  e.g., dp1 == dp2.
	 */
	i = 0;
	i_tab[i++] = dp1;
	i_tab[i++] = dp2;
	i_tab[i++] = ip1;
	if (ip2)
		i_tab[i++] = ip2;
	if (wip)
		i_tab[i++] = wip;
	*num_inodes = i;

	/*
	 * Sort the elements via bubble sort.  (Remember, there are at
	 * most 5 elements to sort, so this is adequate.)
	 */
	for (i = 0; i < *num_inodes; i++) {
		for (j = 1; j < *num_inodes; j++) {
			if (i_tab[j]->i_ino < i_tab[j-1]->i_ino) {
				struct xfs_inode *temp = i_tab[j];
				i_tab[j] = i_tab[j-1];
				i_tab[j-1] = temp;
			}
		}
	}
}

static int
xfs_finish_rename(
	struct xfs_trans	*tp,
	struct xfs_defer_ops	*dfops)
{
	int			error;

	/*
	 * If this is a synchronous mount, make sure that the rename transaction
	 * goes to disk before returning to the user.
	 */
	if (tp->t_mountp->m_flags & (XFS_MOUNT_WSYNC|XFS_MOUNT_DIRSYNC))
		xfs_trans_set_sync(tp);

	error = xfs_defer_finish(&tp, dfops);
	if (error) {
		xfs_defer_cancel(dfops);
		xfs_trans_cancel(tp);
		return error;
	}

	return xfs_trans_commit(tp);
}

/*
 * xfs_cross_rename()
 *
 * responsible for handling RENAME_EXCHANGE flag in renameat2() sytemcall
 */
STATIC int
xfs_cross_rename(
	struct xfs_trans	*tp,
	struct xfs_inode	*dp1,
	struct xfs_name		*name1,
	struct xfs_inode	*ip1,
	struct xfs_inode	*dp2,
	struct xfs_name		*name2,
	struct xfs_inode	*ip2,
	struct xfs_defer_ops	*dfops,
	xfs_fsblock_t		*first_block,
	int			spaceres)
{
	int		error = 0;
	int		ip1_flags = 0;
	int		ip2_flags = 0;
	int		dp2_flags = 0;

	/* Swap inode number for dirent in first parent */
	error = xfs_dir_replace(tp, dp1, name1,
				ip2->i_ino,
				first_block, dfops, spaceres);
	if (error)
		goto out_trans_abort;

	/* Swap inode number for dirent in second parent */
	error = xfs_dir_replace(tp, dp2, name2,
				ip1->i_ino,
				first_block, dfops, spaceres);
	if (error)
		goto out_trans_abort;

	/*
	 * If we're renaming one or more directories across different parents,
	 * update the respective ".." entries (and link counts) to match the new
	 * parents.
	 */
	if (dp1 != dp2) {
		dp2_flags = XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;

		if (S_ISDIR(VFS_I(ip2)->i_mode)) {
			error = xfs_dir_replace(tp, ip2, &xfs_name_dotdot,
						dp1->i_ino, first_block,
						dfops, spaceres);
			if (error)
				goto out_trans_abort;

			/* transfer ip2 ".." reference to dp1 */
			if (!S_ISDIR(VFS_I(ip1)->i_mode)) {
				error = xfs_droplink(tp, dp2);
				if (error)
					goto out_trans_abort;
				error = xfs_bumplink(tp, dp1);
				if (error)
					goto out_trans_abort;
			}

			/*
			 * Although ip1 isn't changed here, userspace needs
			 * to be warned about the change, so that applications
			 * relying on it (like backup ones), will properly
			 * notify the change
			 */
			ip1_flags |= XFS_ICHGTIME_CHG;
			ip2_flags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
		}

		if (S_ISDIR(VFS_I(ip1)->i_mode)) {
			error = xfs_dir_replace(tp, ip1, &xfs_name_dotdot,
						dp2->i_ino, first_block,
						dfops, spaceres);
			if (error)
				goto out_trans_abort;

			/* transfer ip1 ".." reference to dp2 */
			if (!S_ISDIR(VFS_I(ip2)->i_mode)) {
				error = xfs_droplink(tp, dp1);
				if (error)
					goto out_trans_abort;
				error = xfs_bumplink(tp, dp2);
				if (error)
					goto out_trans_abort;
			}

			/*
			 * Although ip2 isn't changed here, userspace needs
			 * to be warned about the change, so that applications
			 * relying on it (like backup ones), will properly
			 * notify the change
			 */
			ip1_flags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
			ip2_flags |= XFS_ICHGTIME_CHG;
		}
	}

	if (ip1_flags) {
		xfs_trans_ichgtime(tp, ip1, ip1_flags);
		xfs_trans_log_inode(tp, ip1, XFS_ILOG_CORE);
	}
	if (ip2_flags) {
		xfs_trans_ichgtime(tp, ip2, ip2_flags);
		xfs_trans_log_inode(tp, ip2, XFS_ILOG_CORE);
	}
	if (dp2_flags) {
		xfs_trans_ichgtime(tp, dp2, dp2_flags);
		xfs_trans_log_inode(tp, dp2, XFS_ILOG_CORE);
	}
	xfs_trans_ichgtime(tp, dp1, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, dp1, XFS_ILOG_CORE);
	return xfs_finish_rename(tp, dfops);

out_trans_abort:
	xfs_defer_cancel(dfops);
	xfs_trans_cancel(tp);
	return error;
}

/*
 * xfs_rename_alloc_whiteout()
 *
 * Return a referenced, unlinked, unlocked inode that that can be used as a
 * whiteout in a rename transaction. We use a tmpfile inode here so that if we
 * crash between allocating the inode and linking it into the rename transaction
 * recovery will free the inode and we won't leak it.
 */
static int
xfs_rename_alloc_whiteout(
	struct xfs_inode	*dp,
	struct xfs_inode	**wip)
{
	struct xfs_inode	*tmpfile;
	int			error;

	error = xfs_create_tmpfile(dp, S_IFCHR | WHITEOUT_MODE, &tmpfile);
	if (error)
		return error;

	/*
	 * Prepare the tmpfile inode as if it were created through the VFS.
	 * Otherwise, the link increment paths will complain about nlink 0->1.
	 * Drop the link count as done by d_tmpfile(), complete the inode setup
	 * and flag it as linkable.
	 */
	drop_nlink(VFS_I(tmpfile));
	xfs_setup_iops(tmpfile);
	xfs_finish_inode_setup(tmpfile);
	VFS_I(tmpfile)->i_state |= I_LINKABLE;

	*wip = tmpfile;
	return 0;
}

/*
 * xfs_rename
 */
int
xfs_rename(
	struct xfs_inode	*src_dp,
	struct xfs_name		*src_name,
	struct xfs_inode	*src_ip,
	struct xfs_inode	*target_dp,
	struct xfs_name		*target_name,
	struct xfs_inode	*target_ip,
	unsigned int		flags)
{
	struct xfs_mount	*mp = src_dp->i_mount;
	struct xfs_trans	*tp;
	struct xfs_defer_ops	dfops;
	xfs_fsblock_t		first_block;
	struct xfs_inode	*wip = NULL;		/* whiteout inode */
	struct xfs_inode	*inodes[__XFS_SORT_INODES];
	int			num_inodes = __XFS_SORT_INODES;
	bool			new_parent = (src_dp != target_dp);
	bool			src_is_directory = S_ISDIR(VFS_I(src_ip)->i_mode);
	int			spaceres;
	int			error;

	trace_xfs_rename(src_dp, target_dp, src_name, target_name);

	if ((flags & RENAME_EXCHANGE) && !target_ip)
		return -EINVAL;

	/*
	 * If we are doing a whiteout operation, allocate the whiteout inode
	 * we will be placing at the target and ensure the type is set
	 * appropriately.
	 */
	if (flags & RENAME_WHITEOUT) {
		ASSERT(!(flags & (RENAME_NOREPLACE | RENAME_EXCHANGE)));
		error = xfs_rename_alloc_whiteout(target_dp, &wip);
		if (error)
			return error;

		/* setup target dirent info as whiteout */
		src_name->type = XFS_DIR3_FT_CHRDEV;
	}

	xfs_sort_for_rename(src_dp, target_dp, src_ip, target_ip, wip,
				inodes, &num_inodes);

	spaceres = XFS_RENAME_SPACE_RES(mp, target_name->len);
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_rename, spaceres, 0, 0, &tp);
	if (error == -ENOSPC) {
		spaceres = 0;
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_rename, 0, 0, 0,
				&tp);
	}
	if (error)
		goto out_release_wip;

	/*
	 * Attach the dquots to the inodes
	 */
	error = xfs_qm_vop_rename_dqattach(inodes);
	if (error)
		goto out_trans_cancel;

	/*
	 * Lock all the participating inodes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 4 inodes.
	 */
	xfs_lock_inodes(inodes, num_inodes, XFS_ILOCK_EXCL);

	/*
	 * Join all the inodes to the transaction. From this point on,
	 * we can rely on either trans_commit or trans_cancel to unlock
	 * them.
	 */
	xfs_trans_ijoin(tp, src_dp, XFS_ILOCK_EXCL);
	if (new_parent)
		xfs_trans_ijoin(tp, target_dp, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, src_ip, XFS_ILOCK_EXCL);
	if (target_ip)
		xfs_trans_ijoin(tp, target_ip, XFS_ILOCK_EXCL);
	if (wip)
		xfs_trans_ijoin(tp, wip, XFS_ILOCK_EXCL);

	/*
	 * If we are using project inheritance, we only allow renames
	 * into our tree when the project IDs are the same; else the
	 * tree quota mechanism would be circumvented.
	 */
	if (unlikely((target_dp->i_d.di_flags & XFS_DIFLAG_PROJINHERIT) &&
		     (xfs_get_projid(target_dp) != xfs_get_projid(src_ip)))) {
		error = -EXDEV;
		goto out_trans_cancel;
	}

	xfs_defer_init(&dfops, &first_block);
	tp->t_agfl_dfops = &dfops;

	/* RENAME_EXCHANGE is unique from here on. */
	if (flags & RENAME_EXCHANGE)
		return xfs_cross_rename(tp, src_dp, src_name, src_ip,
					target_dp, target_name, target_ip,
					&dfops, &first_block, spaceres);

	/*
	 * Set up the target.
	 */
	if (target_ip == NULL) {
		/*
		 * If there's no space reservation, check the entry will
		 * fit before actually inserting it.
		 */
		if (!spaceres) {
			error = xfs_dir_canenter(tp, target_dp, target_name);
			if (error)
				goto out_trans_cancel;
		}
		/*
		 * If target does not exist and the rename crosses
		 * directories, adjust the target directory link count
		 * to account for the ".." reference from the new entry.
		 */
		error = xfs_dir_createname(tp, target_dp, target_name,
						src_ip->i_ino, &first_block,
						&dfops, spaceres);
		if (error)
			goto out_bmap_cancel;

		xfs_trans_ichgtime(tp, target_dp,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		if (new_parent && src_is_directory) {
			error = xfs_bumplink(tp, target_dp);
			if (error)
				goto out_bmap_cancel;
		}
	} else { /* target_ip != NULL */
		/*
		 * If target exists and it's a directory, check that both
		 * target and source are directories and that target can be
		 * destroyed, or that neither is a directory.
		 */
		if (S_ISDIR(VFS_I(target_ip)->i_mode)) {
			/*
			 * Make sure target dir is empty.
			 */
			if (!(xfs_dir_isempty(target_ip)) ||
			    (VFS_I(target_ip)->i_nlink > 2)) {
				error = -EEXIST;
				goto out_trans_cancel;
			}
		}

		/*
		 * Link the source inode under the target name.
		 * If the source inode is a directory and we are moving
		 * it across directories, its ".." entry will be
		 * inconsistent until we replace that down below.
		 *
		 * In case there is already an entry with the same
		 * name at the destination directory, remove it first.
		 */
		error = xfs_dir_replace(tp, target_dp, target_name,
					src_ip->i_ino,
					&first_block, &dfops, spaceres);
		if (error)
			goto out_bmap_cancel;

		xfs_trans_ichgtime(tp, target_dp,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		/*
		 * Decrement the link count on the target since the target
		 * dir no longer points to it.
		 */
		error = xfs_droplink(tp, target_ip);
		if (error)
			goto out_bmap_cancel;

		if (src_is_directory) {
			/*
			 * Drop the link from the old "." entry.
			 */
			error = xfs_droplink(tp, target_ip);
			if (error)
				goto out_bmap_cancel;
		}
	} /* target_ip != NULL */

	/*
	 * Remove the source.
	 */
	if (new_parent && src_is_directory) {
		/*
		 * Rewrite the ".." entry to point to the new
		 * directory.
		 */
		error = xfs_dir_replace(tp, src_ip, &xfs_name_dotdot,
					target_dp->i_ino,
					&first_block, &dfops, spaceres);
		ASSERT(error != -EEXIST);
		if (error)
			goto out_bmap_cancel;
	}

	/*
	 * We always want to hit the ctime on the source inode.
	 *
	 * This isn't strictly required by the standards since the source
	 * inode isn't really being changed, but old unix file systems did
	 * it and some incremental backup programs won't work without it.
	 */
	xfs_trans_ichgtime(tp, src_ip, XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, src_ip, XFS_ILOG_CORE);

	/*
	 * Adjust the link count on src_dp.  This is necessary when
	 * renaming a directory, either within one parent when
	 * the target existed, or across two parent directories.
	 */
	if (src_is_directory && (new_parent || target_ip != NULL)) {

		/*
		 * Decrement link count on src_directory since the
		 * entry that's moved no longer points to it.
		 */
		error = xfs_droplink(tp, src_dp);
		if (error)
			goto out_bmap_cancel;
	}

	/*
	 * For whiteouts, we only need to update the source dirent with the
	 * inode number of the whiteout inode rather than removing it
	 * altogether.
	 */
	if (wip) {
		error = xfs_dir_replace(tp, src_dp, src_name, wip->i_ino,
					&first_block, &dfops, spaceres);
	} else
		error = xfs_dir_removename(tp, src_dp, src_name, src_ip->i_ino,
					   &first_block, &dfops, spaceres);
	if (error)
		goto out_bmap_cancel;

	/*
	 * For whiteouts, we need to bump the link count on the whiteout inode.
	 * This means that failures all the way up to this point leave the inode
	 * on the unlinked list and so cleanup is a simple matter of dropping
	 * the remaining reference to it. If we fail here after bumping the link
	 * count, we're shutting down the filesystem so we'll never see the
	 * intermediate state on disk.
	 */
	if (wip) {
		ASSERT(VFS_I(wip)->i_nlink == 0);
		error = xfs_bumplink(tp, wip);
		if (error)
			goto out_bmap_cancel;
		error = xfs_iunlink_remove(tp, wip);
		if (error)
			goto out_bmap_cancel;
		xfs_trans_log_inode(tp, wip, XFS_ILOG_CORE);

		/*
		 * Now we have a real link, clear the "I'm a tmpfile" state
		 * flag from the inode so it doesn't accidentally get misused in
		 * future.
		 */
		VFS_I(wip)->i_state &= ~I_LINKABLE;
	}

	xfs_trans_ichgtime(tp, src_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_inode(tp, src_dp, XFS_ILOG_CORE);
	if (new_parent)
		xfs_trans_log_inode(tp, target_dp, XFS_ILOG_CORE);

	error = xfs_finish_rename(tp, &dfops);
	if (wip)
		IRELE(wip);
	return error;

out_bmap_cancel:
	xfs_defer_cancel(&dfops);
out_trans_cancel:
	xfs_trans_cancel(tp);
out_release_wip:
	if (wip)
		IRELE(wip);
	return error;
}

STATIC int
xfs_iflush_cluster(
	struct xfs_inode	*ip,
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_perag	*pag;
	unsigned long		first_index, mask;
	unsigned long		inodes_per_cluster;
	int			cilist_size;
	struct xfs_inode	**cilist;
	struct xfs_inode	*cip;
	int			nr_found;
	int			clcount = 0;
	int			bufwasdelwri;
	int			i;

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));

	inodes_per_cluster = mp->m_inode_cluster_size >> mp->m_sb.sb_inodelog;
	cilist_size = inodes_per_cluster * sizeof(xfs_inode_t *);
	cilist = kmem_alloc(cilist_size, KM_MAYFAIL|KM_NOFS);
	if (!cilist)
		goto out_put;

	mask = ~(((mp->m_inode_cluster_size >> mp->m_sb.sb_inodelog)) - 1);
	first_index = XFS_INO_TO_AGINO(mp, ip->i_ino) & mask;
	rcu_read_lock();
	/* really need a gang lookup range call here */
	nr_found = radix_tree_gang_lookup(&pag->pag_ici_root, (void**)cilist,
					first_index, inodes_per_cluster);
	if (nr_found == 0)
		goto out_free;

	for (i = 0; i < nr_found; i++) {
		cip = cilist[i];
		if (cip == ip)
			continue;

		/*
		 * because this is an RCU protected lookup, we could find a
		 * recently freed or even reallocated inode during the lookup.
		 * We need to check under the i_flags_lock for a valid inode
		 * here. Skip it if it is not valid or the wrong inode.
		 */
		spin_lock(&cip->i_flags_lock);
		if (!cip->i_ino ||
		    __xfs_iflags_test(cip, XFS_ISTALE)) {
			spin_unlock(&cip->i_flags_lock);
			continue;
		}

		/*
		 * Once we fall off the end of the cluster, no point checking
		 * any more inodes in the list because they will also all be
		 * outside the cluster.
		 */
		if ((XFS_INO_TO_AGINO(mp, cip->i_ino) & mask) != first_index) {
			spin_unlock(&cip->i_flags_lock);
			break;
		}
		spin_unlock(&cip->i_flags_lock);

		/*
		 * Do an un-protected check to see if the inode is dirty and
		 * is a candidate for flushing.  These checks will be repeated
		 * later after the appropriate locks are acquired.
		 */
		if (xfs_inode_clean(cip) && xfs_ipincount(cip) == 0)
			continue;

		/*
		 * Try to get locks.  If any are unavailable or it is pinned,
		 * then this inode cannot be flushed and is skipped.
		 */

		if (!xfs_ilock_nowait(cip, XFS_ILOCK_SHARED))
			continue;
		if (!xfs_iflock_nowait(cip)) {
			xfs_iunlock(cip, XFS_ILOCK_SHARED);
			continue;
		}
		if (xfs_ipincount(cip)) {
			xfs_ifunlock(cip);
			xfs_iunlock(cip, XFS_ILOCK_SHARED);
			continue;
		}


		/*
		 * Check the inode number again, just to be certain we are not
		 * racing with freeing in xfs_reclaim_inode(). See the comments
		 * in that function for more information as to why the initial
		 * check is not sufficient.
		 */
		if (!cip->i_ino) {
			xfs_ifunlock(cip);
			xfs_iunlock(cip, XFS_ILOCK_SHARED);
			continue;
		}

		/*
		 * arriving here means that this inode can be flushed.  First
		 * re-check that it's dirty before flushing.
		 */
		if (!xfs_inode_clean(cip)) {
			int	error;
			error = xfs_iflush_int(cip, bp);
			if (error) {
				xfs_iunlock(cip, XFS_ILOCK_SHARED);
				goto cluster_corrupt_out;
			}
			clcount++;
		} else {
			xfs_ifunlock(cip);
		}
		xfs_iunlock(cip, XFS_ILOCK_SHARED);
	}

	if (clcount) {
		XFS_STATS_INC(mp, xs_icluster_flushcnt);
		XFS_STATS_ADD(mp, xs_icluster_flushinode, clcount);
	}

out_free:
	rcu_read_unlock();
	kmem_free(cilist);
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
			bp->b_flags &= ~XBF_DONE;
			xfs_buf_stale(bp);
			xfs_buf_ioerror(bp, -EIO);
			xfs_buf_ioend(bp);
		} else {
			xfs_buf_stale(bp);
			xfs_buf_relse(bp);
		}
	}

	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(cip, false);
	kmem_free(cilist);
	xfs_perag_put(pag);
	return -EFSCORRUPTED;
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
	struct xfs_buf		*bp = NULL;
	struct xfs_dinode	*dip;
	int			error;

	XFS_STATS_INC(mp, xs_iflush_count);

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
		error = -EIO;
		goto abort_out;
	}

	/*
	 * Get the buffer containing the on-disk inode. We are doing a try-lock
	 * operation here, so we may get  an EAGAIN error. In that case, we
	 * simply want to return with the inode still dirty.
	 *
	 * If we get any other error, we effectively have a corruption situation
	 * and we cannot flush the inode, so we treat it the same as failing
	 * xfs_iflush_int().
	 */
	error = xfs_imap_to_bp(mp, NULL, &ip->i_imap, &dip, &bp, XBF_TRYLOCK,
			       0);
	if (error == -EAGAIN) {
		xfs_ifunlock(ip);
		return error;
	}
	if (error)
		goto corrupt_out;

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
	if (bp)
		xfs_buf_relse(bp);
	xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
cluster_corrupt_out:
	error = -EFSCORRUPTED;
abort_out:
	/*
	 * Unlocks the flush lock
	 */
	xfs_iflush_abort(ip, false);
	return error;
}

/*
 * If there are inline format data / attr forks attached to this inode,
 * make sure they're not corrupt.
 */
bool
xfs_inode_verify_forks(
	struct xfs_inode	*ip)
{
	struct xfs_ifork	*ifp;
	xfs_failaddr_t		fa;

	fa = xfs_ifork_verify_data(ip, &xfs_default_ifork_ops);
	if (fa) {
		ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, "data fork",
				ifp->if_u1.if_data, ifp->if_bytes, fa);
		return false;
	}

	fa = xfs_ifork_verify_attr(ip, &xfs_default_ifork_ops);
	if (fa) {
		ifp = XFS_IFORK_PTR(ip, XFS_ATTR_FORK);
		xfs_inode_verifier_error(ip, -EFSCORRUPTED, "attr fork",
				ifp ? ifp->if_u1.if_data : NULL,
				ifp ? ifp->if_bytes : 0, fa);
		return false;
	}
	return true;
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
	ASSERT(ip->i_d.di_version > 1);

	/* set *dip = inode's place in the buffer */
	dip = xfs_buf_offset(bp, ip->i_imap.im_boffset);

	if (XFS_TEST_ERROR(dip->di_magic != cpu_to_be16(XFS_DINODE_MAGIC),
			       mp, XFS_ERRTAG_IFLUSH_1)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad inode %Lu magic number 0x%x, ptr "PTR_FMT,
			__func__, ip->i_ino, be16_to_cpu(dip->di_magic), dip);
		goto corrupt_out;
	}
	if (S_ISREG(VFS_I(ip)->i_mode)) {
		if (XFS_TEST_ERROR(
		    (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_BTREE),
		    mp, XFS_ERRTAG_IFLUSH_3)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad regular inode %Lu, ptr "PTR_FMT,
				__func__, ip->i_ino, ip);
			goto corrupt_out;
		}
	} else if (S_ISDIR(VFS_I(ip)->i_mode)) {
		if (XFS_TEST_ERROR(
		    (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_BTREE) &&
		    (ip->i_d.di_format != XFS_DINODE_FMT_LOCAL),
		    mp, XFS_ERRTAG_IFLUSH_4)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad directory inode %Lu, ptr "PTR_FMT,
				__func__, ip->i_ino, ip);
			goto corrupt_out;
		}
	}
	if (XFS_TEST_ERROR(ip->i_d.di_nextents + ip->i_d.di_anextents >
				ip->i_d.di_nblocks, mp, XFS_ERRTAG_IFLUSH_5)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: detected corrupt incore inode %Lu, "
			"total extents = %d, nblocks = %Ld, ptr "PTR_FMT,
			__func__, ip->i_ino,
			ip->i_d.di_nextents + ip->i_d.di_anextents,
			ip->i_d.di_nblocks, ip);
		goto corrupt_out;
	}
	if (XFS_TEST_ERROR(ip->i_d.di_forkoff > mp->m_sb.sb_inodesize,
				mp, XFS_ERRTAG_IFLUSH_6)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: bad inode %Lu, forkoff 0x%x, ptr "PTR_FMT,
			__func__, ip->i_ino, ip->i_d.di_forkoff, ip);
		goto corrupt_out;
	}

	/*
	 * Inode item log recovery for v2 inodes are dependent on the
	 * di_flushiter count for correct sequencing. We bump the flush
	 * iteration count so we can detect flushes which postdate a log record
	 * during recovery. This is redundant as we now log every change and
	 * hence this can't happen but we need to still do it to ensure
	 * backwards compatibility with old kernels that predate logging all
	 * inode changes.
	 */
	if (ip->i_d.di_version < 3)
		ip->i_d.di_flushiter++;

	/* Check the inline fork data before we write out. */
	if (!xfs_inode_verify_forks(ip))
		goto corrupt_out;

	/*
	 * Copy the dirty parts of the inode into the on-disk inode.  We always
	 * copy out the core of the inode, because if the inode is dirty at all
	 * the core must be.
	 */
	xfs_inode_to_disk(ip, dip, iip->ili_item.li_lsn);

	/* Wrap, we never let the log put out DI_MAX_FLUSH */
	if (ip->i_d.di_flushiter == DI_MAX_FLUSH)
		ip->i_d.di_flushiter = 0;

	xfs_iflush_fork(ip, dip, iip, XFS_DATA_FORK);
	if (XFS_IFORK_Q(ip))
		xfs_iflush_fork(ip, dip, iip, XFS_ATTR_FORK);
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
	iip->ili_fsync_fields = 0;
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

	/* generate the checksum. */
	xfs_dinode_calc_crc(mp, dip);

	ASSERT(!list_empty(&bp->b_li_list));
	ASSERT(bp->b_iodone != NULL);
	return 0;

corrupt_out:
	return -EFSCORRUPTED;
}
