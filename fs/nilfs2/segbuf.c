/*
 * segbuf.c - NILFS segment buffer
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 *
 */

#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/crc32.h>
#include <linux/backing-dev.h>
#include <linux/slab.h>
#include "page.h"
#include "segbuf.h"


struct nilfs_write_info {
	struct the_nilfs       *nilfs;
	struct bio	       *bio;
	int			start, end; /* The region to be submitted */
	int			rest_blocks;
	int			max_pages;
	int			nr_vecs;
	sector_t		blocknr;
};

static int nilfs_segbuf_write(struct nilfs_segment_buffer *segbuf,
			      struct the_nilfs *nilfs);
static int nilfs_segbuf_wait(struct nilfs_segment_buffer *segbuf);

struct nilfs_segment_buffer *nilfs_segbuf_new(struct super_block *sb)
{
	struct nilfs_segment_buffer *segbuf;

	segbuf = kmem_cache_alloc(nilfs_segbuf_cachep, GFP_NOFS);
	if (unlikely(!segbuf))
		return NULL;

	segbuf->sb_super = sb;
	INIT_LIST_HEAD(&segbuf->sb_list);
	INIT_LIST_HEAD(&segbuf->sb_segsum_buffers);
	INIT_LIST_HEAD(&segbuf->sb_payload_buffers);
	segbuf->sb_super_root = NULL;

	init_completion(&segbuf->sb_bio_event);
	atomic_set(&segbuf->sb_err, 0);
	segbuf->sb_nbio = 0;

	return segbuf;
}

void nilfs_segbuf_free(struct nilfs_segment_buffer *segbuf)
{
	kmem_cache_free(nilfs_segbuf_cachep, segbuf);
}

void nilfs_segbuf_map(struct nilfs_segment_buffer *segbuf, __u64 segnum,
		     unsigned long offset, struct the_nilfs *nilfs)
{
	segbuf->sb_segnum = segnum;
	nilfs_get_segment_range(nilfs, segnum, &segbuf->sb_fseg_start,
				&segbuf->sb_fseg_end);

	segbuf->sb_pseg_start = segbuf->sb_fseg_start + offset;
	segbuf->sb_rest_blocks =
		segbuf->sb_fseg_end - segbuf->sb_pseg_start + 1;
}

/**
 * nilfs_segbuf_map_cont - map a new log behind a given log
 * @segbuf: new segment buffer
 * @prev: segment buffer containing a log to be continued
 */
void nilfs_segbuf_map_cont(struct nilfs_segment_buffer *segbuf,
			   struct nilfs_segment_buffer *prev)
{
	segbuf->sb_segnum = prev->sb_segnum;
	segbuf->sb_fseg_start = prev->sb_fseg_start;
	segbuf->sb_fseg_end = prev->sb_fseg_end;
	segbuf->sb_pseg_start = prev->sb_pseg_start + prev->sb_sum.nblocks;
	segbuf->sb_rest_blocks =
		segbuf->sb_fseg_end - segbuf->sb_pseg_start + 1;
}

void nilfs_segbuf_set_next_segnum(struct nilfs_segment_buffer *segbuf,
				  __u64 nextnum, struct the_nilfs *nilfs)
{
	segbuf->sb_nextnum = nextnum;
	segbuf->sb_sum.next = nilfs_get_segment_start_blocknr(nilfs, nextnum);
}

int nilfs_segbuf_extend_segsum(struct nilfs_segment_buffer *segbuf)
{
	struct buffer_head *bh;

	bh = sb_getblk(segbuf->sb_super,
		       segbuf->sb_pseg_start + segbuf->sb_sum.nsumblk);
	if (unlikely(!bh))
		return -ENOMEM;

	nilfs_segbuf_add_segsum_buffer(segbuf, bh);
	return 0;
}

int nilfs_segbuf_extend_payload(struct nilfs_segment_buffer *segbuf,
				struct buffer_head **bhp)
{
	struct buffer_head *bh;

	bh = sb_getblk(segbuf->sb_super,
		       segbuf->sb_pseg_start + segbuf->sb_sum.nblocks);
	if (unlikely(!bh))
		return -ENOMEM;

	nilfs_segbuf_add_payload_buffer(segbuf, bh);
	*bhp = bh;
	return 0;
}

int nilfs_segbuf_reset(struct nilfs_segment_buffer *segbuf, unsigned flags,
		       time_t ctime, __u64 cno)
{
	int err;

	segbuf->sb_sum.nblocks = segbuf->sb_sum.nsumblk = 0;
	err = nilfs_segbuf_extend_segsum(segbuf);
	if (unlikely(err))
		return err;

	segbuf->sb_sum.flags = flags;
	segbuf->sb_sum.sumbytes = sizeof(struct nilfs_segment_summary);
	segbuf->sb_sum.nfinfo = segbuf->sb_sum.nfileblk = 0;
	segbuf->sb_sum.ctime = ctime;
	segbuf->sb_sum.cno = cno;
	return 0;
}

/*
 * Setup segment summary
 */
void nilfs_segbuf_fill_in_segsum(struct nilfs_segment_buffer *segbuf)
{
	struct nilfs_segment_summary *raw_sum;
	struct buffer_head *bh_sum;

	bh_sum = list_entry(segbuf->sb_segsum_buffers.next,
			    struct buffer_head, b_assoc_buffers);
	raw_sum = (struct nilfs_segment_summary *)bh_sum->b_data;

	raw_sum->ss_magic    = cpu_to_le32(NILFS_SEGSUM_MAGIC);
	raw_sum->ss_bytes    = cpu_to_le16(sizeof(*raw_sum));
	raw_sum->ss_flags    = cpu_to_le16(segbuf->sb_sum.flags);
	raw_sum->ss_seq      = cpu_to_le64(segbuf->sb_sum.seg_seq);
	raw_sum->ss_create   = cpu_to_le64(segbuf->sb_sum.ctime);
	raw_sum->ss_next     = cpu_to_le64(segbuf->sb_sum.next);
	raw_sum->ss_nblocks  = cpu_to_le32(segbuf->sb_sum.nblocks);
	raw_sum->ss_nfinfo   = cpu_to_le32(segbuf->sb_sum.nfinfo);
	raw_sum->ss_sumbytes = cpu_to_le32(segbuf->sb_sum.sumbytes);
	raw_sum->ss_pad      = 0;
	raw_sum->ss_cno      = cpu_to_le64(segbuf->sb_sum.cno);
}

/*
 * CRC calculation routines
 */
static void
nilfs_segbuf_fill_in_segsum_crc(struct nilfs_segment_buffer *segbuf, u32 seed)
{
	struct buffer_head *bh;
	struct nilfs_segment_summary *raw_sum;
	unsigned long size, bytes = segbuf->sb_sum.sumbytes;
	u32 crc;

	bh = list_entry(segbuf->sb_segsum_buffers.next, struct buffer_head,
			b_assoc_buffers);

	raw_sum = (struct nilfs_segment_summary *)bh->b_data;
	size = min_t(unsigned long, bytes, bh->b_size);
	crc = crc32_le(seed,
		       (unsigned char *)raw_sum +
		       sizeof(raw_sum->ss_datasum) + sizeof(raw_sum->ss_sumsum),
		       size - (sizeof(raw_sum->ss_datasum) +
			       sizeof(raw_sum->ss_sumsum)));

	list_for_each_entry_continue(bh, &segbuf->sb_segsum_buffers,
				     b_assoc_buffers) {
		bytes -= size;
		size = min_t(unsigned long, bytes, bh->b_size);
		crc = crc32_le(crc, bh->b_data, size);
	}
	raw_sum->ss_sumsum = cpu_to_le32(crc);
}

static void nilfs_segbuf_fill_in_data_crc(struct nilfs_segment_buffer *segbuf,
					  u32 seed)
{
	struct buffer_head *bh;
	struct nilfs_segment_summary *raw_sum;
	void *kaddr;
	u32 crc;

	bh = list_entry(segbuf->sb_segsum_buffers.next, struct buffer_head,
			b_assoc_buffers);
	raw_sum = (struct nilfs_segment_summary *)bh->b_data;
	crc = crc32_le(seed,
		       (unsigned char *)raw_sum + sizeof(raw_sum->ss_datasum),
		       bh->b_size - sizeof(raw_sum->ss_datasum));

	list_for_each_entry_continue(bh, &segbuf->sb_segsum_buffers,
				     b_assoc_buffers) {
		crc = crc32_le(crc, bh->b_data, bh->b_size);
	}
	list_for_each_entry(bh, &segbuf->sb_payload_buffers, b_assoc_buffers) {
		kaddr = kmap_atomic(bh->b_page, KM_USER0);
		crc = crc32_le(crc, kaddr + bh_offset(bh), bh->b_size);
		kunmap_atomic(kaddr, KM_USER0);
	}
	raw_sum->ss_datasum = cpu_to_le32(crc);
}

static void
nilfs_segbuf_fill_in_super_root_crc(struct nilfs_segment_buffer *segbuf,
				    u32 seed)
{
	struct nilfs_super_root *raw_sr;
	u32 crc;

	raw_sr = (struct nilfs_super_root *)segbuf->sb_super_root->b_data;
	crc = crc32_le(seed,
		       (unsigned char *)raw_sr + sizeof(raw_sr->sr_sum),
		       NILFS_SR_BYTES - sizeof(raw_sr->sr_sum));
	raw_sr->sr_sum = cpu_to_le32(crc);
}

static void nilfs_release_buffers(struct list_head *list)
{
	struct buffer_head *bh, *n;

	list_for_each_entry_safe(bh, n, list, b_assoc_buffers) {
		list_del_init(&bh->b_assoc_buffers);
		if (buffer_nilfs_allocated(bh)) {
			struct page *clone_page = bh->b_page;

			/* remove clone page */
			brelse(bh);
			page_cache_release(clone_page); /* for each bh */
			if (page_count(clone_page) <= 2) {
				lock_page(clone_page);
				nilfs_free_private_page(clone_page);
			}
			continue;
		}
		brelse(bh);
	}
}

static void nilfs_segbuf_clear(struct nilfs_segment_buffer *segbuf)
{
	nilfs_release_buffers(&segbuf->sb_segsum_buffers);
	nilfs_release_buffers(&segbuf->sb_payload_buffers);
	segbuf->sb_super_root = NULL;
}

/*
 * Iterators for segment buffers
 */
void nilfs_clear_logs(struct list_head *logs)
{
	struct nilfs_segment_buffer *segbuf;

	list_for_each_entry(segbuf, logs, sb_list)
		nilfs_segbuf_clear(segbuf);
}

void nilfs_truncate_logs(struct list_head *logs,
			 struct nilfs_segment_buffer *last)
{
	struct nilfs_segment_buffer *n, *segbuf;

	segbuf = list_prepare_entry(last, logs, sb_list);
	list_for_each_entry_safe_continue(segbuf, n, logs, sb_list) {
		list_del_init(&segbuf->sb_list);
		nilfs_segbuf_clear(segbuf);
		nilfs_segbuf_free(segbuf);
	}
}

int nilfs_write_logs(struct list_head *logs, struct the_nilfs *nilfs)
{
	struct nilfs_segment_buffer *segbuf;
	int ret = 0;

	list_for_each_entry(segbuf, logs, sb_list) {
		ret = nilfs_segbuf_write(segbuf, nilfs);
		if (ret)
			break;
	}
	return ret;
}

int nilfs_wait_on_logs(struct list_head *logs)
{
	struct nilfs_segment_buffer *segbuf;
	int err, ret = 0;

	list_for_each_entry(segbuf, logs, sb_list) {
		err = nilfs_segbuf_wait(segbuf);
		if (err && !ret)
			ret = err;
	}
	return ret;
}

/**
 * nilfs_add_checksums_on_logs - add checksums on the logs
 * @logs: list of segment buffers storing target logs
 * @seed: checksum seed value
 */
void nilfs_add_checksums_on_logs(struct list_head *logs, u32 seed)
{
	struct nilfs_segment_buffer *segbuf;

	list_for_each_entry(segbuf, logs, sb_list) {
		if (segbuf->sb_super_root)
			nilfs_segbuf_fill_in_super_root_crc(segbuf, seed);
		nilfs_segbuf_fill_in_segsum_crc(segbuf, seed);
		nilfs_segbuf_fill_in_data_crc(segbuf, seed);
	}
}

/*
 * BIO operations
 */
static void nilfs_end_bio_write(struct bio *bio, int err)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct nilfs_segment_buffer *segbuf = bio->bi_private;

	if (err == -EOPNOTSUPP) {
		set_bit(BIO_EOPNOTSUPP, &bio->bi_flags);
		bio_put(bio);
		/* to be detected by submit_seg_bio() */
	}

	if (!uptodate)
		atomic_inc(&segbuf->sb_err);

	bio_put(bio);
	complete(&segbuf->sb_bio_event);
}

static int nilfs_segbuf_submit_bio(struct nilfs_segment_buffer *segbuf,
				   struct nilfs_write_info *wi, int mode)
{
	struct bio *bio = wi->bio;
	int err;

	if (segbuf->sb_nbio > 0 && bdi_write_congested(wi->nilfs->ns_bdi)) {
		wait_for_completion(&segbuf->sb_bio_event);
		segbuf->sb_nbio--;
		if (unlikely(atomic_read(&segbuf->sb_err))) {
			bio_put(bio);
			err = -EIO;
			goto failed;
		}
	}

	bio->bi_end_io = nilfs_end_bio_write;
	bio->bi_private = segbuf;
	bio_get(bio);
	submit_bio(mode, bio);
	if (bio_flagged(bio, BIO_EOPNOTSUPP)) {
		bio_put(bio);
		err = -EOPNOTSUPP;
		goto failed;
	}
	segbuf->sb_nbio++;
	bio_put(bio);

	wi->bio = NULL;
	wi->rest_blocks -= wi->end - wi->start;
	wi->nr_vecs = min(wi->max_pages, wi->rest_blocks);
	wi->start = wi->end;
	return 0;

 failed:
	wi->bio = NULL;
	return err;
}

/**
 * nilfs_alloc_seg_bio - allocate a new bio for writing log
 * @nilfs: nilfs object
 * @start: start block number of the bio
 * @nr_vecs: request size of page vector.
 *
 * Return Value: On success, pointer to the struct bio is returned.
 * On error, NULL is returned.
 */
static struct bio *nilfs_alloc_seg_bio(struct the_nilfs *nilfs, sector_t start,
				       int nr_vecs)
{
	struct bio *bio;

	bio = bio_alloc(GFP_NOIO, nr_vecs);
	if (bio == NULL) {
		while (!bio && (nr_vecs >>= 1))
			bio = bio_alloc(GFP_NOIO, nr_vecs);
	}
	if (likely(bio)) {
		bio->bi_bdev = nilfs->ns_bdev;
		bio->bi_sector = start << (nilfs->ns_blocksize_bits - 9);
	}
	return bio;
}

static void nilfs_segbuf_prepare_write(struct nilfs_segment_buffer *segbuf,
				       struct nilfs_write_info *wi)
{
	wi->bio = NULL;
	wi->rest_blocks = segbuf->sb_sum.nblocks;
	wi->max_pages = bio_get_nr_vecs(wi->nilfs->ns_bdev);
	wi->nr_vecs = min(wi->max_pages, wi->rest_blocks);
	wi->start = wi->end = 0;
	wi->blocknr = segbuf->sb_pseg_start;
}

static int nilfs_segbuf_submit_bh(struct nilfs_segment_buffer *segbuf,
				  struct nilfs_write_info *wi,
				  struct buffer_head *bh, int mode)
{
	int len, err;

	BUG_ON(wi->nr_vecs <= 0);
 repeat:
	if (!wi->bio) {
		wi->bio = nilfs_alloc_seg_bio(wi->nilfs, wi->blocknr + wi->end,
					      wi->nr_vecs);
		if (unlikely(!wi->bio))
			return -ENOMEM;
	}

	len = bio_add_page(wi->bio, bh->b_page, bh->b_size, bh_offset(bh));
	if (len == bh->b_size) {
		wi->end++;
		return 0;
	}
	/* bio is FULL */
	err = nilfs_segbuf_submit_bio(segbuf, wi, mode);
	/* never submit current bh */
	if (likely(!err))
		goto repeat;
	return err;
}

/**
 * nilfs_segbuf_write - submit write requests of a log
 * @segbuf: buffer storing a log to be written
 * @nilfs: nilfs object
 *
 * Return Value: On Success, 0 is returned. On Error, one of the following
 * negative error code is returned.
 *
 * %-EIO - I/O error
 *
 * %-ENOMEM - Insufficient memory available.
 */
static int nilfs_segbuf_write(struct nilfs_segment_buffer *segbuf,
			      struct the_nilfs *nilfs)
{
	struct nilfs_write_info wi;
	struct buffer_head *bh;
	int res = 0, rw = WRITE;

	wi.nilfs = nilfs;
	nilfs_segbuf_prepare_write(segbuf, &wi);

	list_for_each_entry(bh, &segbuf->sb_segsum_buffers, b_assoc_buffers) {
		res = nilfs_segbuf_submit_bh(segbuf, &wi, bh, rw);
		if (unlikely(res))
			goto failed_bio;
	}

	list_for_each_entry(bh, &segbuf->sb_payload_buffers, b_assoc_buffers) {
		res = nilfs_segbuf_submit_bh(segbuf, &wi, bh, rw);
		if (unlikely(res))
			goto failed_bio;
	}

	if (wi.bio) {
		/*
		 * Last BIO is always sent through the following
		 * submission.
		 */
		rw |= (1 << BIO_RW_SYNCIO) | (1 << BIO_RW_UNPLUG);
		res = nilfs_segbuf_submit_bio(segbuf, &wi, rw);
	}

 failed_bio:
	return res;
}

/**
 * nilfs_segbuf_wait - wait for completion of requested BIOs
 * @segbuf: segment buffer
 *
 * Return Value: On Success, 0 is returned. On Error, one of the following
 * negative error code is returned.
 *
 * %-EIO - I/O error
 */
static int nilfs_segbuf_wait(struct nilfs_segment_buffer *segbuf)
{
	int err = 0;

	if (!segbuf->sb_nbio)
		return 0;

	do {
		wait_for_completion(&segbuf->sb_bio_event);
	} while (--segbuf->sb_nbio > 0);

	if (unlikely(atomic_read(&segbuf->sb_err) > 0)) {
		printk(KERN_ERR "NILFS: IO error writing segment\n");
		err = -EIO;
	}
	return err;
}
