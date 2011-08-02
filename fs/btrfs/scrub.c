/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
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

#include <linux/blkdev.h>
#include "ctree.h"
#include "volumes.h"
#include "disk-io.h"
#include "ordered-data.h"

/*
 * This is only the first step towards a full-features scrub. It reads all
 * extent and super block and verifies the checksums. In case a bad checksum
 * is found or the extent cannot be read, good data will be written back if
 * any can be found.
 *
 * Future enhancements:
 *  - To enhance the performance, better read-ahead strategies for the
 *    extent-tree can be employed.
 *  - In case an unrepairable extent is encountered, track which files are
 *    affected and report them
 *  - In case of a read error on files with nodatasum, map the file and read
 *    the extent to trigger a writeback of the good copy
 *  - track and record media errors, throw out bad devices
 *  - add a mode to also read unallocated space
 *  - make the prefetch cancellable
 */

struct scrub_bio;
struct scrub_page;
struct scrub_dev;
static void scrub_bio_end_io(struct bio *bio, int err);
static void scrub_checksum(struct btrfs_work *work);
static int scrub_checksum_data(struct scrub_dev *sdev,
			       struct scrub_page *spag, void *buffer);
static int scrub_checksum_tree_block(struct scrub_dev *sdev,
				     struct scrub_page *spag, u64 logical,
				     void *buffer);
static int scrub_checksum_super(struct scrub_bio *sbio, void *buffer);
static int scrub_fixup_check(struct scrub_bio *sbio, int ix);
static void scrub_fixup_end_io(struct bio *bio, int err);
static int scrub_fixup_io(int rw, struct block_device *bdev, sector_t sector,
			  struct page *page);
static void scrub_fixup(struct scrub_bio *sbio, int ix);

#define SCRUB_PAGES_PER_BIO	16	/* 64k per bio */
#define SCRUB_BIOS_PER_DEV	16	/* 1 MB per device in flight */

struct scrub_page {
	u64			flags;  /* extent flags */
	u64			generation;
	u64			mirror_num;
	int			have_csum;
	u8			csum[BTRFS_CSUM_SIZE];
};

struct scrub_bio {
	int			index;
	struct scrub_dev	*sdev;
	struct bio		*bio;
	int			err;
	u64			logical;
	u64			physical;
	struct scrub_page	spag[SCRUB_PAGES_PER_BIO];
	u64			count;
	int			next_free;
	struct btrfs_work	work;
};

struct scrub_dev {
	struct scrub_bio	*bios[SCRUB_BIOS_PER_DEV];
	struct btrfs_device	*dev;
	int			first_free;
	int			curr;
	atomic_t		in_flight;
	spinlock_t		list_lock;
	wait_queue_head_t	list_wait;
	u16			csum_size;
	struct list_head	csum_list;
	atomic_t		cancel_req;
	int			readonly;
	/*
	 * statistics
	 */
	struct btrfs_scrub_progress stat;
	spinlock_t		stat_lock;
};

static void scrub_free_csums(struct scrub_dev *sdev)
{
	while (!list_empty(&sdev->csum_list)) {
		struct btrfs_ordered_sum *sum;
		sum = list_first_entry(&sdev->csum_list,
				       struct btrfs_ordered_sum, list);
		list_del(&sum->list);
		kfree(sum);
	}
}

static void scrub_free_bio(struct bio *bio)
{
	int i;
	struct page *last_page = NULL;

	if (!bio)
		return;

	for (i = 0; i < bio->bi_vcnt; ++i) {
		if (bio->bi_io_vec[i].bv_page == last_page)
			continue;
		last_page = bio->bi_io_vec[i].bv_page;
		__free_page(last_page);
	}
	bio_put(bio);
}

static noinline_for_stack void scrub_free_dev(struct scrub_dev *sdev)
{
	int i;

	if (!sdev)
		return;

	for (i = 0; i < SCRUB_BIOS_PER_DEV; ++i) {
		struct scrub_bio *sbio = sdev->bios[i];

		if (!sbio)
			break;

		scrub_free_bio(sbio->bio);
		kfree(sbio);
	}

	scrub_free_csums(sdev);
	kfree(sdev);
}

static noinline_for_stack
struct scrub_dev *scrub_setup_dev(struct btrfs_device *dev)
{
	struct scrub_dev *sdev;
	int		i;
	struct btrfs_fs_info *fs_info = dev->dev_root->fs_info;

	sdev = kzalloc(sizeof(*sdev), GFP_NOFS);
	if (!sdev)
		goto nomem;
	sdev->dev = dev;
	for (i = 0; i < SCRUB_BIOS_PER_DEV; ++i) {
		struct scrub_bio *sbio;

		sbio = kzalloc(sizeof(*sbio), GFP_NOFS);
		if (!sbio)
			goto nomem;
		sdev->bios[i] = sbio;

		sbio->index = i;
		sbio->sdev = sdev;
		sbio->count = 0;
		sbio->work.func = scrub_checksum;

		if (i != SCRUB_BIOS_PER_DEV-1)
			sdev->bios[i]->next_free = i + 1;
		 else
			sdev->bios[i]->next_free = -1;
	}
	sdev->first_free = 0;
	sdev->curr = -1;
	atomic_set(&sdev->in_flight, 0);
	atomic_set(&sdev->cancel_req, 0);
	sdev->csum_size = btrfs_super_csum_size(&fs_info->super_copy);
	INIT_LIST_HEAD(&sdev->csum_list);

	spin_lock_init(&sdev->list_lock);
	spin_lock_init(&sdev->stat_lock);
	init_waitqueue_head(&sdev->list_wait);
	return sdev;

nomem:
	scrub_free_dev(sdev);
	return ERR_PTR(-ENOMEM);
}

/*
 * scrub_recheck_error gets called when either verification of the page
 * failed or the bio failed to read, e.g. with EIO. In the latter case,
 * recheck_error gets called for every page in the bio, even though only
 * one may be bad
 */
static void scrub_recheck_error(struct scrub_bio *sbio, int ix)
{
	if (sbio->err) {
		if (scrub_fixup_io(READ, sbio->sdev->dev->bdev,
				   (sbio->physical + ix * PAGE_SIZE) >> 9,
				   sbio->bio->bi_io_vec[ix].bv_page) == 0) {
			if (scrub_fixup_check(sbio, ix) == 0)
				return;
		}
	}

	scrub_fixup(sbio, ix);
}

static int scrub_fixup_check(struct scrub_bio *sbio, int ix)
{
	int ret = 1;
	struct page *page;
	void *buffer;
	u64 flags = sbio->spag[ix].flags;

	page = sbio->bio->bi_io_vec[ix].bv_page;
	buffer = kmap_atomic(page, KM_USER0);
	if (flags & BTRFS_EXTENT_FLAG_DATA) {
		ret = scrub_checksum_data(sbio->sdev,
					  sbio->spag + ix, buffer);
	} else if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		ret = scrub_checksum_tree_block(sbio->sdev,
						sbio->spag + ix,
						sbio->logical + ix * PAGE_SIZE,
						buffer);
	} else {
		WARN_ON(1);
	}
	kunmap_atomic(buffer, KM_USER0);

	return ret;
}

static void scrub_fixup_end_io(struct bio *bio, int err)
{
	complete((struct completion *)bio->bi_private);
}

static void scrub_fixup(struct scrub_bio *sbio, int ix)
{
	struct scrub_dev *sdev = sbio->sdev;
	struct btrfs_fs_info *fs_info = sdev->dev->dev_root->fs_info;
	struct btrfs_mapping_tree *map_tree = &fs_info->mapping_tree;
	struct btrfs_multi_bio *multi = NULL;
	u64 logical = sbio->logical + ix * PAGE_SIZE;
	u64 length;
	int i;
	int ret;
	DECLARE_COMPLETION_ONSTACK(complete);

	if ((sbio->spag[ix].flags & BTRFS_EXTENT_FLAG_DATA) &&
	    (sbio->spag[ix].have_csum == 0)) {
		/*
		 * nodatasum, don't try to fix anything
		 * FIXME: we can do better, open the inode and trigger a
		 * writeback
		 */
		goto uncorrectable;
	}

	length = PAGE_SIZE;
	ret = btrfs_map_block(map_tree, REQ_WRITE, logical, &length,
			      &multi, 0);
	if (ret || !multi || length < PAGE_SIZE) {
		printk(KERN_ERR
		       "scrub_fixup: btrfs_map_block failed us for %llu\n",
		       (unsigned long long)logical);
		WARN_ON(1);
		return;
	}

	if (multi->num_stripes == 1)
		/* there aren't any replicas */
		goto uncorrectable;

	/*
	 * first find a good copy
	 */
	for (i = 0; i < multi->num_stripes; ++i) {
		if (i == sbio->spag[ix].mirror_num)
			continue;

		if (scrub_fixup_io(READ, multi->stripes[i].dev->bdev,
				   multi->stripes[i].physical >> 9,
				   sbio->bio->bi_io_vec[ix].bv_page)) {
			/* I/O-error, this is not a good copy */
			continue;
		}

		if (scrub_fixup_check(sbio, ix) == 0)
			break;
	}
	if (i == multi->num_stripes)
		goto uncorrectable;

	if (!sdev->readonly) {
		/*
		 * bi_io_vec[ix].bv_page now contains good data, write it back
		 */
		if (scrub_fixup_io(WRITE, sdev->dev->bdev,
				   (sbio->physical + ix * PAGE_SIZE) >> 9,
				   sbio->bio->bi_io_vec[ix].bv_page)) {
			/* I/O-error, writeback failed, give up */
			goto uncorrectable;
		}
	}

	kfree(multi);
	spin_lock(&sdev->stat_lock);
	++sdev->stat.corrected_errors;
	spin_unlock(&sdev->stat_lock);

	if (printk_ratelimit())
		printk(KERN_ERR "btrfs: fixed up at %llu\n",
		       (unsigned long long)logical);
	return;

uncorrectable:
	kfree(multi);
	spin_lock(&sdev->stat_lock);
	++sdev->stat.uncorrectable_errors;
	spin_unlock(&sdev->stat_lock);

	if (printk_ratelimit())
		printk(KERN_ERR "btrfs: unable to fixup at %llu\n",
			 (unsigned long long)logical);
}

static int scrub_fixup_io(int rw, struct block_device *bdev, sector_t sector,
			 struct page *page)
{
	struct bio *bio = NULL;
	int ret;
	DECLARE_COMPLETION_ONSTACK(complete);

	bio = bio_alloc(GFP_NOFS, 1);
	bio->bi_bdev = bdev;
	bio->bi_sector = sector;
	bio_add_page(bio, page, PAGE_SIZE, 0);
	bio->bi_end_io = scrub_fixup_end_io;
	bio->bi_private = &complete;
	submit_bio(rw, bio);

	/* this will also unplug the queue */
	wait_for_completion(&complete);

	ret = !test_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_put(bio);
	return ret;
}

static void scrub_bio_end_io(struct bio *bio, int err)
{
	struct scrub_bio *sbio = bio->bi_private;
	struct scrub_dev *sdev = sbio->sdev;
	struct btrfs_fs_info *fs_info = sdev->dev->dev_root->fs_info;

	sbio->err = err;
	sbio->bio = bio;

	btrfs_queue_worker(&fs_info->scrub_workers, &sbio->work);
}

static void scrub_checksum(struct btrfs_work *work)
{
	struct scrub_bio *sbio = container_of(work, struct scrub_bio, work);
	struct scrub_dev *sdev = sbio->sdev;
	struct page *page;
	void *buffer;
	int i;
	u64 flags;
	u64 logical;
	int ret;

	if (sbio->err) {
		for (i = 0; i < sbio->count; ++i)
			scrub_recheck_error(sbio, i);

		sbio->bio->bi_flags &= ~(BIO_POOL_MASK - 1);
		sbio->bio->bi_flags |= 1 << BIO_UPTODATE;
		sbio->bio->bi_phys_segments = 0;
		sbio->bio->bi_idx = 0;

		for (i = 0; i < sbio->count; i++) {
			struct bio_vec *bi;
			bi = &sbio->bio->bi_io_vec[i];
			bi->bv_offset = 0;
			bi->bv_len = PAGE_SIZE;
		}

		spin_lock(&sdev->stat_lock);
		++sdev->stat.read_errors;
		spin_unlock(&sdev->stat_lock);
		goto out;
	}
	for (i = 0; i < sbio->count; ++i) {
		page = sbio->bio->bi_io_vec[i].bv_page;
		buffer = kmap_atomic(page, KM_USER0);
		flags = sbio->spag[i].flags;
		logical = sbio->logical + i * PAGE_SIZE;
		ret = 0;
		if (flags & BTRFS_EXTENT_FLAG_DATA) {
			ret = scrub_checksum_data(sdev, sbio->spag + i, buffer);
		} else if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
			ret = scrub_checksum_tree_block(sdev, sbio->spag + i,
							logical, buffer);
		} else if (flags & BTRFS_EXTENT_FLAG_SUPER) {
			BUG_ON(i);
			(void)scrub_checksum_super(sbio, buffer);
		} else {
			WARN_ON(1);
		}
		kunmap_atomic(buffer, KM_USER0);
		if (ret)
			scrub_recheck_error(sbio, i);
	}

out:
	scrub_free_bio(sbio->bio);
	sbio->bio = NULL;
	spin_lock(&sdev->list_lock);
	sbio->next_free = sdev->first_free;
	sdev->first_free = sbio->index;
	spin_unlock(&sdev->list_lock);
	atomic_dec(&sdev->in_flight);
	wake_up(&sdev->list_wait);
}

static int scrub_checksum_data(struct scrub_dev *sdev,
			       struct scrub_page *spag, void *buffer)
{
	u8 csum[BTRFS_CSUM_SIZE];
	u32 crc = ~(u32)0;
	int fail = 0;
	struct btrfs_root *root = sdev->dev->dev_root;

	if (!spag->have_csum)
		return 0;

	crc = btrfs_csum_data(root, buffer, crc, PAGE_SIZE);
	btrfs_csum_final(crc, csum);
	if (memcmp(csum, spag->csum, sdev->csum_size))
		fail = 1;

	spin_lock(&sdev->stat_lock);
	++sdev->stat.data_extents_scrubbed;
	sdev->stat.data_bytes_scrubbed += PAGE_SIZE;
	if (fail)
		++sdev->stat.csum_errors;
	spin_unlock(&sdev->stat_lock);

	return fail;
}

static int scrub_checksum_tree_block(struct scrub_dev *sdev,
				     struct scrub_page *spag, u64 logical,
				     void *buffer)
{
	struct btrfs_header *h;
	struct btrfs_root *root = sdev->dev->dev_root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u8 csum[BTRFS_CSUM_SIZE];
	u32 crc = ~(u32)0;
	int fail = 0;
	int crc_fail = 0;

	/*
	 * we don't use the getter functions here, as we
	 * a) don't have an extent buffer and
	 * b) the page is already kmapped
	 */
	h = (struct btrfs_header *)buffer;

	if (logical != le64_to_cpu(h->bytenr))
		++fail;

	if (spag->generation != le64_to_cpu(h->generation))
		++fail;

	if (memcmp(h->fsid, fs_info->fsid, BTRFS_UUID_SIZE))
		++fail;

	if (memcmp(h->chunk_tree_uuid, fs_info->chunk_tree_uuid,
		   BTRFS_UUID_SIZE))
		++fail;

	crc = btrfs_csum_data(root, buffer + BTRFS_CSUM_SIZE, crc,
			      PAGE_SIZE - BTRFS_CSUM_SIZE);
	btrfs_csum_final(crc, csum);
	if (memcmp(csum, h->csum, sdev->csum_size))
		++crc_fail;

	spin_lock(&sdev->stat_lock);
	++sdev->stat.tree_extents_scrubbed;
	sdev->stat.tree_bytes_scrubbed += PAGE_SIZE;
	if (crc_fail)
		++sdev->stat.csum_errors;
	if (fail)
		++sdev->stat.verify_errors;
	spin_unlock(&sdev->stat_lock);

	return fail || crc_fail;
}

static int scrub_checksum_super(struct scrub_bio *sbio, void *buffer)
{
	struct btrfs_super_block *s;
	u64 logical;
	struct scrub_dev *sdev = sbio->sdev;
	struct btrfs_root *root = sdev->dev->dev_root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u8 csum[BTRFS_CSUM_SIZE];
	u32 crc = ~(u32)0;
	int fail = 0;

	s = (struct btrfs_super_block *)buffer;
	logical = sbio->logical;

	if (logical != le64_to_cpu(s->bytenr))
		++fail;

	if (sbio->spag[0].generation != le64_to_cpu(s->generation))
		++fail;

	if (memcmp(s->fsid, fs_info->fsid, BTRFS_UUID_SIZE))
		++fail;

	crc = btrfs_csum_data(root, buffer + BTRFS_CSUM_SIZE, crc,
			      PAGE_SIZE - BTRFS_CSUM_SIZE);
	btrfs_csum_final(crc, csum);
	if (memcmp(csum, s->csum, sbio->sdev->csum_size))
		++fail;

	if (fail) {
		/*
		 * if we find an error in a super block, we just report it.
		 * They will get written with the next transaction commit
		 * anyway
		 */
		spin_lock(&sdev->stat_lock);
		++sdev->stat.super_errors;
		spin_unlock(&sdev->stat_lock);
	}

	return fail;
}

static int scrub_submit(struct scrub_dev *sdev)
{
	struct scrub_bio *sbio;
	struct bio *bio;
	int i;

	if (sdev->curr == -1)
		return 0;

	sbio = sdev->bios[sdev->curr];

	bio = bio_alloc(GFP_NOFS, sbio->count);
	if (!bio)
		goto nomem;

	bio->bi_private = sbio;
	bio->bi_end_io = scrub_bio_end_io;
	bio->bi_bdev = sdev->dev->bdev;
	bio->bi_sector = sbio->physical >> 9;

	for (i = 0; i < sbio->count; ++i) {
		struct page *page;
		int ret;

		page = alloc_page(GFP_NOFS);
		if (!page)
			goto nomem;

		ret = bio_add_page(bio, page, PAGE_SIZE, 0);
		if (!ret) {
			__free_page(page);
			goto nomem;
		}
	}

	sbio->err = 0;
	sdev->curr = -1;
	atomic_inc(&sdev->in_flight);

	submit_bio(READ, bio);

	return 0;

nomem:
	scrub_free_bio(bio);

	return -ENOMEM;
}

static int scrub_page(struct scrub_dev *sdev, u64 logical, u64 len,
		      u64 physical, u64 flags, u64 gen, u64 mirror_num,
		      u8 *csum, int force)
{
	struct scrub_bio *sbio;

again:
	/*
	 * grab a fresh bio or wait for one to become available
	 */
	while (sdev->curr == -1) {
		spin_lock(&sdev->list_lock);
		sdev->curr = sdev->first_free;
		if (sdev->curr != -1) {
			sdev->first_free = sdev->bios[sdev->curr]->next_free;
			sdev->bios[sdev->curr]->next_free = -1;
			sdev->bios[sdev->curr]->count = 0;
			spin_unlock(&sdev->list_lock);
		} else {
			spin_unlock(&sdev->list_lock);
			wait_event(sdev->list_wait, sdev->first_free != -1);
		}
	}
	sbio = sdev->bios[sdev->curr];
	if (sbio->count == 0) {
		sbio->physical = physical;
		sbio->logical = logical;
	} else if (sbio->physical + sbio->count * PAGE_SIZE != physical ||
		   sbio->logical + sbio->count * PAGE_SIZE != logical) {
		int ret;

		ret = scrub_submit(sdev);
		if (ret)
			return ret;
		goto again;
	}
	sbio->spag[sbio->count].flags = flags;
	sbio->spag[sbio->count].generation = gen;
	sbio->spag[sbio->count].have_csum = 0;
	sbio->spag[sbio->count].mirror_num = mirror_num;
	if (csum) {
		sbio->spag[sbio->count].have_csum = 1;
		memcpy(sbio->spag[sbio->count].csum, csum, sdev->csum_size);
	}
	++sbio->count;
	if (sbio->count == SCRUB_PAGES_PER_BIO || force) {
		int ret;

		ret = scrub_submit(sdev);
		if (ret)
			return ret;
	}

	return 0;
}

static int scrub_find_csum(struct scrub_dev *sdev, u64 logical, u64 len,
			   u8 *csum)
{
	struct btrfs_ordered_sum *sum = NULL;
	int ret = 0;
	unsigned long i;
	unsigned long num_sectors;
	u32 sectorsize = sdev->dev->dev_root->sectorsize;

	while (!list_empty(&sdev->csum_list)) {
		sum = list_first_entry(&sdev->csum_list,
				       struct btrfs_ordered_sum, list);
		if (sum->bytenr > logical)
			return 0;
		if (sum->bytenr + sum->len > logical)
			break;

		++sdev->stat.csum_discards;
		list_del(&sum->list);
		kfree(sum);
		sum = NULL;
	}
	if (!sum)
		return 0;

	num_sectors = sum->len / sectorsize;
	for (i = 0; i < num_sectors; ++i) {
		if (sum->sums[i].bytenr == logical) {
			memcpy(csum, &sum->sums[i].sum, sdev->csum_size);
			ret = 1;
			break;
		}
	}
	if (ret && i == num_sectors - 1) {
		list_del(&sum->list);
		kfree(sum);
	}
	return ret;
}

/* scrub extent tries to collect up to 64 kB for each bio */
static int scrub_extent(struct scrub_dev *sdev, u64 logical, u64 len,
			u64 physical, u64 flags, u64 gen, u64 mirror_num)
{
	int ret;
	u8 csum[BTRFS_CSUM_SIZE];

	while (len) {
		u64 l = min_t(u64, len, PAGE_SIZE);
		int have_csum = 0;

		if (flags & BTRFS_EXTENT_FLAG_DATA) {
			/* push csums to sbio */
			have_csum = scrub_find_csum(sdev, logical, l, csum);
			if (have_csum == 0)
				++sdev->stat.no_csum;
		}
		ret = scrub_page(sdev, logical, l, physical, flags, gen,
				 mirror_num, have_csum ? csum : NULL, 0);
		if (ret)
			return ret;
		len -= l;
		logical += l;
		physical += l;
	}
	return 0;
}

static noinline_for_stack int scrub_stripe(struct scrub_dev *sdev,
	struct map_lookup *map, int num, u64 base, u64 length)
{
	struct btrfs_path *path;
	struct btrfs_fs_info *fs_info = sdev->dev->dev_root->fs_info;
	struct btrfs_root *root = fs_info->extent_root;
	struct btrfs_root *csum_root = fs_info->csum_root;
	struct btrfs_extent_item *extent;
	struct blk_plug plug;
	u64 flags;
	int ret;
	int slot;
	int i;
	u64 nstripes;
	int start_stripe;
	struct extent_buffer *l;
	struct btrfs_key key;
	u64 physical;
	u64 logical;
	u64 generation;
	u64 mirror_num;

	u64 increment = map->stripe_len;
	u64 offset;

	nstripes = length;
	offset = 0;
	do_div(nstripes, map->stripe_len);
	if (map->type & BTRFS_BLOCK_GROUP_RAID0) {
		offset = map->stripe_len * num;
		increment = map->stripe_len * map->num_stripes;
		mirror_num = 0;
	} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
		int factor = map->num_stripes / map->sub_stripes;
		offset = map->stripe_len * (num / map->sub_stripes);
		increment = map->stripe_len * factor;
		mirror_num = num % map->sub_stripes;
	} else if (map->type & BTRFS_BLOCK_GROUP_RAID1) {
		increment = map->stripe_len;
		mirror_num = num % map->num_stripes;
	} else if (map->type & BTRFS_BLOCK_GROUP_DUP) {
		increment = map->stripe_len;
		mirror_num = num % map->num_stripes;
	} else {
		increment = map->stripe_len;
		mirror_num = 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 2;
	path->search_commit_root = 1;
	path->skip_locking = 1;

	/*
	 * find all extents for each stripe and just read them to get
	 * them into the page cache
	 * FIXME: we can do better. build a more intelligent prefetching
	 */
	logical = base + offset;
	physical = map->stripes[num].physical;
	ret = 0;
	for (i = 0; i < nstripes; ++i) {
		key.objectid = logical;
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = (u64)0;

		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out_noplug;

		/*
		 * we might miss half an extent here, but that doesn't matter,
		 * as it's only the prefetch
		 */
		while (1) {
			l = path->nodes[0];
			slot = path->slots[0];
			if (slot >= btrfs_header_nritems(l)) {
				ret = btrfs_next_leaf(root, path);
				if (ret == 0)
					continue;
				if (ret < 0)
					goto out_noplug;

				break;
			}
			btrfs_item_key_to_cpu(l, &key, slot);

			if (key.objectid >= logical + map->stripe_len)
				break;

			path->slots[0]++;
		}
		btrfs_release_path(path);
		logical += increment;
		physical += map->stripe_len;
		cond_resched();
	}

	/*
	 * collect all data csums for the stripe to avoid seeking during
	 * the scrub. This might currently (crc32) end up to be about 1MB
	 */
	start_stripe = 0;
	blk_start_plug(&plug);
again:
	logical = base + offset + start_stripe * increment;
	for (i = start_stripe; i < nstripes; ++i) {
		ret = btrfs_lookup_csums_range(csum_root, logical,
					       logical + map->stripe_len - 1,
					       &sdev->csum_list, 1);
		if (ret)
			goto out;

		logical += increment;
		cond_resched();
	}
	/*
	 * now find all extents for each stripe and scrub them
	 */
	logical = base + offset + start_stripe * increment;
	physical = map->stripes[num].physical + start_stripe * map->stripe_len;
	ret = 0;
	for (i = start_stripe; i < nstripes; ++i) {
		/*
		 * canceled?
		 */
		if (atomic_read(&fs_info->scrub_cancel_req) ||
		    atomic_read(&sdev->cancel_req)) {
			ret = -ECANCELED;
			goto out;
		}
		/*
		 * check to see if we have to pause
		 */
		if (atomic_read(&fs_info->scrub_pause_req)) {
			/* push queued extents */
			scrub_submit(sdev);
			wait_event(sdev->list_wait,
				   atomic_read(&sdev->in_flight) == 0);
			atomic_inc(&fs_info->scrubs_paused);
			wake_up(&fs_info->scrub_pause_wait);
			mutex_lock(&fs_info->scrub_lock);
			while (atomic_read(&fs_info->scrub_pause_req)) {
				mutex_unlock(&fs_info->scrub_lock);
				wait_event(fs_info->scrub_pause_wait,
				   atomic_read(&fs_info->scrub_pause_req) == 0);
				mutex_lock(&fs_info->scrub_lock);
			}
			atomic_dec(&fs_info->scrubs_paused);
			mutex_unlock(&fs_info->scrub_lock);
			wake_up(&fs_info->scrub_pause_wait);
			scrub_free_csums(sdev);
			start_stripe = i;
			goto again;
		}

		key.objectid = logical;
		key.type = BTRFS_EXTENT_ITEM_KEY;
		key.offset = (u64)0;

		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			ret = btrfs_previous_item(root, path, 0,
						  BTRFS_EXTENT_ITEM_KEY);
			if (ret < 0)
				goto out;
			if (ret > 0) {
				/* there's no smaller item, so stick with the
				 * larger one */
				btrfs_release_path(path);
				ret = btrfs_search_slot(NULL, root, &key,
							path, 0, 0);
				if (ret < 0)
					goto out;
			}
		}

		while (1) {
			l = path->nodes[0];
			slot = path->slots[0];
			if (slot >= btrfs_header_nritems(l)) {
				ret = btrfs_next_leaf(root, path);
				if (ret == 0)
					continue;
				if (ret < 0)
					goto out;

				break;
			}
			btrfs_item_key_to_cpu(l, &key, slot);

			if (key.objectid + key.offset <= logical)
				goto next;

			if (key.objectid >= logical + map->stripe_len)
				break;

			if (btrfs_key_type(&key) != BTRFS_EXTENT_ITEM_KEY)
				goto next;

			extent = btrfs_item_ptr(l, slot,
						struct btrfs_extent_item);
			flags = btrfs_extent_flags(l, extent);
			generation = btrfs_extent_generation(l, extent);

			if (key.objectid < logical &&
			    (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK)) {
				printk(KERN_ERR
				       "btrfs scrub: tree block %llu spanning "
				       "stripes, ignored. logical=%llu\n",
				       (unsigned long long)key.objectid,
				       (unsigned long long)logical);
				goto next;
			}

			/*
			 * trim extent to this stripe
			 */
			if (key.objectid < logical) {
				key.offset -= logical - key.objectid;
				key.objectid = logical;
			}
			if (key.objectid + key.offset >
			    logical + map->stripe_len) {
				key.offset = logical + map->stripe_len -
					     key.objectid;
			}

			ret = scrub_extent(sdev, key.objectid, key.offset,
					   key.objectid - logical + physical,
					   flags, generation, mirror_num);
			if (ret)
				goto out;

next:
			path->slots[0]++;
		}
		btrfs_release_path(path);
		logical += increment;
		physical += map->stripe_len;
		spin_lock(&sdev->stat_lock);
		sdev->stat.last_physical = physical;
		spin_unlock(&sdev->stat_lock);
	}
	/* push queued extents */
	scrub_submit(sdev);

out:
	blk_finish_plug(&plug);
out_noplug:
	btrfs_free_path(path);
	return ret < 0 ? ret : 0;
}

static noinline_for_stack int scrub_chunk(struct scrub_dev *sdev,
	u64 chunk_tree, u64 chunk_objectid, u64 chunk_offset, u64 length)
{
	struct btrfs_mapping_tree *map_tree =
		&sdev->dev->dev_root->fs_info->mapping_tree;
	struct map_lookup *map;
	struct extent_map *em;
	int i;
	int ret = -EINVAL;

	read_lock(&map_tree->map_tree.lock);
	em = lookup_extent_mapping(&map_tree->map_tree, chunk_offset, 1);
	read_unlock(&map_tree->map_tree.lock);

	if (!em)
		return -EINVAL;

	map = (struct map_lookup *)em->bdev;
	if (em->start != chunk_offset)
		goto out;

	if (em->len < length)
		goto out;

	for (i = 0; i < map->num_stripes; ++i) {
		if (map->stripes[i].dev == sdev->dev) {
			ret = scrub_stripe(sdev, map, i, chunk_offset, length);
			if (ret)
				goto out;
		}
	}
out:
	free_extent_map(em);

	return ret;
}

static noinline_for_stack
int scrub_enumerate_chunks(struct scrub_dev *sdev, u64 start, u64 end)
{
	struct btrfs_dev_extent *dev_extent = NULL;
	struct btrfs_path *path;
	struct btrfs_root *root = sdev->dev->dev_root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	u64 length;
	u64 chunk_tree;
	u64 chunk_objectid;
	u64 chunk_offset;
	int ret;
	int slot;
	struct extent_buffer *l;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_block_group_cache *cache;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = 2;
	path->search_commit_root = 1;
	path->skip_locking = 1;

	key.objectid = sdev->dev->devid;
	key.offset = 0ull;
	key.type = BTRFS_DEV_EXTENT_KEY;


	while (1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			break;
		if (ret > 0) {
			if (path->slots[0] >=
			    btrfs_header_nritems(path->nodes[0])) {
				ret = btrfs_next_leaf(root, path);
				if (ret)
					break;
			}
		}

		l = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(l, &found_key, slot);

		if (found_key.objectid != sdev->dev->devid)
			break;

		if (btrfs_key_type(&found_key) != BTRFS_DEV_EXTENT_KEY)
			break;

		if (found_key.offset >= end)
			break;

		if (found_key.offset < key.offset)
			break;

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		length = btrfs_dev_extent_length(l, dev_extent);

		if (found_key.offset + length <= start) {
			key.offset = found_key.offset + length;
			btrfs_release_path(path);
			continue;
		}

		chunk_tree = btrfs_dev_extent_chunk_tree(l, dev_extent);
		chunk_objectid = btrfs_dev_extent_chunk_objectid(l, dev_extent);
		chunk_offset = btrfs_dev_extent_chunk_offset(l, dev_extent);

		/*
		 * get a reference on the corresponding block group to prevent
		 * the chunk from going away while we scrub it
		 */
		cache = btrfs_lookup_block_group(fs_info, chunk_offset);
		if (!cache) {
			ret = -ENOENT;
			break;
		}
		ret = scrub_chunk(sdev, chunk_tree, chunk_objectid,
				  chunk_offset, length);
		btrfs_put_block_group(cache);
		if (ret)
			break;

		key.offset = found_key.offset + length;
		btrfs_release_path(path);
	}

	btrfs_free_path(path);

	/*
	 * ret can still be 1 from search_slot or next_leaf,
	 * that's not an error
	 */
	return ret < 0 ? ret : 0;
}

static noinline_for_stack int scrub_supers(struct scrub_dev *sdev)
{
	int	i;
	u64	bytenr;
	u64	gen;
	int	ret;
	struct btrfs_device *device = sdev->dev;
	struct btrfs_root *root = device->dev_root;

	gen = root->fs_info->last_trans_committed;

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		bytenr = btrfs_sb_offset(i);
		if (bytenr + BTRFS_SUPER_INFO_SIZE >= device->total_bytes)
			break;

		ret = scrub_page(sdev, bytenr, PAGE_SIZE, bytenr,
				 BTRFS_EXTENT_FLAG_SUPER, gen, i, NULL, 1);
		if (ret)
			return ret;
	}
	wait_event(sdev->list_wait, atomic_read(&sdev->in_flight) == 0);

	return 0;
}

/*
 * get a reference count on fs_info->scrub_workers. start worker if necessary
 */
static noinline_for_stack int scrub_workers_get(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	mutex_lock(&fs_info->scrub_lock);
	if (fs_info->scrub_workers_refcnt == 0) {
		btrfs_init_workers(&fs_info->scrub_workers, "scrub",
			   fs_info->thread_pool_size, &fs_info->generic_worker);
		fs_info->scrub_workers.idle_thresh = 4;
		btrfs_start_workers(&fs_info->scrub_workers, 1);
	}
	++fs_info->scrub_workers_refcnt;
	mutex_unlock(&fs_info->scrub_lock);

	return 0;
}

static noinline_for_stack void scrub_workers_put(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	mutex_lock(&fs_info->scrub_lock);
	if (--fs_info->scrub_workers_refcnt == 0)
		btrfs_stop_workers(&fs_info->scrub_workers);
	WARN_ON(fs_info->scrub_workers_refcnt < 0);
	mutex_unlock(&fs_info->scrub_lock);
}


int btrfs_scrub_dev(struct btrfs_root *root, u64 devid, u64 start, u64 end,
		    struct btrfs_scrub_progress *progress, int readonly)
{
	struct scrub_dev *sdev;
	struct btrfs_fs_info *fs_info = root->fs_info;
	int ret;
	struct btrfs_device *dev;

	if (btrfs_fs_closing(root->fs_info))
		return -EINVAL;

	/*
	 * check some assumptions
	 */
	if (root->sectorsize != PAGE_SIZE ||
	    root->sectorsize != root->leafsize ||
	    root->sectorsize != root->nodesize) {
		printk(KERN_ERR "btrfs_scrub: size assumptions fail\n");
		return -EINVAL;
	}

	ret = scrub_workers_get(root);
	if (ret)
		return ret;

	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	dev = btrfs_find_device(root, devid, NULL, NULL);
	if (!dev || dev->missing) {
		mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);
		scrub_workers_put(root);
		return -ENODEV;
	}
	mutex_lock(&fs_info->scrub_lock);

	if (!dev->in_fs_metadata) {
		mutex_unlock(&fs_info->scrub_lock);
		mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);
		scrub_workers_put(root);
		return -ENODEV;
	}

	if (dev->scrub_device) {
		mutex_unlock(&fs_info->scrub_lock);
		mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);
		scrub_workers_put(root);
		return -EINPROGRESS;
	}
	sdev = scrub_setup_dev(dev);
	if (IS_ERR(sdev)) {
		mutex_unlock(&fs_info->scrub_lock);
		mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);
		scrub_workers_put(root);
		return PTR_ERR(sdev);
	}
	sdev->readonly = readonly;
	dev->scrub_device = sdev;

	atomic_inc(&fs_info->scrubs_running);
	mutex_unlock(&fs_info->scrub_lock);
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);

	down_read(&fs_info->scrub_super_lock);
	ret = scrub_supers(sdev);
	up_read(&fs_info->scrub_super_lock);

	if (!ret)
		ret = scrub_enumerate_chunks(sdev, start, end);

	wait_event(sdev->list_wait, atomic_read(&sdev->in_flight) == 0);

	atomic_dec(&fs_info->scrubs_running);
	wake_up(&fs_info->scrub_pause_wait);

	if (progress)
		memcpy(progress, &sdev->stat, sizeof(*progress));

	mutex_lock(&fs_info->scrub_lock);
	dev->scrub_device = NULL;
	mutex_unlock(&fs_info->scrub_lock);

	scrub_free_dev(sdev);
	scrub_workers_put(root);

	return ret;
}

int btrfs_scrub_pause(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	mutex_lock(&fs_info->scrub_lock);
	atomic_inc(&fs_info->scrub_pause_req);
	while (atomic_read(&fs_info->scrubs_paused) !=
	       atomic_read(&fs_info->scrubs_running)) {
		mutex_unlock(&fs_info->scrub_lock);
		wait_event(fs_info->scrub_pause_wait,
			   atomic_read(&fs_info->scrubs_paused) ==
			   atomic_read(&fs_info->scrubs_running));
		mutex_lock(&fs_info->scrub_lock);
	}
	mutex_unlock(&fs_info->scrub_lock);

	return 0;
}

int btrfs_scrub_continue(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	atomic_dec(&fs_info->scrub_pause_req);
	wake_up(&fs_info->scrub_pause_wait);
	return 0;
}

int btrfs_scrub_pause_super(struct btrfs_root *root)
{
	down_write(&root->fs_info->scrub_super_lock);
	return 0;
}

int btrfs_scrub_continue_super(struct btrfs_root *root)
{
	up_write(&root->fs_info->scrub_super_lock);
	return 0;
}

int btrfs_scrub_cancel(struct btrfs_root *root)
{
	struct btrfs_fs_info *fs_info = root->fs_info;

	mutex_lock(&fs_info->scrub_lock);
	if (!atomic_read(&fs_info->scrubs_running)) {
		mutex_unlock(&fs_info->scrub_lock);
		return -ENOTCONN;
	}

	atomic_inc(&fs_info->scrub_cancel_req);
	while (atomic_read(&fs_info->scrubs_running)) {
		mutex_unlock(&fs_info->scrub_lock);
		wait_event(fs_info->scrub_pause_wait,
			   atomic_read(&fs_info->scrubs_running) == 0);
		mutex_lock(&fs_info->scrub_lock);
	}
	atomic_dec(&fs_info->scrub_cancel_req);
	mutex_unlock(&fs_info->scrub_lock);

	return 0;
}

int btrfs_scrub_cancel_dev(struct btrfs_root *root, struct btrfs_device *dev)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct scrub_dev *sdev;

	mutex_lock(&fs_info->scrub_lock);
	sdev = dev->scrub_device;
	if (!sdev) {
		mutex_unlock(&fs_info->scrub_lock);
		return -ENOTCONN;
	}
	atomic_inc(&sdev->cancel_req);
	while (dev->scrub_device) {
		mutex_unlock(&fs_info->scrub_lock);
		wait_event(fs_info->scrub_pause_wait,
			   dev->scrub_device == NULL);
		mutex_lock(&fs_info->scrub_lock);
	}
	mutex_unlock(&fs_info->scrub_lock);

	return 0;
}
int btrfs_scrub_cancel_devid(struct btrfs_root *root, u64 devid)
{
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_device *dev;
	int ret;

	/*
	 * we have to hold the device_list_mutex here so the device
	 * does not go away in cancel_dev. FIXME: find a better solution
	 */
	mutex_lock(&fs_info->fs_devices->device_list_mutex);
	dev = btrfs_find_device(root, devid, NULL, NULL);
	if (!dev) {
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);
		return -ENODEV;
	}
	ret = btrfs_scrub_cancel_dev(root, dev);
	mutex_unlock(&fs_info->fs_devices->device_list_mutex);

	return ret;
}

int btrfs_scrub_progress(struct btrfs_root *root, u64 devid,
			 struct btrfs_scrub_progress *progress)
{
	struct btrfs_device *dev;
	struct scrub_dev *sdev = NULL;

	mutex_lock(&root->fs_info->fs_devices->device_list_mutex);
	dev = btrfs_find_device(root, devid, NULL, NULL);
	if (dev)
		sdev = dev->scrub_device;
	if (sdev)
		memcpy(progress, &sdev->stat, sizeof(*progress));
	mutex_unlock(&root->fs_info->fs_devices->device_list_mutex);

	return dev ? (sdev ? 0 : -ENOTCONN) : -ENODEV;
}
