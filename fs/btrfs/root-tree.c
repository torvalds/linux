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

#include <linux/uuid.h>
#include "ctree.h"
#include "transaction.h"
#include "disk-io.h"
#include "print-tree.h"

/*
 * Read a root item from the tree. In case we detect a root item smaller then
 * sizeof(root_item), we know it's an old version of the root structure and
 * initialize all new fields to zero. The same happens if we detect mismatching
 * generation numbers as then we know the root was once mounted with an older
 * kernel that was not aware of the root item structure change.
 */
void btrfs_read_root_item(struct btrfs_root *root,
			 struct extent_buffer *eb, int slot,
			 struct btrfs_root_item *item)
{
	uuid_le uuid;
	int len;
	int need_reset = 0;

	len = btrfs_item_size_nr(eb, slot);
	read_extent_buffer(eb, item, btrfs_item_ptr_offset(eb, slot),
			min_t(int, len, (int)sizeof(*item)));
	if (len < sizeof(*item))
		need_reset = 1;
	if (!need_reset && btrfs_root_generation(item)
		!= btrfs_root_generation_v2(item)) {
		if (btrfs_root_generation_v2(item) != 0) {
			printk(KERN_WARNING "btrfs: mismatching "
					"generation and generation_v2 "
					"found in root item. This root "
					"was probably mounted with an "
					"older kernel. Resetting all "
					"new fields.\n");
		}
		need_reset = 1;
	}
	if (need_reset) {
		memset(&item->generation_v2, 0,
			sizeof(*item) - offsetof(struct btrfs_root_item,
					generation_v2));

		uuid_le_gen(&uuid);
		memcpy(item->uuid, uuid.b, BTRFS_UUID_SIZE);
	}
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
	if (!path)
		return -ENOMEM;
	ret = btrfs_search_slot(NULL, root, &search_key, path, 0, 0);
	if (ret < 0)
		goto out;

	BUG_ON(ret == 0);
	if (path->slots[0] == 0) {
		ret = 1;
		goto out;
	}
	l = path->nodes[0];
	slot = path->slots[0] - 1;
	btrfs_item_key_to_cpu(l, &found_key, slot);
	if (found_key.objectid != objectid ||
	    found_key.type != BTRFS_ROOT_ITEM_KEY) {
		ret = 1;
		goto out;
	}
	if (item)
		btrfs_read_root_item(root, l, slot, item);
	if (key)
		memcpy(key, &found_key, sizeof(found_key));

	ret = 0;
out:
	btrfs_free_path(path);
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
	struct btrfs_path *path;
	struct extent_buffer *l;
	int ret;
	int slot;
	unsigned long ptr;
	int old_len;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ret = btrfs_search_slot(trans, root, key, path, 0, 1);
	if (ret < 0)
		goto out_abort;

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
		if (ret < 0)
			goto out_abort;
		ret = btrfs_del_item(trans, root, path);
		if (ret < 0)
			goto out_abort;
		btrfs_release_path(path);
		ret = btrfs_insert_empty_item(trans, root, path,
				key, sizeof(*item));
		if (ret < 0)
			goto out_abort;
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

out_abort:
	btrfs_abort_transaction(trans, root, ret);
	goto out;
}

int btrfs_insert_root(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		      struct btrfs_key *key, struct btrfs_root_item *item)
{
	/*
	 * Make sure generation v1 and v2 match. See update_root for details.
	 */
	btrfs_set_root_generation_v2(item, btrfs_root_generation(item));
	return btrfs_insert_item(trans, root, key, item, sizeof(*item));
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
		btrfs_release_path(path);
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

int btrfs_find_orphan_roots(struct btrfs_root *tree_root)
{
	struct extent_buffer *leaf;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_key root_key;
	struct btrfs_root *root;
	int err = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = BTRFS_ORPHAN_OBJECTID;
	key.type = BTRFS_ORPHAN_ITEM_KEY;
	key.offset = 0;

	root_key.type = BTRFS_ROOT_ITEM_KEY;
	root_key.offset = (u64)-1;

	while (1) {
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

		root_key.objectid = key.offset;
		key.offset++;

		root = btrfs_read_fs_root_no_name(tree_root->fs_info,
						  &root_key);
		if (!IS_ERR(root))
			continue;

		ret = PTR_ERR(root);
		if (ret != -ENOENT) {
			err = ret;
			break;
		}

		ret = btrfs_find_dead_roots(tree_root, root_key.objectid);
		if (ret) {
			err = ret;
			break;
		}
	}

	btrfs_free_path(path);
	return err;
}

/* drop the root item for 'key' from 'root' */
int btrfs_del_root(struct btrfs_trans_handle *trans, struct btrfs_root *root,
		   struct btrfs_key *key)
{
	struct btrfs_path *path;
	int ret;
	struct btrfs_root_item *ri;
	struct extent_buffer *leaf;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	ret = btrfs_search_slot(trans, root, key, path, -1, 1);
	if (ret < 0)
		goto out;

	BUG_ON(ret != 0);
	leaf = path->nodes[0];
	ri = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_root_item);

	ret = btrfs_del_item(trans, root, path);
out:
	btrfs_free_path(path);
	return ret;
}

int btrfs_del_root_ref(struct btrfs_trans_handle *trans,
		       struct btrfs_root *tree_root,
		       u64 root_id, u64 ref_id, u64 dirid, u64 *sequence,
		       const char *name, int name_len)

{
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
	BUG_ON(ret < 0);
	if (ret == 0) {
		leaf = path->nodes[0];
		ref = btrfs_item_ptr(leaf, path->slots[0],
				     struct btrfs_root_ref);

		WARN_ON(btrfs_root_ref_dirid(leaf, ref) != dirid);
		WARN_ON(btrfs_root_ref_name_len(leaf, ref) != name_len);
		ptr = (unsigned long)(ref + 1);
		WARN_ON(memcmp_extent_buffer(leaf, name, ptr, name_len));
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
 *
 * Will return 0, -ENOMEM, or anything from the CoW path
 */
int btrfs_add_root_ref(struct btrfs_trans_handle *trans,
		       struct btrfs_root *tree_root,
		       u64 root_id, u64 ref_id, u64 dirid, u64 sequence,
		       const char *name, int name_len)
{
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
		btrfs_abort_transaction(trans, tree_root, ret);
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
	u64 inode_flags = le64_to_cpu(root_item->inode.flags);

	if (!(inode_flags & BTRFS_INODE_ROOT_ITEM_INIT)) {
		inode_flags |= BTRFS_INODE_ROOT_ITEM_INIT;
		root_item->inode.flags = cpu_to_le64(inode_flags);
		root_item->flags = 0;
		root_item->byte_limit = 0;
	}
}

void btrfs_update_root_times(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root)
{
	struct btrfs_root_item *item = &root->root_item;
	struct timespec ct = CURRENT_TIME;

	spin_lock(&root->root_times_lock);
	item->ctransid = trans->transid;
	item->ctime.sec = cpu_to_le64(ct.tv_sec);
	item->ctime.nsec = cpu_to_le64(ct.tv_nsec);
	spin_unlock(&root->root_times_lock);
}
