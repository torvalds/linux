/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/mempool.h>
#include <linux/gfs2_ondisk.h>
#include <linux/bio.h>
#include <linux/fs.h>
#include <linux/list_sort.h>

#include "gfs2.h"
#include "incore.h"
#include "inode.h"
#include "glock.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "recovery.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"
#include "trace_gfs2.h"

/**
 * gfs2_pin - Pin a buffer in memory
 * @sdp: The superblock
 * @bh: The buffer to be pinned
 *
 * The log lock must be held when calling this function
 */
void gfs2_pin(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_bufdata *bd;

	BUG_ON(!current->journal_info);

	clear_buffer_dirty(bh);
	if (test_set_buffer_pinned(bh))
		gfs2_assert_withdraw(sdp, 0);
	if (!buffer_uptodate(bh))
		gfs2_io_error_bh(sdp, bh);
	bd = bh->b_private;
	/* If this buffer is in the AIL and it has already been written
	 * to in-place disk block, remove it from the AIL.
	 */
	spin_lock(&sdp->sd_ail_lock);
	if (bd->bd_tr)
		list_move(&bd->bd_ail_st_list, &bd->bd_tr->tr_ail2_list);
	spin_unlock(&sdp->sd_ail_lock);
	get_bh(bh);
	atomic_inc(&sdp->sd_log_pinned);
	trace_gfs2_pin(bd, 1);
}

static bool buffer_is_rgrp(const struct gfs2_bufdata *bd)
{
	return bd->bd_gl->gl_name.ln_type == LM_TYPE_RGRP;
}

static void maybe_release_space(struct gfs2_bufdata *bd)
{
	struct gfs2_glock *gl = bd->bd_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_rgrpd *rgd = gl->gl_object;
	unsigned int index = bd->bd_bh->b_blocknr - gl->gl_name.ln_number;
	struct gfs2_bitmap *bi = rgd->rd_bits + index;

	if (bi->bi_clone == 0)
		return;
	if (sdp->sd_args.ar_discard)
		gfs2_rgrp_send_discards(sdp, rgd->rd_data0, bd->bd_bh, bi, 1, NULL);
	memcpy(bi->bi_clone + bi->bi_offset,
	       bd->bd_bh->b_data + bi->bi_offset, bi->bi_len);
	clear_bit(GBF_FULL, &bi->bi_flags);
	rgd->rd_free_clone = rgd->rd_free;
}

/**
 * gfs2_unpin - Unpin a buffer
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to unpin
 * @ai:
 * @flags: The inode dirty flags
 *
 */

static void gfs2_unpin(struct gfs2_sbd *sdp, struct buffer_head *bh,
		       struct gfs2_trans *tr)
{
	struct gfs2_bufdata *bd = bh->b_private;

	BUG_ON(!buffer_uptodate(bh));
	BUG_ON(!buffer_pinned(bh));

	lock_buffer(bh);
	mark_buffer_dirty(bh);
	clear_buffer_pinned(bh);

	if (buffer_is_rgrp(bd))
		maybe_release_space(bd);

	spin_lock(&sdp->sd_ail_lock);
	if (bd->bd_tr) {
		list_del(&bd->bd_ail_st_list);
		brelse(bh);
	} else {
		struct gfs2_glock *gl = bd->bd_gl;
		list_add(&bd->bd_ail_gl_list, &gl->gl_ail_list);
		atomic_inc(&gl->gl_ail_count);
	}
	bd->bd_tr = tr;
	list_add(&bd->bd_ail_st_list, &tr->tr_ail1_list);
	spin_unlock(&sdp->sd_ail_lock);

	clear_bit(GLF_LFLUSH, &bd->bd_gl->gl_flags);
	trace_gfs2_pin(bd, 0);
	unlock_buffer(bh);
	atomic_dec(&sdp->sd_log_pinned);
}

static void gfs2_log_incr_head(struct gfs2_sbd *sdp)
{
	BUG_ON((sdp->sd_log_flush_head == sdp->sd_log_tail) &&
	       (sdp->sd_log_flush_head != sdp->sd_log_head));

	if (++sdp->sd_log_flush_head == sdp->sd_jdesc->jd_blocks) {
		sdp->sd_log_flush_head = 0;
		sdp->sd_log_flush_wrapped = 1;
	}
}

static u64 gfs2_log_bmap(struct gfs2_sbd *sdp)
{
	unsigned int lbn = sdp->sd_log_flush_head;
	struct gfs2_journal_extent *je;
	u64 block;

	list_for_each_entry(je, &sdp->sd_jdesc->extent_list, extent_list) {
		if (lbn >= je->lblock && lbn < je->lblock + je->blocks) {
			block = je->dblock + lbn - je->lblock;
			gfs2_log_incr_head(sdp);
			return block;
		}
	}

	return -1;
}

/**
 * gfs2_end_log_write_bh - end log write of pagecache data with buffers
 * @sdp: The superblock
 * @bvec: The bio_vec
 * @error: The i/o status
 *
 * This finds the relavent buffers and unlocks then and sets the
 * error flag according to the status of the i/o request. This is
 * used when the log is writing data which has an in-place version
 * that is pinned in the pagecache.
 */

static void gfs2_end_log_write_bh(struct gfs2_sbd *sdp, struct bio_vec *bvec,
				  int error)
{
	struct buffer_head *bh, *next;
	struct page *page = bvec->bv_page;
	unsigned size;

	bh = page_buffers(page);
	size = bvec->bv_len;
	while (bh_offset(bh) < bvec->bv_offset)
		bh = bh->b_this_page;
	do {
		if (error)
			set_buffer_write_io_error(bh);
		unlock_buffer(bh);
		next = bh->b_this_page;
		size -= bh->b_size;
		brelse(bh);
		bh = next;
	} while(bh && size);
}

/**
 * gfs2_end_log_write - end of i/o to the log
 * @bio: The bio
 * @error: Status of i/o request
 *
 * Each bio_vec contains either data from the pagecache or data
 * relating to the log itself. Here we iterate over the bio_vec
 * array, processing both kinds of data.
 *
 */

static void gfs2_end_log_write(struct bio *bio, int error)
{
	struct gfs2_sbd *sdp = bio->bi_private;
	struct bio_vec *bvec;
	struct page *page;
	int i;

	if (error) {
		sdp->sd_log_error = error;
		fs_err(sdp, "Error %d writing to log\n", error);
	}

	bio_for_each_segment_all(bvec, bio, i) {
		page = bvec->bv_page;
		if (page_has_buffers(page))
			gfs2_end_log_write_bh(sdp, bvec, error);
		else
			mempool_free(page, gfs2_page_pool);
	}

	bio_put(bio);
	if (atomic_dec_and_test(&sdp->sd_log_in_flight))
		wake_up(&sdp->sd_log_flush_wait);
}

/**
 * gfs2_log_flush_bio - Submit any pending log bio
 * @sdp: The superblock
 * @rw: The rw flags
 *
 * Submit any pending part-built or full bio to the block device. If
 * there is no pending bio, then this is a no-op.
 */

void gfs2_log_flush_bio(struct gfs2_sbd *sdp, int rw)
{
	if (sdp->sd_log_bio) {
		atomic_inc(&sdp->sd_log_in_flight);
		submit_bio(rw, sdp->sd_log_bio);
		sdp->sd_log_bio = NULL;
	}
}

/**
 * gfs2_log_alloc_bio - Allocate a new bio for log writing
 * @sdp: The superblock
 * @blkno: The next device block number we want to write to
 *
 * This should never be called when there is a cached bio in the
 * super block. When it returns, there will be a cached bio in the
 * super block which will have as many bio_vecs as the device is
 * happy to handle.
 *
 * Returns: Newly allocated bio
 */

static struct bio *gfs2_log_alloc_bio(struct gfs2_sbd *sdp, u64 blkno)
{
	struct super_block *sb = sdp->sd_vfs;
	unsigned nrvecs = bio_get_nr_vecs(sb->s_bdev);
	struct bio *bio;

	BUG_ON(sdp->sd_log_bio);

	while (1) {
		bio = bio_alloc(GFP_NOIO, nrvecs);
		if (likely(bio))
			break;
		nrvecs = max(nrvecs/2, 1U);
	}

	bio->bi_sector = blkno * (sb->s_blocksize >> 9);
	bio->bi_bdev = sb->s_bdev;
	bio->bi_end_io = gfs2_end_log_write;
	bio->bi_private = sdp;

	sdp->sd_log_bio = bio;

	return bio;
}

/**
 * gfs2_log_get_bio - Get cached log bio, or allocate a new one
 * @sdp: The superblock
 * @blkno: The device block number we want to write to
 *
 * If there is a cached bio, then if the next block number is sequential
 * with the previous one, return it, otherwise flush the bio to the
 * device. If there is not a cached bio, or we just flushed it, then
 * allocate a new one.
 *
 * Returns: The bio to use for log writes
 */

static struct bio *gfs2_log_get_bio(struct gfs2_sbd *sdp, u64 blkno)
{
	struct bio *bio = sdp->sd_log_bio;
	u64 nblk;

	if (bio) {
		nblk = bio_end_sector(bio);
		nblk >>= sdp->sd_fsb2bb_shift;
		if (blkno == nblk)
			return bio;
		gfs2_log_flush_bio(sdp, WRITE);
	}

	return gfs2_log_alloc_bio(sdp, blkno);
}


/**
 * gfs2_log_write - write to log
 * @sdp: the filesystem
 * @page: the page to write
 * @size: the size of the data to write
 * @offset: the offset within the page 
 *
 * Try and add the page segment to the current bio. If that fails,
 * submit the current bio to the device and create a new one, and
 * then add the page segment to that.
 */

static void gfs2_log_write(struct gfs2_sbd *sdp, struct page *page,
			   unsigned size, unsigned offset)
{
	u64 blkno = gfs2_log_bmap(sdp);
	struct bio *bio;
	int ret;

	bio = gfs2_log_get_bio(sdp, blkno);
	ret = bio_add_page(bio, page, size, offset);
	if (ret == 0) {
		gfs2_log_flush_bio(sdp, WRITE);
		bio = gfs2_log_alloc_bio(sdp, blkno);
		ret = bio_add_page(bio, page, size, offset);
		WARN_ON(ret == 0);
	}
}

/**
 * gfs2_log_write_bh - write a buffer's content to the log
 * @sdp: The super block
 * @bh: The buffer pointing to the in-place location
 * 
 * This writes the content of the buffer to the next available location
 * in the log. The buffer will be unlocked once the i/o to the log has
 * completed.
 */

static void gfs2_log_write_bh(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	gfs2_log_write(sdp, bh->b_page, bh->b_size, bh_offset(bh));
}

/**
 * gfs2_log_write_page - write one block stored in a page, into the log
 * @sdp: The superblock
 * @page: The struct page
 *
 * This writes the first block-sized part of the page into the log. Note
 * that the page must have been allocated from the gfs2_page_pool mempool
 * and that after this has been called, ownership has been transferred and
 * the page may be freed at any time.
 */

void gfs2_log_write_page(struct gfs2_sbd *sdp, struct page *page)
{
	struct super_block *sb = sdp->sd_vfs;
	gfs2_log_write(sdp, page, sb->s_blocksize, 0);
}

static struct page *gfs2_get_log_desc(struct gfs2_sbd *sdp, u32 ld_type,
				      u32 ld_length, u32 ld_data1)
{
	struct page *page = mempool_alloc(gfs2_page_pool, GFP_NOIO);
	struct gfs2_log_descriptor *ld = page_address(page);
	clear_page(ld);
	ld->ld_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	ld->ld_header.mh_type = cpu_to_be32(GFS2_METATYPE_LD);
	ld->ld_header.mh_format = cpu_to_be32(GFS2_FORMAT_LD);
	ld->ld_type = cpu_to_be32(ld_type);
	ld->ld_length = cpu_to_be32(ld_length);
	ld->ld_data1 = cpu_to_be32(ld_data1);
	ld->ld_data2 = 0;
	return page;
}

static void gfs2_check_magic(struct buffer_head *bh)
{
	void *kaddr;
	__be32 *ptr;

	clear_buffer_escaped(bh);
	kaddr = kmap_atomic(bh->b_page);
	ptr = kaddr + bh_offset(bh);
	if (*ptr == cpu_to_be32(GFS2_MAGIC))
		set_buffer_escaped(bh);
	kunmap_atomic(kaddr);
}

static int blocknr_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct gfs2_bufdata *bda, *bdb;

	bda = list_entry(a, struct gfs2_bufdata, bd_list);
	bdb = list_entry(b, struct gfs2_bufdata, bd_list);

	if (bda->bd_bh->b_blocknr < bdb->bd_bh->b_blocknr)
		return -1;
	if (bda->bd_bh->b_blocknr > bdb->bd_bh->b_blocknr)
		return 1;
	return 0;
}

static void gfs2_before_commit(struct gfs2_sbd *sdp, unsigned int limit,
				unsigned int total, struct list_head *blist,
				bool is_databuf)
{
	struct gfs2_log_descriptor *ld;
	struct gfs2_bufdata *bd1 = NULL, *bd2;
	struct page *page;
	unsigned int num;
	unsigned n;
	__be64 *ptr;

	gfs2_log_lock(sdp);
	list_sort(NULL, blist, blocknr_cmp);
	bd1 = bd2 = list_prepare_entry(bd1, blist, bd_list);
	while(total) {
		num = total;
		if (total > limit)
			num = limit;
		gfs2_log_unlock(sdp);
		page = gfs2_get_log_desc(sdp,
					 is_databuf ? GFS2_LOG_DESC_JDATA :
					 GFS2_LOG_DESC_METADATA, num + 1, num);
		ld = page_address(page);
		gfs2_log_lock(sdp);
		ptr = (__be64 *)(ld + 1);

		n = 0;
		list_for_each_entry_continue(bd1, blist, bd_list) {
			*ptr++ = cpu_to_be64(bd1->bd_bh->b_blocknr);
			if (is_databuf) {
				gfs2_check_magic(bd1->bd_bh);
				*ptr++ = cpu_to_be64(buffer_escaped(bd1->bd_bh) ? 1 : 0);
			}
			if (++n >= num)
				break;
		}

		gfs2_log_unlock(sdp);
		gfs2_log_write_page(sdp, page);
		gfs2_log_lock(sdp);

		n = 0;
		list_for_each_entry_continue(bd2, blist, bd_list) {
			get_bh(bd2->bd_bh);
			gfs2_log_unlock(sdp);
			lock_buffer(bd2->bd_bh);

			if (buffer_escaped(bd2->bd_bh)) {
				void *kaddr;
				page = mempool_alloc(gfs2_page_pool, GFP_NOIO);
				ptr = page_address(page);
				kaddr = kmap_atomic(bd2->bd_bh->b_page);
				memcpy(ptr, kaddr + bh_offset(bd2->bd_bh),
				       bd2->bd_bh->b_size);
				kunmap_atomic(kaddr);
				*(__be32 *)ptr = 0;
				clear_buffer_escaped(bd2->bd_bh);
				unlock_buffer(bd2->bd_bh);
				brelse(bd2->bd_bh);
				gfs2_log_write_page(sdp, page);
			} else {
				gfs2_log_write_bh(sdp, bd2->bd_bh);
			}
			gfs2_log_lock(sdp);
			if (++n >= num)
				break;
		}

		BUG_ON(total < num);
		total -= num;
	}
	gfs2_log_unlock(sdp);
}

static void buf_lo_before_commit(struct gfs2_sbd *sdp)
{
	unsigned int limit = buf_limit(sdp); /* 503 for 4k blocks */

	gfs2_before_commit(sdp, limit, sdp->sd_log_num_buf,
			   &sdp->sd_log_le_buf, 0);
}

static void buf_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct list_head *head = &sdp->sd_log_le_buf;
	struct gfs2_bufdata *bd;

	if (tr == NULL) {
		gfs2_assert(sdp, list_empty(head));
		return;
	}

	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata, bd_list);
		list_del_init(&bd->bd_list);
		sdp->sd_log_num_buf--;

		gfs2_unpin(sdp, bd->bd_bh, tr);
	}
	gfs2_assert_warn(sdp, !sdp->sd_log_num_buf);
}

static void buf_lo_before_scan(struct gfs2_jdesc *jd,
			       struct gfs2_log_header_host *head, int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (pass != 0)
		return;

	sdp->sd_found_blocks = 0;
	sdp->sd_replayed_blocks = 0;
}

static int buf_lo_scan_elements(struct gfs2_jdesc *jd, unsigned int start,
				struct gfs2_log_descriptor *ld, __be64 *ptr,
				int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct gfs2_glock *gl = ip->i_gl;
	unsigned int blks = be32_to_cpu(ld->ld_data1);
	struct buffer_head *bh_log, *bh_ip;
	u64 blkno;
	int error = 0;

	if (pass != 1 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_METADATA)
		return 0;

	gfs2_replay_incr_blk(sdp, &start);

	for (; blks; gfs2_replay_incr_blk(sdp, &start), blks--) {
		blkno = be64_to_cpu(*ptr++);

		sdp->sd_found_blocks++;

		if (gfs2_revoke_check(sdp, blkno, start))
			continue;

		error = gfs2_replay_read_block(jd, start, &bh_log);
		if (error)
			return error;

		bh_ip = gfs2_meta_new(gl, blkno);
		memcpy(bh_ip->b_data, bh_log->b_data, bh_log->b_size);

		if (gfs2_meta_check(sdp, bh_ip))
			error = -EIO;
		else
			mark_buffer_dirty(bh_ip);

		brelse(bh_log);
		brelse(bh_ip);

		if (error)
			break;

		sdp->sd_replayed_blocks++;
	}

	return error;
}

static void buf_lo_after_scan(struct gfs2_jdesc *jd, int error, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (error) {
		gfs2_meta_sync(ip->i_gl);
		return;
	}
	if (pass != 1)
		return;

	gfs2_meta_sync(ip->i_gl);

	fs_info(sdp, "jid=%u: Replayed %u of %u blocks\n",
	        jd->jd_jid, sdp->sd_replayed_blocks, sdp->sd_found_blocks);
}

static void revoke_lo_before_commit(struct gfs2_sbd *sdp)
{
	struct gfs2_meta_header *mh;
	unsigned int offset;
	struct list_head *head = &sdp->sd_log_le_revoke;
	struct gfs2_bufdata *bd;
	struct page *page;
	unsigned int length;

	gfs2_write_revokes(sdp);
	if (!sdp->sd_log_num_revoke)
		return;

	length = gfs2_struct2blk(sdp, sdp->sd_log_num_revoke, sizeof(u64));
	page = gfs2_get_log_desc(sdp, GFS2_LOG_DESC_REVOKE, length, sdp->sd_log_num_revoke);
	offset = sizeof(struct gfs2_log_descriptor);

	list_for_each_entry(bd, head, bd_list) {
		sdp->sd_log_num_revoke--;

		if (offset + sizeof(u64) > sdp->sd_sb.sb_bsize) {

			gfs2_log_write_page(sdp, page);
			page = mempool_alloc(gfs2_page_pool, GFP_NOIO);
			mh = page_address(page);
			clear_page(mh);
			mh->mh_magic = cpu_to_be32(GFS2_MAGIC);
			mh->mh_type = cpu_to_be32(GFS2_METATYPE_LB);
			mh->mh_format = cpu_to_be32(GFS2_FORMAT_LB);
			offset = sizeof(struct gfs2_meta_header);
		}

		*(__be64 *)(page_address(page) + offset) = cpu_to_be64(bd->bd_blkno);
		offset += sizeof(u64);
	}
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_revoke);

	gfs2_log_write_page(sdp, page);
}

static void revoke_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct list_head *head = &sdp->sd_log_le_revoke;
	struct gfs2_bufdata *bd;
	struct gfs2_glock *gl;

	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata, bd_list);
		list_del_init(&bd->bd_list);
		gl = bd->bd_gl;
		atomic_dec(&gl->gl_revokes);
		clear_bit(GLF_LFLUSH, &gl->gl_flags);
		kmem_cache_free(gfs2_bufdata_cachep, bd);
	}
}

static void revoke_lo_before_scan(struct gfs2_jdesc *jd,
				  struct gfs2_log_header_host *head, int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (pass != 0)
		return;

	sdp->sd_found_revokes = 0;
	sdp->sd_replay_tail = head->lh_tail;
}

static int revoke_lo_scan_elements(struct gfs2_jdesc *jd, unsigned int start,
				   struct gfs2_log_descriptor *ld, __be64 *ptr,
				   int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	unsigned int blks = be32_to_cpu(ld->ld_length);
	unsigned int revokes = be32_to_cpu(ld->ld_data1);
	struct buffer_head *bh;
	unsigned int offset;
	u64 blkno;
	int first = 1;
	int error;

	if (pass != 0 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_REVOKE)
		return 0;

	offset = sizeof(struct gfs2_log_descriptor);

	for (; blks; gfs2_replay_incr_blk(sdp, &start), blks--) {
		error = gfs2_replay_read_block(jd, start, &bh);
		if (error)
			return error;

		if (!first)
			gfs2_metatype_check(sdp, bh, GFS2_METATYPE_LB);

		while (offset + sizeof(u64) <= sdp->sd_sb.sb_bsize) {
			blkno = be64_to_cpu(*(__be64 *)(bh->b_data + offset));

			error = gfs2_revoke_add(sdp, blkno, start);
			if (error < 0) {
				brelse(bh);
				return error;
			}
			else if (error)
				sdp->sd_found_revokes++;

			if (!--revokes)
				break;
			offset += sizeof(u64);
		}

		brelse(bh);
		offset = sizeof(struct gfs2_meta_header);
		first = 0;
	}

	return 0;
}

static void revoke_lo_after_scan(struct gfs2_jdesc *jd, int error, int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (error) {
		gfs2_revoke_clean(sdp);
		return;
	}
	if (pass != 1)
		return;

	fs_info(sdp, "jid=%u: Found %u revoke tags\n",
	        jd->jd_jid, sdp->sd_found_revokes);

	gfs2_revoke_clean(sdp);
}

/**
 * databuf_lo_before_commit - Scan the data buffers, writing as we go
 *
 */

static void databuf_lo_before_commit(struct gfs2_sbd *sdp)
{
	unsigned int limit = buf_limit(sdp) / 2;

	gfs2_before_commit(sdp, limit, sdp->sd_log_num_databuf,
			   &sdp->sd_log_le_databuf, 1);
}

static int databuf_lo_scan_elements(struct gfs2_jdesc *jd, unsigned int start,
				    struct gfs2_log_descriptor *ld,
				    __be64 *ptr, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct gfs2_glock *gl = ip->i_gl;
	unsigned int blks = be32_to_cpu(ld->ld_data1);
	struct buffer_head *bh_log, *bh_ip;
	u64 blkno;
	u64 esc;
	int error = 0;

	if (pass != 1 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_JDATA)
		return 0;

	gfs2_replay_incr_blk(sdp, &start);
	for (; blks; gfs2_replay_incr_blk(sdp, &start), blks--) {
		blkno = be64_to_cpu(*ptr++);
		esc = be64_to_cpu(*ptr++);

		sdp->sd_found_blocks++;

		if (gfs2_revoke_check(sdp, blkno, start))
			continue;

		error = gfs2_replay_read_block(jd, start, &bh_log);
		if (error)
			return error;

		bh_ip = gfs2_meta_new(gl, blkno);
		memcpy(bh_ip->b_data, bh_log->b_data, bh_log->b_size);

		/* Unescape */
		if (esc) {
			__be32 *eptr = (__be32 *)bh_ip->b_data;
			*eptr = cpu_to_be32(GFS2_MAGIC);
		}
		mark_buffer_dirty(bh_ip);

		brelse(bh_log);
		brelse(bh_ip);

		sdp->sd_replayed_blocks++;
	}

	return error;
}

/* FIXME: sort out accounting for log blocks etc. */

static void databuf_lo_after_scan(struct gfs2_jdesc *jd, int error, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (error) {
		gfs2_meta_sync(ip->i_gl);
		return;
	}
	if (pass != 1)
		return;

	/* data sync? */
	gfs2_meta_sync(ip->i_gl);

	fs_info(sdp, "jid=%u: Replayed %u of %u data blocks\n",
		jd->jd_jid, sdp->sd_replayed_blocks, sdp->sd_found_blocks);
}

static void databuf_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct list_head *head = &sdp->sd_log_le_databuf;
	struct gfs2_bufdata *bd;

	if (tr == NULL) {
		gfs2_assert(sdp, list_empty(head));
		return;
	}

	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata, bd_list);
		list_del_init(&bd->bd_list);
		sdp->sd_log_num_databuf--;
		gfs2_unpin(sdp, bd->bd_bh, tr);
	}
	gfs2_assert_warn(sdp, !sdp->sd_log_num_databuf);
}


const struct gfs2_log_operations gfs2_buf_lops = {
	.lo_before_commit = buf_lo_before_commit,
	.lo_after_commit = buf_lo_after_commit,
	.lo_before_scan = buf_lo_before_scan,
	.lo_scan_elements = buf_lo_scan_elements,
	.lo_after_scan = buf_lo_after_scan,
	.lo_name = "buf",
};

const struct gfs2_log_operations gfs2_revoke_lops = {
	.lo_before_commit = revoke_lo_before_commit,
	.lo_after_commit = revoke_lo_after_commit,
	.lo_before_scan = revoke_lo_before_scan,
	.lo_scan_elements = revoke_lo_scan_elements,
	.lo_after_scan = revoke_lo_after_scan,
	.lo_name = "revoke",
};

const struct gfs2_log_operations gfs2_databuf_lops = {
	.lo_before_commit = databuf_lo_before_commit,
	.lo_after_commit = databuf_lo_after_commit,
	.lo_scan_elements = databuf_lo_scan_elements,
	.lo_after_scan = databuf_lo_after_scan,
	.lo_name = "databuf",
};

const struct gfs2_log_operations *gfs2_log_ops[] = {
	&gfs2_databuf_lops,
	&gfs2_buf_lops,
	&gfs2_revoke_lops,
	NULL,
};

