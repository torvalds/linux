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

/*
 * returns 0 on finding something, 1 if no more roots are there
 * and < 0 on error
 */
int btrfs_search_root(struct btrfs_root *root, u64 search_start,
		      u64 *found_objectid)
{
	struct btrfs_path *path;
	struct btrfs_key search_key;
	int ret;

	root = root->fs_info->tree_root;
	search_key.objectid = search_start;
	search_key.type = (u8)-1;
	search_key.offset = (u64)-1;

	path = btrfs_alloc_path();
	BUG_ON(!path);
again:
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto out;
	if (ret == 0) {
		ret = 1;
		goto out;
	}
	if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
		ret = btrfs_next_leaf(root, path);
		if (ret)
			goto out;
	}
	btrfs_item_key_to_cpu(path->nodes[0], &search_key, path->slots[0]);
	if (search_key.type != BTRFS_ROOT_ITEM_KEY) {
		search_key.offset++;
		btrfs_release_path(root, path);
		goto again;
	}
	ret = 0;
	*found_objectid = search_key.objectid;

out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_find_last_root(struct btrfs_root *root, u64 objectid,
			struct btrfs_root_item *item, struct btrfs_key *key)
{
	struct btrfs_path *path;
	struct btrfs_key search_key;
	struct btrfs_key found_key;
	struct extent_buffer *l;
	int ret;
	int slot;

	search_key.objectid = objectid;
	search_key.type = (u8)-1;
	search_key.offset = (u64)-1;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto out;

	BUG_ON(ret == 0);
	l = path->nodes[0];
	BUG_ON(path->slots[0] == 0);
	slot = path->slots[0] - 1;
	btrfs_item_key_to_cpu(l, &found_key, slot);
	if (found_key.objectid != objectid) {
		ret = 1;
		goto out;
	}
	read_extent_buffer(l, item, btrfs_item_ptr_offset(l, slot),
			   sizeof(*item));
	memcpy(key, &found_key, sizeof(found_key));
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_update_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item)
{
	struct btrfs_path *path;
	struct extent_buffer *l;
	int ret;
	int slot;
	unsigned long ptr;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(trans, root, key, path, 0, 1);
	if (ret < 0)
		goto out;

	if (ret != 0) {
		btrfs_print_leaf(root, path->nodes[0]);
		printk("unable to update root key %Lu %u %Lu\n",
		       key->objectid, key->type, key->offset);
		BUG_ON(1);
	}

	l = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr_offset(l, slot);
	write_extent_buffer(l, item, ptr, sizeof(*item));
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
	struct extent_buffer *leaf;
	int slot;

	key.objectid = objectid;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);
	key.offset = 0;
	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;
	while(1) {
		leaf = path->nodes[0];
		nritems = btrfs_header_nritems(leaf);
		slot = path->slots[0];
		if (slot >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret)
				break;
			leaf = path->nodes[0];
			nritems = btrfs_header_nritems(leaf);
			slot = path->slots[0];
		}
		item = btrfs_item_nr(leaf, slot);
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (btrfs_key_type(&key) != BTRFS_ROOT_ITEM_KEY)
			goto next;

		if (key.objectid < objectid)
			goto next;

		if (key.objectid > objectid)
			break;

		ri = btrfs_item_ptr(leaf, slot, struct btrfs_root_item);
		if (btrfs_disk_root_refs(leaf, ri) != 0)
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
	struct extent_buffer *leaf;

	path = btrfs_alloc_path();
	BUG_ON(!path);
	ret = btrfs_search_slot(trans, root, key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret) {
btrfs_print_leaf(root, path->nodes[0]);
printk("failed to del %Lu %u %Lu\n", key->objectid, key->type, key->offset);

	}
	BUG_ON(ret != 0);
	leaf = path->nodes[0];
	ri = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_item);

	refs = btrfs_disk_root_refs(leaf, ri);
	BUG_ON(refs != 0);
	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_release_path(root, path);
	btrfs_free_path(path);
	return ret;
}
