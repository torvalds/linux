/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_INODE_MAP_H
#define BTRFS_INODE_MAP_H

void btrfs_init_free_iyes_ctl(struct btrfs_root *root);
void btrfs_unpin_free_iyes(struct btrfs_root *root);
void btrfs_return_iyes(struct btrfs_root *root, u64 objectid);
int btrfs_find_free_iyes(struct btrfs_root *root, u64 *objectid);
int btrfs_save_iyes_cache(struct btrfs_root *root,
			 struct btrfs_trans_handle *trans);

int btrfs_find_free_objectid(struct btrfs_root *root, u64 *objectid);
int btrfs_find_highest_objectid(struct btrfs_root *root, u64 *objectid);

#endif
