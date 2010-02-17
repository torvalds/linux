/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
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
#ifndef XFS_SYNC_H
#define XFS_SYNC_H 1

struct xfs_mount;
struct xfs_perag;

typedef struct xfs_sync_work {
	struct list_head	w_list;
	struct xfs_mount	*w_mount;
	void			*w_data;	/* syncer routine argument */
	void			(*w_syncer)(struct xfs_mount *, void *);
	struct completion	*w_completion;
} xfs_sync_work_t;

#define SYNC_WAIT		0x0001	/* wait for i/o to complete */
#define SYNC_TRYLOCK		0x0002  /* only try to lock inodes */

int xfs_syncd_init(struct xfs_mount *mp);
void xfs_syncd_stop(struct xfs_mount *mp);

int xfs_sync_attr(struct xfs_mount *mp, int flags);
int xfs_sync_data(struct xfs_mount *mp, int flags);
int xfs_sync_fsdata(struct xfs_mount *mp, int flags);

int xfs_quiesce_data(struct xfs_mount *mp);
void xfs_quiesce_attr(struct xfs_mount *mp);

void xfs_flush_inodes(struct xfs_inode *ip);

int xfs_reclaim_inodes(struct xfs_mount *mp, int mode);

void xfs_inode_set_reclaim_tag(struct xfs_inode *ip);
void __xfs_inode_set_reclaim_tag(struct xfs_perag *pag, struct xfs_inode *ip);
void __xfs_inode_clear_reclaim_tag(struct xfs_mount *mp, struct xfs_perag *pag,
				struct xfs_inode *ip);

int xfs_sync_inode_valid(struct xfs_inode *ip, struct xfs_perag *pag);
int xfs_inode_ag_iterator(struct xfs_mount *mp,
	int (*execute)(struct xfs_inode *ip, struct xfs_perag *pag, int flags),
	int flags, int tag, int write_lock);

#endif
