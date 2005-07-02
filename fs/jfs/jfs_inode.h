/*
 *   Copyright (C) International Business Machines Corp., 2000-2001
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifndef	_H_JFS_INODE
#define _H_JFS_INODE

extern struct inode *ialloc(struct inode *, umode_t);
extern int jfs_fsync(struct file *, struct dentry *, int);
extern void jfs_read_inode(struct inode *);
extern int jfs_commit_inode(struct inode *, int);
extern int jfs_write_inode(struct inode*, int);
extern void jfs_delete_inode(struct inode *);
extern void jfs_dirty_inode(struct inode *);
extern void jfs_truncate(struct inode *);
extern void jfs_truncate_nolock(struct inode *, loff_t);
extern void jfs_free_zero_link(struct inode *);
extern struct dentry *jfs_get_parent(struct dentry *dentry);

extern struct address_space_operations jfs_aops;
extern struct inode_operations jfs_dir_inode_operations;
extern struct file_operations jfs_dir_operations;
extern struct inode_operations jfs_file_inode_operations;
extern struct file_operations jfs_file_operations;
extern struct inode_operations jfs_symlink_inode_operations;
extern struct dentry_operations jfs_ci_dentry_operations;
#endif				/* _H_JFS_INODE */
