/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SUPER_H
#define BTRFS_SUPER_H

int btrfs_parse_options(struct btrfs_fs_info *info, char *options,
			unsigned long new_flags);
int btrfs_sync_fs(struct super_block *sb, int wait);
char *btrfs_get_subvol_name_from_objectid(struct btrfs_fs_info *fs_info,
					  u64 subvol_objectid);

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
