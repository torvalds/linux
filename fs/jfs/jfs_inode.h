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

struct fid;

extern struct inode *ialloc(struct inode *, umode_t);
extern int jfs_fsync(struct file *, loff_t, loff_t, int);
extern long jfs_ioctl(struct file *, unsigned int, unsigned long);
extern long jfs_compat_ioctl(struct file *, unsigned int, unsigned long);
extern struct inode *jfs_iget(struct super_block *, unsigned long);
extern int jfs_commit_inode(struct inode *, int);
extern int jfs_write_inode(struct inode *, struct writeback_control *);
extern void jfs_evict_inode(struct inode *);
extern void jfs_dirty_inode(struct inode *, int);
extern void jfs_truncate(struct inode *);
extern void jfs_truncate_nolock(struct inode *, loff_t);
extern void jfs_free_zero_link(struct inode *);
extern struct dentry *jfs_get_parent(struct dentry *dentry);
extern void jfs_get_inode_flags(struct jfs_inode_info *);
extern struct dentry *jfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
	int fh_len, int fh_type);
extern struct dentry *jfs_fh_to_parent(struct super_block *sb, struct fid *fid,
	int fh_len, int fh_type);
extern void jfs_set_inode_flags(struct inode *);
extern int jfs_get_block(struct inode *, sector_t, struct buffer_head *, int);
extern int jfs_setattr(struct dentry *, struct iattr *);

extern const struct address_space_operations jfs_aops;
extern const struct inode_operations jfs_dir_inode_operations;
extern const struct file_operations jfs_dir_operations;
extern const struct inode_operations jfs_file_inode_operations;
extern const struct file_operations jfs_file_operations;
extern const struct inode_operations jfs_symlink_inode_operations;
extern const struct inode_operations jfs_fast_symlink_inode_operations;
extern const struct dentry_operations jfs_ci_dentry_operations;
#endif				/* _H_JFS_INODE */
