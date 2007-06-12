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

#include <linux/module.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

int btrfs_insert_inode(struct btrfs_trans_handle *trans, struct btrfs_root
		       *root, u64 objectid, struct btrfs_inode_item
		       *inode_item)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;
	key.objectid = objectid;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	btrfs_init_path(path);
	ret = btrfs_insert_item(trans, root, &key, inode_item,
				sizeof(*inode_item));
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	if (ret == 0 && objectid > root->highest_inode)
		root->highest_inode = objectid;
	return ret;
}

int btrfs_lookup_inode(struct btrfs_trans_handle *trans, struct btrfs_root
		       *root, struct btrfs_path *path,
		       struct btrfs_key *location, int mod)
{
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;
	int ret;
	int slot;
	struct btrfs_leaf *leaf;
	struct btrfs_key found_key;

	ret = btrfs_search_slot(trans, root, location, path, ins_len, cow);
	if (ret > 0 && btrfs_key_type(location) == BTRFS_ROOT_ITEM_KEY &&
	    location->offset == (u64)-1 && path->slots[0] != 0) {
		slot = path->slots[0] - 1;
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		btrfs_disk_key_to_cpu(&found_key, &leaf->items[slot].key);
		if (found_key.objectid == location->objectid &&
		    btrfs_key_type(&found_key) == btrfs_key_type(location)) {
			path->slots[0]--;
			return 0;
		}
	}
	return ret;
}
