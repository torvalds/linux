// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include "ctree.h"
#include "fs.h"
#include "messages.h"
#include "inode-item.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "space-info.h"
#include "accessors.h"
#include "extent-tree.h"
#include "file-item.h"

struct btrfs_inode_ref *btrfs_find_name_in_backref(struct extent_buffer *leaf,
						   int slot,
						   const struct fscrypt_str *name)
{
	struct btrfs_inode_ref *ref;
	unsigned long ptr;
	unsigned long name_ptr;
	u32 item_size;
	u32 cur_offset = 0;
	int len;

	item_size = btrfs_item_size(leaf, slot);
	ptr = btrfs_item_ptr_offset(leaf, slot);
	while (cur_offset < item_size) {
		ref = (struct btrfs_inode_ref *)(ptr + cur_offset);
		len = btrfs_inode_ref_name_len(leaf, ref);
		name_ptr = (unsigned long)(ref + 1);
		cur_offset += len + sizeof(*ref);
		if (len != name->len)
			continue;
		if (memcmp_extent_buffer(leaf, name->name, name_ptr,
					 name->len) == 0)
			return ref;
	}
	return NULL;
}

struct btrfs_inode_extref *btrfs_find_name_in_ext_backref(
		struct extent_buffer *leaf, int slot, u64 ref_objectid,
		const struct fscrypt_str *name)
{
	struct btrfs_inode_extref *extref;
	unsigned long ptr;
	unsigned long name_ptr;
	u32 item_size;
	u32 cur_offset = 0;
	int ref_name_len;

	item_size = btrfs_item_size(leaf, slot);
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

		if (ref_name_len == name->len &&
		    btrfs_inode_extref_parent(leaf, extref) == ref_objectid &&
		    (memcmp_extent_buffer(leaf, name->name, name_ptr,
					  name->len) == 0))
			return extref;

		cur_offset += ref_name_len + sizeof(*extref);
	}
	return NULL;
}

/* Returns NULL if no extref found */
struct btrfs_inode_extref *
btrfs_lookup_inode_extref(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  struct btrfs_path *path,
			  const struct fscrypt_str *name,
			  u64 inode_objectid, u64 ref_objectid, int ins_len,
			  int cow)
{
	int ret;
	struct btrfs_key key;

	key.objectid = inode_objectid;
	key.type = BTRFS_INODE_EXTREF_KEY;
	key.offset = btrfs_extref_hash(ref_objectid, name->name, name->len);

	ret = btrfs_search_slot(trans, root, &key, path, ins_len, cow);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret > 0)
		return NULL;
	return btrfs_find_name_in_ext_backref(path->nodes[0], path->slots[0],
					      ref_objectid, name);

}

static int btrfs_del_inode_extref(struct btrfs_trans_handle *trans,
				  struct btrfs_root *root,
				  const struct fscrypt_str *name,
				  u64 inode_objectid, u64 ref_objectid,
				  u64 *index)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_inode_extref *extref;
	struct extent_buffer *leaf;
	int ret;
	int del_len = name->len + sizeof(*extref);
	unsigned long ptr;
	unsigned long item_start;
	u32 item_size;

	key.objectid = inode_objectid;
	key.type = BTRFS_INODE_EXTREF_KEY;
	key.offset = btrfs_extref_hash(ref_objectid, name->name, name->len);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

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
	extref = btrfs_find_name_in_ext_backref(path->nodes[0], path->slots[0],
						ref_objectid, name);
	if (!extref) {
		btrfs_handle_fs_error(root->fs_info, -ENOENT, NULL);
		ret = -EROFS;
		goto out;
	}

	leaf = path->nodes[0];
	item_size = btrfs_item_size(leaf, path->slots[0]);
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

	btrfs_truncate_item(trans, path, item_size - del_len, 1);

out:
	btrfs_free_path(path);

	return ret;
}

int btrfs_del_inode_ref(struct btrfs_trans_handle *trans,
			struct btrfs_root *root, const struct fscrypt_str *name,
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
	int del_len = name->len + sizeof(*ref);

	key.objectid = inode_objectid;
	key.offset = ref_objectid;
	key.type = BTRFS_INODE_REF_KEY;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret > 0) {
		ret = -ENOENT;
		search_ext_refs = 1;
		goto out;
	} else if (ret < 0) {
		goto out;
	}

	ref = btrfs_find_name_in_backref(path->nodes[0], path->slots[0], name);
	if (!ref) {
		ret = -ENOENT;
		search_ext_refs = 1;
		goto out;
	}
	leaf = path->nodes[0];
	item_size = btrfs_item_size(leaf, path->slots[0]);

	if (index)
		*index = btrfs_inode_ref_index(leaf, ref);

	if (del_len == item_size) {
		ret = btrfs_del_item(trans, root, path);
		goto out;
	}
	ptr = (unsigned long)ref;
	sub_item_len = name->len + sizeof(*ref);
	item_start = btrfs_item_ptr_offset(leaf, path->slots[0]);
	memmove_extent_buffer(leaf, ptr, ptr + sub_item_len,
			      item_size - (ptr + sub_item_len - item_start));
	btrfs_truncate_item(trans, path, item_size - sub_item_len, 1);
out:
	btrfs_free_path(path);

	if (search_ext_refs) {
		/*
		 * No refs were found, or we could not find the
		 * name in our ref array. Find and remove the extended
		 * inode ref then.
		 */
		return btrfs_del_inode_extref(trans, root, name,
					      inode_objectid, ref_objectid, index);
	}

	return ret;
}

/*
 * Insert an extended inode ref into a tree.
 *
 * The caller must have checked against BTRFS_LINK_MAX already.
 */
static int btrfs_insert_inode_extref(struct btrfs_trans_handle *trans,
				     struct btrfs_root *root,
				     const struct fscrypt_str *name,
				     u64 inode_objectid, u64 ref_objectid,
				     u64 index)
{
	struct btrfs_inode_extref *extref;
	int ret;
	int ins_len = name->len + sizeof(*extref);
	unsigned long ptr;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;

	key.objectid = inode_objectid;
	key.type = BTRFS_INODE_EXTREF_KEY;
	key.offset = btrfs_extref_hash(ref_objectid, name->name, name->len);

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      ins_len);
	if (ret == -EEXIST) {
		if (btrfs_find_name_in_ext_backref(path->nodes[0],
						   path->slots[0],
						   ref_objectid,
						   name))
			goto out;

		btrfs_extend_item(trans, path, ins_len);
		ret = 0;
	}
	if (ret < 0)
		goto out;

	leaf = path->nodes[0];
	ptr = (unsigned long)btrfs_item_ptr(leaf, path->slots[0], char);
	ptr += btrfs_item_size(leaf, path->slots[0]) - ins_len;
	extref = (struct btrfs_inode_extref *)ptr;

	btrfs_set_inode_extref_name_len(path->nodes[0], extref, name->len);
	btrfs_set_inode_extref_index(path->nodes[0], extref, index);
	btrfs_set_inode_extref_parent(path->nodes[0], extref, ref_objectid);

	ptr = (unsigned long)&extref->name;
	write_extent_buffer(path->nodes[0], name->name, ptr, name->len);
	btrfs_mark_buffer_dirty(trans, path->nodes[0]);

out:
	btrfs_free_path(path);
	return ret;
}

/* Will return 0, -ENOMEM, -EMLINK, or -EEXIST or anything from the CoW path */
int btrfs_insert_inode_ref(struct btrfs_trans_handle *trans,
			   struct btrfs_root *root, const struct fscrypt_str *name,
			   u64 inode_objectid, u64 ref_objectid, u64 index)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_inode_ref *ref;
	unsigned long ptr;
	int ret;
	int ins_len = name->len + sizeof(*ref);

	key.objectid = inode_objectid;
	key.offset = ref_objectid;
	key.type = BTRFS_INODE_REF_KEY;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->skip_release_on_error = 1;
	ret = btrfs_insert_empty_item(trans, root, path, &key,
				      ins_len);
	if (ret == -EEXIST) {
		u32 old_size;
		ref = btrfs_find_name_in_backref(path->nodes[0], path->slots[0],
						 name);
		if (ref)
			goto out;

		old_size = btrfs_item_size(path->nodes[0], path->slots[0]);
		btrfs_extend_item(trans, path, ins_len);
		ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
				     struct btrfs_inode_ref);
		ref = (struct btrfs_inode_ref *)((unsigned long)ref + old_size);
		btrfs_set_inode_ref_name_len(path->nodes[0], ref, name->len);
		btrfs_set_inode_ref_index(path->nodes[0], ref, index);
		ptr = (unsigned long)(ref + 1);
		ret = 0;
	} else if (ret < 0) {
		if (ret == -EOVERFLOW) {
			if (btrfs_find_name_in_backref(path->nodes[0],
						       path->slots[0],
						       name))
				ret = -EEXIST;
			else
				ret = -EMLINK;
		}
		goto out;
	} else {
		ref = btrfs_item_ptr(path->nodes[0], path->slots[0],
				     struct btrfs_inode_ref);
		btrfs_set_inode_ref_name_len(path->nodes[0], ref, name->len);
		btrfs_set_inode_ref_index(path->nodes[0], ref, index);
		ptr = (unsigned long)(ref + 1);
	}
	write_extent_buffer(path->nodes[0], name->name, ptr, name->len);
	btrfs_mark_buffer_dirty(trans, path->nodes[0]);

out:
	btrfs_free_path(path);

	if (ret == -EMLINK) {
		struct btrfs_super_block *disk_super = fs_info->super_copy;
		/* We ran out of space in the ref array. Need to
		 * add an extended ref. */
		if (btrfs_super_incompat_flags(disk_super)
		    & BTRFS_FEATURE_INCOMPAT_EXTENDED_IREF)
			ret = btrfs_insert_inode_extref(trans, root, name,
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

static inline void btrfs_trace_truncate(struct btrfs_inode *inode,
					struct extent_buffer *leaf,
					struct btrfs_file_extent_item *fi,
					u64 offset, int extent_type, int slot)
{
	if (!inode)
		return;
	if (extent_type == BTRFS_FILE_EXTENT_INLINE)
		trace_btrfs_truncate_show_fi_inline(inode, leaf, fi, slot,
						    offset);
	else
		trace_btrfs_truncate_show_fi_regular(inode, leaf, fi, offset);
}

/*
 * Remove inode items from a given root.
 *
 * @trans:		A transaction handle.
 * @root:		The root from which to remove items.
 * @inode:		The inode whose items we want to remove.
 * @control:		The btrfs_truncate_control to control how and what we
 *			are truncating.
 *
 * Remove all keys associated with the inode from the given root that have a key
 * with a type greater than or equals to @min_type. When @min_type has a value of
 * BTRFS_EXTENT_DATA_KEY, only remove file extent items that have an offset value
 * greater than or equals to @new_size. If a file extent item that starts before
 * @new_size and ends after it is found, its length is adjusted.
 *
 * Returns: 0 on success, < 0 on error and NEED_TRUNCATE_BLOCK when @min_type is
 * BTRFS_EXTENT_DATA_KEY and the caller must truncate the last block.
 */
int btrfs_truncate_inode_items(struct btrfs_trans_handle *trans,
			       struct btrfs_root *root,
			       struct btrfs_truncate_control *control)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *fi;
	struct btrfs_key key;
	struct btrfs_key found_key;
	u64 new_size = control->new_size;
	u64 extent_num_bytes = 0;
	u64 extent_offset = 0;
	u64 item_end = 0;
	u32 found_type = (u8)-1;
	int del_item;
	int pending_del_nr = 0;
	int pending_del_slot = 0;
	int extent_type = -1;
	int ret;
	u64 bytes_deleted = 0;
	bool be_nice = false;

	ASSERT(control->inode || !control->clear_extent_range);
	ASSERT(new_size == 0 || control->min_type == BTRFS_EXTENT_DATA_KEY);

	control->last_size = new_size;
	control->sub_bytes = 0;

	/*
	 * For shareable roots we want to back off from time to time, this turns
	 * out to be subvolume roots, reloc roots, and data reloc roots.
	 */
	if (test_bit(BTRFS_ROOT_SHAREABLE, &root->state))
		be_nice = true;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->reada = READA_BACK;

	key.objectid = control->ino;
	key.offset = (u64)-1;
	key.type = (u8)-1;

search_again:
	/*
	 * With a 16K leaf size and 128MiB extents, you can actually queue up a
	 * huge file in a single leaf.  Most of the time that bytes_deleted is
	 * > 0, it will be huge by the time we get here
	 */
	if (be_nice && bytes_deleted > SZ_32M &&
	    btrfs_should_end_transaction(trans)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = btrfs_search_slot(trans, root, &key, path, -1, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		ret = 0;
		/* There are no items in the tree for us to truncate, we're done */
		if (path->slots[0] == 0)
			goto out;
		path->slots[0]--;
	}

	while (1) {
		u64 clear_start = 0, clear_len = 0, extent_start = 0;
		bool refill_delayed_refs_rsv = false;

		fi = NULL;
		leaf = path->nodes[0];
		btrfs_item_key_to_cpu(leaf, &found_key, path->slots[0]);
		found_type = found_key.type;

		if (found_key.objectid != control->ino)
			break;

		if (found_type < control->min_type)
			break;

		item_end = found_key.offset;
		if (found_type == BTRFS_EXTENT_DATA_KEY) {
			fi = btrfs_item_ptr(leaf, path->slots[0],
					    struct btrfs_file_extent_item);
			extent_type = btrfs_file_extent_type(leaf, fi);
			if (extent_type != BTRFS_FILE_EXTENT_INLINE)
				item_end +=
				    btrfs_file_extent_num_bytes(leaf, fi);
			else if (extent_type == BTRFS_FILE_EXTENT_INLINE)
				item_end += btrfs_file_extent_ram_bytes(leaf, fi);

			btrfs_trace_truncate(control->inode, leaf, fi,
					     found_key.offset, extent_type,
					     path->slots[0]);
			item_end--;
		}
		if (found_type > control->min_type) {
			del_item = 1;
		} else {
			if (item_end < new_size)
				break;
			if (found_key.offset >= new_size)
				del_item = 1;
			else
				del_item = 0;
		}

		/* FIXME, shrink the extent if the ref count is only 1 */
		if (found_type != BTRFS_EXTENT_DATA_KEY)
			goto delete;

		control->extents_found++;

		if (extent_type != BTRFS_FILE_EXTENT_INLINE) {
			u64 num_dec;

			clear_start = found_key.offset;
			extent_start = btrfs_file_extent_disk_bytenr(leaf, fi);
			if (!del_item) {
				u64 orig_num_bytes =
					btrfs_file_extent_num_bytes(leaf, fi);
				extent_num_bytes = ALIGN(new_size -
						found_key.offset,
						fs_info->sectorsize);
				clear_start = ALIGN(new_size, fs_info->sectorsize);

				btrfs_set_file_extent_num_bytes(leaf, fi,
							 extent_num_bytes);
				num_dec = (orig_num_bytes - extent_num_bytes);
				if (extent_start != 0)
					control->sub_bytes += num_dec;
				btrfs_mark_buffer_dirty(trans, leaf);
			} else {
				extent_num_bytes =
					btrfs_file_extent_disk_num_bytes(leaf, fi);
				extent_offset = found_key.offset -
					btrfs_file_extent_offset(leaf, fi);

				/* FIXME blocksize != 4096 */
				num_dec = btrfs_file_extent_num_bytes(leaf, fi);
				if (extent_start != 0)
					control->sub_bytes += num_dec;
			}
			clear_len = num_dec;
		} else if (extent_type == BTRFS_FILE_EXTENT_INLINE) {
			/*
			 * We can't truncate inline items that have had
			 * special encodings
			 */
			if (!del_item &&
			    btrfs_file_extent_encryption(leaf, fi) == 0 &&
			    btrfs_file_extent_other_encoding(leaf, fi) == 0 &&
			    btrfs_file_extent_compression(leaf, fi) == 0) {
				u32 size = (u32)(new_size - found_key.offset);

				btrfs_set_file_extent_ram_bytes(leaf, fi, size);
				size = btrfs_file_extent_calc_inline_size(size);
				btrfs_truncate_item(trans, path, size, 1);
			} else if (!del_item) {
				/*
				 * We have to bail so the last_size is set to
				 * just before this extent.
				 */
				ret = BTRFS_NEED_TRUNCATE_BLOCK;
				break;
			} else {
				/*
				 * Inline extents are special, we just treat
				 * them as a full sector worth in the file
				 * extent tree just for simplicity sake.
				 */
				clear_len = fs_info->sectorsize;
			}

			control->sub_bytes += item_end + 1 - new_size;
		}
delete:
		/*
		 * We only want to clear the file extent range if we're
		 * modifying the actual inode's mapping, which is just the
		 * normal truncate path.
		 */
		if (control->clear_extent_range) {
			ret = btrfs_inode_clear_file_extent_range(control->inode,
						  clear_start, clear_len);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				break;
			}
		}

		if (del_item) {
			ASSERT(!pending_del_nr ||
			       ((path->slots[0] + 1) == pending_del_slot));

			control->last_size = found_key.offset;
			if (!pending_del_nr) {
				/* No pending yet, add ourselves */
				pending_del_slot = path->slots[0];
				pending_del_nr = 1;
			} else if (path->slots[0] + 1 == pending_del_slot) {
				/* Hop on the pending chunk */
				pending_del_nr++;
				pending_del_slot = path->slots[0];
			}
		} else {
			control->last_size = new_size;
			break;
		}

		if (del_item && extent_start != 0 && !control->skip_ref_updates) {
			struct btrfs_ref ref = { 0 };

			bytes_deleted += extent_num_bytes;

			btrfs_init_generic_ref(&ref, BTRFS_DROP_DELAYED_REF,
					extent_start, extent_num_bytes, 0,
					root->root_key.objectid);
			btrfs_init_data_ref(&ref, btrfs_header_owner(leaf),
					control->ino, extent_offset,
					root->root_key.objectid, false);
			ret = btrfs_free_extent(trans, &ref);
			if (ret) {
				btrfs_abort_transaction(trans, ret);
				break;
			}
			if (be_nice && btrfs_check_space_for_delayed_refs(fs_info))
				refill_delayed_refs_rsv = true;
		}

		if (found_type == BTRFS_INODE_ITEM_KEY)
			break;

		if (path->slots[0] == 0 ||
		    path->slots[0] != pending_del_slot ||
		    refill_delayed_refs_rsv) {
			if (pending_del_nr) {
				ret = btrfs_del_items(trans, root, path,
						pending_del_slot,
						pending_del_nr);
				if (ret) {
					btrfs_abort_transaction(trans, ret);
					break;
				}
				pending_del_nr = 0;
			}
			btrfs_release_path(path);

			/*
			 * We can generate a lot of delayed refs, so we need to
			 * throttle every once and a while and make sure we're
			 * adding enough space to keep up with the work we are
			 * generating.  Since we hold a transaction here we
			 * can't flush, and we don't want to FLUSH_LIMIT because
			 * we could have generated too many delayed refs to
			 * actually allocate, so just bail if we're short and
			 * let the normal reservation dance happen higher up.
			 */
			if (refill_delayed_refs_rsv) {
				ret = btrfs_delayed_refs_rsv_refill(fs_info,
							BTRFS_RESERVE_NO_FLUSH);
				if (ret) {
					ret = -EAGAIN;
					break;
				}
			}
			goto search_again;
		} else {
			path->slots[0]--;
		}
	}
out:
	if (ret >= 0 && pending_del_nr) {
		int err;

		err = btrfs_del_items(trans, root, path, pending_del_slot,
				      pending_del_nr);
		if (err) {
			btrfs_abort_transaction(trans, err);
			ret = err;
		}
	}

	ASSERT(control->last_size >= new_size);
	if (!ret && control->last_size > new_size)
		control->last_size = new_size;

	btrfs_free_path(path);
	return ret;
}
