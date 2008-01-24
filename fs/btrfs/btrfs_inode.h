/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

#ifndef __BTRFS_I__
#define __BTRFS_I__

#include "extent_map.h"
#include "extent_io.h"

/* in memory btrfs inode */
struct btrfs_inode {
	struct btrfs_root *root;
	struct btrfs_block_group_cache *block_group;
	struct btrfs_key location;
	struct extent_map_tree extent_tree;
	struct extent_io_tree io_tree;
	struct inode vfs_inode;

	u64 ordered_trans;
	/*
	 * transid of the trans_handle that last modified this inode
	 */
	u64 last_trans;
	u32 flags;
};
static inline struct btrfs_inode *BTRFS_I(struct inode *inode)
{
	return container_of(inode, struct btrfs_inode, vfs_inode);
}

#endif
