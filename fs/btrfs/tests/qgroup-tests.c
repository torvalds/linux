// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Facebook.  All rights reserved.
 */

#include <linux/types.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../transaction.h"
#include "../disk-io.h"
#include "../qgroup.h"
#include "../backref.h"

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

	btrfs_init_dummy_trans(&trans, NULL);

	ins.objectid = bytenr;
	ins.type = BTRFS_EXTENT_ITEM_KEY;
	ins.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path) {
		test_std_err(TEST_ALLOC_ROOT);
		return -ENOMEM;
	}

	path->leave_spinning = 1;
	ret = btrfs_insert_empty_item(&trans, root, path, &ins, size);
	if (ret) {
		test_err("couldn't insert ref %d", ret);
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

	btrfs_init_dummy_trans(&trans, NULL);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path) {
		test_std_err(TEST_ALLOC_ROOT);
		return -ENOMEM;
	}

	path->leave_spinning = 1;
	ret = btrfs_search_slot(&trans, root, &key, path, 0, 1);
	if (ret) {
		test_err("couldn't find extent ref");
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
		test_err("failed to insert backref");
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

	btrfs_init_dummy_trans(&trans, NULL);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path) {
		test_std_err(TEST_ALLOC_ROOT);
		return -ENOMEM;
	}
	path->leave_spinning = 1;

	ret = btrfs_search_slot(&trans, root, &key, path, -1, 1);
	if (ret) {
		test_err("didn't find our key %d", ret);
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

	btrfs_init_dummy_trans(&trans, NULL);

	key.objectid = bytenr;
	key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = num_bytes;

	path = btrfs_alloc_path();
	if (!path) {
		test_std_err(TEST_ALLOC_ROOT);
		return -ENOMEM;
	}

	path->leave_spinning = 1;
	ret = btrfs_search_slot(&trans, root, &key, path, 0, 1);
	if (ret) {
		test_err("couldn't find extent ref");
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
		test_err("couldn't find backref %d", ret);
		btrfs_free_path(path);
		return ret;
	}
	btrfs_del_item(&trans, root, path);
	btrfs_free_path(path);
	return ret;
}

static int test_no_shared_qgroup(struct btrfs_root *root,
		u32 sectorsize, u32 nodesize)
{
	struct btrfs_trans_handle trans;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct ulist *old_roots = NULL;
	struct ulist *new_roots = NULL;
	int ret;

	btrfs_init_dummy_trans(&trans, fs_info);

	test_msg("running qgroup add/remove tests");
	ret = btrfs_create_qgroup(&trans, BTRFS_FS_TREE_OBJECTID);
	if (ret) {
		test_err("couldn't create a qgroup %d", ret);
		return ret;
	}

	/*
	 * Since the test trans doesn't have the complicated delayed refs,
	 * we can only call btrfs_qgroup_account_extent() directly to test
	 * quota.
	 */
	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &old_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = insert_normal_tree_ref(root, nodesize, nodesize, 0,
				BTRFS_FS_TREE_OBJECTID);
	if (ret)
		return ret;

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &new_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, nodesize, nodesize, old_roots,
					  new_roots);
	if (ret) {
		test_err("couldn't account space for a qgroup %d", ret);
		return ret;
	}

	if (btrfs_verify_qgroup_counts(fs_info, BTRFS_FS_TREE_OBJECTID,
				nodesize, nodesize)) {
		test_err("qgroup counts didn't match expected values");
		return -EINVAL;
	}
	old_roots = NULL;
	new_roots = NULL;

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &old_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = remove_extent_item(root, nodesize, nodesize);
	if (ret)
		return -EINVAL;

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &new_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, nodesize, nodesize, old_roots,
					  new_roots);
	if (ret) {
		test_err("couldn't account space for a qgroup %d", ret);
		return -EINVAL;
	}

	if (btrfs_verify_qgroup_counts(fs_info, BTRFS_FS_TREE_OBJECTID, 0, 0)) {
		test_err("qgroup counts didn't match expected values");
		return -EINVAL;
	}

	return 0;
}

/*
 * Add a ref for two different roots to make sure the shared value comes out
 * right, also remove one of the roots and make sure the exclusive count is
 * adjusted properly.
 */
static int test_multiple_refs(struct btrfs_root *root,
		u32 sectorsize, u32 nodesize)
{
	struct btrfs_trans_handle trans;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct ulist *old_roots = NULL;
	struct ulist *new_roots = NULL;
	int ret;

	btrfs_init_dummy_trans(&trans, fs_info);

	test_msg("running qgroup multiple refs test");

	/*
	 * We have BTRFS_FS_TREE_OBJECTID created already from the
	 * previous test.
	 */
	ret = btrfs_create_qgroup(&trans, BTRFS_FIRST_FREE_OBJECTID);
	if (ret) {
		test_err("couldn't create a qgroup %d", ret);
		return ret;
	}

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &old_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = insert_normal_tree_ref(root, nodesize, nodesize, 0,
				BTRFS_FS_TREE_OBJECTID);
	if (ret)
		return ret;

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &new_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, nodesize, nodesize, old_roots,
					  new_roots);
	if (ret) {
		test_err("couldn't account space for a qgroup %d", ret);
		return ret;
	}

	if (btrfs_verify_qgroup_counts(fs_info, BTRFS_FS_TREE_OBJECTID,
				       nodesize, nodesize)) {
		test_err("qgroup counts didn't match expected values");
		return -EINVAL;
	}

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &old_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = add_tree_ref(root, nodesize, nodesize, 0,
			BTRFS_FIRST_FREE_OBJECTID);
	if (ret)
		return ret;

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &new_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, nodesize, nodesize, old_roots,
					  new_roots);
	if (ret) {
		test_err("couldn't account space for a qgroup %d", ret);
		return ret;
	}

	if (btrfs_verify_qgroup_counts(fs_info, BTRFS_FS_TREE_OBJECTID,
					nodesize, 0)) {
		test_err("qgroup counts didn't match expected values");
		return -EINVAL;
	}

	if (btrfs_verify_qgroup_counts(fs_info, BTRFS_FIRST_FREE_OBJECTID,
					nodesize, 0)) {
		test_err("qgroup counts didn't match expected values");
		return -EINVAL;
	}

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &old_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = remove_extent_ref(root, nodesize, nodesize, 0,
				BTRFS_FIRST_FREE_OBJECTID);
	if (ret)
		return ret;

	ret = btrfs_find_all_roots(&trans, fs_info, nodesize, 0, &new_roots,
			false);
	if (ret) {
		ulist_free(old_roots);
		ulist_free(new_roots);
		test_err("couldn't find old roots: %d", ret);
		return ret;
	}

	ret = btrfs_qgroup_account_extent(&trans, nodesize, nodesize, old_roots,
					  new_roots);
	if (ret) {
		test_err("couldn't account space for a qgroup %d", ret);
		return ret;
	}

	if (btrfs_verify_qgroup_counts(fs_info, BTRFS_FIRST_FREE_OBJECTID,
					0, 0)) {
		test_err("qgroup counts didn't match expected values");
		return -EINVAL;
	}

	if (btrfs_verify_qgroup_counts(fs_info, BTRFS_FS_TREE_OBJECTID,
					nodesize, nodesize)) {
		test_err("qgroup counts didn't match expected values");
		return -EINVAL;
	}

	return 0;
}

int btrfs_test_qgroups(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info = NULL;
	struct btrfs_root *root;
	struct btrfs_root *tmp_root;
	int ret = 0;

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(root)) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = PTR_ERR(root);
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
	set_bit(BTRFS_FS_QUOTA_ENABLED, &fs_info->flags);

	/*
	 * Can't use bytenr 0, some things freak out
	 * *cough*backref walking code*cough*
	 */
	root->node = alloc_test_extent_buffer(root->fs_info, nodesize);
	if (IS_ERR(root->node)) {
		test_err("couldn't allocate dummy buffer");
		ret = PTR_ERR(root->node);
		goto out;
	}
	btrfs_set_header_level(root->node, 0);
	btrfs_set_header_nritems(root->node, 0);
	root->alloc_bytenr += 2 * nodesize;

	tmp_root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(tmp_root)) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = PTR_ERR(tmp_root);
		goto out;
	}

	tmp_root->root_key.objectid = BTRFS_FS_TREE_OBJECTID;
	root->fs_info->fs_root = tmp_root;
	ret = btrfs_insert_fs_root(root->fs_info, tmp_root);
	if (ret) {
		test_err("couldn't insert fs root %d", ret);
		goto out;
	}
	btrfs_put_root(tmp_root);

	tmp_root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(tmp_root)) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = PTR_ERR(tmp_root);
		goto out;
	}

	tmp_root->root_key.objectid = BTRFS_FIRST_FREE_OBJECTID;
	ret = btrfs_insert_fs_root(root->fs_info, tmp_root);
	if (ret) {
		test_err("couldn't insert fs root %d", ret);
		goto out;
	}
	btrfs_put_root(tmp_root);

	test_msg("running qgroup tests");
	ret = test_no_shared_qgroup(root, sectorsize, nodesize);
	if (ret)
		goto out;
	ret = test_multiple_refs(root, sectorsize, nodesize);
out:
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}
