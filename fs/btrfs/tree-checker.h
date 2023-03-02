/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Qu Wenruo 2017.  All rights reserved.
 */

#ifndef BTRFS_TREE_CHECKER_H
#define BTRFS_TREE_CHECKER_H

#include <uapi/linux/btrfs_tree.h>

struct extent_buffer;
struct btrfs_chunk;

/* All the extra info needed to verify the parentness of a tree block. */
struct btrfs_tree_parent_check {
	/*
	 * The owner check against the tree block.
	 *
	 * Can be 0 to skip the owner check.
	 */
	u64 owner_root;

	/*
	 * Expected transid, can be 0 to skip the check, but such skip
	 * should only be utlized for backref walk related code.
	 */
	u64 transid;

	/*
	 * The expected first key.
	 *
	 * This check can be skipped if @has_first_key is false, such skip
	 * can happen for case where we don't have the parent node key,
	 * e.g. reading the tree root, doing backref walk.
	 */
	struct btrfs_key first_key;
	bool has_first_key;

	/* The expected level. Should always be set. */
	u8 level;
};

/*
 * Comprehensive leaf checker.
 * Will check not only the item pointers, but also every possible member
 * in item data.
 */
int btrfs_check_leaf_full(struct extent_buffer *leaf);

/*
 * Less strict leaf checker.
 * Will only check item pointers, not reading item data.
 */
int btrfs_check_leaf_relaxed(struct extent_buffer *leaf);
int btrfs_check_node(struct extent_buffer *node);

int btrfs_check_chunk_valid(struct extent_buffer *leaf,
			    struct btrfs_chunk *chunk, u64 logical);
int btrfs_check_eb_owner(const struct extent_buffer *eb, u64 root_owner);

#endif
