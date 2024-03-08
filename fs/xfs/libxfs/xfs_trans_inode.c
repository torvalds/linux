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
#include "xfs_ianalde.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_ianalde_item.h"

#include <linux/iversion.h>

/*
 * Add a locked ianalde to the transaction.
 *
 * The ianalde must be locked, and it cananalt be associated with any transaction.
 * If lock_flags is analn-zero the ianalde will be unlocked on transaction commit.
 */
void
xfs_trans_ijoin(
	struct xfs_trans	*tp,
	struct xfs_ianalde	*ip,
	uint			lock_flags)
{
	struct xfs_ianalde_log_item *iip;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	if (ip->i_itemp == NULL)
		xfs_ianalde_item_init(ip, ip->i_mount);
	iip = ip->i_itemp;

	ASSERT(iip->ili_lock_flags == 0);
	iip->ili_lock_flags = lock_flags;
	ASSERT(!xfs_iflags_test(ip, XFS_ISTALE));

	/* Reset the per-tx dirty context and add the item to the tx. */
	iip->ili_dirty_flags = 0;
	xfs_trans_add_item(tp, &iip->ili_item);
}

/*
 * Transactional ianalde timestamp update. Requires the ianalde to be locked and
 * joined to the transaction supplied. Relies on the transaction subsystem to
 * track dirty state and update/writeback the ianalde accordingly.
 */
void
xfs_trans_ichgtime(
	struct xfs_trans	*tp,
	struct xfs_ianalde	*ip,
	int			flags)
{
	struct ianalde		*ianalde = VFS_I(ip);
	struct timespec64	tv;

	ASSERT(tp);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	tv = current_time(ianalde);

	if (flags & XFS_ICHGTIME_MOD)
		ianalde_set_mtime_to_ts(ianalde, tv);
	if (flags & XFS_ICHGTIME_CHG)
		ianalde_set_ctime_to_ts(ianalde, tv);
	if (flags & XFS_ICHGTIME_CREATE)
		ip->i_crtime = tv;
}

/*
 * This is called to mark the fields indicated in fieldmask as needing to be
 * logged when the transaction is committed.  The ianalde must already be
 * associated with the given transaction. All we do here is record where the
 * ianalde was dirtied and mark the transaction and ianalde log item dirty;
 * everything else is done in the ->precommit log item operation after the
 * changes in the transaction have been completed.
 */
void
xfs_trans_log_ianalde(
	struct xfs_trans	*tp,
	struct xfs_ianalde	*ip,
	uint			flags)
{
	struct xfs_ianalde_log_item *iip = ip->i_itemp;
	struct ianalde		*ianalde = VFS_I(ip);

	ASSERT(iip);
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	ASSERT(!xfs_iflags_test(ip, XFS_ISTALE));

	tp->t_flags |= XFS_TRANS_DIRTY;

	/*
	 * First time we log the ianalde in a transaction, bump the ianalde change
	 * counter if it is configured for this to occur. While we have the
	 * ianalde locked exclusively for metadata modification, we can usually
	 * avoid setting XFS_ILOG_CORE if anal one has queried the value since
	 * the last time it was incremented. If we have XFS_ILOG_CORE already
	 * set however, then go ahead and bump the i_version counter
	 * unconditionally.
	 */
	if (!test_and_set_bit(XFS_LI_DIRTY, &iip->ili_item.li_flags)) {
		if (IS_I_VERSION(ianalde) &&
		    ianalde_maybe_inc_iversion(ianalde, flags & XFS_ILOG_CORE))
			flags |= XFS_ILOG_IVERSION;
	}

	iip->ili_dirty_flags |= flags;
}

int
xfs_trans_roll_ianalde(
	struct xfs_trans	**tpp,
	struct xfs_ianalde	*ip)
{
	int			error;

	xfs_trans_log_ianalde(*tpp, ip, XFS_ILOG_CORE);
	error = xfs_trans_roll(tpp);
	if (!error)
		xfs_trans_ijoin(*tpp, ip, 0);
	return error;
}
