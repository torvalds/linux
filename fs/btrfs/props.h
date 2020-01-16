/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Filipe David Borba Manana <fdmanana@gmail.com>
 */

#ifndef BTRFS_PROPS_H
#define BTRFS_PROPS_H

#include "ctree.h"

void __init btrfs_props_init(void);

int btrfs_set_prop(struct btrfs_trans_handle *trans, struct iyesde *iyesde,
		   const char *name, const char *value, size_t value_len,
		   int flags);
int btrfs_validate_prop(const char *name, const char *value, size_t value_len);

int btrfs_load_iyesde_props(struct iyesde *iyesde, struct btrfs_path *path);

int btrfs_iyesde_inherit_props(struct btrfs_trans_handle *trans,
			      struct iyesde *iyesde,
			      struct iyesde *dir);

int btrfs_subvol_inherit_props(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_root *parent_root);

#endif
