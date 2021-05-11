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
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
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
	struct xfs_inode_log_item *iip;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	if (ip->i_itemp == NULL)
		xfs_inode_item_init(ip, ip->i_mount);
	iip = ip->i_itemp;

	ASSERT(iip->ili_lock_flags == 0);
	iip->ili_lock_flags = lock_flags;
	ASSERT(!xfs_iflags_test(ip, XFS_ISTALE));

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
		ip->i_crtime = tv;
}

/*
 * This is called to mark the fields indicated in fieldmask as needing to be
 * logged when the transaction is committed.  The inode must already be
 * associated with the given transaction.
 *
 * The values for fieldmask are defined in xfs_inode_item.h.  We always log all
 * of the core inode if any of it has changed, and we always log all of the
 * inline data/extents/b-tree root if any of them has changed.
 *
 * Grab and pin the cluster buffer associated with this inode to avoid RMW
 * cycles at inode writeback time. Avoid the need to add error handling to every
 * xfs_trans_log_inode() call by shutting down on read error.  This will cause
 * transactions to fail and everything to error out, just like if we return a
 * read error in a dirty transaction and cancel it.
 */
void
xfs_trans_log_inode(
	struct xfs_trans	*tp,
	struct xfs_inode	*ip,
	uint			flags)
{
	struct xfs_inode_log_item *iip = ip->i_itemp;
	struct inode		*inode = VFS_I(ip);
	uint			iversion_flags = 0;

	ASSERT(iip);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(!xfs_iflags_test(ip, XFS_ISTALE));

	tp->t_flags |= XFS_TRANS_DIRTY;

	/*
	 * Don't bother with i_lock for the I_DIRTY_TIME check here, as races
	 * don't matter - we either will need an extra transaction in 24 hours
	 * to log the timestamps, or will clear already cleared fields in the
	 * worst case.
	 */
	if (inode->i_state & I_DIRTY_TIME) {
		spin_lock(&inode->i_lock);
		inode->i_state &= ~I_DIRTY_TIME;
		spin_unlock(&inode->i_lock);
	}

	/*
	 * First time we log the inode in a transaction, bump the inode change
	 * counter if it is configured for this to occur. While we have the
	 * inode locked exclusively for metadata modification, we can usually
	 * avoid setting XFS_ILOG_CORE if no one has queried the value since
	 * the last time it was incremented. If we have XFS_ILOG_CORE already
	 * set however, then go ahead and bump the i_version counter
	 * unconditionally.
	 */
	if (!test_and_set_bit(XFS_LI_DIRTY, &iip->ili_item.li_flags)) {
		if (IS_I_VERSION(inode) &&
		    inode_maybe_inc_iversion(inode, flags & XFS_ILOG_CORE))
			iversion_flags = XFS_ILOG_CORE;
	}

	/*
	 * If we're updating the inode core or the timestamps and it's possible
	 * to upgrade this inode to bigtime format, do so now.
	 */
	if ((flags & (XFS_ILOG_CORE | XFS_ILOG_TIMESTAMP)) &&
	    xfs_sb_version_hasbigtime(&ip->i_mount->m_sb) &&
	    !xfs_inode_has_bigtime(ip)) {
		ip->i_diflags2 |= XFS_DIFLAG2_BIGTIME;
		flags |= XFS_ILOG_CORE;
	}

	/*
	 * Record the specific change for fdatasync optimisation. This allows
	 * fdatasync to skip log forces for inodes that are only timestamp
	 * dirty.
	 */
	spin_lock(&iip->ili_lock);
	iip->ili_fsync_fields |= flags;

	if (!iip->ili_item.li_buf) {
		struct xfs_buf	*bp;
		int		error;

		/*
		 * We hold the ILOCK here, so this inode is not going to be
		 * flushed while we are here. Further, because there is no
		 * buffer attached to the item, we know that there is no IO in
		 * progress, so nothing will clear the ili_fields while we read
		 * in the buffer. Hence we can safely drop the spin lock and
		 * read the buffer knowing that the state will not change from
		 * here.
		 */
		spin_unlock(&iip->ili_lock);
		error = xfs_imap_to_bp(ip->i_mount, tp, &ip->i_imap, &bp);
		if (error) {
			xfs_force_shutdown(ip->i_mount, SHUTDOWN_META_IO_ERROR);
			return;
		}

		/*
		 * We need an explicit buffer reference for the log item but
		 * don't want the buffer to remain attached to the transaction.
		 * Hold the buffer but release the transaction reference once
		 * we've attached the inode log item to the buffer log item
		 * list.
		 */
		xfs_buf_hold(bp);
		spin_lock(&iip->ili_lock);
		iip->ili_item.li_buf = bp;
		bp->b_flags |= _XBF_INODES;
		list_add_tail(&iip->ili_item.li_bio_list, &bp->b_li_list);
		xfs_trans_brelse(tp, bp);
	}

	/*
	 * Always OR in the bits from the ili_last_fields field.  This is to
	 * coordinate with the xfs_iflush() and xfs_buf_inode_iodone() routines
	 * in the eventual clearing of the ili_fields bits.  See the big comment
	 * in xfs_iflush() for an explanation of this coordination mechanism.
	 */
	iip->ili_fields |= (flags | iip->ili_last_fields | iversion_flags);
	spin_unlock(&iip->ili_lock);
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
