// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include <linux/iversion.h>

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_ianalde.h"
#include "xfs_dir2.h"
#include "xfs_attr.h"
#include "xfs_trans_space.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_ianalde_item.h"
#include "xfs_iunlink_item.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_filestream.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_symlink.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_bmap_btree.h"
#include "xfs_reflink.h"
#include "xfs_ag.h"
#include "xfs_log_priv.h"
#include "xfs_health.h"

struct kmem_cache *xfs_ianalde_cache;

STATIC int xfs_iunlink(struct xfs_trans *, struct xfs_ianalde *);
STATIC int xfs_iunlink_remove(struct xfs_trans *tp, struct xfs_perag *pag,
	struct xfs_ianalde *);

/*
 * helper function to extract extent size hint from ianalde
 */
xfs_extlen_t
xfs_get_extsz_hint(
	struct xfs_ianalde	*ip)
{
	/*
	 * Anal point in aligning allocations if we need to COW to actually
	 * write to them.
	 */
	if (xfs_is_always_cow_ianalde(ip))
		return 0;
	if ((ip->i_diflags & XFS_DIFLAG_EXTSIZE) && ip->i_extsize)
		return ip->i_extsize;
	if (XFS_IS_REALTIME_IANALDE(ip))
		return ip->i_mount->m_sb.sb_rextsize;
	return 0;
}

/*
 * Helper function to extract CoW extent size hint from ianalde.
 * Between the extent size hint and the CoW extent size hint, we
 * return the greater of the two.  If the value is zero (automatic),
 * use the default size.
 */
xfs_extlen_t
xfs_get_cowextsz_hint(
	struct xfs_ianalde	*ip)
{
	xfs_extlen_t		a, b;

	a = 0;
	if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
		a = ip->i_cowextsize;
	b = xfs_get_extsz_hint(ip);

	a = max(a, b);
	if (a == 0)
		return XFS_DEFAULT_COWEXTSZ_HINT;
	return a;
}

/*
 * These two are wrapper routines around the xfs_ilock() routine used to
 * centralize some grungy code.  They are used in places that wish to lock the
 * ianalde solely for reading the extents.  The reason these places can't just
 * call xfs_ilock(ip, XFS_ILOCK_SHARED) is that the ianalde lock also guards to
 * bringing in of the extents from disk for a file in b-tree format.  If the
 * ianalde is in b-tree format, then we need to lock the ianalde exclusively until
 * the extents are read in.  Locking it exclusively all the time would limit
 * our parallelism unnecessarily, though.  What we do instead is check to see
 * if the extents have been read in yet, and only lock the ianalde exclusively
 * if they have analt.
 *
 * The functions return a value which should be given to the corresponding
 * xfs_iunlock() call.
 */
uint
xfs_ilock_data_map_shared(
	struct xfs_ianalde	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	if (xfs_need_iread_extents(&ip->i_df))
		lock_mode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

uint
xfs_ilock_attr_map_shared(
	struct xfs_ianalde	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	if (xfs_ianalde_has_attr_fork(ip) && xfs_need_iread_extents(&ip->i_af))
		lock_mode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

/*
 * You can't set both SHARED and EXCL for the same lock,
 * and only XFS_IOLOCK_SHARED, XFS_IOLOCK_EXCL, XFS_MMAPLOCK_SHARED,
 * XFS_MMAPLOCK_EXCL, XFS_ILOCK_SHARED, XFS_ILOCK_EXCL are valid values
 * to set in lock_flags.
 */
static inline void
xfs_lock_flags_assert(
	uint		lock_flags)
{
	ASSERT((lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) !=
		(XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL));
	ASSERT((lock_flags & (XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL)) !=
		(XFS_MMAPLOCK_SHARED | XFS_MMAPLOCK_EXCL));
	ASSERT((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) !=
		(XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_LOCK_SUBCLASS_MASK)) == 0);
	ASSERT(lock_flags != 0);
}

/*
 * In addition to i_rwsem in the VFS ianalde, the xfs ianalde contains 2
 * multi-reader locks: invalidate_lock and the i_lock.  This routine allows
 * various combinations of the locks to be obtained.
 *
 * The 3 locks should always be ordered so that the IO lock is obtained first,
 * the mmap lock second and the ilock last in order to prevent deadlock.
 *
 * Basic locking order:
 *
 * i_rwsem -> invalidate_lock -> page_lock -> i_ilock
 *
 * mmap_lock locking order:
 *
 * i_rwsem -> page lock -> mmap_lock
 * mmap_lock -> invalidate_lock -> page_lock
 *
 * The difference in mmap_lock locking order mean that we cananalt hold the
 * invalidate_lock over syscall based read(2)/write(2) based IO. These IO paths
 * can fault in pages during copy in/out (for buffered IO) or require the
 * mmap_lock in get_user_pages() to map the user pages into the kernel address
 * space for direct IO. Similarly the i_rwsem cananalt be taken inside a page
 * fault because page faults already hold the mmap_lock.
 *
 * Hence to serialise fully against both syscall and mmap based IO, we need to
 * take both the i_rwsem and the invalidate_lock. These locks should *only* be
 * both taken in places where we need to invalidate the page cache in a race
 * free manner (e.g. truncate, hole punch and other extent manipulation
 * functions).
 */
void
xfs_ilock(
	xfs_ianalde_t		*ip,
	uint			lock_flags)
{
	trace_xfs_ilock(ip, lock_flags, _RET_IP_);

	xfs_lock_flags_assert(lock_flags);

	if (lock_flags & XFS_IOLOCK_EXCL) {
		down_write_nested(&VFS_I(ip)->i_rwsem,
				  XFS_IOLOCK_DEP(lock_flags));
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		down_read_nested(&VFS_I(ip)->i_rwsem,
				 XFS_IOLOCK_DEP(lock_flags));
	}

	if (lock_flags & XFS_MMAPLOCK_EXCL) {
		down_write_nested(&VFS_I(ip)->i_mapping->invalidate_lock,
				  XFS_MMAPLOCK_DEP(lock_flags));
	} else if (lock_flags & XFS_MMAPLOCK_SHARED) {
		down_read_nested(&VFS_I(ip)->i_mapping->invalidate_lock,
				 XFS_MMAPLOCK_DEP(lock_flags));
	}

	if (lock_flags & XFS_ILOCK_EXCL)
		mrupdate_nested(&ip->i_lock, XFS_ILOCK_DEP(lock_flags));
	else if (lock_flags & XFS_ILOCK_SHARED)
		mraccess_nested(&ip->i_lock, XFS_ILOCK_DEP(lock_flags));
}

/*
 * This is just like xfs_ilock(), except that the caller
 * is guaranteed analt to sleep.  It returns 1 if it gets
 * the requested locks and 0 otherwise.  If the IO lock is
 * obtained but the ianalde lock cananalt be, then the IO lock
 * is dropped before returning.
 *
 * ip -- the ianalde being locked
 * lock_flags -- this parameter indicates the ianalde's locks to be
 *       to be locked.  See the comment for xfs_ilock() for a list
 *	 of valid values.
 */
int
xfs_ilock_analwait(
	xfs_ianalde_t		*ip,
	uint			lock_flags)
{
	trace_xfs_ilock_analwait(ip, lock_flags, _RET_IP_);

	xfs_lock_flags_assert(lock_flags);

	if (lock_flags & XFS_IOLOCK_EXCL) {
		if (!down_write_trylock(&VFS_I(ip)->i_rwsem))
			goto out;
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		if (!down_read_trylock(&VFS_I(ip)->i_rwsem))
			goto out;
	}

	if (lock_flags & XFS_MMAPLOCK_EXCL) {
		if (!down_write_trylock(&VFS_I(ip)->i_mapping->invalidate_lock))
			goto out_undo_iolock;
	} else if (lock_flags & XFS_MMAPLOCK_SHARED) {
		if (!down_read_trylock(&VFS_I(ip)->i_mapping->invalidate_lock))
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
		up_write(&VFS_I(ip)->i_mapping->invalidate_lock);
	else if (lock_flags & XFS_MMAPLOCK_SHARED)
		up_read(&VFS_I(ip)->i_mapping->invalidate_lock);
out_undo_iolock:
	if (lock_flags & XFS_IOLOCK_EXCL)
		up_write(&VFS_I(ip)->i_rwsem);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		up_read(&VFS_I(ip)->i_rwsem);
out:
	return 0;
}

/*
 * xfs_iunlock() is used to drop the ianalde locks acquired with
 * xfs_ilock() and xfs_ilock_analwait().  The caller must pass
 * in the flags given to xfs_ilock() or xfs_ilock_analwait() so
 * that we kanalw which locks to drop.
 *
 * ip -- the ianalde being unlocked
 * lock_flags -- this parameter indicates the ianalde's locks to be
 *       to be unlocked.  See the comment for xfs_ilock() for a list
 *	 of valid values for this parameter.
 *
 */
void
xfs_iunlock(
	xfs_ianalde_t		*ip,
	uint			lock_flags)
{
	xfs_lock_flags_assert(lock_flags);

	if (lock_flags & XFS_IOLOCK_EXCL)
		up_write(&VFS_I(ip)->i_rwsem);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		up_read(&VFS_I(ip)->i_rwsem);

	if (lock_flags & XFS_MMAPLOCK_EXCL)
		up_write(&VFS_I(ip)->i_mapping->invalidate_lock);
	else if (lock_flags & XFS_MMAPLOCK_SHARED)
		up_read(&VFS_I(ip)->i_mapping->invalidate_lock);

	if (lock_flags & XFS_ILOCK_EXCL)
		mrunlock_excl(&ip->i_lock);
	else if (lock_flags & XFS_ILOCK_SHARED)
		mrunlock_shared(&ip->i_lock);

	trace_xfs_iunlock(ip, lock_flags, _RET_IP_);
}

/*
 * give up write locks.  the i/o lock cananalt be held nested
 * if it is being demoted.
 */
void
xfs_ilock_demote(
	xfs_ianalde_t		*ip,
	uint			lock_flags)
{
	ASSERT(lock_flags & (XFS_IOLOCK_EXCL|XFS_MMAPLOCK_EXCL|XFS_ILOCK_EXCL));
	ASSERT((lock_flags &
		~(XFS_IOLOCK_EXCL|XFS_MMAPLOCK_EXCL|XFS_ILOCK_EXCL)) == 0);

	if (lock_flags & XFS_ILOCK_EXCL)
		mrdemote(&ip->i_lock);
	if (lock_flags & XFS_MMAPLOCK_EXCL)
		downgrade_write(&VFS_I(ip)->i_mapping->invalidate_lock);
	if (lock_flags & XFS_IOLOCK_EXCL)
		downgrade_write(&VFS_I(ip)->i_rwsem);

	trace_xfs_ilock_demote(ip, lock_flags, _RET_IP_);
}

#if defined(DEBUG) || defined(XFS_WARN)
static inline bool
__xfs_rwsem_islocked(
	struct rw_semaphore	*rwsem,
	bool			shared)
{
	if (!debug_locks)
		return rwsem_is_locked(rwsem);

	if (!shared)
		return lockdep_is_held_type(rwsem, 0);

	/*
	 * We are checking that the lock is held at least in shared
	 * mode but don't care that it might be held exclusively
	 * (i.e. shared | excl). Hence we check if the lock is held
	 * in any mode rather than an explicit shared mode.
	 */
	return lockdep_is_held_type(rwsem, -1);
}

bool
xfs_isilocked(
	struct xfs_ianalde	*ip,
	uint			lock_flags)
{
	if (lock_flags & (XFS_ILOCK_EXCL|XFS_ILOCK_SHARED)) {
		if (!(lock_flags & XFS_ILOCK_SHARED))
			return !!ip->i_lock.mr_writer;
		return rwsem_is_locked(&ip->i_lock.mr_lock);
	}

	if (lock_flags & (XFS_MMAPLOCK_EXCL|XFS_MMAPLOCK_SHARED)) {
		return __xfs_rwsem_islocked(&VFS_I(ip)->i_mapping->invalidate_lock,
				(lock_flags & XFS_MMAPLOCK_SHARED));
	}

	if (lock_flags & (XFS_IOLOCK_EXCL | XFS_IOLOCK_SHARED)) {
		return __xfs_rwsem_islocked(&VFS_I(ip)->i_rwsem,
				(lock_flags & XFS_IOLOCK_SHARED));
	}

	ASSERT(0);
	return false;
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
 * Bump the subclass so xfs_lock_ianaldes() acquires each lock with a different
 * value. This can be called for any type of ianalde lock combination, including
 * parent locking. Care must be taken to ensure we don't overrun the subclass
 * storage fields in the class mask we build.
 */
static inline uint
xfs_lock_inumorder(
	uint	lock_mode,
	uint	subclass)
{
	uint	class = 0;

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
 * The following routine will lock n ianaldes in exclusive mode.  We assume the
 * caller calls us with the ianaldes in i_ianal order.
 *
 * We need to detect deadlock where an ianalde that we lock is in the AIL and we
 * start waiting for aanalther ianalde that is locked by a thread in a long running
 * transaction (such as truncate). This can result in deadlock since the long
 * running trans might need to wait for the ianalde we just locked in order to
 * push the tail and free space in the log.
 *
 * xfs_lock_ianaldes() can only be used to lock one type of lock at a time -
 * the iolock, the mmaplock or the ilock, but analt more than one at a time. If we
 * lock more than one at a time, lockdep will report false positives saying we
 * have violated locking orders.
 */
static void
xfs_lock_ianaldes(
	struct xfs_ianalde	**ips,
	int			ianaldes,
	uint			lock_mode)
{
	int			attempts = 0;
	uint			i;
	int			j;
	bool			try_lock;
	struct xfs_log_item	*lp;

	/*
	 * Currently supports between 2 and 5 ianaldes with exclusive locking.  We
	 * support an arbitrary depth of locking here, but absolute limits on
	 * ianaldes depend on the type of locking and the limits placed by
	 * lockdep ananaltations in xfs_lock_inumorder.  These are all checked by
	 * the asserts.
	 */
	ASSERT(ips && ianaldes >= 2 && ianaldes <= 5);
	ASSERT(lock_mode & (XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL |
			    XFS_ILOCK_EXCL));
	ASSERT(!(lock_mode & (XFS_IOLOCK_SHARED | XFS_MMAPLOCK_SHARED |
			      XFS_ILOCK_SHARED)));
	ASSERT(!(lock_mode & XFS_MMAPLOCK_EXCL) ||
		ianaldes <= XFS_MMAPLOCK_MAX_SUBCLASS + 1);
	ASSERT(!(lock_mode & XFS_ILOCK_EXCL) ||
		ianaldes <= XFS_ILOCK_MAX_SUBCLASS + 1);

	if (lock_mode & XFS_IOLOCK_EXCL) {
		ASSERT(!(lock_mode & (XFS_MMAPLOCK_EXCL | XFS_ILOCK_EXCL)));
	} else if (lock_mode & XFS_MMAPLOCK_EXCL)
		ASSERT(!(lock_mode & XFS_ILOCK_EXCL));

again:
	try_lock = false;
	i = 0;
	for (; i < ianaldes; i++) {
		ASSERT(ips[i]);

		if (i && (ips[i] == ips[i - 1]))	/* Already locked */
			continue;

		/*
		 * If try_lock is analt set yet, make sure all locked ianaldes are
		 * analt in the AIL.  If any are, set try_lock to be used later.
		 */
		if (!try_lock) {
			for (j = (i - 1); j >= 0 && !try_lock; j--) {
				lp = &ips[j]->i_itemp->ili_item;
				if (lp && test_bit(XFS_LI_IN_AIL, &lp->li_flags))
					try_lock = true;
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

		/* try_lock means we have an ianalde locked that is in the AIL. */
		ASSERT(i != 0);
		if (xfs_ilock_analwait(ips[i], xfs_lock_inumorder(lock_mode, i)))
			continue;

		/*
		 * Unlock all previous guys and try again.  xfs_iunlock will try
		 * to push the tail if the ianalde is in the AIL.
		 */
		attempts++;
		for (j = i - 1; j >= 0; j--) {
			/*
			 * Check to see if we've already unlocked this one.  Analt
			 * the first one going back, and the ianalde ptr is the
			 * same.
			 */
			if (j != (i - 1) && ips[j] == ips[j + 1])
				continue;

			xfs_iunlock(ips[j], lock_mode);
		}

		if ((attempts % 5) == 0) {
			delay(1); /* Don't just spin the CPU */
		}
		goto again;
	}
}

/*
 * xfs_lock_two_ianaldes() can only be used to lock ilock. The iolock and
 * mmaplock must be double-locked separately since we use i_rwsem and
 * invalidate_lock for that. We analw support taking one lock EXCL and the
 * other SHARED.
 */
void
xfs_lock_two_ianaldes(
	struct xfs_ianalde	*ip0,
	uint			ip0_mode,
	struct xfs_ianalde	*ip1,
	uint			ip1_mode)
{
	int			attempts = 0;
	struct xfs_log_item	*lp;

	ASSERT(hweight32(ip0_mode) == 1);
	ASSERT(hweight32(ip1_mode) == 1);
	ASSERT(!(ip0_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL)));
	ASSERT(!(ip1_mode & (XFS_IOLOCK_SHARED|XFS_IOLOCK_EXCL)));
	ASSERT(!(ip0_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)));
	ASSERT(!(ip1_mode & (XFS_MMAPLOCK_SHARED|XFS_MMAPLOCK_EXCL)));
	ASSERT(ip0->i_ianal != ip1->i_ianal);

	if (ip0->i_ianal > ip1->i_ianal) {
		swap(ip0, ip1);
		swap(ip0_mode, ip1_mode);
	}

 again:
	xfs_ilock(ip0, xfs_lock_inumorder(ip0_mode, 0));

	/*
	 * If the first lock we have locked is in the AIL, we must TRY to get
	 * the second lock. If we can't get it, we must release the first one
	 * and try again.
	 */
	lp = &ip0->i_itemp->ili_item;
	if (lp && test_bit(XFS_LI_IN_AIL, &lp->li_flags)) {
		if (!xfs_ilock_analwait(ip1, xfs_lock_inumorder(ip1_mode, 1))) {
			xfs_iunlock(ip0, ip0_mode);
			if ((++attempts % 5) == 0)
				delay(1); /* Don't just spin the CPU */
			goto again;
		}
	} else {
		xfs_ilock(ip1, xfs_lock_inumorder(ip1_mode, 1));
	}
}

uint
xfs_ip2xflags(
	struct xfs_ianalde	*ip)
{
	uint			flags = 0;

	if (ip->i_diflags & XFS_DIFLAG_ANY) {
		if (ip->i_diflags & XFS_DIFLAG_REALTIME)
			flags |= FS_XFLAG_REALTIME;
		if (ip->i_diflags & XFS_DIFLAG_PREALLOC)
			flags |= FS_XFLAG_PREALLOC;
		if (ip->i_diflags & XFS_DIFLAG_IMMUTABLE)
			flags |= FS_XFLAG_IMMUTABLE;
		if (ip->i_diflags & XFS_DIFLAG_APPEND)
			flags |= FS_XFLAG_APPEND;
		if (ip->i_diflags & XFS_DIFLAG_SYNC)
			flags |= FS_XFLAG_SYNC;
		if (ip->i_diflags & XFS_DIFLAG_ANALATIME)
			flags |= FS_XFLAG_ANALATIME;
		if (ip->i_diflags & XFS_DIFLAG_ANALDUMP)
			flags |= FS_XFLAG_ANALDUMP;
		if (ip->i_diflags & XFS_DIFLAG_RTINHERIT)
			flags |= FS_XFLAG_RTINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_PROJINHERIT)
			flags |= FS_XFLAG_PROJINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_ANALSYMLINKS)
			flags |= FS_XFLAG_ANALSYMLINKS;
		if (ip->i_diflags & XFS_DIFLAG_EXTSIZE)
			flags |= FS_XFLAG_EXTSIZE;
		if (ip->i_diflags & XFS_DIFLAG_EXTSZINHERIT)
			flags |= FS_XFLAG_EXTSZINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_ANALDEFRAG)
			flags |= FS_XFLAG_ANALDEFRAG;
		if (ip->i_diflags & XFS_DIFLAG_FILESTREAM)
			flags |= FS_XFLAG_FILESTREAM;
	}

	if (ip->i_diflags2 & XFS_DIFLAG2_ANY) {
		if (ip->i_diflags2 & XFS_DIFLAG2_DAX)
			flags |= FS_XFLAG_DAX;
		if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
			flags |= FS_XFLAG_COWEXTSIZE;
	}

	if (xfs_ianalde_has_attr_fork(ip))
		flags |= FS_XFLAG_HASATTR;
	return flags;
}

/*
 * Lookups up an ianalde from "name". If ci_name is analt NULL, then a CI match
 * is allowed, otherwise it has to be an exact match. If a CI match is found,
 * ci_name->name will point to a the actual name (caller must free) or
 * will be set to NULL if an exact match is found.
 */
int
xfs_lookup(
	struct xfs_ianalde	*dp,
	const struct xfs_name	*name,
	struct xfs_ianalde	**ipp,
	struct xfs_name		*ci_name)
{
	xfs_ianal_t		inum;
	int			error;

	trace_xfs_lookup(dp, name);

	if (xfs_is_shutdown(dp->i_mount))
		return -EIO;
	if (xfs_ifork_zapped(dp, XFS_DATA_FORK))
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

/* Propagate di_flags from a parent ianalde to a child ianalde. */
static void
xfs_ianalde_inherit_flags(
	struct xfs_ianalde	*ip,
	const struct xfs_ianalde	*pip)
{
	unsigned int		di_flags = 0;
	xfs_failaddr_t		failaddr;
	umode_t			mode = VFS_I(ip)->i_mode;

	if (S_ISDIR(mode)) {
		if (pip->i_diflags & XFS_DIFLAG_RTINHERIT)
			di_flags |= XFS_DIFLAG_RTINHERIT;
		if (pip->i_diflags & XFS_DIFLAG_EXTSZINHERIT) {
			di_flags |= XFS_DIFLAG_EXTSZINHERIT;
			ip->i_extsize = pip->i_extsize;
		}
		if (pip->i_diflags & XFS_DIFLAG_PROJINHERIT)
			di_flags |= XFS_DIFLAG_PROJINHERIT;
	} else if (S_ISREG(mode)) {
		if ((pip->i_diflags & XFS_DIFLAG_RTINHERIT) &&
		    xfs_has_realtime(ip->i_mount))
			di_flags |= XFS_DIFLAG_REALTIME;
		if (pip->i_diflags & XFS_DIFLAG_EXTSZINHERIT) {
			di_flags |= XFS_DIFLAG_EXTSIZE;
			ip->i_extsize = pip->i_extsize;
		}
	}
	if ((pip->i_diflags & XFS_DIFLAG_ANALATIME) &&
	    xfs_inherit_analatime)
		di_flags |= XFS_DIFLAG_ANALATIME;
	if ((pip->i_diflags & XFS_DIFLAG_ANALDUMP) &&
	    xfs_inherit_analdump)
		di_flags |= XFS_DIFLAG_ANALDUMP;
	if ((pip->i_diflags & XFS_DIFLAG_SYNC) &&
	    xfs_inherit_sync)
		di_flags |= XFS_DIFLAG_SYNC;
	if ((pip->i_diflags & XFS_DIFLAG_ANALSYMLINKS) &&
	    xfs_inherit_analsymlinks)
		di_flags |= XFS_DIFLAG_ANALSYMLINKS;
	if ((pip->i_diflags & XFS_DIFLAG_ANALDEFRAG) &&
	    xfs_inherit_analdefrag)
		di_flags |= XFS_DIFLAG_ANALDEFRAG;
	if (pip->i_diflags & XFS_DIFLAG_FILESTREAM)
		di_flags |= XFS_DIFLAG_FILESTREAM;

	ip->i_diflags |= di_flags;

	/*
	 * Ianalde verifiers on older kernels only check that the extent size
	 * hint is an integer multiple of the rt extent size on realtime files.
	 * They did analt check the hint alignment on a directory with both
	 * rtinherit and extszinherit flags set.  If the misaligned hint is
	 * propagated from a directory into a new realtime file, new file
	 * allocations will fail due to math errors in the rt allocator and/or
	 * trip the verifiers.  Validate the hint settings in the new file so
	 * that we don't let broken hints propagate.
	 */
	failaddr = xfs_ianalde_validate_extsize(ip->i_mount, ip->i_extsize,
			VFS_I(ip)->i_mode, ip->i_diflags);
	if (failaddr) {
		ip->i_diflags &= ~(XFS_DIFLAG_EXTSIZE |
				   XFS_DIFLAG_EXTSZINHERIT);
		ip->i_extsize = 0;
	}
}

/* Propagate di_flags2 from a parent ianalde to a child ianalde. */
static void
xfs_ianalde_inherit_flags2(
	struct xfs_ianalde	*ip,
	const struct xfs_ianalde	*pip)
{
	xfs_failaddr_t		failaddr;

	if (pip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE) {
		ip->i_diflags2 |= XFS_DIFLAG2_COWEXTSIZE;
		ip->i_cowextsize = pip->i_cowextsize;
	}
	if (pip->i_diflags2 & XFS_DIFLAG2_DAX)
		ip->i_diflags2 |= XFS_DIFLAG2_DAX;

	/* Don't let invalid cowextsize hints propagate. */
	failaddr = xfs_ianalde_validate_cowextsize(ip->i_mount, ip->i_cowextsize,
			VFS_I(ip)->i_mode, ip->i_diflags, ip->i_diflags2);
	if (failaddr) {
		ip->i_diflags2 &= ~XFS_DIFLAG2_COWEXTSIZE;
		ip->i_cowextsize = 0;
	}
}

/*
 * Initialise a newly allocated ianalde and return the in-core ianalde to the
 * caller locked exclusively.
 */
int
xfs_init_new_ianalde(
	struct mnt_idmap	*idmap,
	struct xfs_trans	*tp,
	struct xfs_ianalde	*pip,
	xfs_ianal_t		ianal,
	umode_t			mode,
	xfs_nlink_t		nlink,
	dev_t			rdev,
	prid_t			prid,
	bool			init_xattrs,
	struct xfs_ianalde	**ipp)
{
	struct ianalde		*dir = pip ? VFS_I(pip) : NULL;
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_ianalde	*ip;
	unsigned int		flags;
	int			error;
	struct timespec64	tv;
	struct ianalde		*ianalde;

	/*
	 * Protect against obviously corrupt allocation btree records. Later
	 * xfs_iget checks will catch re-allocation of other active in-memory
	 * and on-disk ianaldes. If we don't catch reallocating the parent ianalde
	 * here we will deadlock in xfs_iget() so we have to do these checks
	 * first.
	 */
	if ((pip && ianal == pip->i_ianal) || !xfs_verify_dir_ianal(mp, ianal)) {
		xfs_alert(mp, "Allocated a kanalwn in-use ianalde 0x%llx!", ianal);
		return -EFSCORRUPTED;
	}

	/*
	 * Get the in-core ianalde with the lock held exclusively to prevent
	 * others from looking at until we're done.
	 */
	error = xfs_iget(mp, tp, ianal, XFS_IGET_CREATE, XFS_ILOCK_EXCL, &ip);
	if (error)
		return error;

	ASSERT(ip != NULL);
	ianalde = VFS_I(ip);
	set_nlink(ianalde, nlink);
	ianalde->i_rdev = rdev;
	ip->i_projid = prid;

	if (dir && !(dir->i_mode & S_ISGID) && xfs_has_grpid(mp)) {
		ianalde_fsuid_set(ianalde, idmap);
		ianalde->i_gid = dir->i_gid;
		ianalde->i_mode = mode;
	} else {
		ianalde_init_owner(idmap, ianalde, dir, mode);
	}

	/*
	 * If the group ID of the new file does analt match the effective group
	 * ID or one of the supplementary group IDs, the S_ISGID bit is cleared
	 * (and only if the irix_sgid_inherit compatibility variable is set).
	 */
	if (irix_sgid_inherit && (ianalde->i_mode & S_ISGID) &&
	    !vfsgid_in_group_p(i_gid_into_vfsgid(idmap, ianalde)))
		ianalde->i_mode &= ~S_ISGID;

	ip->i_disk_size = 0;
	ip->i_df.if_nextents = 0;
	ASSERT(ip->i_nblocks == 0);

	tv = ianalde_set_ctime_current(ianalde);
	ianalde_set_mtime_to_ts(ianalde, tv);
	ianalde_set_atime_to_ts(ianalde, tv);

	ip->i_extsize = 0;
	ip->i_diflags = 0;

	if (xfs_has_v3ianaldes(mp)) {
		ianalde_set_iversion(ianalde, 1);
		ip->i_cowextsize = 0;
		ip->i_crtime = tv;
	}

	flags = XFS_ILOG_CORE;
	switch (mode & S_IFMT) {
	case S_IFIFO:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFSOCK:
		ip->i_df.if_format = XFS_DIANALDE_FMT_DEV;
		flags |= XFS_ILOG_DEV;
		break;
	case S_IFREG:
	case S_IFDIR:
		if (pip && (pip->i_diflags & XFS_DIFLAG_ANY))
			xfs_ianalde_inherit_flags(ip, pip);
		if (pip && (pip->i_diflags2 & XFS_DIFLAG2_ANY))
			xfs_ianalde_inherit_flags2(ip, pip);
		fallthrough;
	case S_IFLNK:
		ip->i_df.if_format = XFS_DIANALDE_FMT_EXTENTS;
		ip->i_df.if_bytes = 0;
		ip->i_df.if_data = NULL;
		break;
	default:
		ASSERT(0);
	}

	/*
	 * If we need to create attributes immediately after allocating the
	 * ianalde, initialise an empty attribute fork right analw. We use the
	 * default fork offset for attributes here as we don't kanalw exactly what
	 * size or how many attributes we might be adding. We can do this
	 * safely here because we kanalw the data fork is completely empty and
	 * this saves us from needing to run a separate transaction to set the
	 * fork offset in the immediate future.
	 */
	if (init_xattrs && xfs_has_attr(mp)) {
		ip->i_forkoff = xfs_default_attroffset(ip) >> 3;
		xfs_ifork_init_attr(ip, XFS_DIANALDE_FMT_EXTENTS, 0);
	}

	/*
	 * Log the new values stuffed into the ianalde.
	 */
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_ianalde(tp, ip, flags);

	/* analw that we have an i_mode we can setup the ianalde structure */
	xfs_setup_ianalde(ip);

	*ipp = ip;
	return 0;
}

/*
 * Decrement the link count on an ianalde & log the change.  If this causes the
 * link count to go to zero, move the ianalde to AGI unlinked list so that it can
 * be freed when the last active reference goes away via xfs_inactive().
 */
static int			/* error */
xfs_droplink(
	xfs_trans_t *tp,
	xfs_ianalde_t *ip)
{
	if (VFS_I(ip)->i_nlink == 0) {
		xfs_alert(ip->i_mount,
			  "%s: Attempt to drop ianalde (%llu) with nlink zero.",
			  __func__, ip->i_ianal);
		return -EFSCORRUPTED;
	}

	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	drop_nlink(VFS_I(ip));
	xfs_trans_log_ianalde(tp, ip, XFS_ILOG_CORE);

	if (VFS_I(ip)->i_nlink)
		return 0;

	return xfs_iunlink(tp, ip);
}

/*
 * Increment the link count on an ianalde & log the change.
 */
static void
xfs_bumplink(
	xfs_trans_t *tp,
	xfs_ianalde_t *ip)
{
	xfs_trans_ichgtime(tp, ip, XFS_ICHGTIME_CHG);

	inc_nlink(VFS_I(ip));
	xfs_trans_log_ianalde(tp, ip, XFS_ILOG_CORE);
}

int
xfs_create(
	struct mnt_idmap	*idmap,
	xfs_ianalde_t		*dp,
	struct xfs_name		*name,
	umode_t			mode,
	dev_t			rdev,
	bool			init_xattrs,
	xfs_ianalde_t		**ipp)
{
	int			is_dir = S_ISDIR(mode);
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_ianalde	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	int			error;
	bool                    unlock_dp_on_error = false;
	prid_t			prid;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_trans_res	*tres;
	uint			resblks;
	xfs_ianal_t		ianal;

	trace_xfs_create(dp, name);

	if (xfs_is_shutdown(mp))
		return -EIO;
	if (xfs_ifork_zapped(dp, XFS_DATA_FORK))
		return -EIO;

	prid = xfs_get_initial_prid(dp);

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, mapped_fsuid(idmap, &init_user_ns),
			mapped_fsgid(idmap, &init_user_ns), prid,
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
	 * Initially assume that the file does analt exist and
	 * reserve the resources for that case.  If that is analt
	 * the case we'll drop the one we have and get a more
	 * appropriate transaction later.
	 */
	error = xfs_trans_alloc_icreate(mp, tres, udqp, gdqp, pdqp, resblks,
			&tp);
	if (error == -EANALSPC) {
		/* flush outstanding delalloc blocks and retry */
		xfs_flush_ianaldes(mp);
		error = xfs_trans_alloc_icreate(mp, tres, udqp, gdqp, pdqp,
				resblks, &tp);
	}
	if (error)
		goto out_release_dquots;

	xfs_ilock(dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	unlock_dp_on_error = true;

	/*
	 * A newly created regular or special file just has one directory
	 * entry pointing to them, but a directory also the "." entry
	 * pointing to itself.
	 */
	error = xfs_dialloc(&tp, dp->i_ianal, mode, &ianal);
	if (!error)
		error = xfs_init_new_ianalde(idmap, tp, dp, ianal, mode,
				is_dir ? 2 : 1, rdev, prid, init_xattrs, &ip);
	if (error)
		goto out_trans_cancel;

	/*
	 * Analw we join the directory ianalde to the transaction.  We do analt do it
	 * earlier because xfs_dialloc might commit the previous transaction
	 * (and release all the locks).  An error from here on will result in
	 * the transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	xfs_trans_ijoin(tp, dp, XFS_ILOCK_EXCL);
	unlock_dp_on_error = false;

	error = xfs_dir_createname(tp, dp, name, ip->i_ianal,
					resblks - XFS_IALLOC_SPACE_RES(mp));
	if (error) {
		ASSERT(error != -EANALSPC);
		goto out_trans_cancel;
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_ianalde(tp, dp, XFS_ILOG_CORE);

	if (is_dir) {
		error = xfs_dir_init(tp, ip, dp);
		if (error)
			goto out_trans_cancel;

		xfs_bumplink(tp, dp);
	}

	/*
	 * If this is a synchroanalus mount, make sure that the
	 * create transaction goes to disk before returning to
	 * the user.
	 */
	if (xfs_has_wsync(mp) || xfs_has_dirsync(mp))
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the ianaldes and modify them incore.
	 * These ids of the ianalde couldn't have changed since the new
	 * ianalde has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp, pdqp);

	error = xfs_trans_commit(tp);
	if (error)
		goto out_release_ianalde;

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	*ipp = ip;
	return 0;

 out_trans_cancel:
	xfs_trans_cancel(tp);
 out_release_ianalde:
	/*
	 * Wait until after the current transaction is aborted to finish the
	 * setup of the ianalde and release the ianalde.  This prevents recursive
	 * transactions and deadlocks from xfs_inactive.
	 */
	if (ip) {
		xfs_finish_ianalde_setup(ip);
		xfs_irele(ip);
	}
 out_release_dquots:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	if (unlock_dp_on_error)
		xfs_iunlock(dp, XFS_ILOCK_EXCL);
	return error;
}

int
xfs_create_tmpfile(
	struct mnt_idmap	*idmap,
	struct xfs_ianalde	*dp,
	umode_t			mode,
	struct xfs_ianalde	**ipp)
{
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_ianalde	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	int			error;
	prid_t                  prid;
	struct xfs_dquot	*udqp = NULL;
	struct xfs_dquot	*gdqp = NULL;
	struct xfs_dquot	*pdqp = NULL;
	struct xfs_trans_res	*tres;
	uint			resblks;
	xfs_ianal_t		ianal;

	if (xfs_is_shutdown(mp))
		return -EIO;

	prid = xfs_get_initial_prid(dp);

	/*
	 * Make sure that we have allocated dquot(s) on disk.
	 */
	error = xfs_qm_vop_dqalloc(dp, mapped_fsuid(idmap, &init_user_ns),
			mapped_fsgid(idmap, &init_user_ns), prid,
			XFS_QMOPT_QUOTALL | XFS_QMOPT_INHERIT,
			&udqp, &gdqp, &pdqp);
	if (error)
		return error;

	resblks = XFS_IALLOC_SPACE_RES(mp);
	tres = &M_RES(mp)->tr_create_tmpfile;

	error = xfs_trans_alloc_icreate(mp, tres, udqp, gdqp, pdqp, resblks,
			&tp);
	if (error)
		goto out_release_dquots;

	error = xfs_dialloc(&tp, dp->i_ianal, mode, &ianal);
	if (!error)
		error = xfs_init_new_ianalde(idmap, tp, dp, ianal, mode,
				0, 0, prid, false, &ip);
	if (error)
		goto out_trans_cancel;

	if (xfs_has_wsync(mp))
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the ianaldes and modify them incore.
	 * These ids of the ianalde couldn't have changed since the new
	 * ianalde has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, ip, udqp, gdqp, pdqp);

	error = xfs_iunlink(tp, ip);
	if (error)
		goto out_trans_cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto out_release_ianalde;

	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	*ipp = ip;
	return 0;

 out_trans_cancel:
	xfs_trans_cancel(tp);
 out_release_ianalde:
	/*
	 * Wait until after the current transaction is aborted to finish the
	 * setup of the ianalde and release the ianalde.  This prevents recursive
	 * transactions and deadlocks from xfs_inactive.
	 */
	if (ip) {
		xfs_finish_ianalde_setup(ip);
		xfs_irele(ip);
	}
 out_release_dquots:
	xfs_qm_dqrele(udqp);
	xfs_qm_dqrele(gdqp);
	xfs_qm_dqrele(pdqp);

	return error;
}

int
xfs_link(
	xfs_ianalde_t		*tdp,
	xfs_ianalde_t		*sip,
	struct xfs_name		*target_name)
{
	xfs_mount_t		*mp = tdp->i_mount;
	xfs_trans_t		*tp;
	int			error, analspace_error = 0;
	int			resblks;

	trace_xfs_link(tdp, target_name);

	ASSERT(!S_ISDIR(VFS_I(sip)->i_mode));

	if (xfs_is_shutdown(mp))
		return -EIO;
	if (xfs_ifork_zapped(tdp, XFS_DATA_FORK))
		return -EIO;

	error = xfs_qm_dqattach(sip);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(tdp);
	if (error)
		goto std_return;

	resblks = XFS_LINK_SPACE_RES(mp, target_name->len);
	error = xfs_trans_alloc_dir(tdp, &M_RES(mp)->tr_link, sip, &resblks,
			&tp, &analspace_error);
	if (error)
		goto std_return;

	/*
	 * If we are using project inheritance, we only allow hard link
	 * creation in our tree when the project IDs are the same; else
	 * the tree quota mechanism could be circumvented.
	 */
	if (unlikely((tdp->i_diflags & XFS_DIFLAG_PROJINHERIT) &&
		     tdp->i_projid != sip->i_projid)) {
		error = -EXDEV;
		goto error_return;
	}

	if (!resblks) {
		error = xfs_dir_canenter(tp, tdp, target_name);
		if (error)
			goto error_return;
	}

	/*
	 * Handle initial link state of O_TMPFILE ianalde
	 */
	if (VFS_I(sip)->i_nlink == 0) {
		struct xfs_perag	*pag;

		pag = xfs_perag_get(mp, XFS_IANAL_TO_AGANAL(mp, sip->i_ianal));
		error = xfs_iunlink_remove(tp, pag, sip);
		xfs_perag_put(pag);
		if (error)
			goto error_return;
	}

	error = xfs_dir_createname(tp, tdp, target_name, sip->i_ianal,
				   resblks);
	if (error)
		goto error_return;
	xfs_trans_ichgtime(tp, tdp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_ianalde(tp, tdp, XFS_ILOG_CORE);

	xfs_bumplink(tp, sip);

	/*
	 * If this is a synchroanalus mount, make sure that the
	 * link transaction goes to disk before returning to
	 * the user.
	 */
	if (xfs_has_wsync(mp) || xfs_has_dirsync(mp))
		xfs_trans_set_sync(tp);

	return xfs_trans_commit(tp);

 error_return:
	xfs_trans_cancel(tp);
 std_return:
	if (error == -EANALSPC && analspace_error)
		error = analspace_error;
	return error;
}

/* Clear the reflink flag and the cowblocks tag if possible. */
static void
xfs_itruncate_clear_reflink_flags(
	struct xfs_ianalde	*ip)
{
	struct xfs_ifork	*dfork;
	struct xfs_ifork	*cfork;

	if (!xfs_is_reflink_ianalde(ip))
		return;
	dfork = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	cfork = xfs_ifork_ptr(ip, XFS_COW_FORK);
	if (dfork->if_bytes == 0 && cfork->if_bytes == 0)
		ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;
	if (cfork->if_bytes == 0)
		xfs_ianalde_clear_cowblocks_tag(ip);
}

/*
 * Free up the underlying blocks past new_size.  The new size must be smaller
 * than the current size.  This routine can be used both for the attribute and
 * data fork, and does analt modify the ianalde size, which is left to the caller.
 *
 * The transaction passed to this routine must have made a permanent log
 * reservation of at least XFS_ITRUNCATE_LOG_RES.  This routine may commit the
 * given transaction and start new ones, so make sure everything involved in
 * the transaction is tidy before calling here.  Some transaction will be
 * returned to the caller to be committed.  The incoming transaction must
 * already include the ianalde, and both ianalde locks must be held exclusively.
 * The ianalde must also be "held" within the transaction.  On return the ianalde
 * will be "held" within the returned transaction.  This routine does ANALT
 * require any disk space to be reserved for it within the transaction.
 *
 * If we get an error, we must return with the ianalde locked and linked into the
 * current transaction. This keeps things simple for the higher level code,
 * because it always kanalws that the ianalde is locked and held in the transaction
 * that returns to it whether errors occur or analt.  We don't mark the ianalde
 * dirty on error so that transactions can be easily aborted if possible.
 */
int
xfs_itruncate_extents_flags(
	struct xfs_trans	**tpp,
	struct xfs_ianalde	*ip,
	int			whichfork,
	xfs_fsize_t		new_size,
	int			flags)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp = *tpp;
	xfs_fileoff_t		first_unmap_block;
	int			error = 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(!atomic_read(&VFS_I(ip)->i_count) ||
	       xfs_isilocked(ip, XFS_IOLOCK_EXCL));
	ASSERT(new_size <= XFS_ISIZE(ip));
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ip->i_itemp->ili_lock_flags == 0);
	ASSERT(!XFS_ANALT_DQATTACHED(mp, ip));

	trace_xfs_itruncate_extents_start(ip, new_size);

	flags |= xfs_bmapi_aflag(whichfork);

	/*
	 * Since it is possible for space to become allocated beyond
	 * the end of the file (in a crash where the space is allocated
	 * but the ianalde size is analt yet updated), simply remove any
	 * blocks which show up between the new EOF and the maximum
	 * possible file size.
	 *
	 * We have to free all the blocks to the bmbt maximum offset, even if
	 * the page cache can't scale that far.
	 */
	first_unmap_block = XFS_B_TO_FSB(mp, (xfs_ufsize_t)new_size);
	if (!xfs_verify_fileoff(mp, first_unmap_block)) {
		WARN_ON_ONCE(first_unmap_block > XFS_MAX_FILEOFF);
		return 0;
	}

	error = xfs_bunmapi_range(&tp, ip, flags, first_unmap_block,
			XFS_MAX_FILEOFF);
	if (error)
		goto out;

	if (whichfork == XFS_DATA_FORK) {
		/* Remove all pending CoW reservations. */
		error = xfs_reflink_cancel_cow_blocks(ip, &tp,
				first_unmap_block, XFS_MAX_FILEOFF, true);
		if (error)
			goto out;

		xfs_itruncate_clear_reflink_flags(ip);
	}

	/*
	 * Always re-log the ianalde so that our permanent transaction can keep
	 * on rolling it forward in the log.
	 */
	xfs_trans_log_ianalde(tp, ip, XFS_ILOG_CORE);

	trace_xfs_itruncate_extents_end(ip, new_size);

out:
	*tpp = tp;
	return error;
}

int
xfs_release(
	xfs_ianalde_t	*ip)
{
	xfs_mount_t	*mp = ip->i_mount;
	int		error = 0;

	if (!S_ISREG(VFS_I(ip)->i_mode) || (VFS_I(ip)->i_mode == 0))
		return 0;

	/* If this is a read-only mount, don't do this (would generate I/O) */
	if (xfs_is_readonly(mp))
		return 0;

	if (!xfs_is_shutdown(mp)) {
		int truncated;

		/*
		 * If we previously truncated this file and removed old data
		 * in the process, we want to initiate "early" writeout on
		 * the last close.  This is an attempt to combat the analtorious
		 * NULL files problem which is particularly analticeable from a
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

	/*
	 * If we can't get the iolock just skip truncating the blocks past EOF
	 * because we could deadlock with the mmap_lock otherwise. We'll get
	 * aanalther chance to drop them once the last reference to the ianalde is
	 * dropped, so we'll never leak blocks permanently.
	 */
	if (!xfs_ilock_analwait(ip, XFS_IOLOCK_EXCL))
		return 0;

	if (xfs_can_free_eofblocks(ip, false)) {
		/*
		 * Check if the ianalde is being opened, written and closed
		 * frequently and we have delayed allocation blocks outstanding
		 * (e.g. streaming writes from the NFS server), truncating the
		 * blocks past EOF will cause fragmentation to occur.
		 *
		 * In this case don't do the truncation, but we have to be
		 * careful how we detect this case. Blocks beyond EOF show up as
		 * i_delayed_blks even when the ianalde is clean, so we need to
		 * truncate them away first before checking for a dirty release.
		 * Hence on the first dirty close we will still remove the
		 * speculative allocation, but after that we will leave it in
		 * place.
		 */
		if (xfs_iflags_test(ip, XFS_IDIRTY_RELEASE))
			goto out_unlock;

		error = xfs_free_eofblocks(ip);
		if (error)
			goto out_unlock;

		/* delalloc blocks after truncation means it really is dirty */
		if (ip->i_delayed_blks)
			xfs_iflags_set(ip, XFS_IDIRTY_RELEASE);
	}

out_unlock:
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	return error;
}

/*
 * xfs_inactive_truncate
 *
 * Called to perform a truncate when an ianalde becomes unlinked.
 */
STATIC int
xfs_inactive_truncate(
	struct xfs_ianalde *ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_itruncate, 0, 0, 0, &tp);
	if (error) {
		ASSERT(xfs_is_shutdown(mp));
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * Log the ianalde size first to prevent stale data exposure in the event
	 * of a system crash before the truncate completes. See the related
	 * comment in xfs_vn_setattr_size() for details.
	 */
	ip->i_disk_size = 0;
	xfs_trans_log_ianalde(tp, ip, XFS_ILOG_CORE);

	error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK, 0);
	if (error)
		goto error_trans_cancel;

	ASSERT(ip->i_df.if_nextents == 0);

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
 * Perform the ianalde free when an ianalde is unlinked.
 */
STATIC int
xfs_inactive_ifree(
	struct xfs_ianalde *ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	/*
	 * We try to use a per-AG reservation for any block needed by the fianalbt
	 * tree, but as the fianalbt feature predates the per-AG reservation
	 * support a degraded file system might analt have eanalugh space for the
	 * reservation at mount time.  In that case try to dip into the reserved
	 * pool and pray.
	 *
	 * Send a warning if the reservation does happen to fail, as the ianalde
	 * analw remains allocated and sits on the unlinked list until the fs is
	 * repaired.
	 */
	if (unlikely(mp->m_fianalbt_analres)) {
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ifree,
				XFS_IFREE_SPACE_RES(mp), 0, XFS_TRANS_RESERVE,
				&tp);
	} else {
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ifree, 0, 0, 0, &tp);
	}
	if (error) {
		if (error == -EANALSPC) {
			xfs_warn_ratelimited(mp,
			"Failed to remove ianalde(s) from unlinked list. "
			"Please free space, unmount and run xfs_repair.");
		} else {
			ASSERT(xfs_is_shutdown(mp));
		}
		return error;
	}

	/*
	 * We do analt hold the ianalde locked across the entire rolling transaction
	 * here. We only need to hold it for the first transaction that
	 * xfs_ifree() builds, which may mark the ianalde XFS_ISTALE if the
	 * underlying cluster buffer is freed. Relogging an XFS_ISTALE ianalde
	 * here breaks the relationship between cluster buffer invalidation and
	 * stale ianalde invalidation on cluster buffer item journal commit
	 * completion, and can result in leaving dirty stale ianaldes hanging
	 * around in memory.
	 *
	 * We have anal need for serialising this ianalde operation against other
	 * operations - we freed the ianalde and hence reallocation is required
	 * and that will serialise on reallocating the space the deferops need
	 * to free. Hence we can unlock the ianalde on the first commit of
	 * the transaction rather than roll it right through the deferops. This
	 * avoids relogging the XFS_ISTALE ianalde.
	 *
	 * We check that xfs_ifree() hasn't grown an internal transaction roll
	 * by asserting that the ianalde is still locked when it returns.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	error = xfs_ifree(tp, ip);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	if (error) {
		/*
		 * If we fail to free the ianalde, shut down.  The cancel
		 * might do that, we need to make sure.  Otherwise the
		 * ianalde might be lost for a long time or forever.
		 */
		if (!xfs_is_shutdown(mp)) {
			xfs_analtice(mp, "%s: xfs_ifree returned error %d",
				__func__, error);
			xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		}
		xfs_trans_cancel(tp);
		return error;
	}

	/*
	 * Credit the quota account(s). The ianalde is gone.
	 */
	xfs_trans_mod_dquot_byianal(tp, ip, XFS_TRANS_DQ_ICOUNT, -1);

	return xfs_trans_commit(tp);
}

/*
 * Returns true if we need to update the on-disk metadata before we can free
 * the memory used by this ianalde.  Updates include freeing post-eof
 * preallocations; freeing COW staging extents; and marking the ianalde free in
 * the ianalbt if it is on the unlinked list.
 */
bool
xfs_ianalde_needs_inactive(
	struct xfs_ianalde	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*cow_ifp = xfs_ifork_ptr(ip, XFS_COW_FORK);

	/*
	 * If the ianalde is already free, then there can be analthing
	 * to clean up here.
	 */
	if (VFS_I(ip)->i_mode == 0)
		return false;

	/*
	 * If this is a read-only mount, don't do this (would generate I/O)
	 * unless we're in log recovery and cleaning the iunlinked list.
	 */
	if (xfs_is_readonly(mp) && !xlog_recovery_needed(mp->m_log))
		return false;

	/* If the log isn't running, push ianaldes straight to reclaim. */
	if (xfs_is_shutdown(mp) || xfs_has_analrecovery(mp))
		return false;

	/* Metadata ianaldes require explicit resource cleanup. */
	if (xfs_is_metadata_ianalde(ip))
		return false;

	/* Want to clean out the cow blocks if there are any. */
	if (cow_ifp && cow_ifp->if_bytes > 0)
		return true;

	/* Unlinked files must be freed. */
	if (VFS_I(ip)->i_nlink == 0)
		return true;

	/*
	 * This file isn't being freed, so check if there are post-eof blocks
	 * to free.  @force is true because we are evicting an ianalde from the
	 * cache.  Post-eof blocks must be freed, lest we end up with broken
	 * free space accounting.
	 *
	 * Analte: don't bother with iolock here since lockdep complains about
	 * acquiring it in reclaim context. We have the only reference to the
	 * ianalde at this point anyways.
	 */
	return xfs_can_free_eofblocks(ip, true);
}

/*
 * xfs_inactive
 *
 * This is called when the vanalde reference count for the vanalde
 * goes to zero.  If the file has been unlinked, then it must
 * analw be truncated.  Also, we clear all of the read-ahead state
 * kept for the ianalde here since the file is analw closed.
 */
int
xfs_inactive(
	xfs_ianalde_t	*ip)
{
	struct xfs_mount	*mp;
	int			error = 0;
	int			truncate = 0;

	/*
	 * If the ianalde is already free, then there can be analthing
	 * to clean up here.
	 */
	if (VFS_I(ip)->i_mode == 0) {
		ASSERT(ip->i_df.if_broot_bytes == 0);
		goto out;
	}

	mp = ip->i_mount;
	ASSERT(!xfs_iflags_test(ip, XFS_IRECOVERY));

	/*
	 * If this is a read-only mount, don't do this (would generate I/O)
	 * unless we're in log recovery and cleaning the iunlinked list.
	 */
	if (xfs_is_readonly(mp) && !xlog_recovery_needed(mp->m_log))
		goto out;

	/* Metadata ianaldes require explicit resource cleanup. */
	if (xfs_is_metadata_ianalde(ip))
		goto out;

	/* Try to clean out the cow blocks if there are any. */
	if (xfs_ianalde_has_cow_data(ip))
		xfs_reflink_cancel_cow_range(ip, 0, NULLFILEOFF, true);

	if (VFS_I(ip)->i_nlink != 0) {
		/*
		 * force is true because we are evicting an ianalde from the
		 * cache. Post-eof blocks must be freed, lest we end up with
		 * broken free space accounting.
		 *
		 * Analte: don't bother with iolock here since lockdep complains
		 * about acquiring it in reclaim context. We have the only
		 * reference to the ianalde at this point anyways.
		 */
		if (xfs_can_free_eofblocks(ip, true))
			error = xfs_free_eofblocks(ip);

		goto out;
	}

	if (S_ISREG(VFS_I(ip)->i_mode) &&
	    (ip->i_disk_size != 0 || XFS_ISIZE(ip) != 0 ||
	     ip->i_df.if_nextents > 0 || ip->i_delayed_blks > 0))
		truncate = 1;

	if (xfs_iflags_test(ip, XFS_IQUOTAUNCHECKED)) {
		/*
		 * If this ianalde is being inactivated during a quotacheck and
		 * has analt yet been scanned by quotacheck, we /must/ remove
		 * the dquots from the ianalde before inactivation changes the
		 * block and ianalde counts.  Most probably this is a result of
		 * reloading the incore iunlinked list to purge unrecovered
		 * unlinked ianaldes.
		 */
		xfs_qm_dqdetach(ip);
	} else {
		error = xfs_qm_dqattach(ip);
		if (error)
			goto out;
	}

	if (S_ISLNK(VFS_I(ip)->i_mode))
		error = xfs_inactive_symlink(ip);
	else if (truncate)
		error = xfs_inactive_truncate(ip);
	if (error)
		goto out;

	/*
	 * If there are attributes associated with the file then blow them away
	 * analw.  The code calls a routine that recursively deconstructs the
	 * attribute fork. If also blows away the in-core attribute fork.
	 */
	if (xfs_ianalde_has_attr_fork(ip)) {
		error = xfs_attr_inactive(ip);
		if (error)
			goto out;
	}

	ASSERT(ip->i_forkoff == 0);

	/*
	 * Free the ianalde.
	 */
	error = xfs_inactive_ifree(ip);

out:
	/*
	 * We're done making metadata updates for this ianalde, so we can release
	 * the attached dquots.
	 */
	xfs_qm_dqdetach(ip);
	return error;
}

/*
 * In-Core Unlinked List Lookups
 * =============================
 *
 * Every ianalde is supposed to be reachable from some other piece of metadata
 * with the exception of the root directory.  Ianaldes with a connection to a
 * file descriptor but analt linked from anywhere in the on-disk directory tree
 * are collectively kanalwn as unlinked ianaldes, though the filesystem itself
 * maintains links to these ianaldes so that on-disk metadata are consistent.
 *
 * XFS implements a per-AG on-disk hash table of unlinked ianaldes.  The AGI
 * header contains a number of buckets that point to an ianalde, and each ianalde
 * record has a pointer to the next ianalde in the hash chain.  This
 * singly-linked list causes scaling problems in the iunlink remove function
 * because we must walk that list to find the ianalde that points to the ianalde
 * being removed from the unlinked hash bucket list.
 *
 * Hence we keep an in-memory double linked list to link each ianalde on an
 * unlinked list. Because there are 64 unlinked lists per AGI, keeping pointer
 * based lists would require having 64 list heads in the perag, one for each
 * list. This is expensive in terms of memory (think millions of AGs) and cache
 * misses on lookups. Instead, use the fact that ianaldes on the unlinked list
 * must be referenced at the VFS level to keep them on the list and hence we
 * have an existence guarantee for ianaldes on the unlinked list.
 *
 * Given we have an existence guarantee, we can use lockless ianalde cache lookups
 * to resolve agianals to xfs ianaldes. This means we only need 8 bytes per ianalde
 * for the double linked unlinked list, and we don't need any extra locking to
 * keep the list safe as all manipulations are done under the AGI buffer lock.
 * Keeping the list up to date does analt require memory allocation, just finding
 * the XFS ianalde and updating the next/prev unlinked list agianals.
 */

/*
 * Find an ianalde on the unlinked list. This does analt take references to the
 * ianalde as we have existence guarantees by holding the AGI buffer lock and that
 * only unlinked, referenced ianaldes can be on the unlinked ianalde list.  If we
 * don't find the ianalde in cache, then let the caller handle the situation.
 */
static struct xfs_ianalde *
xfs_iunlink_lookup(
	struct xfs_perag	*pag,
	xfs_agianal_t		agianal)
{
	struct xfs_ianalde	*ip;

	rcu_read_lock();
	ip = radix_tree_lookup(&pag->pag_ici_root, agianal);
	if (!ip) {
		/* Caller can handle ianalde analt being in memory. */
		rcu_read_unlock();
		return NULL;
	}

	/*
	 * Ianalde in RCU freeing limbo should analt happen.  Warn about this and
	 * let the caller handle the failure.
	 */
	if (WARN_ON_ONCE(!ip->i_ianal)) {
		rcu_read_unlock();
		return NULL;
	}
	ASSERT(!xfs_iflags_test(ip, XFS_IRECLAIMABLE | XFS_IRECLAIM));
	rcu_read_unlock();
	return ip;
}

/*
 * Update the prev pointer of the next agianal.  Returns -EANALLINK if the ianalde
 * is analt in cache.
 */
static int
xfs_iunlink_update_backref(
	struct xfs_perag	*pag,
	xfs_agianal_t		prev_agianal,
	xfs_agianal_t		next_agianal)
{
	struct xfs_ianalde	*ip;

	/* Anal update necessary if we are at the end of the list. */
	if (next_agianal == NULLAGIANAL)
		return 0;

	ip = xfs_iunlink_lookup(pag, next_agianal);
	if (!ip)
		return -EANALLINK;

	ip->i_prev_unlinked = prev_agianal;
	return 0;
}

/*
 * Point the AGI unlinked bucket at an ianalde and log the results.  The caller
 * is responsible for validating the old value.
 */
STATIC int
xfs_iunlink_update_bucket(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		*agibp,
	unsigned int		bucket_index,
	xfs_agianal_t		new_agianal)
{
	struct xfs_agi		*agi = agibp->b_addr;
	xfs_agianal_t		old_value;
	int			offset;

	ASSERT(xfs_verify_agianal_or_null(pag, new_agianal));

	old_value = be32_to_cpu(agi->agi_unlinked[bucket_index]);
	trace_xfs_iunlink_update_bucket(tp->t_mountp, pag->pag_aganal, bucket_index,
			old_value, new_agianal);

	/*
	 * We should never find the head of the list already set to the value
	 * passed in because either we're adding or removing ourselves from the
	 * head of the list.
	 */
	if (old_value == new_agianal) {
		xfs_buf_mark_corrupt(agibp);
		return -EFSCORRUPTED;
	}

	agi->agi_unlinked[bucket_index] = cpu_to_be32(new_agianal);
	offset = offsetof(struct xfs_agi, agi_unlinked) +
			(sizeof(xfs_agianal_t) * bucket_index);
	xfs_trans_log_buf(tp, agibp, offset, offset + sizeof(xfs_agianal_t) - 1);
	return 0;
}

/*
 * Load the ianalde @next_agianal into the cache and set its prev_unlinked pointer
 * to @prev_agianal.  Caller must hold the AGI to synchronize with other changes
 * to the unlinked list.
 */
STATIC int
xfs_iunlink_reload_next(
	struct xfs_trans	*tp,
	struct xfs_buf		*agibp,
	xfs_agianal_t		prev_agianal,
	xfs_agianal_t		next_agianal)
{
	struct xfs_perag	*pag = agibp->b_pag;
	struct xfs_mount	*mp = pag->pag_mount;
	struct xfs_ianalde	*next_ip = NULL;
	xfs_ianal_t		ianal;
	int			error;

	ASSERT(next_agianal != NULLAGIANAL);

#ifdef DEBUG
	rcu_read_lock();
	next_ip = radix_tree_lookup(&pag->pag_ici_root, next_agianal);
	ASSERT(next_ip == NULL);
	rcu_read_unlock();
#endif

	xfs_info_ratelimited(mp,
 "Found unrecovered unlinked ianalde 0x%x in AG 0x%x.  Initiating recovery.",
			next_agianal, pag->pag_aganal);

	/*
	 * Use an untrusted lookup just to be cautious in case the AGI has been
	 * corrupted and analw points at a free ianalde.  That shouldn't happen,
	 * but we'd rather shut down analw since we're already running in a weird
	 * situation.
	 */
	ianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, next_agianal);
	error = xfs_iget(mp, tp, ianal, XFS_IGET_UNTRUSTED, 0, &next_ip);
	if (error)
		return error;

	/* If this is analt an unlinked ianalde, something is very wrong. */
	if (VFS_I(next_ip)->i_nlink != 0) {
		error = -EFSCORRUPTED;
		goto rele;
	}

	next_ip->i_prev_unlinked = prev_agianal;
	trace_xfs_iunlink_reload_next(next_ip);
rele:
	ASSERT(!(VFS_I(next_ip)->i_state & I_DONTCACHE));
	if (xfs_is_quotacheck_running(mp) && next_ip)
		xfs_iflags_set(next_ip, XFS_IQUOTAUNCHECKED);
	xfs_irele(next_ip);
	return error;
}

static int
xfs_iunlink_insert_ianalde(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		*agibp,
	struct xfs_ianalde	*ip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agi		*agi = agibp->b_addr;
	xfs_agianal_t		next_agianal;
	xfs_agianal_t		agianal = XFS_IANAL_TO_AGIANAL(mp, ip->i_ianal);
	short			bucket_index = agianal % XFS_AGI_UNLINKED_BUCKETS;
	int			error;

	/*
	 * Get the index into the agi hash table for the list this ianalde will
	 * go on.  Make sure the pointer isn't garbage and that this ianalde
	 * isn't already on the list.
	 */
	next_agianal = be32_to_cpu(agi->agi_unlinked[bucket_index]);
	if (next_agianal == agianal ||
	    !xfs_verify_agianal_or_null(pag, next_agianal)) {
		xfs_buf_mark_corrupt(agibp);
		return -EFSCORRUPTED;
	}

	/*
	 * Update the prev pointer in the next ianalde to point back to this
	 * ianalde.
	 */
	error = xfs_iunlink_update_backref(pag, agianal, next_agianal);
	if (error == -EANALLINK)
		error = xfs_iunlink_reload_next(tp, agibp, agianal, next_agianal);
	if (error)
		return error;

	if (next_agianal != NULLAGIANAL) {
		/*
		 * There is already aanalther ianalde in the bucket, so point this
		 * ianalde to the current head of the list.
		 */
		error = xfs_iunlink_log_ianalde(tp, ip, pag, next_agianal);
		if (error)
			return error;
		ip->i_next_unlinked = next_agianal;
	}

	/* Point the head of the list to point to this ianalde. */
	ip->i_prev_unlinked = NULLAGIANAL;
	return xfs_iunlink_update_bucket(tp, pag, agibp, bucket_index, agianal);
}

/*
 * This is called when the ianalde's link count has gone to 0 or we are creating
 * a tmpfile via O_TMPFILE.  The ianalde @ip must have nlink == 0.
 *
 * We place the on-disk ianalde on a list in the AGI.  It will be pulled from this
 * list when the ianalde is freed.
 */
STATIC int
xfs_iunlink(
	struct xfs_trans	*tp,
	struct xfs_ianalde	*ip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_perag	*pag;
	struct xfs_buf		*agibp;
	int			error;

	ASSERT(VFS_I(ip)->i_nlink == 0);
	ASSERT(VFS_I(ip)->i_mode != 0);
	trace_xfs_iunlink(ip);

	pag = xfs_perag_get(mp, XFS_IANAL_TO_AGANAL(mp, ip->i_ianal));

	/* Get the agi buffer first.  It ensures lock ordering on the list. */
	error = xfs_read_agi(pag, tp, &agibp);
	if (error)
		goto out;

	error = xfs_iunlink_insert_ianalde(tp, pag, agibp, ip);
out:
	xfs_perag_put(pag);
	return error;
}

static int
xfs_iunlink_remove_ianalde(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_buf		*agibp,
	struct xfs_ianalde	*ip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agi		*agi = agibp->b_addr;
	xfs_agianal_t		agianal = XFS_IANAL_TO_AGIANAL(mp, ip->i_ianal);
	xfs_agianal_t		head_agianal;
	short			bucket_index = agianal % XFS_AGI_UNLINKED_BUCKETS;
	int			error;

	trace_xfs_iunlink_remove(ip);

	/*
	 * Get the index into the agi hash table for the list this ianalde will
	 * go on.  Make sure the head pointer isn't garbage.
	 */
	head_agianal = be32_to_cpu(agi->agi_unlinked[bucket_index]);
	if (!xfs_verify_agianal(pag, head_agianal)) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
				agi, sizeof(*agi));
		return -EFSCORRUPTED;
	}

	/*
	 * Set our ianalde's next_unlinked pointer to NULL and then return
	 * the old pointer value so that we can update whatever was previous
	 * to us in the list to point to whatever was next in the list.
	 */
	error = xfs_iunlink_log_ianalde(tp, ip, pag, NULLAGIANAL);
	if (error)
		return error;

	/*
	 * Update the prev pointer in the next ianalde to point back to previous
	 * ianalde in the chain.
	 */
	error = xfs_iunlink_update_backref(pag, ip->i_prev_unlinked,
			ip->i_next_unlinked);
	if (error == -EANALLINK)
		error = xfs_iunlink_reload_next(tp, agibp, ip->i_prev_unlinked,
				ip->i_next_unlinked);
	if (error)
		return error;

	if (head_agianal != agianal) {
		struct xfs_ianalde	*prev_ip;

		prev_ip = xfs_iunlink_lookup(pag, ip->i_prev_unlinked);
		if (!prev_ip)
			return -EFSCORRUPTED;

		error = xfs_iunlink_log_ianalde(tp, prev_ip, pag,
				ip->i_next_unlinked);
		prev_ip->i_next_unlinked = ip->i_next_unlinked;
	} else {
		/* Point the head of the list to the next unlinked ianalde. */
		error = xfs_iunlink_update_bucket(tp, pag, agibp, bucket_index,
				ip->i_next_unlinked);
	}

	ip->i_next_unlinked = NULLAGIANAL;
	ip->i_prev_unlinked = 0;
	return error;
}

/*
 * Pull the on-disk ianalde from the AGI unlinked list.
 */
STATIC int
xfs_iunlink_remove(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_ianalde	*ip)
{
	struct xfs_buf		*agibp;
	int			error;

	trace_xfs_iunlink_remove(ip);

	/* Get the agi buffer first.  It ensures lock ordering on the list. */
	error = xfs_read_agi(pag, tp, &agibp);
	if (error)
		return error;

	return xfs_iunlink_remove_ianalde(tp, pag, agibp, ip);
}

/*
 * Look up the ianalde number specified and if it is analt already marked XFS_ISTALE
 * mark it stale. We should only find clean ianaldes in this lookup that aren't
 * already stale.
 */
static void
xfs_ifree_mark_ianalde_stale(
	struct xfs_perag	*pag,
	struct xfs_ianalde	*free_ip,
	xfs_ianal_t		inum)
{
	struct xfs_mount	*mp = pag->pag_mount;
	struct xfs_ianalde_log_item *iip;
	struct xfs_ianalde	*ip;

retry:
	rcu_read_lock();
	ip = radix_tree_lookup(&pag->pag_ici_root, XFS_IANAL_TO_AGIANAL(mp, inum));

	/* Ianalde analt in memory, analthing to do */
	if (!ip) {
		rcu_read_unlock();
		return;
	}

	/*
	 * because this is an RCU protected lookup, we could find a recently
	 * freed or even reallocated ianalde during the lookup. We need to check
	 * under the i_flags_lock for a valid ianalde here. Skip it if it is analt
	 * valid, the wrong ianalde or stale.
	 */
	spin_lock(&ip->i_flags_lock);
	if (ip->i_ianal != inum || __xfs_iflags_test(ip, XFS_ISTALE))
		goto out_iflags_unlock;

	/*
	 * Don't try to lock/unlock the current ianalde, but we _cananalt_ skip the
	 * other ianaldes that we did analt find in the list attached to the buffer
	 * and are analt already marked stale. If we can't lock it, back off and
	 * retry.
	 */
	if (ip != free_ip) {
		if (!xfs_ilock_analwait(ip, XFS_ILOCK_EXCL)) {
			spin_unlock(&ip->i_flags_lock);
			rcu_read_unlock();
			delay(1);
			goto retry;
		}
	}
	ip->i_flags |= XFS_ISTALE;

	/*
	 * If the ianalde is flushing, it is already attached to the buffer.  All
	 * we needed to do here is mark the ianalde stale so buffer IO completion
	 * will remove it from the AIL.
	 */
	iip = ip->i_itemp;
	if (__xfs_iflags_test(ip, XFS_IFLUSHING)) {
		ASSERT(!list_empty(&iip->ili_item.li_bio_list));
		ASSERT(iip->ili_last_fields);
		goto out_iunlock;
	}

	/*
	 * Ianaldes analt attached to the buffer can be released immediately.
	 * Everything else has to go through xfs_iflush_abort() on journal
	 * commit as the flock synchronises removal of the ianalde from the
	 * cluster buffer against ianalde reclaim.
	 */
	if (!iip || list_empty(&iip->ili_item.li_bio_list))
		goto out_iunlock;

	__xfs_iflags_set(ip, XFS_IFLUSHING);
	spin_unlock(&ip->i_flags_lock);
	rcu_read_unlock();

	/* we have a dirty ianalde in memory that has analt yet been flushed. */
	spin_lock(&iip->ili_lock);
	iip->ili_last_fields = iip->ili_fields;
	iip->ili_fields = 0;
	iip->ili_fsync_fields = 0;
	spin_unlock(&iip->ili_lock);
	ASSERT(iip->ili_last_fields);

	if (ip != free_ip)
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return;

out_iunlock:
	if (ip != free_ip)
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
out_iflags_unlock:
	spin_unlock(&ip->i_flags_lock);
	rcu_read_unlock();
}

/*
 * A big issue when freeing the ianalde cluster is that we _cananalt_ skip any
 * ianaldes that are in memory - they all must be marked stale and attached to
 * the cluster buffer.
 */
static int
xfs_ifree_cluster(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_ianalde	*free_ip,
	struct xfs_icluster	*xic)
{
	struct xfs_mount	*mp = free_ip->i_mount;
	struct xfs_ianal_geometry	*igeo = M_IGEO(mp);
	struct xfs_buf		*bp;
	xfs_daddr_t		blkanal;
	xfs_ianal_t		inum = xic->first_ianal;
	int			nbufs;
	int			i, j;
	int			ioffset;
	int			error;

	nbufs = igeo->ialloc_blks / igeo->blocks_per_cluster;

	for (j = 0; j < nbufs; j++, inum += igeo->ianaldes_per_cluster) {
		/*
		 * The allocation bitmap tells us which ianaldes of the chunk were
		 * physically allocated. Skip the cluster if an ianalde falls into
		 * a sparse region.
		 */
		ioffset = inum - xic->first_ianal;
		if ((xic->alloc & XFS_IANALBT_MASK(ioffset)) == 0) {
			ASSERT(ioffset % igeo->ianaldes_per_cluster == 0);
			continue;
		}

		blkanal = XFS_AGB_TO_DADDR(mp, XFS_IANAL_TO_AGANAL(mp, inum),
					 XFS_IANAL_TO_AGBANAL(mp, inum));

		/*
		 * We obtain and lock the backing buffer first in the process
		 * here to ensure dirty ianaldes attached to the buffer remain in
		 * the flushing state while we mark them stale.
		 *
		 * If we scan the in-memory ianaldes first, then buffer IO can
		 * complete before we get a lock on it, and hence we may fail
		 * to mark all the active ianaldes on the buffer stale.
		 */
		error = xfs_trans_get_buf(tp, mp->m_ddev_targp, blkanal,
				mp->m_bsize * igeo->blocks_per_cluster,
				XBF_UNMAPPED, &bp);
		if (error)
			return error;

		/*
		 * This buffer may analt have been correctly initialised as we
		 * didn't read it from disk. That's analt important because we are
		 * only using to mark the buffer as stale in the log, and to
		 * attach stale cached ianaldes on it. That means it will never be
		 * dispatched for IO. If it is, we want to kanalw about it, and we
		 * want it to fail. We can acheive this by adding a write
		 * verifier to the buffer.
		 */
		bp->b_ops = &xfs_ianalde_buf_ops;

		/*
		 * Analw we need to set all the cached clean ianaldes as XFS_ISTALE,
		 * too. This requires lookups, and will skip ianaldes that we've
		 * already marked XFS_ISTALE.
		 */
		for (i = 0; i < igeo->ianaldes_per_cluster; i++)
			xfs_ifree_mark_ianalde_stale(pag, free_ip, inum + i);

		xfs_trans_stale_ianalde_buf(tp, bp);
		xfs_trans_binval(tp, bp);
	}
	return 0;
}

/*
 * This is called to return an ianalde to the ianalde free list.  The ianalde should
 * already be truncated to 0 length and have anal pages associated with it.  This
 * routine also assumes that the ianalde is already a part of the transaction.
 *
 * The on-disk copy of the ianalde will have been added to the list of unlinked
 * ianaldes in the AGI. We need to remove the ianalde from that list atomically with
 * respect to freeing it here.
 */
int
xfs_ifree(
	struct xfs_trans	*tp,
	struct xfs_ianalde	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_perag	*pag;
	struct xfs_icluster	xic = { 0 };
	struct xfs_ianalde_log_item *iip = ip->i_itemp;
	int			error;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(VFS_I(ip)->i_nlink == 0);
	ASSERT(ip->i_df.if_nextents == 0);
	ASSERT(ip->i_disk_size == 0 || !S_ISREG(VFS_I(ip)->i_mode));
	ASSERT(ip->i_nblocks == 0);

	pag = xfs_perag_get(mp, XFS_IANAL_TO_AGANAL(mp, ip->i_ianal));

	/*
	 * Free the ianalde first so that we guarantee that the AGI lock is going
	 * to be taken before we remove the ianalde from the unlinked list. This
	 * makes the AGI lock -> unlinked list modification order the same as
	 * used in O_TMPFILE creation.
	 */
	error = xfs_difree(tp, pag, ip->i_ianal, &xic);
	if (error)
		goto out;

	error = xfs_iunlink_remove(tp, pag, ip);
	if (error)
		goto out;

	/*
	 * Free any local-format data sitting around before we reset the
	 * data fork to extents format.  Analte that the attr fork data has
	 * already been freed by xfs_attr_inactive.
	 */
	if (ip->i_df.if_format == XFS_DIANALDE_FMT_LOCAL) {
		kmem_free(ip->i_df.if_data);
		ip->i_df.if_data = NULL;
		ip->i_df.if_bytes = 0;
	}

	VFS_I(ip)->i_mode = 0;		/* mark incore ianalde as free */
	ip->i_diflags = 0;
	ip->i_diflags2 = mp->m_ianal_geo.new_diflags2;
	ip->i_forkoff = 0;		/* mark the attr fork analt in use */
	ip->i_df.if_format = XFS_DIANALDE_FMT_EXTENTS;
	if (xfs_iflags_test(ip, XFS_IPRESERVE_DM_FIELDS))
		xfs_iflags_clear(ip, XFS_IPRESERVE_DM_FIELDS);

	/* Don't attempt to replay owner changes for a deleted ianalde */
	spin_lock(&iip->ili_lock);
	iip->ili_fields &= ~(XFS_ILOG_AOWNER | XFS_ILOG_DOWNER);
	spin_unlock(&iip->ili_lock);

	/*
	 * Bump the generation count so anal one will be confused
	 * by reincarnations of this ianalde.
	 */
	VFS_I(ip)->i_generation++;
	xfs_trans_log_ianalde(tp, ip, XFS_ILOG_CORE);

	if (xic.deleted)
		error = xfs_ifree_cluster(tp, pag, ip, &xic);
out:
	xfs_perag_put(pag);
	return error;
}

/*
 * This is called to unpin an ianalde.  The caller must have the ianalde locked
 * in at least shared mode so that the buffer cananalt be subsequently pinned
 * once someone is waiting for it to be unpinned.
 */
static void
xfs_iunpin(
	struct xfs_ianalde	*ip)
{
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));

	trace_xfs_ianalde_unpin_analwait(ip, _RET_IP_);

	/* Give the log a push to start the unpinning I/O */
	xfs_log_force_seq(ip->i_mount, ip->i_itemp->ili_commit_seq, 0, NULL);

}

static void
__xfs_iunpin_wait(
	struct xfs_ianalde	*ip)
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
	struct xfs_ianalde	*ip)
{
	if (xfs_ipincount(ip))
		__xfs_iunpin_wait(ip);
}

/*
 * Removing an ianalde from the namespace involves removing the directory entry
 * and dropping the link count on the ianalde. Removing the directory entry can
 * result in locking an AGF (directory blocks were freed) and removing a link
 * count can result in placing the ianalde on an unlinked list which results in
 * locking an AGI.
 *
 * The big problem here is that we have an ordering constraint on AGF and AGI
 * locking - ianalde allocation locks the AGI, then can allocate a new extent for
 * new ianaldes, locking the AGF after the AGI. Similarly, freeing the ianalde
 * removes the ianalde from the unlinked list, requiring that we lock the AGI
 * first, and then freeing the ianalde can result in an ianalde chunk being freed
 * and hence freeing disk space requiring that we lock an AGF.
 *
 * Hence the ordering that is imposed by other parts of the code is AGI before
 * AGF. This means we cananalt remove the directory entry before we drop the ianalde
 * reference count and put it on the unlinked list as this results in a lock
 * order of AGF then AGI, and this can deadlock against ianalde allocation and
 * freeing. Therefore we must drop the link counts before we remove the
 * directory entry.
 *
 * This is still safe from a transactional point of view - it is analt until we
 * get to xfs_defer_finish() that we have the possibility of multiple
 * transactions in this operation. Hence as long as we remove the directory
 * entry and drop the link count in the first transaction of the remove
 * operation, there are anal transactional constraints on the ordering here.
 */
int
xfs_remove(
	xfs_ianalde_t             *dp,
	struct xfs_name		*name,
	xfs_ianalde_t		*ip)
{
	xfs_mount_t		*mp = dp->i_mount;
	xfs_trans_t             *tp = NULL;
	int			is_dir = S_ISDIR(VFS_I(ip)->i_mode);
	int			dontcare;
	int                     error = 0;
	uint			resblks;

	trace_xfs_remove(dp, name);

	if (xfs_is_shutdown(mp))
		return -EIO;
	if (xfs_ifork_zapped(dp, XFS_DATA_FORK))
		return -EIO;

	error = xfs_qm_dqattach(dp);
	if (error)
		goto std_return;

	error = xfs_qm_dqattach(ip);
	if (error)
		goto std_return;

	/*
	 * We try to get the real space reservation first, allowing for
	 * directory btree deletion(s) implying possible bmap insert(s).  If we
	 * can't get the space reservation then we use 0 instead, and avoid the
	 * bmap btree insert(s) in the directory code by, if the bmap insert
	 * tries to happen, instead trimming the LAST block from the directory.
	 *
	 * Iganalre EDQUOT and EANALSPC being returned via analspace_error because
	 * the directory code can handle a reservationless update and we don't
	 * want to prevent a user from trying to free space by deleting things.
	 */
	resblks = XFS_REMOVE_SPACE_RES(mp);
	error = xfs_trans_alloc_dir(dp, &M_RES(mp)->tr_remove, ip, &resblks,
			&tp, &dontcare);
	if (error) {
		ASSERT(error != -EANALSPC);
		goto std_return;
	}

	/*
	 * If we're removing a directory perform some additional validation.
	 */
	if (is_dir) {
		ASSERT(VFS_I(ip)->i_nlink >= 2);
		if (VFS_I(ip)->i_nlink != 2) {
			error = -EANALTEMPTY;
			goto out_trans_cancel;
		}
		if (!xfs_dir_isempty(ip)) {
			error = -EANALTEMPTY;
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

		/*
		 * Point the unlinked child directory's ".." entry to the root
		 * directory to eliminate back-references to ianaldes that may
		 * get freed before the child directory is closed.  If the fs
		 * gets shrunk, this can lead to dirent ianalde validation errors.
		 */
		if (dp->i_ianal != tp->t_mountp->m_sb.sb_rootianal) {
			error = xfs_dir_replace(tp, ip, &xfs_name_dotdot,
					tp->t_mountp->m_sb.sb_rootianal, 0);
			if (error)
				goto out_trans_cancel;
		}
	} else {
		/*
		 * When removing a analn-directory we need to log the parent
		 * ianalde here.  For a directory this is done implicitly
		 * by the xfs_droplink call for the ".." entry.
		 */
		xfs_trans_log_ianalde(tp, dp, XFS_ILOG_CORE);
	}
	xfs_trans_ichgtime(tp, dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

	/* Drop the link from dp to ip. */
	error = xfs_droplink(tp, ip);
	if (error)
		goto out_trans_cancel;

	error = xfs_dir_removename(tp, dp, name, ip->i_ianal, resblks);
	if (error) {
		ASSERT(error != -EANALENT);
		goto out_trans_cancel;
	}

	/*
	 * If this is a synchroanalus mount, make sure that the
	 * remove transaction goes to disk before returning to
	 * the user.
	 */
	if (xfs_has_wsync(mp) || xfs_has_dirsync(mp))
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp);
	if (error)
		goto std_return;

	if (is_dir && xfs_ianalde_is_filestream(ip))
		xfs_filestream_deassociate(ip);

	return 0;

 out_trans_cancel:
	xfs_trans_cancel(tp);
 std_return:
	return error;
}

/*
 * Enter all ianaldes for a rename transaction into a sorted array.
 */
#define __XFS_SORT_IANALDES	5
STATIC void
xfs_sort_for_rename(
	struct xfs_ianalde	*dp1,	/* in: old (source) directory ianalde */
	struct xfs_ianalde	*dp2,	/* in: new (target) directory ianalde */
	struct xfs_ianalde	*ip1,	/* in: ianalde of old entry */
	struct xfs_ianalde	*ip2,	/* in: ianalde of new entry */
	struct xfs_ianalde	*wip,	/* in: whiteout ianalde */
	struct xfs_ianalde	**i_tab,/* out: sorted array of ianaldes */
	int			*num_ianaldes)  /* in/out: ianaldes in array */
{
	int			i, j;

	ASSERT(*num_ianaldes == __XFS_SORT_IANALDES);
	memset(i_tab, 0, *num_ianaldes * sizeof(struct xfs_ianalde *));

	/*
	 * i_tab contains a list of pointers to ianaldes.  We initialize
	 * the table here & we'll sort it.  We will then use it to
	 * order the acquisition of the ianalde locks.
	 *
	 * Analte that the table may contain duplicates.  e.g., dp1 == dp2.
	 */
	i = 0;
	i_tab[i++] = dp1;
	i_tab[i++] = dp2;
	i_tab[i++] = ip1;
	if (ip2)
		i_tab[i++] = ip2;
	if (wip)
		i_tab[i++] = wip;
	*num_ianaldes = i;

	/*
	 * Sort the elements via bubble sort.  (Remember, there are at
	 * most 5 elements to sort, so this is adequate.)
	 */
	for (i = 0; i < *num_ianaldes; i++) {
		for (j = 1; j < *num_ianaldes; j++) {
			if (i_tab[j]->i_ianal < i_tab[j-1]->i_ianal) {
				struct xfs_ianalde *temp = i_tab[j];
				i_tab[j] = i_tab[j-1];
				i_tab[j-1] = temp;
			}
		}
	}
}

static int
xfs_finish_rename(
	struct xfs_trans	*tp)
{
	/*
	 * If this is a synchroanalus mount, make sure that the rename transaction
	 * goes to disk before returning to the user.
	 */
	if (xfs_has_wsync(tp->t_mountp) || xfs_has_dirsync(tp->t_mountp))
		xfs_trans_set_sync(tp);

	return xfs_trans_commit(tp);
}

/*
 * xfs_cross_rename()
 *
 * responsible for handling RENAME_EXCHANGE flag in renameat2() syscall
 */
STATIC int
xfs_cross_rename(
	struct xfs_trans	*tp,
	struct xfs_ianalde	*dp1,
	struct xfs_name		*name1,
	struct xfs_ianalde	*ip1,
	struct xfs_ianalde	*dp2,
	struct xfs_name		*name2,
	struct xfs_ianalde	*ip2,
	int			spaceres)
{
	int		error = 0;
	int		ip1_flags = 0;
	int		ip2_flags = 0;
	int		dp2_flags = 0;

	/* Swap ianalde number for dirent in first parent */
	error = xfs_dir_replace(tp, dp1, name1, ip2->i_ianal, spaceres);
	if (error)
		goto out_trans_abort;

	/* Swap ianalde number for dirent in second parent */
	error = xfs_dir_replace(tp, dp2, name2, ip1->i_ianal, spaceres);
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
						dp1->i_ianal, spaceres);
			if (error)
				goto out_trans_abort;

			/* transfer ip2 ".." reference to dp1 */
			if (!S_ISDIR(VFS_I(ip1)->i_mode)) {
				error = xfs_droplink(tp, dp2);
				if (error)
					goto out_trans_abort;
				xfs_bumplink(tp, dp1);
			}

			/*
			 * Although ip1 isn't changed here, userspace needs
			 * to be warned about the change, so that applications
			 * relying on it (like backup ones), will properly
			 * analtify the change
			 */
			ip1_flags |= XFS_ICHGTIME_CHG;
			ip2_flags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
		}

		if (S_ISDIR(VFS_I(ip1)->i_mode)) {
			error = xfs_dir_replace(tp, ip1, &xfs_name_dotdot,
						dp2->i_ianal, spaceres);
			if (error)
				goto out_trans_abort;

			/* transfer ip1 ".." reference to dp2 */
			if (!S_ISDIR(VFS_I(ip2)->i_mode)) {
				error = xfs_droplink(tp, dp1);
				if (error)
					goto out_trans_abort;
				xfs_bumplink(tp, dp2);
			}

			/*
			 * Although ip2 isn't changed here, userspace needs
			 * to be warned about the change, so that applications
			 * relying on it (like backup ones), will properly
			 * analtify the change
			 */
			ip1_flags |= XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG;
			ip2_flags |= XFS_ICHGTIME_CHG;
		}
	}

	if (ip1_flags) {
		xfs_trans_ichgtime(tp, ip1, ip1_flags);
		xfs_trans_log_ianalde(tp, ip1, XFS_ILOG_CORE);
	}
	if (ip2_flags) {
		xfs_trans_ichgtime(tp, ip2, ip2_flags);
		xfs_trans_log_ianalde(tp, ip2, XFS_ILOG_CORE);
	}
	if (dp2_flags) {
		xfs_trans_ichgtime(tp, dp2, dp2_flags);
		xfs_trans_log_ianalde(tp, dp2, XFS_ILOG_CORE);
	}
	xfs_trans_ichgtime(tp, dp1, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_ianalde(tp, dp1, XFS_ILOG_CORE);
	return xfs_finish_rename(tp);

out_trans_abort:
	xfs_trans_cancel(tp);
	return error;
}

/*
 * xfs_rename_alloc_whiteout()
 *
 * Return a referenced, unlinked, unlocked ianalde that can be used as a
 * whiteout in a rename transaction. We use a tmpfile ianalde here so that if we
 * crash between allocating the ianalde and linking it into the rename transaction
 * recovery will free the ianalde and we won't leak it.
 */
static int
xfs_rename_alloc_whiteout(
	struct mnt_idmap	*idmap,
	struct xfs_name		*src_name,
	struct xfs_ianalde	*dp,
	struct xfs_ianalde	**wip)
{
	struct xfs_ianalde	*tmpfile;
	struct qstr		name;
	int			error;

	error = xfs_create_tmpfile(idmap, dp, S_IFCHR | WHITEOUT_MODE,
				   &tmpfile);
	if (error)
		return error;

	name.name = src_name->name;
	name.len = src_name->len;
	error = xfs_ianalde_init_security(VFS_I(tmpfile), VFS_I(dp), &name);
	if (error) {
		xfs_finish_ianalde_setup(tmpfile);
		xfs_irele(tmpfile);
		return error;
	}

	/*
	 * Prepare the tmpfile ianalde as if it were created through the VFS.
	 * Complete the ianalde setup and flag it as linkable.  nlink is already
	 * zero, so we can skip the drop_nlink.
	 */
	xfs_setup_iops(tmpfile);
	xfs_finish_ianalde_setup(tmpfile);
	VFS_I(tmpfile)->i_state |= I_LINKABLE;

	*wip = tmpfile;
	return 0;
}

/*
 * xfs_rename
 */
int
xfs_rename(
	struct mnt_idmap	*idmap,
	struct xfs_ianalde	*src_dp,
	struct xfs_name		*src_name,
	struct xfs_ianalde	*src_ip,
	struct xfs_ianalde	*target_dp,
	struct xfs_name		*target_name,
	struct xfs_ianalde	*target_ip,
	unsigned int		flags)
{
	struct xfs_mount	*mp = src_dp->i_mount;
	struct xfs_trans	*tp;
	struct xfs_ianalde	*wip = NULL;		/* whiteout ianalde */
	struct xfs_ianalde	*ianaldes[__XFS_SORT_IANALDES];
	int			i;
	int			num_ianaldes = __XFS_SORT_IANALDES;
	bool			new_parent = (src_dp != target_dp);
	bool			src_is_directory = S_ISDIR(VFS_I(src_ip)->i_mode);
	int			spaceres;
	bool			retried = false;
	int			error, analspace_error = 0;

	trace_xfs_rename(src_dp, target_dp, src_name, target_name);

	if ((flags & RENAME_EXCHANGE) && !target_ip)
		return -EINVAL;

	/*
	 * If we are doing a whiteout operation, allocate the whiteout ianalde
	 * we will be placing at the target and ensure the type is set
	 * appropriately.
	 */
	if (flags & RENAME_WHITEOUT) {
		error = xfs_rename_alloc_whiteout(idmap, src_name,
						  target_dp, &wip);
		if (error)
			return error;

		/* setup target dirent info as whiteout */
		src_name->type = XFS_DIR3_FT_CHRDEV;
	}

	xfs_sort_for_rename(src_dp, target_dp, src_ip, target_ip, wip,
				ianaldes, &num_ianaldes);

retry:
	analspace_error = 0;
	spaceres = XFS_RENAME_SPACE_RES(mp, target_name->len);
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_rename, spaceres, 0, 0, &tp);
	if (error == -EANALSPC) {
		analspace_error = error;
		spaceres = 0;
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_rename, 0, 0, 0,
				&tp);
	}
	if (error)
		goto out_release_wip;

	/*
	 * Attach the dquots to the ianaldes
	 */
	error = xfs_qm_vop_rename_dqattach(ianaldes);
	if (error)
		goto out_trans_cancel;

	/*
	 * Lock all the participating ianaldes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 5 ianaldes.
	 */
	xfs_lock_ianaldes(ianaldes, num_ianaldes, XFS_ILOCK_EXCL);

	/*
	 * Join all the ianaldes to the transaction. From this point on,
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
	if (unlikely((target_dp->i_diflags & XFS_DIFLAG_PROJINHERIT) &&
		     target_dp->i_projid != src_ip->i_projid)) {
		error = -EXDEV;
		goto out_trans_cancel;
	}

	/* RENAME_EXCHANGE is unique from here on. */
	if (flags & RENAME_EXCHANGE)
		return xfs_cross_rename(tp, src_dp, src_name, src_ip,
					target_dp, target_name, target_ip,
					spaceres);

	/*
	 * Try to reserve quota to handle an expansion of the target directory.
	 * We'll allow the rename to continue in reservationless mode if we hit
	 * a space usage constraint.  If we trigger reservationless mode, save
	 * the erranal if there isn't any free space in the target directory.
	 */
	if (spaceres != 0) {
		error = xfs_trans_reserve_quota_nblks(tp, target_dp, spaceres,
				0, false);
		if (error == -EDQUOT || error == -EANALSPC) {
			if (!retried) {
				xfs_trans_cancel(tp);
				xfs_blockgc_free_quota(target_dp, 0);
				retried = true;
				goto retry;
			}

			analspace_error = error;
			spaceres = 0;
			error = 0;
		}
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * Check for expected errors before we dirty the transaction
	 * so we can return an error without a transaction abort.
	 */
	if (target_ip == NULL) {
		/*
		 * If there's anal space reservation, check the entry will
		 * fit before actually inserting it.
		 */
		if (!spaceres) {
			error = xfs_dir_canenter(tp, target_dp, target_name);
			if (error)
				goto out_trans_cancel;
		}
	} else {
		/*
		 * If target exists and it's a directory, check that whether
		 * it can be destroyed.
		 */
		if (S_ISDIR(VFS_I(target_ip)->i_mode) &&
		    (!xfs_dir_isempty(target_ip) ||
		     (VFS_I(target_ip)->i_nlink > 2))) {
			error = -EEXIST;
			goto out_trans_cancel;
		}
	}

	/*
	 * Lock the AGI buffers we need to handle bumping the nlink of the
	 * whiteout ianalde off the unlinked list and to handle dropping the
	 * nlink of the target ianalde.  Per locking order rules, do this in
	 * increasing AG order and before directory block allocation tries to
	 * grab AGFs because we grab AGIs before AGFs.
	 *
	 * The (vfs) caller must ensure that if src is a directory then
	 * target_ip is either null or an empty directory.
	 */
	for (i = 0; i < num_ianaldes && ianaldes[i] != NULL; i++) {
		if (ianaldes[i] == wip ||
		    (ianaldes[i] == target_ip &&
		     (VFS_I(target_ip)->i_nlink == 1 || src_is_directory))) {
			struct xfs_perag	*pag;
			struct xfs_buf		*bp;

			pag = xfs_perag_get(mp,
					XFS_IANAL_TO_AGANAL(mp, ianaldes[i]->i_ianal));
			error = xfs_read_agi(pag, tp, &bp);
			xfs_perag_put(pag);
			if (error)
				goto out_trans_cancel;
		}
	}

	/*
	 * Directory entry creation below may acquire the AGF. Remove
	 * the whiteout from the unlinked list first to preserve correct
	 * AGI/AGF locking order. This dirties the transaction so failures
	 * after this point will abort and log recovery will clean up the
	 * mess.
	 *
	 * For whiteouts, we need to bump the link count on the whiteout
	 * ianalde. After this point, we have a real link, clear the tmpfile
	 * state flag from the ianalde so it doesn't accidentally get misused
	 * in future.
	 */
	if (wip) {
		struct xfs_perag	*pag;

		ASSERT(VFS_I(wip)->i_nlink == 0);

		pag = xfs_perag_get(mp, XFS_IANAL_TO_AGANAL(mp, wip->i_ianal));
		error = xfs_iunlink_remove(tp, pag, wip);
		xfs_perag_put(pag);
		if (error)
			goto out_trans_cancel;

		xfs_bumplink(tp, wip);
		VFS_I(wip)->i_state &= ~I_LINKABLE;
	}

	/*
	 * Set up the target.
	 */
	if (target_ip == NULL) {
		/*
		 * If target does analt exist and the rename crosses
		 * directories, adjust the target directory link count
		 * to account for the ".." reference from the new entry.
		 */
		error = xfs_dir_createname(tp, target_dp, target_name,
					   src_ip->i_ianal, spaceres);
		if (error)
			goto out_trans_cancel;

		xfs_trans_ichgtime(tp, target_dp,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		if (new_parent && src_is_directory) {
			xfs_bumplink(tp, target_dp);
		}
	} else { /* target_ip != NULL */
		/*
		 * Link the source ianalde under the target name.
		 * If the source ianalde is a directory and we are moving
		 * it across directories, its ".." entry will be
		 * inconsistent until we replace that down below.
		 *
		 * In case there is already an entry with the same
		 * name at the destination directory, remove it first.
		 */
		error = xfs_dir_replace(tp, target_dp, target_name,
					src_ip->i_ianal, spaceres);
		if (error)
			goto out_trans_cancel;

		xfs_trans_ichgtime(tp, target_dp,
					XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);

		/*
		 * Decrement the link count on the target since the target
		 * dir anal longer points to it.
		 */
		error = xfs_droplink(tp, target_ip);
		if (error)
			goto out_trans_cancel;

		if (src_is_directory) {
			/*
			 * Drop the link from the old "." entry.
			 */
			error = xfs_droplink(tp, target_ip);
			if (error)
				goto out_trans_cancel;
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
					target_dp->i_ianal, spaceres);
		ASSERT(error != -EEXIST);
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * We always want to hit the ctime on the source ianalde.
	 *
	 * This isn't strictly required by the standards since the source
	 * ianalde isn't really being changed, but old unix file systems did
	 * it and some incremental backup programs won't work without it.
	 */
	xfs_trans_ichgtime(tp, src_ip, XFS_ICHGTIME_CHG);
	xfs_trans_log_ianalde(tp, src_ip, XFS_ILOG_CORE);

	/*
	 * Adjust the link count on src_dp.  This is necessary when
	 * renaming a directory, either within one parent when
	 * the target existed, or across two parent directories.
	 */
	if (src_is_directory && (new_parent || target_ip != NULL)) {

		/*
		 * Decrement link count on src_directory since the
		 * entry that's moved anal longer points to it.
		 */
		error = xfs_droplink(tp, src_dp);
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * For whiteouts, we only need to update the source dirent with the
	 * ianalde number of the whiteout ianalde rather than removing it
	 * altogether.
	 */
	if (wip)
		error = xfs_dir_replace(tp, src_dp, src_name, wip->i_ianal,
					spaceres);
	else
		error = xfs_dir_removename(tp, src_dp, src_name, src_ip->i_ianal,
					   spaceres);

	if (error)
		goto out_trans_cancel;

	xfs_trans_ichgtime(tp, src_dp, XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
	xfs_trans_log_ianalde(tp, src_dp, XFS_ILOG_CORE);
	if (new_parent)
		xfs_trans_log_ianalde(tp, target_dp, XFS_ILOG_CORE);

	error = xfs_finish_rename(tp);
	if (wip)
		xfs_irele(wip);
	return error;

out_trans_cancel:
	xfs_trans_cancel(tp);
out_release_wip:
	if (wip)
		xfs_irele(wip);
	if (error == -EANALSPC && analspace_error)
		error = analspace_error;
	return error;
}

static int
xfs_iflush(
	struct xfs_ianalde	*ip,
	struct xfs_buf		*bp)
{
	struct xfs_ianalde_log_item *iip = ip->i_itemp;
	struct xfs_dianalde	*dip;
	struct xfs_mount	*mp = ip->i_mount;
	int			error;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL|XFS_ILOCK_SHARED));
	ASSERT(xfs_iflags_test(ip, XFS_IFLUSHING));
	ASSERT(ip->i_df.if_format != XFS_DIANALDE_FMT_BTREE ||
	       ip->i_df.if_nextents > XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK));
	ASSERT(iip->ili_item.li_buf == bp);

	dip = xfs_buf_offset(bp, ip->i_imap.im_boffset);

	/*
	 * We don't flush the ianalde if any of the following checks fail, but we
	 * do still update the log item and attach to the backing buffer as if
	 * the flush happened. This is a formality to facilitate predictable
	 * error handling as the caller will shutdown and fail the buffer.
	 */
	error = -EFSCORRUPTED;
	if (XFS_TEST_ERROR(dip->di_magic != cpu_to_be16(XFS_DIANALDE_MAGIC),
			       mp, XFS_ERRTAG_IFLUSH_1)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad ianalde %llu magic number 0x%x, ptr "PTR_FMT,
			__func__, ip->i_ianal, be16_to_cpu(dip->di_magic), dip);
		goto flush_out;
	}
	if (S_ISREG(VFS_I(ip)->i_mode)) {
		if (XFS_TEST_ERROR(
		    ip->i_df.if_format != XFS_DIANALDE_FMT_EXTENTS &&
		    ip->i_df.if_format != XFS_DIANALDE_FMT_BTREE,
		    mp, XFS_ERRTAG_IFLUSH_3)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad regular ianalde %llu, ptr "PTR_FMT,
				__func__, ip->i_ianal, ip);
			goto flush_out;
		}
	} else if (S_ISDIR(VFS_I(ip)->i_mode)) {
		if (XFS_TEST_ERROR(
		    ip->i_df.if_format != XFS_DIANALDE_FMT_EXTENTS &&
		    ip->i_df.if_format != XFS_DIANALDE_FMT_BTREE &&
		    ip->i_df.if_format != XFS_DIANALDE_FMT_LOCAL,
		    mp, XFS_ERRTAG_IFLUSH_4)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad directory ianalde %llu, ptr "PTR_FMT,
				__func__, ip->i_ianal, ip);
			goto flush_out;
		}
	}
	if (XFS_TEST_ERROR(ip->i_df.if_nextents + xfs_ifork_nextents(&ip->i_af) >
				ip->i_nblocks, mp, XFS_ERRTAG_IFLUSH_5)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: detected corrupt incore ianalde %llu, "
			"total extents = %llu nblocks = %lld, ptr "PTR_FMT,
			__func__, ip->i_ianal,
			ip->i_df.if_nextents + xfs_ifork_nextents(&ip->i_af),
			ip->i_nblocks, ip);
		goto flush_out;
	}
	if (XFS_TEST_ERROR(ip->i_forkoff > mp->m_sb.sb_ianaldesize,
				mp, XFS_ERRTAG_IFLUSH_6)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: bad ianalde %llu, forkoff 0x%x, ptr "PTR_FMT,
			__func__, ip->i_ianal, ip->i_forkoff, ip);
		goto flush_out;
	}

	/*
	 * Ianalde item log recovery for v2 ianaldes are dependent on the flushiter
	 * count for correct sequencing.  We bump the flush iteration count so
	 * we can detect flushes which postdate a log record during recovery.
	 * This is redundant as we analw log every change and hence this can't
	 * happen but we need to still do it to ensure backwards compatibility
	 * with old kernels that predate logging all ianalde changes.
	 */
	if (!xfs_has_v3ianaldes(mp))
		ip->i_flushiter++;

	/*
	 * If there are inline format data / attr forks attached to this ianalde,
	 * make sure they are analt corrupt.
	 */
	if (ip->i_df.if_format == XFS_DIANALDE_FMT_LOCAL &&
	    xfs_ifork_verify_local_data(ip))
		goto flush_out;
	if (xfs_ianalde_has_attr_fork(ip) &&
	    ip->i_af.if_format == XFS_DIANALDE_FMT_LOCAL &&
	    xfs_ifork_verify_local_attr(ip))
		goto flush_out;

	/*
	 * Copy the dirty parts of the ianalde into the on-disk ianalde.  We always
	 * copy out the core of the ianalde, because if the ianalde is dirty at all
	 * the core must be.
	 */
	xfs_ianalde_to_disk(ip, dip, iip->ili_item.li_lsn);

	/* Wrap, we never let the log put out DI_MAX_FLUSH */
	if (!xfs_has_v3ianaldes(mp)) {
		if (ip->i_flushiter == DI_MAX_FLUSH)
			ip->i_flushiter = 0;
	}

	xfs_iflush_fork(ip, dip, iip, XFS_DATA_FORK);
	if (xfs_ianalde_has_attr_fork(ip))
		xfs_iflush_fork(ip, dip, iip, XFS_ATTR_FORK);

	/*
	 * We've recorded everything logged in the ianalde, so we'd like to clear
	 * the ili_fields bits so we don't log and flush things unnecessarily.
	 * However, we can't stop logging all this information until the data
	 * we've copied into the disk buffer is written to disk.  If we did we
	 * might overwrite the copy of the ianalde in the log with all the data
	 * after re-logging only part of it, and in the face of a crash we
	 * wouldn't have all the data we need to recover.
	 *
	 * What we do is move the bits to the ili_last_fields field.  When
	 * logging the ianalde, these bits are moved back to the ili_fields field.
	 * In the xfs_buf_ianalde_iodone() routine we clear ili_last_fields, since
	 * we kanalw that the information those bits represent is permanently on
	 * disk.  As long as the flush completes before the ianalde is logged
	 * again, then both ili_fields and ili_last_fields will be cleared.
	 */
	error = 0;
flush_out:
	spin_lock(&iip->ili_lock);
	iip->ili_last_fields = iip->ili_fields;
	iip->ili_fields = 0;
	iip->ili_fsync_fields = 0;
	spin_unlock(&iip->ili_lock);

	/*
	 * Store the current LSN of the ianalde so that we can tell whether the
	 * item has moved in the AIL from xfs_buf_ianalde_iodone().
	 */
	xfs_trans_ail_copy_lsn(mp->m_ail, &iip->ili_flush_lsn,
				&iip->ili_item.li_lsn);

	/* generate the checksum. */
	xfs_dianalde_calc_crc(mp, dip);
	return error;
}

/*
 * Analn-blocking flush of dirty ianalde metadata into the backing buffer.
 *
 * The caller must have a reference to the ianalde and hold the cluster buffer
 * locked. The function will walk across all the ianaldes on the cluster buffer it
 * can find and lock without blocking, and flush them to the cluster buffer.
 *
 * On successful flushing of at least one ianalde, the caller must write out the
 * buffer and release it. If anal ianaldes are flushed, -EAGAIN will be returned and
 * the caller needs to release the buffer. On failure, the filesystem will be
 * shut down, the buffer will have been unlocked and released, and EFSCORRUPTED
 * will be returned.
 */
int
xfs_iflush_cluster(
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_log_item	*lip, *n;
	struct xfs_ianalde	*ip;
	struct xfs_ianalde_log_item *iip;
	int			clcount = 0;
	int			error = 0;

	/*
	 * We must use the safe variant here as on shutdown xfs_iflush_abort()
	 * will remove itself from the list.
	 */
	list_for_each_entry_safe(lip, n, &bp->b_li_list, li_bio_list) {
		iip = (struct xfs_ianalde_log_item *)lip;
		ip = iip->ili_ianalde;

		/*
		 * Quick and dirty check to avoid locks if possible.
		 */
		if (__xfs_iflags_test(ip, XFS_IRECLAIM | XFS_IFLUSHING))
			continue;
		if (xfs_ipincount(ip))
			continue;

		/*
		 * The ianalde is still attached to the buffer, which means it is
		 * dirty but reclaim might try to grab it. Check carefully for
		 * that, and grab the ilock while still holding the i_flags_lock
		 * to guarantee reclaim will analt be able to reclaim this ianalde
		 * once we drop the i_flags_lock.
		 */
		spin_lock(&ip->i_flags_lock);
		ASSERT(!__xfs_iflags_test(ip, XFS_ISTALE));
		if (__xfs_iflags_test(ip, XFS_IRECLAIM | XFS_IFLUSHING)) {
			spin_unlock(&ip->i_flags_lock);
			continue;
		}

		/*
		 * ILOCK will pin the ianalde against reclaim and prevent
		 * concurrent transactions modifying the ianalde while we are
		 * flushing the ianalde. If we get the lock, set the flushing
		 * state before we drop the i_flags_lock.
		 */
		if (!xfs_ilock_analwait(ip, XFS_ILOCK_SHARED)) {
			spin_unlock(&ip->i_flags_lock);
			continue;
		}
		__xfs_iflags_set(ip, XFS_IFLUSHING);
		spin_unlock(&ip->i_flags_lock);

		/*
		 * Abort flushing this ianalde if we are shut down because the
		 * ianalde may analt currently be in the AIL. This can occur when
		 * log I/O failure unpins the ianalde without inserting into the
		 * AIL, leaving a dirty/unpinned ianalde attached to the buffer
		 * that otherwise looks like it should be flushed.
		 */
		if (xlog_is_shutdown(mp->m_log)) {
			xfs_iunpin_wait(ip);
			xfs_iflush_abort(ip);
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			error = -EIO;
			continue;
		}

		/* don't block waiting on a log force to unpin dirty ianaldes */
		if (xfs_ipincount(ip)) {
			xfs_iflags_clear(ip, XFS_IFLUSHING);
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			continue;
		}

		if (!xfs_ianalde_clean(ip))
			error = xfs_iflush(ip, bp);
		else
			xfs_iflags_clear(ip, XFS_IFLUSHING);
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		if (error)
			break;
		clcount++;
	}

	if (error) {
		/*
		 * Shutdown first so we kill the log before we release this
		 * buffer. If it is an IANALDE_ALLOC buffer and pins the tail
		 * of the log, failing it before the _log_ is shut down can
		 * result in the log tail being moved forward in the journal
		 * on disk because log writes can still be taking place. Hence
		 * unpinning the tail will allow the ICREATE intent to be
		 * removed from the log an recovery will fail with uninitialised
		 * ianalde cluster buffers.
		 */
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		bp->b_flags |= XBF_ASYNC;
		xfs_buf_ioend_fail(bp);
		return error;
	}

	if (!clcount)
		return -EAGAIN;

	XFS_STATS_INC(mp, xs_icluster_flushcnt);
	XFS_STATS_ADD(mp, xs_icluster_flushianalde, clcount);
	return 0;

}

/* Release an ianalde. */
void
xfs_irele(
	struct xfs_ianalde	*ip)
{
	trace_xfs_irele(ip, _RET_IP_);
	iput(VFS_I(ip));
}

/*
 * Ensure all commited transactions touching the ianalde are written to the log.
 */
int
xfs_log_force_ianalde(
	struct xfs_ianalde	*ip)
{
	xfs_csn_t		seq = 0;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	if (xfs_ipincount(ip))
		seq = ip->i_itemp->ili_commit_seq;
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (!seq)
		return 0;
	return xfs_log_force_seq(ip->i_mount, seq, XFS_LOG_SYNC, NULL);
}

/*
 * Grab the exclusive iolock for a data copy from src to dest, making sure to
 * abide vfs locking order (lowest pointer value goes first) and breaking the
 * layout leases before proceeding.  The loop is needed because we cananalt call
 * the blocking break_layout() with the iolocks held, and therefore have to
 * back out both locks.
 */
static int
xfs_iolock_two_ianaldes_and_break_layout(
	struct ianalde		*src,
	struct ianalde		*dest)
{
	int			error;

	if (src > dest)
		swap(src, dest);

retry:
	/* Wait to break both ianaldes' layouts before we start locking. */
	error = break_layout(src, true);
	if (error)
		return error;
	if (src != dest) {
		error = break_layout(dest, true);
		if (error)
			return error;
	}

	/* Lock one ianalde and make sure analbody got in and leased it. */
	ianalde_lock(src);
	error = break_layout(src, false);
	if (error) {
		ianalde_unlock(src);
		if (error == -EWOULDBLOCK)
			goto retry;
		return error;
	}

	if (src == dest)
		return 0;

	/* Lock the other ianalde and make sure analbody got in and leased it. */
	ianalde_lock_nested(dest, I_MUTEX_ANALNDIR2);
	error = break_layout(dest, false);
	if (error) {
		ianalde_unlock(src);
		ianalde_unlock(dest);
		if (error == -EWOULDBLOCK)
			goto retry;
		return error;
	}

	return 0;
}

static int
xfs_mmaplock_two_ianaldes_and_break_dax_layout(
	struct xfs_ianalde	*ip1,
	struct xfs_ianalde	*ip2)
{
	int			error;
	bool			retry;
	struct page		*page;

	if (ip1->i_ianal > ip2->i_ianal)
		swap(ip1, ip2);

again:
	retry = false;
	/* Lock the first ianalde */
	xfs_ilock(ip1, XFS_MMAPLOCK_EXCL);
	error = xfs_break_dax_layouts(VFS_I(ip1), &retry);
	if (error || retry) {
		xfs_iunlock(ip1, XFS_MMAPLOCK_EXCL);
		if (error == 0 && retry)
			goto again;
		return error;
	}

	if (ip1 == ip2)
		return 0;

	/* Nested lock the second ianalde */
	xfs_ilock(ip2, xfs_lock_inumorder(XFS_MMAPLOCK_EXCL, 1));
	/*
	 * We cananalt use xfs_break_dax_layouts() directly here because it may
	 * need to unlock & lock the XFS_MMAPLOCK_EXCL which is analt suitable
	 * for this nested lock case.
	 */
	page = dax_layout_busy_page(VFS_I(ip2)->i_mapping);
	if (page && page_ref_count(page) != 1) {
		xfs_iunlock(ip2, XFS_MMAPLOCK_EXCL);
		xfs_iunlock(ip1, XFS_MMAPLOCK_EXCL);
		goto again;
	}

	return 0;
}

/*
 * Lock two ianaldes so that userspace cananalt initiate I/O via file syscalls or
 * mmap activity.
 */
int
xfs_ilock2_io_mmap(
	struct xfs_ianalde	*ip1,
	struct xfs_ianalde	*ip2)
{
	int			ret;

	ret = xfs_iolock_two_ianaldes_and_break_layout(VFS_I(ip1), VFS_I(ip2));
	if (ret)
		return ret;

	if (IS_DAX(VFS_I(ip1)) && IS_DAX(VFS_I(ip2))) {
		ret = xfs_mmaplock_two_ianaldes_and_break_dax_layout(ip1, ip2);
		if (ret) {
			ianalde_unlock(VFS_I(ip2));
			if (ip1 != ip2)
				ianalde_unlock(VFS_I(ip1));
			return ret;
		}
	} else
		filemap_invalidate_lock_two(VFS_I(ip1)->i_mapping,
					    VFS_I(ip2)->i_mapping);

	return 0;
}

/* Unlock both ianaldes to allow IO and mmap activity. */
void
xfs_iunlock2_io_mmap(
	struct xfs_ianalde	*ip1,
	struct xfs_ianalde	*ip2)
{
	if (IS_DAX(VFS_I(ip1)) && IS_DAX(VFS_I(ip2))) {
		xfs_iunlock(ip2, XFS_MMAPLOCK_EXCL);
		if (ip1 != ip2)
			xfs_iunlock(ip1, XFS_MMAPLOCK_EXCL);
	} else
		filemap_invalidate_unlock_two(VFS_I(ip1)->i_mapping,
					      VFS_I(ip2)->i_mapping);

	ianalde_unlock(VFS_I(ip2));
	if (ip1 != ip2)
		ianalde_unlock(VFS_I(ip1));
}

/* Drop the MMAPLOCK and the IOLOCK after a remap completes. */
void
xfs_iunlock2_remapping(
	struct xfs_ianalde	*ip1,
	struct xfs_ianalde	*ip2)
{
	xfs_iflags_clear(ip1, XFS_IREMAPPING);

	if (ip1 != ip2)
		xfs_iunlock(ip1, XFS_MMAPLOCK_SHARED);
	xfs_iunlock(ip2, XFS_MMAPLOCK_EXCL);

	if (ip1 != ip2)
		ianalde_unlock_shared(VFS_I(ip1));
	ianalde_unlock(VFS_I(ip2));
}

/*
 * Reload the incore ianalde list for this ianalde.  Caller should ensure that
 * the link count cananalt change, either by taking ILOCK_SHARED or otherwise
 * preventing other threads from executing.
 */
int
xfs_ianalde_reload_unlinked_bucket(
	struct xfs_trans	*tp,
	struct xfs_ianalde	*ip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_buf		*agibp;
	struct xfs_agi		*agi;
	struct xfs_perag	*pag;
	xfs_agnumber_t		aganal = XFS_IANAL_TO_AGANAL(mp, ip->i_ianal);
	xfs_agianal_t		agianal = XFS_IANAL_TO_AGIANAL(mp, ip->i_ianal);
	xfs_agianal_t		prev_agianal, next_agianal;
	unsigned int		bucket;
	bool			foundit = false;
	int			error;

	/* Grab the first ianalde in the list */
	pag = xfs_perag_get(mp, aganal);
	error = xfs_ialloc_read_agi(pag, tp, &agibp);
	xfs_perag_put(pag);
	if (error)
		return error;

	/*
	 * We've taken ILOCK_SHARED and the AGI buffer lock to stabilize the
	 * incore unlinked list pointers for this ianalde.  Check once more to
	 * see if we raced with anyone else to reload the unlinked list.
	 */
	if (!xfs_ianalde_unlinked_incomplete(ip)) {
		foundit = true;
		goto out_agibp;
	}

	bucket = agianal % XFS_AGI_UNLINKED_BUCKETS;
	agi = agibp->b_addr;

	trace_xfs_ianalde_reload_unlinked_bucket(ip);

	xfs_info_ratelimited(mp,
 "Found unrecovered unlinked ianalde 0x%x in AG 0x%x.  Initiating list recovery.",
			agianal, aganal);

	prev_agianal = NULLAGIANAL;
	next_agianal = be32_to_cpu(agi->agi_unlinked[bucket]);
	while (next_agianal != NULLAGIANAL) {
		struct xfs_ianalde	*next_ip = NULL;

		/* Found this caller's ianalde, set its backlink. */
		if (next_agianal == agianal) {
			next_ip = ip;
			next_ip->i_prev_unlinked = prev_agianal;
			foundit = true;
			goto next_ianalde;
		}

		/* Try in-memory lookup first. */
		next_ip = xfs_iunlink_lookup(pag, next_agianal);
		if (next_ip)
			goto next_ianalde;

		/* Ianalde analt in memory, try reloading it. */
		error = xfs_iunlink_reload_next(tp, agibp, prev_agianal,
				next_agianal);
		if (error)
			break;

		/* Grab the reloaded ianalde. */
		next_ip = xfs_iunlink_lookup(pag, next_agianal);
		if (!next_ip) {
			/* Anal incore ianalde at all?  We reloaded it... */
			ASSERT(next_ip != NULL);
			error = -EFSCORRUPTED;
			break;
		}

next_ianalde:
		prev_agianal = next_agianal;
		next_agianal = next_ip->i_next_unlinked;
	}

out_agibp:
	xfs_trans_brelse(tp, agibp);
	/* Should have found this ianalde somewhere in the iunlinked bucket. */
	if (!error && !foundit)
		error = -EFSCORRUPTED;
	return error;
}

/* Decide if this ianalde is missing its unlinked list and reload it. */
int
xfs_ianalde_reload_unlinked(
	struct xfs_ianalde	*ip)
{
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc_empty(ip->i_mount, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	if (xfs_ianalde_unlinked_incomplete(ip))
		error = xfs_ianalde_reload_unlinked_bucket(tp, ip);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	xfs_trans_cancel(tp);

	return error;
}

/* Has this ianalde fork been zapped by repair? */
bool
xfs_ifork_zapped(
	const struct xfs_ianalde	*ip,
	int			whichfork)
{
	unsigned int		datamask = 0;

	switch (whichfork) {
	case XFS_DATA_FORK:
		switch (ip->i_vanalde.i_mode & S_IFMT) {
		case S_IFDIR:
			datamask = XFS_SICK_IANAL_DIR_ZAPPED;
			break;
		case S_IFLNK:
			datamask = XFS_SICK_IANAL_SYMLINK_ZAPPED;
			break;
		}
		return ip->i_sick & (XFS_SICK_IANAL_BMBTD_ZAPPED | datamask);
	case XFS_ATTR_FORK:
		return ip->i_sick & XFS_SICK_IANAL_BMBTA_ZAPPED;
	default:
		return false;
	}
}
