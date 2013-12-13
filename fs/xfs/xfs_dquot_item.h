/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.
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
#ifndef __XFS_DQUOT_ITEM_H__
#define __XFS_DQUOT_ITEM_H__

struct xfs_dquot;
struct xfs_trans;
struct xfs_mount;
struct xfs_qoff_logitem;

typedef struct xfs_dq_logitem {
	xfs_log_item_t		 qli_item;	   /* common portion */
	struct xfs_dquot	*qli_dquot;	   /* dquot ptr */
	xfs_lsn_t		 qli_flush_lsn;	   /* lsn at last flush */
} xfs_dq_logitem_t;

typedef struct xfs_qoff_logitem {
	xfs_log_item_t		 qql_item;	/* common portion */
	struct xfs_qoff_logitem *qql_start_lip; /* qoff-start logitem, if any */
	unsigned int		qql_flags;
} xfs_qoff_logitem_t;


extern void		   xfs_qm_dquot_logitem_init(struct xfs_dquot *);
extern xfs_qoff_logitem_t *xfs_qm_qoff_logitem_init(struct xfs_mount *,
					struct xfs_qoff_logitem *, uint);
extern xfs_qoff_logitem_t *xfs_trans_get_qoff_item(struct xfs_trans *,
					struct xfs_qoff_logitem *, uint);
extern void		   xfs_trans_log_quotaoff_item(struct xfs_trans *,
					struct xfs_qoff_logitem *);

#endif	/* __XFS_DQUOT_ITEM_H__ */
