/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Filipe David Borba Manana <fdmanana@gmail.com>
 */

#ifndef BTRFS_PROPS_H
#define BTRFS_PROPS_H

#include "ctree.h"

void __init btrfs_props_init(void);

int btrfs_set_prop_trans(struct inode *inode, const char *name,
			 const char *value, size_t value_len, int flags);

int btrfs_load_inode_props(struct inode *inode, struct btrfs_path *path);

int btrfs_inode_inherit_props(struct btrfs_trans_handle *trans,
			      struct inode *inode,
			      struct inode *dir);

int btrfs_subvol_inherit_props(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_root *parent_root);

#endif
