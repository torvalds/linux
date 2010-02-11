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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 */

#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/swap.h>
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
	NILFS_SEG_FAIL_CHECKSUM_SEGSUM,
	NILFS_SEG_FAIL_CHECKSUM_SUPER_ROOT,
	NILFS_SEG_FAIL_CHECKSUM_FULL,
	NILFS_SEG_FAIL_CONSISTENCY,
};

/* work structure for recovery */
struct nilfs_recovery_block {
	ino_t ino;		/* Inode number of the file that this block
				   belongs to */
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
	case NILFS_SEG_FAIL_CHECKSUM_SEGSUM:
		printk(KERN_WARNING
		       "NILFS warning: Checksum error in segment summary\n");
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

static void store_segsum_info(struct nilfs_segsum_info *ssi,
			      struct nilfs_segment_summary *sum,
			      unsigned int blocksize)
{
	ssi->flags = le16_to_cpu(sum->ss_flags);
	ssi->seg_seq = le64_to_cpu(sum->ss_seq);
	ssi->ctime = le64_to_cpu(sum->ss_create);
	ssi->next = le64_to_cpu(sum->ss_next);
	ssi->nblocks = le32_to_cpu(sum->ss_nblocks);
	ssi->nfinfo = le32_to_cpu(sum->ss_nfinfo);
	ssi->sumbytes = le32_to_cpu(sum->ss_sumbytes);

	ssi->nsumblk = DIV_ROUND_UP(ssi->sumbytes, blocksize);
	ssi->nfileblk = ssi->nblocks - ssi->nsumblk - !!NILFS_SEG_HAS_SR(ssi);
}

/**
 * calc_crc_cont - check CRC of blocks continuously
 * @sbi: nilfs_sb_info
 * @bhs: buffer head of start block
 * @sum: place to store result
 * @offset: offset bytes in the first block
 * @check_bytes: number of bytes to be checked
 * @start: DBN of start block
 * @nblock: number of blocks to be checked
 */
static int calc_crc_cont(struct nilfs_sb_info *sbi, struct buffer_head *bhs,
			 u32 *sum, unsigned long offset, u64 check_bytes,
			 sector_t start, unsigned long nblock)
{
	unsigned long blocksize = sbi->s_super->s_blocksize;
	unsigned long size;
	u32 crc;

	BUG_ON(offset >= blocksize);
	check_bytes -= offset;
	size = min_t(u64, check_bytes, blocksize - offset);
	crc = crc32_le(sbi->s_nilfs->ns_crc_seed,
		       (unsigned char *)bhs->b_data + offset, size);
	if (--nblock > 0) {
		do {
			struct buffer_head *bh
				= sb_bread(sbi->s_super, ++start);
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
 * @sb: super_block
 * @sr_block: disk block number of the super root block
 * @pbh: address of a buffer_head pointer to return super root buffer
 * @check: CRC check flag
 */
int nilfs_read_super_root_block(struct super_block *sb, sector_t sr_block,
				struct buffer_head **pbh, int check)
{
	struct buffer_head *bh_sr;
	struct nilfs_super_root *sr;
	u32 crc;
	int ret;

	*pbh = NULL;
	bh_sr = sb_bread(sb, sr_block);
	if (unlikely(!bh_sr)) {
		ret = NILFS_SEG_FAIL_IO;
		goto failed;
	}

	sr = (struct nilfs_super_root *)bh_sr->b_data;
	if (check) {
		unsigned bytes = le16_to_cpu(sr->sr_bytes);

		if (bytes == 0 || bytes > sb->s_blocksize) {
			ret = NILFS_SEG_FAIL_CHECKSUM_SUPER_ROOT;
			goto failed_bh;
		}
		if (calc_crc_cont(NILFS_SB(sb), bh_sr, &crc,
				  sizeof(sr->sr_sum), bytes, sr_block, 1)) {
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
 * load_segment_summary - read segment summary of the specified partial segment
 * @sbi: nilfs_sb_info
 * @pseg_start: start disk block number of partial segment
 * @seg_seq: sequence number requested
 * @ssi: pointer to nilfs_segsum_info struct to store information
 * @full_check: full check flag
 *              (0: only checks segment summary CRC, 1: data CRC)
 */
static int
load_segment_summary(struct nilfs_sb_info *sbi, sector_t pseg_start,
		     u64 seg_seq, struct nilfs_segsum_info *ssi,
		     int full_check)
{
	struct buffer_head *bh_sum;
	struct nilfs_segment_summary *sum;
	unsigned long offset, nblock;
	u64 check_bytes;
	u32 crc, crc_sum;
	int ret = NILFS_SEG_FAIL_IO;

	bh_sum = sb_bread(sbi->s_super, pseg_start);
	if (!bh_sum)
		goto out;

	sum = (struct nilfs_segment_summary *)bh_sum->b_data;

	/* Check consistency of segment summary */
	if (le32_to_cpu(sum->ss_magic) != NILFS_SEGSUM_MAGIC) {
		ret = NILFS_SEG_FAIL_MAGIC;
		goto failed;
	}
	store_segsum_info(ssi, sum, sbi->s_super->s_blocksize);
	if (seg_seq != ssi->seg_seq) {
		ret = NILFS_SEG_FAIL_SEQ;
		goto failed;
	}
	if (full_check) {
		offset = sizeof(sum->ss_datasum);
		check_bytes =
			((u64)ssi->nblocks << sbi->s_super->s_blocksize_bits);
		nblock = ssi->nblocks;
		crc_sum = le32_to_cpu(sum->ss_datasum);
		ret = NILFS_SEG_FAIL_CHECKSUM_FULL;
	} else { /* only checks segment summary */
		offset = sizeof(sum->ss_datasum) + sizeof(sum->ss_sumsum);
		check_bytes = ssi->sumbytes;
		nblock = ssi->nsumblk;
		crc_sum = le32_to_cpu(sum->ss_sumsum);
		ret = NILFS_SEG_FAIL_CHECKSUM_SEGSUM;
	}

	if (unlikely(nblock == 0 ||
		     nblock > sbi->s_nilfs->ns_blocks_per_segment)) {
		/* This limits the number of blocks read in the CRC check */
		ret = NILFS_SEG_FAIL_CONSISTENCY;
		goto failed;
	}
	if (calc_crc_cont(sbi, bh_sum, &crc, offset, check_bytes,
			  pseg_start, nblock)) {
		ret = NILFS_SEG_FAIL_IO;
		goto failed;
	}
	if (crc == crc_sum)
		ret = 0;
 failed:
	brelse(bh_sum);
 out:
	return ret;
}

static void *segsum_get(struct super_block *sb, struct buffer_head **pbh,
			unsigned int *offset, unsigned int bytes)
{
	void *ptr;
	sector_t blocknr;

	BUG_ON((*pbh)->b_size < *offset);
	if (bytes > (*pbh)->b_size - *offset) {
		blocknr = (*pbh)->b_blocknr;
		brelse(*pbh);
		*pbh = sb_bread(sb, blocknr + 1);
		if (unlikely(!*pbh))
			return NULL;
		*offset = 0;
	}
	ptr = (*pbh)->b_data + *offset;
	*offset += bytes;
	return ptr;
}

static void segsum_skip(struct super_block *sb, struct buffer_head **pbh,
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
		*pbh = sb_bread(sb, blocknr + bcnt);
	}
}

static int
collect_blocks_from_segsum(struct nilfs_sb_info *sbi, sector_t sum_blocknr,
			   struct nilfs_segsum_info *ssi,
			   struct list_head *head)
{
	struct buffer_head *bh;
	unsigned int offset;
	unsigned long nfinfo = ssi->nfinfo;
	sector_t blocknr = sum_blocknr + ssi->nsumblk;
	ino_t ino;
	int err = -EIO;

	if (!nfinfo)
		return 0;

	bh = sb_bread(sbi->s_super, sum_blocknr);
	if (unlikely(!bh))
		goto out;

	offset = le16_to_cpu(
		((struct nilfs_segment_summary *)bh->b_data)->ss_bytes);
	for (;;) {
		unsigned long nblocks, ndatablk, nnodeblk;
		struct nilfs_finfo *finfo;

		finfo = segsum_get(sbi->s_super, &bh, &offset, sizeof(*finfo));
		if (unlikely(!finfo))
			goto out;

		ino = le64_to_cpu(finfo->fi_ino);
		nblocks = le32_to_cpu(finfo->fi_nblocks);
		ndatablk = le32_to_cpu(finfo->fi_ndatablk);
		nnodeblk = nblocks - ndatablk;

		while (ndatablk-- > 0) {
			struct nilfs_recovery_block *rb;
			struct nilfs_binfo_v *binfo;

			binfo = segsum_get(sbi->s_super, &bh, &offset,
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
		blocknr += nnodeblk; /* always 0 for the data sync segments */
		segsum_skip(sbi->s_super, &bh, &offset, sizeof(__le64),
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
		struct nilfs_recovery_block *rb
			= list_entry(head->next,
				     struct nilfs_recovery_block, list);
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
		struct nilfs_segment_entry *ent
			= list_entry(head->next,
				     struct nilfs_segment_entry, list);
		list_del(&ent->list);
		kfree(ent);
	}
}

static int nilfs_prepare_segment_for_recovery(struct the_nilfs *nilfs,
					      struct nilfs_sb_info *sbi,
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

	nilfs_attach_writer(nilfs, sbi);
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
	nilfs_detach_writer(nilfs, sbi);
	return err;
}

static int nilfs_recovery_copy_block(struct nilfs_sb_info *sbi,
				     struct nilfs_recovery_block *rb,
				     struct page *page)
{
	struct buffer_head *bh_org;
	void *kaddr;

	bh_org = sb_bread(sbi->s_super, rb->blocknr);
	if (unlikely(!bh_org))
		return -EIO;

	kaddr = kmap_atomic(page, KM_USER0);
	memcpy(kaddr + bh_offset(bh_org), bh_org->b_data, bh_org->b_size);
	kunmap_atomic(kaddr, KM_USER0);
	brelse(bh_org);
	return 0;
}

static int recover_dsync_blocks(struct nilfs_sb_info *sbi,
				struct list_head *head,
				unsigned long *nr_salvaged_blocks)
{
	struct inode *inode;
	struct nilfs_recovery_block *rb, *n;
	unsigned blocksize = sbi->s_super->s_blocksize;
	struct page *page;
	loff_t pos;
	int err = 0, err2 = 0;

	list_for_each_entry_safe(rb, n, head, list) {
		inode = nilfs_iget(sbi->s_super, rb->ino);
		if (IS_ERR(inode)) {
			err = PTR_ERR(inode);
			inode = NULL;
			goto failed_inode;
		}

		pos = rb->blkoff << inode->i_blkbits;
		page = NULL;
		err = block_write_begin(NULL, inode->i_mapping, pos, blocksize,
					0, &page, NULL, nilfs_get_block);
		if (unlikely(err))
			goto failed_inode;

		err = nilfs_recovery_copy_block(sbi, rb, page);
		if (unlikely(err))
			goto failed_page;

		err = nilfs_set_file_dirty(sbi, inode, 1);
		if (unlikely(err))
			goto failed_page;

		block_write_end(NULL, inode->i_mapping, pos, blocksize,
				blocksize, page, NULL);

		unlock_page(page);
		page_cache_release(page);

		(*nr_salvaged_blocks)++;
		goto next;

 failed_page:
		unlock_page(page);
		page_cache_release(page);

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
 * @sbi: nilfs_sb_info
 * @nilfs: the_nilfs
 * @ri: pointer to a nilfs_recovery_info
 */
static int nilfs_do_roll_forward(struct the_nilfs *nilfs,
				 struct nilfs_sb_info *sbi,
				 struct nilfs_recovery_info *ri)
{
	struct nilfs_segsum_info ssi;
	sector_t pseg_start;
	sector_t seg_start, seg_end;  /* Starting/ending DBN of full segment */
	unsigned long nsalvaged_blocks = 0;
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

	nilfs_attach_writer(nilfs, sbi);
	pseg_start = ri->ri_lsegs_start;
	seg_seq = ri->ri_lsegs_start_seq;
	segnum = nilfs_get_segnum_of_block(nilfs, pseg_start);
	nilfs_get_segment_range(nilfs, segnum, &seg_start, &seg_end);

	while (segnum != ri->ri_segnum || pseg_start <= ri->ri_pseg_start) {

		ret = load_segment_summary(sbi, pseg_start, seg_seq, &ssi, 1);
		if (ret) {
			if (ret == NILFS_SEG_FAIL_IO) {
				err = -EIO;
				goto failed;
			}
			goto strayed;
		}
		if (unlikely(NILFS_SEG_HAS_SR(&ssi)))
			goto confused;

		/* Found a valid partial segment; do recovery actions */
		nextnum = nilfs_get_segnum_of_block(nilfs, ssi.next);
		empty_seg = 0;
		nilfs->ns_ctime = ssi.ctime;
		if (!(ssi.flags & NILFS_SS_GC))
			nilfs->ns_nongc_ctime = ssi.ctime;

		switch (state) {
		case RF_INIT_ST:
			if (!NILFS_SEG_LOGBGN(&ssi) || !NILFS_SEG_DSYNC(&ssi))
				goto try_next_pseg;
			state = RF_DSYNC_ST;
			/* Fall through */
		case RF_DSYNC_ST:
			if (!NILFS_SEG_DSYNC(&ssi))
				goto confused;

			err = collect_blocks_from_segsum(
				sbi, pseg_start, &ssi, &dsync_blocks);
			if (unlikely(err))
				goto failed;
			if (NILFS_SEG_LOGEND(&ssi)) {
				err = recover_dsync_blocks(
					sbi, &dsync_blocks, &nsalvaged_blocks);
				if (unlikely(err))
					goto failed;
				state = RF_INIT_ST;
			}
			break; /* Fall through to try_next_pseg */
		}

 try_next_pseg:
		if (pseg_start == ri->ri_lsegs_end)
			break;
		pseg_start += ssi.nblocks;
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
		       sbi->s_super->s_id, nsalvaged_blocks);
		ri->ri_need_recovery = NILFS_RECOVERY_ROLLFORWARD_DONE;
	}
 out:
	dispose_recovery_list(&dsync_blocks);
	nilfs_detach_writer(sbi->s_nilfs, sbi);
	return err;

 confused:
	err = -EINVAL;
 failed:
	printk(KERN_ERR
	       "NILFS (device %s): Error roll-forwarding "
	       "(err=%d, pseg block=%llu). ",
	       sbi->s_super->s_id, err, (unsigned long long)pseg_start);
	goto out;
}

static void nilfs_finish_roll_forward(struct the_nilfs *nilfs,
				      struct nilfs_sb_info *sbi,
				      struct nilfs_recovery_info *ri)
{
	struct buffer_head *bh;
	int err;

	if (nilfs_get_segnum_of_block(nilfs, ri->ri_lsegs_start) !=
	    nilfs_get_segnum_of_block(nilfs, ri->ri_super_root))
		return;

	bh = sb_getblk(sbi->s_super, ri->ri_lsegs_start);
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
 * nilfs_recover_logical_segments - salvage logical segments written after
 * the latest super root
 * @nilfs: the_nilfs
 * @sbi: nilfs_sb_info
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
int nilfs_recover_logical_segments(struct the_nilfs *nilfs,
				   struct nilfs_sb_info *sbi,
				   struct nilfs_recovery_info *ri)
{
	int err;

	if (ri->ri_lsegs_start == 0 || ri->ri_lsegs_end == 0)
		return 0;

	err = nilfs_attach_checkpoint(sbi, ri->ri_cno);
	if (unlikely(err)) {
		printk(KERN_ERR
		       "NILFS: error loading the latest checkpoint.\n");
		return err;
	}

	err = nilfs_do_roll_forward(nilfs, sbi, ri);
	if (unlikely(err))
		goto failed;

	if (ri->ri_need_recovery == NILFS_RECOVERY_ROLLFORWARD_DONE) {
		err = nilfs_prepare_segment_for_recovery(nilfs, sbi, ri);
		if (unlikely(err)) {
			printk(KERN_ERR "NILFS: Error preparing segments for "
			       "recovery.\n");
			goto failed;
		}

		err = nilfs_attach_segment_constructor(sbi);
		if (unlikely(err))
			goto failed;

		set_nilfs_discontinued(nilfs);
		err = nilfs_construct_segment(sbi->s_super);
		nilfs_detach_segment_constructor(sbi);

		if (unlikely(err)) {
			printk(KERN_ERR "NILFS: Oops! recovery failed. "
			       "(err=%d)\n", err);
			goto failed;
		}

		nilfs_finish_roll_forward(nilfs, sbi, ri);
	}

 failed:
	nilfs_detach_checkpoint(sbi);
	return err;
}

/**
 * nilfs_search_super_root - search the latest valid super root
 * @nilfs: the_nilfs
 * @sbi: nilfs_sb_info
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
 */
int nilfs_search_super_root(struct the_nilfs *nilfs, struct nilfs_sb_info *sbi,
			    struct nilfs_recovery_info *ri)
{
	struct nilfs_segsum_info ssi;
	sector_t pseg_start, pseg_end, sr_pseg_start = 0;
	sector_t seg_start, seg_end; /* range of full segment (block number) */
	sector_t b, end;
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
		sb_breadahead(sbi->s_super, b++);

	for (;;) {
		/* Load segment summary */
		ret = load_segment_summary(sbi, pseg_start, seg_seq, &ssi, 1);
		if (ret) {
			if (ret == NILFS_SEG_FAIL_IO)
				goto failed;
			goto strayed;
		}
		pseg_end = pseg_start + ssi.nblocks - 1;
		if (unlikely(pseg_end > seg_end)) {
			ret = NILFS_SEG_FAIL_CONSISTENCY;
			goto strayed;
		}

		/* A valid partial segment */
		ri->ri_pseg_start = pseg_start;
		ri->ri_seq = seg_seq;
		ri->ri_segnum = segnum;
		nextnum = nilfs_get_segnum_of_block(nilfs, ssi.next);
		ri->ri_nextnum = nextnum;
		empty_seg = 0;

		if (!NILFS_SEG_HAS_SR(&ssi) && !scan_newer) {
			/* This will never happen because a superblock
			   (last_segment) always points to a pseg
			   having a super root. */
			ret = NILFS_SEG_FAIL_CONSISTENCY;
			goto failed;
		}

		if (pseg_start == seg_start) {
			nilfs_get_segment_range(nilfs, nextnum, &b, &end);
			while (b <= end)
				sb_breadahead(sbi->s_super, b++);
		}
		if (!NILFS_SEG_HAS_SR(&ssi)) {
			if (!ri->ri_lsegs_start && NILFS_SEG_LOGBGN(&ssi)) {
				ri->ri_lsegs_start = pseg_start;
				ri->ri_lsegs_start_seq = seg_seq;
			}
			if (NILFS_SEG_LOGEND(&ssi))
				ri->ri_lsegs_end = pseg_start;
			goto try_next_pseg;
		}

		/* A valid super root was found. */
		ri->ri_cno = cno++;
		ri->ri_super_root = pseg_end;
		ri->ri_lsegs_start = ri->ri_lsegs_end = 0;

		nilfs_dispose_segment_list(&segments);
		nilfs->ns_pseg_offset = (sr_pseg_start = pseg_start)
			+ ssi.nblocks - seg_start;
		nilfs->ns_seg_seq = seg_seq;
		nilfs->ns_segnum = segnum;
		nilfs->ns_cno = cno;  /* nilfs->ns_cno = ri->ri_cno + 1 */
		nilfs->ns_ctime = ssi.ctime;
		nilfs->ns_nextnum = nextnum;

		if (scan_newer)
			ri->ri_need_recovery = NILFS_RECOVERY_SR_UPDATED;
		else {
			if (nilfs->ns_mount_state & NILFS_VALID_FS)
				goto super_root_found;
			scan_newer = 1;
		}

		/* reset region for roll-forward */
		pseg_start += ssi.nblocks;
		if (pseg_start < seg_end)
			continue;
		goto feed_segment;

 try_next_pseg:
		/* Standing on a course, or met an inconsistent state */
		pseg_start += ssi.nblocks;
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
	list_splice_tail(&segments, &ri->ri_used_segments);
	nilfs->ns_last_pseg = sr_pseg_start;
	nilfs->ns_last_seq = nilfs->ns_seg_seq;
	nilfs->ns_last_cno = ri->ri_cno;
	return 0;

 failed:
	nilfs_dispose_segment_list(&segments);
	return (ret < 0) ? ret : nilfs_warn_segment_error(ret);
}
