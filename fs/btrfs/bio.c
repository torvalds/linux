// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 * Copyright (C) 2022 Christoph Hellwig.
 */

#include <linux/bio.h>
#include "bio.h"
#include "ctree.h"
#include "volumes.h"
#include "raid56.h"
#include "async-thread.h"
#include "check-integrity.h"
#include "dev-replace.h"
#include "rcu-string.h"
#include "zoned.h"

static struct bio_set btrfs_bioset;

/*
 * Initialize a btrfs_bio structure.  This skips the embedded bio itself as it
 * is already initialized by the block layer.
 */
static inline void btrfs_bio_init(struct btrfs_bio *bbio,
				  btrfs_bio_end_io_t end_io, void *private)
{
	memset(bbio, 0, offsetof(struct btrfs_bio, bio));
	bbio->end_io = end_io;
	bbio->private = private;
}

/*
 * Allocate a btrfs_bio structure.  The btrfs_bio is the main I/O container for
 * btrfs, and is used for all I/O submitted through btrfs_submit_bio.
 *
 * Just like the underlying bio_alloc_bioset it will not fail as it is backed by
 * a mempool.
 */
struct bio *btrfs_bio_alloc(unsigned int nr_vecs, blk_opf_t opf,
			    btrfs_bio_end_io_t end_io, void *private)
{
	struct bio *bio;

	bio = bio_alloc_bioset(NULL, nr_vecs, opf, GFP_NOFS, &btrfs_bioset);
	btrfs_bio_init(btrfs_bio(bio), end_io, private);
	return bio;
}

struct bio *btrfs_bio_clone_partial(struct bio *orig, u64 offset, u64 size,
				    btrfs_bio_end_io_t end_io, void *private)
{
	struct bio *bio;
	struct btrfs_bio *bbio;

	ASSERT(offset <= UINT_MAX && size <= UINT_MAX);

	bio = bio_alloc_clone(orig->bi_bdev, orig, GFP_NOFS, &btrfs_bioset);
	bbio = btrfs_bio(bio);
	btrfs_bio_init(bbio, end_io, private);

	bio_trim(bio, offset >> 9, size >> 9);
	bbio->iter = bio->bi_iter;
	return bio;
}

static void btrfs_log_dev_io_error(struct bio *bio, struct btrfs_device *dev)
{
	if (!dev || !dev->bdev)
		return;
	if (bio->bi_status != BLK_STS_IOERR && bio->bi_status != BLK_STS_TARGET)
		return;

	if (btrfs_op(bio) == BTRFS_MAP_WRITE)
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_WRITE_ERRS);
	if (!(bio->bi_opf & REQ_RAHEAD))
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
	if (bio->bi_opf & REQ_PREFLUSH)
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_FLUSH_ERRS);
}

static struct workqueue_struct *btrfs_end_io_wq(struct btrfs_fs_info *fs_info,
						struct bio *bio)
{
	if (bio->bi_opf & REQ_META)
		return fs_info->endio_meta_workers;
	return fs_info->endio_workers;
}

static void btrfs_end_bio_work(struct work_struct *work)
{
	struct btrfs_bio *bbio = container_of(work, struct btrfs_bio, end_io_work);

	bbio->end_io(bbio);
}

static void btrfs_simple_end_io(struct bio *bio)
{
	struct btrfs_fs_info *fs_info = bio->bi_private;
	struct btrfs_bio *bbio = btrfs_bio(bio);

	btrfs_bio_counter_dec(fs_info);

	if (bio->bi_status)
		btrfs_log_dev_io_error(bio, bbio->device);

	if (bio_op(bio) == REQ_OP_READ) {
		INIT_WORK(&bbio->end_io_work, btrfs_end_bio_work);
		queue_work(btrfs_end_io_wq(fs_info, bio), &bbio->end_io_work);
	} else {
		bbio->end_io(bbio);
	}
}

static void btrfs_raid56_end_io(struct bio *bio)
{
	struct btrfs_io_context *bioc = bio->bi_private;
	struct btrfs_bio *bbio = btrfs_bio(bio);

	btrfs_bio_counter_dec(bioc->fs_info);
	bbio->mirror_num = bioc->mirror_num;
	bbio->end_io(bbio);

	btrfs_put_bioc(bioc);
}

static void btrfs_orig_write_end_io(struct bio *bio)
{
	struct btrfs_io_stripe *stripe = bio->bi_private;
	struct btrfs_io_context *bioc = stripe->bioc;
	struct btrfs_bio *bbio = btrfs_bio(bio);

	btrfs_bio_counter_dec(bioc->fs_info);

	if (bio->bi_status) {
		atomic_inc(&bioc->error);
		btrfs_log_dev_io_error(bio, stripe->dev);
	}

	/*
	 * Only send an error to the higher layers if it is beyond the tolerance
	 * threshold.
	 */
	if (atomic_read(&bioc->error) > bioc->max_errors)
		bio->bi_status = BLK_STS_IOERR;
	else
		bio->bi_status = BLK_STS_OK;

	bbio->end_io(bbio);
	btrfs_put_bioc(bioc);
}

static void btrfs_clone_write_end_io(struct bio *bio)
{
	struct btrfs_io_stripe *stripe = bio->bi_private;

	if (bio->bi_status) {
		atomic_inc(&stripe->bioc->error);
		btrfs_log_dev_io_error(bio, stripe->dev);
	}

	/* Pass on control to the original bio this one was cloned from */
	bio_endio(stripe->bioc->orig_bio);
	bio_put(bio);
}

static void btrfs_submit_dev_bio(struct btrfs_device *dev, struct bio *bio)
{
	if (!dev || !dev->bdev ||
	    test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state) ||
	    (btrfs_op(bio) == BTRFS_MAP_WRITE &&
	     !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state))) {
		bio_io_error(bio);
		return;
	}

	bio_set_dev(bio, dev->bdev);

	/*
	 * For zone append writing, bi_sector must point the beginning of the
	 * zone
	 */
	if (bio_op(bio) == REQ_OP_ZONE_APPEND) {
		u64 physical = bio->bi_iter.bi_sector << SECTOR_SHIFT;

		if (btrfs_dev_is_sequential(dev, physical)) {
			u64 zone_start = round_down(physical,
						    dev->fs_info->zone_size);

			bio->bi_iter.bi_sector = zone_start >> SECTOR_SHIFT;
		} else {
			bio->bi_opf &= ~REQ_OP_ZONE_APPEND;
			bio->bi_opf |= REQ_OP_WRITE;
		}
	}
	btrfs_debug_in_rcu(dev->fs_info,
	"%s: rw %d 0x%x, sector=%llu, dev=%lu (%s id %llu), size=%u",
		__func__, bio_op(bio), bio->bi_opf, bio->bi_iter.bi_sector,
		(unsigned long)dev->bdev->bd_dev, btrfs_dev_name(dev),
		dev->devid, bio->bi_iter.bi_size);

	btrfsic_check_bio(bio);
	submit_bio(bio);
}

static void btrfs_submit_mirrored_bio(struct btrfs_io_context *bioc, int dev_nr)
{
	struct bio *orig_bio = bioc->orig_bio, *bio;

	ASSERT(bio_op(orig_bio) != REQ_OP_READ);

	/* Reuse the bio embedded into the btrfs_bio for the last mirror */
	if (dev_nr == bioc->num_stripes - 1) {
		bio = orig_bio;
		bio->bi_end_io = btrfs_orig_write_end_io;
	} else {
		bio = bio_alloc_clone(NULL, orig_bio, GFP_NOFS, &fs_bio_set);
		bio_inc_remaining(orig_bio);
		bio->bi_end_io = btrfs_clone_write_end_io;
	}

	bio->bi_private = &bioc->stripes[dev_nr];
	bio->bi_iter.bi_sector = bioc->stripes[dev_nr].physical >> SECTOR_SHIFT;
	bioc->stripes[dev_nr].bioc = bioc;
	btrfs_submit_dev_bio(bioc->stripes[dev_nr].dev, bio);
}

void btrfs_submit_bio(struct btrfs_fs_info *fs_info, struct bio *bio, int mirror_num)
{
	u64 logical = bio->bi_iter.bi_sector << 9;
	u64 length = bio->bi_iter.bi_size;
	u64 map_length = length;
	struct btrfs_io_context *bioc = NULL;
	struct btrfs_io_stripe smap;
	int ret;

	btrfs_bio_counter_inc_blocked(fs_info);
	ret = __btrfs_map_block(fs_info, btrfs_op(bio), logical, &map_length,
				&bioc, &smap, &mirror_num, 1);
	if (ret) {
		btrfs_bio_counter_dec(fs_info);
		btrfs_bio_end_io(btrfs_bio(bio), errno_to_blk_status(ret));
		return;
	}

	if (map_length < length) {
		btrfs_crit(fs_info,
			   "mapping failed logical %llu bio len %llu len %llu",
			   logical, length, map_length);
		BUG();
	}

	if (!bioc) {
		/* Single mirror read/write fast path */
		btrfs_bio(bio)->mirror_num = mirror_num;
		btrfs_bio(bio)->device = smap.dev;
		bio->bi_iter.bi_sector = smap.physical >> SECTOR_SHIFT;
		bio->bi_private = fs_info;
		bio->bi_end_io = btrfs_simple_end_io;
		btrfs_submit_dev_bio(smap.dev, bio);
	} else if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		/* Parity RAID write or read recovery */
		bio->bi_private = bioc;
		bio->bi_end_io = btrfs_raid56_end_io;
		if (bio_op(bio) == REQ_OP_READ)
			raid56_parity_recover(bio, bioc, mirror_num);
		else
			raid56_parity_write(bio, bioc);
	} else {
		/* Write to multiple mirrors */
		int total_devs = bioc->num_stripes;
		int dev_nr;

		bioc->orig_bio = bio;
		for (dev_nr = 0; dev_nr < total_devs; dev_nr++)
			btrfs_submit_mirrored_bio(bioc, dev_nr);
	}
}

/*
 * Submit a repair write.
 *
 * This bypasses btrfs_submit_bio deliberately, as that writes all copies in a
 * RAID setup.  Here we only want to write the one bad copy, so we do the
 * mapping ourselves and submit the bio directly.
 *
 * The I/O is issued sychronously to block the repair read completion from
 * freeing the bio.
 */
int btrfs_repair_io_failure(struct btrfs_fs_info *fs_info, u64 ino, u64 start,
			    u64 length, u64 logical, struct page *page,
			    unsigned int pg_offset, int mirror_num)
{
	struct btrfs_device *dev;
	struct bio_vec bvec;
	struct bio bio;
	u64 map_length = 0;
	u64 sector;
	struct btrfs_io_context *bioc = NULL;
	int ret = 0;

	ASSERT(!(fs_info->sb->s_flags & SB_RDONLY));
	BUG_ON(!mirror_num);

	if (btrfs_repair_one_zone(fs_info, logical))
		return 0;

	map_length = length;

	/*
	 * Avoid races with device replace and make sure our bioc has devices
	 * associated to its stripes that don't go away while we are doing the
	 * read repair operation.
	 */
	btrfs_bio_counter_inc_blocked(fs_info);
	if (btrfs_is_parity_mirror(fs_info, logical, length)) {
		/*
		 * Note that we don't use BTRFS_MAP_WRITE because it's supposed
		 * to update all raid stripes, but here we just want to correct
		 * bad stripe, thus BTRFS_MAP_READ is abused to only get the bad
		 * stripe's dev and sector.
		 */
		ret = btrfs_map_block(fs_info, BTRFS_MAP_READ, logical,
				      &map_length, &bioc, 0);
		if (ret)
			goto out_counter_dec;
		ASSERT(bioc->mirror_num == 1);
	} else {
		ret = btrfs_map_block(fs_info, BTRFS_MAP_WRITE, logical,
				      &map_length, &bioc, mirror_num);
		if (ret)
			goto out_counter_dec;
		/*
		 * This happens when dev-replace is also running, and the
		 * mirror_num indicates the dev-replace target.
		 *
		 * In this case, we don't need to do anything, as the read
		 * error just means the replace progress hasn't reached our
		 * read range, and later replace routine would handle it well.
		 */
		if (mirror_num != bioc->mirror_num)
			goto out_counter_dec;
	}

	sector = bioc->stripes[bioc->mirror_num - 1].physical >> 9;
	dev = bioc->stripes[bioc->mirror_num - 1].dev;
	btrfs_put_bioc(bioc);

	if (!dev || !dev->bdev ||
	    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state)) {
		ret = -EIO;
		goto out_counter_dec;
	}

	bio_init(&bio, dev->bdev, &bvec, 1, REQ_OP_WRITE | REQ_SYNC);
	bio.bi_iter.bi_sector = sector;
	__bio_add_page(&bio, page, length, pg_offset);

	btrfsic_check_bio(&bio);
	ret = submit_bio_wait(&bio);
	if (ret) {
		/* try to remap that extent elsewhere? */
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_WRITE_ERRS);
		goto out_bio_uninit;
	}

	btrfs_info_rl_in_rcu(fs_info,
		"read error corrected: ino %llu off %llu (dev %s sector %llu)",
			     ino, start, btrfs_dev_name(dev), sector);
	ret = 0;

out_bio_uninit:
	bio_uninit(&bio);
out_counter_dec:
	btrfs_bio_counter_dec(fs_info);
	return ret;
}

int __init btrfs_bioset_init(void)
{
	if (bioset_init(&btrfs_bioset, BIO_POOL_SIZE,
			offsetof(struct btrfs_bio, bio),
			BIOSET_NEED_BVECS))
		return -ENOMEM;
	return 0;
}

void __cold btrfs_bioset_exit(void)
{
	bioset_exit(&btrfs_bioset);
}
