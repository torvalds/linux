/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#ifndef BTRFS_PRINT_TREE_H
#define BTRFS_PRINT_TREE_H

/* Buffer size to contain tree name and possibly additional data (offset) */
#define BTRFS_ROOT_NAME_BUF_LEN				48

struct extent_buffer;
struct btrfs_key;

void btrfs_print_leaf(const struct extent_buffer *l);
void btrfs_print_tree(const struct extent_buffer *c, bool follow);
const char *btrfs_root_name(const struct btrfs_key *key, char *buf);

#endif
