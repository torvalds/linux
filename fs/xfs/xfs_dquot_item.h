// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef __XFS_DQUOT_ITEM_H__
#define __XFS_DQUOT_ITEM_H__

struct xfs_dquot;
struct xfs_trans;
struct xfs_mount;

struct xfs_dq_logitem {
	struct xfs_log_item	qli_item;	/* common portion */
	struct xfs_dquot	*qli_dquot;	/* dquot ptr */
	xfs_lsn_t		qli_flush_lsn;	/* lsn at last flush */

	/*
	 * We use this spinlock to coordinate access to the li_buf pointer in
	 * the log item and the qli_dirty flag.
	 */
	spinlock_t		qli_lock;
	bool			qli_dirty;	/* dirtied since last flush? */
};

void xfs_qm_dquot_logitem_init(struct xfs_dquot *dqp);

#endif	/* __XFS_DQUOT_ITEM_H__ */
