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
#include "transaction.h"
#include "disk-io.h"
#include "print-tree.h"

int btrfs_find_last_root(struct btrfs_root *root, u64 objectid,
			struct btrfs_root_item *item, struct btrfs_key *key)
{
	struct btrfs_path *path;
	struct btrfs_key search_key;
	struct btrfs_leaf *l;
	int ret;
	int slot;

	search_key.objectid = objectid;
	search_key.flags = (u32)-1;
	search_key.offset = (u64)-1;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret == 0);
	l = btrfs_buffer_leaf(path->nodes[0]);
	BUG_ON(path->slots[0] == 0);
	slot = path->slots[0] - 1;
	if (btrfs_disk_key_objectid(&l->items[slot].key) != objectid) {
		ret = 1;
		goto out;
	}
	memcpy(item, btrfs_item_ptr(l, slot, struct btrfs_root_item),
		sizeof(*item));
	btrfs_disk_key_to_cpu(key, &l->items[slot].key);
	ret = 0;
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}

int btrfs_update_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item)
{
	struct btrfs_path *path;
	struct btrfs_leaf *l;
	int ret;
	int slot;
	struct btrfs_root_item *update_item;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(trans, root, key, path, 0, 1);
	if (ret < 0)
		goto out;
	BUG_ON(ret != 0);
	l = btrfs_buffer_leaf(path->nodes[0]);
	slot = path->slots[0];
	update_item = btrfs_item_ptr(l, slot, struct btrfs_root_item);
	btrfs_memcpy(root, l, update_item, item, sizeof(*item));
	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}

int btrfs_insert_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item)
{
	int ret;
	ret = btrfs_insert_item(trans, root, key, item, sizeof(*item));
	return ret;
}

int btrfs_find_dead_roots(struct btrfs_root *root, u64 objectid,
			  struct btrfs_root *latest)
{
	struct btrfs_root *dead_root;
	struct btrfs_item *item;
	struct btrfs_root_item *ri;
	struct btrfs_key key;
	struct btrfs_path *path;
	int ret;
	u32 nritems;
	struct btrfs_leaf *leaf;
	int slot;

	key.objectid = objectid;
	key.flags = 0;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);
	key.offset = 0;
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;
	while(1) {
		leaf = btrfs_buffer_leaf(path->nodes[0]);
		nritems = btrfs_header_nritems(&leaf->header);
		slot = path->slots[0];
		if (slot >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret)
				break;
			leaf = btrfs_buffer_leaf(path->nodes[0]);
			nritems = btrfs_header_nritems(&leaf->header);
			slot = path->slots[0];
		}
		item = leaf->items + slot;
		btrfs_disk_key_to_cpu(&key, &item->key);
		if (btrfs_key_type(&key) != BTRFS_ROOT_ITEM_KEY)
			goto next;

		if (key.objectid < objectid)
			goto next;

		if (key.objectid > objectid)
			break;

		ri = btrfs_item_ptr(leaf, slot, struct btrfs_root_item);
		if (btrfs_root_refs(ri) != 0)
			goto next;

		dead_root = btrfs_read_fs_root_no_radix(root->fs_info, &key);
		if (IS_ERR(dead_root)) {
			ret = PTR_ERR(dead_root);
			goto err;
		}

		ret = btrfs_add_dead_root(dead_root, latest,
					  &root->fs_info->dead_roots);
		if (ret)
			goto err;
next:
		slot++;
		path->slots[0]++;
	}
	ret = 0;
err:
	btrfs_free_path(path);
	return ret;
}

int btrfs_del_root(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_key *key)
{
	struct btrfs_path *path;
	int ret;
	u32 refs;
	struct btrfs_root_item *ri;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(trans, root, key, path, -1, 1);
	if (ret < 0)
		goto out;
	BUG_ON(ret != 0);
	ri = btrfs_item_ptr(btrfs_buffer_leaf(path->nodes[0]),
			    path->slots[0], struct btrfs_root_item);

	refs = btrfs_root_refs(ri);
	BUG_ON(refs != 0);
	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}
