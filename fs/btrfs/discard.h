// SPDX-License-Identifier: GPL-2.0

#ifndef BTRFS_DISCARD_H
#define BTRFS_DISCARD_H

struct btrfs_fs_info;
struct btrfs_discard_ctl;
struct btrfs_block_group;

/* Work operations */
void btrfs_discard_cancel_work(struct btrfs_discard_ctl *discard_ctl,
			       struct btrfs_block_group *block_group);
void btrfs_discard_queue_work(struct btrfs_discard_ctl *discard_ctl,
			      struct btrfs_block_group *block_group);
void btrfs_discard_schedule_work(struct btrfs_discard_ctl *discard_ctl,
				 bool override);
bool btrfs_run_discard_work(struct btrfs_discard_ctl *discard_ctl);

/* Setup/cleanup operations */
void btrfs_discard_punt_unused_bgs_list(struct btrfs_fs_info *fs_info);
void btrfs_discard_resume(struct btrfs_fs_info *fs_info);
void btrfs_discard_stop(struct btrfs_fs_info *fs_info);
void btrfs_discard_init(struct btrfs_fs_info *fs_info);
void btrfs_discard_cleanup(struct btrfs_fs_info *fs_info);

#endif
