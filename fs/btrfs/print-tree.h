/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_PRINT_TREE_H
#define BTRFS_PRINT_TREE_H

void btrfs_print_leaf(struct extent_buffer *l);
void btrfs_print_tree(struct extent_buffer *c, bool follow);

#endif
