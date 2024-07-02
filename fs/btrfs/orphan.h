/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_ORPHAN_H
#define BTRFS_ORPHAN_H

#include <linux/types.h>

struct btrfs_trans_handle;
struct btrfs_root;

int btrfs_insert_orphan_item(struct btrfs_trans_handle *trans,
			     struct btrfs_root *root, u64 offset);
int btrfs_del_orphan_item(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root, u64 offset);

#endif
