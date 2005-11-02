/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
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
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_trans_priv.h"
#include "xfs_inode_item.h"

#ifdef XFS_TRANS_DEBUG
STATIC void
xfs_trans_inode_broot_debug(
	xfs_inode_t	*ip);
#else
#define	xfs_trans_inode_broot_debug(ip)
#endif


/*
 * Get and lock the inode for the caller if it is not already
 * locked within the given transaction.  If it is already locked
 * within the transaction, just increment its lock recursion count
 * and return a pointer to it.
 *
 * For an inode to be locked in a transaction, the inode lock, as
 * opposed to the io lock, must be taken exclusively.  This ensures
 * that the inode can be involved in only 1 transaction at a time.
 * Lock recursion is handled on the io lock, but only for lock modes
 * of equal or lesser strength.  That is, you can recur on the io lock
 * held EXCL with a SHARED request but not vice versa.  Also, if
 * the inode is already a part of the transaction then you cannot
 * go from not holding the io lock to having it EXCL or SHARED.
 *
 * Use the inode cache routine xfs_inode_incore() to find the inode
 * if it is already owned by this transaction.
 *
 * If we don't already own the inode, use xfs_iget() to get it.
 * Since the inode log item structure is embedded in the incore
 * inode structure and is initialized when the inode is brought
 * into memory, there is nothing to do with it here.
 *
 * If the given transaction pointer is NULL, just call xfs_iget().
 * This simplifies code which must handle both cases.
 */
int
xfs_trans_iget(
	xfs_mount_t	*mp,
	xfs_trans_t	*tp,
	xfs_ino_t	ino,
	uint		flags,
	uint		lock_flags,
	xfs_inode_t	**ipp)
{
	int			error;
	xfs_inode_t		*ip;
	xfs_inode_log_item_t	*iip;

	/*
	 * If the transaction pointer is NULL, just call the normal
	 * xfs_iget().
	 */
	if (tp == NULL)
		return xfs_iget(mp, NULL, ino, flags, lock_flags, ipp, 0);

	/*
	 * If we find the inode in core with this transaction
	 * pointer in its i_transp field, then we know we already
	 * have it locked.  In this case we just increment the lock
	 * recursion count and return the inode to the caller.
	 * Assert that the inode is already locked in the mode requested
	 * by the caller.  We cannot do lock promotions yet, so
	 * die if someone gets this wrong.
	 */
	if ((ip = xfs_inode_incore(tp->t_mountp, ino, tp)) != NULL) {
		/*
		 * Make sure that the inode lock is held EXCL and
		 * that the io lock is never upgraded when the inode
		 * is already a part of the transaction.
		 */
		ASSERT(ip->i_itemp != NULL);
		ASSERT(lock_flags & XFS_ILOCK_EXCL);
		ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));
		ASSERT((!(lock_flags & XFS_IOLOCK_EXCL)) ||
		       ismrlocked(&ip->i_iolock, MR_UPDATE));
		ASSERT((!(lock_flags & XFS_IOLOCK_EXCL)) ||
		       (ip->i_itemp->ili_flags & XFS_ILI_IOLOCKED_EXCL));
		ASSERT((!(lock_flags & XFS_IOLOCK_SHARED)) ||
		       ismrlocked(&ip->i_iolock, (MR_UPDATE | MR_ACCESS)));
		ASSERT((!(lock_flags & XFS_IOLOCK_SHARED)) ||
		       (ip->i_itemp->ili_flags & XFS_ILI_IOLOCKED_ANY));

		if (lock_flags & (XFS_IOLOCK_SHARED | XFS_IOLOCK_EXCL)) {
			ip->i_itemp->ili_iolock_recur++;
		}
		if (lock_flags & XFS_ILOCK_EXCL) {
			ip->i_itemp->ili_ilock_recur++;
		}
		*ipp = ip;
		return 0;
	}

	ASSERT(lock_flags & XFS_ILOCK_EXCL);
	error = xfs_iget(tp->t_mountp, tp, ino, flags, lock_flags, &ip, 0);
	if (error) {
		return error;
	}
	ASSERT(ip != NULL);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	if (ip->i_itemp == NULL)
		xfs_inode_item_init(ip, mp);
	iip = ip->i_itemp;
	(void) xfs_trans_add_item(tp, (xfs_log_item_t *)(iip));

	xfs_trans_inode_broot_debug(ip);

	/*
	 * If the IO lock has been acquired, mark that in
	 * the inode log item so we'll know to unlock it
	 * when the transaction commits.
	 */
	ASSERT(iip->ili_flags == 0);
	if (lock_flags & XFS_IOLOCK_EXCL) {
		iip->ili_flags |= XFS_ILI_IOLOCKED_EXCL;
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		iip->ili_flags |= XFS_ILI_IOLOCKED_SHARED;
	}

	/*
	 * Initialize i_transp so we can find it with xfs_inode_incore()
	 * above.
	 */
	ip->i_transp = tp;

	*ipp = ip;
	return 0;
}

/*
 * Add the locked inode to the transaction.
 * The inode must be locked, and it cannot be associated with any
 * transaction.  The caller must specify the locks already held
 * on the inode.
 */
void
xfs_trans_ijoin(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	uint		lock_flags)
{
	xfs_inode_log_item_t	*iip;

	ASSERT(ip->i_transp == NULL);
	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));
	ASSERT(lock_flags & XFS_ILOCK_EXCL);
	if (ip->i_itemp == NULL)
		xfs_inode_item_init(ip, ip->i_mount);
	iip = ip->i_itemp;
	ASSERT(iip->ili_flags == 0);
	ASSERT(iip->ili_ilock_recur == 0);
	ASSERT(iip->ili_iolock_recur == 0);

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	(void) xfs_trans_add_item(tp, (xfs_log_item_t*)(iip));

	xfs_trans_inode_broot_debug(ip);

	/*
	 * If the IO lock is already held, mark that in the inode log item.
	 */
	if (lock_flags & XFS_IOLOCK_EXCL) {
		iip->ili_flags |= XFS_ILI_IOLOCKED_EXCL;
	} else if (lock_flags & XFS_IOLOCK_SHARED) {
		iip->ili_flags |= XFS_ILI_IOLOCKED_SHARED;
	}

	/*
	 * Initialize i_transp so we can find it with xfs_inode_incore()
	 * in xfs_trans_iget() above.
	 */
	ip->i_transp = tp;
}



/*
 * Mark the inode as not needing to be unlocked when the inode item's
 * IOP_UNLOCK() routine is called.  The inode must already be locked
 * and associated with the given transaction.
 */
/*ARGSUSED*/
void
xfs_trans_ihold(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip)
{
	ASSERT(ip->i_transp == tp);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));

	ip->i_itemp->ili_flags |= XFS_ILI_HOLD;
}


/*
 * This is called to mark the fields indicated in fieldmask as needing
 * to be logged when the transaction is committed.  The inode must
 * already be associated with the given transaction.
 *
 * The values for fieldmask are defined in xfs_inode_item.h.  We always
 * log all of the core inode if any of it has changed, and we always log
 * all of the inline data/extents/b-tree root if any of them has changed.
 */
void
xfs_trans_log_inode(
	xfs_trans_t	*tp,
	xfs_inode_t	*ip,
	uint		flags)
{
	xfs_log_item_desc_t	*lidp;

	ASSERT(ip->i_transp == tp);
	ASSERT(ip->i_itemp != NULL);
	ASSERT(ismrlocked(&ip->i_lock, MR_UPDATE));

	lidp = xfs_trans_find_item(tp, (xfs_log_item_t*)(ip->i_itemp));
	ASSERT(lidp != NULL);

	tp->t_flags |= XFS_TRANS_DIRTY;
	lidp->lid_flags |= XFS_LID_DIRTY;

	/*
	 * Always OR in the bits from the ili_last_fields field.
	 * This is to coordinate with the xfs_iflush() and xfs_iflush_done()
	 * routines in the eventual clearing of the ilf_fields bits.
	 * See the big comment in xfs_iflush() for an explanation of
	 * this coorination mechanism.
	 */
	flags |= ip->i_itemp->ili_last_fields;
	ip->i_itemp->ili_format.ilf_fields |= flags;
}

#ifdef XFS_TRANS_DEBUG
/*
 * Keep track of the state of the inode btree root to make sure we
 * log it properly.
 */
STATIC void
xfs_trans_inode_broot_debug(
	xfs_inode_t	*ip)
{
	xfs_inode_log_item_t	*iip;

	ASSERT(ip->i_itemp != NULL);
	iip = ip->i_itemp;
	if (iip->ili_root_size != 0) {
		ASSERT(iip->ili_orig_root != NULL);
		kmem_free(iip->ili_orig_root, iip->ili_root_size);
		iip->ili_root_size = 0;
		iip->ili_orig_root = NULL;
	}
	if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE) {
		ASSERT((ip->i_df.if_broot != NULL) &&
		       (ip->i_df.if_broot_bytes > 0));
		iip->ili_root_size = ip->i_df.if_broot_bytes;
		iip->ili_orig_root =
			(char*)kmem_alloc(iip->ili_root_size, KM_SLEEP);
		memcpy(iip->ili_orig_root, (char*)(ip->i_df.if_broot),
		      iip->ili_root_size);
	}
}
#endif
