/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
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
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/swap.h>
#include <linux/delay.h>
#include <linux/bio.h>
#include <linux/gfs2_ondisk.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"
#include "trace_gfs2.h"

static int gfs2_aspace_writepage(struct page *page, struct writeback_control *wbc)
{
	struct buffer_head *bh, *head;
	int nr_underway = 0;
	int write_flags = REQ_META | REQ_PRIO | wbc_to_write_flags(wbc);

	BUG_ON(!PageLocked(page));
	BUG_ON(!page_has_buffers(page));

	head = page_buffers(page);
	bh = head;

	do {
		if (!buffer_mapped(bh))
			continue;
		/*
		 * If it's a fully non-blocking write attempt and we cannot
		 * lock the buffer then redirty the page.  Note that this can
		 * potentially cause a busy-wait loop from flusher thread and kswapd
		 * activity, but those code paths have their own higher-level
		 * throttling.
		 */
		if (wbc->sync_mode != WB_SYNC_NONE) {
			lock_buffer(bh);
		} else if (!trylock_buffer(bh)) {
			redirty_page_for_writepage(wbc, page);
			continue;
		}
		if (test_clear_buffer_dirty(bh)) {
			mark_buffer_async_write(bh);
		} else {
			unlock_buffer(bh);
		}
	} while ((bh = bh->b_this_page) != head);

	/*
	 * The page and its buffers are protected by PageWriteback(), so we can
	 * drop the bh refcounts early.
	 */
	BUG_ON(PageWriteback(page));
	set_page_writeback(page);

	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			submit_bh(REQ_OP_WRITE, write_flags, bh);
			nr_underway++;
		}
		bh = next;
	} while (bh != head);
	unlock_page(page);

	if (nr_underway == 0)
		end_page_writeback(page);

	return 0;
}

const struct address_space_operations gfs2_meta_aops = {
	.writepage = gfs2_aspace_writepage,
	.releasepage = gfs2_releasepage,
};

const struct address_space_operations gfs2_rgrp_aops = {
	.writepage = gfs2_aspace_writepage,
	.releasepage = gfs2_releasepage,
};

/**
 * gfs2_getbuf - Get a buffer with a given address space
 * @gl: the glock
 * @blkno: the block number (filesystem scope)
 * @create: 1 if the buffer should be created
 *
 * Returns: the buffer
 */

struct buffer_head *gfs2_getbuf(struct gfs2_glock *gl, u64 blkno, int create)
{
	struct address_space *mapping = gfs2_glock2aspace(gl);
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct page *page;
	struct buffer_head *bh;
	unsigned int shift;
	unsigned long index;
	unsigned int bufnum;

	if (mapping == NULL)
		mapping = &sdp->sd_aspace;

	shift = PAGE_SHIFT - sdp->sd_sb.sb_bsize_shift;
	index = blkno >> shift;             /* convert block to page */
	bufnum = blkno - (index << shift);  /* block buf index within page */

	if (create) {
		for (;;) {
			page = grab_cache_page(mapping, index);
			if (page)
				break;
			yield();
		}
	} else {
		page = find_get_page_flags(mapping, index,
						FGP_LOCK|FGP_ACCESSED);
		if (!page)
			return NULL;
	}

	if (!page_has_buffers(page))
		create_empty_buffers(page, sdp->sd_sb.sb_bsize, 0);

	/* Locate header for our buffer within our page */
	for (bh = page_buffers(page); bufnum--; bh = bh->b_this_page)
		/* Do nothing */;
	get_bh(bh);

	if (!buffer_mapped(bh))
		map_bh(bh, sdp->sd_vfs, blkno);

	unlock_page(page);
	put_page(page);

	return bh;
}

static void meta_prep_new(struct buffer_head *bh)
{
	struct gfs2_meta_header *mh = (struct gfs2_meta_header *)bh->b_data;

	lock_buffer(bh);
	clear_buffer_dirty(bh);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);

	mh->mh_magic = cpu_to_be32(GFS2_MAGIC);
}

/**
 * gfs2_meta_new - Get a block
 * @gl: The glock associated with this block
 * @blkno: The block number
 *
 * Returns: The buffer
 */

struct buffer_head *gfs2_meta_new(struct gfs2_glock *gl, u64 blkno)
{
	struct buffer_head *bh;
	bh = gfs2_getbuf(gl, blkno, CREATE);
	meta_prep_new(bh);
	return bh;
}

static void gfs2_meta_read_endio(struct bio *bio)
{
	struct bio_vec *bvec;
	int i;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, i, iter_all) {
		struct page *page = bvec->bv_page;
		struct buffer_head *bh = page_buffers(page);
		unsigned int len = bvec->bv_len;

		while (bh_offset(bh) < bvec->bv_offset)
			bh = bh->b_this_page;
		do {
			struct buffer_head *next = bh->b_this_page;
			len -= bh->b_size;
			bh->b_end_io(bh, !bio->bi_status);
			bh = next;
		} while (bh && len);
	}
	bio_put(bio);
}

/*
 * Submit several consecutive buffer head I/O requests as a single bio I/O
 * request.  (See submit_bh_wbc.)
 */
static void gfs2_submit_bhs(int op, int op_flags, struct buffer_head *bhs[],
			    int num)
{
	while (num > 0) {
		struct buffer_head *bh = *bhs;
		struct bio *bio;

		bio = bio_alloc(GFP_NOIO, num);
		bio->bi_iter.bi_sector = bh->b_blocknr * (bh->b_size >> 9);
		bio_set_dev(bio, bh->b_bdev);
		while (num > 0) {
			bh = *bhs;
			if (!bio_add_page(bio, bh->b_page, bh->b_size, bh_offset(bh))) {
				BUG_ON(bio->bi_iter.bi_size == 0);
				break;
			}
			bhs++;
			num--;
		}
		bio->bi_end_io = gfs2_meta_read_endio;
		bio_set_op_attrs(bio, op, op_flags);
		submit_bio(bio);
	}
}

/**
 * gfs2_meta_read - Read a block from disk
 * @gl: The glock covering the block
 * @blkno: The block number
 * @flags: flags
 * @bhp: the place where the buffer is returned (NULL on failure)
 *
 * Returns: errno
 */

int gfs2_meta_read(struct gfs2_glock *gl, u64 blkno, int flags,
		   int rahead, struct buffer_head **bhp)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct buffer_head *bh, *bhs[2];
	int num = 0;

	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags))) {
		*bhp = NULL;
		return -EIO;
	}

	*bhp = bh = gfs2_getbuf(gl, blkno, CREATE);

	lock_buffer(bh);
	if (buffer_uptodate(bh)) {
		unlock_buffer(bh);
		flags &= ~DIO_WAIT;
	} else {
		bh->b_end_io = end_buffer_read_sync;
		get_bh(bh);
		bhs[num++] = bh;
	}

	if (rahead) {
		bh = gfs2_getbuf(gl, blkno + 1, CREATE);

		lock_buffer(bh);
		if (buffer_uptodate(bh)) {
			unlock_buffer(bh);
			brelse(bh);
		} else {
			bh->b_end_io = end_buffer_read_sync;
			bhs[num++] = bh;
		}
	}

	gfs2_submit_bhs(REQ_OP_READ, REQ_META | REQ_PRIO, bhs, num);
	if (!(flags & DIO_WAIT))
		return 0;

	bh = *bhp;
	wait_on_buffer(bh);
	if (unlikely(!buffer_uptodate(bh))) {
		struct gfs2_trans *tr = current->journal_info;
		if (tr && test_bit(TR_TOUCHED, &tr->tr_flags))
			gfs2_io_error_bh_wd(sdp, bh);
		brelse(bh);
		*bhp = NULL;
		return -EIO;
	}

	return 0;
}

/**
 * gfs2_meta_wait - Reread a block from disk
 * @sdp: the filesystem
 * @bh: The block to wait for
 *
 * Returns: errno
 */

int gfs2_meta_wait(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	wait_on_buffer(bh);

	if (!buffer_uptodate(bh)) {
		struct gfs2_trans *tr = current->journal_info;
		if (tr && test_bit(TR_TOUCHED, &tr->tr_flags))
			gfs2_io_error_bh_wd(sdp, bh);
		return -EIO;
	}
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	return 0;
}

void gfs2_remove_from_journal(struct buffer_head *bh, int meta)
{
	struct address_space *mapping = bh->b_page->mapping;
	struct gfs2_sbd *sdp = gfs2_mapping2sbd(mapping);
	struct gfs2_bufdata *bd = bh->b_private;
	struct gfs2_trans *tr = current->journal_info;
	int was_pinned = 0;

	if (test_clear_buffer_pinned(bh)) {
		trace_gfs2_pin(bd, 0);
		atomic_dec(&sdp->sd_log_pinned);
		list_del_init(&bd->bd_list);
		if (meta == REMOVE_META)
			tr->tr_num_buf_rm++;
		else
			tr->tr_num_databuf_rm++;
		set_bit(TR_TOUCHED, &tr->tr_flags);
		was_pinned = 1;
		brelse(bh);
	}
	if (bd) {
		spin_lock(&sdp->sd_ail_lock);
		if (bd->bd_tr) {
			gfs2_trans_add_revoke(sdp, bd);
		} else if (was_pinned) {
			bh->b_private = NULL;
			kmem_cache_free(gfs2_bufdata_cachep, bd);
		}
		spin_unlock(&sdp->sd_ail_lock);
	}
	clear_buffer_dirty(bh);
	clear_buffer_uptodate(bh);
}

/**
 * gfs2_meta_wipe - make inode's buffers so they aren't dirty/pinned anymore
 * @ip: the inode who owns the buffers
 * @bstart: the first buffer in the run
 * @blen: the number of buffers in the run
 *
 */

void gfs2_meta_wipe(struct gfs2_inode *ip, u64 bstart, u32 blen)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *bh;

	while (blen) {
		bh = gfs2_getbuf(ip->i_gl, bstart, NO_CREATE);
		if (bh) {
			lock_buffer(bh);
			gfs2_log_lock(sdp);
			gfs2_remove_from_journal(bh, REMOVE_META);
			gfs2_log_unlock(sdp);
			unlock_buffer(bh);
			brelse(bh);
		}

		bstart++;
		blen--;
	}
}

/**
 * gfs2_meta_indirect_buffer - Get a metadata buffer
 * @ip: The GFS2 inode
 * @height: The level of this buf in the metadata (indir addr) tree (if any)
 * @num: The block number (device relative) of the buffer
 * @bhp: the buffer is returned here
 *
 * Returns: errno
 */

int gfs2_meta_indirect_buffer(struct gfs2_inode *ip, int height, u64 num,
			      struct buffer_head **bhp)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_glock *gl = ip->i_gl;
	struct buffer_head *bh;
	int ret = 0;
	u32 mtype = height ? GFS2_METATYPE_IN : GFS2_METATYPE_DI;
	int rahead = 0;

	if (num == ip->i_no_addr)
		rahead = ip->i_rahead;

	ret = gfs2_meta_read(gl, num, DIO_WAIT, rahead, &bh);
	if (ret == 0 && gfs2_metatype_check(sdp, bh, mtype)) {
		brelse(bh);
		ret = -EIO;
	} else {
		*bhp = bh;
	}
	return ret;
}

/**
 * gfs2_meta_ra - start readahead on an extent of a file
 * @gl: the glock the blocks belong to
 * @dblock: the starting disk block
 * @extlen: the number of blocks in the extent
 *
 * returns: the first buffer in the extent
 */

struct buffer_head *gfs2_meta_ra(struct gfs2_glock *gl, u64 dblock, u32 extlen)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct buffer_head *first_bh, *bh;
	u32 max_ra = gfs2_tune_get(sdp, gt_max_readahead) >>
			  sdp->sd_sb.sb_bsize_shift;

	BUG_ON(!extlen);

	if (max_ra < 1)
		max_ra = 1;
	if (extlen > max_ra)
		extlen = max_ra;

	first_bh = gfs2_getbuf(gl, dblock, CREATE);

	if (buffer_uptodate(first_bh))
		goto out;
	if (!buffer_locked(first_bh))
		ll_rw_block(REQ_OP_READ, REQ_META | REQ_PRIO, 1, &first_bh);

	dblock++;
	extlen--;

	while (extlen) {
		bh = gfs2_getbuf(gl, dblock, CREATE);

		if (!buffer_uptodate(bh) && !buffer_locked(bh))
			ll_rw_block(REQ_OP_READ,
				    REQ_RAHEAD | REQ_META | REQ_PRIO,
				    1, &bh);
		brelse(bh);
		dblock++;
		extlen--;
		if (!buffer_locked(first_bh) && buffer_uptodate(first_bh))
			goto out;
	}

	wait_on_buffer(first_bh);
out:
	return first_bh;
}

