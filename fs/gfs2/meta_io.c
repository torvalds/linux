// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
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

static void gfs2_aspace_write_folio(struct folio *folio,
		struct writeback_control *wbc)
{
	struct buffer_head *bh, *head;
	int nr_underway = 0;
	blk_opf_t write_flags = REQ_META | REQ_PRIO | wbc_to_write_flags(wbc);

	BUG_ON(!folio_test_locked(folio));

	head = folio_buffers(folio);
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
			folio_redirty_for_writepage(wbc, folio);
			continue;
		}
		if (test_clear_buffer_dirty(bh)) {
			mark_buffer_async_write(bh);
		} else {
			unlock_buffer(bh);
		}
	} while ((bh = bh->b_this_page) != head);

	/*
	 * The folio and its buffers are protected from truncation by
	 * the writeback flag, so we can drop the bh refcounts early.
	 */
	BUG_ON(folio_test_writeback(folio));
	folio_start_writeback(folio);

	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			submit_bh(REQ_OP_WRITE | write_flags, bh);
			nr_underway++;
		}
		bh = next;
	} while (bh != head);
	folio_unlock(folio);

	if (nr_underway == 0)
		folio_end_writeback(folio);
}

static int gfs2_aspace_writepages(struct address_space *mapping,
		struct writeback_control *wbc)
{
	struct folio *folio = NULL;
	int error;

	while ((folio = writeback_iter(mapping, wbc, folio, &error)))
		gfs2_aspace_write_folio(folio, wbc);

	return error;
}

const struct address_space_operations gfs2_meta_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.writepages = gfs2_aspace_writepages,
	.release_folio = gfs2_release_folio,
	.migrate_folio = buffer_migrate_folio_norefs,
};

const struct address_space_operations gfs2_rgrp_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.writepages = gfs2_aspace_writepages,
	.release_folio = gfs2_release_folio,
	.migrate_folio = buffer_migrate_folio_norefs,
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
	struct folio *folio;
	struct buffer_head *bh;
	unsigned int shift;
	unsigned long index;
	unsigned int bufnum;

	if (mapping == NULL)
		mapping = gfs2_aspace(sdp);

	shift = PAGE_SHIFT - sdp->sd_sb.sb_bsize_shift;
	index = blkno >> shift;             /* convert block to page */
	bufnum = blkno - (index << shift);  /* block buf index within page */

	if (create) {
		folio = __filemap_get_folio(mapping, index,
				FGP_LOCK | FGP_ACCESSED | FGP_CREAT,
				mapping_gfp_mask(mapping) | __GFP_NOFAIL);
		bh = folio_buffers(folio);
		if (!bh)
			bh = create_empty_buffers(folio,
				sdp->sd_sb.sb_bsize, 0);
	} else {
		folio = __filemap_get_folio(mapping, index,
				FGP_LOCK | FGP_ACCESSED, 0);
		if (IS_ERR(folio))
			return NULL;
		bh = folio_buffers(folio);
	}

	if (!bh)
		goto out_unlock;

	bh = get_nth_bh(bh, bufnum);
	if (!buffer_mapped(bh))
		map_bh(bh, sdp->sd_vfs, blkno);

out_unlock:
	folio_unlock(folio);
	folio_put(folio);

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
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio) {
		struct folio *folio = fi.folio;
		struct buffer_head *bh = folio_buffers(folio);
		size_t len = fi.length;

		while (bh_offset(bh) < fi.offset)
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
static void gfs2_submit_bhs(blk_opf_t opf, struct buffer_head *bhs[], int num)
{
	while (num > 0) {
		struct buffer_head *bh = *bhs;
		struct bio *bio;

		bio = bio_alloc(bh->b_bdev, num, opf, GFP_NOIO);
		bio->bi_iter.bi_sector = bh->b_blocknr * (bh->b_size >> SECTOR_SHIFT);
		while (num > 0) {
			bh = *bhs;
			if (!bio_add_folio(bio, bh->b_folio, bh->b_size, bh_offset(bh))) {
				BUG_ON(bio->bi_iter.bi_size == 0);
				break;
			}
			bhs++;
			num--;
		}
		bio->bi_end_io = gfs2_meta_read_endio;
		submit_bio(bio);
	}
}

/**
 * gfs2_meta_read - Read a block from disk
 * @gl: The glock covering the block
 * @blkno: The block number
 * @flags: flags
 * @rahead: Do read-ahead
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

	if (gfs2_withdrawing_or_withdrawn(sdp) &&
	    !gfs2_withdraw_in_prog(sdp)) {
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

	gfs2_submit_bhs(REQ_OP_READ | REQ_META | REQ_PRIO, bhs, num);
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
	if (gfs2_withdrawing_or_withdrawn(sdp) &&
	    !gfs2_withdraw_in_prog(sdp))
		return -EIO;

	wait_on_buffer(bh);

	if (!buffer_uptodate(bh)) {
		struct gfs2_trans *tr = current->journal_info;
		if (tr && test_bit(TR_TOUCHED, &tr->tr_flags))
			gfs2_io_error_bh_wd(sdp, bh);
		return -EIO;
	}
	if (gfs2_withdrawing_or_withdrawn(sdp) &&
	    !gfs2_withdraw_in_prog(sdp))
		return -EIO;

	return 0;
}

void gfs2_remove_from_journal(struct buffer_head *bh, int meta)
{
	struct address_space *mapping = bh->b_folio->mapping;
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
		if (bd->bd_tr) {
			gfs2_trans_add_revoke(sdp, bd);
		} else if (was_pinned) {
			bh->b_private = NULL;
			kmem_cache_free(gfs2_bufdata_cachep, bd);
		} else if (!list_empty(&bd->bd_ail_st_list) &&
					!list_empty(&bd->bd_ail_gl_list)) {
			gfs2_remove_from_ail(bd);
		}
	}
	clear_buffer_dirty(bh);
	clear_buffer_uptodate(bh);
}

/**
 * gfs2_ail1_wipe - remove deleted/freed buffers from the ail1 list
 * @sdp: superblock
 * @bstart: starting block address of buffers to remove
 * @blen: length of buffers to be removed
 *
 * This function is called from gfs2_journal wipe, whose job is to remove
 * buffers, corresponding to deleted blocks, from the journal. If we find any
 * bufdata elements on the system ail1 list, they haven't been written to
 * the journal yet. So we remove them.
 */
static void gfs2_ail1_wipe(struct gfs2_sbd *sdp, u64 bstart, u32 blen)
{
	struct gfs2_trans *tr, *s;
	struct gfs2_bufdata *bd, *bs;
	struct buffer_head *bh;
	u64 end = bstart + blen;

	gfs2_log_lock(sdp);
	spin_lock(&sdp->sd_ail_lock);
	list_for_each_entry_safe(tr, s, &sdp->sd_ail1_list, tr_list) {
		list_for_each_entry_safe(bd, bs, &tr->tr_ail1_list,
					 bd_ail_st_list) {
			bh = bd->bd_bh;
			if (bh->b_blocknr < bstart || bh->b_blocknr >= end)
				continue;

			gfs2_remove_from_journal(bh, REMOVE_JDATA);
		}
	}
	spin_unlock(&sdp->sd_ail_lock);
	gfs2_log_unlock(sdp);
}

static struct buffer_head *gfs2_getjdatabuf(struct gfs2_inode *ip, u64 blkno)
{
	struct address_space *mapping = ip->i_inode.i_mapping;
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct folio *folio;
	struct buffer_head *bh;
	unsigned int shift = PAGE_SHIFT - sdp->sd_sb.sb_bsize_shift;
	unsigned long index = blkno >> shift; /* convert block to page */
	unsigned int bufnum = blkno - (index << shift);

	folio = __filemap_get_folio(mapping, index, FGP_LOCK | FGP_ACCESSED, 0);
	if (IS_ERR(folio))
		return NULL;
	bh = folio_buffers(folio);
	if (bh)
		bh = get_nth_bh(bh, bufnum);
	folio_unlock(folio);
	folio_put(folio);
	return bh;
}

/**
 * gfs2_journal_wipe - make inode's buffers so they aren't dirty/pinned anymore
 * @ip: the inode who owns the buffers
 * @bstart: the first buffer in the run
 * @blen: the number of buffers in the run
 *
 */

void gfs2_journal_wipe(struct gfs2_inode *ip, u64 bstart, u32 blen)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *bh;
	int ty;

	/* This can only happen during incomplete inode creation. */
	if (!ip->i_gl)
		return;

	gfs2_ail1_wipe(sdp, bstart, blen);
	while (blen) {
		ty = REMOVE_META;
		bh = gfs2_getbuf(ip->i_gl, bstart, NO_CREATE);
		if (!bh && gfs2_is_jdata(ip)) {
			bh = gfs2_getjdatabuf(ip, bstart);
			ty = REMOVE_JDATA;
		}
		if (bh) {
			lock_buffer(bh);
			gfs2_log_lock(sdp);
			spin_lock(&sdp->sd_ail_lock);
			gfs2_remove_from_journal(bh, ty);
			spin_unlock(&sdp->sd_ail_lock);
			gfs2_log_unlock(sdp);
			unlock_buffer(bh);
			brelse(bh);
		}

		bstart++;
		blen--;
	}
}

/**
 * gfs2_meta_buffer - Get a metadata buffer
 * @ip: The GFS2 inode
 * @mtype: The block type (GFS2_METATYPE_*)
 * @num: The block number (device relative) of the buffer
 * @bhp: the buffer is returned here
 *
 * Returns: errno
 */

int gfs2_meta_buffer(struct gfs2_inode *ip, u32 mtype, u64 num,
		     struct buffer_head **bhp)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_glock *gl = ip->i_gl;
	struct buffer_head *bh;
	int ret = 0;
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
	bh_read_nowait(first_bh, REQ_META | REQ_PRIO);

	dblock++;
	extlen--;

	while (extlen) {
		bh = gfs2_getbuf(gl, dblock, CREATE);

		bh_readahead(bh, REQ_RAHEAD | REQ_META | REQ_PRIO);
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

