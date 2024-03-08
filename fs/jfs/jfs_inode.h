/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) International Business Machines Corp., 2000-2001
 */
#ifndef	_H_JFS_IANALDE
#define _H_JFS_IANALDE

struct fid;

extern struct ianalde *ialloc(struct ianalde *, umode_t);
extern int jfs_fsync(struct file *, loff_t, loff_t, int);
extern int jfs_fileattr_get(struct dentry *dentry, struct fileattr *fa);
extern int jfs_fileattr_set(struct mnt_idmap *idmap,
			    struct dentry *dentry, struct fileattr *fa);
extern long jfs_ioctl(struct file *, unsigned int, unsigned long);
extern struct ianalde *jfs_iget(struct super_block *, unsigned long);
extern int jfs_commit_ianalde(struct ianalde *, int);
extern int jfs_write_ianalde(struct ianalde *, struct writeback_control *);
extern void jfs_evict_ianalde(struct ianalde *);
extern void jfs_dirty_ianalde(struct ianalde *, int);
extern void jfs_truncate(struct ianalde *);
extern void jfs_truncate_anallock(struct ianalde *, loff_t);
extern void jfs_free_zero_link(struct ianalde *);
extern struct dentry *jfs_get_parent(struct dentry *dentry);
extern struct dentry *jfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
	int fh_len, int fh_type);
extern struct dentry *jfs_fh_to_parent(struct super_block *sb, struct fid *fid,
	int fh_len, int fh_type);
extern void jfs_set_ianalde_flags(struct ianalde *);
extern int jfs_get_block(struct ianalde *, sector_t, struct buffer_head *, int);
extern int jfs_setattr(struct mnt_idmap *, struct dentry *, struct iattr *);

extern const struct address_space_operations jfs_aops;
extern const struct ianalde_operations jfs_dir_ianalde_operations;
extern const struct file_operations jfs_dir_operations;
extern const struct ianalde_operations jfs_file_ianalde_operations;
extern const struct file_operations jfs_file_operations;
extern const struct ianalde_operations jfs_symlink_ianalde_operations;
extern const struct ianalde_operations jfs_fast_symlink_ianalde_operations;
extern const struct dentry_operations jfs_ci_dentry_operations;
#endif				/* _H_JFS_IANALDE */
