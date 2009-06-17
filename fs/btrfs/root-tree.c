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
 *  search forward for a root, starting with objectid 'search_start'
 *  if a root key is found, the objectid we find is filled into 'found_objectid'
 *  and 0 is returned.  < 0 is returned on error, 1 if there is nothing
 *  left in the tree.
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

/*
 * lookup the root with the highest offset for a given objectid.  The key we do
 * find is copied into 'key'.  If we find something return 0, otherwise 1, < 0
 * on error.
 */
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
	search_key.type = BTRFS_ROOT_ITEM_KEY;
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

int btrfs_set_root_node(struct btrfs_root_item *item,
			struct extent_buffer *node)
{
	btrfs_set_root_bytenr(item, node->start);
	btrfs_set_root_level(item, btrfs_header_level(node));
	btrfs_set_root_generation(item, btrfs_header_generation(node));
	return 0;
}

/*
 * copy the data in 'item' into the btree
 */
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
		printk(KERN_CRIT "unable to update root key %llu %u %llu\n",
		       (unsigned long long)key->objectid, key->type,
		       (unsigned long long)key->offset);
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

/*
 * at mount time we want to find all the old transaction snapshots that were in
 * the process of being deleted if we crashed.  This is any root item with an
 * offset lower than the latest root.  They need to be queued for deletion to
 * finish what was happening when we crashed.
 */
int btrfs_find_dead_roots(struct btrfs_root *root, u64 objectid)
{
	struct btrfs_root *dead_root;
	struct btrfs_item *item;
	struct btrfs_root_item *ri;
	struct btrfs_key key;
	struct btrfs_key found_key;
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

again:
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0)
		goto err;
	while (1) {
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

		memcpy(&found_key, &key, sizeof(key));
		key.offset++;
		btrfs_release_path(root, path);
		dead_root =
			btrfs_read_fs_root_no_radix(root->fs_info->tree_root,
						    &found_key);
		if (IS_ERR(dead_root)) {
			ret = PTR_ERR(dead_root);
			goto err;
		}

		ret = btrfs_add_dead_root(dead_root);
		if (ret)
			goto err;
		goto again;
next:
		slot++;
		path->slots[0]++;
	}
	ret = 0;
err:
	btrfs_free_path(path);
	return ret;
}

/* drop the root item for 'key' from 'root' */
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

#if 0 /* this will get used when snapshot deletion is implemented */
int btrfs_del_root_ref(struct btrfs_trans_handle *trans,
		       struct btrfs_root *tree_root,
		       u64 root_id, u8 type, u64 ref_id)
{
	struct btrfs_key key;
	int ret;
	struct btrfs_path *path;

	path = btrfs_alloc_path();

	key.objectid = root_id;
	key.type = type;
	key.offset = ref_id;

	ret = btrfs_search_slot(trans, tree_root, &key, path, -1, 1);
	BUG_ON(ret);

	ret = btrfs_del_item(trans, tree_root, path);
	BUG_ON(ret);

	btrfs_free_path(path);
	return ret;
}
#endif

int btrfs_find_root_ref(struct btrfs_root *tree_root,
		   struct btrfs_path *path,
		   u64 root_id, u64 ref_id)
{
	struct btrfs_key key;
	int ret;

	key.objectid = root_id;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = ref_id;

	ret = btrfs_search_slot(NULL, tree_root, &key, path, 0, 0);
	return ret;
}


/*
 * add a btrfs_root_ref item.  type is either BTRFS_ROOT_REF_KEY
 * or BTRFS_ROOT_BACKREF_KEY.
 *
 * The dirid, sequence, name and name_len refer to the directory entry
 * that is referencing the root.
 *
 * For a forward ref, the root_id is the id of the tree referencing
 * the root and ref_id is the id of the subvol  or snapshot.
 *
 * For a back ref the root_id is the id of the subvol or snapshot and
 * ref_id is the id of the tree referencing it.
 */
int btrfs_add_root_ref(struct btrfs_trans_handle *trans,
		       struct btrfs_root *tree_root,
		       u64 root_id, u8 type, u64 ref_id,
		       u64 dirid, u64 sequence,
		       const char *name, int name_len)
{
	struct btrfs_key key;
	int ret;
	struct btrfs_path *path;
	struct btrfs_root_ref *ref;
	struct extent_buffer *leaf;
	unsigned long ptr;


	path = btrfs_alloc_path();

	key.objectid = root_id;
	key.type = type;
	key.offset = ref_id;

	ret = btrfs_insert_empty_item(trans, tree_root, path, &key,
				      sizeof(*ref) + name_len);
	BUG_ON(ret);

	leaf = path->nodes[0];
	ref = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_ref);
	btrfs_set_root_ref_dirid(leaf, ref, dirid);
	btrfs_set_root_ref_sequence(leaf, ref, sequence);
	btrfs_set_root_ref_name_len(leaf, ref, name_len);
	ptr = (unsigned long)(ref + 1);
	write_extent_buffer(leaf, name, ptr, name_len);
	btrfs_mark_buffer_dirty(leaf);

	btrfs_free_path(path);
	return ret;
}
