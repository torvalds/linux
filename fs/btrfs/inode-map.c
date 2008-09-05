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

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"

int btrfs_find_highest_inode(struct btrfs_root *root, u64 *objectid)
{
	struct btrfs_path *path;
	int ret;
	struct extent_buffer *l;
	struct btrfs_key search_key;
	struct btrfs_key found_key;
	int slot;

	path = btrfs_alloc_path();
	BUG_ON(!path);

	search_key.objectid = BTRFS_LAST_FREE_OBJECTID;
	search_key.type = -1;
	search_key.offset = (u64)-1;
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto error;
	BUG_ON(ret == 0);
	if (path->slots[0] > 0) {
		slot = path->slots[0] - 1;
		l = path->nodes[0];
		btrfs_item_key_to_cpu(l, &found_key, slot);
		*objectid = found_key.objectid;
	} else {
		*objectid = BTRFS_FIRST_FREE_OBJECTID;
	}
	ret = 0;
error:
	btrfs_free_path(path);
	return ret;
}

/*
 * walks the btree of allocated inodes and find a hole.
 */
int btrfs_find_free_objectid(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     u64 dirid, u64 *objectid)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;
	int slot = 0;
	u64 last_ino = 0;
	int start_found;
	struct extent_buffer *l;
	struct btrfs_key search_key;
	u64 search_start = dirid;

	mutex_lock(&root->objectid_mutex);
	if (root->last_inode_alloc >= BTRFS_FIRST_FREE_OBJECTID &&
	    root->last_inode_alloc < BTRFS_LAST_FREE_OBJECTID) {
		*objectid = ++root->last_inode_alloc;
		mutex_unlock(&root->objectid_mutex);
		return 0;
	}
	path = btrfs_alloc_path();
	BUG_ON(!path);
	search_start = max(search_start, BTRFS_FIRST_FREE_OBJECTID);
	search_key.objectid = search_start;
	search_key.type = 0;
	search_key.offset = 0;

	btrfs_init_path(path);
	start_found = 0;
	ret = btrfs_search_slot(trans, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto error;

	while (1) {
		l = path->nodes[0];
		slot = path->slots[0];
		if (slot >= btrfs_header_nritems(l)) {
			ret = btrfs_next_leaf(root, path);
			if (ret == 0)
				continue;
			if (ret < 0)
				goto error;
			if (!start_found) {
				*objectid = search_start;
				start_found = 1;
				goto found;
			}
			*objectid = last_ino > search_start ?
				last_ino : search_start;
			goto found;
		}
		btrfs_item_key_to_cpu(l, &key, slot);
		if (key.objectid >= search_start) {
			if (start_found) {
				if (last_ino < search_start)
					last_ino = search_start;
				if (key.objectid > last_ino) {
					*objectid = last_ino;
					goto found;
				}
			}
		}
		if (key.objectid >= BTRFS_LAST_FREE_OBJECTID)
			break;
		start_found = 1;
		last_ino = key.objectid + 1;
		path->slots[0]++;
	}
	// FIXME -ENOSPC
	BUG_ON(1);
found:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	BUG_ON(*objectid < search_start);
	mutex_unlock(&root->objectid_mutex);
	return 0;
error:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	mutex_unlock(&root->objectid_mutex);
	return ret;
}
