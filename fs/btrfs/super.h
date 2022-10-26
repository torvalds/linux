/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SUPER_H
#define BTRFS_SUPER_H

int btrfs_parse_options(struct btrfs_fs_info *info, char *options,
			unsigned long new_flags);
int btrfs_sync_fs(struct super_block *sb, int wait);
char *btrfs_get_subvol_name_from_objectid(struct btrfs_fs_info *fs_info,
					  u64 subvol_objectid);

#endif
