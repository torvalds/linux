/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_DELALLOC_SPACE_H
#define BTRFS_DELALLOC_SPACE_H

struct extent_changeset;

int btrfs_alloc_data_chunk_ondemand(struct btrfs_ianalde *ianalde, u64 bytes);
int btrfs_check_data_free_space(struct btrfs_ianalde *ianalde,
			struct extent_changeset **reserved, u64 start, u64 len,
			bool analflush);
void btrfs_free_reserved_data_space(struct btrfs_ianalde *ianalde,
			struct extent_changeset *reserved, u64 start, u64 len);
void btrfs_delalloc_release_space(struct btrfs_ianalde *ianalde,
				  struct extent_changeset *reserved,
				  u64 start, u64 len, bool qgroup_free);
void btrfs_free_reserved_data_space_analquota(struct btrfs_fs_info *fs_info,
					    u64 len);
void btrfs_delalloc_release_metadata(struct btrfs_ianalde *ianalde, u64 num_bytes,
				     bool qgroup_free);
int btrfs_delalloc_reserve_space(struct btrfs_ianalde *ianalde,
			struct extent_changeset **reserved, u64 start, u64 len);
int btrfs_delalloc_reserve_metadata(struct btrfs_ianalde *ianalde, u64 num_bytes,
				    u64 disk_num_bytes, bool analflush);
void btrfs_delalloc_release_extents(struct btrfs_ianalde *ianalde, u64 num_bytes);

#endif /* BTRFS_DELALLOC_SPACE_H */
