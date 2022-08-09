// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/err.h>
#include <linux/uuid.h>
#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "print-tree.h"
#include "qgroup.h"
#include "space-info.h"

/*
 * Read a root item from the tree. In case we detect a root item smaller then
 * sizeof(root_item), we know it's an old version of the root structure and
 * initialize all new fields to zero. The same happens if we detect mismatching
 * generation numbers as then we know the root was once mounted with an older
 * kernel that was not aware of the root item structure change.
 */
static void btrfs_read_root_item(struct extent_buffer *eb, int slot,
				struct btrfs_root_item *item)
{
	u32 len;
	int need_reset = 0;

	len = btrfs_item_size_nr(eb, slot);
	read_extent_buffer(eb, item, btrfs_item_ptr_offset(eb, slot),
			   min_t(u32, len, sizeof(*item)));
	if (len < sizeof(*item))
		need_reset = 1;
	if (!need_reset && btrfs_root_generation(item)
		!= btrfs_root_generation_v2(item)) {
		if (btrfs_root_generation_v2(item) != 0) {
			btrfs_warn(eb->fs_info,
					"mismatching generation and generation_v2 found in root item. This root was probably mounted with an older kernel. Resetting all new fields.");
		}
		need_reset = 1;
	}
	if (need_reset) {
		/* Clear all members from generation_v2 onwards. */
		memset_startat(item, 0, generation_v2);
		generate_random_guid(item->uuid);
	}
}

/*
 * btrfs_find_root - lookup the root by the key.
 * root: the root of the root tree
 * search_key: the key to search
 * path: the path we search
 * root_item: the root item of the tree we look for
 * root_key: the root key of the tree we look for
 *
 * If ->offset of 'search_key' is -1ULL, it means we are not sure the offset
 * of the search key, just lookup the root with the highest offset for a
 * given objectid.
 *
 * If we find something return 0, otherwise > 0, < 0 on error.
 */
int btrfs_find_root(struct btrfs_root *root, const struct btrfs_key *search_key,
		    struct btrfs_path *path, struct btrfs_root_item *root_item,
		    struct btrfs_key *root_key)
{
	struct btrfs_key found_key;
	struct extent_buffer *l;
	int ret;
	int slot;

	ret = btrfs_search_slot(NULL, root, search_key, path, 0, 0);
	if (ret < 0)
		return ret;

	if (search_key->offset != -1ULL) {	/* the search key is exact */
		if (ret > 0)
			goto out;
	} else {
		BUG_ON(ret == 0);		/* Logical error */
		if (path->slots[0] == 0)
			goto out;
		path->slots[0]--;
		ret = 0;
	}

	l = path->nodes[0];
	slot = path->slots[0];

	btrfs_item_key_to_cpu(l, &found_key, slot);
	if (found_key.objectid != search_key->objectid ||
	    found_key.type != BTRFS_ROOT_ITEM_KEY) {
		ret = 1;
		goto out;
	}

	if (root_item)
		btrfs_read_root_item(l, slot, root_item);
	if (root_key)
		memcpy(root_key, &found_key, sizeof(found_key));
out:
	btrfs_release_path(path);
	return ret;
}

void btrfs_set_root_node(struct btrfs_root_item *item,
			 struct extent_buffer *node)
{
	btrfs_set_root_bytenr(item, node->start);
	btrfs_set_root_level(item, btrfs_header_level(node));
	btrfs_set_root_generation(item, btrfs_header_generation(node));
}

/*
 * copy the data in 'item' into the btree
 */
int btrfs_update_root(struct btrfs_trans_handle *trans, struct btrfs_root
		      *root, struct btrfs_key *key, struct btrfs_root_item
		      *item)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_path *path;
	struct extent_buffer *l;
	int ret;
	int slot;
	unsigned long ptr;
	u32 old_len;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, key, path, 0, 1);
	if (ret < 0)
		goto out;

	if (ret > 0) {
		btrfs_crit(fs_info,
			"unable to find root key (%llu %u %llu) in tree %llu",
			key->objectid, key->type, key->offset,
			root->root_key.objectid);
		ret = -EUCLEAN;
		btrfs_abort_transaction(trans, ret);
		goto out;
	}

	l = path->nodes[0];
	slot = path->slots[0];
	ptr = btrfs_item_ptr_offset(l, slot);
	old_len = btrfs_item_size_nr(l, slot);

	/*
	 * If this is the first time we update the root item which originated
	 * from an older kernel, we need to enlarge the item size to make room
	 * for the added fields.
	 */
	if (old_len < sizeof(*item)) {
		btrfs_release_path(path);
		ret = btrfs_search_slot(trans, root, key, path,
				-1, 1);
		if (ret < 0) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}

		ret = btrfs_del_item(trans, root, path);
		if (ret < 0) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
		btrfs_release_path(path);
		ret = btrfs_insert_empty_item(trans, root, path,
				key, sizeof(*item));
		if (ret < 0) {
			btrfs_abort_transaction(trans, ret);
			goto out;
		}
		l = path->nodes[0];
		slot = path->slots[0];
		ptr = btrfs_item_ptr_offset(l, slot);
	}

	/*
	 * Update generation_v2 so at the next mount we know the new root
	 * fields are valid.
	 */
	btrfs_set_root_generation_v2(item, btrfs_root_generation(item));

	write_extent_buffer(l, item, ptr, sizeof(*item));
	btrfs_mark_buffer_dirty(path->nodes[0]);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_insert_root(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      const struct btrfs_key *key, struct btrfs_root_item *item)
{
	/*
	 * Make sure generation v1 and v2 match. See update_root for details.
	 */
	btrfs_set_root_generation_v2(item, btrfs_root_generation(item));
	return btrfs_insert_item(trans, root, key, item, sizeof(*item));
}

int btrfs_find_orphan_roots(struct btrfs_fs_info *fs_info)
{
	struct btrfs_root *tree_root = fs_info->tree_root;
	struct extent_buffer *leaf;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root *root;
	int err = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_ORPHAN_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = 0;

	while (1) {
		u64 root_objectid;

		ret = btrfs_search_slot(NULL, tree_root, &key, path, 0, 0);
		if (ret < 0) {
			err = ret;
			break;
		}

		leaf = path->nodes[0];
		if (path->slots[0] >= btrfs_header_nritems(leaf)) {
			ret = btrfs_next_leaf(tree_root, path);
			if (ret < 0)
				err = ret;
			if (ret != 0)
				break;
			leaf = path->nodes[0];
		}

		btrfs_item_key_to_cpu(leaf, &key, path->slots[0]);
		btrfs_release_path(path);

		if (key.objectid != BTRFS_ORPHAN_OBJECTID ||
		    key.type != BTRFS_ORPHAN_ITEM_KEY)
			break;

		root_objectid = key.offset;
		key.offset++;

		root = btrfs_get_fs_root(fs_info, root_objectid, false);
		err = PTR_ERR_OR_ZERO(root);
		if (err && err != -ENOENT) {
			break;
		} else if (err == -ENOENT) {
			struct btrfs_trans_handle *trans;

			btrfs_release_path(path);

			trans = btrfs_join_transaction(tree_root);
			if (IS_ERR(trans)) {
				err = PTR_ERR(trans);
				btrfs_handle_fs_error(fs_info, err,
					    "Failed to start trans to delete orphan item");
				break;
			}
			err = btrfs_del_orphan_item(trans, tree_root,
						    root_objectid);
			btrfs_end_transaction(trans);
			if (err) {
				btrfs_handle_fs_error(fs_info, err,
					    "Failed to delete root orphan item");
				break;
			}
			continue;
		}

		WARN_ON(!test_bit(BTRFS_ROOT_ORPHAN_ITEM_INSERTED, &root->state));
		if (btrfs_root_refs(&root->root_item) == 0) {
			set_bit(BTRFS_ROOT_DEAD_TREE, &root->state);
			btrfs_add_dead_root(root);
		}
		btrfs_put_root(root);
	}

	btrfs_free_path(path);
	return err;
}

/* drop the root item for 'key' from the tree root */
int btrfs_del_root(struct btrfs_trans_handle *trans,
		   const struct btrfs_key *key)
{
	struct btrfs_root *root = trans->fs_info->tree_root;
	struct btrfs_path *path;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	ret = btrfs_search_slot(trans, root, key, path, -1, 1);
	if (ret < 0)
		goto out;

	BUG_ON(ret != 0);

	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_del_root_ref(struct btrfs_trans_handle *trans, u64 root_id,
		       u64 ref_id, u64 dirid, u64 *sequence, const char *name,
		       int name_len)

{
	struct btrfs_root *tree_root = trans->fs_info->tree_root;
	struct btrfs_path *path;
	struct btrfs_root_ref *ref;
	struct extent_buffer *leaf;
	struct btrfs_key key;
	unsigned long ptr;
	int err = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = root_id;
	key.type = BTRFS_ROOT_BACKREF_KEY;
	key.offset = ref_id;
again:
	ret = btrfs_search_slot(trans, tree_root, &key, path, -1, 1);
	if (ret < 0)
		goto out;
	if (ret == 0) {
		leaf = path->nodes[0];
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_root_ref);
		ptr = (unsigned long)(ref + 1);
		if ((btrfs_root_ref_dirid(leaf, ref) != dirid) ||
		    (btrfs_root_ref_name_len(leaf, ref) != name_len) ||
		    memcmp_extent_buffer(leaf, name, ptr, name_len)) {
			err = -ENOENT;
			goto out;
		}
		*sequence = btrfs_root_ref_sequence(leaf, ref);

		ret = btrfs_del_item(trans, tree_root, path);
		if (ret) {
			err = ret;
			goto out;
		}
	} else
		err = -ENOENT;

	if (key.type == BTRFS_ROOT_BACKREF_KEY) {
		btrfs_release_path(path);
		key.objectid = ref_id;
		key.type = BTRFS_ROOT_REF_KEY;
		key.offset = root_id;
		goto again;
	}

out:
	btrfs_free_path(path);
	return err;
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
 *
 * Will return 0, -ENOMEM, or anything from the CoW path
 */
int btrfs_add_root_ref(struct btrfs_trans_handle *trans, u64 root_id,
		       u64 ref_id, u64 dirid, u64 sequence, const char *name,
		       int name_len)
{
	struct btrfs_root *tree_root = trans->fs_info->tree_root;
	struct btrfs_key key;
	int ret;
	struct btrfs_path *path;
	struct btrfs_root_ref *ref;
	struct extent_buffer *leaf;
	unsigned long ptr;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = root_id;
	key.type = BTRFS_ROOT_BACKREF_KEY;
	key.offset = ref_id;
again:
	ret = btrfs_insert_empty_item(trans, tree_root, path, &key,
				      sizeof(*ref) + name_len);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_free_path(path);
		return ret;
	}

	leaf = path->nodes[0];
	ref = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_ref);
	btrfs_set_root_ref_dirid(leaf, ref, dirid);
	btrfs_set_root_ref_sequence(leaf, ref, sequence);
	btrfs_set_root_ref_name_len(leaf, ref, name_len);
	ptr = (unsigned long)(ref + 1);
	write_extent_buffer(leaf, name, ptr, name_len);
	btrfs_mark_buffer_dirty(leaf);

	if (key.type == BTRFS_ROOT_BACKREF_KEY) {
		btrfs_release_path(path);
		key.objectid = ref_id;
		key.type = BTRFS_ROOT_REF_KEY;
		key.offset = root_id;
		goto again;
	}

	btrfs_free_path(path);
	return 0;
}

/*
 * Old btrfs forgets to init root_item->flags and root_item->byte_limit
 * for subvolumes. To work around this problem, we steal a bit from
 * root_item->inode_item->flags, and use it to indicate if those fields
 * have been properly initialized.
 */
void btrfs_check_and_init_root_item(struct btrfs_root_item *root_item)
{
	u64 inode_flags = btrfs_stack_inode_flags(&root_item->inode);

	if (!(inode_flags & BTRFS_INODE_ROOT_ITEM_INIT)) {
		inode_flags |= BTRFS_INODE_ROOT_ITEM_INIT;
		btrfs_set_stack_inode_flags(&root_item->inode, inode_flags);
		btrfs_set_root_flags(root_item, 0);
		btrfs_set_root_limit(root_item, 0);
	}
}

void btrfs_update_root_times(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root)
{
	struct btrfs_root_item *item = &root->root_item;
	struct timespec64 ct;

	ktime_get_real_ts64(&ct);
	spin_lock(&root->root_item_lock);
	btrfs_set_root_ctransid(item, trans->transid);
	btrfs_set_stack_timespec_sec(&item->ctime, ct.tv_sec);
	btrfs_set_stack_timespec_nsec(&item->ctime, ct.tv_nsec);
	spin_unlock(&root->root_item_lock);
}

/*
 * btrfs_subvolume_reserve_metadata() - reserve space for subvolume operation
 * root: the root of the parent directory
 * rsv: block reservation
 * items: the number of items that we need do reservation
 * use_global_rsv: allow fallback to the global block reservation
 *
 * This function is used to reserve the space for snapshot/subvolume
 * creation and deletion. Those operations are different with the
 * common file/directory operations, they change two fs/file trees
 * and root tree, the number of items that the qgroup reserves is
 * different with the free space reservation. So we can not use
 * the space reservation mechanism in start_transaction().
 */
int btrfs_subvolume_reserve_metadata(struct btrfs_root *root,
				     struct btrfs_block_rsv *rsv, int items,
				     bool use_global_rsv)
{
	u64 qgroup_num_bytes = 0;
	u64 num_bytes;
	int ret;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_block_rsv *global_rsv = &fs_info->global_block_rsv;

	if (test_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags)) {
		/* One for parent inode, two for dir entries */
		qgroup_num_bytes = 3 * fs_info->nodesize;
		ret = btrfs_qgroup_reserve_meta_prealloc(root,
				qgroup_num_bytes, true);
		if (ret)
			return ret;
	}

	num_bytes = btrfs_calc_insert_metadata_size(fs_info, items);
	rsv->space_info = btrfs_find_space_info(fs_info,
					    BTRFS_BLOCK_GROUP_METADATA);
	ret = btrfs_block_rsv_add(root, rsv, num_bytes,
				  BTRFS_RESERVE_FLUSH_ALL);

	if (ret == -ENOSPC && use_global_rsv)
		ret = btrfs_block_rsv_migrate(global_rsv, rsv, num_bytes, true);

	if (ret && qgroup_num_bytes)
		btrfs_qgroup_free_meta_prealloc(root, qgroup_num_bytes);

	if (!ret) {
		spin_lock(&rsv->lock);
		rsv->qgroup_rsv_reserved += qgroup_num_bytes;
		spin_unlock(&rsv->lock);
	}
	return ret;
}

void btrfs_subvolume_release_metadata(struct btrfs_root *root,
				      struct btrfs_block_rsv *rsv)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 qgroup_to_release;

	btrfs_block_rsv_release(fs_info, rsv, (u64)-1, &qgroup_to_release);
	btrfs_qgroup_convert_reserved_meta(root, qgroup_to_release);
}
