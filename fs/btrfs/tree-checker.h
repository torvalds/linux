/*
 * Copyright (C) Qu Wenruo 2017.  All rights reserved.
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
 * License along with this program.
 */

#ifndef __BTRFS_TREE_CHECKER__
#define __BTRFS_TREE_CHECKER__

#include "ctree.h"
#include "extent_io.h"

int btrfs_check_leaf(struct btrfs_root *root, struct extent_buffer *leaf);
int btrfs_check_node(struct btrfs_root *root, struct extent_buffer *node);

#endif
