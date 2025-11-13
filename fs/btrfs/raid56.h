/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Fusion-io  All rights reserved.
 * Copyright (C) 2012 Intel Corp. All rights reserved.
 */

#ifndef BTRFS_RAID56_H
#define BTRFS_RAID56_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/bio.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>
#include "volumes.h"

struct page;
struct btrfs_fs_info;

enum btrfs_rbio_ops {
	BTRFS_RBIO_WRITE,
	BTRFS_RBIO_READ_REBUILD,
	BTRFS_RBIO_PARITY_SCRUB,
};

/*
 * Overview of btrfs_raid_bio.
 *
 * One btrfs_raid_bio represents a full stripe of RAID56, including both data
 * and P/Q stripes. For now, each data and P/Q stripe is of a fixed length (64K).
 *
 * One btrfs_raid_bio can have one or more bios from higher layer, covering
 * part or all of the data stripes.
 *
 * [PAGES FROM HIGHER LAYER BIOS]
 * Higher layer bios are in the btrfs_raid_bio::bio_list.
 *
 * Pages from the bio_list are represented like the following:
 *
 * bio_list:	     |<- Bio 1 ->|             |<- Bio 2 ->|  ...
 * bio_paddrs:	    [0]   [1]   [2]    [3]    [4]    [5]      ...
 *
 * If there is a bio covering a sector (one btrfs fs block), the corresponding
 * pointer in btrfs_raid_bio::bio_paddrs[] will point to the physical address
 * (with the offset inside the page) of the corresponding bio.
 *
 * If there is no bio covering a sector, then btrfs_raid_bio::bio_paddrs[i] will
 * be INVALID_PADDR.
 *
 * The length of each entry in bio_paddrs[] is a step (aka, min(sectorsize, PAGE_SIZE)).
 *
 * [PAGES FOR INTERNAL USAGES]
 * Pages not covered by any bio or belonging to P/Q stripes are stored in
 * btrfs_raid_bio::stripe_pages[] and stripe_paddrs[], like the following:
 *
 * stripe_pages:       |<- Page 0 ->|<- Page 1 ->|  ...
 * stripe_paddrs:     [0]    [1]   [2]    [3]   [4] ...
 *
 * stripe_pages[] array stores all the pages covering the full stripe, including
 * data and P/Q pages.
 * stripe_pages[0] is the first page of the first data stripe.
 * stripe_pages[BTRFS_STRIPE_LEN / PAGE_SIZE] is the first page of the second
 * data stripe.
 *
 * Some pointers inside stripe_pages[] can be NULL, e.g. for a full stripe write
 * (the bio covers all data stripes) there is no need to allocate pages for
 * data stripes (can grab from bio_paddrs[]).
 *
 * If the corresponding page of stripe_paddrs[i] is not allocated, the value of
 * stripe_paddrs[i] will be INVALID_PADDR.
 *
 * The length of each entry in stripe_paddrs[] is a step.
 *
 * [LOCATING A SECTOR]
 * To locate a sector for IO, we need the following info:
 *
 * - stripe_nr
 *   Starts from 0 (representing the first data stripe), ends at
 *   @nr_data (RAID5, P stripe) or @nr_data + 1 (RAID6, Q stripe).
 *
 * - sector_nr
 *   Starts from 0 (representing the first sector of the stripe), ends
 *   at BTRFS_STRIPE_LEN / sectorsize - 1.
 *
 * - step_nr
 *   A step is min(sector_size, PAGE_SIZE).
 *
 *   Starts from 0 (representing the first step of the sector), ends
 *   at @sector_nsteps - 1.
 *
 *   For most call sites they do not need to bother this parameter.
 *   It is for bs > ps support and only for vertical stripe related works.
 *   (e.g. RMW/recover)
 *
 * - from which array
 *   Whether grabbing from stripe_paddrs[] (aka, internal pages) or from the
 *   bio_paddrs[] (aka, from the higher layer bios).
 *
 * For IO, a physical address is returned, so that we can extract the page and
 * the offset inside the page for IO.
 * A special value INVALID_PADDR represents when the physical address is invalid,
 * normally meaning there is no page allocated for the specified sector.
 */
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

	/*
	 * How many steps there are for one sector.
	 *
	 * For bs > ps cases, it's sectorsize / PAGE_SIZE.
	 * For bs <= ps cases, it's always 1.
	 */
	u8 sector_nsteps;

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
	phys_addr_t *bio_paddrs;

	/* Pointers to the sectors in the stripe_pages[]. */
	phys_addr_t *stripe_paddrs;

	/* Each set bit means the corresponding sector in stripe_sectors[] is uptodate. */
	unsigned long *stripe_uptodate_bitmap;

	/* Allocated with real_stripes-many pointers for finish_*() calls */
	void **finish_pointers;

	/*
	 * The bitmap recording where IO errors happened.
	 * Each bit is corresponding to one sector in either bio_sectors[] or
	 * stripe_sectors[] array.
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

static inline int nr_data_stripes(const struct btrfs_chunk_map *map)
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

void raid56_parity_cache_data_folios(struct btrfs_raid_bio *rbio,
				     struct folio **data_folios, u64 data_logical);

int btrfs_alloc_stripe_hash_table(struct btrfs_fs_info *info);
void btrfs_free_stripe_hash_table(struct btrfs_fs_info *info);

#endif
