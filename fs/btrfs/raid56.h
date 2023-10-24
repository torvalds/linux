/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Fusion-io  All rights reserved.
 * Copyright (C) 2012 Intel Corp. All rights reserved.
 */

#ifndef BTRFS_RAID56_H
#define BTRFS_RAID56_H

#include <linux/workqueue.h>
#include "volumes.h"

enum btrfs_rbio_ops {
	BTRFS_RBIO_WRITE,
	BTRFS_RBIO_READ_REBUILD,
	BTRFS_RBIO_PARITY_SCRUB,
};

struct btrfs_raid_bio {
	struct btrfs_io_context *bioc;

	/*
	 * While we're doing RMW on a stripe we put it into a hash table so we
	 * can lock the stripe and merge more rbios into it.
	 */
	struct list_head hash_list;

	/* LRU list for the stripe cache */
	struct list_head stripe_cache;

	/* For scheduling work in the helper threads */
	struct work_struct work;

	/*
	 * bio_list and bio_list_lock are used to add more bios into the stripe
	 * in hopes of avoiding the full RMW
	 */
	struct bio_list bio_list;
	spinlock_t bio_list_lock;

	/*
	 * Also protected by the bio_list_lock, the plug list is used by the
	 * plugging code to collect partial bios while plugged.  The stripe
	 * locking code also uses it to hand off the stripe lock to the next
	 * pending IO.
	 */
	struct list_head plug_list;

	/* Flags that tell us if it is safe to merge with this bio. */
	unsigned long flags;

	/*
	 * Set if we're doing a parity rebuild for a read from higher up, which
	 * is handled differently from a parity rebuild as part of RMW.
	 */
	enum btrfs_rbio_ops operation;

	/* How many pages there are for the full stripe including P/Q */
	u16 nr_pages;

	/* How many sectors there are for the full stripe including P/Q */
	u16 nr_sectors;

	/* Number of data stripes (no p/q) */
	u8 nr_data;

	/* Number of all stripes (including P/Q) */
	u8 real_stripes;

	/* How many pages there are for each stripe */
	u8 stripe_npages;

	/* How many sectors there are for each stripe */
	u8 stripe_nsectors;

	/* Stripe number that we're scrubbing  */
	u8 scrubp;

	/*
	 * Size of all the bios in the bio_list.  This helps us decide if the
	 * rbio maps to a full stripe or not.
	 */
	int bio_list_bytes;

	refcount_t refs;

	atomic_t stripes_pending;

	wait_queue_head_t io_wait;

	/* Bitmap to record which horizontal stripe has data */
	unsigned long dbitmap;

	/* Allocated with stripe_nsectors-many bits for finish_*() calls */
	unsigned long finish_pbitmap;

	/*
	 * These are two arrays of pointers.  We allocate the rbio big enough
	 * to hold them both and setup their locations when the rbio is
	 * allocated.
	 */

	/*
	 * Pointers to pages that we allocated for reading/writing stripes
	 * directly from the disk (including P/Q).
	 */
	struct page **stripe_pages;

	/* Pointers to the sectors in the bio_list, for faster lookup */
	struct sector_ptr *bio_sectors;

	/*
	 * For subpage support, we need to map each sector to above
	 * stripe_pages.
	 */
	struct sector_ptr *stripe_sectors;

	/* Allocated with real_stripes-many pointers for finish_*() calls */
	void **finish_pointers;

	/*
	 * The bitmap recording where IO errors happened.
	 * Each bit is corresponding to one sector in either bio_sectors[] or
	 * stripe_sectors[] array.
	 *
	 * The reason we don't use another bit in sector_ptr is, we have two
	 * arrays of sectors, and a lot of IO can use sectors in both arrays.
	 * Thus making it much harder to iterate.
	 */
	unsigned long *error_bitmap;

	/*
	 * Checksum buffer if the rbio is for data.  The buffer should cover
	 * all data sectors (excluding P/Q sectors).
	 */
	u8 *csum_buf;

	/*
	 * Each bit represents if the corresponding sector has data csum found.
	 * Should only cover data sectors (excluding P/Q sectors).
	 */
	unsigned long *csum_bitmap;
};

/*
 * For trace event usage only. Records useful debug info for each bio submitted
 * by RAID56 to each physical device.
 *
 * No matter signed or not, (-1) is always the one indicating we can not grab
 * the proper stripe number.
 */
struct raid56_bio_trace_info {
	u64 devid;

	/* The offset inside the stripe. (<= STRIPE_LEN) */
	u32 offset;

	/*
	 * Stripe number.
	 * 0 is the first data stripe, and nr_data for P stripe,
	 * nr_data + 1 for Q stripe.
	 * >= real_stripes for
	 */
	u8 stripe_nr;
};

static inline int nr_data_stripes(const struct map_lookup *map)
{
	return map->num_stripes - btrfs_nr_parity_stripes(map->type);
}

static inline int nr_bioc_data_stripes(const struct btrfs_io_context *bioc)
{
	return bioc->num_stripes - btrfs_nr_parity_stripes(bioc->map_type);
}

#define RAID5_P_STRIPE ((u64)-2)
#define RAID6_Q_STRIPE ((u64)-1)

#define is_parity_stripe(x) (((x) == RAID5_P_STRIPE) ||		\
			     ((x) == RAID6_Q_STRIPE))

struct btrfs_device;

void raid56_parity_recover(struct bio *bio, struct btrfs_io_context *bioc,
			   int mirror_num);
void raid56_parity_write(struct bio *bio, struct btrfs_io_context *bioc);

struct btrfs_raid_bio *raid56_parity_alloc_scrub_rbio(struct bio *bio,
				struct btrfs_io_context *bioc,
				struct btrfs_device *scrub_dev,
				unsigned long *dbitmap, int stripe_nsectors);
void raid56_parity_submit_scrub_rbio(struct btrfs_raid_bio *rbio);

void raid56_parity_cache_data_pages(struct btrfs_raid_bio *rbio,
				    struct page **data_pages, u64 data_logical);

int btrfs_alloc_stripe_hash_table(struct btrfs_fs_info *info);
void btrfs_free_stripe_hash_table(struct btrfs_fs_info *info);

#endif
