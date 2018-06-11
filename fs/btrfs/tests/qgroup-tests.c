/*
 * Copyright (C) 2013 Facebook.  All rights reserved.
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

#include "btrfs-tests.h"
#include "../ctree.h"
#include "../transaction.h"
#include "../disk-io.h"
#include "../qgroup.h"
#include "../backref.h"

static void init_dummy_trans(struct btrfs_trans_handle *trans)
{
	memset(trans, 0, sizeof(*trans));
	trans->transid = 1;
	INIT_LIST_HEAD(&trans->qgroup_ref_list);
	trans->type = __TRANS_DUMMY;
}

static int insert_normal_tree_ref(struct btrfs_root *root, u64 bytenr,
				  u64 num_bytes, u64 parent, u64 root_objectid)
{
	struct btrfs_trans_handle trans;
	struct btrfs_extent_item *item;
	struct btrfs_extent_inline_ref *iref;
	struct btrfs_tree_block_info *block_info;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	struct btrfs_key ins;
	u32 size = sizeof(*item) + sizeof(*iref) + sizeof(*block_info);
	int ret;

	init_dummy_trans(&trans);

	ins.objectid = bytenr;
	ins.type = BTRFS_EXTENT_ITEM_KEY;
	ins.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path) {
		test_msg("Couldn't allocate path\n");
		return -ENOMEM;
	}

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(&trans, root, path, &ins, size);
	if (ret) {
		test_msg("Couldn't insert ref %d\n", ret);
		btrfs_free_path(path);
		return ret;
	}

	leaf = path->nodes[0];
	item = btrfs_item_ptr(leaf, path->slots[0], struct btrfs_extent_item);
	btrfs_set_extent_refs(leaf, item, 1);
	btrfs_set_extent_generation(leaf, item, 1);
	btrfs_set_extent_flags(leaf, item, BTRFS_EXTENT_FLAG_TREE_BLOCK);
	block_info = (struct btrfs_tree_block_info *)(item + 1);
	btrfs_set_tree_block_level(leaf, block_info, 0);
	iref = (struct btrfs_extent_inline_ref *)(block_info + 1);
	if (parent > 0) {
		btrfs_set_extent_inline_ref_type(leaf, iref,
						 BTRFS_SHARED_BLOCK_REF_KEY);
		btrfs_set_extent_inline_ref_offset(leaf, iref, parent);
	} else {
		btrfs_set_extent_inline_ref_type(leaf, iref, BTRFS_TREE_BLOCK_REF_KEY);
		btrfs_set_extent_inline_ref_offset(leaf, iref, root_objectid);
	}
	btrfs_free_path(path);
	return 0;
}

static int add_tree_ref(struct btrfs_root *root, u64 bytenr, u64 num_bytes,
			u64 parent, u64 root_objectid)
{
	struct btrfs_trans_handle trans;
	struct btrfs_extent_item *item;
	struct btrfs_path *path;
	struct btrfs_key key;
	u64 refs;
	int ret;

	init_dummy_trans(&trans);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path) {
		test_msg("Couldn't allocate path\n");
		return -ENOMEM;
	}

	path->leave_spinning = 1;
	ret = btrfs_search_slot(&trans, root, &key, path, 0, 1);
	if (ret) {
		test_msg("Couldn't find extent ref\n");
		btrfs_free_path(path);
		return ret;
	}

	item = btrfs_item_ptr(path->nodes[0], path->slots[0],
			      struct btrfs_extent_item);
	refs = btrfs_extent_refs(path->nodes[0], item);
	btrfs_set_extent_refs(path->nodes[0], item, refs + 1);
	btrfs_release_path(path);

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_BLOCK_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_TREE_BLOCK_REF_KEY;
		key.offset = root_objectid;
	}

	ret = btrfs_insert_empty_item(&trans, root, path, &key, 0);
	if (ret)
		test_msg("Failed to insert backref\n");
	btrfs_free_path(path);
	return ret;
}

static int remove_extent_item(struct btrfs_root *root, u64 bytenr,
			      u64 num_bytes)
{
	struct btrfs_trans_handle trans;
	struct btrfs_key key;
	struct btrfs_path *path;
	int ret;

	init_dummy_trans(&trans);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path) {
		test_msg("Couldn't allocate path\n");
		return -ENOMEM;
	}
	path->leave_spinning = 1;

	ret = btrfs_search_slot(&trans, root, &key, path, -1, 1);
	if (ret) {
		test_msg("Didn't find our key %d\n", ret);
		btrfs_free_path(path);
		return ret;
	}
	btrfs_del_item(&trans, root, path);
	btrfs_free_path(path);
	return 0;
}

static int remove_extent_ref(struct btrfs_root *root, u64 bytenr,
			     u64 num_bytes, u64 parent, u64 root_objectid)
{
	struct btrfs_trans_handle trans;
	struct btrfs_extent_item *item;
	struct btrfs_path *path;
	struct btrfs_key key;
	u64 refs;
	int ret;

	init_dummy_trans(&trans);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path) {
		test_msg("Couldn't allocate path\n");
		return -ENOMEM;
	}

	path->leave_spinning = 1;
	ret = btrfs_search_slot(&trans, root, &key, path, 0, 1);
	if (ret) {
		test_msg("Couldn't find extent ref\n");
		btrfs_free_path(path);
		return ret;
	}

	item = btrfs_item_ptr(path->nodes[0], path->slots[0],
			      struct btrfs_extent_item);
	refs = btrfs_extent_refs(path->nodes[0], item);
	btrfs_set_extent_refs(path->nodes[0], item, refs - 1);
	btrfs_release_path(path);

	key.objectid = bytenr;
	if (parent) {
		key.type = BTRFS_SHARED_BLOCK_REF_KEY;
		key.offset = parent;
	} else {
		key.type = BTRFS_TREE_BLOCK_REF_KEY;
		key.offset = root_objectid;
	}

	ret = btrfs_search_slot(&trans, root, &key, path, -1, 1);
	if (ret) {
		test_msg("Couldn't find backref %d\n", ret);
		btrfs_free_path(path);
		return ret;
	}
	btrfs_del_item(&trans, root, path);
	btrfs_free_path(path);
	return ret;
}

static int test_no_shared_qgroup(struct btrfs_root *root)
{
	struct btrfs_trans_handle trans;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct ulist *old_roots = NULL;
	struct ulist *new_roots = NULL;
	int ret;

	init_dummy_trans(&trans);

	test_msg("Qgroup basic add\n");
	ret = btrfs_create_qgroup(NULL, fs_info, 5);
	if (ret) {
		test_msg("Couldn't create a qgroup %d\n", ret);
		return ret;
	}

	/*
	 * Since the test trans doesn't havee the complicated delayed refs,
	 * we can only call btrfs_qgroup_account_extent() directly to test
	 * quota.
	 */
	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &old_roots);
	if (ret) {
		ulist_free(old_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = insert_normal_tree_ref(root, 4096, 4096, 0, 5);
	if (ret)
		return ret;

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &new_roots);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, fs_info, 4096, 4096,
					  old_roots, new_roots);
	if (ret) {
		test_msg("Couldn't account space for a qgroup %d\n", ret);
		return ret;
	}

	if (btrfs_verify_qgroup_counts(fs_info, 5, 4096, 4096)) {
		test_msg("Qgroup counts didn't match expected values\n");
		return -EINVAL;
	}
	old_roots = NULL;
	new_roots = NULL;

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &old_roots);
	if (ret) {
		ulist_free(old_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = remove_extent_item(root, 4096, 4096);
	if (ret)
		return -EINVAL;

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &new_roots);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, fs_info, 4096, 4096,
					  old_roots, new_roots);
	if (ret) {
		test_msg("Couldn't account space for a qgroup %d\n", ret);
		return -EINVAL;
	}

	if (btrfs_verify_qgroup_counts(fs_info, 5, 0, 0)) {
		test_msg("Qgroup counts didn't match expected values\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * Add a ref for two different roots to make sure the shared value comes out
 * right, also remove one of the roots and make sure the exclusive count is
 * adjusted properly.
 */
static int test_multiple_refs(struct btrfs_root *root)
{
	struct btrfs_trans_handle trans;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct ulist *old_roots = NULL;
	struct ulist *new_roots = NULL;
	int ret;

	init_dummy_trans(&trans);

	test_msg("Qgroup multiple refs test\n");

	/* We have 5 created already from the previous test */
	ret = btrfs_create_qgroup(NULL, fs_info, 256);
	if (ret) {
		test_msg("Couldn't create a qgroup %d\n", ret);
		return ret;
	}

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &old_roots);
	if (ret) {
		ulist_free(old_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = insert_normal_tree_ref(root, 4096, 4096, 0, 5);
	if (ret)
		return ret;

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &new_roots);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, fs_info, 4096, 4096,
					  old_roots, new_roots);
	if (ret) {
		test_msg("Couldn't account space for a qgroup %d\n", ret);
		return ret;
	}

	if (btrfs_verify_qgroup_counts(fs_info, 5, 4096, 4096)) {
		test_msg("Qgroup counts didn't match expected values\n");
		return -EINVAL;
	}

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &old_roots);
	if (ret) {
		ulist_free(old_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = add_tree_ref(root, 4096, 4096, 0, 256);
	if (ret)
		return ret;

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &new_roots);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, fs_info, 4096, 4096,
					  old_roots, new_roots);
	if (ret) {
		test_msg("Couldn't account space for a qgroup %d\n", ret);
		return ret;
	}

	if (btrfs_verify_qgroup_counts(fs_info, 5, 4096, 0)) {
		test_msg("Qgroup counts didn't match expected values\n");
		return -EINVAL;
	}

	if (btrfs_verify_qgroup_counts(fs_info, 256, 4096, 0)) {
		test_msg("Qgroup counts didn't match expected values\n");
		return -EINVAL;
	}

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &old_roots);
	if (ret) {
		ulist_free(old_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = remove_extent_ref(root, 4096, 4096, 0, 256);
	if (ret)
		return ret;

	ret = btrfs_find_all_roots(&trans, fs_info, 4096, 0, &new_roots);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_msg("Couldn't find old roots: %d\n", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, fs_info, 4096, 4096,
					  old_roots, new_roots);
	if (ret) {
		test_msg("Couldn't account space for a qgroup %d\n", ret);
		return ret;
	}

	if (btrfs_verify_qgroup_counts(fs_info, 256, 0, 0)) {
		test_msg("Qgroup counts didn't match expected values\n");
		return -EINVAL;
	}

	if (btrfs_verify_qgroup_counts(fs_info, 5, 4096, 4096)) {
		test_msg("Qgroup counts didn't match expected values\n");
		return -EINVAL;
	}

	return 0;
}

int btrfs_test_qgroups(void)
{
	struct btrfs_root *root;
	struct btrfs_root *tmp_root;
	int ret = 0;

	root = btrfs_alloc_dummy_root();
	if (IS_ERR(root)) {
		test_msg("Couldn't allocate root\n");
		return PTR_ERR(root);
	}

	root->fs_info = btrfs_alloc_dummy_fs_info();
	if (!root->fs_info) {
		test_msg("Couldn't allocate dummy fs info\n");
		ret = -ENOMEM;
		goto out;
	}
	/* We are using this root as our extent root */
	root->fs_info->extent_root = root;

	/*
	 * Some of the paths we test assume we have a filled out fs_info, so we
	 * just need to add the root in there so we don't panic.
	 */
	root->fs_info->tree_root = root;
	root->fs_info->quota_root = root;
	root->fs_info->quota_enabled = 1;

	/*
	 * Can't use bytenr 0, some things freak out
	 * *cough*backref walking code*cough*
	 */
	root->node = alloc_test_extent_buffer(root->fs_info, 4096);
	if (!root->node) {
		test_msg("Couldn't allocate dummy buffer\n");
		ret = -ENOMEM;
		goto out;
	}
	btrfs_set_header_level(root->node, 0);
	btrfs_set_header_nritems(root->node, 0);
	root->alloc_bytenr += 8192;

	tmp_root = btrfs_alloc_dummy_root();
	if (IS_ERR(tmp_root)) {
		test_msg("Couldn't allocate a fs root\n");
		ret = PTR_ERR(tmp_root);
		goto out;
	}

	tmp_root->root_key.objectid = 5;
	root->fs_info->fs_root = tmp_root;
	ret = btrfs_insert_fs_root(root->fs_info, tmp_root);
	if (ret) {
		test_msg("Couldn't insert fs root %d\n", ret);
		goto out;
	}

	tmp_root = btrfs_alloc_dummy_root();
	if (IS_ERR(tmp_root)) {
		test_msg("Couldn't allocate a fs root\n");
		ret = PTR_ERR(tmp_root);
		goto out;
	}

	tmp_root->root_key.objectid = 256;
	ret = btrfs_insert_fs_root(root->fs_info, tmp_root);
	if (ret) {
		test_msg("Couldn't insert fs root %d\n", ret);
		goto out;
	}

	test_msg("Running qgroup tests\n");
	ret = test_no_shared_qgroup(root);
	if (ret)
		goto out;
	ret = test_multiple_refs(root);
out:
	btrfs_free_dummy_root(root);
	return ret;
}
