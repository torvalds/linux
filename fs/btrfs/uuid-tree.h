/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_UUID_TREE_H
#define BTRFS_UUID_TREE_H

int btrfs_uuid_tree_add(struct btrfs_trans_handle *trans, u8 *uuid, u8 type,
			u64 subid);
int btrfs_uuid_tree_remove(struct btrfs_trans_handle *trans, u8 *uuid, u8 type,
			u64 subid);
int btrfs_uuid_tree_iterate(struct btrfs_fs_info *fs_info);

#endif
