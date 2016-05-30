/*
 * recovery.c - NILFS recovery logic
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
 * Written by Ryusuke Konishi.
 */

#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include "nilfs.h"
#include "segment.h"
#include "sufile.h"
#include "page.h"
#include "segbuf.h"

/*
 * Segment check result
 */
enum {
	NILFS_SEG_VALID,
	NILFS_SEG_NO_SUPER_ROOT,
	NILFS_SEG_FAIL_IO,
	NILFS_SEG_FAIL_MAGIC,
	NILFS_SEG_FAIL_SEQ,
	NILFS_SEG_FAIL_CHECKSUM_SUPER_ROOT,
	NILFS_SEG_FAIL_CHECKSUM_FULL,
	NILFS_SEG_FAIL_CONSISTENCY,
};

/* work structure for recovery */
struct nilfs_recovery_block {
	ino_t ino;		/*
				 * Inode number of the file that this block
				 * belongs to
				 */
	sector_t blocknr;	/* block number */
	__u64 vblocknr;		/* virtual block number */
	unsigned long blkoff;	/* File offset of the data block (per block) */
	struct list_head list;
};


static int nilfs_warn_segment_error(int err)
{
	switch (err) {
	case NILFS_SEG_FAIL_IO:
		printk(KERN_WARNING
		       "NILFS warning: I/O error on loading last segment\n");
		return -EIO;
	case NILFS_SEG_FAIL_MAGIC:
		printk(KERN_WARNING
		       "NILFS warning: Segment magic number invalid\n");
		break;
	case NILFS_SEG_FAIL_SEQ:
		printk(KERN_WARNING
		       "NILFS warning: Sequence number mismatch\n");
		break;
	case NILFS_SEG_FAIL_CHECKSUM_SUPER_ROOT:
		printk(KERN_WARNING
		       "NILFS warning: Checksum error in super root\n");
		break;
	case NILFS_SEG_FAIL_CHECKSUM_FULL:
		printk(KERN_WARNING
		       "NILFS warning: Checksum error in segment payload\n");
		break;
	case NILFS_SEG_FAIL_CONSISTENCY:
		printk(KERN_WARNING
		       "NILFS warning: Inconsistent segment\n");
		break;
	case NILFS_SEG_NO_SUPER_ROOT:
		printk(KERN_WARNING
		       "NILFS warning: No super root in the last segment\n");
		break;
	}
	return -EINVAL;
}

/**
 * nilfs_compute_checksum - compute checksum of blocks continuously
 * @nilfs: nilfs object
 * @bhs: buffer head of start block
 * @sum: place to store result
 * @offset: offset bytes in the first block
 * @check_bytes: number of bytes to be checked
 * @start: DBN of start block
 * @nblock: number of blocks to be checked
 */
static int nilfs_compute_checksum(struct the_nilfs *nilfs,
				  struct buffer_head *bhs, u32 *sum,
				  unsigned long offset, u64 check_bytes,
				  sector_t start, unsigned long nblock)
{
	unsigned int blocksize = nilfs->ns_blocksize;
	unsigned long size;
	u32 crc;

	BUG_ON(offset >= blocksize);
	check_bytes -= offset;
	size = min_t(u64, check_bytes, blocksize - offset);
	crc = crc32_le(nilfs->ns_crc_seed,
		       (unsigned char *)bhs->b_data + offset, size);
	if (--nblock > 0) {
		do {
			struct buffer_head *bh;

			bh = __bread(nilfs->ns_bdev, ++start, blocksize);
			if (!bh)
				return -EIO;
			check_bytes -= size;
			size = min_t(u64, check_bytes, blocksize);
			crc = crc32_le(crc, bh->b_data, size);
			brelse(bh);
		} while (--nblock > 0);
	}
	*sum = crc;
	return 0;
}

/**
 * nilfs_read_super_root_block - read super root block
 * @nilfs: nilfs object
 * @sr_block: disk block number of the super root block
 * @pbh: address of a buffer_head pointer to return super root buffer
 * @check: CRC check flag
 */
int nilfs_read_super_root_block(struct the_nilfs *nilfs, sector_t sr_block,
				struct buffer_head **pbh, int check)
{
	struct buffer_head *bh_sr;
	struct nilfs_super_root *sr;
	u32 crc;
	int ret;

	*pbh = NULL;
	bh_sr = __bread(nilfs->ns_bdev, sr_block, nilfs->ns_blocksize);
	if (unlikely(!bh_sr)) {
		ret = NILFS_SEG_FAIL_IO;
		goto failed;
	}

	sr = (struct nilfs_super_root *)bh_sr->b_data;
	if (check) {
		unsigned int bytes = le16_to_cpu(sr->sr_bytes);

		if (bytes == 0 || bytes > nilfs->ns_blocksize) {
			ret = NILFS_SEG_FAIL_CHECKSUM_SUPER_ROOT;
			goto failed_bh;
		}
		if (nilfs_compute_checksum(
			    nilfs, bh_sr, &crc, sizeof(sr->sr_sum), bytes,
			    sr_block, 1)) {
			ret = NILFS_SEG_FAIL_IO;
			goto failed_bh;
		}
		if (crc != le32_to_cpu(sr->sr_sum)) {
			ret = NILFS_SEG_FAIL_CHECKSUM_SUPER_ROOT;
			goto failed_bh;
		}
	}
	*pbh = bh_sr;
	return 0;

 failed_bh:
	brelse(bh_sr);

 failed:
	return nilfs_warn_segment_error(ret);
}

/**
 * nilfs_read_log_header - read summary header of the specified log
 * @nilfs: nilfs object
 * @start_blocknr: start block number of the log
 * @sum: pointer to return segment summary structure
 */
static struct buffer_head *
nilfs_read_log_header(struct the_nilfs *nilfs, sector_t start_blocknr,
		      struct nilfs_segment_summary **sum)
{
	struct buffer_head *bh_sum;

	bh_sum = __bread(nilfs->ns_bdev, start_blocknr, nilfs->ns_blocksize);
	if (bh_sum)
		*sum = (struct nilfs_segment_summary *)bh_sum->b_data;
	return bh_sum;
}

/**
 * nilfs_validate_log - verify consistency of log
 * @nilfs: nilfs object
 * @seg_seq: sequence number of segment
 * @bh_sum: buffer head of summary block
 * @sum: segment summary struct
 */
static int nilfs_validate_log(struct the_nilfs *nilfs, u64 seg_seq,
			      struct buffer_head *bh_sum,
			      struct nilfs_segment_summary *sum)
{
	unsigned long nblock;
	u32 crc;
	int ret;

	ret = NILFS_SEG_FAIL_MAGIC;
	if (le32_to_cpu(sum->ss_magic) != NILFS_SEGSUM_MAGIC)
		goto out;

	ret = NILFS_SEG_FAIL_SEQ;
	if (le64_to_cpu(sum->ss_seq) != seg_seq)
		goto out;

	nblock = le32_to_cpu(sum->ss_nblocks);
	ret = NILFS_SEG_FAIL_CONSISTENCY;
	if (unlikely(nblock == 0 || nblock > nilfs->ns_blocks_per_segment))
		/* This limits the number of blocks read in the CRC check */
		goto out;

	ret = NILFS_SEG_FAIL_IO;
	if (nilfs_compute_checksum(nilfs, bh_sum, &crc, sizeof(sum->ss_datasum),
				   ((u64)nblock << nilfs->ns_blocksize_bits),
				   bh_sum->b_blocknr, nblock))
		goto out;

	ret = NILFS_SEG_FAIL_CHECKSUM_FULL;
	if (crc != le32_to_cpu(sum->ss_datasum))
		goto out;
	ret = 0;
out:
	return ret;
}

/**
 * nilfs_read_summary_info - read an item on summary blocks of a log
 * @nilfs: nilfs object
 * @pbh: the current buffer head on summary blocks [in, out]
 * @offset: the current byte offset on summary blocks [in, out]
 * @bytes: byte size of the item to be read
 */
static void *nilfs_read_summary_info(struct the_nilfs *nilfs,
				     struct buffer_head **pbh,
				     unsigned int *offset, unsigned int bytes)
{
	void *ptr;
	sector_t blocknr;

	BUG_ON((*pbh)->b_size < *offset);
	if (bytes > (*pbh)->b_size - *offset) {
		blocknr = (*pbh)->b_blocknr;
		brelse(*pbh);
		*pbh = __bread(nilfs->ns_bdev, blocknr + 1,
			       nilfs->ns_blocksize);
		if (unlikely(!*pbh))
			return NULL;
		*offset = 0;
	}
	ptr = (*pbh)->b_data + *offset;
	*offset += bytes;
	return ptr;
}

/**
 * nilfs_skip_summary_info - skip items on summary blocks of a log
 * @nilfs: nilfs object
 * @pbh: the current buffer head on summary blocks [in, out]
 * @offset: the current byte offset on summary blocks [in, out]
 * @bytes: byte size of the item to be skipped
 * @count: number of items to be skipped
 */
static void nilfs_skip_summary_info(struct the_nilfs *nilfs,
				    struct buffer_head **pbh,
				    unsigned int *offset, unsigned int bytes,
				    unsigned long count)
{
	unsigned int rest_item_in_current_block
		= ((*pbh)->b_size - *offset) / bytes;

	if (count <= rest_item_in_current_block) {
		*offset += bytes * count;
	} else {
		sector_t blocknr = (*pbh)->b_blocknr;
		unsigned int nitem_per_block = (*pbh)->b_size / bytes;
		unsigned int bcnt;

		count -= rest_item_in_current_block;
		bcnt = DIV_ROUND_UP(count, nitem_per_block);
		*offset = bytes * (count - (bcnt - 1) * nitem_per_block);

		brelse(*pbh);
		*pbh = __bread(nilfs->ns_bdev, blocknr + bcnt,
			       nilfs->ns_blocksize);
	}
}

/**
 * nilfs_scan_dsync_log - get block information of a log written for data sync
 * @nilfs: nilfs object
 * @start_blocknr: start block number of the log
 * @sum: log summary information
 * @head: list head to add nilfs_recovery_block struct
 */
static int nilfs_scan_dsync_log(struct the_nilfs *nilfs, sector_t start_blocknr,
				struct nilfs_segment_summary *sum,
				struct list_head *head)
{
	struct buffer_head *bh;
	unsigned int offset;
	u32 nfinfo, sumbytes;
	sector_t blocknr;
	ino_t ino;
	int err = -EIO;

	nfinfo = le32_to_cpu(sum->ss_nfinfo);
	if (!nfinfo)
		return 0;

	sumbytes = le32_to_cpu(sum->ss_sumbytes);
	blocknr = start_blocknr + DIV_ROUND_UP(sumbytes, nilfs->ns_blocksize);
	bh = __bread(nilfs->ns_bdev, start_blocknr, nilfs->ns_blocksize);
	if (unlikely(!bh))
		goto out;

	offset = le16_to_cpu(sum->ss_bytes);
	for (;;) {
		unsigned long nblocks, ndatablk, nnodeblk;
		struct nilfs_finfo *finfo;

		finfo = nilfs_read_summary_info(nilfs, &bh, &offset,
						sizeof(*finfo));
		if (unlikely(!finfo))
			goto out;

		ino = le64_to_cpu(finfo->fi_ino);
		nblocks = le32_to_cpu(finfo->fi_nblocks);
		ndatablk = le32_to_cpu(finfo->fi_ndatablk);
		nnodeblk = nblocks - ndatablk;

		while (ndatablk-- > 0) {
			struct nilfs_recovery_block *rb;
			struct nilfs_binfo_v *binfo;

			binfo = nilfs_read_summary_info(nilfs, &bh, &offset,
							sizeof(*binfo));
			if (unlikely(!binfo))
				goto out;

			rb = kmalloc(sizeof(*rb), GFP_NOFS);
			if (unlikely(!rb)) {
				err = -ENOMEM;
				goto out;
			}
			rb->ino = ino;
			rb->blocknr = blocknr++;
			rb->vblocknr = le64_to_cpu(binfo->bi_vblocknr);
			rb->blkoff = le64_to_cpu(binfo->bi_blkoff);
			/* INIT_LIST_HEAD(&rb->list); */
			list_add_tail(&rb->list, head);
		}
		if (--nfinfo == 0)
			break;
		blocknr += nnodeblk; /* always 0 for data sync logs */
		nilfs_skip_summary_info(nilfs, &bh, &offset, sizeof(__le64),
					nnodeblk);
		if (unlikely(!bh))
			goto out;
	}
	err = 0;
 out:
	brelse(bh);   /* brelse(NULL) is just ignored */
	return err;
}

static void dispose_recovery_list(struct list_head *head)
{
	while (!list_empty(head)) {
		struct nilfs_recovery_block *rb;

		rb = list_first_entry(head, struct nilfs_recovery_block, list);
		list_del(&rb->list);
		kfree(rb);
	}
}

struct nilfs_segment_entry {
	struct list_head	list;
	__u64			segnum;
};

static int nilfs_segment_list_add(struct list_head *head, __u64 segnum)
{
	struct nilfs_segment_entry *ent = kmalloc(sizeof(*ent), GFP_NOFS);

	if (unlikely(!ent))
		return -ENOMEM;

	ent->segnum = segnum;
	INIT_LIST_HEAD(&ent->list);
	list_add_tail(&ent->list, head);
	return 0;
}

void nilfs_dispose_segment_list(struct list_head *head)
{
	while (!list_empty(head)) {
		struct nilfs_segment_entry *ent;

		ent = list_first_entry(head, struct nilfs_segment_entry, list);
		list_del(&ent->list);
		kfree(ent);
	}
}

static int nilfs_prepare_segment_for_recovery(struct the_nilfs *nilfs,
					      struct super_block *sb,
					      struct nilfs_recovery_info *ri)
{
	struct list_head *head = &ri->ri_used_segments;
	struct nilfs_segment_entry *ent, *n;
	struct inode *sufile = nilfs->ns_sufile;
	__u64 segnum[4];
	int err;
	int i;

	segnum[0] = nilfs->ns_segnum;
	segnum[1] = nilfs->ns_nextnum;
	segnum[2] = ri->ri_segnum;
	segnum[3] = ri->ri_nextnum;

	/*
	 * Releasing the next segment of the latest super root.
	 * The next segment is invalidated by this recovery.
	 */
	err = nilfs_sufile_free(sufile, segnum[1]);
	if (unlikely(err))
		goto failed;

	for (i = 1; i < 4; i++) {
		err = nilfs_segment_list_add(head, segnum[i]);
		if (unlikely(err))
			goto failed;
	}

	/*
	 * Collecting segments written after the latest super root.
	 * These are marked dirty to avoid being reallocated in the next write.
	 */
	list_for_each_entry_safe(ent, n, head, list) {
		if (ent->segnum != segnum[0]) {
			err = nilfs_sufile_scrap(sufile, ent->segnum);
			if (unlikely(err))
				goto failed;
		}
		list_del(&ent->list);
		kfree(ent);
	}

	/* Allocate new segments for recovery */
	err = nilfs_sufile_alloc(sufile, &segnum[0]);
	if (unlikely(err))
		goto failed;

	nilfs->ns_pseg_offset = 0;
	nilfs->ns_seg_seq = ri->ri_seq + 2;
	nilfs->ns_nextnum = nilfs->ns_segnum = segnum[0];

 failed:
	/* No need to recover sufile because it will be destroyed on error */
	return err;
}

static int nilfs_recovery_copy_block(struct the_nilfs *nilfs,
				     struct nilfs_recovery_block *rb,
				     struct page *page)
{
	struct buffer_head *bh_org;
	void *kaddr;

	bh_org = __bread(nilfs->ns_bdev, rb->blocknr, nilfs->ns_blocksize);
	if (unlikely(!bh_org))
		return -EIO;

	kaddr = kmap_atomic(page);
	memcpy(kaddr + bh_offset(bh_org), bh_org->b_data, bh_org->b_size);
	kunmap_atomic(kaddr);
	brelse(bh_org);
	return 0;
}

static int nilfs_recover_dsync_blocks(struct the_nilfs *nilfs,
				      struct super_block *sb,
				      struct nilfs_root *root,
				      struct list_head *head,
				      unsigned long *nr_salvaged_blocks)
{
	struct inode *inode;
	struct nilfs_recovery_block *rb, *n;
	unsigned int blocksize = nilfs->ns_blocksize;
	struct page *page;
	loff_t pos;
	int err = 0, err2 = 0;

	list_for_each_entry_safe(rb, n, head, list) {
		inode = nilfs_iget(sb, root, rb->ino);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			inode = NULL;
			goto failed_inode;
		}

		pos = rb->blkoff << inode->i_blkbits;
		err = block_write_begin(inode->i_mapping, pos, blocksize,
					0, &page, nilfs_get_block);
		if (unlikely(err)) {
			loff_t isize = inode->i_size;

			if (pos + blocksize > isize)
				nilfs_write_failed(inode->i_mapping,
							pos + blocksize);
			goto failed_inode;
		}

		err = nilfs_recovery_copy_block(nilfs, rb, page);
		if (unlikely(err))
			goto failed_page;

		err = nilfs_set_file_dirty(inode, 1);
		if (unlikely(err))
			goto failed_page;

		block_write_end(NULL, inode->i_mapping, pos, blocksize,
				blocksize, page, NULL);

		unlock_page(page);
		put_page(page);

		(*nr_salvaged_blocks)++;
		goto next;

 failed_page:
		unlock_page(page);
		put_page(page);

 failed_inode:
		printk(KERN_WARNING
		       "NILFS warning: error recovering data block "
		       "(err=%d, ino=%lu, block-offset=%llu)\n",
		       err, (unsigned long)rb->ino,
		       (unsigned long long)rb->blkoff);
		if (!err2)
			err2 = err;
 next:
		iput(inode); /* iput(NULL) is just ignored */
		list_del_init(&rb->list);
		kfree(rb);
	}
	return err2;
}

/**
 * nilfs_do_roll_forward - salvage logical segments newer than the latest
 * checkpoint
 * @nilfs: nilfs object
 * @sb: super block instance
 * @ri: pointer to a nilfs_recovery_info
 */
static int nilfs_do_roll_forward(struct the_nilfs *nilfs,
				 struct super_block *sb,
				 struct nilfs_root *root,
				 struct nilfs_recovery_info *ri)
{
	struct buffer_head *bh_sum = NULL;
	struct nilfs_segment_summary *sum = NULL;
	sector_t pseg_start;
	sector_t seg_start, seg_end;  /* Starting/ending DBN of full segment */
	unsigned long nsalvaged_blocks = 0;
	unsigned int flags;
	u64 seg_seq;
	__u64 segnum, nextnum = 0;
	int empty_seg = 0;
	int err = 0, ret;
	LIST_HEAD(dsync_blocks);  /* list of data blocks to be recovered */
	enum {
		RF_INIT_ST,
		RF_DSYNC_ST,   /* scanning data-sync segments */
	};
	int state = RF_INIT_ST;

	pseg_start = ri->ri_lsegs_start;
	seg_seq = ri->ri_lsegs_start_seq;
	segnum = nilfs_get_segnum_of_block(nilfs, pseg_start);
	nilfs_get_segment_range(nilfs, segnum, &seg_start, &seg_end);

	while (segnum != ri->ri_segnum || pseg_start <= ri->ri_pseg_start) {
		brelse(bh_sum);
		bh_sum = nilfs_read_log_header(nilfs, pseg_start, &sum);
		if (!bh_sum) {
			err = -EIO;
			goto failed;
		}

		ret = nilfs_validate_log(nilfs, seg_seq, bh_sum, sum);
		if (ret) {
			if (ret == NILFS_SEG_FAIL_IO) {
				err = -EIO;
				goto failed;
			}
			goto strayed;
		}

		flags = le16_to_cpu(sum->ss_flags);
		if (flags & NILFS_SS_SR)
			goto confused;

		/* Found a valid partial segment; do recovery actions */
		nextnum = nilfs_get_segnum_of_block(nilfs,
						    le64_to_cpu(sum->ss_next));
		empty_seg = 0;
		nilfs->ns_ctime = le64_to_cpu(sum->ss_create);
		if (!(flags & NILFS_SS_GC))
			nilfs->ns_nongc_ctime = nilfs->ns_ctime;

		switch (state) {
		case RF_INIT_ST:
			if (!(flags & NILFS_SS_LOGBGN) ||
			    !(flags & NILFS_SS_SYNDT))
				goto try_next_pseg;
			state = RF_DSYNC_ST;
			/* Fall through */
		case RF_DSYNC_ST:
			if (!(flags & NILFS_SS_SYNDT))
				goto confused;

			err = nilfs_scan_dsync_log(nilfs, pseg_start, sum,
						   &dsync_blocks);
			if (unlikely(err))
				goto failed;
			if (flags & NILFS_SS_LOGEND) {
				err = nilfs_recover_dsync_blocks(
					nilfs, sb, root, &dsync_blocks,
					&nsalvaged_blocks);
				if (unlikely(err))
					goto failed;
				state = RF_INIT_ST;
			}
			break; /* Fall through to try_next_pseg */
		}

 try_next_pseg:
		if (pseg_start == ri->ri_lsegs_end)
			break;
		pseg_start += le32_to_cpu(sum->ss_nblocks);
		if (pseg_start < seg_end)
			continue;
		goto feed_segment;

 strayed:
		if (pseg_start == ri->ri_lsegs_end)
			break;

 feed_segment:
		/* Looking to the next full segment */
		if (empty_seg++)
			break;
		seg_seq++;
		segnum = nextnum;
		nilfs_get_segment_range(nilfs, segnum, &seg_start, &seg_end);
		pseg_start = seg_start;
	}

	if (nsalvaged_blocks) {
		printk(KERN_INFO "NILFS (device %s): salvaged %lu blocks\n",
		       sb->s_id, nsalvaged_blocks);
		ri->ri_need_recovery = NILFS_RECOVERY_ROLLFORWARD_DONE;
	}
 out:
	brelse(bh_sum);
	dispose_recovery_list(&dsync_blocks);
	return err;

 confused:
	err = -EINVAL;
 failed:
	printk(KERN_ERR
	       "NILFS (device %s): Error roll-forwarding "
	       "(err=%d, pseg block=%llu). ",
	       sb->s_id, err, (unsigned long long)pseg_start);
	goto out;
}

static void nilfs_finish_roll_forward(struct the_nilfs *nilfs,
				      struct nilfs_recovery_info *ri)
{
	struct buffer_head *bh;
	int err;

	if (nilfs_get_segnum_of_block(nilfs, ri->ri_lsegs_start) !=
	    nilfs_get_segnum_of_block(nilfs, ri->ri_super_root))
		return;

	bh = __getblk(nilfs->ns_bdev, ri->ri_lsegs_start, nilfs->ns_blocksize);
	BUG_ON(!bh);
	memset(bh->b_data, 0, bh->b_size);
	set_buffer_dirty(bh);
	err = sync_dirty_buffer(bh);
	if (unlikely(err))
		printk(KERN_WARNING
		       "NILFS warning: buffer sync write failed during "
		       "post-cleaning of recovery.\n");
	brelse(bh);
}

/**
 * nilfs_salvage_orphan_logs - salvage logs written after the latest checkpoint
 * @nilfs: nilfs object
 * @sb: super block instance
 * @ri: pointer to a nilfs_recovery_info struct to store search results.
 *
 * Return Value: On success, 0 is returned.  On error, one of the following
 * negative error code is returned.
 *
 * %-EINVAL - Inconsistent filesystem state.
 *
 * %-EIO - I/O error
 *
 * %-ENOSPC - No space left on device (only in a panic state).
 *
 * %-ERESTARTSYS - Interrupted.
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_salvage_orphan_logs(struct the_nilfs *nilfs,
			      struct super_block *sb,
			      struct nilfs_recovery_info *ri)
{
	struct nilfs_root *root;
	int err;

	if (ri->ri_lsegs_start == 0 || ri->ri_lsegs_end == 0)
		return 0;

	err = nilfs_attach_checkpoint(sb, ri->ri_cno, true, &root);
	if (unlikely(err)) {
		printk(KERN_ERR
		       "NILFS: error loading the latest checkpoint.\n");
		return err;
	}

	err = nilfs_do_roll_forward(nilfs, sb, root, ri);
	if (unlikely(err))
		goto failed;

	if (ri->ri_need_recovery == NILFS_RECOVERY_ROLLFORWARD_DONE) {
		err = nilfs_prepare_segment_for_recovery(nilfs, sb, ri);
		if (unlikely(err)) {
			printk(KERN_ERR "NILFS: Error preparing segments for "
			       "recovery.\n");
			goto failed;
		}

		err = nilfs_attach_log_writer(sb, root);
		if (unlikely(err))
			goto failed;

		set_nilfs_discontinued(nilfs);
		err = nilfs_construct_segment(sb);
		nilfs_detach_log_writer(sb);

		if (unlikely(err)) {
			printk(KERN_ERR "NILFS: Oops! recovery failed. "
			       "(err=%d)\n", err);
			goto failed;
		}

		nilfs_finish_roll_forward(nilfs, ri);
	}

 failed:
	nilfs_put_root(root);
	return err;
}

/**
 * nilfs_search_super_root - search the latest valid super root
 * @nilfs: the_nilfs
 * @ri: pointer to a nilfs_recovery_info struct to store search results.
 *
 * nilfs_search_super_root() looks for the latest super-root from a partial
 * segment pointed by the superblock.  It sets up struct the_nilfs through
 * this search. It fills nilfs_recovery_info (ri) required for recovery.
 *
 * Return Value: On success, 0 is returned.  On error, one of the following
 * negative error code is returned.
 *
 * %-EINVAL - No valid segment found
 *
 * %-EIO - I/O error
 *
 * %-ENOMEM - Insufficient memory available.
 */
int nilfs_search_super_root(struct the_nilfs *nilfs,
			    struct nilfs_recovery_info *ri)
{
	struct buffer_head *bh_sum = NULL;
	struct nilfs_segment_summary *sum = NULL;
	sector_t pseg_start, pseg_end, sr_pseg_start = 0;
	sector_t seg_start, seg_end; /* range of full segment (block number) */
	sector_t b, end;
	unsigned long nblocks;
	unsigned int flags;
	u64 seg_seq;
	__u64 segnum, nextnum = 0;
	__u64 cno;
	LIST_HEAD(segments);
	int empty_seg = 0, scan_newer = 0;
	int ret;

	pseg_start = nilfs->ns_last_pseg;
	seg_seq = nilfs->ns_last_seq;
	cno = nilfs->ns_last_cno;
	segnum = nilfs_get_segnum_of_block(nilfs, pseg_start);

	/* Calculate range of segment */
	nilfs_get_segment_range(nilfs, segnum, &seg_start, &seg_end);

	/* Read ahead segment */
	b = seg_start;
	while (b <= seg_end)
		__breadahead(nilfs->ns_bdev, b++, nilfs->ns_blocksize);

	for (;;) {
		brelse(bh_sum);
		ret = NILFS_SEG_FAIL_IO;
		bh_sum = nilfs_read_log_header(nilfs, pseg_start, &sum);
		if (!bh_sum)
			goto failed;

		ret = nilfs_validate_log(nilfs, seg_seq, bh_sum, sum);
		if (ret) {
			if (ret == NILFS_SEG_FAIL_IO)
				goto failed;
			goto strayed;
		}

		nblocks = le32_to_cpu(sum->ss_nblocks);
		pseg_end = pseg_start + nblocks - 1;
		if (unlikely(pseg_end > seg_end)) {
			ret = NILFS_SEG_FAIL_CONSISTENCY;
			goto strayed;
		}

		/* A valid partial segment */
		ri->ri_pseg_start = pseg_start;
		ri->ri_seq = seg_seq;
		ri->ri_segnum = segnum;
		nextnum = nilfs_get_segnum_of_block(nilfs,
						    le64_to_cpu(sum->ss_next));
		ri->ri_nextnum = nextnum;
		empty_seg = 0;

		flags = le16_to_cpu(sum->ss_flags);
		if (!(flags & NILFS_SS_SR) && !scan_newer) {
			/*
			 * This will never happen because a superblock
			 * (last_segment) always points to a pseg with
			 * a super root.
			 */
			ret = NILFS_SEG_FAIL_CONSISTENCY;
			goto failed;
		}

		if (pseg_start == seg_start) {
			nilfs_get_segment_range(nilfs, nextnum, &b, &end);
			while (b <= end)
				__breadahead(nilfs->ns_bdev, b++,
					     nilfs->ns_blocksize);
		}
		if (!(flags & NILFS_SS_SR)) {
			if (!ri->ri_lsegs_start && (flags & NILFS_SS_LOGBGN)) {
				ri->ri_lsegs_start = pseg_start;
				ri->ri_lsegs_start_seq = seg_seq;
			}
			if (flags & NILFS_SS_LOGEND)
				ri->ri_lsegs_end = pseg_start;
			goto try_next_pseg;
		}

		/* A valid super root was found. */
		ri->ri_cno = cno++;
		ri->ri_super_root = pseg_end;
		ri->ri_lsegs_start = ri->ri_lsegs_end = 0;

		nilfs_dispose_segment_list(&segments);
		sr_pseg_start = pseg_start;
		nilfs->ns_pseg_offset = pseg_start + nblocks - seg_start;
		nilfs->ns_seg_seq = seg_seq;
		nilfs->ns_segnum = segnum;
		nilfs->ns_cno = cno;  /* nilfs->ns_cno = ri->ri_cno + 1 */
		nilfs->ns_ctime = le64_to_cpu(sum->ss_create);
		nilfs->ns_nextnum = nextnum;

		if (scan_newer)
			ri->ri_need_recovery = NILFS_RECOVERY_SR_UPDATED;
		else {
			if (nilfs->ns_mount_state & NILFS_VALID_FS)
				goto super_root_found;
			scan_newer = 1;
		}

 try_next_pseg:
		/* Standing on a course, or met an inconsistent state */
		pseg_start += nblocks;
		if (pseg_start < seg_end)
			continue;
		goto feed_segment;

 strayed:
		/* Off the trail */
		if (!scan_newer)
			/*
			 * This can happen if a checkpoint was written without
			 * barriers, or as a result of an I/O failure.
			 */
			goto failed;

 feed_segment:
		/* Looking to the next full segment */
		if (empty_seg++)
			goto super_root_found; /* found a valid super root */

		ret = nilfs_segment_list_add(&segments, segnum);
		if (unlikely(ret))
			goto failed;

		seg_seq++;
		segnum = nextnum;
		nilfs_get_segment_range(nilfs, segnum, &seg_start, &seg_end);
		pseg_start = seg_start;
	}

 super_root_found:
	/* Updating pointers relating to the latest checkpoint */
	brelse(bh_sum);
	list_splice_tail(&segments, &ri->ri_used_segments);
	nilfs->ns_last_pseg = sr_pseg_start;
	nilfs->ns_last_seq = nilfs->ns_seg_seq;
	nilfs->ns_last_cno = ri->ri_cno;
	return 0;

 failed:
	brelse(bh_sum);
	nilfs_dispose_segment_list(&segments);
	return (ret < 0) ? ret : nilfs_warn_segment_error(ret);
}
