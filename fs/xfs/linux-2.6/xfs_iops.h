/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_IOPS_H__
#define __XFS_IOPS_H__

extern const struct inode_operations xfs_inode_operations;
extern const struct inode_operations xfs_dir_inode_operations;
extern const struct inode_operations xfs_symlink_inode_operations;

extern const struct file_operations xfs_file_operations;
extern const struct file_operations xfs_dir_file_operations;
extern const struct file_operations xfs_invis_file_operations;


struct xfs_inode;
extern void xfs_ichgtime(struct xfs_inode *, int);
extern void xfs_ichgtime_fast(struct xfs_inode *, struct inode *, int);

#define xfs_vtoi(vp) \
	((struct xfs_inode *)vn_to_inode(vp)->i_private)

#define XFS_I(inode) \
	((struct xfs_inode *)(inode)->i_private)

#endif /* __XFS_IOPS_H__ */
