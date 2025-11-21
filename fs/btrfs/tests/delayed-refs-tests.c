// SPDX-License-Identifier: GPL-2.0
#include <linux/sizes.h>
#include "btrfs-tests.h"
#include "../transaction.h"
#include "../delayed-ref.h"
#include "../extent-tree.h"

#define FAKE_ROOT_OBJECTID 256
#define FAKE_BYTENR 0
#define FAKE_LEVEL 1
#define FAKE_INO 256
#define FAKE_FILE_OFFSET 0
#define FAKE_PARENT SZ_1M

struct ref_head_check {
	u64 bytenr;
	u64 num_bytes;
	int ref_mod;
	int total_ref_mod;
	int must_insert;
};

struct ref_node_check {
	u64 bytenr;
	u64 num_bytes;
	int ref_mod;
	enum btrfs_delayed_ref_action action;
	u8 type;
	u64 parent;
	u64 root;
	u64 owner;
	u64 offset;
};

static enum btrfs_ref_type ref_type_from_disk_ref_type(u8 type)
{
	if ((type == BTRFS_TREE_BLOCK_REF_KEY) ||
	    (type == BTRFS_SHARED_BLOCK_REF_KEY))
		return BTRFS_REF_METADATA;
	return BTRFS_REF_DATA;
}

static void delete_delayed_ref_head(struct btrfs_trans_handle *trans,
				    struct btrfs_delayed_ref_head *head)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_ref_root *delayed_refs =
		&trans->transaction->delayed_refs;

	spin_lock(&delayed_refs->lock);
	spin_lock(&head->lock);
	btrfs_delete_ref_head(fs_info, delayed_refs, head);
	spin_unlock(&head->lock);
	spin_unlock(&delayed_refs->lock);

	btrfs_delayed_ref_unlock(head);
	btrfs_put_delayed_ref_head(head);
}

static void delete_delayed_ref_node(struct btrfs_delayed_ref_head *head,
				    struct btrfs_delayed_ref_node *node)
{
	rb_erase_cached(&node->ref_node, &head->ref_tree);
	RB_CLEAR_NODE(&node->ref_node);
	if (!list_empty(&node->add_list))
		list_del_init(&node->add_list);
	btrfs_put_delayed_ref(node);
}

static int validate_ref_head(struct btrfs_delayed_ref_head *head,
			     struct ref_head_check *check)
{
	if (head->bytenr != check->bytenr) {
		test_err("invalid bytenr have: %llu want: %llu", head->bytenr,
			 check->bytenr);
		return -EINVAL;
	}

	if (head->num_bytes != check->num_bytes) {
		test_err("invalid num_bytes have: %llu want: %llu",
			 head->num_bytes, check->num_bytes);
		return -EINVAL;
	}

	if (head->ref_mod != check->ref_mod) {
		test_err("invalid ref_mod have: %d want: %d", head->ref_mod,
			 check->ref_mod);
		return -EINVAL;
	}

	if (head->total_ref_mod != check->total_ref_mod) {
		test_err("invalid total_ref_mod have: %d want: %d",
			 head->total_ref_mod, check->total_ref_mod);
		return -EINVAL;
	}

	if (head->must_insert_reserved != check->must_insert) {
		test_err("invalid must_insert have: %d want: %d",
			 head->must_insert_reserved, check->must_insert);
		return -EINVAL;
	}

	return 0;
}

static int validate_ref_node(struct btrfs_delayed_ref_node *node,
			     struct ref_node_check *check)
{
	if (node->bytenr != check->bytenr) {
		test_err("invalid bytenr have: %llu want: %llu", node->bytenr,
			 check->bytenr);
		return -EINVAL;
	}

	if (node->num_bytes != check->num_bytes) {
		test_err("invalid num_bytes have: %llu want: %llu",
			 node->num_bytes, check->num_bytes);
		return -EINVAL;
	}

	if (node->ref_mod != check->ref_mod) {
		test_err("invalid ref_mod have: %d want: %d", node->ref_mod,
			 check->ref_mod);
		return -EINVAL;
	}

	if (node->action != check->action) {
		test_err("invalid action have: %d want: %d", node->action,
			 check->action);
		return -EINVAL;
	}

	if (node->parent != check->parent) {
		test_err("invalid parent have: %llu want: %llu", node->parent,
			 check->parent);
		return -EINVAL;
	}

	if (node->ref_root != check->root) {
		test_err("invalid root have: %llu want: %llu", node->ref_root,
			 check->root);
		return -EINVAL;
	}

	if (node->type != check->type) {
		test_err("invalid type have: %d want: %d", node->type,
			 check->type);
		return -EINVAL;
	}

	if (btrfs_delayed_ref_owner(node) != check->owner) {
		test_err("invalid owner have: %llu want: %llu",
			 btrfs_delayed_ref_owner(node), check->owner);
		return -EINVAL;
	}

	if (btrfs_delayed_ref_offset(node) != check->offset) {
		test_err("invalid offset have: %llu want: %llu",
			 btrfs_delayed_ref_offset(node), check->offset);
		return -EINVAL;
	}

	return 0;
}

static int simple_test(struct btrfs_trans_handle *trans,
		       struct ref_head_check *head_check,
		       struct ref_node_check *node_check)
{
	struct btrfs_delayed_ref_root *delayed_refs =
		&trans->transaction->delayed_refs;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_ref_head *head;
	struct btrfs_delayed_ref_node *node;
	struct btrfs_ref ref = {
		.type = ref_type_from_disk_ref_type(node_check->type),
		.action = node_check->action,
		.parent = node_check->parent,
		.ref_root = node_check->root,
		.bytenr = node_check->bytenr,
		.num_bytes = fs_info->nodesize,
	};
	int ret;

	if (ref.type == BTRFS_REF_METADATA)
		btrfs_init_tree_ref(&ref, node_check->owner, node_check->root,
				    false);
	else
		btrfs_init_data_ref(&ref, node_check->owner, node_check->offset,
				    node_check->root, true);

	if (ref.type == BTRFS_REF_METADATA)
		ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	else
		ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
	if (ret) {
		test_err("failed ref action %d", ret);
		return ret;
	}

	head = btrfs_select_ref_head(fs_info, delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		return -EINVAL;
	}

	ret = -EINVAL;
	if (validate_ref_head(head, head_check))
		goto out;

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	if (validate_ref_node(node, node_check))
		goto out;
	ret = 0;
out:
	btrfs_unselect_ref_head(delayed_refs, head);
	btrfs_destroy_delayed_refs(trans->transaction);
	return ret;
}

/*
 * These are simple tests, make sure that our btrfs_ref's get turned into the
 * appropriate btrfs_delayed_ref_node based on their settings and action.
 */
static int simple_tests(struct btrfs_trans_handle *trans)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct ref_head_check head_check = {
		.bytenr = FAKE_BYTENR,
		.num_bytes = fs_info->nodesize,
		.ref_mod = 1,
		.total_ref_mod = 1,
	};
	struct ref_node_check node_check = {
		.bytenr = FAKE_BYTENR,
		.num_bytes = fs_info->nodesize,
		.ref_mod = 1,
		.action = BTRFS_ADD_DELAYED_REF,
		.type = BTRFS_TREE_BLOCK_REF_KEY,
		.parent = 0,
		.root = FAKE_ROOT_OBJECTID,
		.owner = FAKE_LEVEL,
		.offset = 0,
	};

	if (simple_test(trans, &head_check, &node_check)) {
		test_err("single add tree block failed");
		return -EINVAL;
	}

	node_check.type = BTRFS_EXTENT_DATA_REF_KEY;
	node_check.owner = FAKE_INO;
	node_check.offset = FAKE_FILE_OFFSET;

	if (simple_test(trans, &head_check, &node_check)) {
		test_err("single add extent data failed");
		return -EINVAL;
	}

	node_check.parent = FAKE_PARENT;
	node_check.type = BTRFS_SHARED_BLOCK_REF_KEY;
	node_check.owner = FAKE_LEVEL;
	node_check.offset = 0;

	if (simple_test(trans, &head_check, &node_check)) {
		test_err("single add shared block failed");
		return -EINVAL;
	}

	node_check.type = BTRFS_SHARED_DATA_REF_KEY;
	node_check.owner = FAKE_INO;
	node_check.offset = FAKE_FILE_OFFSET;

	if (simple_test(trans, &head_check, &node_check)) {
		test_err("single add shared data failed");
		return -EINVAL;
	}

	head_check.ref_mod = -1;
	head_check.total_ref_mod = -1;
	node_check.action = BTRFS_DROP_DELAYED_REF;
	node_check.type = BTRFS_TREE_BLOCK_REF_KEY;
	node_check.owner = FAKE_LEVEL;
	node_check.offset = 0;
	node_check.parent = 0;

	if (simple_test(trans, &head_check, &node_check)) {
		test_err("single drop tree block failed");
		return -EINVAL;
	}

	node_check.type = BTRFS_EXTENT_DATA_REF_KEY;
	node_check.owner = FAKE_INO;
	node_check.offset = FAKE_FILE_OFFSET;

	if (simple_test(trans, &head_check, &node_check)) {
		test_err("single drop extent data failed");
		return -EINVAL;
	}

	node_check.parent = FAKE_PARENT;
	node_check.type = BTRFS_SHARED_BLOCK_REF_KEY;
	node_check.owner = FAKE_LEVEL;
	node_check.offset = 0;
	if (simple_test(trans, &head_check, &node_check)) {
		test_err("single drop shared block failed");
		return -EINVAL;
	}

	node_check.type = BTRFS_SHARED_DATA_REF_KEY;
	node_check.owner = FAKE_INO;
	node_check.offset = FAKE_FILE_OFFSET;
	if (simple_test(trans, &head_check, &node_check)) {
		test_err("single drop shared data failed");
		return -EINVAL;
	}

	return 0;
}

/*
 * Merge tests, validate that we do delayed ref merging properly, the ref counts
 * all end up properly, and delayed refs are deleted once they're no longer
 * needed.
 */
static int merge_tests(struct btrfs_trans_handle *trans,
		       enum btrfs_ref_type type)
{
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_ref_head *head = NULL;
	struct btrfs_delayed_ref_node *node;
	struct btrfs_ref ref = {
		.type = type,
		.action = BTRFS_ADD_DELAYED_REF,
		.parent = 0,
		.ref_root = FAKE_ROOT_OBJECTID,
		.bytenr = FAKE_BYTENR,
		.num_bytes = fs_info->nodesize,
	};
	struct ref_head_check head_check = {
		.bytenr = FAKE_BYTENR,
		.num_bytes = fs_info->nodesize,
		.ref_mod = 0,
		.total_ref_mod = 0,
	};
	struct ref_node_check node_check = {
		.bytenr = FAKE_BYTENR,
		.num_bytes = fs_info->nodesize,
		.ref_mod = 2,
		.action = BTRFS_ADD_DELAYED_REF,
		.parent = 0,
		.root = FAKE_ROOT_OBJECTID,
	};
	int ret;

	/*
	 * First add a ref and then drop it, make sure we get a head ref with a
	 * 0 total ref mod and no nodes.
	 */
	if (type == BTRFS_REF_METADATA) {
		node_check.type = BTRFS_TREE_BLOCK_REF_KEY;
		node_check.owner = FAKE_LEVEL;
		btrfs_init_tree_ref(&ref, FAKE_LEVEL, FAKE_ROOT_OBJECTID, false);
	} else {
		node_check.type = BTRFS_EXTENT_DATA_REF_KEY;
		node_check.owner = FAKE_INO;
		node_check.offset = FAKE_FILE_OFFSET;
		btrfs_init_data_ref(&ref, FAKE_INO, FAKE_FILE_OFFSET,
				    FAKE_ROOT_OBJECTID, true);
	}

	if (type == BTRFS_REF_METADATA)
		ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	else
		ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
	if (ret) {
		test_err("failed ref action %d", ret);
		return ret;
	}

	ref.action = BTRFS_DROP_DELAYED_REF;
	if (type == BTRFS_REF_METADATA)
		ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	else
		ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	head = btrfs_select_ref_head(fs_info, &trans->transaction->delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		goto out;
	}

	ret = -EINVAL;
	if (validate_ref_head(head, &head_check)) {
		test_err("single add and drop failed");
		goto out;
	}

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (node) {
		test_err("found node when none should exist");
		goto out;
	}

	delete_delayed_ref_head(trans, head);
	head = NULL;

	/*
	 * Add a ref, then add another ref, make sure we get a head ref with a
	 * 2 total ref mod and 1 node.
	 */
	ref.action = BTRFS_ADD_DELAYED_REF;
	if (type == BTRFS_REF_METADATA)
		ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	else
		ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	if (type == BTRFS_REF_METADATA)
		ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	else
		ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	head = btrfs_select_ref_head(fs_info, &trans->transaction->delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		goto out;
	}

	head_check.ref_mod = 2;
	head_check.total_ref_mod = 2;
	ret = -EINVAL;
	if (validate_ref_head(head, &head_check)) {
		test_err("double add failed");
		goto out;
	}

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	if (validate_ref_node(node, &node_check)) {
		test_err("node check failed");
		goto out;
	}

	delete_delayed_ref_node(head, node);

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (node) {
		test_err("found node when none should exist");
		goto out;
	}
	delete_delayed_ref_head(trans, head);
	head = NULL;

	/* Add two drop refs, make sure they are merged properly. */
	ref.action = BTRFS_DROP_DELAYED_REF;
	if (type == BTRFS_REF_METADATA)
		ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	else
		ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	if (type == BTRFS_REF_METADATA)
		ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	else
		ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	head = btrfs_select_ref_head(fs_info, &trans->transaction->delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		goto out;
	}

	head_check.ref_mod = -2;
	head_check.total_ref_mod = -2;
	ret = -EINVAL;
	if (validate_ref_head(head, &head_check)) {
		test_err("double drop failed");
		goto out;
	}

	node_check.action = BTRFS_DROP_DELAYED_REF;
	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	if (validate_ref_node(node, &node_check)) {
		test_err("node check failed");
		goto out;
	}

	delete_delayed_ref_node(head, node);

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (node) {
		test_err("found node when none should exist");
		goto out;
	}
	delete_delayed_ref_head(trans, head);
	head = NULL;

	/* Add multiple refs, then drop until we go negative again. */
	ref.action = BTRFS_ADD_DELAYED_REF;
	for (int i = 0; i < 10; i++) {
		if (type == BTRFS_REF_METADATA)
			ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
		else
			ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
		if (ret) {
			test_err("failed ref action %d", ret);
			goto out;
		}
	}

	ref.action = BTRFS_DROP_DELAYED_REF;
	for (int i = 0; i < 12; i++) {
		if (type == BTRFS_REF_METADATA)
			ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
		else
			ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
		if (ret) {
			test_err("failed ref action %d", ret);
			goto out;
		}
	}

	head = btrfs_select_ref_head(fs_info, &trans->transaction->delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		ret = -EINVAL;
		goto out;
	}

	head_check.ref_mod = -2;
	head_check.total_ref_mod = -2;
	ret = -EINVAL;
	if (validate_ref_head(head, &head_check)) {
		test_err("double drop failed");
		goto out;
	}

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	if (validate_ref_node(node, &node_check)) {
		test_err("node check failed");
		goto out;
	}

	delete_delayed_ref_node(head, node);

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (node) {
		test_err("found node when none should exist");
		goto out;
	}

	delete_delayed_ref_head(trans, head);
	head = NULL;

	/* Drop multiple refs, then add until we go positive again. */
	ref.action = BTRFS_DROP_DELAYED_REF;
	for (int i = 0; i < 10; i++) {
		if (type == BTRFS_REF_METADATA)
			ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
		else
			ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
		if (ret) {
			test_err("failed ref action %d", ret);
			goto out;
		}
	}

	ref.action = BTRFS_ADD_DELAYED_REF;
	for (int i = 0; i < 12; i++) {
		if (type == BTRFS_REF_METADATA)
			ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
		else
			ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
		if (ret) {
			test_err("failed ref action %d", ret);
			goto out;
		}
	}

	head = btrfs_select_ref_head(fs_info, &trans->transaction->delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		ret = -EINVAL;
		goto out;
	}

	head_check.ref_mod = 2;
	head_check.total_ref_mod = 2;
	ret = -EINVAL;
	if (validate_ref_head(head, &head_check)) {
		test_err("add and drop to positive failed");
		goto out;
	}

	node_check.action = BTRFS_ADD_DELAYED_REF;
	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	if (validate_ref_node(node, &node_check)) {
		test_err("node check failed");
		goto out;
	}

	delete_delayed_ref_node(head, node);

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (node) {
		test_err("found node when none should exist");
		goto out;
	}
	delete_delayed_ref_head(trans, head);
	head = NULL;

	/*
	 * Add a bunch of refs with different roots and parents, then drop them
	 * all, make sure everything is properly merged.
	 */
	ref.action = BTRFS_ADD_DELAYED_REF;
	for (int i = 0; i < 50; i++) {
		if (!(i % 2)) {
			ref.parent = 0;
			ref.ref_root = FAKE_ROOT_OBJECTID + i;
		} else {
			ref.parent = FAKE_PARENT + (i * fs_info->nodesize);
		}
		if (type == BTRFS_REF_METADATA)
			ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
		else
			ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
		if (ret) {
			test_err("failed ref action %d", ret);
			goto out;
		}
	}

	ref.action = BTRFS_DROP_DELAYED_REF;
	for (int i = 0; i < 50; i++) {
		if (!(i % 2)) {
			ref.parent = 0;
			ref.ref_root = FAKE_ROOT_OBJECTID + i;
		} else {
			ref.parent = FAKE_PARENT + (i * fs_info->nodesize);
		}
		if (type == BTRFS_REF_METADATA)
			ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
		else
			ret = btrfs_add_delayed_data_ref(trans, &ref, 0);
		if (ret) {
			test_err("failed ref action %d", ret);
			goto out;
		}
	}

	head = btrfs_select_ref_head(fs_info, &trans->transaction->delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		ret = -EINVAL;
		goto out;
	}

	head_check.ref_mod = 0;
	head_check.total_ref_mod = 0;
	ret = -EINVAL;
	if (validate_ref_head(head, &head_check)) {
		test_err("add and drop multiple failed");
		goto out;
	}

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (node) {
		test_err("found node when none should exist");
		goto out;
	}
	ret = 0;
out:
	if (!IS_ERR_OR_NULL(head))
		btrfs_unselect_ref_head(&trans->transaction->delayed_refs, head);
	btrfs_destroy_delayed_refs(trans->transaction);
	return ret;
}

/*
 * Basic test to validate we always get the add operations first followed by any
 * delete operations.
 */
static int select_delayed_refs_test(struct btrfs_trans_handle *trans)
{
	struct btrfs_delayed_ref_root *delayed_refs =
		&trans->transaction->delayed_refs;
	struct btrfs_fs_info *fs_info = trans->fs_info;
	struct btrfs_delayed_ref_head *head = NULL;
	struct btrfs_delayed_ref_node *node;
	struct btrfs_ref ref = {
		.type = BTRFS_REF_METADATA,
		.action = BTRFS_DROP_DELAYED_REF,
		.parent = 0,
		.ref_root = FAKE_ROOT_OBJECTID,
		.bytenr = FAKE_BYTENR,
		.num_bytes = fs_info->nodesize,
	};
	struct ref_head_check head_check = {
		.bytenr = FAKE_BYTENR,
		.num_bytes = fs_info->nodesize,
		.ref_mod = 0,
		.total_ref_mod = 0,
	};
	struct ref_node_check node_check = {
		.bytenr = FAKE_BYTENR,
		.num_bytes = fs_info->nodesize,
		.ref_mod = 1,
		.action = BTRFS_ADD_DELAYED_REF,
		.type = BTRFS_TREE_BLOCK_REF_KEY,
		.parent = 0,
		.owner = FAKE_LEVEL,
		.offset = 0,
	};
	int ret;

	/* Add the drop first. */
	btrfs_init_tree_ref(&ref, FAKE_LEVEL, FAKE_ROOT_OBJECTID, false);
	ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	if (ret) {
		test_err("failed ref action %d", ret);
		return ret;
	}

	/*
	 * Now add the add, and make it a different root so it's logically later
	 * in the rb tree.
	 */
	ref.action = BTRFS_ADD_DELAYED_REF;
	ref.ref_root = FAKE_ROOT_OBJECTID + 1;
	ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	head = btrfs_select_ref_head(fs_info, delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		ret = -EINVAL;
		head = NULL;
		goto out;
	}

	ret = -EINVAL;
	if (validate_ref_head(head, &head_check)) {
		test_err("head check failed");
		goto out;
	}

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	node_check.root = FAKE_ROOT_OBJECTID + 1;
	if (validate_ref_node(node, &node_check)) {
		test_err("node check failed");
		goto out;
	}
	delete_delayed_ref_node(head, node);

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	node_check.action = BTRFS_DROP_DELAYED_REF;
	node_check.root = FAKE_ROOT_OBJECTID;
	if (validate_ref_node(node, &node_check)) {
		test_err("node check failed");
		goto out;
	}
	delete_delayed_ref_node(head, node);
	delete_delayed_ref_head(trans, head);
	head = NULL;

	/*
	 * Now we're going to do the same thing, but we're going to have an add
	 * that gets deleted because of a merge, and make sure we still have
	 * another add in place.
	 */
	ref.action = BTRFS_DROP_DELAYED_REF;
	ref.ref_root = FAKE_ROOT_OBJECTID;
	ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	ref.action = BTRFS_ADD_DELAYED_REF;
	ref.ref_root = FAKE_ROOT_OBJECTID + 1;
	ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	ref.action = BTRFS_DROP_DELAYED_REF;
	ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	ref.action = BTRFS_ADD_DELAYED_REF;
	ref.ref_root = FAKE_ROOT_OBJECTID + 2;
	ret = btrfs_add_delayed_tree_ref(trans, &ref, NULL);
	if (ret) {
		test_err("failed ref action %d", ret);
		goto out;
	}

	head = btrfs_select_ref_head(fs_info, delayed_refs);
	if (IS_ERR_OR_NULL(head)) {
		if (IS_ERR(head))
			test_err("failed to select delayed ref head: %ld",
				 PTR_ERR(head));
		else
			test_err("failed to find delayed ref head");
		ret = -EINVAL;
		head = NULL;
		goto out;
	}

	ret = -EINVAL;
	if (validate_ref_head(head, &head_check)) {
		test_err("head check failed");
		goto out;
	}

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	node_check.action = BTRFS_ADD_DELAYED_REF;
	node_check.root = FAKE_ROOT_OBJECTID + 2;
	if (validate_ref_node(node, &node_check)) {
		test_err("node check failed");
		goto out;
	}
	delete_delayed_ref_node(head, node);

	spin_lock(&head->lock);
	node = btrfs_select_delayed_ref(head);
	spin_unlock(&head->lock);
	if (!node) {
		test_err("failed to select delayed ref");
		goto out;
	}

	node_check.action = BTRFS_DROP_DELAYED_REF;
	node_check.root = FAKE_ROOT_OBJECTID;
	if (validate_ref_node(node, &node_check)) {
		test_err("node check failed");
		goto out;
	}
	delete_delayed_ref_node(head, node);
	ret = 0;
out:
	if (head)
		btrfs_unselect_ref_head(delayed_refs, head);
	btrfs_destroy_delayed_refs(trans->transaction);
	return ret;
}

int btrfs_test_delayed_refs(u32 sectorsize, u32 nodesize)
{
	struct btrfs_transaction *transaction;
	struct btrfs_trans_handle trans;
	struct btrfs_fs_info *fs_info;
	int ret;

	test_msg("running delayed refs tests");

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}
	transaction = kmalloc(sizeof(*transaction), GFP_KERNEL);
	if (!transaction) {
		test_std_err(TEST_ALLOC_TRANSACTION);
		ret = -ENOMEM;
		goto out_free_fs_info;
	}
	btrfs_init_dummy_trans(&trans, fs_info);
	btrfs_init_dummy_transaction(transaction, fs_info);
	trans.transaction = transaction;

	ret = simple_tests(&trans);
	if (!ret) {
		test_msg("running delayed refs merge tests on metadata refs");
		ret = merge_tests(&trans, BTRFS_REF_METADATA);
	}

	if (!ret) {
		test_msg("running delayed refs merge tests on data refs");
		ret = merge_tests(&trans, BTRFS_REF_DATA);
	}

	if (!ret)
		ret = select_delayed_refs_test(&trans);

	kfree(transaction);
out_free_fs_info:
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}
