/*
 * Copyright (C) 2012 Fusion-io  All rights reserved.
 * Copyright (C) 2012 Intel Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __BTRFS_RAID56__
#define __BTRFS_RAID56__
static inline int nr_parity_stripes(struct map_lookup *map)
{
	if (map->type & BTRFS_BLOCK_GROUP_RAID5)
		return 1;
	else if (map->type & BTRFS_BLOCK_GROUP_RAID6)
		return 2;
	else
		return 0;
}

static inline int nr_data_stripes(struct map_lookup *map)
{
	return map->num_stripes - nr_parity_stripes(map);
}
#define RAID5_P_STRIPE ((u64)-2)
#define RAID6_Q_STRIPE ((u64)-1)

#define is_parity_stripe(x) (((x) == RAID5_P_STRIPE) ||		\
			     ((x) == RAID6_Q_STRIPE))

struct btrfs_raid_bio;
struct btrfs_device;

int raid56_parity_recover(struct btrfs_root *root, struct bio *bio,
			  struct btrfs_bio *bbio, u64 stripe_len,
			  int mirror_num, int generic_io);
int raid56_parity_write(struct btrfs_root *root, struct bio *bio,
			       struct btrfs_bio *bbio, u64 stripe_len);

void raid56_add_scrub_pages(struct btrfs_raid_bio *rbio, struct page *page,
			    u64 logical);

struct btrfs_raid_bio *
raid56_parity_alloc_scrub_rbio(struct btrfs_root *root, struct bio *bio,
			       struct btrfs_bio *bbio, u64 stripe_len,
			       struct btrfs_device *scrub_dev,
			       unsigned long *dbitmap, int stripe_nsectors);
void raid56_parity_submit_scrub_rbio(struct btrfs_raid_bio *rbio);

struct btrfs_raid_bio *
raid56_alloc_missing_rbio(struct btrfs_root *root, struct bio *bio,
			  struct btrfs_bio *bbio, u64 length);
void raid56_submit_missing_rbio(struct btrfs_raid_bio *rbio);

int btrfs_alloc_stripe_hash_table(struct btrfs_fs_info *info);
void btrfs_free_stripe_hash_table(struct btrfs_fs_info *info);
#endif
