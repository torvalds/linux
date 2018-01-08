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
#include "print-tree.h"

int btrfs_find_name_in_backref(struct extent_buffer *leaf, int slot,
			       const char *name,
			       int name_len, struct btrfs_inode_ref **ref_ret)
{
	struct btrfs_inode_ref *ref;
	unsigned long ptr;
	unsigned long name_ptr;
	u32 item_size;
	u32 cur_offset = 0;
	int len;

	item_size = btrfs_item_size_nr(leaf, slot);
	ptr = btrfs_item_ptr_offset(leaf, slot);
	while (cur_offset < item_size) {
		ref = (struct btrfs_inode_ref *)(ptr + cur_offset);
		len = btrfs_inode_ref_name_len(leaf, ref);
		name_ptr = (unsigned long)(ref + 1);
		cur_offset += len + sizeof(*ref);
		if (len != name_len)
			continue;
		if (memcmp_extent_buffer(leaf, name, name_ptr, name_len) == 0) {
			if (ref_ret)
				*ref_ret = ref;
			return 1;
		}
	}
	return 0;
}

int btrfs_find_name_in_ext_backref(struct extent_buffer *leaf, int slot,
				   u64 ref_objectid,
				   const char *name, int name_len,
				   struct btrfs_inode_extref **extref_ret)
{
	struct btrfs_inode_extref *extref;
	unsigned long ptr;
	unsigned long name_ptr;
	u32 item_size;
	u32 cur_offset = 0;
	int ref_name_len;

	item_size = btrfs_item_size_nr(leaf, slot);
	ptr = btrfs_item_ptr_offset(leaf, slot);

	/*
	 * Search all extended backrefs in this item. We're only
	 * looking through any collisions so most of the time this is
	 * just going to compare against one buffer. If all is well,
	 * we'll return success and the inode ref object.
	 */
	while (cur_offset < item_size) {
		extref = (struct btrfs_inode_extref *) (ptr + cur_offset);
		name_ptr = (unsigned long)(&extref->name);
		ref_name_len = btrfs_inode_extref_name_len(leaf, extref);

		if (ref_name_len == name_len &&
		    btrfs_inode_extref_parent(leaf, extref) == ref_objectid &&
		    (memcmp_extent_buffer(leaf, name, name_ptr, name_len) == 0)) {
			if (extref_ret)
				*extref_ret = extref;
			return 1;
		}

		cur_offset += ref_name_len + sizeof(*extref);
	}
	return 0;
}

/* Returns NULL if no extref found */
struct btrfs_inode_extref *
btrfs_lookup_inode_extref(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct btrfs_path *path,
			  const char *name, int name_len,
			  u64 inode_objectid, u64 ref_objectid, int ins_len,
			  int cow)
{
	int ret;
	struct btrfs_key key;
	struct btrfs_inode_extref *extref;

	key.objectid = inode_objectid;
	key.type = BTRFS_INODE_EXTREF_KEY;
	key.offset = btrfs_extref_hash(ref_objectid, name, name_len);

	ret = btrfs_search_slot(trans, root, &key, path, ins_len, cow);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret > 0)
		return NULL;
	if (!btrfs_find_name_in_ext_backref(path->nodes[0], path->slots[0],
					    ref_objectid, name, name_len,
					    &extref))
		return NULL;
	return extref;
}

static int btrfs_del_inode_extref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  const char *name, int name_len,
				  u64 inode_objectid, u64 ref_objectid,
				  u64 *index)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_inode_extref *extref;
	struct extent_buffer *leaf;
	int ret;
	int del_len = name_len + sizeof(*extref);
	unsigned long ptr;
	unsigned long item_start;
	u32 item_size;

	key.objectid = inode_objectid;
	key.type = BTRFS_INODE_EXTREF_KEY;
	key.offset = btrfs_extref_hash(ref_objectid, name, name_len);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0)
		ret = -ENOENT;
	if (ret < 0)
		goto out;

	/*
	 * Sanity check - did we find the right item for this name?
	 * This should always succeed so error here will make the FS
	 * readonly.
	 */
	if (!btrfs_find_name_in_ext_backref(path->nodes[0], path->slots[0],
					    ref_objectid,
					    name, name_len, &extref)) {
		btrfs_handle_fs_error(root->fs_info, -ENOENT, NULL);
		ret = -EROFS;
		goto out;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size_nr(leaf, path->slots[0]);
	if (index)
		*index = btrfs_inode_extref_index(leaf, extref);

	if (del_len == item_size) {
		/*
		 * Common case only one ref in the item, remove the
		 * whole item.
		 */
		ret = btrfs_del_item(trans, root, path);
		goto out;
	}

	ptr = (unsigned long)extref;
	item_start = btrfs_item_ptr_offset(leaf, path->slots[0]);

	memmove_extent_buffer(leaf, ptr, ptr + del_len,
			      item_size - (ptr + del_len - item_start));

	btrfs_truncate_item(root->fs_info, path, item_size - del_len, 1);

out:
	btrfs_free_path(path);

	return ret;
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
	int search_ext_refs = 0;
	int del_len = name_len + sizeof(*ref);

	key.objectid = inode_objectid;
	key.offset = ref_objectid;
	key.type = BTRFS_INODE_REF_KEY;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0) {
		ret = -ENOENT;
		search_ext_refs = 1;
		goto out;
	} else if (ret < 0) {
		goto out;
	}
	if (!btrfs_find_name_in_backref(path->nodes[0], path->slots[0],
					name, name_len, &ref)) {
		ret = -ENOENT;
		search_ext_refs = 1;
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
	btrfs_truncate_item(root->fs_info, path, item_size - sub_item_len, 1);
out:
	btrfs_free_path(path);

	if (search_ext_refs) {
		/*
		 * No refs were found, or we could not find the
		 * name in our ref array. Find and remove the extended
		 * inode ref then.
		 */
		return btrfs_del_inode_extref(trans, root, name, name_len,
					      inode_objectid, ref_objectid, index);
	}

	return ret;
}

/*
 * btrfs_insert_inode_extref() - Inserts an extended inode ref into a tree.
 *
 * The caller must have checked against BTRFS_LINK_MAX already.
 */
static int btrfs_insert_inode_extref(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     const char *name, int name_len,
				     u64 inode_objectid, u64 ref_objectid, u64 index)
{
	struct btrfs_inode_extref *extref;
	int ret;
	int ins_len = name_len + sizeof(*extref);
	unsigned long ptr;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct btrfs_item *item;

	key.objectid = inode_objectid;
	key.type = BTRFS_INODE_EXTREF_KEY;
	key.offset = btrfs_extref_hash(ref_objectid, name, name_len);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      ins_len);
	if (ret == -EEXIST) {
		if (btrfs_find_name_in_ext_backref(path->nodes[0],
						   path->slots[0],
						   ref_objectid,
						   name, name_len, NULL))
			goto out;

		btrfs_extend_item(root->fs_info, path, ins_len);
		ret = 0;
	}
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	item = btrfs_item_nr(path->slots[0]);
	ptr = (unsigned long)btrfs_item_ptr(leaf, path->slots[0], char);
	ptr += btrfs_item_size(leaf, item) - ins_len;
	extref = (struct btrfs_inode_extref *)ptr;

	btrfs_set_inode_extref_name_len(path->nodes[0], extref, name_len);
	btrfs_set_inode_extref_index(path->nodes[0], extref, index);
	btrfs_set_inode_extref_parent(path->nodes[0], extref, ref_objectid);

	ptr = (unsigned long)&extref->name;
	write_extent_buffer(path->nodes[0], name, ptr, name_len);
	btrfs_mark_buffer_dirty(path->nodes[0]);

out:
	btrfs_free_path(path);
	return ret;
}

/* Will return 0, -ENOMEM, -EMLINK, or -EEXIST or anything from the CoW path */
int btrfs_insert_inode_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root,
			   const char *name, int name_len,
			   u64 inode_objectid, u64 ref_objectid, u64 index)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_inode_ref *ref;
	unsigned long ptr;
	int ret;
	int ins_len = name_len + sizeof(*ref);

	key.objectid = inode_objectid;
	key.offset = ref_objectid;
	key.type = BTRFS_INODE_REF_KEY;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->leave_spinning = 1;
	path->skip_release_on_error = 1;
	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      ins_len);
	if (ret == -EEXIST) {
		u32 old_size;

		if (btrfs_find_name_in_backref(path->nodes[0], path->slots[0],
					       name, name_len, &ref))
			goto out;

		old_size = btrfs_item_size_nr(path->nodes[0], path->slots[0]);
		btrfs_extend_item(fs_info, path, ins_len);
		ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
				     struct btrfs_inode_ref);
		ref = (struct btrfs_inode_ref *)((unsigned long)ref + old_size);
		btrfs_set_inode_ref_name_len(path->nodes[0], ref, name_len);
		btrfs_set_inode_ref_index(path->nodes[0], ref, index);
		ptr = (unsigned long)(ref + 1);
		ret = 0;
	} else if (ret < 0) {
		if (ret == -EOVERFLOW) {
			if (btrfs_find_name_in_backref(path->nodes[0],
						       path->slots[0],
						       name, name_len, &ref))
				ret = -EEXIST;
			else
				ret = -EMLINK;
		}
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

	if (ret == -EMLINK) {
		struct btrfs_super_block *disk_super = fs_info->super_copy;
		/* We ran out of space in the ref array. Need to
		 * add an extended ref. */
		if (btrfs_super_incompat_flags(disk_super)
		    & BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF)
			ret = btrfs_insert_inode_extref(trans, root, name,
							name_len,
							inode_objectid,
							ref_objectid, index);
	}

	return ret;
}

int btrfs_insert_empty_inode(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root,
			     struct btrfs_path *path, u64 objectid)
{
	struct btrfs_key key;
	int ret;
	key.objectid = objectid;
	key.type = BTRFS_INODE_ITEM_KEY;
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
	if (ret > 0 && location->type == BTRFS_ROOT_ITEM_KEY &&
	    location->offset == (u64)-1 && path->slots[0] != 0) {
		slot = path->slots[0] - 1;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, slot);
		if (found_key.objectid == location->objectid &&
		    found_key.type == location->type) {
			path->slots[0]--;
			return 0;
		}
	}
	return ret;
}
