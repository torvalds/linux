/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2001
 */
#ifndef	_H_JFS_INODE
#define _H_JFS_INODE

struct fid;

extern struct iyesde *ialloc(struct iyesde *, umode_t);
extern int jfs_fsync(struct file *, loff_t, loff_t, int);
extern long jfs_ioctl(struct file *, unsigned int, unsigned long);
extern long jfs_compat_ioctl(struct file *, unsigned int, unsigned long);
extern struct iyesde *jfs_iget(struct super_block *, unsigned long);
extern int jfs_commit_iyesde(struct iyesde *, int);
extern int jfs_write_iyesde(struct iyesde *, struct writeback_control *);
extern void jfs_evict_iyesde(struct iyesde *);
extern void jfs_dirty_iyesde(struct iyesde *, int);
extern void jfs_truncate(struct iyesde *);
extern void jfs_truncate_yeslock(struct iyesde *, loff_t);
extern void jfs_free_zero_link(struct iyesde *);
extern struct dentry *jfs_get_parent(struct dentry *dentry);
extern struct dentry *jfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
	int fh_len, int fh_type);
extern struct dentry *jfs_fh_to_parent(struct super_block *sb, struct fid *fid,
	int fh_len, int fh_type);
extern void jfs_set_iyesde_flags(struct iyesde *);
extern int jfs_get_block(struct iyesde *, sector_t, struct buffer_head *, int);
extern int jfs_setattr(struct dentry *, struct iattr *);

extern const struct address_space_operations jfs_aops;
extern const struct iyesde_operations jfs_dir_iyesde_operations;
extern const struct file_operations jfs_dir_operations;
extern const struct iyesde_operations jfs_file_iyesde_operations;
extern const struct file_operations jfs_file_operations;
extern const struct iyesde_operations jfs_symlink_iyesde_operations;
extern const struct iyesde_operations jfs_fast_symlink_iyesde_operations;
extern const struct dentry_operations jfs_ci_dentry_operations;
#endif				/* _H_JFS_INODE */
