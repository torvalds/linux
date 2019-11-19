/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018 Google LLC
 */

#ifndef _INCFS_VFS_H
#define _INCFS_VFS_H

void incfs_kill_sb(struct super_block *sb);
struct dentry *incfs_mount_fs(struct file_system_type *type, int flags,
			      const char *dev_name, void *data);

#endif
