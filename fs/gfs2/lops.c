// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
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
#include <linux/blkdev.h>

#include "bmap.h"
#include "dir.h"
#include "gfs2.h"
#include "incore.h"
#include "inode.h"
#include "glock.h"
#include "glops.h"
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
		gfs2_io_error_bh_wd(sdp, bh);
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
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct gfs2_rgrpd *rgd = gfs2_glock2rgrp(gl);
	unsigned int index = bd->bd_bh->b_blocknr - gl->gl_name.ln_number;
	struct gfs2_bitmap *bi = rgd->rd_bits + index;

	rgrp_lock_local(rgd);
	if (bi->bi_clone == NULL)
		goto out;
	if (sdp->sd_args.ar_discard)
		gfs2_rgrp_send_discards(sdp, rgd->rd_data0, bd->bd_bh, bi, 1, NULL);
	memcpy(bi->bi_clone + bi->bi_offset,
	       bd->bd_bh->b_data + bi->bi_offset, bi->bi_bytes);
	clear_bit(GBF_FULL, &bi->bi_flags);
	rgd->rd_free_clone = rgd->rd_free;
	BUG_ON(rgd->rd_free_clone < rgd->rd_reserved);
	rgd->rd_extfail_pt = rgd->rd_free;

out:
	rgrp_unlock_local(rgd);
}

/**
 * gfs2_unpin - Unpin a buffer
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to unpin
 * @tr: The system transaction being flushed
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

void gfs2_log_incr_head(struct gfs2_sbd *sdp)
{
	BUG_ON((sdp->sd_log_flush_head == sdp->sd_log_tail) &&
	       (sdp->sd_log_flush_head != sdp->sd_log_head));

	if (++sdp->sd_log_flush_head == sdp->sd_jdesc->jd_blocks)
		sdp->sd_log_flush_head = 0;
}

u64 gfs2_log_bmap(struct gfs2_jdesc *jd, unsigned int lblock)
{
	struct gfs2_journal_extent *je;

	list_for_each_entry(je, &jd->extent_list, list) {
		if (lblock >= je->lblock && lblock < je->lblock + je->blocks)
			return je->dblock + lblock - je->lblock;
	}

	return -1;
}

/**
 * gfs2_end_log_write_bh - end log write of pagecache data with buffers
 * @sdp: The superblock
 * @folio: The folio
 * @offset: The first byte within the folio that completed
 * @size: The number of bytes that completed
 * @error: The i/o status
 *
 * This finds the relevant buffers and unlocks them and sets the
 * error flag according to the status of the i/o request. This is
 * used when the log is writing data which has an in-place version
 * that is pinned in the pagecache.
 */

static void gfs2_end_log_write_bh(struct gfs2_sbd *sdp, struct folio *folio,
		size_t offset, size_t size, blk_status_t error)
{
	struct buffer_head *bh, *next;

	bh = folio_buffers(folio);
	while (bh_offset(bh) < offset)
		bh = bh->b_this_page;
	do {
		if (error)
			mark_buffer_write_io_error(bh);
		unlock_buffer(bh);
		next = bh->b_this_page;
		size -= bh->b_size;
		brelse(bh);
		bh = next;
	} while (bh && size);
}

/**
 * gfs2_end_log_write - end of i/o to the log
 * @bio: The bio
 *
 * Each bio_vec contains either data from the pagecache or data
 * relating to the log itself. Here we iterate over the bio_vec
 * array, processing both kinds of data.
 *
 */

static void gfs2_end_log_write(struct bio *bio)
{
	struct gfs2_sbd *sdp = bio->bi_private;
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	if (bio->bi_status) {
		int err = blk_status_to_errno(bio->bi_status);

		if (!cmpxchg(&sdp->sd_log_error, 0, err))
			fs_err(sdp, "Error %d writing to journal, jid=%u\n",
			       err, sdp->sd_jdesc->jd_jid);
		gfs2_withdraw_delayed(sdp);
		/* prevent more writes to the journal */
		clear_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);
		wake_up(&sdp->sd_logd_waitq);
	}

	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *page = bvec->bv_page;
		struct folio *folio = page_folio(page);

		if (folio && folio_buffers(folio))
			gfs2_end_log_write_bh(sdp, folio, bvec->bv_offset,
					bvec->bv_len, bio->bi_status);
		else
			mempool_free(page, gfs2_page_pool);
	}

	bio_put(bio);
	if (atomic_dec_and_test(&sdp->sd_log_in_flight))
		wake_up(&sdp->sd_log_flush_wait);
}

/**
 * gfs2_log_submit_bio - Submit any pending log bio
 * @biop: Address of the bio pointer
 * @opf: REQ_OP | op_flags
 *
 * Submit any pending part-built or full bio to the block device. If
 * there is no pending bio, then this is a no-op.
 */

void gfs2_log_submit_bio(struct bio **biop, blk_opf_t opf)
{
	struct bio *bio = *biop;
	if (bio) {
		struct gfs2_sbd *sdp = bio->bi_private;
		atomic_inc(&sdp->sd_log_in_flight);
		bio->bi_opf = opf;
		submit_bio(bio);
		*biop = NULL;
	}
}

/**
 * gfs2_log_alloc_bio - Allocate a bio
 * @sdp: The super block
 * @blkno: The device block number we want to write to
 * @end_io: The bi_end_io callback
 *
 * Allocate a new bio, initialize it with the given parameters and return it.
 *
 * Returns: The newly allocated bio
 */

static struct bio *gfs2_log_alloc_bio(struct gfs2_sbd *sdp, u64 blkno,
				      bio_end_io_t *end_io)
{
	struct super_block *sb = sdp->sd_vfs;
	struct bio *bio = bio_alloc(sb->s_bdev, BIO_MAX_VECS, 0, GFP_NOIO);

	bio->bi_iter.bi_sector = blkno << sdp->sd_fsb2bb_shift;
	bio->bi_end_io = end_io;
	bio->bi_private = sdp;

	return bio;
}

/**
 * gfs2_log_get_bio - Get cached log bio, or allocate a new one
 * @sdp: The super block
 * @blkno: The device block number we want to write to
 * @biop: The bio to get or allocate
 * @op: REQ_OP
 * @end_io: The bi_end_io callback
 * @flush: Always flush the current bio and allocate a new one?
 *
 * If there is a cached bio, then if the next block number is sequential
 * with the previous one, return it, otherwise flush the bio to the
 * device. If there is no cached bio, or we just flushed it, then
 * allocate a new one.
 *
 * Returns: The bio to use for log writes
 */

static struct bio *gfs2_log_get_bio(struct gfs2_sbd *sdp, u64 blkno,
				    struct bio **biop, enum req_op op,
				    bio_end_io_t *end_io, bool flush)
{
	struct bio *bio = *biop;

	if (bio) {
		u64 nblk;

		nblk = bio_end_sector(bio);
		nblk >>= sdp->sd_fsb2bb_shift;
		if (blkno == nblk && !flush)
			return bio;
		gfs2_log_submit_bio(biop, op);
	}

	*biop = gfs2_log_alloc_bio(sdp, blkno, end_io);
	return *biop;
}

/**
 * gfs2_log_write - write to log
 * @sdp: the filesystem
 * @jd: The journal descriptor
 * @page: the page to write
 * @size: the size of the data to write
 * @offset: the offset within the page 
 * @blkno: block number of the log entry
 *
 * Try and add the page segment to the current bio. If that fails,
 * submit the current bio to the device and create a new one, and
 * then add the page segment to that.
 */

void gfs2_log_write(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd,
		    struct page *page, unsigned size, unsigned offset,
		    u64 blkno)
{
	struct bio *bio;
	int ret;

	bio = gfs2_log_get_bio(sdp, blkno, &jd->jd_log_bio, REQ_OP_WRITE,
			       gfs2_end_log_write, false);
	ret = bio_add_page(bio, page, size, offset);
	if (ret == 0) {
		bio = gfs2_log_get_bio(sdp, blkno, &jd->jd_log_bio,
				       REQ_OP_WRITE, gfs2_end_log_write, true);
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
	u64 dblock;

	dblock = gfs2_log_bmap(sdp->sd_jdesc, sdp->sd_log_flush_head);
	gfs2_log_incr_head(sdp);
	gfs2_log_write(sdp, sdp->sd_jdesc, folio_page(bh->b_folio, 0),
			bh->b_size, bh_offset(bh), dblock);
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

static void gfs2_log_write_page(struct gfs2_sbd *sdp, struct page *page)
{
	struct super_block *sb = sdp->sd_vfs;
	u64 dblock;

	dblock = gfs2_log_bmap(sdp->sd_jdesc, sdp->sd_log_flush_head);
	gfs2_log_incr_head(sdp);
	gfs2_log_write(sdp, sdp->sd_jdesc, page, sb->s_blocksize, 0, dblock);
}

/**
 * gfs2_end_log_read - end I/O callback for reads from the log
 * @bio: The bio
 *
 * Simply unlock the pages in the bio. The main thread will wait on them and
 * process them in order as necessary.
 */
static void gfs2_end_log_read(struct bio *bio)
{
	int error = blk_status_to_errno(bio->bi_status);
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		/* We're abusing wb_err to get the error to gfs2_find_jhead */
		filemap_set_wb_err(fi.folio->mapping, error);
		folio_end_read(fi.folio, !error);
	}

	bio_put(bio);
}

/**
 * gfs2_jhead_folio_search - Look for the journal head in a given page.
 * @jd: The journal descriptor
 * @head: The journal head to start from
 * @folio: The folio to look in
 *
 * Returns: 1 if found, 0 otherwise.
 */
static bool gfs2_jhead_folio_search(struct gfs2_jdesc *jd,
				    struct gfs2_log_header_host *head,
				    struct folio *folio)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct gfs2_log_header_host lh;
	void *kaddr;
	unsigned int offset;
	bool ret = false;

	VM_BUG_ON_FOLIO(folio_test_large(folio), folio);
	kaddr = kmap_local_folio(folio, 0);
	for (offset = 0; offset < PAGE_SIZE; offset += sdp->sd_sb.sb_bsize) {
		if (!__get_log_header(sdp, kaddr + offset, 0, &lh)) {
			if (lh.lh_sequence >= head->lh_sequence)
				*head = lh;
			else {
				ret = true;
				break;
			}
		}
	}
	kunmap_local(kaddr);
	return ret;
}

/**
 * gfs2_jhead_process_page - Search/cleanup a page
 * @jd: The journal descriptor
 * @index: Index of the page to look into
 * @head: The journal head to start from
 * @done: If set, perform only cleanup, else search and set if found.
 *
 * Find the folio with 'index' in the journal's mapping. Search the folio for
 * the journal head if requested (cleanup == false). Release refs on the
 * folio so the page cache can reclaim it. We grabbed a
 * reference on this folio twice, first when we did a filemap_grab_folio()
 * to obtain the folio to add it to the bio and second when we do a
 * filemap_get_folio() here to get the folio to wait on while I/O on it is being
 * completed.
 * This function is also used to free up a folio we might've grabbed but not
 * used. Maybe we added it to a bio, but not submitted it for I/O. Or we
 * submitted the I/O, but we already found the jhead so we only need to drop
 * our references to the folio.
 */

static void gfs2_jhead_process_page(struct gfs2_jdesc *jd, unsigned long index,
				    struct gfs2_log_header_host *head,
				    bool *done)
{
	struct folio *folio;

	folio = filemap_get_folio(jd->jd_inode->i_mapping, index);

	folio_wait_locked(folio);
	if (!folio_test_uptodate(folio))
		*done = true;

	if (!*done)
		*done = gfs2_jhead_folio_search(jd, head, folio);

	/* filemap_get_folio() and the earlier filemap_grab_folio() */
	folio_put_refs(folio, 2);
}

static struct bio *gfs2_chain_bio(struct bio *prev, unsigned int nr_iovecs)
{
	struct bio *new;

	new = bio_alloc(prev->bi_bdev, nr_iovecs, prev->bi_opf, GFP_NOIO);
	bio_clone_blkg_association(new, prev);
	new->bi_iter.bi_sector = bio_end_sector(prev);
	bio_chain(new, prev);
	submit_bio(prev);
	return new;
}

/**
 * gfs2_find_jhead - find the head of a log
 * @jd: The journal descriptor
 * @head: The log descriptor for the head of the log is returned here
 *
 * Do a search of a journal by reading it in large chunks using bios and find
 * the valid log entry with the highest sequence number.  (i.e. the log head)
 *
 * Returns: 0 on success, errno otherwise
 */
int gfs2_find_jhead(struct gfs2_jdesc *jd, struct gfs2_log_header_host *head)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct address_space *mapping = jd->jd_inode->i_mapping;
	unsigned int block = 0, blocks_submitted = 0, blocks_read = 0;
	unsigned int bsize = sdp->sd_sb.sb_bsize, off;
	unsigned int bsize_shift = sdp->sd_sb.sb_bsize_shift;
	unsigned int shift = PAGE_SHIFT - bsize_shift;
	unsigned int max_blocks = 2 * 1024 * 1024 >> bsize_shift;
	struct gfs2_journal_extent *je;
	int ret = 0;
	struct bio *bio = NULL;
	struct folio *folio = NULL;
	bool done = false;
	errseq_t since;

	memset(head, 0, sizeof(*head));
	if (list_empty(&jd->extent_list))
		gfs2_map_journal_extents(sdp, jd);

	since = filemap_sample_wb_err(mapping);
	list_for_each_entry(je, &jd->extent_list, list) {
		u64 dblock = je->dblock;

		for (; block < je->lblock + je->blocks; block++, dblock++) {
			if (!folio) {
				folio = filemap_grab_folio(mapping,
						block >> shift);
				if (IS_ERR(folio)) {
					ret = PTR_ERR(folio);
					done = true;
					goto out;
				}
				off = 0;
			}

			if (bio && (off || block < blocks_submitted + max_blocks)) {
				sector_t sector = dblock << sdp->sd_fsb2bb_shift;

				if (bio_end_sector(bio) == sector) {
					if (bio_add_folio(bio, folio, bsize, off))
						goto block_added;
				}
				if (off) {
					unsigned int blocks =
						(PAGE_SIZE - off) >> bsize_shift;

					bio = gfs2_chain_bio(bio, blocks);
					goto add_block_to_new_bio;
				}
			}

			if (bio) {
				blocks_submitted = block;
				submit_bio(bio);
			}

			bio = gfs2_log_alloc_bio(sdp, dblock, gfs2_end_log_read);
			bio->bi_opf = REQ_OP_READ;
add_block_to_new_bio:
			if (!bio_add_folio(bio, folio, bsize, off))
				BUG();
block_added:
			off += bsize;
			if (off == folio_size(folio))
				folio = NULL;
			if (blocks_submitted <= blocks_read + max_blocks) {
				/* Keep at least one bio in flight */
				continue;
			}

			gfs2_jhead_process_page(jd, blocks_read >> shift, head, &done);
			blocks_read += PAGE_SIZE >> bsize_shift;
			if (done)
				goto out;  /* found */
		}
	}

out:
	if (bio)
		submit_bio(bio);
	while (blocks_read < block) {
		gfs2_jhead_process_page(jd, blocks_read >> shift, head, &done);
		blocks_read += PAGE_SIZE >> bsize_shift;
	}

	if (!ret)
		ret = filemap_check_wb_err(mapping, since);

	truncate_inode_pages(mapping, 0);

	return ret;
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
	__be32 *ptr;

	clear_buffer_escaped(bh);
	ptr = kmap_local_folio(bh->b_folio, bh_offset(bh));
	if (*ptr == cpu_to_be32(GFS2_MAGIC))
		set_buffer_escaped(bh);
	kunmap_local(ptr);
}

static int blocknr_cmp(void *priv, const struct list_head *a,
		       const struct list_head *b)
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
				void *p;

				page = mempool_alloc(gfs2_page_pool, GFP_NOIO);
				p = page_address(page);
				memcpy_from_page(p, page, bh_offset(bd2->bd_bh), bd2->bd_bh->b_size);
				*(__be32 *)p = 0;
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

static void buf_lo_before_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	unsigned int limit = buf_limit(sdp); /* 503 for 4k blocks */
	unsigned int nbuf;
	if (tr == NULL)
		return;
	nbuf = tr->tr_num_buf_new - tr->tr_num_buf_rm;
	gfs2_before_commit(sdp, limit, nbuf, &tr->tr_buf, 0);
}

static void buf_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct list_head *head;
	struct gfs2_bufdata *bd;

	if (tr == NULL)
		return;

	head = &tr->tr_buf;
	while (!list_empty(head)) {
		bd = list_first_entry(head, struct gfs2_bufdata, bd_list);
		list_del_init(&bd->bd_list);
		gfs2_unpin(sdp, bd->bd_bh, tr);
	}
}

static void buf_lo_before_scan(struct gfs2_jdesc *jd,
			       struct gfs2_log_header_host *head, int pass)
{
	if (pass != 0)
		return;

	jd->jd_found_blocks = 0;
	jd->jd_replayed_blocks = 0;
}

#define obsolete_rgrp_replay \
"Replaying 0x%llx from jid=%d/0x%llx but we already have a bh!\n"
#define obsolete_rgrp_replay2 \
"busy:%d, pinned:%d rg_gen:0x%llx, j_gen:0x%llx\n"

static void obsolete_rgrp(struct gfs2_jdesc *jd, struct buffer_head *bh_log,
			  u64 blkno)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct gfs2_rgrpd *rgd;
	struct gfs2_rgrp *jrgd = (struct gfs2_rgrp *)bh_log->b_data;

	rgd = gfs2_blk2rgrpd(sdp, blkno, false);
	if (rgd && rgd->rd_addr == blkno &&
	    rgd->rd_bits && rgd->rd_bits->bi_bh) {
		fs_info(sdp, obsolete_rgrp_replay, (unsigned long long)blkno,
			jd->jd_jid, bh_log->b_blocknr);
		fs_info(sdp, obsolete_rgrp_replay2,
			buffer_busy(rgd->rd_bits->bi_bh) ? 1 : 0,
			buffer_pinned(rgd->rd_bits->bi_bh),
			rgd->rd_igeneration,
			be64_to_cpu(jrgd->rg_igeneration));
		gfs2_dump_glock(NULL, rgd->rd_gl, true);
	}
}

static int buf_lo_scan_elements(struct gfs2_jdesc *jd, u32 start,
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

	gfs2_replay_incr_blk(jd, &start);

	for (; blks; gfs2_replay_incr_blk(jd, &start), blks--) {
		blkno = be64_to_cpu(*ptr++);

		jd->jd_found_blocks++;

		if (gfs2_revoke_check(jd, blkno, start))
			continue;

		error = gfs2_replay_read_block(jd, start, &bh_log);
		if (error)
			return error;

		bh_ip = gfs2_meta_new(gl, blkno);
		memcpy(bh_ip->b_data, bh_log->b_data, bh_log->b_size);

		if (gfs2_meta_check(sdp, bh_ip))
			error = -EIO;
		else {
			struct gfs2_meta_header *mh =
				(struct gfs2_meta_header *)bh_ip->b_data;

			if (mh->mh_type == cpu_to_be32(GFS2_METATYPE_RG))
				obsolete_rgrp(jd, bh_log, blkno);

			mark_buffer_dirty(bh_ip);
		}
		brelse(bh_log);
		brelse(bh_ip);

		if (error)
			break;

		jd->jd_replayed_blocks++;
	}

	return error;
}

static void buf_lo_after_scan(struct gfs2_jdesc *jd, int error, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (error) {
		gfs2_inode_metasync(ip->i_gl);
		return;
	}
	if (pass != 1)
		return;

	gfs2_inode_metasync(ip->i_gl);

	fs_info(sdp, "jid=%u: Replayed %u of %u blocks\n",
	        jd->jd_jid, jd->jd_replayed_blocks, jd->jd_found_blocks);
}

static void revoke_lo_before_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct gfs2_meta_header *mh;
	unsigned int offset;
	struct list_head *head = &sdp->sd_log_revokes;
	struct gfs2_bufdata *bd;
	struct page *page;
	unsigned int length;

	gfs2_flush_revokes(sdp);
	if (!sdp->sd_log_num_revoke)
		return;

	length = gfs2_struct2blk(sdp, sdp->sd_log_num_revoke);
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

void gfs2_drain_revokes(struct gfs2_sbd *sdp)
{
	struct list_head *head = &sdp->sd_log_revokes;
	struct gfs2_bufdata *bd;
	struct gfs2_glock *gl;

	while (!list_empty(head)) {
		bd = list_first_entry(head, struct gfs2_bufdata, bd_list);
		list_del_init(&bd->bd_list);
		gl = bd->bd_gl;
		gfs2_glock_remove_revoke(gl);
		kmem_cache_free(gfs2_bufdata_cachep, bd);
	}
}

static void revoke_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	gfs2_drain_revokes(sdp);
}

static void revoke_lo_before_scan(struct gfs2_jdesc *jd,
				  struct gfs2_log_header_host *head, int pass)
{
	if (pass != 0)
		return;

	jd->jd_found_revokes = 0;
	jd->jd_replay_tail = head->lh_tail;
}

static int revoke_lo_scan_elements(struct gfs2_jdesc *jd, u32 start,
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

	for (; blks; gfs2_replay_incr_blk(jd, &start), blks--) {
		error = gfs2_replay_read_block(jd, start, &bh);
		if (error)
			return error;

		if (!first)
			gfs2_metatype_check(sdp, bh, GFS2_METATYPE_LB);

		while (offset + sizeof(u64) <= sdp->sd_sb.sb_bsize) {
			blkno = be64_to_cpu(*(__be64 *)(bh->b_data + offset));

			error = gfs2_revoke_add(jd, blkno, start);
			if (error < 0) {
				brelse(bh);
				return error;
			}
			else if (error)
				jd->jd_found_revokes++;

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
		gfs2_revoke_clean(jd);
		return;
	}
	if (pass != 1)
		return;

	fs_info(sdp, "jid=%u: Found %u revoke tags\n",
	        jd->jd_jid, jd->jd_found_revokes);

	gfs2_revoke_clean(jd);
}

/**
 * databuf_lo_before_commit - Scan the data buffers, writing as we go
 * @sdp: The filesystem
 * @tr: The system transaction being flushed
 */

static void databuf_lo_before_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	unsigned int limit = databuf_limit(sdp);
	unsigned int nbuf;
	if (tr == NULL)
		return;
	nbuf = tr->tr_num_databuf_new - tr->tr_num_databuf_rm;
	gfs2_before_commit(sdp, limit, nbuf, &tr->tr_databuf, 1);
}

static int databuf_lo_scan_elements(struct gfs2_jdesc *jd, u32 start,
				    struct gfs2_log_descriptor *ld,
				    __be64 *ptr, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_glock *gl = ip->i_gl;
	unsigned int blks = be32_to_cpu(ld->ld_data1);
	struct buffer_head *bh_log, *bh_ip;
	u64 blkno;
	u64 esc;
	int error = 0;

	if (pass != 1 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_JDATA)
		return 0;

	gfs2_replay_incr_blk(jd, &start);
	for (; blks; gfs2_replay_incr_blk(jd, &start), blks--) {
		blkno = be64_to_cpu(*ptr++);
		esc = be64_to_cpu(*ptr++);

		jd->jd_found_blocks++;

		if (gfs2_revoke_check(jd, blkno, start))
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

		jd->jd_replayed_blocks++;
	}

	return error;
}

/* FIXME: sort out accounting for log blocks etc. */

static void databuf_lo_after_scan(struct gfs2_jdesc *jd, int error, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (error) {
		gfs2_inode_metasync(ip->i_gl);
		return;
	}
	if (pass != 1)
		return;

	/* data sync? */
	gfs2_inode_metasync(ip->i_gl);

	fs_info(sdp, "jid=%u: Replayed %u of %u data blocks\n",
		jd->jd_jid, jd->jd_replayed_blocks, jd->jd_found_blocks);
}

static void databuf_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct list_head *head;
	struct gfs2_bufdata *bd;

	if (tr == NULL)
		return;

	head = &tr->tr_databuf;
	while (!list_empty(head)) {
		bd = list_first_entry(head, struct gfs2_bufdata, bd_list);
		list_del_init(&bd->bd_list);
		gfs2_unpin(sdp, bd->bd_bh, tr);
	}
}


static const struct gfs2_log_operations gfs2_buf_lops = {
	.lo_before_commit = buf_lo_before_commit,
	.lo_after_commit = buf_lo_after_commit,
	.lo_before_scan = buf_lo_before_scan,
	.lo_scan_elements = buf_lo_scan_elements,
	.lo_after_scan = buf_lo_after_scan,
	.lo_name = "buf",
};

static const struct gfs2_log_operations gfs2_revoke_lops = {
	.lo_before_commit = revoke_lo_before_commit,
	.lo_after_commit = revoke_lo_after_commit,
	.lo_before_scan = revoke_lo_before_scan,
	.lo_scan_elements = revoke_lo_scan_elements,
	.lo_after_scan = revoke_lo_after_scan,
	.lo_name = "revoke",
};

static const struct gfs2_log_operations gfs2_databuf_lops = {
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

