/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@cayesnical.com>
 */
#ifndef EFIVAR_FS_INTERNAL_H
#define EFIVAR_FS_INTERNAL_H

#include <linux/list.h>

extern const struct file_operations efivarfs_file_operations;
extern const struct iyesde_operations efivarfs_dir_iyesde_operations;
extern bool efivarfs_valid_name(const char *str, int len);
extern struct iyesde *efivarfs_get_iyesde(struct super_block *sb,
			const struct iyesde *dir, int mode, dev_t dev,
			bool is_removable);

extern struct list_head efivarfs_list;

#endif /* EFIVAR_FS_INTERNAL_H */
