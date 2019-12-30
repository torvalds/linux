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
struct xfs_qoff_logitem;

struct xfs_dq_logitem {
	struct xfs_log_item	qli_item;	/* common portion */
	struct xfs_dquot	*qli_dquot;	/* dquot ptr */
	xfs_lsn_t		qli_flush_lsn;	/* lsn at last flush */
};

struct xfs_qoff_logitem {
	struct xfs_log_item	qql_item;	/* common portion */
	struct xfs_qoff_logitem *qql_start_lip;	/* qoff-start logitem, if any */
	unsigned int		qql_flags;
};


void xfs_qm_dquot_logitem_init(struct xfs_dquot *dqp);
struct xfs_qoff_logitem	*xfs_qm_qoff_logitem_init(struct xfs_mount *mp,
		struct xfs_qoff_logitem *start,
		uint flags);
struct xfs_qoff_logitem	*xfs_trans_get_qoff_item(struct xfs_trans *tp,
		struct xfs_qoff_logitem *startqoff,
		uint flags);
void xfs_trans_log_quotaoff_item(struct xfs_trans *tp,
		struct xfs_qoff_logitem *qlp);

#endif	/* __XFS_DQUOT_ITEM_H__ */
