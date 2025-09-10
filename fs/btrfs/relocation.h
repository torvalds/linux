/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_RELOCATION_H
#define BTRFS_RELOCATION_H

#include <linux/types.h>

struct extent_buffer;
struct btrfs_fs_info;
struct btrfs_root;
struct btrfs_trans_handle;
struct btrfs_ordered_extent;
struct btrfs_pending_snapshot;

int btrfs_relocate_block_group(struct btrfs_fs_info *fs_info, u64 group_start,
			       bool verbose);
int btrfs_init_reloc_root(struct btrfs_trans_handle *trans, struct btrfs_root *root);
int btrfs_update_reloc_root(struct btrfs_trans_handle *trans,
			    struct btrfs_root *root);
int btrfs_recover_relocation(struct btrfs_fs_info *fs_info);
int btrfs_reloc_clone_csums(struct btrfs_ordered_extent *ordered);
int btrfs_reloc_cow_block(struct btrfs_trans_handle *trans,
			  struct btrfs_root *root,
			  const struct extent_buffer *buf,
			  struct extent_buffer *cow);
void btrfs_reloc_pre_snapshot(struct btrfs_pending_snapshot *pending,
			      u64 *bytes_to_reserve);
int btrfs_reloc_post_snapshot(struct btrfs_trans_handle *trans,
			      struct btrfs_pending_snapshot *pending);
int btrfs_should_cancel_balance(const struct btrfs_fs_info *fs_info);
struct btrfs_root *find_reloc_root(struct btrfs_fs_info *fs_info, u64 bytenr);
bool btrfs_should_ignore_reloc_root(const struct btrfs_root *root);
u64 btrfs_get_reloc_bg_bytenr(const struct btrfs_fs_info *fs_info);

#endif
