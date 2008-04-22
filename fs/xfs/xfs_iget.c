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
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_quota.h"
#include "xfs_utils.h"

/*
 * Look up an inode by number in the given file system.
 * The inode is looked up in the cache held in each AG.
 * If the inode is found in the cache, attach it to the provided
 * vnode.
 *
 * If it is not in core, read it in from the file system's device,
 * add it to the cache and attach the provided vnode.
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
 * bno -- the block number starting the buffer containing the inode,
 *	  if known (as by bulkstat), else 0.
 */
STATIC int
xfs_iget_core(
	struct inode	*inode,
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	uint		flags,
	uint		lock_flags,
	xfs_inode_t	**ipp,
	xfs_daddr_t	bno)
{
	struct inode	*old_inode;
	xfs_inode_t	*ip;
	xfs_inode_t	*iq;
	int		error;
	unsigned long	first_index, mask;
	xfs_perag_t	*pag;
	xfs_agino_t	agino;

	/* the radix tree exists only in inode capable AGs */
	if (XFS_INO_TO_AGNO(mp, ino) >= mp->m_maxagi)
		return EINVAL;

	/* get the perag structure and ensure that it's inode capable */
	pag = xfs_get_perag(mp, ino);
	if (!pag->pagi_inodeok)
		return EINVAL;
	ASSERT(pag->pag_ici_init);
	agino = XFS_INO_TO_AGINO(mp, ino);

again:
	read_lock(&pag->pag_ici_lock);
	ip = radix_tree_lookup(&pag->pag_ici_root, agino);

	if (ip != NULL) {
		/*
		 * If INEW is set this inode is being set up
		 * we need to pause and try again.
		 */
		if (xfs_iflags_test(ip, XFS_INEW)) {
			read_unlock(&pag->pag_ici_lock);
			delay(1);
			XFS_STATS_INC(xs_ig_frecycle);

			goto again;
		}

		old_inode = ip->i_vnode;
		if (old_inode == NULL) {
			/*
			 * If IRECLAIM is set this inode is
			 * on its way out of the system,
			 * we need to pause and try again.
			 */
			if (xfs_iflags_test(ip, XFS_IRECLAIM)) {
				read_unlock(&pag->pag_ici_lock);
				delay(1);
				XFS_STATS_INC(xs_ig_frecycle);

				goto again;
			}
			ASSERT(xfs_iflags_test(ip, XFS_IRECLAIMABLE));

			/*
			 * If lookup is racing with unlink, then we
			 * should return an error immediately so we
			 * don't remove it from the reclaim list and
			 * potentially leak the inode.
			 */
			if ((ip->i_d.di_mode == 0) &&
			    !(flags & XFS_IGET_CREATE)) {
				read_unlock(&pag->pag_ici_lock);
				xfs_put_perag(mp, pag);
				return ENOENT;
			}

			xfs_itrace_exit_tag(ip, "xfs_iget.alloc");

			XFS_STATS_INC(xs_ig_found);
			xfs_iflags_clear(ip, XFS_IRECLAIMABLE);
			read_unlock(&pag->pag_ici_lock);

			XFS_MOUNT_ILOCK(mp);
			list_del_init(&ip->i_reclaim);
			XFS_MOUNT_IUNLOCK(mp);

			goto finish_inode;

		} else if (inode != old_inode) {
			/* The inode is being torn down, pause and
			 * try again.
			 */
			if (old_inode->i_state & (I_FREEING | I_CLEAR)) {
				read_unlock(&pag->pag_ici_lock);
				delay(1);
				XFS_STATS_INC(xs_ig_frecycle);

				goto again;
			}
/* Chances are the other vnode (the one in the inode) is being torn
* down right now, and we landed on top of it. Question is, what do
* we do? Unhook the old inode and hook up the new one?
*/
			cmn_err(CE_PANIC,
		"xfs_iget_core: ambiguous vns: vp/0x%p, invp/0x%p",
					old_inode, inode);
		}

		/*
		 * Inode cache hit
		 */
		read_unlock(&pag->pag_ici_lock);
		XFS_STATS_INC(xs_ig_found);

finish_inode:
		if (ip->i_d.di_mode == 0 && !(flags & XFS_IGET_CREATE)) {
			xfs_put_perag(mp, pag);
			return ENOENT;
		}

		if (lock_flags != 0)
			xfs_ilock(ip, lock_flags);

		xfs_iflags_clear(ip, XFS_ISTALE);
		xfs_itrace_exit_tag(ip, "xfs_iget.found");
		goto return_ip;
	}

	/*
	 * Inode cache miss
	 */
	read_unlock(&pag->pag_ici_lock);
	XFS_STATS_INC(xs_ig_missed);

	/*
	 * Read the disk inode attributes into a new inode structure and get
	 * a new vnode for it. This should also initialize i_ino and i_mount.
	 */
	error = xfs_iread(mp, tp, ino, &ip, bno,
			  (flags & XFS_IGET_BULKSTAT) ? XFS_IMAP_BULKSTAT : 0);
	if (error) {
		xfs_put_perag(mp, pag);
		return error;
	}

	xfs_itrace_exit_tag(ip, "xfs_iget.alloc");


	mrlock_init(&ip->i_lock, MRLOCK_ALLOW_EQUAL_PRI|MRLOCK_BARRIER,
		     "xfsino", ip->i_ino);
	mrlock_init(&ip->i_iolock, MRLOCK_BARRIER, "xfsio", ip->i_ino);
	init_waitqueue_head(&ip->i_ipin_wait);
	atomic_set(&ip->i_pincount, 0);
	initnsema(&ip->i_flock, 1, "xfsfino");

	if (lock_flags)
		xfs_ilock(ip, lock_flags);

	if ((ip->i_d.di_mode == 0) && !(flags & XFS_IGET_CREATE)) {
		xfs_idestroy(ip);
		xfs_put_perag(mp, pag);
		return ENOENT;
	}

	/*
	 * Preload the radix tree so we can insert safely under the
	 * write spinlock.
	 */
	if (radix_tree_preload(GFP_KERNEL)) {
		xfs_idestroy(ip);
		delay(1);
		goto again;
	}
	mask = ~(((XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_inodelog)) - 1);
	first_index = agino & mask;
	write_lock(&pag->pag_ici_lock);
	/*
	 * insert the new inode
	 */
	error = radix_tree_insert(&pag->pag_ici_root, agino, ip);
	if (unlikely(error)) {
		BUG_ON(error != -EEXIST);
		write_unlock(&pag->pag_ici_lock);
		radix_tree_preload_end();
		xfs_idestroy(ip);
		XFS_STATS_INC(xs_ig_dup);
		goto again;
	}

	/*
	 * These values _must_ be set before releasing the radix tree lock!
	 */
	ip->i_udquot = ip->i_gdquot = NULL;
	xfs_iflags_set(ip, XFS_INEW);

	write_unlock(&pag->pag_ici_lock);
	radix_tree_preload_end();

	/*
	 * Link ip to its mount and thread it on the mount's inode list.
	 */
	XFS_MOUNT_ILOCK(mp);
	if ((iq = mp->m_inodes)) {
		ASSERT(iq->i_mprev->i_mnext == iq);
		ip->i_mprev = iq->i_mprev;
		iq->i_mprev->i_mnext = ip;
		iq->i_mprev = ip;
		ip->i_mnext = iq;
	} else {
		ip->i_mnext = ip;
		ip->i_mprev = ip;
	}
	mp->m_inodes = ip;

	XFS_MOUNT_IUNLOCK(mp);
	xfs_put_perag(mp, pag);

 return_ip:
	ASSERT(ip->i_df.if_ext_max ==
	       XFS_IFORK_DSIZE(ip) / sizeof(xfs_bmbt_rec_t));

	xfs_iflags_set(ip, XFS_IMODIFIED);
	*ipp = ip;

	/*
	 * If we have a real type for an on-disk inode, we can set ops(&unlock)
	 * now.	 If it's a new inode being created, xfs_ialloc will handle it.
	 */
	xfs_initialize_vnode(mp, inode, ip);
	return 0;
}


/*
 * The 'normal' internal xfs_iget, if needed it will
 * 'allocate', or 'get', the vnode.
 */
int
xfs_iget(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	uint		flags,
	uint		lock_flags,
	xfs_inode_t	**ipp,
	xfs_daddr_t	bno)
{
	struct inode	*inode;
	xfs_inode_t	*ip;
	int		error;

	XFS_STATS_INC(xs_ig_attempts);

retry:
	inode = iget_locked(mp->m_super, ino);
	if (!inode)
		/* If we got no inode we are out of memory */
		return ENOMEM;

	if (inode->i_state & I_NEW) {
		XFS_STATS_INC(vn_active);
		XFS_STATS_INC(vn_alloc);

		error = xfs_iget_core(inode, mp, tp, ino, flags,
				lock_flags, ipp, bno);
		if (error) {
			make_bad_inode(inode);
			if (inode->i_state & I_NEW)
				unlock_new_inode(inode);
			iput(inode);
		}
		return error;
	}

	/*
	 * If the inode is not fully constructed due to
	 * filehandle mismatches wait for the inode to go
	 * away and try again.
	 *
	 * iget_locked will call __wait_on_freeing_inode
	 * to wait for the inode to go away.
	 */
	if (is_bad_inode(inode)) {
		iput(inode);
		delay(1);
		goto retry;
	}

	ip = XFS_I(inode);
	if (!ip) {
		iput(inode);
		delay(1);
		goto retry;
	}

	if (lock_flags != 0)
		xfs_ilock(ip, lock_flags);
	XFS_STATS_INC(xs_ig_found);
	*ipp = ip;
	return 0;
}

/*
 * Look for the inode corresponding to the given ino in the hash table.
 * If it is there and its i_transp pointer matches tp, return it.
 * Otherwise, return NULL.
 */
xfs_inode_t *
xfs_inode_incore(xfs_mount_t	*mp,
		 xfs_ino_t	ino,
		 xfs_trans_t	*tp)
{
	xfs_inode_t	*ip;
	xfs_perag_t	*pag;

	pag = xfs_get_perag(mp, ino);
	read_lock(&pag->pag_ici_lock);
	ip = radix_tree_lookup(&pag->pag_ici_root, XFS_INO_TO_AGINO(mp, ino));
	read_unlock(&pag->pag_ici_lock);
	xfs_put_perag(mp, pag);

	/* the returned inode must match the transaction */
	if (ip && (ip->i_transp != tp))
		return NULL;
	return ip;
}

/*
 * Decrement reference count of an inode structure and unlock it.
 *
 * ip -- the inode being released
 * lock_flags -- this parameter indicates the inode's locks to be
 *       to be released.  See the comment on xfs_iunlock() for a list
 *	 of valid values.
 */
void
xfs_iput(xfs_inode_t	*ip,
	 uint		lock_flags)
{
	xfs_itrace_entry(ip);
	xfs_iunlock(ip, lock_flags);
	IRELE(ip);
}

/*
 * Special iput for brand-new inodes that are still locked
 */
void
xfs_iput_new(xfs_inode_t	*ip,
	     uint		lock_flags)
{
	struct inode	*inode = ip->i_vnode;

	xfs_itrace_entry(ip);

	if ((ip->i_d.di_mode == 0)) {
		ASSERT(!xfs_iflags_test(ip, XFS_IRECLAIMABLE));
		make_bad_inode(inode);
	}
	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);
	if (lock_flags)
		xfs_iunlock(ip, lock_flags);
	IRELE(ip);
}


/*
 * This routine embodies the part of the reclaim code that pulls
 * the inode from the inode hash table and the mount structure's
 * inode list.
 * This should only be called from xfs_reclaim().
 */
void
xfs_ireclaim(xfs_inode_t *ip)
{
	/*
	 * Remove from old hash list and mount list.
	 */
	XFS_STATS_INC(xs_ig_reclaims);

	xfs_iextract(ip);

	/*
	 * Here we do a spurious inode lock in order to coordinate with
	 * xfs_sync().  This is because xfs_sync() references the inodes
	 * in the mount list without taking references on the corresponding
	 * vnodes.  We make that OK here by ensuring that we wait until
	 * the inode is unlocked in xfs_sync() before we go ahead and
	 * free it.  We get both the regular lock and the io lock because
	 * the xfs_sync() code may need to drop the regular one but will
	 * still hold the io lock.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);

	/*
	 * Release dquots (and their references) if any. An inode may escape
	 * xfs_inactive and get here via vn_alloc->vn_reclaim path.
	 */
	XFS_QM_DQDETACH(ip->i_mount, ip);

	/*
	 * Pull our behavior descriptor from the vnode chain.
	 */
	if (ip->i_vnode) {
		ip->i_vnode->i_private = NULL;
		ip->i_vnode = NULL;
	}

	/*
	 * Free all memory associated with the inode.
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	xfs_idestroy(ip);
}

/*
 * This routine removes an about-to-be-destroyed inode from
 * all of the lists in which it is located with the exception
 * of the behavior chain.
 */
void
xfs_iextract(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_perag_t	*pag = xfs_get_perag(mp, ip->i_ino);
	xfs_inode_t	*iq;

	write_lock(&pag->pag_ici_lock);
	radix_tree_delete(&pag->pag_ici_root, XFS_INO_TO_AGINO(mp, ip->i_ino));
	write_unlock(&pag->pag_ici_lock);
	xfs_put_perag(mp, pag);

	/*
	 * Remove from mount's inode list.
	 */
	XFS_MOUNT_ILOCK(mp);
	ASSERT((ip->i_mnext != NULL) && (ip->i_mprev != NULL));
	iq = ip->i_mnext;
	iq->i_mprev = ip->i_mprev;
	ip->i_mprev->i_mnext = iq;

	/*
	 * Fix up the head pointer if it points to the inode being deleted.
	 */
	if (mp->m_inodes == ip) {
		if (ip == iq) {
			mp->m_inodes = NULL;
		} else {
			mp->m_inodes = iq;
		}
	}

	/* Deal with the deleted inodes list */
	list_del_init(&ip->i_reclaim);

	mp->m_ireclaims++;
	XFS_MOUNT_IUNLOCK(mp);
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

	xfs_ilock_trace(ip, 1, lock_flags, (inst_t *)__return_address);
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
	xfs_ilock_trace(ip, 2, lock_flags, (inst_t *)__return_address);
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
		xfs_trans_unlocked_item(ip->i_mount,
					(xfs_log_item_t*)(ip->i_itemp));
	}
	xfs_ilock_trace(ip, 3, lock_flags, (inst_t *)__return_address);
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
}

#ifdef DEBUG
/*
 * Debug-only routine, without additional rw_semaphore APIs, we can
 * now only answer requests regarding whether we hold the lock for write
 * (reader state is outside our visibility, we only track writer state).
 *
 * Note: this means !xfs_isilocked would give false positives, so don't do that.
 */
int
xfs_isilocked(
	xfs_inode_t		*ip,
	uint			lock_flags)
{
	if ((lock_flags & (XFS_ILOCK_EXCL|XFS_ILOCK_SHARED)) ==
			XFS_ILOCK_EXCL) {
		if (!ip->i_lock.mr_writer)
			return 0;
	}

	if ((lock_flags & (XFS_IOLOCK_EXCL|XFS_IOLOCK_SHARED)) ==
			XFS_IOLOCK_EXCL) {
		if (!ip->i_iolock.mr_writer)
			return 0;
	}

	return 1;
}
#endif

/*
 * The following three routines simply manage the i_flock
 * semaphore embedded in the inode.  This semaphore synchronizes
 * processes attempting to flush the in-core inode back to disk.
 */
void
xfs_iflock(xfs_inode_t *ip)
{
	psema(&(ip->i_flock), PINOD|PLTWAIT);
}

int
xfs_iflock_nowait(xfs_inode_t *ip)
{
	return (cpsema(&(ip->i_flock)));
}

void
xfs_ifunlock(xfs_inode_t *ip)
{
	ASSERT(issemalocked(&(ip->i_flock)));
	vsema(&(ip->i_flock));
}
