/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SCRUB_H
#define BTRFS_SCRUB_H

int btrfs_scrub_dev(struct btrfs_fs_info *fs_info, u64 devid, u64 start,
		    u64 end, struct btrfs_scrub_progress *progress,
		    int readonly, int is_dev_replace);
void btrfs_scrub_pause(struct btrfs_fs_info *fs_info);
void btrfs_scrub_continue(struct btrfs_fs_info *fs_info);
int btrfs_scrub_cancel(struct btrfs_fs_info *info);
int btrfs_scrub_cancel_dev(struct btrfs_device *dev);
int btrfs_scrub_progress(struct btrfs_fs_info *fs_info, u64 devid,
			 struct btrfs_scrub_progress *progress);

/* Temporary declaration, would be deleted later. */
struct scrub_ctx;
struct scrub_sector;
struct scrub_block;
int scrub_find_csum(struct scrub_ctx *sctx, u64 logical, u8 *csum);
int scrub_add_sector_to_rd_bio(struct scrub_ctx *sctx,
			       struct scrub_sector *sector);
void scrub_sector_get(struct scrub_sector *sector);
struct scrub_sector *alloc_scrub_sector(struct scrub_block *sblock, u64 logical);
struct scrub_block *alloc_scrub_block(struct scrub_ctx *sctx,
				     struct btrfs_device *dev,
				     u64 logical, u64 physical,
				     u64 physical_for_dev_replace,
				     int mirror_num);

#endif
