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
#include "xfs_acl.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_quota.h"
#include "xfs_utils.h"
#include "xfs_trans_priv.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_btree_trace.h"
#include "xfs_trace.h"


/*
 * Allocate and initialise an xfs_inode.
 */
STATIC struct xfs_inode *
xfs_inode_alloc(
	struct xfs_mount	*mp,
	xfs_ino_t		ino)
{
	struct xfs_inode	*ip;

	/*
	 * if this didn't occur in transactions, we could use
	 * KM_MAYFAIL and return NULL here on ENOMEM. Set the
	 * code up to do this anyway.
	 */
	ip = kmem_zone_alloc(xfs_inode_zone, KM_SLEEP);
	if (!ip)
		return NULL;
	if (inode_init_always(mp->m_super, VFS_I(ip))) {
		kmem_zone_free(xfs_inode_zone, ip);
		return NULL;
	}

	ASSERT(atomic_read(&ip->i_iocount) == 0);
	ASSERT(atomic_read(&ip->i_pincount) == 0);
	ASSERT(!spin_is_locked(&ip->i_flags_lock));
	ASSERT(completion_done(&ip->i_flush));

	mrlock_init(&ip->i_iolock, MRLOCK_BARRIER, "xfsio", ip->i_ino);

	/* initialise the xfs inode */
	ip->i_ino = ino;
	ip->i_mount = mp;
	memset(&ip->i_imap, 0, sizeof(struct xfs_imap));
	ip->i_afp = NULL;
	memset(&ip->i_df, 0, sizeof(xfs_ifork_t));
	ip->i_flags = 0;
	ip->i_update_core = 0;
	ip->i_delayed_blks = 0;
	memset(&ip->i_d, 0, sizeof(xfs_icdinode_t));
	ip->i_size = 0;
	ip->i_new_size = 0;

	/* prevent anyone from using this yet */
	VFS_I(ip)->i_state = I_NEW;

	return ip;
}

void
xfs_inode_free(
	struct xfs_inode	*ip)
{
	switch (ip->i_d.di_mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		xfs_idestroy_fork(ip, XFS_DATA_FORK);
		break;
	}

	if (ip->i_afp)
		xfs_idestroy_fork(ip, XFS_ATTR_FORK);

	if (ip->i_itemp) {
		/*
		 * Only if we are shutting down the fs will we see an
		 * inode still in the AIL. If it is there, we should remove
		 * it to prevent a use-after-free from occurring.
		 */
		xfs_log_item_t	*lip = &ip->i_itemp->ili_item;
		struct xfs_ail	*ailp = lip->li_ailp;

		ASSERT(((lip->li_flags & XFS_LI_IN_AIL) == 0) ||
				       XFS_FORCED_SHUTDOWN(ip->i_mount));
		if (lip->li_flags & XFS_LI_IN_AIL) {
			spin_lock(&ailp->xa_lock);
			if (lip->li_flags & XFS_LI_IN_AIL)
				xfs_trans_ail_delete(ailp, lip);
			else
				spin_unlock(&ailp->xa_lock);
		}
		xfs_inode_item_destroy(ip);
		ip->i_itemp = NULL;
	}

	/* asserts to verify all state is correct here */
	ASSERT(atomic_read(&ip->i_iocount) == 0);
	ASSERT(atomic_read(&ip->i_pincount) == 0);
	ASSERT(!spin_is_locked(&ip->i_flags_lock));
	ASSERT(completion_done(&ip->i_flush));

	kmem_zone_free(xfs_inode_zone, ip);
}

/*
 * Check the validity of the inode we just found it the cache
 */
static int
xfs_iget_cache_hit(
	struct xfs_perag	*pag,
	struct xfs_inode	*ip,
	int			flags,
	int			lock_flags) __releases(pag->pag_ici_lock)
{
	struct inode		*inode = VFS_I(ip);
	struct xfs_mount	*mp = ip->i_mount;
	int			error;

	spin_lock(&ip->i_flags_lock);

	/*
	 * If we are racing with another cache hit that is currently
	 * instantiating this inode or currently recycling it out of
	 * reclaimabe state, wait for the initialisation to complete
	 * before continuing.
	 *
	 * XXX(hch): eventually we should do something equivalent to
	 *	     wait_on_inode to wait for these flags to be cleared
	 *	     instead of polling for it.
	 */
	if (ip->i_flags & (XFS_INEW|XFS_IRECLAIM)) {
		trace_xfs_iget_skip(ip);
		XFS_STATS_INC(xs_ig_frecycle);
		error = EAGAIN;
		goto out_error;
	}

	/*
	 * If lookup is racing with unlink return an error immediately.
	 */
	if (ip->i_d.di_mode == 0 && !(flags & XFS_IGET_CREATE)) {
		error = ENOENT;
		goto out_error;
	}

	/*
	 * If IRECLAIMABLE is set, we've torn down the VFS inode already.
	 * Need to carefully get it back into useable state.
	 */
	if (ip->i_flags & XFS_IRECLAIMABLE) {
		trace_xfs_iget_reclaim(ip);

		/*
		 * We need to set XFS_IRECLAIM to prevent xfs_reclaim_inode
		 * from stomping over us while we recycle the inode.  We can't
		 * clear the radix tree reclaimable tag yet as it requires
		 * pag_ici_lock to be held exclusive.
		 */
		ip->i_flags |= XFS_IRECLAIM;

		spin_unlock(&ip->i_flags_lock);
		read_unlock(&pag->pag_ici_lock);

		error = -inode_init_always(mp->m_super, inode);
		if (error) {
			/*
			 * Re-initializing the inode failed, and we are in deep
			 * trouble.  Try to re-add it to the reclaim list.
			 */
			read_lock(&pag->pag_ici_lock);
			spin_lock(&ip->i_flags_lock);

			ip->i_flags &= ~XFS_INEW;
			ip->i_flags |= XFS_IRECLAIMABLE;
			__xfs_inode_set_reclaim_tag(pag, ip);
			trace_xfs_iget_reclaim_fail(ip);
			goto out_error;
		}

		write_lock(&pag->pag_ici_lock);
		spin_lock(&ip->i_flags_lock);
		ip->i_flags &= ~(XFS_IRECLAIMABLE | XFS_IRECLAIM);
		ip->i_flags |= XFS_INEW;
		__xfs_inode_clear_reclaim_tag(mp, pag, ip);
		inode->i_state = I_NEW;
		spin_unlock(&ip->i_flags_lock);
		write_unlock(&pag->pag_ici_lock);
	} else {
		/* If the VFS inode is being torn down, pause and try again. */
		if (!igrab(inode)) {
			trace_xfs_iget_skip(ip);
			error = EAGAIN;
			goto out_error;
		}

		/* We've got a live one. */
		spin_unlock(&ip->i_flags_lock);
		read_unlock(&pag->pag_ici_lock);
		trace_xfs_iget_hit(ip);
	}

	if (lock_flags != 0)
		xfs_ilock(ip, lock_flags);

	xfs_iflags_clear(ip, XFS_ISTALE);
	XFS_STATS_INC(xs_ig_found);

	return 0;

out_error:
	spin_unlock(&ip->i_flags_lock);
	read_unlock(&pag->pag_ici_lock);
	return error;
}


static int
xfs_iget_cache_miss(
	struct xfs_mount	*mp,
	struct xfs_perag	*pag,
	xfs_trans_t		*tp,
	xfs_ino_t		ino,
	struct xfs_inode	**ipp,
	int			flags,
	int			lock_flags)
{
	struct xfs_inode	*ip;
	int			error;
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, ino);

	ip = xfs_inode_alloc(mp, ino);
	if (!ip)
		return ENOMEM;

	error = xfs_iread(mp, tp, ip, flags);
	if (error)
		goto out_destroy;

	trace_xfs_iget_miss(ip);

	if ((ip->i_d.di_mode == 0) && !(flags & XFS_IGET_CREATE)) {
		error = ENOENT;
		goto out_destroy;
	}

	/*
	 * Preload the radix tree so we can insert safely under the
	 * write spinlock. Note that we cannot sleep inside the preload
	 * region.
	 */
	if (radix_tree_preload(GFP_KERNEL)) {
		error = EAGAIN;
		goto out_destroy;
	}

	/*
	 * Because the inode hasn't been added to the radix-tree yet it can't
	 * be found by another thread, so we can do the non-sleeping lock here.
	 */
	if (lock_flags) {
		if (!xfs_ilock_nowait(ip, lock_flags))
			BUG();
	}

	write_lock(&pag->pag_ici_lock);

	/* insert the new inode */
	error = radix_tree_insert(&pag->pag_ici_root, agino, ip);
	if (unlikely(error)) {
		WARN_ON(error != -EEXIST);
		XFS_STATS_INC(xs_ig_dup);
		error = EAGAIN;
		goto out_preload_end;
	}

	/* These values _must_ be set before releasing the radix tree lock! */
	ip->i_udquot = ip->i_gdquot = NULL;
	xfs_iflags_set(ip, XFS_INEW);

	write_unlock(&pag->pag_ici_lock);
	radix_tree_preload_end();

	*ipp = ip;
	return 0;

out_preload_end:
	write_unlock(&pag->pag_ici_lock);
	radix_tree_preload_end();
	if (lock_flags)
		xfs_iunlock(ip, lock_flags);
out_destroy:
	__destroy_inode(VFS_I(ip));
	xfs_inode_free(ip);
	return error;
}

/*
 * Look up an inode by number in the given file system.
 * The inode is looked up in the cache held in each AG.
 * If the inode is found in the cache, initialise the vfs inode
 * if necessary.
 *
 * If it is not in core, read it in from the file system's device,
 * add it to the cache and initialise the vfs inode.
 *
 * The inode is locked according to the value of the lock_flags parameter.
 * This flag parameter indicates how and if the inode's IO lock and inode lock
 * should be taken.
 *
 * mp -- the mount point structure for the current file system.  It points
 *       to the inode hash table.
 * tp -- a pointer to the current transaction if there is one.  This is
 *       simply passed through to the xfs_iread() call.
 * ino -- the number of the inode desired.  This is the unique identifier
 *        within the file system for the inode being requested.
 * lock_flags -- flags indicating how to lock the inode.  See the comment
 *		 for xfs_ilock() for a list of valid values.
 */
int
xfs_iget(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	uint		flags,
	uint		lock_flags,
	xfs_inode_t	**ipp)
{
	xfs_inode_t	*ip;
	int		error;
	xfs_perag_t	*pag;
	xfs_agino_t	agino;

	/* the radix tree exists only in inode capable AGs */
	if (XFS_INO_TO_AGNO(mp, ino) >= mp->m_maxagi)
		return EINVAL;

	/* get the perag structure and ensure that it's inode capable */
	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ino));
	agino = XFS_INO_TO_AGINO(mp, ino);

again:
	error = 0;
	read_lock(&pag->pag_ici_lock);
	ip = radix_tree_lookup(&pag->pag_ici_root, agino);

	if (ip) {
		error = xfs_iget_cache_hit(pag, ip, flags, lock_flags);
		if (error)
			goto out_error_or_again;
	} else {
		read_unlock(&pag->pag_ici_lock);
		XFS_STATS_INC(xs_ig_missed);

		error = xfs_iget_cache_miss(mp, pag, tp, ino, &ip,
							flags, lock_flags);
		if (error)
			goto out_error_or_again;
	}
	xfs_perag_put(pag);

	*ipp = ip;

	ASSERT(ip->i_df.if_ext_max ==
	       XFS_IFORK_DSIZE(ip) / sizeof(xfs_bmbt_rec_t));
	/*
	 * If we have a real type for an on-disk inode, we can set ops(&unlock)
	 * now.	 If it's a new inode being created, xfs_ialloc will handle it.
	 */
	if (xfs_iflags_test(ip, XFS_INEW) && ip->i_d.di_mode != 0)
		xfs_setup_inode(ip);
	return 0;

out_error_or_again:
	if (error == EAGAIN) {
		delay(1);
		goto again;
	}
	xfs_perag_put(pag);
	return error;
}

/*
 * This is a wrapper routine around the xfs_ilock() routine
 * used to centralize some grungy code.  It is used in places
 * that wish to lock the inode solely for reading the extents.
 * The reason these places can't just call xfs_ilock(SHARED)
 * is that the inode lock also guards to bringing in of the
 * extents from disk for a file in b-tree format.  If the inode
 * is in b-tree format, then we need to lock the inode exclusively
 * until the extents are read in.  Locking it exclusively all
 * the time would limit our parallelism unnecessarily, though.
 * What we do instead is check to see if the extents have been
 * read in yet, and only lock the inode exclusively if they
 * have not.
 *
 * The function returns a value which should be given to the
 * corresponding xfs_iunlock_map_shared().  This value is
 * the mode in which the lock was actually taken.
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

	trace_xfs_ilock(ip, lock_flags, _RET_IP_);
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
	trace_xfs_ilock_nowait(ip, lock_flags, _RET_IP_);
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
	ASSERT((lock_flags & ~(XFS_LOCK_MASK | XFS_IUNLOCK_NONOTIFY |
			XFS_LOCK_DEP_MASK)) == 0);
	ASSERT(lock_flags != 0);

	if (lock_flags & XFS_IOLOCK_EXCL)
		mrunlock_excl(&ip->i_iolock);
	else if (lock_flags & XFS_IOLOCK_SHARED)
		mrunlock_shared(&ip->i_iolock);

	if (lock_flags & XFS_ILOCK_EXCL)
		mrunlock_excl(&ip->i_lock);
	else if (lock_flags & XFS_ILOCK_SHARED)
		mrunlock_shared(&ip->i_lock);

	if ((lock_flags & (XFS_ILOCK_SHARED | XFS_ILOCK_EXCL)) &&
	    !(lock_flags & XFS_IUNLOCK_NONOTIFY) && ip->i_itemp) {
		/*
		 * Let the AIL know that this item has been unlocked in case
		 * it is in the AIL and anyone is waiting on it.  Don't do
		 * this if the caller has asked us not to.
		 */
		xfs_trans_unlocked_item(ip->i_itemp->ili_item.li_ailp,
					(xfs_log_item_t*)(ip->i_itemp));
	}
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

#ifdef DEBUG
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
