/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_UUID_TREE_H
#define BTRFS_UUID_TREE_H

#include <linux/types.h>

struct btrfs_trans_handle;
struct btrfs_fs_info;

int btrfs_uuid_tree_add(struct btrfs_trans_handle *trans, const u8 *uuid, u8 type,
			u64 subid);
int btrfs_uuid_tree_remove(struct btrfs_trans_handle *trans, const u8 *uuid, u8 type,
			u64 subid);
int btrfs_uuid_tree_iterate(struct btrfs_fs_info *fs_info);
int btrfs_create_uuid_tree(struct btrfs_fs_info *fs_info);
int btrfs_uuid_scan_kthread(void *data);

#endif
