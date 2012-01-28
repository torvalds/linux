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

#define SYNC_WAIT		0x0001	/* wait for i/o to complete */
#define SYNC_TRYLOCK		0x0002  /* only try to lock inodes */

extern struct workqueue_struct	*xfs_syncd_wq;	/* sync workqueue */

int xfs_syncd_init(struct xfs_mount *mp);
void xfs_syncd_stop(struct xfs_mount *mp);

int xfs_quiesce_data(struct xfs_mount *mp);
void xfs_quiesce_attr(struct xfs_mount *mp);

void xfs_flush_inodes(struct xfs_inode *ip);

int xfs_log_dirty_inode(struct xfs_inode *ip, struct xfs_perag *pag, int flags);

int xfs_reclaim_inodes(struct xfs_mount *mp, int mode);
int xfs_reclaim_inodes_count(struct xfs_mount *mp);
void xfs_reclaim_inodes_nr(struct xfs_mount *mp, int nr_to_scan);

void xfs_inode_set_reclaim_tag(struct xfs_inode *ip);
void __xfs_inode_set_reclaim_tag(struct xfs_perag *pag, struct xfs_inode *ip);
void __xfs_inode_clear_reclaim_tag(struct xfs_mount *mp, struct xfs_perag *pag,
				struct xfs_inode *ip);

int xfs_sync_inode_grab(struct xfs_inode *ip);
int xfs_inode_ag_iterator(struct xfs_mount *mp,
	int (*execute)(struct xfs_inode *ip, struct xfs_perag *pag, int flags),
	int flags);

#endif
