/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STRATO AG 2011.  All rights reserved.
 */

#ifndef BTRFS_CHECK_INTEGRITY_H
#define BTRFS_CHECK_INTEGRITY_H

#ifdef CONFIG_BTRFS_FS_CHECK_INTEGRITY
void btrfsic_submit_bio(struct bio *bio);
int btrfsic_submit_bio_wait(struct bio *bio);
#else
#define btrfsic_submit_bio submit_bio
#define btrfsic_submit_bio_wait submit_bio_wait
#endif

int btrfsic_mount(struct btrfs_fs_info *fs_info,
		  struct btrfs_fs_devices *fs_devices,
		  int including_extent_data, u32 print_mask);
void btrfsic_unmount(struct btrfs_fs_devices *fs_devices);

#endif
