/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@canonical.com>
 */
#ifndef EFIVAR_FS_INTERNAL_H
#define EFIVAR_FS_INTERNAL_H

#include <linux/list.h>

extern const struct file_operations efivarfs_file_operations;
extern const struct inode_operations efivarfs_dir_inode_operations;
extern bool efivarfs_valid_name(const char *str, int len);
extern struct inode *efivarfs_get_inode(struct super_block *sb,
			const struct inode *dir, int mode, dev_t dev,
			bool is_removable);

extern struct list_head efivarfs_list;

#endif /* EFIVAR_FS_INTERNAL_H */
