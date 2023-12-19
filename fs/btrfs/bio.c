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
#include "dev-replace.h"
#include "rcu-string.h"
#include "zoned.h"
#include "file-item.h"
#include "raid-stripe-tree.h"

static struct bio_set btrfs_bioset;
static struct bio_set btrfs_clone_bioset;
static struct bio_set btrfs_repair_bioset;
static mempool_t btrfs_failed_bio_pool;

struct btrfs_failed_bio {
	struct btrfs_bio *bbio;
	int num_copies;
	atomic_t repair_count;
};

/* Is this a data path I/O that needs storage layer checksum and repair? */
static inline bool is_data_bbio(struct btrfs_bio *bbio)
{
	return bbio->inode && is_data_inode(&bbio->inode->vfs_inode);
}

static bool bbio_has_ordered_extent(struct btrfs_bio *bbio)
{
	return is_data_bbio(bbio) && btrfs_op(&bbio->bio) == BTRFS_MAP_WRITE;
}

/*
 * Initialize a btrfs_bio structure.  This skips the embedded bio itself as it
 * is already initialized by the block layer.
 */
void btrfs_bio_init(struct btrfs_bio *bbio, struct btrfs_fs_info *fs_info,
		    btrfs_bio_end_io_t end_io, void *private)
{
	memset(bbio, 0, offsetof(struct btrfs_bio, bio));
	bbio->fs_info = fs_info;
	bbio->end_io = end_io;
	bbio->private = private;
	atomic_set(&bbio->pending_ios, 1);
}

/*
 * Allocate a btrfs_bio structure.  The btrfs_bio is the main I/O container for
 * btrfs, and is used for all I/O submitted through btrfs_submit_bio.
 *
 * Just like the underlying bio_alloc_bioset it will not fail as it is backed by
 * a mempool.
 */
struct btrfs_bio *btrfs_bio_alloc(unsigned int nr_vecs, blk_opf_t opf,
				  struct btrfs_fs_info *fs_info,
				  btrfs_bio_end_io_t end_io, void *private)
{
	struct btrfs_bio *bbio;
	struct bio *bio;

	bio = bio_alloc_bioset(NULL, nr_vecs, opf, GFP_NOFS, &btrfs_bioset);
	bbio = btrfs_bio(bio);
	btrfs_bio_init(bbio, fs_info, end_io, private);
	return bbio;
}

static struct btrfs_bio *btrfs_split_bio(struct btrfs_fs_info *fs_info,
					 struct btrfs_bio *orig_bbio,
					 u64 map_length, bool use_append)
{
	struct btrfs_bio *bbio;
	struct bio *bio;

	if (use_append) {
		unsigned int nr_segs;

		bio = bio_split_rw(&orig_bbio->bio, &fs_info->limits, &nr_segs,
				   &btrfs_clone_bioset, map_length);
	} else {
		bio = bio_split(&orig_bbio->bio, map_length >> SECTOR_SHIFT,
				GFP_NOFS, &btrfs_clone_bioset);
	}
	bbio = btrfs_bio(bio);
	btrfs_bio_init(bbio, fs_info, NULL, orig_bbio);
	bbio->inode = orig_bbio->inode;
	bbio->file_offset = orig_bbio->file_offset;
	orig_bbio->file_offset += map_length;
	if (bbio_has_ordered_extent(bbio)) {
		refcount_inc(&orig_bbio->ordered->refs);
		bbio->ordered = orig_bbio->ordered;
	}
	atomic_inc(&orig_bbio->pending_ios);
	return bbio;
}

/* Free a bio that was never submitted to the underlying device. */
static void btrfs_cleanup_bio(struct btrfs_bio *bbio)
{
	if (bbio_has_ordered_extent(bbio))
		btrfs_put_ordered_extent(bbio->ordered);
	bio_put(&bbio->bio);
}

static void __btrfs_bio_end_io(struct btrfs_bio *bbio)
{
	if (bbio_has_ordered_extent(bbio)) {
		struct btrfs_ordered_extent *ordered = bbio->ordered;

		bbio->end_io(bbio);
		btrfs_put_ordered_extent(ordered);
	} else {
		bbio->end_io(bbio);
	}
}

void btrfs_bio_end_io(struct btrfs_bio *bbio, blk_status_t status)
{
	bbio->bio.bi_status = status;
	__btrfs_bio_end_io(bbio);
}

static void btrfs_orig_write_end_io(struct bio *bio);

static void btrfs_bbio_propagate_error(struct btrfs_bio *bbio,
				       struct btrfs_bio *orig_bbio)
{
	/*
	 * For writes we tolerate nr_mirrors - 1 write failures, so we can't
	 * just blindly propagate a write failure here.  Instead increment the
	 * error count in the original I/O context so that it is guaranteed to
	 * be larger than the error tolerance.
	 */
	if (bbio->bio.bi_end_io == &btrfs_orig_write_end_io) {
		struct btrfs_io_stripe *orig_stripe = orig_bbio->bio.bi_private;
		struct btrfs_io_context *orig_bioc = orig_stripe->bioc;

		atomic_add(orig_bioc->max_errors, &orig_bioc->error);
	} else {
		orig_bbio->bio.bi_status = bbio->bio.bi_status;
	}
}

static void btrfs_orig_bbio_end_io(struct btrfs_bio *bbio)
{
	if (bbio->bio.bi_pool == &btrfs_clone_bioset) {
		struct btrfs_bio *orig_bbio = bbio->private;

		if (bbio->bio.bi_status)
			btrfs_bbio_propagate_error(bbio, orig_bbio);
		btrfs_cleanup_bio(bbio);
		bbio = orig_bbio;
	}

	if (atomic_dec_and_test(&bbio->pending_ios))
		__btrfs_bio_end_io(bbio);
}

static int next_repair_mirror(struct btrfs_failed_bio *fbio, int cur_mirror)
{
	if (cur_mirror == fbio->num_copies)
		return cur_mirror + 1 - fbio->num_copies;
	return cur_mirror + 1;
}

static int prev_repair_mirror(struct btrfs_failed_bio *fbio, int cur_mirror)
{
	if (cur_mirror == 1)
		return fbio->num_copies;
	return cur_mirror - 1;
}

static void btrfs_repair_done(struct btrfs_failed_bio *fbio)
{
	if (atomic_dec_and_test(&fbio->repair_count)) {
		btrfs_orig_bbio_end_io(fbio->bbio);
		mempool_free(fbio, &btrfs_failed_bio_pool);
	}
}

static void btrfs_end_repair_bio(struct btrfs_bio *repair_bbio,
				 struct btrfs_device *dev)
{
	struct btrfs_failed_bio *fbio = repair_bbio->private;
	struct btrfs_inode *inode = repair_bbio->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct bio_vec *bv = bio_first_bvec_all(&repair_bbio->bio);
	int mirror = repair_bbio->mirror_num;

	if (repair_bbio->bio.bi_status ||
	    !btrfs_data_csum_ok(repair_bbio, dev, 0, bv)) {
		bio_reset(&repair_bbio->bio, NULL, REQ_OP_READ);
		repair_bbio->bio.bi_iter = repair_bbio->saved_iter;

		mirror = next_repair_mirror(fbio, mirror);
		if (mirror == fbio->bbio->mirror_num) {
			btrfs_debug(fs_info, "no mirror left");
			fbio->bbio->bio.bi_status = BLK_STS_IOERR;
			goto done;
		}

		btrfs_submit_bio(repair_bbio, mirror);
		return;
	}

	do {
		mirror = prev_repair_mirror(fbio, mirror);
		btrfs_repair_io_failure(fs_info, btrfs_ino(inode),
				  repair_bbio->file_offset, fs_info->sectorsize,
				  repair_bbio->saved_iter.bi_sector << SECTOR_SHIFT,
				  bv->bv_page, bv->bv_offset, mirror);
	} while (mirror != fbio->bbio->mirror_num);

done:
	btrfs_repair_done(fbio);
	bio_put(&repair_bbio->bio);
}

/*
 * Try to kick off a repair read to the next available mirror for a bad sector.
 *
 * This primarily tries to recover good data to serve the actual read request,
 * but also tries to write the good data back to the bad mirror(s) when a
 * read succeeded to restore the redundancy.
 */
static struct btrfs_failed_bio *repair_one_sector(struct btrfs_bio *failed_bbio,
						  u32 bio_offset,
						  struct bio_vec *bv,
						  struct btrfs_failed_bio *fbio)
{
	struct btrfs_inode *inode = failed_bbio->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	const u32 sectorsize = fs_info->sectorsize;
	const u64 logical = (failed_bbio->saved_iter.bi_sector << SECTOR_SHIFT);
	struct btrfs_bio *repair_bbio;
	struct bio *repair_bio;
	int num_copies;
	int mirror;

	btrfs_debug(fs_info, "repair read error: read error at %llu",
		    failed_bbio->file_offset + bio_offset);

	num_copies = btrfs_num_copies(fs_info, logical, sectorsize);
	if (num_copies == 1) {
		btrfs_debug(fs_info, "no copy to repair from");
		failed_bbio->bio.bi_status = BLK_STS_IOERR;
		return fbio;
	}

	if (!fbio) {
		fbio = mempool_alloc(&btrfs_failed_bio_pool, GFP_NOFS);
		fbio->bbio = failed_bbio;
		fbio->num_copies = num_copies;
		atomic_set(&fbio->repair_count, 1);
	}

	atomic_inc(&fbio->repair_count);

	repair_bio = bio_alloc_bioset(NULL, 1, REQ_OP_READ, GFP_NOFS,
				      &btrfs_repair_bioset);
	repair_bio->bi_iter.bi_sector = failed_bbio->saved_iter.bi_sector;
	__bio_add_page(repair_bio, bv->bv_page, bv->bv_len, bv->bv_offset);

	repair_bbio = btrfs_bio(repair_bio);
	btrfs_bio_init(repair_bbio, fs_info, NULL, fbio);
	repair_bbio->inode = failed_bbio->inode;
	repair_bbio->file_offset = failed_bbio->file_offset + bio_offset;

	mirror = next_repair_mirror(fbio, failed_bbio->mirror_num);
	btrfs_debug(fs_info, "submitting repair read to mirror %d", mirror);
	btrfs_submit_bio(repair_bbio, mirror);
	return fbio;
}

static void btrfs_check_read_bio(struct btrfs_bio *bbio, struct btrfs_device *dev)
{
	struct btrfs_inode *inode = bbio->inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	u32 sectorsize = fs_info->sectorsize;
	struct bvec_iter *iter = &bbio->saved_iter;
	blk_status_t status = bbio->bio.bi_status;
	struct btrfs_failed_bio *fbio = NULL;
	u32 offset = 0;

	/* Read-repair requires the inode field to be set by the submitter. */
	ASSERT(inode);

	/*
	 * Hand off repair bios to the repair code as there is no upper level
	 * submitter for them.
	 */
	if (bbio->bio.bi_pool == &btrfs_repair_bioset) {
		btrfs_end_repair_bio(bbio, dev);
		return;
	}

	/* Clear the I/O error. A failed repair will reset it. */
	bbio->bio.bi_status = BLK_STS_OK;

	while (iter->bi_size) {
		struct bio_vec bv = bio_iter_iovec(&bbio->bio, *iter);

		bv.bv_len = min(bv.bv_len, sectorsize);
		if (status || !btrfs_data_csum_ok(bbio, dev, offset, &bv))
			fbio = repair_one_sector(bbio, offset, &bv, fbio);

		bio_advance_iter_single(&bbio->bio, iter, sectorsize);
		offset += sectorsize;
	}

	if (bbio->csum != bbio->csum_inline)
		kfree(bbio->csum);

	if (fbio)
		btrfs_repair_done(fbio);
	else
		btrfs_orig_bbio_end_io(bbio);
}

static void btrfs_log_dev_io_error(struct bio *bio, struct btrfs_device *dev)
{
	if (!dev || !dev->bdev)
		return;
	if (bio->bi_status != BLK_STS_IOERR && bio->bi_status != BLK_STS_TARGET)
		return;

	if (btrfs_op(bio) == BTRFS_MAP_WRITE)
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_WRITE_ERRS);
	else if (!(bio->bi_opf & REQ_RAHEAD))
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

	/* Metadata reads are checked and repaired by the submitter. */
	if (is_data_bbio(bbio))
		btrfs_check_read_bio(bbio, bbio->bio.bi_private);
	else
		btrfs_orig_bbio_end_io(bbio);
}

static void btrfs_simple_end_io(struct bio *bio)
{
	struct btrfs_bio *bbio = btrfs_bio(bio);
	struct btrfs_device *dev = bio->bi_private;
	struct btrfs_fs_info *fs_info = bbio->fs_info;

	btrfs_bio_counter_dec(fs_info);

	if (bio->bi_status)
		btrfs_log_dev_io_error(bio, dev);

	if (bio_op(bio) == REQ_OP_READ) {
		INIT_WORK(&bbio->end_io_work, btrfs_end_bio_work);
		queue_work(btrfs_end_io_wq(fs_info, bio), &bbio->end_io_work);
	} else {
		if (bio_op(bio) == REQ_OP_ZONE_APPEND && !bio->bi_status)
			btrfs_record_physical_zoned(bbio);
		btrfs_orig_bbio_end_io(bbio);
	}
}

static void btrfs_raid56_end_io(struct bio *bio)
{
	struct btrfs_io_context *bioc = bio->bi_private;
	struct btrfs_bio *bbio = btrfs_bio(bio);

	btrfs_bio_counter_dec(bioc->fs_info);
	bbio->mirror_num = bioc->mirror_num;
	if (bio_op(bio) == REQ_OP_READ && is_data_bbio(bbio))
		btrfs_check_read_bio(bbio, NULL);
	else
		btrfs_orig_bbio_end_io(bbio);

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

	if (bio_op(bio) == REQ_OP_ZONE_APPEND && !bio->bi_status)
		stripe->physical = bio->bi_iter.bi_sector << SECTOR_SHIFT;

	btrfs_orig_bbio_end_io(bbio);
	btrfs_put_bioc(bioc);
}

static void btrfs_clone_write_end_io(struct bio *bio)
{
	struct btrfs_io_stripe *stripe = bio->bi_private;

	if (bio->bi_status) {
		atomic_inc(&stripe->bioc->error);
		btrfs_log_dev_io_error(bio, stripe->dev);
	} else if (bio_op(bio) == REQ_OP_ZONE_APPEND) {
		stripe->physical = bio->bi_iter.bi_sector << SECTOR_SHIFT;
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
		u64 zone_start = round_down(physical, dev->fs_info->zone_size);

		ASSERT(btrfs_dev_is_sequential(dev, physical));
		bio->bi_iter.bi_sector = zone_start >> SECTOR_SHIFT;
	}
	btrfs_debug_in_rcu(dev->fs_info,
	"%s: rw %d 0x%x, sector=%llu, dev=%lu (%s id %llu), size=%u",
		__func__, bio_op(bio), bio->bi_opf, bio->bi_iter.bi_sector,
		(unsigned long)dev->bdev->bd_dev, btrfs_dev_name(dev),
		dev->devid, bio->bi_iter.bi_size);

	if (bio->bi_opf & REQ_BTRFS_CGROUP_PUNT)
		blkcg_punt_bio_submit(bio);
	else
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
	bioc->size = bio->bi_iter.bi_size;
	btrfs_submit_dev_bio(bioc->stripes[dev_nr].dev, bio);
}

static void __btrfs_submit_bio(struct bio *bio, struct btrfs_io_context *bioc,
			       struct btrfs_io_stripe *smap, int mirror_num)
{
	if (!bioc) {
		/* Single mirror read/write fast path. */
		btrfs_bio(bio)->mirror_num = mirror_num;
		if (bio_op(bio) != REQ_OP_READ)
			btrfs_bio(bio)->orig_physical = smap->physical;
		bio->bi_iter.bi_sector = smap->physical >> SECTOR_SHIFT;
		if (bio_op(bio) != REQ_OP_READ)
			btrfs_bio(bio)->orig_physical = smap->physical;
		bio->bi_private = smap->dev;
		bio->bi_end_io = btrfs_simple_end_io;
		btrfs_submit_dev_bio(smap->dev, bio);
	} else if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		/* Parity RAID write or read recovery. */
		bio->bi_private = bioc;
		bio->bi_end_io = btrfs_raid56_end_io;
		if (bio_op(bio) == REQ_OP_READ)
			raid56_parity_recover(bio, bioc, mirror_num);
		else
			raid56_parity_write(bio, bioc);
	} else {
		/* Write to multiple mirrors. */
		int total_devs = bioc->num_stripes;

		bioc->orig_bio = bio;
		for (int dev_nr = 0; dev_nr < total_devs; dev_nr++)
			btrfs_submit_mirrored_bio(bioc, dev_nr);
	}
}

static blk_status_t btrfs_bio_csum(struct btrfs_bio *bbio)
{
	if (bbio->bio.bi_opf & REQ_META)
		return btree_csum_one_bio(bbio);
	return btrfs_csum_one_bio(bbio);
}

/*
 * Async submit bios are used to offload expensive checksumming onto the worker
 * threads.
 */
struct async_submit_bio {
	struct btrfs_bio *bbio;
	struct btrfs_io_context *bioc;
	struct btrfs_io_stripe smap;
	int mirror_num;
	struct btrfs_work work;
};

/*
 * In order to insert checksums into the metadata in large chunks, we wait
 * until bio submission time.   All the pages in the bio are checksummed and
 * sums are attached onto the ordered extent record.
 *
 * At IO completion time the csums attached on the ordered extent record are
 * inserted into the btree.
 */
static void run_one_async_start(struct btrfs_work *work)
{
	struct async_submit_bio *async =
		container_of(work, struct async_submit_bio, work);
	blk_status_t ret;

	ret = btrfs_bio_csum(async->bbio);
	if (ret)
		async->bbio->bio.bi_status = ret;
}

/*
 * In order to insert checksums into the metadata in large chunks, we wait
 * until bio submission time.   All the pages in the bio are checksummed and
 * sums are attached onto the ordered extent record.
 *
 * At IO completion time the csums attached on the ordered extent record are
 * inserted into the tree.
 *
 * If called with @do_free == true, then it will free the work struct.
 */
static void run_one_async_done(struct btrfs_work *work, bool do_free)
{
	struct async_submit_bio *async =
		container_of(work, struct async_submit_bio, work);
	struct bio *bio = &async->bbio->bio;

	if (do_free) {
		kfree(container_of(work, struct async_submit_bio, work));
		return;
	}

	/* If an error occurred we just want to clean up the bio and move on. */
	if (bio->bi_status) {
		btrfs_orig_bbio_end_io(async->bbio);
		return;
	}

	/*
	 * All of the bios that pass through here are from async helpers.
	 * Use REQ_BTRFS_CGROUP_PUNT to issue them from the owning cgroup's
	 * context.  This changes nothing when cgroups aren't in use.
	 */
	bio->bi_opf |= REQ_BTRFS_CGROUP_PUNT;
	__btrfs_submit_bio(bio, async->bioc, &async->smap, async->mirror_num);
}

static bool should_async_write(struct btrfs_bio *bbio)
{
	/* Submit synchronously if the checksum implementation is fast. */
	if (test_bit(BTRFS_FS_CSUM_IMPL_FAST, &bbio->fs_info->flags))
		return false;

	/*
	 * Try to defer the submission to a workqueue to parallelize the
	 * checksum calculation unless the I/O is issued synchronously.
	 */
	if (op_is_sync(bbio->bio.bi_opf))
		return false;

	/* Zoned devices require I/O to be submitted in order. */
	if ((bbio->bio.bi_opf & REQ_META) && btrfs_is_zoned(bbio->fs_info))
		return false;

	return true;
}

/*
 * Submit bio to an async queue.
 *
 * Return true if the work has been succesfuly submitted, else false.
 */
static bool btrfs_wq_submit_bio(struct btrfs_bio *bbio,
				struct btrfs_io_context *bioc,
				struct btrfs_io_stripe *smap, int mirror_num)
{
	struct btrfs_fs_info *fs_info = bbio->fs_info;
	struct async_submit_bio *async;

	async = kmalloc(sizeof(*async), GFP_NOFS);
	if (!async)
		return false;

	async->bbio = bbio;
	async->bioc = bioc;
	async->smap = *smap;
	async->mirror_num = mirror_num;

	btrfs_init_work(&async->work, run_one_async_start, run_one_async_done);
	btrfs_queue_work(fs_info->workers, &async->work);
	return true;
}

static bool btrfs_submit_chunk(struct btrfs_bio *bbio, int mirror_num)
{
	struct btrfs_inode *inode = bbio->inode;
	struct btrfs_fs_info *fs_info = bbio->fs_info;
	struct btrfs_bio *orig_bbio = bbio;
	struct bio *bio = &bbio->bio;
	u64 logical = bio->bi_iter.bi_sector << SECTOR_SHIFT;
	u64 length = bio->bi_iter.bi_size;
	u64 map_length = length;
	bool use_append = btrfs_use_zone_append(bbio);
	struct btrfs_io_context *bioc = NULL;
	struct btrfs_io_stripe smap;
	blk_status_t ret;
	int error;

	smap.is_scrub = !bbio->inode;

	btrfs_bio_counter_inc_blocked(fs_info);
	error = btrfs_map_block(fs_info, btrfs_op(bio), logical, &map_length,
				&bioc, &smap, &mirror_num);
	if (error) {
		ret = errno_to_blk_status(error);
		goto fail;
	}

	map_length = min(map_length, length);
	if (use_append)
		map_length = min(map_length, fs_info->max_zone_append_size);

	if (map_length < length) {
		bbio = btrfs_split_bio(fs_info, bbio, map_length, use_append);
		bio = &bbio->bio;
	}

	/*
	 * Save the iter for the end_io handler and preload the checksums for
	 * data reads.
	 */
	if (bio_op(bio) == REQ_OP_READ && is_data_bbio(bbio)) {
		bbio->saved_iter = bio->bi_iter;
		ret = btrfs_lookup_bio_sums(bbio);
		if (ret)
			goto fail_put_bio;
	}

	if (btrfs_op(bio) == BTRFS_MAP_WRITE) {
		if (use_append) {
			bio->bi_opf &= ~REQ_OP_WRITE;
			bio->bi_opf |= REQ_OP_ZONE_APPEND;
		}

		if (is_data_bbio(bbio) && bioc &&
		    btrfs_need_stripe_tree_update(bioc->fs_info, bioc->map_type)) {
			/*
			 * No locking for the list update, as we only add to
			 * the list in the I/O submission path, and list
			 * iteration only happens in the completion path, which
			 * can't happen until after the last submission.
			 */
			btrfs_get_bioc(bioc);
			list_add_tail(&bioc->rst_ordered_entry, &bbio->ordered->bioc_list);
		}

		/*
		 * Csum items for reloc roots have already been cloned at this
		 * point, so they are handled as part of the no-checksum case.
		 */
		if (inode && !(inode->flags & BTRFS_INODE_NODATASUM) &&
		    !test_bit(BTRFS_FS_STATE_NO_CSUMS, &fs_info->fs_state) &&
		    !btrfs_is_data_reloc_root(inode->root)) {
			if (should_async_write(bbio) &&
			    btrfs_wq_submit_bio(bbio, bioc, &smap, mirror_num))
				goto done;

			ret = btrfs_bio_csum(bbio);
			if (ret)
				goto fail_put_bio;
		} else if (use_append) {
			ret = btrfs_alloc_dummy_sum(bbio);
			if (ret)
				goto fail_put_bio;
		}
	}

	__btrfs_submit_bio(bio, bioc, &smap, mirror_num);
done:
	return map_length == length;

fail_put_bio:
	if (map_length < length)
		btrfs_cleanup_bio(bbio);
fail:
	btrfs_bio_counter_dec(fs_info);
	btrfs_bio_end_io(orig_bbio, ret);
	/* Do not submit another chunk */
	return true;
}

void btrfs_submit_bio(struct btrfs_bio *bbio, int mirror_num)
{
	/* If bbio->inode is not populated, its file_offset must be 0. */
	ASSERT(bbio->inode || bbio->file_offset == 0);

	while (!btrfs_submit_chunk(bbio, mirror_num))
		;
}

/*
 * Submit a repair write.
 *
 * This bypasses btrfs_submit_bio deliberately, as that writes all copies in a
 * RAID setup.  Here we only want to write the one bad copy, so we do the
 * mapping ourselves and submit the bio directly.
 *
 * The I/O is issued synchronously to block the repair read completion from
 * freeing the bio.
 */
int btrfs_repair_io_failure(struct btrfs_fs_info *fs_info, u64 ino, u64 start,
			    u64 length, u64 logical, struct page *page,
			    unsigned int pg_offset, int mirror_num)
{
	struct btrfs_io_stripe smap = { 0 };
	struct bio_vec bvec;
	struct bio bio;
	int ret = 0;

	ASSERT(!(fs_info->sb->s_flags & SB_RDONLY));
	BUG_ON(!mirror_num);

	if (btrfs_repair_one_zone(fs_info, logical))
		return 0;

	/*
	 * Avoid races with device replace and make sure our bioc has devices
	 * associated to its stripes that don't go away while we are doing the
	 * read repair operation.
	 */
	btrfs_bio_counter_inc_blocked(fs_info);
	ret = btrfs_map_repair_block(fs_info, &smap, logical, length, mirror_num);
	if (ret < 0)
		goto out_counter_dec;

	if (!smap.dev->bdev ||
	    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &smap.dev->dev_state)) {
		ret = -EIO;
		goto out_counter_dec;
	}

	bio_init(&bio, smap.dev->bdev, &bvec, 1, REQ_OP_WRITE | REQ_SYNC);
	bio.bi_iter.bi_sector = smap.physical >> SECTOR_SHIFT;
	__bio_add_page(&bio, page, length, pg_offset);
	ret = submit_bio_wait(&bio);
	if (ret) {
		/* try to remap that extent elsewhere? */
		btrfs_dev_stat_inc_and_print(smap.dev, BTRFS_DEV_STAT_WRITE_ERRS);
		goto out_bio_uninit;
	}

	btrfs_info_rl_in_rcu(fs_info,
		"read error corrected: ino %llu off %llu (dev %s sector %llu)",
			     ino, start, btrfs_dev_name(smap.dev),
			     smap.physical >> SECTOR_SHIFT);
	ret = 0;

out_bio_uninit:
	bio_uninit(&bio);
out_counter_dec:
	btrfs_bio_counter_dec(fs_info);
	return ret;
}

/*
 * Submit a btrfs_bio based repair write.
 *
 * If @dev_replace is true, the write would be submitted to dev-replace target.
 */
void btrfs_submit_repair_write(struct btrfs_bio *bbio, int mirror_num, bool dev_replace)
{
	struct btrfs_fs_info *fs_info = bbio->fs_info;
	u64 logical = bbio->bio.bi_iter.bi_sector << SECTOR_SHIFT;
	u64 length = bbio->bio.bi_iter.bi_size;
	struct btrfs_io_stripe smap = { 0 };
	int ret;

	ASSERT(fs_info);
	ASSERT(mirror_num > 0);
	ASSERT(btrfs_op(&bbio->bio) == BTRFS_MAP_WRITE);
	ASSERT(!bbio->inode);

	btrfs_bio_counter_inc_blocked(fs_info);
	ret = btrfs_map_repair_block(fs_info, &smap, logical, length, mirror_num);
	if (ret < 0)
		goto fail;

	if (dev_replace) {
		ASSERT(smap.dev == fs_info->dev_replace.srcdev);
		smap.dev = fs_info->dev_replace.tgtdev;
	}
	__btrfs_submit_bio(&bbio->bio, NULL, &smap, mirror_num);
	return;

fail:
	btrfs_bio_counter_dec(fs_info);
	btrfs_bio_end_io(bbio, errno_to_blk_status(ret));
}

int __init btrfs_bioset_init(void)
{
	if (bioset_init(&btrfs_bioset, BIO_POOL_SIZE,
			offsetof(struct btrfs_bio, bio),
			BIOSET_NEED_BVECS))
		return -ENOMEM;
	if (bioset_init(&btrfs_clone_bioset, BIO_POOL_SIZE,
			offsetof(struct btrfs_bio, bio), 0))
		goto out_free_bioset;
	if (bioset_init(&btrfs_repair_bioset, BIO_POOL_SIZE,
			offsetof(struct btrfs_bio, bio),
			BIOSET_NEED_BVECS))
		goto out_free_clone_bioset;
	if (mempool_init_kmalloc_pool(&btrfs_failed_bio_pool, BIO_POOL_SIZE,
				      sizeof(struct btrfs_failed_bio)))
		goto out_free_repair_bioset;
	return 0;

out_free_repair_bioset:
	bioset_exit(&btrfs_repair_bioset);
out_free_clone_bioset:
	bioset_exit(&btrfs_clone_bioset);
out_free_bioset:
	bioset_exit(&btrfs_bioset);
	return -ENOMEM;
}

void __cold btrfs_bioset_exit(void)
{
	mempool_exit(&btrfs_failed_bio_pool);
	bioset_exit(&btrfs_repair_bioset);
	bioset_exit(&btrfs_clone_bioset);
	bioset_exit(&btrfs_bioset);
}
