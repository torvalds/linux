/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_DISCARD_H
#define BTRFS_DISCARD_H

#include <linux/sizes.h>

struct btrfs_fs_info;
struct btrfs_discard_ctl;
struct btrfs_block_group;

/* Discard size limits */
#define BTRFS_ASYNC_DISCARD_DEFAULT_MAX_SIZE		(SZ_64M)
#define BTRFS_ASYNC_DISCARD_MAX_FILTER			(SZ_1M)
#define BTRFS_ASYNC_DISCARD_MIN_FILTER			(SZ_32K)

/* List operations */
void btrfs_discard_check_filter(struct btrfs_block_group *block_group, u64 bytes);

/* Work operations */
void btrfs_discard_cancel_work(struct btrfs_discard_ctl *discard_ctl,
			       struct btrfs_block_group *block_group);
void btrfs_discard_queue_work(struct btrfs_discard_ctl *discard_ctl,
			      struct btrfs_block_group *block_group);
void btrfs_discard_schedule_work(struct btrfs_discard_ctl *discard_ctl,
				 bool override);
bool btrfs_run_discard_work(struct btrfs_discard_ctl *discard_ctl);

/* Update operations */
void btrfs_discard_calc_delay(struct btrfs_discard_ctl *discard_ctl);
void btrfs_discard_update_discardable(struct btrfs_block_group *block_group);

/* Setup/cleanup operations */
void btrfs_discard_punt_unused_bgs_list(struct btrfs_fs_info *fs_info);
void btrfs_discard_resume(struct btrfs_fs_info *fs_info);
void btrfs_discard_stop(struct btrfs_fs_info *fs_info);
void btrfs_discard_init(struct btrfs_fs_info *fs_info);
void btrfs_discard_cleanup(struct btrfs_fs_info *fs_info);

#endif
