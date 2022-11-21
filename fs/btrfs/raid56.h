/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Fusion-io  All rights reserved.
 * Copyright (C) 2012 Intel Corp. All rights reserved.
 */

#ifndef BTRFS_RAID56_H
#define BTRFS_RAID56_H

static inline int nr_parity_stripes(const struct map_lookup *map)
{
	if (map->type & BTRFS_BLOCK_GROUP_RAID5)
		return 1;
	else if (map->type & BTRFS_BLOCK_GROUP_RAID6)
		return 2;
	else
		return 0;
}

static inline int nr_data_stripes(const struct map_lookup *map)
{
	return map->num_stripes - nr_parity_stripes(map);
}
#define RAID5_P_STRIPE ((u64)-2)
#define RAID6_Q_STRIPE ((u64)-1)

#define is_parity_stripe(x) (((x) == RAID5_P_STRIPE) ||		\
			     ((x) == RAID6_Q_STRIPE))

struct btrfs_raid_bio;
struct btrfs_device;

int raid56_parity_recover(struct bio *bio, struct btrfs_io_context *bioc,
			  u64 stripe_len, int mirror_num, int generic_io);
int raid56_parity_write(struct bio *bio, struct btrfs_io_context *bioc,
			u64 stripe_len);

void raid56_add_scrub_pages(struct btrfs_raid_bio *rbio, struct page *page,
			    u64 logical);

struct btrfs_raid_bio *raid56_parity_alloc_scrub_rbio(struct bio *bio,
				struct btrfs_io_context *bioc, u64 stripe_len,
				struct btrfs_device *scrub_dev,
				unsigned long *dbitmap, int stripe_nsectors);
void raid56_parity_submit_scrub_rbio(struct btrfs_raid_bio *rbio);

struct btrfs_raid_bio *
raid56_alloc_missing_rbio(struct bio *bio, struct btrfs_io_context *bioc,
			  u64 length);
void raid56_submit_missing_rbio(struct btrfs_raid_bio *rbio);

int btrfs_alloc_stripe_hash_table(struct btrfs_fs_info *info);
void btrfs_free_stripe_hash_table(struct btrfs_fs_info *info);

#endif
