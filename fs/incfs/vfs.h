/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018 Google LLC
 */

#ifndef _INCFS_VFS_H
#define _INCFS_VFS_H

enum FILL_PERMISSION {
	CANT_FILL = 0,
	CAN_FILL = 1,
};

extern const struct file_operations incfs_file_ops;
extern const struct inode_operations incfs_file_inode_ops;

void incfs_kill_sb(struct super_block *sb);
struct dentry *incfs_mount_fs(struct file_system_type *type, int flags,
			      const char *dev_name, void *data);
int incfs_link(struct dentry *what, struct dentry *where);
int incfs_unlink(struct dentry *dentry);

static inline struct mount_info *get_mount_info(struct super_block *sb)
{
	struct mount_info *result = sb->s_fs_info;

	WARN_ON(!result);
	return result;
}

static inline struct super_block *file_superblock(struct file *f)
{
	struct inode *inode = file_inode(f);

	return inode->i_sb;
}

#endif
