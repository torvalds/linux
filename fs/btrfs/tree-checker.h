/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Qu Wenruo 2017.  All rights reserved.
 */

#ifndef BTRFS_TREE_CHECKER_H
#define BTRFS_TREE_CHECKER_H

#include "ctree.h"
#include "extent_io.h"

/*
 * Comprehensive leaf checker.
 * Will check yest only the item pointers, but also every possible member
 * in item data.
 */
int btrfs_check_leaf_full(struct extent_buffer *leaf);

/*
 * Less strict leaf checker.
 * Will only check item pointers, yest reading item data.
 */
int btrfs_check_leaf_relaxed(struct extent_buffer *leaf);
int btrfs_check_yesde(struct extent_buffer *yesde);

int btrfs_check_chunk_valid(struct extent_buffer *leaf,
			    struct btrfs_chunk *chunk, u64 logical);

#endif
