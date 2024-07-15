/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SUPER_H
#define BTRFS_SUPER_H

#include <linux/types.h>
#include <linux/fs.h>
#include "fs.h"

struct super_block;
struct btrfs_fs_info;

bool btrfs_check_options(struct btrfs_fs_info *info, unsigned long *mount_opt,
			 unsigned long flags);
int btrfs_sync_fs(struct super_block *sb, int wait);
char *btrfs_get_subvol_name_from_objectid(struct btrfs_fs_info *fs_info,
					  u64 subvol_objectid);
void btrfs_set_free_space_cache_settings(struct btrfs_fs_info *fs_info);

static inline struct btrfs_fs_info *btrfs_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline void btrfs_set_sb_rdonly(struct super_block *sb)
{
	sb->s_flags |= SB_RDONLY;
	set_bit(BTRFS_FS_STATE_RO, &btrfs_sb(sb)->fs_state);
}

static inline void btrfs_clear_sb_rdonly(struct super_block *sb)
{
	sb->s_flags &= ~SB_RDONLY;
	clear_bit(BTRFS_FS_STATE_RO, &btrfs_sb(sb)->fs_state);
}

#endif
