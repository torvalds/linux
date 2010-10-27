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

static int find_name_in_backref(struct btrfs_path *path, const char *name,
			 int name_len, struct btrfs_inode_ref **ref_ret)
{
	struct extent_buffer *leaf;
	struct btrfs_inode_ref *ref;
	unsigned long ptr;
	unsigned long name_ptr;
	u32 item_size;
	u32 cur_offset = 0;
	int len;

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	ptr = btrfs_item_ptr_offset(leaf, path->slots[0]);
	while (cur_offset < item_size) {
		ref = (struct btrfs_inode_ref *)(ptr + cur_offset);
		len = btrfs_inode_ref_name_len(leaf, ref);
		name_ptr = (unsigned long)(ref + 1);
		cur_offset += len + sizeof(*ref);
		if (len != name_len)
			continue;
		if (memcmp_extent_buffer(leaf, name, name_ptr, name_len) == 0) {
			*ref_ret = ref;
			return 1;
		}
	}
	return 0;
}

struct btrfs_inode_ref *
btrfs_lookup_inode_ref(struct btrfs_trans_handle *trans,
			struct btrfs_root *root,
			struct btrfs_path *path,
			const char *name, int name_len,
			u64 inode_objectid, u64 ref_objectid, int mod)
{
	struct btrfs_key key;
	struct btrfs_inode_ref *ref;
	int ins_len = mod < 0 ? -1 : 0;
	int cow = mod != 0;
	int ret;

	key.objectid = inode_objectid;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = ref_objectid;

	ret = btrfs_search_slot(trans, root, &key, path, ins_len, cow);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret > 0)
		return NULL;
	if (!find_name_in_backref(path, name, name_len, &ref))
		return NULL;
	return ref;
}

int btrfs_del_inode_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   const char *name, int name_len,
			   u64 inode_objectid, u64 ref_objectid, u64 *index)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_inode_ref *ref;
	struct extent_buffer *leaf;
	unsigned long ptr;
	unsigned long item_start;
	u32 item_size;
	u32 sub_item_len;
	int ret;
	int del_len = name_len + sizeof(*ref);

	key.objectid = inode_objectid;
	key.offset = ref_objectid;
	btrfs_set_key_type(&key, BTRFS_INODE_REF_KEY);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0) {
		ret = -ENOENT;
		goto out;
	} else if (ret < 0) {
		goto out;
	}
	if (!find_name_in_backref(path, name, name_len, &ref)) {
		ret = -ENOENT;
		goto out;
	}
	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);

	if (index)
		*index = btrfs_inode_ref_index(leaf, ref);

	if (del_len == item_size) {
		ret = btrfs_del_item(trans, root, path);
		goto out;
	}
	ptr = (unsigned long)ref;
	sub_item_len = name_len + sizeof(*ref);
	item_start = btrfs_item_ptr_offset(leaf, path->slots[0]);
	memmove_extent_buffer(leaf, ptr, ptr + sub_item_len,
			      item_size - (ptr + sub_item_len - item_start));
	ret = btrfs_truncate_item(trans, root, path,
				  item_size - sub_item_len, 1);
	BUG_ON(ret);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_insert_inode_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   const char *name, int name_len,
			   u64 inode_objectid, u64 ref_objectid, u64 index)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_inode_ref *ref;
	unsigned long ptr;
	int ret;
	int ins_len = name_len + sizeof(*ref);

	key.objectid = inode_objectid;
	key.offset = ref_objectid;
	btrfs_set_key_type(&key, BTRFS_INODE_REF_KEY);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      ins_len);
	if (ret == -EEXIST) {
		u32 old_size;

		if (find_name_in_backref(path, name, name_len, &ref))
			goto out;

		old_size = btrfs_item_size_nr(path->nodes[0], path->slots[0]);
		ret = btrfs_extend_item(trans, root, path, ins_len);
		BUG_ON(ret);
		ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
				     struct btrfs_inode_ref);
		ref = (struct btrfs_inode_ref *)((unsigned long)ref + old_size);
		btrfs_set_inode_ref_name_len(path->nodes[0], ref, name_len);
		btrfs_set_inode_ref_index(path->nodes[0], ref, index);
		ptr = (unsigned long)(ref + 1);
		ret = 0;
	} else if (ret < 0) {
		if (ret == -EOVERFLOW)
			ret = -EMLINK;
		goto out;
	} else {
		ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
				     struct btrfs_inode_ref);
		btrfs_set_inode_ref_name_len(path->nodes[0], ref, name_len);
		btrfs_set_inode_ref_index(path->nodes[0], ref, index);
		ptr = (unsigned long)(ref + 1);
	}
	write_extent_buffer(path->nodes[0], name, ptr, name_len);
	btrfs_mark_buffer_dirty(path->nodes[0]);

out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_insert_empty_inode(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid)
{
	struct btrfs_key key;
	int ret;
	key.objectid = objectid;
	btrfs_set_key_type(&key, BTRFS_INODE_ITEM_KEY);
	key.offset = 0;

	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      sizeof(struct btrfs_inode_item));
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
	struct extent_buffer *leaf;
	struct btrfs_key found_key;

	ret = btrfs_search_slot(trans, root, location, path, ins_len, cow);
	if (ret > 0 && btrfs_key_type(location) == BTRFS_ROOT_ITEM_KEY &&
	    location->offset == (u64)-1 && path->slots[0] != 0) {
		slot = path->slots[0] - 1;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.objectid == location->objectid &&
		    btrfs_key_type(&found_key) == btrfs_key_type(location)) {
			path->slots[0]--;
			return 0;
		}
	}
	return ret;
}
