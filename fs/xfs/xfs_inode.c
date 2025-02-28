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
#include "xfs_inode.h"
#include "xfs_dir2.h"
#include "xfs_attr.h"
#include "xfs_bit.h"
#include "xfs_trans_space.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_inode_item.h"
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
#include "xfs_pnfs.h"
#include "xfs_parent.h"
#include "xfs_xattr.h"
#include "xfs_inode_util.h"
#include "xfs_metafile.h"

struct kmem_cache *xfs_inode_cache;

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

	if (xfs_need_iread_extents(&ip->i_df))
		lock_mode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lock_mode);
	return lock_mode;
}

uint
xfs_ilock_attr_map_shared(
	struct xfs_inode	*ip)
{
	uint			lock_mode = XFS_ILOCK_SHARED;

	if (xfs_inode_has_attr_fork(ip) && xfs_need_iread_extents(&ip->i_af))
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
 * In addition to i_rwsem in the VFS inode, the xfs inode contains 2
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
 * The difference in mmap_lock locking order mean that we cannot hold the
 * invalidate_lock over syscall based read(2)/write(2) based IO. These IO paths
 * can fault in pages during copy in/out (for buffered IO) or require the
 * mmap_lock in get_user_pages() to map the user pages into the kernel address
 * space for direct IO. Similarly the i_rwsem cannot be taken inside a page
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
	xfs_inode_t		*ip,
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
		down_write_nested(&ip->i_lock, XFS_ILOCK_DEP(lock_flags));
	else if (lock_flags & XFS_ILOCK_SHARED)
		down_read_nested(&ip->i_lock, XFS_ILOCK_DEP(lock_flags));
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
		if (!down_write_trylock(&ip->i_lock))
			goto out_undo_mmaplock;
	} else if (lock_flags & XFS_ILOCK_SHARED) {
		if (!down_read_trylock(&ip->i_lock))
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
		up_write(&ip->i_lock);
	else if (lock_flags & XFS_ILOCK_SHARED)
		up_read(&ip->i_lock);

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
		downgrade_write(&ip->i_lock);
	if (lock_flags & XFS_MMAPLOCK_EXCL)
		downgrade_write(&VFS_I(ip)->i_mapping->invalidate_lock);
	if (lock_flags & XFS_IOLOCK_EXCL)
		downgrade_write(&VFS_I(ip)->i_rwsem);

	trace_xfs_ilock_demote(ip, lock_flags, _RET_IP_);
}

void
xfs_assert_ilocked(
	struct xfs_inode	*ip,
	uint			lock_flags)
{
	/*
	 * Sometimes we assert the ILOCK is held exclusively, but we're in
	 * a workqueue, so lockdep doesn't know we're the owner.
	 */
	if (lock_flags & XFS_ILOCK_SHARED)
		rwsem_assert_held(&ip->i_lock);
	else if (lock_flags & XFS_ILOCK_EXCL)
		rwsem_assert_held_write_nolockdep(&ip->i_lock);

	if (lock_flags & XFS_MMAPLOCK_SHARED)
		rwsem_assert_held(&VFS_I(ip)->i_mapping->invalidate_lock);
	else if (lock_flags & XFS_MMAPLOCK_EXCL)
		rwsem_assert_held_write(&VFS_I(ip)->i_mapping->invalidate_lock);

	if (lock_flags & XFS_IOLOCK_SHARED)
		rwsem_assert_held(&VFS_I(ip)->i_rwsem);
	else if (lock_flags & XFS_IOLOCK_EXCL)
		rwsem_assert_held_write(&VFS_I(ip)->i_rwsem);
}

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
static inline uint
xfs_lock_inumorder(
	uint	lock_mode,
	uint	subclass)
{
	uint	class = 0;

	ASSERT(!(lock_mode & XFS_ILOCK_PARENT));
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
void
xfs_lock_inodes(
	struct xfs_inode	**ips,
	int			inodes,
	uint			lock_mode)
{
	int			attempts = 0;
	uint			i;
	int			j;
	bool			try_lock;
	struct xfs_log_item	*lp;

	/*
	 * Currently supports between 2 and 5 inodes with exclusive locking.  We
	 * support an arbitrary depth of locking here, but absolute limits on
	 * inodes depend on the type of locking and the limits placed by
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

again:
	try_lock = false;
	i = 0;
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
		goto again;
	}
}

/*
 * xfs_lock_two_inodes() can only be used to lock ilock. The iolock and
 * mmaplock must be double-locked separately since we use i_rwsem and
 * invalidate_lock for that. We now support taking one lock EXCL and the
 * other SHARED.
 */
void
xfs_lock_two_inodes(
	struct xfs_inode	*ip0,
	uint			ip0_mode,
	struct xfs_inode	*ip1,
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
	ASSERT(ip0->i_ino != ip1->i_ino);

	if (ip0->i_ino > ip1->i_ino) {
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

/*
 * Lookups up an inode from "name". If ci_name is not NULL, then a CI match
 * is allowed, otherwise it has to be an exact match. If a CI match is found,
 * ci_name->name will point to a the actual name (caller must free) or
 * will be set to NULL if an exact match is found.
 */
int
xfs_lookup(
	struct xfs_inode	*dp,
	const struct xfs_name	*name,
	struct xfs_inode	**ipp,
	struct xfs_name		*ci_name)
{
	xfs_ino_t		inum;
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

	/*
	 * Fail if a directory entry in the regular directory tree points to
	 * a metadata file.
	 */
	if (XFS_IS_CORRUPT(dp->i_mount, xfs_is_metadir_inode(*ipp))) {
		xfs_fs_mark_sick(dp->i_mount, XFS_SICK_FS_METADIR);
		error = -EFSCORRUPTED;
		goto out_irele;
	}

	return 0;

out_irele:
	xfs_irele(*ipp);
out_free_name:
	if (ci_name)
		kfree(ci_name->name);
out_unlock:
	*ipp = NULL;
	return error;
}

/*
 * Initialise a newly allocated inode and return the in-core inode to the
 * caller locked exclusively.
 *
 * Caller is responsible for unlocking the inode manually upon return
 */
int
xfs_icreate(
	struct xfs_trans	*tp,
	xfs_ino_t		ino,
	const struct xfs_icreate_args *args,
	struct xfs_inode	**ipp)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_inode	*ip = NULL;
	int			error;

	/*
	 * Get the in-core inode with the lock held exclusively to prevent
	 * others from looking at until we're done.
	 */
	error = xfs_iget(mp, tp, ino, XFS_IGET_CREATE, XFS_ILOCK_EXCL, &ip);
	if (error)
		return error;

	ASSERT(ip != NULL);
	xfs_trans_ijoin(tp, ip, 0);
	xfs_inode_init(tp, args, ip);

	/* now that we have an i_mode we can setup the inode structure */
	xfs_setup_inode(ip);

	*ipp = ip;
	return 0;
}

/* Return dquots for the ids that will be assigned to a new file. */
int
xfs_icreate_dqalloc(
	const struct xfs_icreate_args	*args,
	struct xfs_dquot		**udqpp,
	struct xfs_dquot		**gdqpp,
	struct xfs_dquot		**pdqpp)
{
	struct inode			*dir = VFS_I(args->pip);
	kuid_t				uid = GLOBAL_ROOT_UID;
	kgid_t				gid = GLOBAL_ROOT_GID;
	prid_t				prid = 0;
	unsigned int			flags = XFS_QMOPT_QUOTALL;

	if (args->idmap) {
		/*
		 * The uid/gid computation code must match what the VFS uses to
		 * assign i_[ug]id.  INHERIT adjusts the gid computation for
		 * setgid/grpid systems.
		 */
		uid = mapped_fsuid(args->idmap, i_user_ns(dir));
		gid = mapped_fsgid(args->idmap, i_user_ns(dir));
		prid = xfs_get_initial_prid(args->pip);
		flags |= XFS_QMOPT_INHERIT;
	}

	*udqpp = *gdqpp = *pdqpp = NULL;

	return xfs_qm_vop_dqalloc(args->pip, uid, gid, prid, flags, udqpp,
			gdqpp, pdqpp);
}

int
xfs_create(
	const struct xfs_icreate_args *args,
	struct xfs_name		*name,
	struct xfs_inode	**ipp)
{
	struct xfs_inode	*dp = args->pip;
	struct xfs_dir_update	du = {
		.dp		= dp,
		.name		= name,
	};
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_trans	*tp = NULL;
	struct xfs_dquot	*udqp;
	struct xfs_dquot	*gdqp;
	struct xfs_dquot	*pdqp;
	struct xfs_trans_res	*tres;
	xfs_ino_t		ino;
	bool			unlock_dp_on_error = false;
	bool			is_dir = S_ISDIR(args->mode);
	uint			resblks;
	int			error;

	trace_xfs_create(dp, name);

	if (xfs_is_shutdown(mp))
		return -EIO;
	if (xfs_ifork_zapped(dp, XFS_DATA_FORK))
		return -EIO;

	/* Make sure that we have allocated dquot(s) on disk. */
	error = xfs_icreate_dqalloc(args, &udqp, &gdqp, &pdqp);
	if (error)
		return error;

	if (is_dir) {
		resblks = xfs_mkdir_space_res(mp, name->len);
		tres = &M_RES(mp)->tr_mkdir;
	} else {
		resblks = xfs_create_space_res(mp, name->len);
		tres = &M_RES(mp)->tr_create;
	}

	error = xfs_parent_start(mp, &du.ppargs);
	if (error)
		goto out_release_dquots;

	/*
	 * Initially assume that the file does not exist and
	 * reserve the resources for that case.  If that is not
	 * the case we'll drop the one we have and get a more
	 * appropriate transaction later.
	 */
	error = xfs_trans_alloc_icreate(mp, tres, udqp, gdqp, pdqp, resblks,
			&tp);
	if (error == -ENOSPC) {
		/* flush outstanding delalloc blocks and retry */
		xfs_flush_inodes(mp);
		error = xfs_trans_alloc_icreate(mp, tres, udqp, gdqp, pdqp,
				resblks, &tp);
	}
	if (error)
		goto out_parent;

	xfs_ilock(dp, XFS_ILOCK_EXCL | XFS_ILOCK_PARENT);
	unlock_dp_on_error = true;

	/*
	 * A newly created regular or special file just has one directory
	 * entry pointing to them, but a directory also the "." entry
	 * pointing to itself.
	 */
	error = xfs_dialloc(&tp, args, &ino);
	if (!error)
		error = xfs_icreate(tp, ino, args, &du.ip);
	if (error)
		goto out_trans_cancel;

	/*
	 * Now we join the directory inode to the transaction.  We do not do it
	 * earlier because xfs_dialloc might commit the previous transaction
	 * (and release all the locks).  An error from here on will result in
	 * the transaction cancel unlocking dp so don't do it explicitly in the
	 * error path.
	 */
	xfs_trans_ijoin(tp, dp, 0);

	error = xfs_dir_create_child(tp, resblks, &du);
	if (error)
		goto out_trans_cancel;

	/*
	 * If this is a synchronous mount, make sure that the
	 * create transaction goes to disk before returning to
	 * the user.
	 */
	if (xfs_has_wsync(mp) || xfs_has_dirsync(mp))
		xfs_trans_set_sync(tp);

	/*
	 * Attach the dquot(s) to the inodes and modify them incore.
	 * These ids of the inode couldn't have changed since the new
	 * inode has been locked ever since it was created.
	 */
	xfs_qm_vop_create_dqattach(tp, du.ip, udqp, gdqp, pdqp);

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

int
xfs_create_tmpfile(
	const struct xfs_icreate_args *args,
	struct xfs_inode	**ipp)
{
	struct xfs_inode	*dp = args->pip;
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_inode	*ip = NULL;
	struct xfs_trans	*tp = NULL;
	struct xfs_dquot	*udqp;
	struct xfs_dquot	*gdqp;
	struct xfs_dquot	*pdqp;
	struct xfs_trans_res	*tres;
	xfs_ino_t		ino;
	uint			resblks;
	int			error;

	ASSERT(args->flags & XFS_ICREATE_TMPFILE);

	if (xfs_is_shutdown(mp))
		return -EIO;

	/* Make sure that we have allocated dquot(s) on disk. */
	error = xfs_icreate_dqalloc(args, &udqp, &gdqp, &pdqp);
	if (error)
		return error;

	resblks = XFS_IALLOC_SPACE_RES(mp);
	tres = &M_RES(mp)->tr_create_tmpfile;

	error = xfs_trans_alloc_icreate(mp, tres, udqp, gdqp, pdqp, resblks,
			&tp);
	if (error)
		goto out_release_dquots;

	error = xfs_dialloc(&tp, args, &ino);
	if (!error)
		error = xfs_icreate(tp, ino, args, &ip);
	if (error)
		goto out_trans_cancel;

	if (xfs_has_wsync(mp))
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
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
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
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_finish_inode_setup(ip);
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
	struct xfs_inode	*tdp,
	struct xfs_inode	*sip,
	struct xfs_name		*target_name)
{
	struct xfs_dir_update	du = {
		.dp		= tdp,
		.name		= target_name,
		.ip		= sip,
	};
	struct xfs_mount	*mp = tdp->i_mount;
	struct xfs_trans	*tp;
	int			error, nospace_error = 0;
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

	error = xfs_parent_start(mp, &du.ppargs);
	if (error)
		goto std_return;

	resblks = xfs_link_space_res(mp, target_name->len);
	error = xfs_trans_alloc_dir(tdp, &M_RES(mp)->tr_link, sip, &resblks,
			&tp, &nospace_error);
	if (error)
		goto out_parent;

	/*
	 * We don't allow reservationless or quotaless hardlinking when parent
	 * pointers are enabled because we can't back out if the xattrs must
	 * grow.
	 */
	if (du.ppargs && nospace_error) {
		error = nospace_error;
		goto error_return;
	}

	/*
	 * If we are using project inheritance, we only allow hard link
	 * creation in our tree when the project IDs are the same; else
	 * the tree quota mechanism could be circumvented.
	 */
	if (unlikely((tdp->i_diflags & XFS_DIFLAG_PROJINHERIT) &&
		     tdp->i_projid != sip->i_projid)) {
		/*
		 * Project quota setup skips special files which can
		 * leave inodes in a PROJINHERIT directory without a
		 * project ID set. We need to allow links to be made
		 * to these "project-less" inodes because userspace
		 * expects them to succeed after project ID setup,
		 * but everything else should be rejected.
		 */
		if (!special_file(VFS_I(sip)->i_mode) ||
		    sip->i_projid != 0) {
			error = -EXDEV;
			goto error_return;
		}
	}

	error = xfs_dir_add_child(tp, resblks, &du);
	if (error)
		goto error_return;

	/*
	 * If this is a synchronous mount, make sure that the
	 * link transaction goes to disk before returning to
	 * the user.
	 */
	if (xfs_has_wsync(mp) || xfs_has_dirsync(mp))
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp);
	xfs_iunlock(tdp, XFS_ILOCK_EXCL);
	xfs_iunlock(sip, XFS_ILOCK_EXCL);
	xfs_parent_finish(mp, du.ppargs);
	return error;

 error_return:
	xfs_trans_cancel(tp);
	xfs_iunlock(tdp, XFS_ILOCK_EXCL);
	xfs_iunlock(sip, XFS_ILOCK_EXCL);
 out_parent:
	xfs_parent_finish(mp, du.ppargs);
 std_return:
	if (error == -ENOSPC && nospace_error)
		error = nospace_error;
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
	dfork = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	cfork = xfs_ifork_ptr(ip, XFS_COW_FORK);
	if (dfork->if_bytes == 0 && cfork->if_bytes == 0)
		ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;
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
xfs_itruncate_extents_flags(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fsize_t		new_size,
	int			flags)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp = *tpp;
	xfs_fileoff_t		first_unmap_block;
	int			error = 0;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);
	if (atomic_read(&VFS_I(ip)->i_count))
		xfs_assert_ilocked(ip, XFS_IOLOCK_EXCL);
	ASSERT(new_size <= XFS_ISIZE(ip));
	ASSERT(tp->t_flags & XFS_TRANS_PERM_LOG_RES);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ip->i_itemp->ili_lock_flags == 0);
	ASSERT(!XFS_NOT_DQATTACHED(mp, ip));

	trace_xfs_itruncate_extents_start(ip, new_size);

	flags |= xfs_bmapi_aflag(whichfork);

	/*
	 * Since it is possible for space to become allocated beyond
	 * the end of the file (in a crash where the space is allocated
	 * but the inode size is not yet updated), simply remove any
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
	 * Always re-log the inode so that our permanent transaction can keep
	 * on rolling it forward in the log.
	 */
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	trace_xfs_itruncate_extents_end(ip, new_size);

out:
	*tpp = tp;
	return error;
}

/*
 * Mark all the buffers attached to this directory stale.  In theory we should
 * never be freeing a directory with any blocks at all, but this covers the
 * case where we've recovered a directory swap with a "temporary" directory
 * created by online repair and now need to dump it.
 */
STATIC void
xfs_inactive_dir(
	struct xfs_inode	*dp)
{
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	got;
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_da_geometry	*geo = mp->m_dir_geo;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(dp, XFS_DATA_FORK);
	xfs_fileoff_t		off;

	/*
	 * Invalidate each directory block.  All directory blocks are of
	 * fsbcount length and alignment, so we only need to walk those same
	 * offsets.  We hold the only reference to this inode, so we must wait
	 * for the buffer locks.
	 */
	for_each_xfs_iext(ifp, &icur, &got) {
		for (off = round_up(got.br_startoff, geo->fsbcount);
		     off < got.br_startoff + got.br_blockcount;
		     off += geo->fsbcount) {
			struct xfs_buf	*bp = NULL;
			xfs_fsblock_t	fsbno;
			int		error;

			fsbno = (off - got.br_startoff) + got.br_startblock;
			error = xfs_buf_incore(mp->m_ddev_targp,
					XFS_FSB_TO_DADDR(mp, fsbno),
					XFS_FSB_TO_BB(mp, geo->fsbcount),
					XBF_LIVESCAN, &bp);
			if (error)
				continue;

			xfs_buf_stale(bp);
			xfs_buf_relse(bp);
		}
	}
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
		ASSERT(xfs_is_shutdown(mp));
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * Log the inode size first to prevent stale data exposure in the event
	 * of a system crash before the truncate completes. See the related
	 * comment in xfs_vn_setattr_size() for details.
	 */
	ip->i_disk_size = 0;
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

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
 * Perform the inode free when an inode is unlinked.
 */
STATIC int
xfs_inactive_ifree(
	struct xfs_inode *ip)
{
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
	if (unlikely(mp->m_finobt_nores)) {
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
			ASSERT(xfs_is_shutdown(mp));
		}
		return error;
	}

	/*
	 * We do not hold the inode locked across the entire rolling transaction
	 * here. We only need to hold it for the first transaction that
	 * xfs_ifree() builds, which may mark the inode XFS_ISTALE if the
	 * underlying cluster buffer is freed. Relogging an XFS_ISTALE inode
	 * here breaks the relationship between cluster buffer invalidation and
	 * stale inode invalidation on cluster buffer item journal commit
	 * completion, and can result in leaving dirty stale inodes hanging
	 * around in memory.
	 *
	 * We have no need for serialising this inode operation against other
	 * operations - we freed the inode and hence reallocation is required
	 * and that will serialise on reallocating the space the deferops need
	 * to free. Hence we can unlock the inode on the first commit of
	 * the transaction rather than roll it right through the deferops. This
	 * avoids relogging the XFS_ISTALE inode.
	 *
	 * We check that xfs_ifree() hasn't grown an internal transaction roll
	 * by asserting that the inode is still locked when it returns.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	error = xfs_ifree(tp, ip);
	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);
	if (error) {
		/*
		 * If we fail to free the inode, shut down.  The cancel
		 * might do that, we need to make sure.  Otherwise the
		 * inode might be lost for a long time or forever.
		 */
		if (!xfs_is_shutdown(mp)) {
			xfs_notice(mp, "%s: xfs_ifree returned error %d",
				__func__, error);
			xfs_force_shutdown(mp, SHUTDOWN_META_IO_ERROR);
		}
		xfs_trans_cancel(tp);
		return error;
	}

	/*
	 * Credit the quota account(s). The inode is gone.
	 */
	xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_ICOUNT, -1);

	return xfs_trans_commit(tp);
}

/*
 * Returns true if we need to update the on-disk metadata before we can free
 * the memory used by this inode.  Updates include freeing post-eof
 * preallocations; freeing COW staging extents; and marking the inode free in
 * the inobt if it is on the unlinked list.
 */
bool
xfs_inode_needs_inactive(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*cow_ifp = xfs_ifork_ptr(ip, XFS_COW_FORK);

	/*
	 * If the inode is already free, then there can be nothing
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

	/* If the log isn't running, push inodes straight to reclaim. */
	if (xfs_is_shutdown(mp) || xfs_has_norecovery(mp))
		return false;

	/* Metadata inodes require explicit resource cleanup. */
	if (xfs_is_internal_inode(ip))
		return false;

	/* Want to clean out the cow blocks if there are any. */
	if (cow_ifp && cow_ifp->if_bytes > 0)
		return true;

	/* Unlinked files must be freed. */
	if (VFS_I(ip)->i_nlink == 0)
		return true;

	/*
	 * This file isn't being freed, so check if there are post-eof blocks
	 * to free.
	 *
	 * Note: don't bother with iolock here since lockdep complains about
	 * acquiring it in reclaim context. We have the only reference to the
	 * inode at this point anyways.
	 */
	return xfs_can_free_eofblocks(ip);
}

/*
 * Save health status somewhere, if we're dumping an inode with uncorrected
 * errors and online repair isn't running.
 */
static inline void
xfs_inactive_health(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_perag	*pag;
	unsigned int		sick;
	unsigned int		checked;

	xfs_inode_measure_sickness(ip, &sick, &checked);
	if (!sick)
		return;

	trace_xfs_inode_unfixed_corruption(ip, sick);

	if (sick & XFS_SICK_INO_FORGET)
		return;

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));
	if (!pag) {
		/* There had better still be a perag structure! */
		ASSERT(0);
		return;
	}

	xfs_ag_mark_sick(pag, XFS_SICK_AG_INODES);
	xfs_perag_put(pag);
}

/*
 * xfs_inactive
 *
 * This is called when the vnode reference count for the vnode
 * goes to zero.  If the file has been unlinked, then it must
 * now be truncated.  Also, we clear all of the read-ahead state
 * kept for the inode here since the file is now closed.
 */
int
xfs_inactive(
	xfs_inode_t	*ip)
{
	struct xfs_mount	*mp;
	int			error = 0;
	int			truncate = 0;

	/*
	 * If the inode is already free, then there can be nothing
	 * to clean up here.
	 */
	if (VFS_I(ip)->i_mode == 0) {
		ASSERT(ip->i_df.if_broot_bytes == 0);
		goto out;
	}

	mp = ip->i_mount;
	ASSERT(!xfs_iflags_test(ip, XFS_IRECOVERY));

	xfs_inactive_health(ip);

	/*
	 * If this is a read-only mount, don't do this (would generate I/O)
	 * unless we're in log recovery and cleaning the iunlinked list.
	 */
	if (xfs_is_readonly(mp) && !xlog_recovery_needed(mp->m_log))
		goto out;

	/* Metadata inodes require explicit resource cleanup. */
	if (xfs_is_internal_inode(ip))
		goto out;

	/* Try to clean out the cow blocks if there are any. */
	if (xfs_inode_has_cow_data(ip)) {
		error = xfs_reflink_cancel_cow_range(ip, 0, NULLFILEOFF, true);
		if (error)
			goto out;
	}

	if (VFS_I(ip)->i_nlink != 0) {
		/*
		 * Note: don't bother with iolock here since lockdep complains
		 * about acquiring it in reclaim context. We have the only
		 * reference to the inode at this point anyways.
		 */
		if (xfs_can_free_eofblocks(ip))
			error = xfs_free_eofblocks(ip);

		goto out;
	}

	if (S_ISREG(VFS_I(ip)->i_mode) &&
	    (ip->i_disk_size != 0 || XFS_ISIZE(ip) != 0 ||
	     xfs_inode_has_filedata(ip)))
		truncate = 1;

	if (xfs_iflags_test(ip, XFS_IQUOTAUNCHECKED)) {
		/*
		 * If this inode is being inactivated during a quotacheck and
		 * has not yet been scanned by quotacheck, we /must/ remove
		 * the dquots from the inode before inactivation changes the
		 * block and inode counts.  Most probably this is a result of
		 * reloading the incore iunlinked list to purge unrecovered
		 * unlinked inodes.
		 */
		xfs_qm_dqdetach(ip);
	} else {
		error = xfs_qm_dqattach(ip);
		if (error)
			goto out;
	}

	if (S_ISDIR(VFS_I(ip)->i_mode) && ip->i_df.if_nextents > 0) {
		xfs_inactive_dir(ip);
		truncate = 1;
	}

	if (S_ISLNK(VFS_I(ip)->i_mode))
		error = xfs_inactive_symlink(ip);
	else if (truncate)
		error = xfs_inactive_truncate(ip);
	if (error)
		goto out;

	/*
	 * If there are attributes associated with the file then blow them away
	 * now.  The code calls a routine that recursively deconstructs the
	 * attribute fork. If also blows away the in-core attribute fork.
	 */
	if (xfs_inode_has_attr_fork(ip)) {
		error = xfs_attr_inactive(ip);
		if (error)
			goto out;
	}

	ASSERT(ip->i_forkoff == 0);

	/*
	 * Free the inode.
	 */
	error = xfs_inactive_ifree(ip);

out:
	/*
	 * We're done making metadata updates for this inode, so we can release
	 * the attached dquots.
	 */
	xfs_qm_dqdetach(ip);
	return error;
}

/*
 * Find an inode on the unlinked list. This does not take references to the
 * inode as we have existence guarantees by holding the AGI buffer lock and that
 * only unlinked, referenced inodes can be on the unlinked inode list.  If we
 * don't find the inode in cache, then let the caller handle the situation.
 */
struct xfs_inode *
xfs_iunlink_lookup(
	struct xfs_perag	*pag,
	xfs_agino_t		agino)
{
	struct xfs_inode	*ip;

	rcu_read_lock();
	ip = radix_tree_lookup(&pag->pag_ici_root, agino);
	if (!ip) {
		/* Caller can handle inode not being in memory. */
		rcu_read_unlock();
		return NULL;
	}

	/*
	 * Inode in RCU freeing limbo should not happen.  Warn about this and
	 * let the caller handle the failure.
	 */
	if (WARN_ON_ONCE(!ip->i_ino)) {
		rcu_read_unlock();
		return NULL;
	}
	ASSERT(!xfs_iflags_test(ip, XFS_IRECLAIMABLE | XFS_IRECLAIM));
	rcu_read_unlock();
	return ip;
}

/*
 * Load the inode @next_agino into the cache and set its prev_unlinked pointer
 * to @prev_agino.  Caller must hold the AGI to synchronize with other changes
 * to the unlinked list.
 */
int
xfs_iunlink_reload_next(
	struct xfs_trans	*tp,
	struct xfs_buf		*agibp,
	xfs_agino_t		prev_agino,
	xfs_agino_t		next_agino)
{
	struct xfs_perag	*pag = agibp->b_pag;
	struct xfs_mount	*mp = pag_mount(pag);
	struct xfs_inode	*next_ip = NULL;
	int			error;

	ASSERT(next_agino != NULLAGINO);

#ifdef DEBUG
	rcu_read_lock();
	next_ip = radix_tree_lookup(&pag->pag_ici_root, next_agino);
	ASSERT(next_ip == NULL);
	rcu_read_unlock();
#endif

	xfs_info_ratelimited(mp,
 "Found unrecovered unlinked inode 0x%x in AG 0x%x.  Initiating recovery.",
			next_agino, pag_agno(pag));

	/*
	 * Use an untrusted lookup just to be cautious in case the AGI has been
	 * corrupted and now points at a free inode.  That shouldn't happen,
	 * but we'd rather shut down now since we're already running in a weird
	 * situation.
	 */
	error = xfs_iget(mp, tp, xfs_agino_to_ino(pag, next_agino),
			XFS_IGET_UNTRUSTED, 0, &next_ip);
	if (error) {
		xfs_ag_mark_sick(pag, XFS_SICK_AG_AGI);
		return error;
	}

	/* If this is not an unlinked inode, something is very wrong. */
	if (VFS_I(next_ip)->i_nlink != 0) {
		xfs_ag_mark_sick(pag, XFS_SICK_AG_AGI);
		error = -EFSCORRUPTED;
		goto rele;
	}

	next_ip->i_prev_unlinked = prev_agino;
	trace_xfs_iunlink_reload_next(next_ip);
rele:
	ASSERT(!(VFS_I(next_ip)->i_state & I_DONTCACHE));
	if (xfs_is_quotacheck_running(mp) && next_ip)
		xfs_iflags_set(next_ip, XFS_IQUOTAUNCHECKED);
	xfs_irele(next_ip);
	return error;
}

/*
 * Look up the inode number specified and if it is not already marked XFS_ISTALE
 * mark it stale. We should only find clean inodes in this lookup that aren't
 * already stale.
 */
static void
xfs_ifree_mark_inode_stale(
	struct xfs_perag	*pag,
	struct xfs_inode	*free_ip,
	xfs_ino_t		inum)
{
	struct xfs_mount	*mp = pag_mount(pag);
	struct xfs_inode_log_item *iip;
	struct xfs_inode	*ip;

retry:
	rcu_read_lock();
	ip = radix_tree_lookup(&pag->pag_ici_root, XFS_INO_TO_AGINO(mp, inum));

	/* Inode not in memory, nothing to do */
	if (!ip) {
		rcu_read_unlock();
		return;
	}

	/*
	 * because this is an RCU protected lookup, we could find a recently
	 * freed or even reallocated inode during the lookup. We need to check
	 * under the i_flags_lock for a valid inode here. Skip it if it is not
	 * valid, the wrong inode or stale.
	 */
	spin_lock(&ip->i_flags_lock);
	if (ip->i_ino != inum || __xfs_iflags_test(ip, XFS_ISTALE))
		goto out_iflags_unlock;

	/*
	 * Don't try to lock/unlock the current inode, but we _cannot_ skip the
	 * other inodes that we did not find in the list attached to the buffer
	 * and are not already marked stale. If we can't lock it, back off and
	 * retry.
	 */
	if (ip != free_ip) {
		if (!xfs_ilock_nowait(ip, XFS_ILOCK_EXCL)) {
			spin_unlock(&ip->i_flags_lock);
			rcu_read_unlock();
			delay(1);
			goto retry;
		}
	}
	ip->i_flags |= XFS_ISTALE;

	/*
	 * If the inode is flushing, it is already attached to the buffer.  All
	 * we needed to do here is mark the inode stale so buffer IO completion
	 * will remove it from the AIL.
	 */
	iip = ip->i_itemp;
	if (__xfs_iflags_test(ip, XFS_IFLUSHING)) {
		ASSERT(!list_empty(&iip->ili_item.li_bio_list));
		ASSERT(iip->ili_last_fields);
		goto out_iunlock;
	}

	/*
	 * Inodes not attached to the buffer can be released immediately.
	 * Everything else has to go through xfs_iflush_abort() on journal
	 * commit as the flock synchronises removal of the inode from the
	 * cluster buffer against inode reclaim.
	 */
	if (!iip || list_empty(&iip->ili_item.li_bio_list))
		goto out_iunlock;

	__xfs_iflags_set(ip, XFS_IFLUSHING);
	spin_unlock(&ip->i_flags_lock);
	rcu_read_unlock();

	/* we have a dirty inode in memory that has not yet been flushed. */
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
 * A big issue when freeing the inode cluster is that we _cannot_ skip any
 * inodes that are in memory - they all must be marked stale and attached to
 * the cluster buffer.
 */
static int
xfs_ifree_cluster(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	struct xfs_inode	*free_ip,
	struct xfs_icluster	*xic)
{
	struct xfs_mount	*mp = free_ip->i_mount;
	struct xfs_ino_geometry	*igeo = M_IGEO(mp);
	struct xfs_buf		*bp;
	xfs_daddr_t		blkno;
	xfs_ino_t		inum = xic->first_ino;
	int			nbufs;
	int			i, j;
	int			ioffset;
	int			error;

	nbufs = igeo->ialloc_blks / igeo->blocks_per_cluster;

	for (j = 0; j < nbufs; j++, inum += igeo->inodes_per_cluster) {
		/*
		 * The allocation bitmap tells us which inodes of the chunk were
		 * physically allocated. Skip the cluster if an inode falls into
		 * a sparse region.
		 */
		ioffset = inum - xic->first_ino;
		if ((xic->alloc & XFS_INOBT_MASK(ioffset)) == 0) {
			ASSERT(ioffset % igeo->inodes_per_cluster == 0);
			continue;
		}

		blkno = XFS_AGB_TO_DADDR(mp, XFS_INO_TO_AGNO(mp, inum),
					 XFS_INO_TO_AGBNO(mp, inum));

		/*
		 * We obtain and lock the backing buffer first in the process
		 * here to ensure dirty inodes attached to the buffer remain in
		 * the flushing state while we mark them stale.
		 *
		 * If we scan the in-memory inodes first, then buffer IO can
		 * complete before we get a lock on it, and hence we may fail
		 * to mark all the active inodes on the buffer stale.
		 */
		error = xfs_trans_get_buf(tp, mp->m_ddev_targp, blkno,
				mp->m_bsize * igeo->blocks_per_cluster,
				XBF_UNMAPPED, &bp);
		if (error)
			return error;

		/*
		 * This buffer may not have been correctly initialised as we
		 * didn't read it from disk. That's not important because we are
		 * only using to mark the buffer as stale in the log, and to
		 * attach stale cached inodes on it.
		 *
		 * For the inode that triggered the cluster freeing, this
		 * attachment may occur in xfs_inode_item_precommit() after we
		 * have marked this buffer stale.  If this buffer was not in
		 * memory before xfs_ifree_cluster() started, it will not be
		 * marked XBF_DONE and this will cause problems later in
		 * xfs_inode_item_precommit() when we trip over a (stale, !done)
		 * buffer to attached to the transaction.
		 *
		 * Hence we have to mark the buffer as XFS_DONE here. This is
		 * safe because we are also marking the buffer as XBF_STALE and
		 * XFS_BLI_STALE. That means it will never be dispatched for
		 * IO and it won't be unlocked until the cluster freeing has
		 * been committed to the journal and the buffer unpinned. If it
		 * is written, we want to know about it, and we want it to
		 * fail. We can acheive this by adding a write verifier to the
		 * buffer.
		 */
		bp->b_flags |= XBF_DONE;
		bp->b_ops = &xfs_inode_buf_ops;

		/*
		 * Now we need to set all the cached clean inodes as XFS_ISTALE,
		 * too. This requires lookups, and will skip inodes that we've
		 * already marked XFS_ISTALE.
		 */
		for (i = 0; i < igeo->inodes_per_cluster; i++)
			xfs_ifree_mark_inode_stale(pag, free_ip, inum + i);

		xfs_trans_stale_inode_buf(tp, bp);
		xfs_trans_binval(tp, bp);
	}
	return 0;
}

/*
 * This is called to return an inode to the inode free list.  The inode should
 * already be truncated to 0 length and have no pages associated with it.  This
 * routine also assumes that the inode is already a part of the transaction.
 *
 * The on-disk copy of the inode will have been added to the list of unlinked
 * inodes in the AGI. We need to remove the inode from that list atomically with
 * respect to freeing it here.
 */
int
xfs_ifree(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_perag	*pag;
	struct xfs_icluster	xic = { 0 };
	struct xfs_inode_log_item *iip = ip->i_itemp;
	int			error;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);
	ASSERT(VFS_I(ip)->i_nlink == 0);
	ASSERT(ip->i_df.if_nextents == 0);
	ASSERT(ip->i_disk_size == 0 || !S_ISREG(VFS_I(ip)->i_mode));
	ASSERT(ip->i_nblocks == 0);

	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ip->i_ino));

	error = xfs_inode_uninit(tp, pag, ip, &xic);
	if (error)
		goto out;

	if (xfs_iflags_test(ip, XFS_IPRESERVE_DM_FIELDS))
		xfs_iflags_clear(ip, XFS_IPRESERVE_DM_FIELDS);

	/* Don't attempt to replay owner changes for a deleted inode */
	spin_lock(&iip->ili_lock);
	iip->ili_fields &= ~(XFS_ILOG_AOWNER | XFS_ILOG_DOWNER);
	spin_unlock(&iip->ili_lock);

	if (xic.deleted)
		error = xfs_ifree_cluster(tp, pag, ip, &xic);
out:
	xfs_perag_put(pag);
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
	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL | XFS_ILOCK_SHARED);

	trace_xfs_inode_unpin_nowait(ip, _RET_IP_);

	/* Give the log a push to start the unpinning I/O */
	xfs_log_force_seq(ip->i_mount, ip->i_itemp->ili_commit_seq, 0, NULL);

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
	struct xfs_inode	*dp,
	struct xfs_name		*name,
	struct xfs_inode	*ip)
{
	struct xfs_dir_update	du = {
		.dp		= dp,
		.name		= name,
		.ip		= ip,
	};
	struct xfs_mount	*mp = dp->i_mount;
	struct xfs_trans	*tp = NULL;
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

	error = xfs_parent_start(mp, &du.ppargs);
	if (error)
		goto std_return;

	/*
	 * We try to get the real space reservation first, allowing for
	 * directory btree deletion(s) implying possible bmap insert(s).  If we
	 * can't get the space reservation then we use 0 instead, and avoid the
	 * bmap btree insert(s) in the directory code by, if the bmap insert
	 * tries to happen, instead trimming the LAST block from the directory.
	 *
	 * Ignore EDQUOT and ENOSPC being returned via nospace_error because
	 * the directory code can handle a reservationless update and we don't
	 * want to prevent a user from trying to free space by deleting things.
	 */
	resblks = xfs_remove_space_res(mp, name->len);
	error = xfs_trans_alloc_dir(dp, &M_RES(mp)->tr_remove, ip, &resblks,
			&tp, &dontcare);
	if (error) {
		ASSERT(error != -ENOSPC);
		goto out_parent;
	}

	error = xfs_dir_remove_child(tp, resblks, &du);
	if (error)
		goto out_trans_cancel;

	/*
	 * If this is a synchronous mount, make sure that the
	 * remove transaction goes to disk before returning to
	 * the user.
	 */
	if (xfs_has_wsync(mp) || xfs_has_dirsync(mp))
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp);
	if (error)
		goto out_unlock;

	if (is_dir && xfs_inode_is_filestream(ip))
		xfs_filestream_deassociate(ip);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
	xfs_parent_finish(mp, du.ppargs);
	return 0;

 out_trans_cancel:
	xfs_trans_cancel(tp);
 out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	xfs_iunlock(dp, XFS_ILOCK_EXCL);
 out_parent:
	xfs_parent_finish(mp, du.ppargs);
 std_return:
	return error;
}

static inline void
xfs_iunlock_rename(
	struct xfs_inode	**i_tab,
	int			num_inodes)
{
	int			i;

	for (i = num_inodes - 1; i >= 0; i--) {
		/* Skip duplicate inodes if src and target dps are the same */
		if (!i_tab[i] || (i > 0 && i_tab[i] == i_tab[i - 1]))
			continue;
		xfs_iunlock(i_tab[i], XFS_ILOCK_EXCL);
	}
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
	int			i;

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

	xfs_sort_inodes(i_tab, *num_inodes);
}

void
xfs_sort_inodes(
	struct xfs_inode	**i_tab,
	unsigned int		num_inodes)
{
	int			i, j;

	ASSERT(num_inodes <= __XFS_SORT_INODES);

	/*
	 * Sort the elements via bubble sort.  (Remember, there are at
	 * most 5 elements to sort, so this is adequate.)
	 */
	for (i = 0; i < num_inodes; i++) {
		for (j = 1; j < num_inodes; j++) {
			if (i_tab[j]->i_ino < i_tab[j-1]->i_ino)
				swap(i_tab[j], i_tab[j - 1]);
		}
	}
}

/*
 * xfs_rename_alloc_whiteout()
 *
 * Return a referenced, unlinked, unlocked inode that can be used as a
 * whiteout in a rename transaction. We use a tmpfile inode here so that if we
 * crash between allocating the inode and linking it into the rename transaction
 * recovery will free the inode and we won't leak it.
 */
static int
xfs_rename_alloc_whiteout(
	struct mnt_idmap	*idmap,
	struct xfs_name		*src_name,
	struct xfs_inode	*dp,
	struct xfs_inode	**wip)
{
	struct xfs_icreate_args	args = {
		.idmap		= idmap,
		.pip		= dp,
		.mode		= S_IFCHR | WHITEOUT_MODE,
		.flags		= XFS_ICREATE_TMPFILE,
	};
	struct xfs_inode	*tmpfile;
	struct qstr		name;
	int			error;

	error = xfs_create_tmpfile(&args, &tmpfile);
	if (error)
		return error;

	name.name = src_name->name;
	name.len = src_name->len;
	error = xfs_inode_init_security(VFS_I(tmpfile), VFS_I(dp), &name);
	if (error) {
		xfs_finish_inode_setup(tmpfile);
		xfs_irele(tmpfile);
		return error;
	}

	/*
	 * Prepare the tmpfile inode as if it were created through the VFS.
	 * Complete the inode setup and flag it as linkable.  nlink is already
	 * zero, so we can skip the drop_nlink.
	 */
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
	struct mnt_idmap	*idmap,
	struct xfs_inode	*src_dp,
	struct xfs_name		*src_name,
	struct xfs_inode	*src_ip,
	struct xfs_inode	*target_dp,
	struct xfs_name		*target_name,
	struct xfs_inode	*target_ip,
	unsigned int		flags)
{
	struct xfs_dir_update	du_src = {
		.dp		= src_dp,
		.name		= src_name,
		.ip		= src_ip,
	};
	struct xfs_dir_update	du_tgt = {
		.dp		= target_dp,
		.name		= target_name,
		.ip		= target_ip,
	};
	struct xfs_dir_update	du_wip = { };
	struct xfs_mount	*mp = src_dp->i_mount;
	struct xfs_trans	*tp;
	struct xfs_inode	*inodes[__XFS_SORT_INODES];
	int			i;
	int			num_inodes = __XFS_SORT_INODES;
	bool			new_parent = (src_dp != target_dp);
	bool			src_is_directory = S_ISDIR(VFS_I(src_ip)->i_mode);
	int			spaceres;
	bool			retried = false;
	int			error, nospace_error = 0;

	trace_xfs_rename(src_dp, target_dp, src_name, target_name);

	if ((flags & RENAME_EXCHANGE) && !target_ip)
		return -EINVAL;

	/*
	 * If we are doing a whiteout operation, allocate the whiteout inode
	 * we will be placing at the target and ensure the type is set
	 * appropriately.
	 */
	if (flags & RENAME_WHITEOUT) {
		error = xfs_rename_alloc_whiteout(idmap, src_name, target_dp,
				&du_wip.ip);
		if (error)
			return error;

		/* setup target dirent info as whiteout */
		src_name->type = XFS_DIR3_FT_CHRDEV;
	}

	xfs_sort_for_rename(src_dp, target_dp, src_ip, target_ip, du_wip.ip,
			inodes, &num_inodes);

	error = xfs_parent_start(mp, &du_src.ppargs);
	if (error)
		goto out_release_wip;

	if (du_wip.ip) {
		error = xfs_parent_start(mp, &du_wip.ppargs);
		if (error)
			goto out_src_ppargs;
	}

	if (target_ip) {
		error = xfs_parent_start(mp, &du_tgt.ppargs);
		if (error)
			goto out_wip_ppargs;
	}

retry:
	nospace_error = 0;
	spaceres = xfs_rename_space_res(mp, src_name->len, target_ip != NULL,
			target_name->len, du_wip.ip != NULL);
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_rename, spaceres, 0, 0, &tp);
	if (error == -ENOSPC) {
		nospace_error = error;
		spaceres = 0;
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_rename, 0, 0, 0,
				&tp);
	}
	if (error)
		goto out_tgt_ppargs;

	/*
	 * We don't allow reservationless renaming when parent pointers are
	 * enabled because we can't back out if the xattrs must grow.
	 */
	if (du_src.ppargs && nospace_error) {
		error = nospace_error;
		xfs_trans_cancel(tp);
		goto out_tgt_ppargs;
	}

	/*
	 * Attach the dquots to the inodes
	 */
	error = xfs_qm_vop_rename_dqattach(inodes);
	if (error) {
		xfs_trans_cancel(tp);
		goto out_tgt_ppargs;
	}

	/*
	 * Lock all the participating inodes. Depending upon whether
	 * the target_name exists in the target directory, and
	 * whether the target directory is the same as the source
	 * directory, we can lock from 2 to 5 inodes.
	 */
	xfs_lock_inodes(inodes, num_inodes, XFS_ILOCK_EXCL);

	/*
	 * Join all the inodes to the transaction.
	 */
	xfs_trans_ijoin(tp, src_dp, 0);
	if (new_parent)
		xfs_trans_ijoin(tp, target_dp, 0);
	xfs_trans_ijoin(tp, src_ip, 0);
	if (target_ip)
		xfs_trans_ijoin(tp, target_ip, 0);
	if (du_wip.ip)
		xfs_trans_ijoin(tp, du_wip.ip, 0);

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
	if (flags & RENAME_EXCHANGE) {
		error = xfs_dir_exchange_children(tp, &du_src, &du_tgt,
				spaceres);
		if (error)
			goto out_trans_cancel;
		goto out_commit;
	}

	/*
	 * Try to reserve quota to handle an expansion of the target directory.
	 * We'll allow the rename to continue in reservationless mode if we hit
	 * a space usage constraint.  If we trigger reservationless mode, save
	 * the errno if there isn't any free space in the target directory.
	 */
	if (spaceres != 0) {
		error = xfs_trans_reserve_quota_nblks(tp, target_dp, spaceres,
				0, false);
		if (error == -EDQUOT || error == -ENOSPC) {
			if (!retried) {
				xfs_trans_cancel(tp);
				xfs_iunlock_rename(inodes, num_inodes);
				xfs_blockgc_free_quota(target_dp, 0);
				retried = true;
				goto retry;
			}

			nospace_error = error;
			spaceres = 0;
			error = 0;
		}
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * We don't allow quotaless renaming when parent pointers are enabled
	 * because we can't back out if the xattrs must grow.
	 */
	if (du_src.ppargs && nospace_error) {
		error = nospace_error;
		goto out_trans_cancel;
	}

	/*
	 * Lock the AGI buffers we need to handle bumping the nlink of the
	 * whiteout inode off the unlinked list and to handle dropping the
	 * nlink of the target inode.  Per locking order rules, do this in
	 * increasing AG order and before directory block allocation tries to
	 * grab AGFs because we grab AGIs before AGFs.
	 *
	 * The (vfs) caller must ensure that if src is a directory then
	 * target_ip is either null or an empty directory.
	 */
	for (i = 0; i < num_inodes && inodes[i] != NULL; i++) {
		if (inodes[i] == du_wip.ip ||
		    (inodes[i] == target_ip &&
		     (VFS_I(target_ip)->i_nlink == 1 || src_is_directory))) {
			struct xfs_perag	*pag;
			struct xfs_buf		*bp;

			pag = xfs_perag_get(mp,
					XFS_INO_TO_AGNO(mp, inodes[i]->i_ino));
			error = xfs_read_agi(pag, tp, 0, &bp);
			xfs_perag_put(pag);
			if (error)
				goto out_trans_cancel;
		}
	}

	error = xfs_dir_rename_children(tp, &du_src, &du_tgt, spaceres,
			&du_wip);
	if (error)
		goto out_trans_cancel;

	if (du_wip.ip) {
		/*
		 * Now we have a real link, clear the "I'm a tmpfile" state
		 * flag from the inode so it doesn't accidentally get misused in
		 * future.
		 */
		VFS_I(du_wip.ip)->i_state &= ~I_LINKABLE;
	}

out_commit:
	/*
	 * If this is a synchronous mount, make sure that the rename
	 * transaction goes to disk before returning to the user.
	 */
	if (xfs_has_wsync(tp->t_mountp) || xfs_has_dirsync(tp->t_mountp))
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp);
	nospace_error = 0;
	goto out_unlock;

out_trans_cancel:
	xfs_trans_cancel(tp);
out_unlock:
	xfs_iunlock_rename(inodes, num_inodes);
out_tgt_ppargs:
	xfs_parent_finish(mp, du_tgt.ppargs);
out_wip_ppargs:
	xfs_parent_finish(mp, du_wip.ppargs);
out_src_ppargs:
	xfs_parent_finish(mp, du_src.ppargs);
out_release_wip:
	if (du_wip.ip)
		xfs_irele(du_wip.ip);
	if (error == -ENOSPC && nospace_error)
		error = nospace_error;
	return error;
}

static int
xfs_iflush(
	struct xfs_inode	*ip,
	struct xfs_buf		*bp)
{
	struct xfs_inode_log_item *iip = ip->i_itemp;
	struct xfs_dinode	*dip;
	struct xfs_mount	*mp = ip->i_mount;
	int			error;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL | XFS_ILOCK_SHARED);
	ASSERT(xfs_iflags_test(ip, XFS_IFLUSHING));
	ASSERT(ip->i_df.if_format != XFS_DINODE_FMT_BTREE ||
	       ip->i_df.if_nextents > XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK));
	ASSERT(iip->ili_item.li_buf == bp);

	dip = xfs_buf_offset(bp, ip->i_imap.im_boffset);

	/*
	 * We don't flush the inode if any of the following checks fail, but we
	 * do still update the log item and attach to the backing buffer as if
	 * the flush happened. This is a formality to facilitate predictable
	 * error handling as the caller will shutdown and fail the buffer.
	 */
	error = -EFSCORRUPTED;
	if (XFS_TEST_ERROR(dip->di_magic != cpu_to_be16(XFS_DINODE_MAGIC),
			       mp, XFS_ERRTAG_IFLUSH_1)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: Bad inode %llu magic number 0x%x, ptr "PTR_FMT,
			__func__, ip->i_ino, be16_to_cpu(dip->di_magic), dip);
		goto flush_out;
	}
	if (ip->i_df.if_format == XFS_DINODE_FMT_META_BTREE) {
		if (!S_ISREG(VFS_I(ip)->i_mode) ||
		    !(ip->i_diflags2 & XFS_DIFLAG2_METADATA)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad %s meta btree inode %Lu, ptr "PTR_FMT,
				__func__, xfs_metafile_type_str(ip->i_metatype),
				ip->i_ino, ip);
			goto flush_out;
		}
	} else if (S_ISREG(VFS_I(ip)->i_mode)) {
		if (XFS_TEST_ERROR(
		    ip->i_df.if_format != XFS_DINODE_FMT_EXTENTS &&
		    ip->i_df.if_format != XFS_DINODE_FMT_BTREE,
		    mp, XFS_ERRTAG_IFLUSH_3)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad regular inode %llu, ptr "PTR_FMT,
				__func__, ip->i_ino, ip);
			goto flush_out;
		}
	} else if (S_ISDIR(VFS_I(ip)->i_mode)) {
		if (XFS_TEST_ERROR(
		    ip->i_df.if_format != XFS_DINODE_FMT_EXTENTS &&
		    ip->i_df.if_format != XFS_DINODE_FMT_BTREE &&
		    ip->i_df.if_format != XFS_DINODE_FMT_LOCAL,
		    mp, XFS_ERRTAG_IFLUSH_4)) {
			xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
				"%s: Bad directory inode %llu, ptr "PTR_FMT,
				__func__, ip->i_ino, ip);
			goto flush_out;
		}
	}
	if (XFS_TEST_ERROR(ip->i_df.if_nextents + xfs_ifork_nextents(&ip->i_af) >
				ip->i_nblocks, mp, XFS_ERRTAG_IFLUSH_5)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: detected corrupt incore inode %llu, "
			"total extents = %llu nblocks = %lld, ptr "PTR_FMT,
			__func__, ip->i_ino,
			ip->i_df.if_nextents + xfs_ifork_nextents(&ip->i_af),
			ip->i_nblocks, ip);
		goto flush_out;
	}
	if (XFS_TEST_ERROR(ip->i_forkoff > mp->m_sb.sb_inodesize,
				mp, XFS_ERRTAG_IFLUSH_6)) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: bad inode %llu, forkoff 0x%x, ptr "PTR_FMT,
			__func__, ip->i_ino, ip->i_forkoff, ip);
		goto flush_out;
	}

	if (xfs_inode_has_attr_fork(ip) &&
	    ip->i_af.if_format == XFS_DINODE_FMT_META_BTREE) {
		xfs_alert_tag(mp, XFS_PTAG_IFLUSH,
			"%s: meta btree in inode %Lu attr fork, ptr "PTR_FMT,
			__func__, ip->i_ino, ip);
		goto flush_out;
	}

	/*
	 * Inode item log recovery for v2 inodes are dependent on the flushiter
	 * count for correct sequencing.  We bump the flush iteration count so
	 * we can detect flushes which postdate a log record during recovery.
	 * This is redundant as we now log every change and hence this can't
	 * happen but we need to still do it to ensure backwards compatibility
	 * with old kernels that predate logging all inode changes.
	 */
	if (!xfs_has_v3inodes(mp))
		ip->i_flushiter++;

	/*
	 * If there are inline format data / attr forks attached to this inode,
	 * make sure they are not corrupt.
	 */
	if (ip->i_df.if_format == XFS_DINODE_FMT_LOCAL &&
	    xfs_ifork_verify_local_data(ip))
		goto flush_out;
	if (xfs_inode_has_attr_fork(ip) &&
	    ip->i_af.if_format == XFS_DINODE_FMT_LOCAL &&
	    xfs_ifork_verify_local_attr(ip))
		goto flush_out;

	/*
	 * Copy the dirty parts of the inode into the on-disk inode.  We always
	 * copy out the core of the inode, because if the inode is dirty at all
	 * the core must be.
	 */
	xfs_inode_to_disk(ip, dip, iip->ili_item.li_lsn);

	/* Wrap, we never let the log put out DI_MAX_FLUSH */
	if (!xfs_has_v3inodes(mp)) {
		if (ip->i_flushiter == DI_MAX_FLUSH)
			ip->i_flushiter = 0;
	}

	xfs_iflush_fork(ip, dip, iip, XFS_DATA_FORK);
	if (xfs_inode_has_attr_fork(ip))
		xfs_iflush_fork(ip, dip, iip, XFS_ATTR_FORK);

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
	 * In the xfs_buf_inode_iodone() routine we clear ili_last_fields, since
	 * we know that the information those bits represent is permanently on
	 * disk.  As long as the flush completes before the inode is logged
	 * again, then both ili_fields and ili_last_fields will be cleared.
	 */
	error = 0;
flush_out:
	spin_lock(&iip->ili_lock);
	iip->ili_last_fields = iip->ili_fields;
	iip->ili_fields = 0;
	iip->ili_fsync_fields = 0;
	set_bit(XFS_LI_FLUSHING, &iip->ili_item.li_flags);
	spin_unlock(&iip->ili_lock);

	/*
	 * Store the current LSN of the inode so that we can tell whether the
	 * item has moved in the AIL from xfs_buf_inode_iodone().
	 */
	xfs_trans_ail_copy_lsn(mp->m_ail, &iip->ili_flush_lsn,
				&iip->ili_item.li_lsn);

	/* generate the checksum. */
	xfs_dinode_calc_crc(mp, dip);
	if (error)
		xfs_inode_mark_sick(ip, XFS_SICK_INO_CORE);
	return error;
}

/*
 * Non-blocking flush of dirty inode metadata into the backing buffer.
 *
 * The caller must have a reference to the inode and hold the cluster buffer
 * locked. The function will walk across all the inodes on the cluster buffer it
 * can find and lock without blocking, and flush them to the cluster buffer.
 *
 * On successful flushing of at least one inode, the caller must write out the
 * buffer and release it. If no inodes are flushed, -EAGAIN will be returned and
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
	struct xfs_inode	*ip;
	struct xfs_inode_log_item *iip;
	int			clcount = 0;
	int			error = 0;

	/*
	 * We must use the safe variant here as on shutdown xfs_iflush_abort()
	 * will remove itself from the list.
	 */
	list_for_each_entry_safe(lip, n, &bp->b_li_list, li_bio_list) {
		iip = (struct xfs_inode_log_item *)lip;
		ip = iip->ili_inode;

		/*
		 * Quick and dirty check to avoid locks if possible.
		 */
		if (__xfs_iflags_test(ip, XFS_IRECLAIM | XFS_IFLUSHING))
			continue;
		if (xfs_ipincount(ip))
			continue;

		/*
		 * The inode is still attached to the buffer, which means it is
		 * dirty but reclaim might try to grab it. Check carefully for
		 * that, and grab the ilock while still holding the i_flags_lock
		 * to guarantee reclaim will not be able to reclaim this inode
		 * once we drop the i_flags_lock.
		 */
		spin_lock(&ip->i_flags_lock);
		ASSERT(!__xfs_iflags_test(ip, XFS_ISTALE));
		if (__xfs_iflags_test(ip, XFS_IRECLAIM | XFS_IFLUSHING)) {
			spin_unlock(&ip->i_flags_lock);
			continue;
		}

		/*
		 * ILOCK will pin the inode against reclaim and prevent
		 * concurrent transactions modifying the inode while we are
		 * flushing the inode. If we get the lock, set the flushing
		 * state before we drop the i_flags_lock.
		 */
		if (!xfs_ilock_nowait(ip, XFS_ILOCK_SHARED)) {
			spin_unlock(&ip->i_flags_lock);
			continue;
		}
		__xfs_iflags_set(ip, XFS_IFLUSHING);
		spin_unlock(&ip->i_flags_lock);

		/*
		 * Abort flushing this inode if we are shut down because the
		 * inode may not currently be in the AIL. This can occur when
		 * log I/O failure unpins the inode without inserting into the
		 * AIL, leaving a dirty/unpinned inode attached to the buffer
		 * that otherwise looks like it should be flushed.
		 */
		if (xlog_is_shutdown(mp->m_log)) {
			xfs_iunpin_wait(ip);
			xfs_iflush_abort(ip);
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			error = -EIO;
			continue;
		}

		/* don't block waiting on a log force to unpin dirty inodes */
		if (xfs_ipincount(ip)) {
			xfs_iflags_clear(ip, XFS_IFLUSHING);
			xfs_iunlock(ip, XFS_ILOCK_SHARED);
			continue;
		}

		if (!xfs_inode_clean(ip))
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
		 * buffer. If it is an INODE_ALLOC buffer and pins the tail
		 * of the log, failing it before the _log_ is shut down can
		 * result in the log tail being moved forward in the journal
		 * on disk because log writes can still be taking place. Hence
		 * unpinning the tail will allow the ICREATE intent to be
		 * removed from the log an recovery will fail with uninitialised
		 * inode cluster buffers.
		 */
		xfs_force_shutdown(mp, SHUTDOWN_CORRUPT_INCORE);
		bp->b_flags |= XBF_ASYNC;
		xfs_buf_ioend_fail(bp);
		return error;
	}

	if (!clcount)
		return -EAGAIN;

	XFS_STATS_INC(mp, xs_icluster_flushcnt);
	XFS_STATS_ADD(mp, xs_icluster_flushinode, clcount);
	return 0;

}

/* Release an inode. */
void
xfs_irele(
	struct xfs_inode	*ip)
{
	trace_xfs_irele(ip, _RET_IP_);
	iput(VFS_I(ip));
}

/*
 * Ensure all commited transactions touching the inode are written to the log.
 */
int
xfs_log_force_inode(
	struct xfs_inode	*ip)
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
 * layout leases before proceeding.  The loop is needed because we cannot call
 * the blocking break_layout() with the iolocks held, and therefore have to
 * back out both locks.
 */
static int
xfs_iolock_two_inodes_and_break_layout(
	struct inode		*src,
	struct inode		*dest)
{
	int			error;

	if (src > dest)
		swap(src, dest);

retry:
	/* Wait to break both inodes' layouts before we start locking. */
	error = break_layout(src, true);
	if (error)
		return error;
	if (src != dest) {
		error = break_layout(dest, true);
		if (error)
			return error;
	}

	/* Lock one inode and make sure nobody got in and leased it. */
	inode_lock(src);
	error = break_layout(src, false);
	if (error) {
		inode_unlock(src);
		if (error == -EWOULDBLOCK)
			goto retry;
		return error;
	}

	if (src == dest)
		return 0;

	/* Lock the other inode and make sure nobody got in and leased it. */
	inode_lock_nested(dest, I_MUTEX_NONDIR2);
	error = break_layout(dest, false);
	if (error) {
		inode_unlock(src);
		inode_unlock(dest);
		if (error == -EWOULDBLOCK)
			goto retry;
		return error;
	}

	return 0;
}

static int
xfs_mmaplock_two_inodes_and_break_dax_layout(
	struct xfs_inode	*ip1,
	struct xfs_inode	*ip2)
{
	int			error;
	struct page		*page;

	if (ip1->i_ino > ip2->i_ino)
		swap(ip1, ip2);

again:
	/* Lock the first inode */
	xfs_ilock(ip1, XFS_MMAPLOCK_EXCL);
	error = xfs_break_dax_layouts(VFS_I(ip1));
	if (error) {
		xfs_iunlock(ip1, XFS_MMAPLOCK_EXCL);
		return error;
	}

	if (ip1 == ip2)
		return 0;

	/* Nested lock the second inode */
	xfs_ilock(ip2, xfs_lock_inumorder(XFS_MMAPLOCK_EXCL, 1));
	/*
	 * We cannot use xfs_break_dax_layouts() directly here because it may
	 * need to unlock & lock the XFS_MMAPLOCK_EXCL which is not suitable
	 * for this nested lock case.
	 */
	page = dax_layout_busy_page(VFS_I(ip2)->i_mapping);
	if (!dax_page_is_idle(page)) {
		xfs_iunlock(ip2, XFS_MMAPLOCK_EXCL);
		xfs_iunlock(ip1, XFS_MMAPLOCK_EXCL);
		goto again;
	}

	return 0;
}

/*
 * Lock two inodes so that userspace cannot initiate I/O via file syscalls or
 * mmap activity.
 */
int
xfs_ilock2_io_mmap(
	struct xfs_inode	*ip1,
	struct xfs_inode	*ip2)
{
	int			ret;

	ret = xfs_iolock_two_inodes_and_break_layout(VFS_I(ip1), VFS_I(ip2));
	if (ret)
		return ret;

	if (IS_DAX(VFS_I(ip1)) && IS_DAX(VFS_I(ip2))) {
		ret = xfs_mmaplock_two_inodes_and_break_dax_layout(ip1, ip2);
		if (ret) {
			inode_unlock(VFS_I(ip2));
			if (ip1 != ip2)
				inode_unlock(VFS_I(ip1));
			return ret;
		}
	} else
		filemap_invalidate_lock_two(VFS_I(ip1)->i_mapping,
					    VFS_I(ip2)->i_mapping);

	return 0;
}

/* Unlock both inodes to allow IO and mmap activity. */
void
xfs_iunlock2_io_mmap(
	struct xfs_inode	*ip1,
	struct xfs_inode	*ip2)
{
	if (IS_DAX(VFS_I(ip1)) && IS_DAX(VFS_I(ip2))) {
		xfs_iunlock(ip2, XFS_MMAPLOCK_EXCL);
		if (ip1 != ip2)
			xfs_iunlock(ip1, XFS_MMAPLOCK_EXCL);
	} else
		filemap_invalidate_unlock_two(VFS_I(ip1)->i_mapping,
					      VFS_I(ip2)->i_mapping);

	inode_unlock(VFS_I(ip2));
	if (ip1 != ip2)
		inode_unlock(VFS_I(ip1));
}

/* Drop the MMAPLOCK and the IOLOCK after a remap completes. */
void
xfs_iunlock2_remapping(
	struct xfs_inode	*ip1,
	struct xfs_inode	*ip2)
{
	xfs_iflags_clear(ip1, XFS_IREMAPPING);

	if (ip1 != ip2)
		xfs_iunlock(ip1, XFS_MMAPLOCK_SHARED);
	xfs_iunlock(ip2, XFS_MMAPLOCK_EXCL);

	if (ip1 != ip2)
		inode_unlock_shared(VFS_I(ip1));
	inode_unlock(VFS_I(ip2));
}

/*
 * Reload the incore inode list for this inode.  Caller should ensure that
 * the link count cannot change, either by taking ILOCK_SHARED or otherwise
 * preventing other threads from executing.
 */
int
xfs_inode_reload_unlinked_bucket(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_buf		*agibp;
	struct xfs_agi		*agi;
	struct xfs_perag	*pag;
	xfs_agnumber_t		agno = XFS_INO_TO_AGNO(mp, ip->i_ino);
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, ip->i_ino);
	xfs_agino_t		prev_agino, next_agino;
	unsigned int		bucket;
	bool			foundit = false;
	int			error;

	/* Grab the first inode in the list */
	pag = xfs_perag_get(mp, agno);
	error = xfs_ialloc_read_agi(pag, tp, 0, &agibp);
	xfs_perag_put(pag);
	if (error)
		return error;

	/*
	 * We've taken ILOCK_SHARED and the AGI buffer lock to stabilize the
	 * incore unlinked list pointers for this inode.  Check once more to
	 * see if we raced with anyone else to reload the unlinked list.
	 */
	if (!xfs_inode_unlinked_incomplete(ip)) {
		foundit = true;
		goto out_agibp;
	}

	bucket = agino % XFS_AGI_UNLINKED_BUCKETS;
	agi = agibp->b_addr;

	trace_xfs_inode_reload_unlinked_bucket(ip);

	xfs_info_ratelimited(mp,
 "Found unrecovered unlinked inode 0x%x in AG 0x%x.  Initiating list recovery.",
			agino, agno);

	prev_agino = NULLAGINO;
	next_agino = be32_to_cpu(agi->agi_unlinked[bucket]);
	while (next_agino != NULLAGINO) {
		struct xfs_inode	*next_ip = NULL;

		/* Found this caller's inode, set its backlink. */
		if (next_agino == agino) {
			next_ip = ip;
			next_ip->i_prev_unlinked = prev_agino;
			foundit = true;
			goto next_inode;
		}

		/* Try in-memory lookup first. */
		next_ip = xfs_iunlink_lookup(pag, next_agino);
		if (next_ip)
			goto next_inode;

		/* Inode not in memory, try reloading it. */
		error = xfs_iunlink_reload_next(tp, agibp, prev_agino,
				next_agino);
		if (error)
			break;

		/* Grab the reloaded inode. */
		next_ip = xfs_iunlink_lookup(pag, next_agino);
		if (!next_ip) {
			/* No incore inode at all?  We reloaded it... */
			ASSERT(next_ip != NULL);
			error = -EFSCORRUPTED;
			break;
		}

next_inode:
		prev_agino = next_agino;
		next_agino = next_ip->i_next_unlinked;
	}

out_agibp:
	xfs_trans_brelse(tp, agibp);
	/* Should have found this inode somewhere in the iunlinked bucket. */
	if (!error && !foundit)
		error = -EFSCORRUPTED;
	return error;
}

/* Decide if this inode is missing its unlinked list and reload it. */
int
xfs_inode_reload_unlinked(
	struct xfs_inode	*ip)
{
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc_empty(ip->i_mount, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	if (xfs_inode_unlinked_incomplete(ip))
		error = xfs_inode_reload_unlinked_bucket(tp, ip);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	xfs_trans_cancel(tp);

	return error;
}

/* Has this inode fork been zapped by repair? */
bool
xfs_ifork_zapped(
	const struct xfs_inode	*ip,
	int			whichfork)
{
	unsigned int		datamask = 0;

	switch (whichfork) {
	case XFS_DATA_FORK:
		switch (ip->i_vnode.i_mode & S_IFMT) {
		case S_IFDIR:
			datamask = XFS_SICK_INO_DIR_ZAPPED;
			break;
		case S_IFLNK:
			datamask = XFS_SICK_INO_SYMLINK_ZAPPED;
			break;
		}
		return ip->i_sick & (XFS_SICK_INO_BMBTD_ZAPPED | datamask);
	case XFS_ATTR_FORK:
		return ip->i_sick & XFS_SICK_INO_BMBTA_ZAPPED;
	default:
		return false;
	}
}

/* Compute the number of data and realtime blocks used by a file. */
void
xfs_inode_count_blocks(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	xfs_filblks_t		*dblocks,
	xfs_filblks_t		*rblocks)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);

	*rblocks = 0;
	if (XFS_IS_REALTIME_INODE(ip))
		xfs_bmap_count_leaves(ifp, rblocks);
	*dblocks = ip->i_nblocks - *rblocks;
}

static void
xfs_wait_dax_page(
	struct inode		*inode)
{
	struct xfs_inode        *ip = XFS_I(inode);

	xfs_iunlock(ip, XFS_MMAPLOCK_EXCL);
	schedule();
	xfs_ilock(ip, XFS_MMAPLOCK_EXCL);
}

int
xfs_break_dax_layouts(
	struct inode		*inode)
{
	xfs_assert_ilocked(XFS_I(inode), XFS_MMAPLOCK_EXCL);

	return dax_break_layout_inode(inode, xfs_wait_dax_page);
}

int
xfs_break_layouts(
	struct inode		*inode,
	uint			*iolock,
	enum layout_break_reason reason)
{
	bool			retry;
	int			error;

	xfs_assert_ilocked(XFS_I(inode), XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL);

	do {
		retry = false;
		switch (reason) {
		case BREAK_UNMAP:
			error = xfs_break_dax_layouts(inode);
			if (error)
				break;
			fallthrough;
		case BREAK_WRITE:
			error = xfs_break_leased_layouts(inode, iolock, &retry);
			break;
		default:
			WARN_ON_ONCE(1);
			error = -EINVAL;
		}
	} while (error == 0 && retry);

	return error;
}

/* Returns the size of fundamental allocation unit for a file, in bytes. */
unsigned int
xfs_inode_alloc_unitsize(
	struct xfs_inode	*ip)
{
	unsigned int		blocks = 1;

	if (XFS_IS_REALTIME_INODE(ip))
		blocks = ip->i_mount->m_sb.sb_rextsize;

	return XFS_FSB_TO_B(ip->i_mount, blocks);
}

/* Should we always be using copy on write for file writes? */
bool
xfs_is_always_cow_inode(
	const struct xfs_inode	*ip)
{
	return ip->i_mount->m_always_cow && xfs_has_reflink(ip->i_mount);
}
