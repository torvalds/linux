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

/*
 * The following functions are temporary exports to avoid warning on unused
 * static functions.
 */
struct scrub_stripe;
int init_scrub_stripe(struct btrfs_fs_info *fs_info, struct scrub_stripe *stripe);
int scrub_find_fill_first_stripe(struct btrfs_block_group *bg,
				 struct btrfs_device *dev, u64 physical,
				 int mirror_num, u64 logical_start,
				 u32 logical_len, struct scrub_stripe *stripe);
void scrub_read_endio(struct btrfs_bio *bbio);
void scrub_write_sectors(struct scrub_ctx *sctx,
			struct scrub_stripe *stripe,
			unsigned long write_bitmap, bool dev_replace);

#endif
