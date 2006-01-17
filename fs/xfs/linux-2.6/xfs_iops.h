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

extern struct inode_operations linvfs_file_inode_operations;
extern struct inode_operations linvfs_dir_inode_operations;
extern struct inode_operations linvfs_symlink_inode_operations;

extern struct file_operations linvfs_file_operations;
extern struct file_operations linvfs_invis_file_operations;
extern struct file_operations linvfs_dir_operations;

extern int xfs_ioctl(struct bhv_desc *, struct inode *, struct file *,
                        int, unsigned int, void __user *);

struct xfs_inode;
extern void xfs_ichgtime(struct xfs_inode *, int);
extern void xfs_ichgtime_fast(struct xfs_inode *, struct inode *, int);

#endif /* __XFS_IOPS_H__ */
