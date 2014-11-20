/*
 * Copyright (C) 2014 Filipe David Borba Manana <fdmanana@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __BTRFS_PROPS_H
#define __BTRFS_PROPS_H

#include "ctree.h"

void __init btrfs_props_init(void);

int btrfs_set_prop(struct inode *inode,
		   const char *name,
		   const char *value,
		   size_t value_len,
		   int flags);

int btrfs_load_inode_props(struct inode *inode, struct btrfs_path *path);

int btrfs_inode_inherit_props(struct btrfs_trans_handle *trans,
			      struct inode *inode,
			      struct inode *dir);

int btrfs_subvol_inherit_props(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_root *parent_root);

#endif
