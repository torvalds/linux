// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_inode_item.h"

#include <linux/iversion.h>

/*
 * Add a locked inode to the transaction.
 *
 * The inode must be locked, and it cannot be associated with any transaction.
 * If lock_flags is non-zero the inode will be unlocked on transaction commit.
 */
void
xfs_trans_ijoin(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	uint			lock_flags)
{
	xfs_inode_log_item_t	*iip;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	if (ip->i_itemp == NULL)
		xfs_inode_item_init(ip, ip->i_mount);
	iip = ip->i_itemp;

	ASSERT(iip->ili_lock_flags == 0);
	iip->ili_lock_flags = lock_flags;

	/*
	 * Get a log_item_desc to point at the new item.
	 */
	xfs_trans_add_item(tp, &iip->ili_item);
}

/*
 * Transactional inode timestamp update. Requires the inode to be locked and
 * joined to the transaction supplied. Relies on the transaction subsystem to
 * track dirty state and update/writeback the inode accordingly.
 */
void
xfs_trans_ichgtime(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	int			flags)
{
	struct inode		*inode = VFS_I(ip);
	struct timespec64	tv;

	ASSERT(tp);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	tv = current_time(inode);

	if (flags & XFS_ICHGTIME_MOD)
		inode->i_mtime = tv;
	if (flags & XFS_ICHGTIME_CHG)
		inode->i_ctime = tv;
	if (flags & XFS_ICHGTIME_CREATE)
		ip->i_d.di_crtime = tv;
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
	struct inode	*inode = VFS_I(ip);

	ASSERT(ip->i_itemp != NULL);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	/*
	 * Don't bother with i_lock for the I_DIRTY_TIME check here, as races
	 * don't matter - we either will need an extra transaction in 24 hours
	 * to log the timestamps, or will clear already cleared fields in the
	 * worst case.
	 */
	if (inode->i_state & (I_DIRTY_TIME | I_DIRTY_TIME_EXPIRED)) {
		spin_lock(&inode->i_lock);
		inode->i_state &= ~(I_DIRTY_TIME | I_DIRTY_TIME_EXPIRED);
		spin_unlock(&inode->i_lock);
	}

	/*
	 * Record the specific change for fdatasync optimisation. This
	 * allows fdatasync to skip log forces for inodes that are only
	 * timestamp dirty. We do this before the change count so that
	 * the core being logged in this case does not impact on fdatasync
	 * behaviour.
	 */
	ip->i_itemp->ili_fsync_fields |= flags;

	/*
	 * First time we log the inode in a transaction, bump the inode change
	 * counter if it is configured for this to occur. While we have the
	 * inode locked exclusively for metadata modification, we can usually
	 * avoid setting XFS_ILOG_CORE if no one has queried the value since
	 * the last time it was incremented. If we have XFS_ILOG_CORE already
	 * set however, then go ahead and bump the i_version counter
	 * unconditionally.
	 */
	if (!test_and_set_bit(XFS_LI_DIRTY, &ip->i_itemp->ili_item.li_flags) &&
	    IS_I_VERSION(VFS_I(ip))) {
		if (inode_maybe_inc_iversion(VFS_I(ip), flags & XFS_ILOG_CORE))
			flags |= XFS_ILOG_CORE;
	}

	tp->t_flags |= XFS_TRANS_DIRTY;

	/*
	 * Always OR in the bits from the ili_last_fields field.
	 * This is to coordinate with the xfs_iflush() and xfs_iflush_done()
	 * routines in the eventual clearing of the ili_fields bits.
	 * See the big comment in xfs_iflush() for an explanation of
	 * this coordination mechanism.
	 */
	flags |= ip->i_itemp->ili_last_fields;
	ip->i_itemp->ili_fields |= flags;
}

int
xfs_trans_roll_inode(
	struct xfs_trans	**tpp,
	struct xfs_inode	*ip)
{
	int			error;

	xfs_trans_log_inode(*tpp, ip, XFS_ILOG_CORE);
	error = xfs_trans_roll(tpp);
	if (!error)
		xfs_trans_ijoin(*tpp, ip, 0);
	return error;
}
